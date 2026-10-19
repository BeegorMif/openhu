#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QWebSocket>
#include <QtTest>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Api/ClientFilter.hpp>
#include <f1x/openauto/autoapp/Api/CoreMethods.hpp>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Shell/SettingsMethods.hpp>
#include "TestHome.hpp"

using f1x::openauto::autoapp::api::ApiError;
using f1x::openauto::autoapp::api::ApiServer;
using f1x::openauto::autoapp::api::HotspotNetwork;
namespace api = f1x::openauto::autoapp::api;
using f1x::openauto::autoapp::audio::VolumeController;
using f1x::openauto::autoapp::core::EventBus;
using f1x::openauto::autoapp::input::ActionDispatcher;
using f1x::openauto::autoapp::configuration::Configuration;
using f1x::openauto::autoapp::shell::SettingsController;
using f1x::openauto::autoapp::shell::ShellController;

namespace
{

quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

const QString cToken = QStringLiteral("test-token-0123456789");

// WebSocket client that keeps the received messages as JSON objects. With an origin it acts like a browser.
class Client: public QObject
{
public:
    explicit Client(quint16 port, const QString& origin = QString(), const QString& query = QString(),
                    const QByteArray& authorization = QByteArray(), const QString& host = "127.0.0.1")
        : socket_(origin)
    {
        connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString& message) {
            messages_.append(QJsonDocument::fromJson(message.toUtf8()).object().toVariantMap());
        });
        QNetworkRequest request(QUrl(QString("ws://%1:%2/%3").arg(host).arg(port).arg(query)));
        if(!authorization.isEmpty())
        {
            request.setRawHeader("Authorization", authorization);
        }
        socket_.open(request);
    }

    // Closed by the server with this close code (waits up to 5 s)
    bool closedWith(QWebSocketProtocol::CloseCode code)
    {
        QTest::qWaitFor([this]() { return socket_.state() == QAbstractSocket::UnconnectedState; }, 5000);
        return socket_.state() == QAbstractSocket::UnconnectedState && socket_.closeCode() == code;
    }

    // Result of a request, or its error as {"error": ...}
    QVariantMap call(const QString& method, const QVariantMap& params = QVariantMap())
    {
        this->send({{"type", "request"}, {"id", 1}, {"method", method}, {"params", params}});
        const QVariantMap response = this->next();
        return response.contains("error") ? QVariantMap{{"error", response.value("error")}}
                                          : response.value("result").toMap();
    }

    bool connected()
    {
        return QTest::qWaitFor([this]() { return socket_.state() == QAbstractSocket::ConnectedState; }, 5000);
    }

    void send(const QVariantMap& message)
    {
        socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(message)).toJson()));
    }

    void sendRaw(const QString& text) { socket_.sendTextMessage(text); }

    // Next message (waits up to 5 s), empty map on timeout
    QVariantMap next()
    {
        if(!QTest::qWaitFor([this]() { return !messages_.isEmpty(); }, 5000))
        {
            return QVariantMap();
        }
        return messages_.takeFirst();
    }

    bool idle(int ms)
    {
        QTest::qWait(ms);
        return messages_.isEmpty();
    }

    QWebSocket& socket() { return socket_; }

private:
    QWebSocket socket_;
    QList<QVariantMap> messages_;
};

}

class TestApi: public QObject
{
    Q_OBJECT

private slots:
    void callInProcess()
    {
        ApiServer server;
        server.registerMethod("test.echo", [](const QVariantMap& params) -> QVariant { return params; });

        QVERIFY(server.methods().contains("test.echo"));
        QCOMPARE(server.call("test.echo", {{"a", 1}}).toMap().value("a").toInt(), 1);
        QVERIFY_EXCEPTION_THROWN(server.call("test.missing"), ApiError);
    }

    void coreMethods()
    {
        TestHome home;
        ApiServer server;
        VolumeController volume;
        ShellController shell;
        ActionDispatcher actions(volume, shell);
        f1x::openauto::autoapp::api::registerCoreMethods(server, volume, shell, actions);

        QCOMPARE(server.call("system.ping").toMap().value("pong").toBool(), true);
        QVERIFY(server.call("system.methods").toStringList().contains("bus.publish"));

        server.call("bus.publish", {{"topic", "test.api"}, {"data", QVariantMap{{"x", 5}}}});
        QCOMPARE(server.call("bus.get", {{"topic", "test.api"}}).toMap().value("x").toInt(), 5);
        QVERIFY(server.call("bus.topics").toStringList().contains("test.api"));
        QVERIFY_EXCEPTION_THROWN(server.call("bus.publish", {}), ApiError);
        QVERIFY_EXCEPTION_THROWN(server.call("bus.publish", {{"topic", ""}}), ApiError);

        QSignalSpy navigate(&shell, &ShellController::navigateRequested);
        server.call("ui.navigate", {{"page", "settings"}});
        QCOMPARE(navigate.count(), 1);
        QVERIFY_EXCEPTION_THROWN(server.call("ui.navigate", {}), ApiError);

        QCOMPARE(server.call("input.action", {{"action", "ui.home"}}).toMap().value("handled").toBool(), true);
        QVERIFY_EXCEPTION_THROWN(server.call("input.action", {{"action", "ui.home"}, {"phase", "hold"}}), ApiError);
        QVERIFY(server.call("input.actions").toMap().contains("volume.up"));
        QCOMPARE(server.call("input.keymap").toMap().value("N").toString(), QString("media.next"));

        server.call("daynight.set", {{"night", true}});
        QCOMPARE(EventBus::instance().last("system.daynight").value("night").toBool(), true);
    }

    // settings.* (openhu#42): the same checks as the settings page, values from the command line as text
    // Settings → Debug (#120): the debug keys exist only while the debug mode is on
    void debugSettings()
    {
        TestHome home;
        ApiServer server;
        SettingsController settings(std::make_shared<Configuration>(), 0, nullptr);
        f1x::openauto::autoapp::shell::registerSettingsMethods(server, settings);

        QVERIFY(!server.call("settings.keys").toMap().contains("debug.simulate"));
        QVERIFY(server.call("settings.keys").toMap().contains("debug.enabled"));
        QVERIFY(!server.call("settings.list").toMap().value("values").toMap().contains("debug.logLevel"));
        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "debug.simulate"}, {"value", "park"}}), ApiError);
        QVERIFY(EventBus::instance().last("vehicle.simulate").value("state").toString().isEmpty());

        server.call("settings.set", {{"key", "debug.enabled"}, {"value", true}});
        QVERIFY(EventBus::instance().last("debug.state").value("enabled").toBool());
        QStringList keys = server.call("settings.keys").toMap().keys();
        QStringList values = server.call("settings.list").toMap().value("values").toMap().keys();
        keys.sort();
        values.sort();
        QCOMPARE(keys, values);
        QVERIFY(keys.contains("debug.videoCodec"));

        server.call("settings.set", {{"key", "debug.simulate"}, {"value", "park"}});
        QCOMPARE(EventBus::instance().last("vehicle.simulate").value("state").toString(), QString("park"));
        QCOMPARE(server.call("settings.get", {{"key", "debug.simulate"}}).toMap().value("value").toString(), QString("park"));
        server.call("settings.set", {{"key", "debug.logLevel"}, {"value", "debug"}});
        QCOMPARE(server.call("settings.get", {{"key", "debug.logLevel"}}).toMap().value("value").toString(), QString("debug"));
        server.call("settings.set", {{"key", "debug.logLevel"}, {"value", "info"}});
        server.call("settings.set", {{"key", "debug.videoOverlay"}, {"value", true}});
        QVERIFY(EventBus::instance().last("debug.state").value("videoOverlay").toBool());
        QVERIFY(server.call("settings.set", {{"key", "debug.videoDecoder"}, {"value", "software"}}).toMap()
                    .value("pending").toStringList().contains("reconnect"));
        // saved: the decoder and the mode itself; the simulated state and the overlay are not
        QCOMPARE(Configuration().getVideoDecoder(), std::string("software"));
        QVERIFY(Configuration().getDebugEnabled());

        // switched off: nothing of it stays active
        server.call("settings.set", {{"key", "debug.enabled"}, {"value", false}});
        QVERIFY(EventBus::instance().last("vehicle.simulate").value("state").toString().isEmpty());
        QVERIFY(!EventBus::instance().last("debug.state").value("videoOverlay").toBool());
        QVERIFY(!server.call("settings.keys").toMap().contains("debug.videoDecoder"));
    }

    void settingsMethods()
    {
        TestHome home;
        ApiServer server;
        SettingsController settings(std::make_shared<Configuration>(), 0, nullptr);
        f1x::openauto::autoapp::shell::registerSettingsMethods(server, settings);

        // every value has a description, and every description a value
        QStringList keys = server.call("settings.keys").toMap().keys();
        QStringList values = server.call("settings.list").toMap().value("values").toMap().keys();
        keys.sort();
        values.sort();
        QCOMPARE(keys, values);
        QCOMPARE(server.call("settings.get", {{"key", "aa.fps"}}).toMap().value("value").toInt(), 30);

        QVariantMap result = server.call("settings.set", {{"key", "aa.fps"}, {"value", "60"}}).toMap();
        QCOMPARE(result.value("value").toInt(), 60);
        QCOMPARE(result.value("applies").toString(), QString("reconnect"));
        QVERIFY(result.value("pending").toStringList().contains("reconnect"));
        const QVariantMap changed = EventBus::instance().last("settings.changed");
        QCOMPARE(changed.value("key").toString(), QString("aa.fps"));
        QCOMPARE(changed.value("value").toInt(), 60);
        // saved to openhu.ini
        QVERIFY(Configuration().getVideoFPS() == aap_protobuf::service::media::sink::message::VideoFrameRateType::VIDEO_FPS_60);

        QCOMPARE(server.call("settings.set", {{"key", "general.showCursor"}, {"value", "off"}}).toMap().value("value").toBool(), false);
        QCOMPARE(server.call("settings.set", {{"key", "aa.dpi"}, {"value", 200}}).toMap().value("value").toInt(), 200);
        QVERIFY(server.call("settings.set", {{"key", "api.port"}, {"value", "6000"}}).toMap().value("pending").toStringList().contains("restart"));

        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "aa.fps"}, {"value", "45"}}), ApiError);
        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "aa.dpi"}, {"value", "many"}}), ApiError);
        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "aa.dpi"}, {"value", 500}}), ApiError);  // set()'s own check
        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "general.showCursor"}, {"value", "maybe"}}), ApiError);
        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "aa.activeResolution"}, {"value", "auto"}}), ApiError);
        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "aa.fps"}}), ApiError);
        QVERIFY_EXCEPTION_THROWN(server.call("settings.get", {{"key", "no.such"}}), ApiError);
        QCOMPARE(server.call("settings.get", {{"key", "aa.dpi"}}).toMap().value("value").toInt(), 200);

        // passwords: only on the device itself, not in the event
        server.call("settings.set", {{"key", "wireless.password"}, {"value", "secret-123"}});
        QVERIFY(!EventBus::instance().last("settings.changed").contains("value"));
        QCOMPARE(server.call("settings.get", {{"key", "wireless.password"}}).toMap().value("value").toString(), QString("secret-123"));
        const QVariantMap remote{{ApiServer::cLocal, false}};
        QVERIFY(!server.call("settings.list", remote).toMap().value("values").toMap().contains("wireless.password"));
        QVERIFY(server.call("settings.list", remote).toMap().value("values").toMap().contains("wireless.ssid"));
        QVERIFY_EXCEPTION_THROWN(server.call("settings.get", {{"key", "wireless.password"}, {ApiServer::cLocal, false}}), ApiError);
        QVERIFY(!server.call("settings.set", {{"key", "carAudio.pin"}, {"value", "1234"}, {ApiServer::cLocal, false}})
                     .toMap().contains("value"));

        // API token (openhu#23): read on the device only, a new one only from the shell
        QVERIFY_EXCEPTION_THROWN(server.call("settings.set", {{"key", "api.token"}, {"value", "mine"}}), ApiError);
        QVERIFY_EXCEPTION_THROWN(server.call("settings.get", {{"key", "api.token"}, {ApiServer::cLocal, false}}), ApiError);
        QSignalSpy access(&settings, &SettingsController::apiAccessChanged);
        settings.regenerateApiToken();
        QCOMPARE(access.count(), 1);
        const QString token = server.call("settings.get", {{"key", "api.token"}}).toMap().value("value").toString();
        QCOMPARE(token.size(), 43);
        QVERIFY(!EventBus::instance().last("settings.changed").contains("value"));
        QCOMPARE(Configuration().getApiToken(), token.toStdString());
        settings.regenerateApiToken();
        QVERIFY(server.call("settings.get", {{"key", "api.token"}}).toMap().value("value").toString() != token);

        QCOMPARE(server.call("settings.get", {{"key", "api.origins"}}).toMap().value("value").toString(), QString("*"));
        QCOMPARE(server.call("settings.set", {{"key", "api.origins"}, {"value", " HTTP://A.example/, http://b.example"}})
                     .toMap().value("value").toString(), QString("http://a.example,http://b.example"));
        QTRY_COMPARE(access.count(), 3);  // queued
    }

    void requestsOverWebSocket()
    {
        ApiServer server;
        server.registerMethod("test.echo", [](const QVariantMap& params) -> QVariant { return params; });
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));

        Client client(port);
        QVERIFY(client.connected());

        client.send({{"type", "request"}, {"id", 7}, {"method", "test.echo"}, {"params", QVariantMap{{"v", "x"}}}});
        QVariantMap response = client.next();
        QCOMPARE(response.value("type").toString(), QString("response"));
        QCOMPARE(response.value("id").toInt(), 7);
        QCOMPARE(response.value("result").toMap().value("v").toString(), QString("x"));
        // set by the server, a client cannot claim it
        QCOMPARE(response.value("result").toMap().value(ApiServer::cLocal).toBool(), true);
        client.send({{"id", 9}, {"method", "test.echo"}, {"params", QVariantMap{{ApiServer::cLocal, false}}}});
        QCOMPARE(client.next().value("result").toMap().value(ApiServer::cLocal).toBool(), true);

        // "type" may be left out for requests
        client.send({{"id", 8}, {"method", "test.missing"}});
        response = client.next();
        QCOMPARE(response.value("id").toInt(), 8);
        QVERIFY(response.value("error").toString().contains("unknown method"));

        client.sendRaw("{not json");
        QCOMPARE(client.next().value("type").toString(), QString("error"));

        client.send({{"type", "dance"}});
        QVERIFY(client.next().value("error").toString().contains("unknown message type"));
    }

    // plugin processes log in with their token, their values count as the plugin's (#123)
    void pluginTokenSetsTheSource()
    {
        ApiServer server;
        server.setPluginResolver([](const QString& token) { return token == "plugin-token" ? QString("org.test") : QString(); });
        server.registerMethod("test.echo", [](const QVariantMap& params) -> QVariant { return params; });
        server.registerMethod("test.publish", [](const QVariantMap& params) -> QVariant {
            EventBus::instance().publish(params.value("topic").toString(), {{"value", 1}},
                                         params.value(ApiServer::cSource).toString());
            return QVariantMap();
        });
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));

        Client plugin(port);
        QVERIFY(plugin.connected());
        plugin.send({{"type", "auth"}, {"pluginToken", "plugin-token"}});
        const QVariantMap auth = plugin.next();
        QVERIFY(auth.value("authenticated").toBool());
        QCOMPARE(auth.value("plugin").toString(), QString("org.test"));
        QCOMPARE(plugin.call("test.echo").value(ApiServer::cSource).toString(), QString("plugin:org.test"));
        // a client cannot claim a source
        QCOMPARE(plugin.call("test.echo", {{ApiServer::cSource, "plugin:other"}}).value(ApiServer::cSource).toString(),
                 QString("plugin:org.test"));

        Client client(port);
        QVERIFY(client.connected());
        QVERIFY(client.call("test.echo").value(ApiServer::cSource).toString().startsWith("client:"));

        // values of a closed connection are not current any more
        plugin.call("test.publish", {{"topic", "vehicle.testPlugin"}});
        client.call("test.publish", {{"topic", "vehicle.testClient"}});
        QCOMPARE(EventBus::instance().source("vehicle.testPlugin"), QString("plugin:org.test"));
        plugin.socket().close();
        QTRY_VERIFY(!EventBus::instance().last("vehicle.testPlugin").value("available", true).toBool());
        QVERIFY(EventBus::instance().last("vehicle.testClient").value("available", true).toBool());
        client.socket().close();
        QTRY_VERIFY(!EventBus::instance().last("vehicle.testClient").value("available", true).toBool());

        Client wrong(port);
        QVERIFY(wrong.connected());
        wrong.send({{"type", "auth"}, {"pluginToken", "guess"}});
        QVERIFY(wrong.closedWith(QWebSocketProtocol::CloseCodePolicyViolated));
    }

    void subscriptions()
    {
        ApiServer server;
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));
        EventBus::instance().publish("test.sub.state", {{"value", 1}});

        Client client(port);
        QVERIFY(client.connected());

        // current state right after subscribing
        client.send({{"type", "subscribe"}, {"topics", QStringList{"test.sub.*"}}});
        QVariantMap event = client.next();
        QCOMPARE(event.value("type").toString(), QString("event"));
        QCOMPARE(event.value("topic").toString(), QString("test.sub.state"));
        QCOMPARE(event.value("data").toMap().value("value").toInt(), 1);

        EventBus::instance().publish("test.sub.state", {{"value", 2}});
        QCOMPARE(client.next().value("data").toMap().value("value").toInt(), 2);
        EventBus::instance().emitEvent("test.sub.command", {{"cmd", "x"}});
        QCOMPARE(client.next().value("topic").toString(), QString("test.sub.command"));

        // other topics are not sent
        EventBus::instance().publish("test.other", {{"value", 3}});
        QVERIFY(client.idle(200));

        client.send({{"type", "unsubscribe"}, {"topics", QStringList{"test.sub.*"}}});
        QTest::qWait(100);
        EventBus::instance().publish("test.sub.state", {{"value", 4}});
        QVERIFY(client.idle(200));
    }

    void clientFilter()
    {
        ApiServer server;
        server.setClientFilter([](const QHostAddress&) { return false; });
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));

        Client client(port);
        QSignalSpy closed(&client.socket(), &QWebSocket::disconnected);
        QVERIFY(closed.wait(5000) || closed.count() > 0);
        QCOMPARE(client.socket().closeCode(), QWebSocketProtocol::CloseCodePolicyViolated);
    }

    // openhu#17: the hotspot is recognised by its IPv4 subnet and by every address of its interface
    void hotspotFilter_data()
    {
        QTest::addColumn<QString>("peer");
        QTest::addColumn<bool>("blockHotspot");
        QTest::addColumn<bool>("hotspotUp");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("loopback v4") << "127.0.0.1" << true << true << true;
        QTest::newRow("loopback v6") << "::1" << true << true << true;
        QTest::newRow("loopback v4-mapped") << "::ffff:127.0.0.1" << true << true << true;
        QTest::newRow("hotspot v4") << "10.42.0.5" << true << true << false;
        QTest::newRow("hotspot v4-mapped (dual stack)") << "::ffff:10.42.0.5" << true << true << false;
        QTest::newRow("hotspot v4, hotspot down") << "10.42.0.5" << true << false << false;
        QTest::newRow("hotspot v6 ULA") << "fd42::5" << true << true << false;
        QTest::newRow("hotspot link-local") << "fe80::1234%wlan0" << true << true << false;
        QTest::newRow("link-local other interface") << "fe80::1234%eth0" << true << true << true;
        QTest::newRow("LAN v4") << "192.168.1.20" << true << true << true;
        QTest::newRow("LAN v6") << "2001:db8::20" << true << true << true;
        QTest::newRow("v6 while the hotspot interface is unknown") << "2001:db8::20" << true << false << false;
        QTest::newRow("hotspot allowed v4") << "10.42.0.5" << false << true << true;
        QTest::newRow("hotspot allowed v6") << "fd42::5" << false << true << true;
    }

    void hotspotFilter()
    {
        QFETCH(QString, peer);
        QFETCH(bool, blockHotspot);
        QFETCH(bool, hotspotUp);
        QFETCH(bool, allowed);

        HotspotNetwork hotspot;
        hotspot.subnet = "10.42.0.1/24";
        if(hotspotUp)
        {
            hotspot.interfaceName = "wlan0";
            QNetworkAddressEntry ipv4;
            ipv4.setIp(QHostAddress("10.42.0.1"));
            ipv4.setPrefixLength(24);
            QNetworkAddressEntry ula;
            ula.setIp(QHostAddress("fd42::1"));
            ula.setPrefixLength(64);
            QNetworkAddressEntry linkLocal;
            linkLocal.setIp(QHostAddress("fe80::1"));
            linkLocal.setPrefixLength(64);
            hotspot.addresses = {ipv4, ula, linkLocal};
        }
        QCOMPARE(f1x::openauto::autoapp::api::clientAllowed(QHostAddress(peer), blockHotspot, hotspot), allowed);
    }

    // openhu#23: token, origins
    void tokenHelpers()
    {
        const QString token = api::generateToken();
        QCOMPARE(token.size(), 43);  // 32 bytes base64url
        QVERIFY(QRegularExpression("^[A-Za-z0-9_-]+$").match(token).hasMatch());
        QVERIFY(token != api::generateToken());

        QVERIFY(api::tokenMatches(token, token));
        QVERIFY(!api::tokenMatches(token.left(42), token));
        QVERIFY(!api::tokenMatches(token + "x", token));
        QVERIFY(!api::tokenMatches("", token));
        QVERIFY(!api::tokenMatches("", ""));  // no token configured: nobody logs in

        QNetworkRequest request(QUrl("ws://h:5520/?token=abc%2Bd"));
        QCOMPARE(api::handshakeToken(request), QString("abc+d"));
        request.setRawHeader("Authorization", "Bearer  xyz ");
        QCOMPARE(api::handshakeToken(request), QString("xyz"));
        QCOMPARE(api::handshakeToken(QNetworkRequest(QUrl("ws://h:5520/"))), QString());

        QCOMPARE(api::parseOrigins(" HTTP://A.example:8080/ ,, http://b.example"),
                 QStringList({"http://a.example:8080", "http://b.example"}));
        QVERIFY(api::originAllowed("", {}));  // not a browser
        QVERIFY(api::originAllowed("http://evil.example", {"*"}));
        QVERIFY(api::originAllowed("http://A.example:8080", api::parseOrigins("http://a.example:8080")));
        QVERIFY(!api::originAllowed("http://evil.example", api::parseOrigins("http://a.example:8080")));
        QVERIFY(!api::originAllowed("http://evil.example", {}));

        QVERIFY(api::isLoopbackPeer(QHostAddress("::ffff:127.0.0.1")));
        QVERIFY(api::isLoopbackPeer(QHostAddress("::1")));
        QVERIFY(!api::isLoopbackPeer(QHostAddress("192.168.1.20")));
    }

    // Browsers send an Origin header: also on this device they need the token and get no secrets
    void browserNeedsToken()
    {
        ApiServer server;
        server.setToken(cToken);
        server.registerMethod("test.echo", [](const QVariantMap& params) -> QVariant { return params; });
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));
        EventBus::instance().publish("test.auth.state", {{"value", 1}});

        Client browser(port, "http://evil.example");
        QVERIFY(browser.connected());
        QVERIFY(browser.call("test.echo").value("error").toString().contains("authentication required"));
        browser.send({{"type", "subscribe"}, {"topics", QStringList{"*"}}});
        QVERIFY(browser.next().value("error").toString().contains("authentication required"));
        // no events without login
        EventBus::instance().publish("test.auth.state", {{"value", 2}});
        QVERIFY(browser.idle(200));

        browser.send({{"type", "auth"}, {"token", "wrong"}});
        QCOMPARE(browser.next().value("error").toString(), QString("invalid token"));
        QVERIFY(browser.closedWith(QWebSocketProtocol::CloseCodePolicyViolated));

        Client loggedIn(port, "http://evil.example");
        QVERIFY(loggedIn.connected());
        loggedIn.send({{"type", "auth"}, {"token", cToken}});
        const QVariantMap auth = loggedIn.next();
        QCOMPARE(auth.value("type").toString(), QString("auth"));
        QVERIFY(auth.value("authenticated").toBool());
        QCOMPARE(loggedIn.call("test.echo").value(ApiServer::cLocal).toBool(), false);
        loggedIn.send({{"type", "subscribe"}, {"topics", QStringList{"test.auth.*"}}});
        QCOMPARE(loggedIn.next().value("data").toMap().value("value").toInt(), 2);

        // the token in the handshake
        Client query(port, "http://evil.example", "?token=" + cToken);
        QVERIFY(query.connected());
        QCOMPARE(query.call("test.echo").value(ApiServer::cLocal).toBool(), false);
        Client header(port, "http://evil.example", QString(), "Bearer " + cToken.toLatin1());
        QVERIFY(header.connected());
        QVERIFY(!header.call("test.echo").contains("error"));
        Client wrong(port, "http://evil.example", "?token=wrong");
        QVERIFY(wrong.closedWith(QWebSocketProtocol::CloseCodePolicyViolated));

        // plugins and tools on this device: no Origin, no token
        Client plugin(port);
        QVERIFY(plugin.connected());
        QCOMPARE(plugin.call("test.echo").value(ApiServer::cLocal).toBool(), true);
        plugin.send({{"type", "auth"}, {"token", "anything"}});
        QVERIFY(plugin.next().value("authenticated").toBool());

        // a new token ends the logins with the old one, local clients stay
        server.setToken("another-token");
        QVERIFY(loggedIn.closedWith(QWebSocketProtocol::CloseCodePolicyViolated));
        QVERIFY(query.closedWith(QWebSocketProtocol::CloseCodePolicyViolated));
        QVERIFY(!plugin.call("test.echo").contains("error"));
    }

    void allowedOrigins()
    {
        ApiServer server;
        server.setToken(cToken);
        server.setAllowedOrigins(api::parseOrigins("http://dashboard.local:8080"));
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));

        Client evil(port, "http://evil.example", "?token=" + cToken);
        QVERIFY(!evil.connected());
        Client allowed(port, "http://dashboard.local:8080", "?token=" + cToken);
        QVERIFY(allowed.connected());
        QVERIFY(!allowed.call("system.methods").value("error").toString().contains("authentication"));
        Client plugin(port);
        QVERIFY(plugin.connected());
    }

    // A client from the network: the first non-loopback address of this machine
    void networkClientNeedsToken()
    {
        QHostAddress address;
        for(const QHostAddress& candidate: QNetworkInterface::allAddresses())
        {
            if(candidate.protocol() == QAbstractSocket::IPv4Protocol && !candidate.isLoopback())
            {
                address = candidate;
                break;
            }
        }
        if(address.isNull())
        {
            QSKIP("no network address on this machine");
        }

        ApiServer server;
        server.setToken(cToken);
        server.registerMethod("test.echo", [](const QVariantMap& params) -> QVariant { return params; });
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::AnyIPv4, port));

        Client client(port, QString(), QString(), QByteArray(), address.toString());
        QVERIFY(client.connected());
        QVERIFY(client.call("test.echo").value("error").toString().contains("authentication required"));
        client.send({{"type", "auth"}, {"token", cToken}});
        QVERIFY(client.next().value("authenticated").toBool());
        QCOMPARE(client.call("test.echo").value(ApiServer::cLocal).toBool(), false);

        Client wrong(port, QString(), "?token=wrong", QByteArray(), address.toString());
        QVERIFY(wrong.closedWith(QWebSocketProtocol::CloseCodePolicyViolated));
    }

    // "::" listens on IPv4 and IPv6, a QHostAddress("::") alone would be IPv6 only
    void listenAddress()
    {
        using f1x::openauto::autoapp::api::listenAddress;
        QCOMPARE(listenAddress("::"), QHostAddress(QHostAddress::Any));
        QCOMPARE(listenAddress("0.0.0.0"), QHostAddress(QHostAddress::AnyIPv4));
        QCOMPARE(listenAddress("127.0.0.1"), QHostAddress(QHostAddress::LocalHost));

        const quint16 port = freePort();
        ApiServer server;
        QVERIFY(server.listen(listenAddress("::"), port));
        QTcpSocket ipv4;
        ipv4.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY2(ipv4.waitForConnected(3000), "\"::\" does not accept IPv4");
    }
};

QTEST_MAIN(TestApi)
#include "test_api.moc"
