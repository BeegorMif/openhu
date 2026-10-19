// Plugin dependencies (#104): "requires" with a minimum version, "recommends"; start order, why a plugin does not
// start, enabling and disabling along, and the Android Auto lock of a required plugin (#123).
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include "TestHome.hpp"

using f1x::openauto::autoapp::core::EventBus;
using f1x::openauto::autoapp::plugins::PluginManager;

namespace
{

// a plugin whose process writes its start to a shared file and keeps running. The file shows that the processes
// ran, not their order: they race to write, so the start order comes from PluginManager::processStarted.
void writePlugin(const QString& id, const QString& version, const QVariantMap& extra = QVariantMap())
{
    const QString dir = PluginManager::pluginsDir() + "/" + id;
    QDir().mkpath(dir + "/bin");
    QVariantMap manifest{{"id", id}, {"name", id}, {"version", version}, {"api", PluginManager::cApiVersion},
                         {"process", QVariantMap{{"exec", "bin/run.sh"}}}};
    for(auto it = extra.cbegin(); it != extra.cend(); ++it)
    {
        manifest.insert(it.key(), it.value());
    }
    QFile script(dir + "/bin/run.sh");
    script.open(QIODevice::WriteOnly);
    script.write("echo \"$OPENHU_PLUGIN_ID\" >> \"$OPENHU_DATA_DIR/../../starts\"\nexec sleep 30\n");
    QFile file(dir + "/manifest.json");
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(QJsonObject::fromVariantMap(manifest)).toJson());
}

QStringList starts()
{
    QFile file(PluginManager::baseDir() + "/starts");
    file.open(QIODevice::ReadOnly);
    return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
}

QStringList sorted(QStringList ids)
{
    ids.sort();
    return ids;
}

QStringList startOrder(const QSignalSpy& spy)
{
    QStringList ids;
    for(const auto& arguments: spy)
    {
        ids.append(arguments.first().toString());
    }
    return ids;
}

QString reasonOf(const PluginManager& manager, const QString& id)
{
    const QVariantList missing = manager.plugin(id).value("missing").toList();
    return missing.isEmpty() ? QString() : missing.first().toMap().value("reason").toString();
}

}

class TestPluginDeps: public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        EventBus::instance().publish("projection.state", {{"connected", false}});
    }

    void manifest()
    {
        QCOMPARE(PluginManager::requiresOf({{"requires", QVariantMap{{"a", "1.2.0"}}}}), (QVariantMap{{"a", "1.2.0"}}));
        // a list: any version
        QCOMPARE(PluginManager::requiresOf({{"requires", QStringList{"a", "b"}}}).keys(), QStringList({"a", "b"}));
        QCOMPARE(PluginManager::requiresOf({{"requires", QVariantMap{{"a", "1.2.0"}}}}).value("a").toString(), QString("1.2.0"));
        QVERIFY(PluginManager::requiresOf({}).isEmpty());
        QCOMPARE(PluginManager::recommendsOf({{"recommends", QStringList{"c"}}}), QStringList{"c"});
    }

    void requiredOnesStartFirst()
    {
        TestHome home;
        // alphabetical order would start "a" first
        writePlugin("a", "1.0.0", {{"requires", QVariantMap{{"b", "1.0.0"}}}});
        writePlugin("b", "1.1.0");
        PluginManager manager(0);
        QSignalSpy started(&manager, &PluginManager::processStarted);
        manager.load();
        QCOMPARE(startOrder(started), QStringList({"b", "a"}));
        QTRY_COMPARE(sorted(starts()), QStringList({"a", "b"}));
        QVERIFY(manager.plugin("a").value("missing").toList().isEmpty());
        QCOMPARE(manager.plugin("a").value("requires").toMap().value("b").toString(), QString("1.0.0"));
        QCOMPARE(manager.dependents("b"), QStringList{"a"});
    }

    void missingRequirementsDoNotStart()
    {
        TestHome home;
        writePlugin("needsNothing", "1.0.0", {{"requires", QVariantMap{{"absent", ""}}}});
        writePlugin("needsNewer", "1.0.0", {{"requires", QVariantMap{{"old", "2.0.0"}}}});
        writePlugin("old", "1.5.0");
        PluginManager manager(0);
        manager.load();
        QTRY_COMPARE(starts(), QStringList{"old"});
        QCOMPARE(reasonOf(manager, "needsNothing"), QString("notInstalled"));
        QCOMPARE(reasonOf(manager, "needsNewer"), QString("tooOld"));
        QVERIFY(!manager.plugin("needsNewer").value("running").toBool());
    }

    void enablingAndDisablingAlong()
    {
        TestHome home;
        writePlugin("app", "1.0.0", {{"requires", QStringList{"lib"}}});
        writePlugin("lib", "1.0.0", {{"requires", QStringList{"base"}}});
        writePlugin("base", "1.0.0");
        PluginManager manager(0);
        QSignalSpy started(&manager, &PluginManager::processStarted);
        manager.load();
        QCOMPARE(startOrder(started), QStringList({"base", "lib", "app"}));
        QTRY_COMPARE(starts().size(), 3);

        // disabling a required plugin disables what requires it
        QCOMPARE(manager.dependents("base"), QStringList({"lib", "app"}));
        QVERIFY(manager.setEnabled("base", false));
        QVERIFY(!manager.plugin("lib").value("enabled").toBool());
        QVERIFY(!manager.plugin("app").value("enabled").toBool());
        QVERIFY(manager.dependents("base").isEmpty());

        // enabling one enables what it requires, in order
        QVERIFY(manager.setEnabled("app", true));
        QVERIFY(manager.plugin("lib").value("enabled").toBool());
        QVERIFY(manager.plugin("base").value("enabled").toBool());
        QCOMPARE(startOrder(started).mid(3), QStringList({"base", "lib", "app"}));
        QTRY_COMPARE(starts().size(), 6);
        QCOMPARE(sorted(starts().mid(3)), QStringList({"app", "base", "lib"}));
    }

    void requiredByALockedPlugin()
    {
        TestHome home;
        writePlugin("provider", "1.0.0", {{"requires", QStringList{"decoder"}},
                                          {"androidAuto", QVariantMap{{"sensors", QStringList{"speed"}}}}});
        writePlugin("decoder", "1.0.0");
        writePlugin("other", "1.0.0");
        PluginManager manager(0);
        manager.load();
        EventBus::instance().publish("projection.state", {{"connected", true}});
        // the decoder feeds a plugin that Android Auto uses: locked too
        QCOMPARE(manager.changeBlocked("decoder"), QString("android auto connected"));
        QVERIFY(manager.changeBlocked("other").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPluginDeps)
#include "test_plugindeps.moc"
