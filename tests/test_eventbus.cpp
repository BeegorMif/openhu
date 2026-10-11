#include <thread>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/DataUrl.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>

using f1x::openauto::autoapp::core::EventBus;

class TestEventBus: public QObject
{
    Q_OBJECT

private slots:
    void matches_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QString>("topic");
        QTest::addColumn<bool>("expected");

        QTest::newRow("everything") << "*" << "nav.turn" << true;
        QTest::newRow("exact") << "nav.turn" << "nav.turn" << true;
        QTest::newRow("other exact") << "nav.turn" << "nav.distance" << false;
        QTest::newRow("prefix") << "nav.*" << "nav.turn" << true;
        QTest::newRow("prefix nested") << "nav.*" << "nav.turn.icon" << true;
        QTest::newRow("prefix itself") << "nav.*" << "nav" << false;
        QTest::newRow("prefix other") << "nav.*" << "navigation.turn" << false;
        QTest::newRow("no wildcard in the middle") << "n*" << "nav.turn" << false;
    }

    void matches()
    {
        QFETCH(QString, pattern);
        QFETCH(QString, topic);
        QFETCH(bool, expected);
        QCOMPARE(EventBus::matches(pattern, topic), expected);
    }

    void publishKeepsLastValue()
    {
        auto& bus = EventBus::instance();
        QSignalSpy spy(&bus, &EventBus::event);

        bus.publish("test.publish", {{"value", 1}});
        bus.publish("test.publish", {{"value", 2}});

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QString("test.publish"));
        QCOMPARE(bus.last("test.publish").value("value").toInt(), 2);
        QVERIFY(bus.snapshot().contains("test.publish"));
    }

    void emitEventIsNotKept()
    {
        auto& bus = EventBus::instance();
        QSignalSpy spy(&bus, &EventBus::event);

        bus.emitEvent("test.command", {{"cmd", "tune"}});

        QCOMPARE(spy.count(), 1);
        QVERIFY(bus.last("test.command").isEmpty());
        QVERIFY(!bus.snapshot().contains("test.command"));
    }

    void publishFromAnotherThreadArrivesInGuiThread()
    {
        auto& bus = EventBus::instance();
        QThread* receivedIn = nullptr;
        connect(&bus, &EventBus::event, this, [&receivedIn](const QString& topic, const QVariantMap&) {
            if(topic == "test.thread")
            {
                receivedIn = QThread::currentThread();
            }
        });

        std::thread([&bus]() { bus.publish("test.thread", {{"value", 3}}); }).join();

        QTRY_VERIFY(receivedIn != nullptr);
        QCOMPARE(receivedIn, QThread::currentThread());
        QCOMPARE(bus.last("test.thread").value("value").toInt(), 3);
    }

    void dataUrl()
    {
        using f1x::openauto::autoapp::core::imageDataUrl;
        QVERIFY(imageDataUrl("").isEmpty());
        QVERIFY(imageDataUrl("\x89PNG\r\n").startsWith("data:image/png;base64,"));
        QCOMPARE(imageDataUrl("\xff\xd8\xff"), QString("data:image/jpeg;base64,/9j/"));
        QCOMPARE(f1x::openauto::autoapp::core::enumName("TURN_NORMAL_LEFT"), QString("turn_normal_left"));
    }
};

QTEST_GUILESS_MAIN(TestEventBus)
#include "test_eventbus.moc"
