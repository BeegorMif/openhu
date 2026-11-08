// What plays now (media.current): the metadata and status of the source that started last, also when the sources
// publish in between (the home page showed the last publisher's track after a switch).
#include <QtTest>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Service/NowPlaying.hpp>

using f1x::openauto::autoapp::core::EventBus;
using f1x::openauto::autoapp::service::NowPlaying;

namespace
{

QVariantMap metadata(const QString& source, const QString& title)
{
    return {{"source", source}, {"title", title}};
}

QVariantMap status(const QString& source, const QString& state)
{
    return {{"source", source}, {"state", state}};
}

QString title(const QVariantMap& current)
{
    return current.value("metadata").toMap().value("title").toString();
}

QString state(const QVariantMap& current)
{
    return current.value("status").toMap().value("state").toString();
}

}

class TestNowPlaying: public QObject
{
    Q_OBJECT

private slots:
    void switchesWithTheSource()
    {
        NowPlaying now;
        QVERIFY(now.current().isEmpty());

        // Spotify plays (its metadata before any status: nothing to show yet)
        QVERIFY(!now.update("media.metadata", metadata("androidauto", "Recall")));
        QVERIFY(now.update("media.status", status("androidauto", "playing")));
        QCOMPARE(now.current().value("source").toString(), QString("androidauto"));
        QCOMPARE(title(now.current()), QString("Recall"));

        // the music player published its (restored) track earlier... and starts: its track, not Spotify's
        QVERIFY(!now.update("media.metadata", metadata("music", "Coast Road")));
        QVERIFY(now.update("media.status", status("music", "playing")));
        QCOMPARE(now.current().value("source").toString(), QString("music"));
        QCOMPARE(title(now.current()), QString("Coast Road"));
        QCOMPARE(state(now.current()), QString("playing"));

        // the phone reports paused afterwards: still the music
        QVERIFY(!now.update("media.status", status("androidauto", "paused")));
        QVERIFY(!now.update("media.metadata", metadata("androidauto", "Other")));
        QCOMPARE(title(now.current()), QString("Coast Road"));

        // Spotify again: its last track right away
        QVERIFY(now.update("media.status", status("androidauto", "playing")));
        QCOMPARE(title(now.current()), QString("Other"));
        QCOMPARE(state(now.current()), QString("playing"));

        // the music pauses itself: stays Spotify
        QVERIFY(!now.update("media.status", status("music", "paused")));
        QVERIFY(!now.update("input.action", {{"action", "media.next"}}));
    }

    void published()
    {
        EventBus::instance().publish("media.metadata", metadata("radio", "NDR 2"));
        EventBus::instance().publish("media.status", status("radio", "playing"));
        NowPlaying now;
        // what was there before
        QCOMPARE(title(EventBus::instance().last("media.current")), QString("NDR 2"));
        EventBus::instance().publish("media.metadata", metadata("androidauto", "Recall"));
        QCOMPARE(title(EventBus::instance().last("media.current")), QString("NDR 2"));
        EventBus::instance().publish("media.status", status("androidauto", "playing"));
        QCOMPARE(title(EventBus::instance().last("media.current")), QString("Recall"));
        QCOMPARE(EventBus::instance().last("media.current").value("source").toString(), QString("androidauto"));
    }
};

QTEST_GUILESS_MAIN(TestNowPlaying)
#include "test_nowplaying.moc"
