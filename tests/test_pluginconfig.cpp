// Config store of the plugins (#152): the "settings" schema of the manifest, value checks and conversion, the
// stored file (free and declared keys, defaults, size limit, changes from outside), the API methods and the restart
// of a plugin for settings with "applies": "restart".
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginConfig.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginMethods.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginStore.hpp>
#include "TestHome.hpp"

using namespace f1x::openauto::autoapp;
using namespace f1x::openauto::autoapp::plugins;
using f1x::openauto::autoapp::core::EventBus;

namespace
{

QVariantMap parse(const QByteArray& json)
{
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

const QByteArray cSchema = R"({"settings": [
    {"type": "section", "key": "tuner", "title": "Tuner"},
    {"key": "rds", "type": "switch", "default": true, "title": "RDS"},
    {"key": "gain", "type": "choice", "default": "auto", "options": ["auto", 0, 20, 49.6]},
    {"key": "ppm", "type": "number", "default": 0, "min": -100, "max": 100},
    {"key": "columns", "type": "number", "default": 4, "min": 3, "max": 6, "applies": "restart"},
    {"key": "even", "type": "number", "default": 2, "min": 0, "max": 10, "step": 2},
    {"key": "name", "type": "text", "default": "", "maxLength": 5, "placeholder": "Golf"}
]})";

QVariantList schema()
{
    return PluginConfig::parseSchema(parse(cSchema));
}

QVariantMap entryOf(const QString& key)
{
    for(const auto& item: schema())
    {
        if(item.toMap().value("key") == key)
        {
            return item.toMap();
        }
    }
    return QVariantMap();
}

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QVariantMap readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? parse(file.readAll()) : QVariantMap();
}

void writePlugin(const QString& id, const QByteArray& settings, bool process = false)
{
    const QString dir = PluginManager::pluginsDir() + "/" + id;
    QVariantMap manifest{{"id", id}, {"name", id}, {"version", "1.0.0"}, {"api", PluginManager::cApiVersion}};
    manifest.insert(parse(settings));
    if(process)
    {
        manifest.insert("process", QVariantMap{{"exec", "bin/run.sh"}});
        writeFile(dir + "/bin/run.sh", "echo start >> \"$OPENHU_DATA_DIR/starts\"\nexec sleep 30\n");
    }
    writeFile(dir + "/manifest.json", QJsonDocument(QJsonObject::fromVariantMap(manifest)).toJson());
}

int startsOf(const QString& id)
{
    QFile file(PluginManager::baseDir() + "/plugin-data/" + id + "/starts");
    return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
}

}

class TestPluginConfig: public QObject
{
    Q_OBJECT

private slots:
    void schemaIsParsed()
    {
        QStringList errors;
        const QVariantList parsed = PluginConfig::parseSchema(parse(cSchema), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join("; ")));
        QCOMPARE(parsed.size(), 7);
        QCOMPARE(parsed[0].toMap().value("type").toString(), QString("section"));
        // completed: step, applies, maxLength; options as texts
        QCOMPARE(entryOf("ppm").value("step").toLongLong(), 1);
        QCOMPARE(entryOf("ppm").value("applies").toString(), QString("now"));
        QCOMPARE(entryOf("columns").value("applies").toString(), QString("restart"));
        QCOMPARE(entryOf("gain").value("options").toStringList(), QStringList({"auto", "0", "20", "49.6"}));
        QCOMPARE(entryOf("name").value("placeholder").toString(), QString("Golf"));
        QCOMPARE(entryOf("rds").value("title").toString(), QString("RDS"));
        QVERIFY(PluginConfig::parseSchema(QVariantMap()).isEmpty());
    }

    void invalidEntriesAreLeftOut_data()
    {
        QTest::addColumn<QByteArray>("entry");
        QTest::newRow("no key") << QByteArray(R"({"type": "switch", "default": true})");
        QTest::newRow("bad key") << QByteArray(R"({"key": "a.b", "type": "switch", "default": true})");
        QTest::newRow("unknown type") << QByteArray(R"({"key": "a", "type": "slider", "default": 1})");
        QTest::newRow("no default") << QByteArray(R"({"key": "a", "type": "switch"})");
        QTest::newRow("default as text") << QByteArray(R"({"key": "a", "type": "switch", "default": "on"})");
        QTest::newRow("default outside") << QByteArray(R"({"key": "a", "type": "number", "default": 9, "min": 0, "max": 5})");
        QTest::newRow("no range") << QByteArray(R"({"key": "a", "type": "number", "default": 1})");
        QTest::newRow("min > max") << QByteArray(R"({"key": "a", "type": "number", "default": 1, "min": 5, "max": 0})");
        QTest::newRow("step 0") << QByteArray(R"({"key": "a", "type": "number", "default": 1, "min": 0, "max": 5, "step": 0})");
        QTest::newRow("no options") << QByteArray(R"({"key": "a", "type": "choice", "default": "x"})");
        QTest::newRow("same options") << QByteArray(R"({"key": "a", "type": "choice", "default": "x", "options": ["x", "x"]})");
        QTest::newRow("default not an option") << QByteArray(R"({"key": "a", "type": "choice", "default": "y", "options": ["x"]})");
        QTest::newRow("text too long") << QByteArray(R"({"key": "a", "type": "text", "default": "abc", "maxLength": 2})");
        QTest::newRow("applies") << QByteArray(R"({"key": "a", "type": "switch", "default": true, "applies": "later"})");
        QTest::newRow("not an object") << QByteArray(R"("a")");
    }

    void invalidEntriesAreLeftOut()
    {
        QFETCH(QByteArray, entry);
        QStringList errors;
        const QVariantList parsed = PluginConfig::parseSchema(
            parse(R"({"settings": [{"key": "ok", "type": "switch", "default": false}, )" + entry + "]}"), &errors);
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed[0].toMap().value("key").toString(), QString("ok"));
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors[0].startsWith("settings[1]"));
    }

    void duplicateKeysAreLeftOut()
    {
        QStringList errors;
        const QVariantList parsed = PluginConfig::parseSchema(
            parse(R"({"settings": [{"key": "a", "type": "switch", "default": false},
                                   {"key": "a", "type": "text", "default": ""}]})"), &errors);
        QCOMPARE(parsed.size(), 1);
        QVERIFY(errors.value(0).contains("duplicate"));
        PluginConfig::parseSchema(parse(R"({"settings": {"a": 1}})"), &errors);
        QVERIFY(errors.last().contains("not a list"));
    }

    void valuesAreConverted_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QVariant>("expected");  // invalid: refused
        QTest::newRow("switch bool") << "rds" << QVariant(false) << QVariant(false);
        QTest::newRow("switch on") << "rds" << QVariant("on") << QVariant(true);
        QTest::newRow("switch 0") << "rds" << QVariant(0) << QVariant(false);
        QTest::newRow("switch other") << "rds" << QVariant("maybe") << QVariant();
        QTest::newRow("switch 2") << "rds" << QVariant(2) << QVariant();
        QTest::newRow("number") << "ppm" << QVariant(-20) << QVariant(qint64(-20));
        QTest::newRow("number double") << "ppm" << QVariant(5.0) << QVariant(qint64(5));
        QTest::newRow("number text") << "ppm" << QVariant(" 7 ") << QVariant(qint64(7));
        QTest::newRow("number fraction") << "ppm" << QVariant(1.5) << QVariant();
        QTest::newRow("number outside") << "ppm" << QVariant(101) << QVariant();
        QTest::newRow("number step") << "even" << QVariant(4) << QVariant(qint64(4));
        QTest::newRow("number off step") << "even" << QVariant(3) << QVariant();
        QTest::newRow("number word") << "ppm" << QVariant("x") << QVariant();
        QTest::newRow("choice") << "gain" << QVariant("auto") << QVariant("auto");
        QTest::newRow("choice number") << "gain" << QVariant(49.6) << QVariant("49.6");
        QTest::newRow("choice int") << "gain" << QVariant(20) << QVariant("20");
        QTest::newRow("choice other") << "gain" << QVariant("10") << QVariant();
        QTest::newRow("text") << "name" << QVariant("Golf") << QVariant("Golf");
        QTest::newRow("text long") << "name" << QVariant("Passat") << QVariant();
        QTest::newRow("text list") << "name" << QVariant(QVariantList{1}) << QVariant();
    }

    void valuesAreConverted()
    {
        QFETCH(QString, key);
        QFETCH(QVariant, value);
        QFETCH(QVariant, expected);
        QVariant result;
        const QString error = PluginConfig::convert(entryOf(key), value, &result);
        if(!expected.isValid())
        {
            QVERIFY(!error.isEmpty());
            return;
        }
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result, expected);
    }

    void storeKeepsFreeAndDeclaredKeys()
    {
        TestHome home;
        const QString path = PluginConfig::filePath("p");
        // the file of a plugin from before the store: its keys stay
        writeFile(path, R"({"stations": [101.3, 99.5], "ppm": 12, "columns": 9})");
        PluginConfig config;
        config.load("p", schema());

        QVariantMap values = config.values("p");
        QCOMPARE(values.value("stations").toList().size(), 2);
        QCOMPARE(values.value("ppm").toLongLong(), 12);
        // defaults, and the default for a stored value that does not fit any more (kept in the file)
        QCOMPARE(values.value("rds").toBool(), true);
        QCOMPARE(values.value("columns").toLongLong(), 4);
        QVERIFY(!values.contains("tuner"));
        QCOMPARE(EventBus::instance().last(PluginConfig::topic("p")).value("values").toMap(), values);

        QStringList changed;
        QCOMPARE(config.set("p", {{"rds", "off"}, {"presets", QVariantMap{{"fm", QVariantList{87.6}}}}}, &changed),
                 QString());
        changed.sort();
        QCOMPARE(changed, QStringList({"presets", "rds"}));
        QVariantMap file = readFile(path);
        QCOMPARE(file.value("rds").toBool(), false);
        QCOMPARE(file.value("columns").toLongLong(), 9);
        QCOMPARE(file.value("presets").toMap().value("fm").toList().value(0).toDouble(), 87.6);
        const QVariantMap published = EventBus::instance().last(PluginConfig::topic("p"));
        QCOMPARE(published.value("values").toMap().value("rds").toBool(), false);
        QCOMPARE(published.value("changed").toStringList().size(), 2);

        // all or nothing
        QVERIFY(!config.set("p", {{"ppm", 1}, {"rds", "maybe"}}).isEmpty());
        QCOMPARE(config.values("p").value("ppm").toLongLong(), 12);
        QVERIFY(config.set("p", {{"tuner", true}}).contains("section"));

        // a value equal to the default is stored too (a new default does not change what the user picked)
        QCOMPARE(config.set("p", {{"columns", 4}}), QString());
        QCOMPARE(readFile(path).value("columns").toLongLong(), 4);

        // remove: free key gone, declared key back to its default
        QCOMPARE(config.remove("p", {"presets", "rds"}), QString());
        values = config.values("p");
        QVERIFY(!values.contains("presets"));
        QCOMPARE(values.value("rds").toBool(), true);
        QVERIFY(!readFile(path).contains("rds"));

        // reset: declared keys only
        QCOMPARE(config.set("p", {{"ppm", 3}, {"name", "Golf"}}), QString());
        QCOMPARE(config.reset("p"), QString());
        file = readFile(path);
        QVERIFY(!file.contains("ppm") && !file.contains("name") && !file.contains("columns"));
        QCOMPARE(file.value("stations").toList().size(), 2);

        QVERIFY(config.set("unknown", {{"a", 1}}).contains("unknown plugin"));
    }

    void unchangedValuesAreNotWritten()
    {
        TestHome home;
        PluginConfig config;
        config.load("p", schema());
        QCOMPARE(config.set("p", {{"ppm", 5}}), QString());
        QSignalSpy published(&EventBus::instance(), &EventBus::event);
        QStringList changed{"x"};
        QCOMPARE(config.set("p", {{"ppm", "5"}}, &changed), QString());
        QVERIFY(changed.isEmpty());
        QCOMPARE(published.count(), 0);
    }

    void sizeIsLimited()
    {
        TestHome home;
        PluginConfig config;
        config.load("p", QVariantList());
        const QString big(PluginConfig::cMaxBytes, 'x');
        QVERIFY(config.set("p", {{"big", big}}).contains("too large"));
        QVERIFY(!QFile::exists(PluginConfig::filePath("p")));
        QCOMPARE(config.set("p", {{"small", "x"}}), QString());
    }

    void changesFromOutsideAreRead()
    {
        TestHome home;
        const QString path = PluginConfig::filePath("p");
        PluginConfig config;
        config.load("p", schema());
        QCOMPARE(config.set("p", {{"stations", QVariantList{1, 2}}}), QString());

        // written by hand (or by a plugin that still saves its config itself): not overwritten
        QTest::qWait(20);
        writeFile(path, R"({"stations": [1, 2, 3], "ppm": 50, "manual": true})");
        QCOMPARE(config.values("p").value("ppm").toLongLong(), 50);
        QCOMPARE(EventBus::instance().last(PluginConfig::topic("p")).value("changed").toStringList(),
                 QStringList({"manual", "ppm", "stations"}));
        QTest::qWait(20);
        writeFile(path, R"({"stations": [1, 2, 3, 4], "ppm": 50, "manual": true})");
        QCOMPARE(config.set("p", {{"rds", false}}), QString());
        const QVariantMap file = readFile(path);
        QCOMPARE(file.value("stations").toList().size(), 4);
        QCOMPARE(file.value("manual").toBool(), true);
        QCOMPARE(file.value("rds").toBool(), false);
    }

    void brokenFileIsKept()
    {
        TestHome home;
        const QString path = PluginConfig::filePath("p");
        writeFile(path, "{broken");
        PluginConfig config;
        config.load("p", schema());
        QCOMPARE(config.values("p").value("ppm").toLongLong(), 0);
        QVERIFY(QFile::exists(path + ".broken"));
        QCOMPARE(config.set("p", {{"ppm", 1}}), QString());
        QCOMPARE(readFile(path).value("ppm").toLongLong(), 1);
    }

    void apiMethods()
    {
        TestHome home;
        writePlugin("org.example.a", cSchema);
        writePlugin("org.example.plain", "{}");
        PluginManager manager(5520);
        manager.load();
        PluginStore store(manager);
        api::ApiServer server;
        registerPluginMethods(server, manager, store);

        // the schema in plugin.list (shown in Settings → Apps), none for a plugin without settings
        QCOMPARE(manager.plugin("org.example.a").value("settings").toList().size(), 7);
        QVERIFY(!manager.plugin("org.example.plain").contains("settings"));

        QVariantMap result = server.call("plugin.config.get", {{"id", "org.example.a"}}).toMap();
        QCOMPARE(result.value("values").toMap().value("gain").toString(), QString("auto"));
        result = server.call("plugin.config.set", {{"id", "org.example.a"}, {"key", "ppm"}, {"value", "-3"}}).toMap();
        QCOMPARE(result.value("value").toLongLong(), -3);
        result = server.call("plugin.config.get", {{"id", "org.example.a"}, {"key", "ppm"}}).toMap();
        QCOMPARE(result.value("value").toLongLong(), -3);
        result = server.call("plugin.config.set", {{"id", "org.example.a"},
                                                   {"values", QVariantMap{{"rds", false}, {"free", "x"}}}}).toMap();
        QCOMPARE(result.value("values").toMap().value("free").toString(), QString("x"));
        QVERIFY_THROWS_EXCEPTION(api::ApiError,
                                 server.call("plugin.config.set", {{"id", "org.example.a"}, {"key", "ppm"}, {"value", 500}}));
        QVERIFY_THROWS_EXCEPTION(api::ApiError, server.call("plugin.config.get", {{"id", "org.example.none"}}));
        QVERIFY_THROWS_EXCEPTION(api::ApiError,
                                 server.call("plugin.config.get", {{"id", "org.example.a"}, {"key", "none"}}));

        result = server.call("plugin.config.remove", {{"id", "org.example.a"}, {"key", "free"}}).toMap();
        QVERIFY(!result.value("values").toMap().contains("free"));
        result = server.call("plugin.config.reset", {{"id", "org.example.a"}}).toMap();
        QCOMPARE(result.value("values").toMap().value("ppm").toLongLong(), 0);
        QCOMPARE(result.value("values").toMap().value("rds").toBool(), true);

        // a disabled plugin keeps its store, also a plugin without settings has one
        QVERIFY(manager.setEnabled("org.example.a", false));
        server.call("plugin.config.set", {{"id", "org.example.a"}, {"key", "ppm"}, {"value", 9}});
        QCOMPARE(readFile(PluginConfig::filePath("org.example.a")).value("ppm").toLongLong(), 9);
        server.call("plugin.config.set", {{"id", "org.example.plain"}, {"key", "x"}, {"value", 1}});
        QCOMPARE(readFile(PluginConfig::filePath("org.example.plain")).value("x").toLongLong(), 1);

        // removed: no store any more
        manager.unload("org.example.plain");
        QVERIFY_THROWS_EXCEPTION(api::ApiError, server.call("plugin.config.get", {{"id", "org.example.plain"}}));
        manager.stopAll();
    }

    void restartSetting()
    {
        TestHome home;
        writePlugin("org.example.r", cSchema, true);
        PluginManager manager(5520);
        manager.load();
        QTRY_COMPARE(startsOf("org.example.r"), 1);

        // "applies": "now" (default): the process follows the topic itself
        QCOMPARE(manager.setConfig("org.example.r", {{"ppm", 4}}), QString());
        QTest::qWait(300);
        QCOMPARE(startsOf("org.example.r"), 1);
        // "applies": "restart"
        QCOMPARE(manager.setConfig("org.example.r", {{"columns", 5}}), QString());
        QTRY_COMPARE(startsOf("org.example.r"), 2);
        // unchanged: no restart
        QCOMPARE(manager.setConfig("org.example.r", {{"columns", 5}}), QString());
        QTest::qWait(300);
        QCOMPARE(startsOf("org.example.r"), 2);
        QCOMPARE(manager.resetConfig("org.example.r"), QString());
        QTRY_COMPARE(startsOf("org.example.r"), 3);
        manager.stopAll();
    }
};

QTEST_GUILESS_MAIN(TestPluginConfig)
#include "test_pluginconfig.moc"
