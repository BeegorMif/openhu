// Car local media (openhu#147): media.status / media.metadata of a local source -> the phone's media card, and its
// buttons -> media actions. The values follow what worked with a phone (Fairphone 4, Android Auto 17.9, see aasdk's
// CarLocalMediaProtocol.ut.cpp).
#include <QBuffer>
#include <QImage>
#include <QtTest>
#include <f1x/openauto/autoapp/Plugins/AndroidAutoProviders.hpp>
#include <f1x/openauto/autoapp/Service/CarLocalMedia/CarLocalMediaMapping.hpp>

namespace lm = f1x::openauto::autoapp::service::carlocalmedia;
namespace msg = aap_protobuf::service::carlocalmedia::message;
namespace plugins = f1x::openauto::autoapp::plugins;

namespace
{

QString dataUrl(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

QVariantMap status(const QString& state, double position = 0)
{
    return {{"source", "music"}, {"state", state}, {"position", position}};
}

}

class TestCarLocalMedia: public QObject
{
    Q_OBJECT

private slots:
    void status_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<int>("expected");
        QTest::newRow("playing") << "playing" << int(msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_PLAYING);
        QTest::newRow("paused") << "paused" << int(msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_PAUSED);
        QTest::newRow("stopped") << "stopped" << int(msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_STOPPED);
        QTest::newRow("unknown") << "" << int(msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_STOPPED);
    }

    void status()
    {
        QFETCH(QString, state);
        QFETCH(int, expected);
        const auto result = lm::playbackStatus(::status(state, 83.6), "Music");
        QCOMPARE(int(result.playback_state()), expected);
        QCOMPARE(result.media_source(), std::string("Music"));
        QCOMPARE(result.playback_position(), 84);
        QCOMPARE(result.actions_size(), 4);
        QCOMPARE(int(result.actions(0)), int(msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PLAY));
        QCOMPARE(int(result.actions(3)), int(msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_NEXT));
        // the phone's values (the spike): playing is 2
        QCOMPARE(int(msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_PLAYING), 2);
    }

    void metadata()
    {
        const auto result = lm::playbackMetadata({{"source", "music"}, {"title", "Song"}, {"artist", "Artist"},
                                                  {"album", "Album"}, {"duration", 201.4}, {"cover", dataUrl(64, 64)}});
        QCOMPARE(result.song(), std::string("Song"));
        QCOMPARE(result.artist(), std::string("Artist"));
        QCOMPARE(result.album(), std::string("Album"));
        QCOMPARE(result.duration_seconds(), 201u);
        QVERIFY(result.has_album_art());
        QImage image;
        QVERIFY(image.loadFromData(QByteArray::fromStdString(result.album_art())));
        QCOMPARE(image.size(), QSize(64, 64));

        // no duration, no cover
        const auto bare = lm::playbackMetadata({{"title", "Song"}, {"cover", "file:///cover.jpg"}});
        QVERIFY(!bare.has_duration_seconds());
        QVERIFY(!bare.has_album_art());
    }

    void cover()
    {
        QImage image;
        QVERIFY(image.loadFromData(QByteArray::fromStdString(lm::coverBytes(dataUrl(1200, 600)))));
        QCOMPARE(image.size(), QSize(lm::cMaxCoverSize, lm::cMaxCoverSize / 2));
        QVERIFY(lm::coverBytes("data:image/png;base64,bm8gaW1hZ2U=").empty());
        QVERIFY(lm::coverBytes("data:text/plain,hello").empty());
        QVERIFY(lm::coverBytes("").empty());
    }

    void actions()
    {
        QCOMPARE(lm::mediaAction(msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PLAY), QString("media.play"));
        QCOMPARE(lm::mediaAction(msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PAUSE), QString("media.pause"));
        QCOMPARE(lm::mediaAction(msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_STOP), QString("media.pause"));
        QCOMPARE(lm::mediaAction(msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PREVIOUS), QString("media.previous"));
        QCOMPARE(lm::mediaAction(msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_NEXT), QString("media.next"));
    }

    void currentSource()
    {
        lm::LocalMediaState state;
        // metadata before any status: kept, not shown yet
        auto changes = state.onMetadata("music", {{"title", "Song"}});
        QVERIFY(!changes.status && !changes.metadata);

        // the first source that reports is shown, with its metadata
        changes = state.onStatus("music", "Music", ::status("paused"));
        QVERIFY(changes.status && changes.metadata);
        QCOMPARE(state.current(), QString("music"));
        QCOMPARE(state.name(), QString("Music"));
        QCOMPARE(state.metadata().value("title").toString(), QString("Song"));

        // another source that is paused does not take over
        changes = state.onStatus("other", "Other", {{"state", "paused"}});
        QVERIFY(!changes.status && !changes.metadata);
        changes = state.onMetadata("other", {{"title", "Other song"}});
        QVERIFY(!changes.metadata);

        // it does once it plays
        changes = state.onStatus("other", "Other", {{"state", "playing"}});
        QVERIFY(changes.status && changes.metadata);
        QCOMPARE(state.current(), QString("other"));
        QCOMPARE(state.metadata().value("title").toString(), QString("Other song"));

        // updates of the shown source
        QVERIFY(state.onStatus("other", "Other", {{"state", "paused"}}).status);
        QVERIFY(state.onMetadata("other", {{"title", "Next"}}).metadata);
        QVERIFY(!state.onStatus("", "", {{"state", "playing"}}).status);
    }

    void sources()
    {
        const QVariantMap state{{"plugins", QVariantList{
            QVariantMap{{"id", "org.openhu.media"}, {"enabled", true}, {"compatible", true},
                        {"androidAuto", QVariantMap{{"services", QStringList{"localMedia"}}}}},
            QVariantMap{{"id", "org.openhu.radio"}, {"enabled", true}, {"compatible", true}}}}};
        QVERIFY(plugins::androidAutoDeclared(state, "services", "localMedia"));
        QVERIFY(plugins::androidAutoServiceSourceAllowed(state, "localMedia", "plugin:org.openhu.media"));
        QVERIFY(!plugins::androidAutoServiceSourceAllowed(state, "localMedia", "plugin:org.openhu.radio"));
        QVERIFY(plugins::androidAutoServiceNames().contains("localMedia"));
    }
};

QTEST_GUILESS_MAIN(TestCarLocalMedia)
#include "test_carlocalmedia.moc"
