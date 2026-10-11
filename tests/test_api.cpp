#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QWebSocket>
#include <QtTest>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Api/CoreMethods.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include "TestHome.hpp"

using f1x::openauto::autoapp::api::ApiError;
using f1x::openauto::autoapp::api::ApiServer;
using f1x::openauto::autoapp::audio::VolumeController;
using f1x::openauto::autoapp::core::EventBus;
using f1x::openauto::autoapp::input::ActionDispatcher;
using f1x::openauto::autoapp::shell::ShellController;

namespace
{

quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

// WebSocket client that keeps the received messages as JSON objects
class Client: public QObject
{
public:
    explicit Client(quint16 port)
    {
        connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString& message) {
            messages_.append(QJsonDocument::fromJson(message.toUtf8()).object().toVariantMap());
        });
        socket_.open(QUrl(QString("ws://127.0.0.1:%1").arg(port)));
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
};

QTEST_MAIN(TestApi)
#include "test_api.moc"
