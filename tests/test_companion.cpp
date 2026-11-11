// The companion app (openhu#158): the Bluetooth bridge into the API (with a socket pair instead of RFCOMM: this side
// plays the app), the filter of what goes to the phone, and the helpers that recognize the app in Android Auto.

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QElapsedTimer>
#include <QtTest>
#include <functional>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Api/CompanionBridge.hpp>
#include <f1x/openauto/autoapp/Core/Companion.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>

using f1x::openauto::autoapp::api::ApiServer;
using f1x::openauto::autoapp::api::CompanionBridge;
using f1x::openauto::autoapp::core::EventBus;
namespace core = f1x::openauto::autoapp::core;

namespace
{

const QString cToken = QStringLiteral("companion-test-token");

quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

// The app's end of the link: JSON lines over the other socket of the pair
class Phone
{
public:
    explicit Phone(int fd): fd_(fd) { ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK); }
    ~Phone() { this->close(); }

    void close()
    {
        if(fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

    void send(const QVariantMap& message)
    {
        const QByteArray line = QJsonDocument(QJsonObject::fromVariantMap(message)).toJson(QJsonDocument::Compact) + '\n';
        QCOMPARE(::write(fd_, line.constData(), size_t(line.size())), ssize_t(line.size()));
    }

    // the next line (waits up to 5 s), empty on a timeout
    QVariantMap next()
    {
        QVariantMap message;
        QTest::qWaitFor([&]() {
            this->read();
            const qsizetype newline = input_.indexOf('\n');
            if(newline < 0)
            {
                return false;
            }
            message = QJsonDocument::fromJson(input_.left(newline)).object().toVariantMap();
            input_.remove(0, newline + 1);
            return true;
        }, 5000);
        return message;
    }

    // the next message that fits, skipping others; empty when none comes
    QVariantMap find(const std::function<bool(const QVariantMap&)>& fits)
    {
        for(int i = 0; i < 20; ++i)
        {
            const QVariantMap message = this->next();
            if(message.isEmpty() || fits(message))
            {
                return message;
            }
        }
        return QVariantMap();
    }

    QVariantMap event(const QString& topic)
    {
        return this->find([&](const QVariantMap& m) { return m.value("type") == "event" && m.value("topic") == topic; })
            .value("data").toMap();
    }

    bool idle(int ms)
    {
        QTest::qWait(ms);
        this->read();
        return input_.isEmpty();
    }

private:
    void read()
    {
        char buffer[65536];
        ssize_t n;
        while((n = ::read(fd_, buffer, sizeof(buffer))) > 0)
        {
            input_.append(buffer, qsizetype(n));
        }
    }

    int fd_;
    QByteArray input_;
};

}

class TestCompanion: public QObject
{
    Q_OBJECT

private slots:
    void recognizesTheApp()
    {
        EventBus::instance().publish("companion.state", {{"connected", true}, {"label", "OpenHU"}});
        QVERIFY(core::isCompanionApp("OpenHU"));
        QVERIFY(!core::isCompanionApp("Spotify"));
        QVERIFY(!core::isCompanionApp(""));
        // the music app is an extension: Android Auto names it, the companion reports its name
        EventBus::instance().publish("companion.state", {{"connected", true}, {"label", "OpenHU"},
                                                          {"mediaLabels", QStringList{"Musik"}}});
        QVERIFY(core::isCompanionApp("Musik"));
        QVERIFY(!core::isCompanionApp("Spotify"));
        EventBus::instance().publish("companion.state", {{"connected", false}});
        QVERIFY(!core::isCompanionApp("OpenHU"));
        QVERIFY(!core::isCompanionApp("Musik"));

        EventBus::instance().publish("androidauto.mediaAudio", {{"playing", true}});
        QVERIFY(core::phoneMusicPlaying());
        EventBus::instance().publish("androidauto.mediaAudio", {{"playing", false}});
        QVERIFY(!core::phoneMusicPlaying());
    }

    void extensionsFromTheManifests()
    {
        const QVariantMap manifest{{"id", "org.openhu.media"}, {"name", "Music"},
                                   {"companion", QVariantMap{{"package", "org.openhu.companion.music"},
                                                             {"topics", QStringList{"music.*"}},
                                                             {"commands", QStringList{"music.command"}}}}};
        const QVariantMap companion = core::companionOf(manifest);
        QCOMPARE(companion.value("package").toString(), QString("org.openhu.companion.music"));
        QCOMPARE(companion.value("name").toString(), QString("Music"));  // the plugin's name by default
        QCOMPARE(companion.value("topics").toStringList(), QStringList{"music.*"});
        QVERIFY(core::companionOf({{"id", "x"}, {"companion", QVariantMap{{"name", "no package"}}}}).isEmpty());
        QVERIFY(core::companionOf({{"id", "x"}}).isEmpty());

        const QVariantList plugins{
            QVariantMap{{"id", "org.openhu.media"}, {"enabled", true}, {"companion", companion}},
            QVariantMap{{"id", "org.openhu.off"}, {"enabled", false}, {"companion", companion}},
            QVariantMap{{"id", "org.openhu.radio"}, {"enabled", true}}};
        const QVariantList extensions = core::companionExtensions(plugins).value("extensions").toList();
        QCOMPARE(extensions.size(), 1);
        QCOMPARE(extensions.first().toMap().value("plugin").toString(), QString("org.openhu.media"));
        QCOMPARE(extensions.first().toMap().value("commands").toStringList(), QStringList{"music.command"});
    }

    void androidAutoMediaStaysOnTheHeadUnit()
    {
        const auto event = [](const QString& topic, const QString& source) {
            return QString::fromUtf8(QJsonDocument(QJsonObject{
                {"type", "event"}, {"topic", topic}, {"data", QJsonObject{{"source", source}}}}).toJson());
        };
        for(const QString topic: {"media.current", "media.metadata", "media.status"})
        {
            QVERIFY(!CompanionBridge::forwardToPhone(event(topic, "androidauto")));
            QVERIFY(CompanionBridge::forwardToPhone(event(topic, "music")));
        }
        QVERIFY(CompanionBridge::forwardToPhone(event("music.state", "androidauto")));
        QVERIFY(CompanionBridge::forwardToPhone(R"({"type":"response","id":1,"result":{}})"));
    }

    void bridge()
    {
        ApiServer server;
        server.setToken(cToken);
        server.registerMethod("test.echo", [](const QVariantMap& params) -> QVariant { return params; });
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));

        int fds[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        CompanionBridge bridge(port, []() { return cToken; });
        QSignalSpy states(&EventBus::instance(), &EventBus::event);
        bridge.adopt("/org/bluez/hci0/dev_test", fds[0]);
        Phone phone(fds[1]);

        // the hello: answered, the app is known
        phone.send({{"type", "hello"}, {"app", "org.openhu.companion"}, {"version", "1.0.0"}, {"label", "OpenHU"},
                    {"protocol", 1}});
        QVERIFY(!phone.find([](const QVariantMap& m) { return m.value("type") == "hello"; }).isEmpty());
        QCOMPARE(EventBus::instance().last("companion.state").value("label").toString(), QString("OpenHU"));
        QVERIFY(core::isCompanionApp("OpenHU"));

        // a request before and after the API connection: the bridge logged the link in with the token
        phone.send({{"type", "request"}, {"id", 7}, {"method", "test.echo"}, {"params", QVariantMap{{"a", 1}}}});
        const QVariantMap response = phone.find([](const QVariantMap& m) { return m.value("type") == "response"; });
        QCOMPARE(response.value("id").toInt(), 7);
        QCOMPARE(response.value("result").toMap().value("a").toInt(), 1);

        // events: Android Auto's media state is not sent
        phone.send({{"type", "subscribe"}, {"topics", QStringList{"media.current", "music.state"}}});
        QTest::qWait(200);
        while(!phone.idle(100)) {}
        EventBus::instance().publish("media.current", {{"source", "androidauto"}, {"metadata", QVariantMap{{"cover", "x"}}}});
        QVERIFY(phone.idle(200));
        EventBus::instance().publish("music.state", {{"state", "playing"}});
        QCOMPARE(phone.event("music.state").value("state").toString(), QString("playing"));

        // the app goes away: known no more
        phone.close();
        QVERIFY(QTest::qWaitFor([]() { return !core::isCompanionApp("OpenHU"); }, 5000));
    }

    // a phone that does not read: OpenHU does not block, the link is dropped
    void phoneThatDoesNotRead()
    {
        ApiServer server;
        server.setToken(cToken);
        const quint16 port = freePort();
        QVERIFY(server.listen(QHostAddress::LocalHost, port));
        int fds[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        CompanionBridge bridge(port, []() { return cToken; });
        bridge.adopt("/org/bluez/hci0/dev_slow", fds[0]);
        Phone phone(fds[1]);
        phone.send({{"type", "hello"}, {"app", "org.openhu.companion"}, {"label", "OpenHU"}});
        phone.send({{"type", "subscribe"}, {"topics", QStringList{"test.big"}}});
        QVERIFY(QTest::qWaitFor([]() { return core::isCompanionApp("OpenHU"); }, 5000));
        QTest::qWait(200);

        const QString big(1024 * 1024, QLatin1Char('x'));
        QElapsedTimer timer;
        timer.start();
        for(int i = 0; i < 12 && core::isCompanionApp("OpenHU"); ++i)
        {
            EventBus::instance().publish("test.big", {{"i", i}, {"data", big}});
            QTest::qWait(50);
        }
        QVERIFY(QTest::qWaitFor([]() { return !core::isCompanionApp("OpenHU"); }, 5000));
        QVERIFY2(timer.elapsed() < 10000, "the bridge blocked");
    }
};

QTEST_GUILESS_MAIN(TestCompanion)
#include "test_companion.moc"
