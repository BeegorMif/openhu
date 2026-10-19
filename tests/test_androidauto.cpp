// Plugins that provide to Android Auto (#123): the manifest section, which sources may feed Android Auto, and that
// such plugins change only while no phone is connected.
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Plugins/AndroidAutoProviders.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include "TestHome.hpp"

using f1x::openauto::autoapp::core::EventBus;
using f1x::openauto::autoapp::plugins::PluginManager;
namespace plugins = f1x::openauto::autoapp::plugins;

namespace
{

// a plugin in ~/.openhu/plugins; with a process that writes its token to its data folder and keeps running
void writePlugin(const QString& id, const QVariantMap& androidAuto = QVariantMap(), bool process = false)
{
    const QString dir = PluginManager::pluginsDir() + "/" + id;
    QDir().mkpath(dir + "/bin");
    QVariantMap manifest{{"id", id}, {"name", id}, {"version", "1.0.0"}, {"api", PluginManager::cApiVersion}};
    if(!androidAuto.isEmpty())
    {
        manifest.insert("androidAuto", androidAuto);
    }
    if(process)
    {
        manifest.insert("process", QVariantMap{{"exec", "bin/run.sh"}});
        QFile script(dir + "/bin/run.sh");
        script.open(QIODevice::WriteOnly);
        script.write("echo -n \"$OPENHU_PLUGIN_TOKEN\" > \"$OPENHU_DATA_DIR/token\"\nexec sleep 30\n");
    }
    QFile file(dir + "/manifest.json");
    file.open(QIODevice::WriteOnly);
    file.write(QJsonDocument(QJsonObject::fromVariantMap(manifest)).toJson());
}

void setConnected(bool connected)
{
    EventBus::instance().publish("projection.state", {{"connected", connected}});
}

QVariantMap pluginsState(const QVariantList& plugins)
{
    return {{"plugins", plugins}};
}

}

class TestAndroidAuto: public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        setConnected(false);
    }

    void parseManifest()
    {
        QStringList ignored;
        const QVariantMap section = plugins::parseAndroidAuto(
            {{"androidAuto", QVariantMap{{"sensors", QStringList{"speed", "gear", "warpDrive"}},
                                         {"services", QStringList{"radio", "teleport"}}, {"dynamic", true}}}},
            &ignored);
        QCOMPARE(section.value("sensors").toStringList(), QStringList({"speed", "gear"}));
        QCOMPARE(section.value("services").toStringList(), QStringList{"radio"});
        QCOMPARE(ignored, QStringList({"sensor warpDrive", "service teleport"}));
        QVERIFY(plugins::parseAndroidAuto({{"id", "x"}}).isEmpty());
    }

    void sourcesThatMayFeedAndroidAuto()
    {
        const QVariantMap state = pluginsState({
            QVariantMap{{"id", "vehicle"}, {"enabled", true}, {"compatible", true},
                        {"androidAuto", QVariantMap{{"sensors", QStringList{"speed"}}, {"services", QStringList{"carControl"}}}}},
            QVariantMap{{"id", "off"}, {"enabled", false}, {"compatible", true},
                        {"androidAuto", QVariantMap{{"sensors", QStringList{"gear"}}, {"services", QStringList{"radio"}}}}},
            QVariantMap{{"id", "can"}, {"enabled", true}, {"compatible", true}}});

        QVERIFY(plugins::androidAutoDeclared(state, "sensors", "speed"));
        QVERIFY(plugins::androidAutoDeclared(state, "services", "carControl"));
        // a disabled plugin declares nothing
        QVERIFY(!plugins::androidAutoDeclared(state, "sensors", "gear"));
        QVERIFY(!plugins::androidAutoDeclared(state, "services", "radio"));

        QVERIFY(plugins::androidAutoSourceAllowed(state, "vehicle.speed", "plugin:vehicle"));
        QVERIFY(!plugins::androidAutoSourceAllowed(state, "vehicle.gear", "plugin:vehicle"));
        // a plugin without the section only prepares data
        QVERIFY(!plugins::androidAutoSourceAllowed(state, "vehicle.speed", "plugin:can"));
        QVERIFY(!plugins::androidAutoSourceAllowed(state, "vehicle.gear", "plugin:off"));
        // API clients that are not plugins, QML pages and OpenHU itself
        QVERIFY(plugins::androidAutoSourceAllowed(state, "vehicle.speed", "client:3"));
        QVERIFY(plugins::androidAutoSourceAllowed(state, "vehicle.speed", QString()));
        // the test topic from anyone
        QVERIFY(plugins::androidAutoSourceAllowed(state, "vehicle.simulate", "plugin:can"));
    }

    void busKeepsTheSource()
    {
        auto& bus = EventBus::instance();
        bus.publish("vehicle.testSource", {{"kmh", 50}}, "plugin:a");
        bus.publish("test.notVehicle", {{"value", 1}}, "plugin:a");
        bus.publish("vehicle.testOther", {{"kmh", 50}}, "plugin:b");
        QCOMPARE(bus.source("vehicle.testSource"), QString("plugin:a"));

        bus.releaseSource("plugin:a");
        QCOMPARE(bus.last("vehicle.testSource"), (QVariantMap{{"available", false}}));
        QCOMPARE(bus.source("vehicle.testSource"), QString("plugin:a"));
        QCOMPARE(bus.last("test.notVehicle").value("value").toInt(), 1);
        QCOMPARE(bus.last("vehicle.testOther").value("kmh").toInt(), 50);

        bus.publish("vehicle.testSource", {{"kmh", 30}});
        QVERIFY(bus.source("vehicle.testSource").isEmpty());
    }

    void lockedWhileConnected()
    {
        TestHome home;
        writePlugin("provider", {{"sensors", QStringList{"speed"}}});
        writePlugin("plain");
        PluginManager manager(0);
        manager.load();
        QVERIFY(manager.plugin("provider").value("enabled").toBool());
        QCOMPARE(manager.plugin("provider").value("androidAuto").toMap().value("sensors").toStringList(), QStringList{"speed"});
        QVERIFY(!manager.plugin("plain").contains("androidAuto"));
        // the services read it
        QVERIFY(plugins::androidAutoDeclared(EventBus::instance().last("plugins.state"), "sensors", "speed"));

        setConnected(true);
        QCOMPARE(manager.changeBlocked("provider"), QString("android auto connected"));
        QVERIFY(!manager.setEnabled("provider", false));
        QVERIFY(manager.plugin("provider").value("enabled").toBool());
        QVERIFY(manager.changeBlocked("plain").isEmpty());
        QVERIFY(manager.setEnabled("plain", false));

        // installed during the session: disabled until the user enables it without a phone
        writePlugin("newProvider", {{"services", QStringList{"radio"}}});
        writePlugin("newPlain");
        manager.load();
        QVERIFY(!manager.plugin("newProvider").value("enabled").toBool());
        QVERIFY(manager.plugin("newPlain").value("enabled").toBool());
        // a reload keeps the installed ones as they were
        QVERIFY(manager.plugin("provider").value("enabled").toBool());

        setConnected(false);
        QVERIFY(manager.changeBlocked("newProvider").isEmpty());
        QVERIFY(manager.setEnabled("newProvider", true));
        QVERIFY(plugins::androidAutoDeclared(EventBus::instance().last("plugins.state"), "services", "radio"));
    }

    void processTokenAndStaleValues()
    {
        TestHome home;
        writePlugin("tokens", {{"sensors", QStringList{"speed"}}}, true);
        PluginManager manager(0);
        manager.load();
        const QString tokenFile = PluginManager::baseDir() + "/plugin-data/tokens/token";
        QTRY_VERIFY(QFile(tokenFile).size() > 0);
        QFile file(tokenFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString token = QString::fromUtf8(file.readAll());
        QCOMPARE(manager.pluginForToken(token), QString("tokens"));
        QVERIFY(manager.pluginForToken("guess").isEmpty());
        QVERIFY(manager.pluginForToken(QString()).isEmpty());

        // the process ends: its token is gone and its vehicle data is not current any more
        EventBus::instance().publish("vehicle.speed", {{"kmh", 80}}, EventBus::sourcePlugin("tokens"));
        QVERIFY(manager.setEnabled("tokens", false));
        QVERIFY(manager.pluginForToken(token).isEmpty());
        QVERIFY(!EventBus::instance().last("vehicle.speed").value("available", true).toBool());
    }
};

QTEST_GUILESS_MAIN(TestAndroidAuto)
#include "test_androidauto.moc"
