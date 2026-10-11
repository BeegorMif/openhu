#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtTest>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginStore.hpp>
#include "TestHome.hpp"

using f1x::openauto::autoapp::plugins::PluginManager;
using f1x::openauto::autoapp::plugins::PluginStore;

namespace
{

void writeJson(const QString& path, const QVariantMap& data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject::fromVariantMap(data)).toJson());
}

QVariantMap readJson(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object().toVariantMap() : QVariantMap();
}

// Local plugin repository (source type "file") like tools/openhu-repo-index generates it
class Repository
{
public:
    explicit Repository(const QString& path)
        : path_(path)
    {
        QDir().mkpath(path_);
    }

    QString path() const { return path_; }

    // Packs a plugin and adds it to the index. manifestId differs from id to test broken archives,
    // nested = false puts manifest.json at the top of the archive instead of <id>/manifest.json.
    QVariantMap add(const QString& id, const QString& version, const QStringList& arch = {"all"},
                    QString manifestId = QString(), bool nested = true, int api = PluginManager::cApiVersion)
    {
        if(manifestId.isEmpty())
        {
            manifestId = id;
        }
        const QString staging = path_ + "/staging-" + id + "-" + version;
        const QString pluginDir = nested ? staging + "/" + id : staging;
        QDir().mkpath(pluginDir);
        writeJson(pluginDir + "/manifest.json", {{"id", manifestId}, {"name", id}, {"version", version}, {"api", api}});

        const QString archive = id + "-" + version + ".tar.gz";
        QProcess tar;
        tar.start("tar", {"-czf", path_ + "/" + archive, "-C", staging, "."});
        tar.waitForFinished();
        QDir(staging).removeRecursively();

        QFile file(path_ + "/" + archive);
        file.open(QIODevice::ReadOnly);
        const QString sha256 = QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());

        QVariantMap entry{{"id", id}, {"name", id}, {"version", version}, {"arch", arch},
                          {"archive", archive}, {"sha256", sha256}, {"api", api}};
        for(int i = entries_.size() - 1; i >= 0; --i)
        {
            if(entries_[i].toMap().value("id") == id)
            {
                entries_.removeAt(i);
            }
        }
        entries_.append(entry);
        this->writeIndex();
        return entry;
    }

    void setSha256(const QString& id, const QString& sha256)
    {
        for(auto& entry: entries_)
        {
            QVariantMap map = entry.toMap();
            if(map.value("id") == id)
            {
                map["sha256"] = sha256;
                entry = map;
            }
        }
        this->writeIndex();
    }

private:
    void writeIndex() { writeJson(path_ + "/index.json", {{"plugins", entries_}}); }

    QString path_;
    QVariantList entries_;
};

void writeSources(const QStringList& lines)
{
    QDir().mkpath(PluginManager::baseDir());
    QFile file(PluginManager::baseDir() + "/sources.list");
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(("# test sources\n" + lines.join('\n') + "\n").toUtf8());
}

QVariantMap package(const PluginStore& store, const QString& id)
{
    for(const auto& entry: store.packages())
    {
        if(entry.toMap().value("id") == id)
        {
            return entry.toMap();
        }
    }
    return QVariantMap();
}

bool waitIdle(const PluginStore& store)
{
    return QTest::qWaitFor([&store]() { return !store.busy(); }, 15000);
}

}

class TestPluginStore: public QObject
{
    Q_OBJECT

private slots:
    void compareVersions_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("expected");
        QTest::newRow("equal") << "1.2.0" << "1.2.0" << 0;
        QTest::newRow("missing parts are 0") << "1.2" << "1.2.0" << 0;
        QTest::newRow("numeric, not text") << "1.10.0" << "1.9.0" << 1;
        QTest::newRow("older") << "0.9" << "1.0" << -1;
    }

    void compareVersions()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, expected);
        QCOMPARE(PluginStore::compareVersions(a, b), expected);
    }

    void defaultSourceIsTheOfficialRepository()
    {
        TestHome home;
        PluginManager manager(0);
        PluginStore store(manager);  // no refresh: nothing is downloaded

        QCOMPARE(store.sources().size(), 1);
        const QVariantMap source = store.sources().first().toMap();
        QCOMPARE(source.value("line").toString(), QString("github average-web-dev/openhu-plugins main"));
        QVERIFY(source.value("official").toBool());
    }

    void installUpdateRemove()
    {
        TestHome home;
        Repository repo(home.path("repo"));
        repo.add("hello", "1.0.0");
        writeSources({"file " + repo.path()});

        PluginManager manager(0);
        PluginStore store(manager);
        store.refresh();
        QVERIFY(waitIdle(store));

        QVariantMap hello = package(store, "hello");
        QCOMPARE(hello.value("version").toString(), QString("1.0.0"));
        QVERIFY(hello.value("compatible").toBool());
        QVERIFY(!hello.value("installed").toBool());
        QVERIFY(!hello.value("official").toBool());

        store.install("hello");
        QVERIFY(waitIdle(store));
        QVERIFY2(QFile::exists(PluginManager::pluginsDir() + "/hello/manifest.json"), qPrintable(store.status()));
        QVERIFY(manager.contains("hello"));
        QCOMPARE(readJson(PluginManager::baseDir() + "/installed.json").value("hello").toMap().value("version").toString(),
                 QString("1.0.0"));
        QVERIFY(!package(store, "hello").value("updateAvailable").toBool());
        QVERIFY(!QDir(PluginManager::baseDir() + "/tmp/unpack-hello").exists());

        // new version in the repository
        repo.add("hello", "1.1.0");
        store.refresh();
        QVERIFY(waitIdle(store));
        QVERIFY(package(store, "hello").value("updateAvailable").toBool());

        store.updateAll();
        QVERIFY(waitIdle(store));
        QCOMPARE(manager.plugin("hello").value("version").toString(), QString("1.1.0"));
        QVERIFY(!package(store, "hello").value("updateAvailable").toBool());

        store.remove("hello");
        QVERIFY(waitIdle(store));
        QVERIFY(!QDir(PluginManager::pluginsDir() + "/hello").exists());
        QVERIFY(!manager.contains("hello"));
        QVERIFY(!readJson(PluginManager::baseDir() + "/installed.json").contains("hello"));
    }

    void manifestAtTheTopOfTheArchive()
    {
        TestHome home;
        Repository repo(home.path("repo"));
        repo.add("flat", "1.0.0", {"all"}, QString(), false);
        writeSources({"file " + repo.path()});

        PluginManager manager(0);
        PluginStore store(manager);
        store.refresh();
        QVERIFY(waitIdle(store));
        store.install("flat");
        QVERIFY(waitIdle(store));
        QVERIFY2(QFile::exists(PluginManager::pluginsDir() + "/flat/manifest.json"), qPrintable(store.status()));
    }

    void refusedInstalls()
    {
        TestHome home;
        Repository repo(home.path("repo"));
        repo.add("tampered", "1.0.0");
        repo.setSha256("tampered", QString(64, 'a'));
        repo.add("wrongid", "1.0.0", {"all"}, "other");
        repo.add("foreign", "1.0.0", {"mips"});
        writeSources({"file " + repo.path()});

        PluginManager manager(0);
        PluginStore store(manager);
        store.refresh();
        QVERIFY(waitIdle(store));
        QVERIFY(!package(store, "foreign").value("compatible").toBool());

        store.install("tampered");
        QVERIFY(waitIdle(store));
        QVERIFY2(store.status().contains("Prüfsumme"), qPrintable(store.status()));

        store.install("wrongid");
        QVERIFY(waitIdle(store));
        QVERIFY2(store.status().contains("manifest.json"), qPrintable(store.status()));

        store.install("foreign");
        QVERIFY(waitIdle(store));
        QVERIFY2(store.status().contains("nicht für"), qPrintable(store.status()));

        store.install("missing");
        QVERIFY(waitIdle(store));
        QVERIFY2(store.status().contains("keiner Quelle"), qPrintable(store.status()));

        // path tricks in the id never reach the file system
        store.remove("../repo");
        QVERIFY(waitIdle(store));
        QVERIFY(QFile::exists(repo.path() + "/index.json"));

        QVERIFY(manager.plugins().isEmpty());
        QVERIFY(QDir(PluginManager::pluginsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }

    void otherPluginApi()
    {
        TestHome home;
        Repository repo(home.path("repo"));
        repo.add("legacy", "1.0.0", {"all"}, QString(), true, 1);
        writeSources({"file " + repo.path()});

        PluginManager manager(0);
        PluginStore store(manager);
        store.refresh();
        QVERIFY(waitIdle(store));
        QVERIFY(!package(store, "legacy").value("compatible").toBool());

        store.install("legacy");
        QVERIFY(waitIdle(store));
        QVERIFY2(store.status().contains("andere OpenHU-Version"), qPrintable(store.status()));
        QVERIFY(!QDir(PluginManager::pluginsDir() + "/legacy").exists());

        // copied by hand (or installed before an OpenHU update): listed, but its QML is not loaded
        const QString dir = PluginManager::pluginsDir() + "/handmade";
        QDir().mkpath(dir);
        writeJson(dir + "/manifest.json", {{"id", "handmade"}, {"name", "Handmade"}, {"version", "1.0.0"},
                                           {"ui", QVariantMap{{"page", "Main.qml"}}}});
        manager.load();
        const QVariantMap handmade = manager.plugin("handmade");
        QVERIFY(manager.contains("handmade"));
        QVERIFY(!handmade.value("compatible").toBool());
        QVERIFY(handmade.value("page").toString().isEmpty());
        QVERIFY(!package(store, "handmade").value("compatible").toBool());
    }

    void firstSourceWins()
    {
        TestHome home;
        Repository first(home.path("first"));
        Repository second(home.path("second"));
        first.add("shared", "1.0.0");
        second.add("shared", "2.0.0");
        second.add("only-second", "1.0.0");
        writeSources({"file " + first.path(), "file " + second.path()});

        PluginManager manager(0);
        PluginStore store(manager);
        store.refresh();
        QVERIFY(waitIdle(store));

        QCOMPARE(package(store, "shared").value("version").toString(), QString("1.0.0"));
        QCOMPARE(package(store, "shared").value("source").toString(), QString("file " + first.path()));
        QCOMPARE(package(store, "only-second").value("version").toString(), QString("1.0.0"));

        // the index is cached: a new store knows the packages without downloading
        PluginStore cached(manager);
        QCOMPARE(package(cached, "only-second").value("version").toString(), QString("1.0.0"));
    }

    void editSources()
    {
        TestHome home;
        Repository repo(home.path("repo"));
        repo.add("hello", "1.0.0");
        writeSources({"file " + repo.path()});

        PluginManager manager(0);
        PluginStore store(manager);

        QVERIFY(!store.addSource("ftp", "example.com").isEmpty());
        QVERIFY(!store.addSource("url", "").isEmpty());
        QVERIFY(!store.addSource("github", "only-a-user").isEmpty());
        QVERIFY(!store.addSource("file", repo.path()).isEmpty());  // already there
        QVERIFY(!store.removeSource(5).isEmpty());

        Repository other(home.path("other"));
        other.add("extra", "1.0.0");
        QCOMPARE(store.addSource("file", other.path()), QString());
        QVERIFY(waitIdle(store));
        QCOMPARE(store.sources().size(), 2);
        QCOMPARE(package(store, "extra").value("version").toString(), QString("1.0.0"));

        // saved for the next start, comments stay
        QFile file(PluginManager::baseDir() + "/sources.list");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY(content.startsWith('#'));
        QVERIFY(content.contains("file " + other.path()));

        QCOMPARE(store.removeSource(0), QString());
        QCOMPARE(store.sources().size(), 1);
        QVERIFY(package(store, "hello").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPluginStore)
#include "test_pluginstore.moc"
