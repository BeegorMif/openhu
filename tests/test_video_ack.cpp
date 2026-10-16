// Video flow control (openhu#22), simulated without a phone: a simulated phone encodes H.264 at 30 fps and sends it
// to the real VideoMediaSinkService with the real FfmpegVideoOutput, whose decoder is slowed down with
// OPENHU_DEBUG_DECODE_DELAY_MS. Like a phone (open-android-auto, docs/interactions/04-channel-lifecycle.md) it keeps
// at most max_unacked messages unacknowledged; a frame it cannot send is skipped by its encoder, so the stream it
// sends stays decodable. The tests compare acks at once (before #22) with acks when the decoder takes the message.
#include <QElapsedTimer>
#include <QVideoSink>
#include <QtTest>
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <boost/asio.hpp>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/autoapp/Projection/FfmpegVideoOutput.hpp>
// moc cannot parse all aasdk protobuf headers
#ifndef Q_MOC_RUN
#include <f1x/openauto/autoapp/Service/MediaSink/VideoMediaSinkService.hpp>
#endif
#include "TestHome.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

using f1x::openauto::autoapp::configuration::Configuration;
using f1x::openauto::autoapp::projection::FfmpegVideoOutput;
using f1x::openauto::autoapp::service::mediasink::VideoAckSettings;
using f1x::openauto::autoapp::service::mediasink::VideoMediaSinkService;

namespace
{
constexpr int cWidth = 320;
constexpr int cHeight = 240;
constexpr int cSession = 7;

using Clock = std::chrono::steady_clock;

// The phone's encoder: one frame at a time, so frames the phone skips are never encoded (references stay intact).
// The first frame comes as two messages, SPS/PPS and the IDR picture, like the phone sends them.
class PhoneEncoder
{
public:
    PhoneEncoder()
    {
        const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
        if(codec == nullptr)
        {
            return;
        }
        context_ = avcodec_alloc_context3(codec);
        context_->width = cWidth;
        context_->height = cHeight;
        context_->pix_fmt = AV_PIX_FMT_YUV420P;
        context_->time_base = {1, 30};
        context_->framerate = {30, 1};
        context_->gop_size = 600;  // like Android Auto: IDR frames are rare
        context_->max_b_frames = 0;
        av_opt_set(context_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(context_->priv_data, "tune", "zerolatency", 0);
        av_opt_set(context_->priv_data, "forced-idr", "1", 0);
        if(avcodec_open2(context_, codec, nullptr) < 0)
        {
            avcodec_free_context(&context_);
            return;
        }
        frame_ = av_frame_alloc();
        frame_->format = context_->pix_fmt;
        frame_->width = cWidth;
        frame_->height = cHeight;
        av_frame_get_buffer(frame_, 0);
        packet_ = av_packet_alloc();
    }
    ~PhoneEncoder()
    {
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&context_);
    }
    bool valid() const { return context_ != nullptr; }

    // The messages of the next frame
    QList<QByteArray> next(bool idr = false)
    {
        QList<QByteArray> messages;
        av_frame_make_writable(frame_);
        // a moving pattern, and a band of noise that changes with every frame: P-frames of a few KB, like a phone's
        uint32_t noise = 12345u + static_cast<uint32_t>(index_) * 2654435761u;
        for(int y = 0; y < cHeight; ++y)
        {
            for(int x = 0; x < cWidth; ++x)
            {
                uint8_t value = uint8_t(40 + ((x / 16 + y / 16 + index_) % 4) * 20);
                if(y >= 64 && y < 96)
                {
                    noise = noise * 1664525u + 1013904223u;
                    value = uint8_t(16 + (noise >> 24) % 200);
                }
                frame_->data[0][y * frame_->linesize[0] + x] = value;
            }
        }
        for(int plane = 1; plane < 3; ++plane)
        {
            for(int y = 0; y < cHeight / 2; ++y)
            {
                memset(frame_->data[plane] + y * frame_->linesize[plane], 128, cWidth / 2);
            }
        }
        frame_->pts = index_++;
        frame_->pict_type = idr ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
        avcodec_send_frame(context_, frame_);
        while(avcodec_receive_packet(context_, packet_) == 0)
        {
            QByteArray data(reinterpret_cast<const char*>(packet_->data), packet_->size);
            av_packet_unref(packet_);
            const int idrAt = nalAt(data, 5);
            if(idrAt > 0)
            {
                messages.append(data.left(idrAt));  // SPS/PPS (and SEI) as a message of their own
                data = data.mid(idrAt);
            }
            messages.append(data);
        }
        return messages;
    }

private:
    // Start (incl. a leading zero of a 4-byte start code) of the first NAL unit of the type, -1 if none
    static int nalAt(const QByteArray& data, int type)
    {
        for(int i = 0; i + 3 < data.size(); ++i)
        {
            if(data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1 && (data[i + 3] & 0x1f) == type)
            {
                return i > 0 && data[i - 1] == 0 ? i - 1 : i;
            }
        }
        return -1;
    }

    AVCodecContext* context_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    int index_ = 0;
};

bool containsPicture(const QByteArray& data)
{
    for(int i = 0; i + 3 < data.size(); ++i)
    {
        const int type = data[i + 3] & 0x1f;
        if(data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1 && type >= 1 && type <= 5)
        {
            return true;
        }
    }
    return false;
}

// The phone's side of the channel: what the service sends; every send succeeds at once
class FakeChannel: public aasdk::channel::mediasink::video::IVideoMediaSinkService
{
public:
    std::atomic<int> acks{0};
    std::atomic<int> wrongSession{0};
    std::atomic<uint32_t> maxUnacked{0};

    aasdk::messenger::ChannelId getId() const override { return aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO; }
    void send(aasdk::messenger::Message::Pointer, aasdk::channel::SendPromise::Pointer promise) override
    {
        promise->resolve();
    }
    void receive(aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler::Pointer) override {}
    void sendChannelOpenResponse(const aap_protobuf::service::control::message::ChannelOpenResponse&,
                                 aasdk::channel::SendPromise::Pointer promise) override { promise->resolve(); }
    void sendChannelSetupResponse(const aap_protobuf::service::media::shared::message::Config& config,
                                  aasdk::channel::SendPromise::Pointer promise) override
    {
        maxUnacked = config.max_unacked();
        promise->resolve();
    }
    void sendMediaAckIndication(const aap_protobuf::service::media::source::message::Ack& ack,
                                aasdk::channel::SendPromise::Pointer promise) override
    {
        if(ack.session_id() != cSession || ack.ack() != 1)
        {
            ++wrongSession;
        }
        ++acks;
        promise->resolve();
    }
    void sendVideoFocusIndication(const aap_protobuf::service::media::video::message::VideoFocusNotification&,
                                  aasdk::channel::SendPromise::Pointer promise) override { promise->resolve(); }
    void sendUpdateUiConfigRequest(const aap_protobuf::service::control::message::UpdateUiConfigRequest&,
                                   aasdk::channel::SendPromise::Pointer promise) override { promise->resolve(); }
};

// Environment variables for one test, restored afterwards
class Environment
{
public:
    void set(const char* name, const QByteArray& value)
    {
        if(!saved_.contains(name))
        {
            saved_.insert(name, qEnvironmentVariableIsSet(name) ? std::optional<QByteArray>(qgetenv(name)) : std::nullopt);
        }
        qputenv(name, value);
    }
    ~Environment()
    {
        for(auto it = saved_.begin(); it != saved_.end(); ++it)
        {
            if(it.value())
            {
                qputenv(it.key().constData(), *it.value());
            }
            else
            {
                qunsetenv(it.key().constData());
            }
        }
    }

private:
    QMap<QByteArray, std::optional<QByteArray>> saved_;
};

// Service and output of one Android Auto session, the service's io_service in a thread of its own
class Session
{
    // first: destroyed last
    boost::asio::io_service io_;
    std::optional<boost::asio::io_service::work> work_;
    std::thread thread_;
    std::shared_ptr<Configuration> configuration_ = std::make_shared<Configuration>();
    QVideoSink sink_;

public:
    Session()
        : work_(std::in_place, io_)
        , thread_([this]() { io_.run(); })
    {
        configuration_->setVideoDecoder("software");
        channel = std::make_shared<FakeChannel>();
        output = std::make_shared<FfmpegVideoOutput>(configuration_, &sink_);
        service = std::make_shared<VideoMediaSinkService>(io_, channel, output);
        service->start();
        service->onMediaChannelSetupRequest(aap_protobuf::service::media::shared::message::Setup());
        aap_protobuf::service::media::shared::message::Start start;
        start.set_session_id(cSession);
        start.set_configuration_index(0);
        service->onMediaChannelStartIndication(start);
        QTRY_VERIFY(!output->decoderName().empty());
    }
    ~Session()
    {
        // stops the output (joins the decoder thread) and the ack timer; run() returns once nothing is left
        service->stop();
        work_.reset();
        thread_.join();
    }

    void send(const QByteArray& data)
    {
        service->onMediaWithTimestampIndication(
            0, aasdk::common::DataConstBuffer(reinterpret_cast<const uint8_t*>(data.constData()), data.size()));
    }

    std::shared_ptr<FakeChannel> channel;
    std::shared_ptr<FfmpegVideoOutput> output;
    std::shared_ptr<VideoMediaSinkService> service;
};

struct PhoneResult
{
    int messages = 0;
    int pictures = 0;
    int skipped = 0;      // frames the encoder skipped: the phone waited for acks
    double ackDelayAvgMs = 0;
    double ackDelayMaxMs = 0;
};

// A phone that wants to send 'fps' frames per second for 'durationMs'. With honorMaxUnacked it sends a message only
// while fewer than max_unacked are unacknowledged and skips the frame of a tick while it waits; without, it sends
// every frame at once (a phone that ignores the acks). idrAtMs >= 0: one IDR frame at that time.
PhoneResult runPhone(Session& session, PhoneEncoder& encoder, int durationMs, bool honorMaxUnacked, int idrAtMs = -1,
                     int fps = 30)
{
    PhoneResult result;
    std::deque<QByteArray> outbox;
    std::deque<Clock::time_point> sentAt;  // of the unacknowledged messages, oldest first
    int acked = 0;
    double delaySum = 0;
    int delays = 0;
    const int maxUnacked = static_cast<int>(session.channel->maxUnacked.load());
    QElapsedTimer timer;
    timer.start();
    int tick = 0;
    bool idrSent = false;

    auto collectAcks = [&]() {
        for(const int acks = session.channel->acks; acked < acks && !sentAt.empty(); ++acked)
        {
            const double delay = std::chrono::duration<double, std::milli>(Clock::now() - sentAt.front()).count();
            sentAt.pop_front();
            delaySum += delay;
            ++delays;
            result.ackDelayMaxMs = std::max(result.ackDelayMaxMs, delay);
        }
    };
    auto sendFromOutbox = [&]() {
        while(!outbox.empty() && (!honorMaxUnacked || static_cast<int>(sentAt.size()) < maxUnacked))
        {
            const QByteArray data = outbox.front();
            outbox.pop_front();
            sentAt.push_back(Clock::now());
            ++result.messages;
            result.pictures += containsPicture(data) ? 1 : 0;
            session.send(data);
            collectAcks();
        }
    };

    while(timer.elapsed() < durationMs)
    {
        if(timer.elapsed() >= tick * 1000 / fps)
        {
            ++tick;
            collectAcks();
            if(honorMaxUnacked && (!outbox.empty() || static_cast<int>(sentAt.size()) >= maxUnacked))
            {
                ++result.skipped;  // still waiting for acks: the encoder skips this frame
            }
            else
            {
                const bool idr = idrAtMs >= 0 && !idrSent && timer.elapsed() >= idrAtMs;
                idrSent = idrSent || idr;
                for(const auto& message: encoder.next(idr))
                {
                    outbox.push_back(message);
                }
            }
        }
        collectAcks();
        sendFromOutbox();
        QTest::qWait(1);
    }
    // until everything sent is acknowledged
    QElapsedTimer drain;
    drain.start();
    while(!sentAt.empty() && drain.elapsed() < 5000)
    {
        collectAcks();
        QTest::qWait(5);
    }
    result.ackDelayAvgMs = delays > 0 ? delaySum / delays : 0;
    return result;
}

void report(const char* name, const PhoneResult& phone, const FfmpegVideoOutput::Counters& output)
{
    qInfo().nospace() << name << ": phone sent " << phone.pictures << " pictures (" << phone.messages
                      << " messages), skipped " << phone.skipped << " frames; decoder: " << output.decodedFrames
                      << " decoded, " << output.droppedMessages << " dropped, up to " << output.maxQueued
                      << " queued; ack delay " << phone.ackDelayAvgMs << " ms on average, " << phone.ackDelayMaxMs
                      << " ms max";
}
}

class TestVideoAck: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if(!PhoneEncoder().valid())
        {
            QSKIP("FFmpeg has no libx264 encoder to create the test stream");
        }
    }

    void settingsFromEnvironment()
    {
        Environment environment;
        environment.set("OPENHU_VIDEO_ACK", "");
        environment.set("OPENHU_VIDEO_MAX_UNACKED", "");
        environment.set("OPENHU_VIDEO_ACK_MAX_DELAY_MS", "");
        auto settings = VideoAckSettings::fromEnvironment();
        QVERIFY(settings.deferred);
        QCOMPARE(settings.maxUnacked, 1u);
        QCOMPARE(settings.maxDelayMs, 500);

        environment.set("OPENHU_VIDEO_ACK", "immediate");
        environment.set("OPENHU_VIDEO_MAX_UNACKED", "3");
        environment.set("OPENHU_VIDEO_ACK_MAX_DELAY_MS", "250");
        settings = VideoAckSettings::fromEnvironment();
        QVERIFY(!settings.deferred);
        QCOMPARE(settings.maxUnacked, 3u);
        QCOMPARE(settings.maxDelayMs, 250);

        environment.set("OPENHU_VIDEO_MAX_UNACKED", "0");
        environment.set("OPENHU_VIDEO_ACK_MAX_DELAY_MS", "soon");
        settings = VideoAckSettings::fromEnvironment();
        QCOMPARE(settings.maxUnacked, 1u);
        QCOMPARE(settings.maxDelayMs, 500);
    }

    // Before #22: acks at once, the phone never waits, the queue overflows and everything up to the next IDR frame
    // is dropped: the picture freezes (the phone sends IDR frames rarely)
    void immediateAcksOverflowAndFreeze()
    {
        TestHome home;
        Environment environment;
        environment.set("OPENHU_VIDEO_ACK", "immediate");
        environment.set("OPENHU_DEBUG_DECODE_DELAY_MS", "100");  // 10 fps for a 30 fps phone
        PhoneEncoder encoder;
        Session session;
        session.output->setMaxQueuedBytes(64 * 1024);  // the 8 MB of a real session would take minutes here

        const auto phone = runPhone(session, encoder, 3000, true);
        QTRY_COMPARE_WITH_TIMEOUT(session.output->counters().decodedFrames + session.output->counters().droppedMessages,
                                  phone.pictures, 10000);
        const auto counters = session.output->counters();
        report("acks at once", phone, counters);

        QCOMPARE(phone.skipped, 0);                 // the phone never waited
        QVERIFY(phone.pictures >= 70);
        QVERIFY(counters.droppedMessages > 0);      // overflow
        QVERIFY(counters.decodedFrames < phone.pictures / 2);
        QVERIFY(counters.maxQueued >= 8);              // ~1 s of backlog before the overflow
        QCOMPARE(session.channel->acks.load(), phone.messages);
        QCOMPARE(session.channel->wrongSession.load(), 0);

        // frozen: no new picture until the phone sends an IDR frame
        const int decoded = counters.decodedFrames;
        const auto more = runPhone(session, encoder, 500, true);
        QTest::qWait(300);
        QCOMPARE(session.output->counters().decodedFrames, decoded);
        QVERIFY(more.pictures > 0);
    }

    // #22: acks when the decoder takes the message: the phone waits and skips frames, nothing is dropped, every
    // frame it sent is decoded, the queue holds at most max_unacked messages
    void deferredAcksThrottleThePhone()
    {
        TestHome home;
        Environment environment;
        environment.set("OPENHU_VIDEO_ACK", "");
        environment.set("OPENHU_DEBUG_DECODE_DELAY_MS", "100");
        PhoneEncoder encoder;
        Session session;
        session.output->setMaxQueuedBytes(64 * 1024);
        QCOMPARE(session.channel->maxUnacked.load(), 1u);

        const auto phone = runPhone(session, encoder, 3000, true);
        QTRY_COMPARE_WITH_TIMEOUT(session.output->counters().decodedFrames, phone.pictures, 5000);
        const auto counters = session.output->counters();
        report("acks when decoded", phone, counters);

        QCOMPARE(counters.droppedMessages, 0);
        QVERIFY(phone.skipped > 30);                // the phone waited
        QVERIFY(phone.pictures >= 15);              // ~10 fps get through
        QVERIFY(counters.maxQueued <= 1);           // bounded by max_unacked
        QCOMPARE(session.channel->acks.load(), phone.messages);
        QCOMPARE(session.channel->wrongSession.load(), 0);
        QVERIFY(phone.ackDelayMaxMs >= 50);         // acks wait for the decoder ...
        QVERIFY(phone.ackDelayMaxMs < 400);         // ... but not until the early ack (500 ms)
    }

    // Without a slow decoder the deferred acks cost nothing: all 30 frames per second get through
    void deferredAcksWithAFastDecoder()
    {
        TestHome home;
        Environment environment;
        environment.set("OPENHU_VIDEO_ACK", "");
        environment.set("OPENHU_DEBUG_DECODE_DELAY_MS", "");
        PhoneEncoder encoder;
        Session session;

        const auto phone = runPhone(session, encoder, 2000, true);
        QTRY_COMPARE_WITH_TIMEOUT(session.output->counters().decodedFrames, phone.pictures, 5000);
        const auto counters = session.output->counters();
        report("fast decoder", phone, counters);
        QCOMPARE(counters.droppedMessages, 0);
        QVERIFY(phone.skipped <= 6);
        QVERIFY(phone.pictures >= 45);
    }

    // Safety net: a phone that does not wait for the acks still overflows the queue; the output drops until the next
    // IDR frame as before, every message is acknowledged (the dropped ones too), and the IDR frame brings the picture back
    void phoneIgnoringAcksFallsBackToDropping()
    {
        TestHome home;
        Environment environment;
        environment.set("OPENHU_VIDEO_ACK", "");
        environment.set("OPENHU_DEBUG_DECODE_DELAY_MS", "100");
        PhoneEncoder encoder;
        Session session;
        session.output->setMaxQueuedBytes(64 * 1024);

        const auto phone = runPhone(session, encoder, 3000, false, 2500);
        QTRY_COMPARE_WITH_TIMEOUT(session.channel->acks.load(), phone.messages, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(session.output->counters().decodedFrames + session.output->counters().droppedMessages,
                                  phone.pictures, 10000);
        const auto counters = session.output->counters();
        report("phone ignores acks", phone, counters);
        QVERIFY(counters.droppedMessages > 0);
        QCOMPARE(phone.skipped, 0);
        QCOMPARE(session.channel->wrongSession.load(), 0);

        // after the IDR frame at 2.5 s the frames are decoded again
        const int decoded = counters.decodedFrames;
        runPhone(session, encoder, 500, false);
        QTRY_VERIFY_WITH_TIMEOUT(session.output->counters().decodedFrames > decoded + 3, 5000);
    }

    // A blocked decoder (here 1 s per picture) does not leave the phone waiting: the ack is sent anyway after
    // OPENHU_VIDEO_ACK_MAX_DELAY_MS
    void blockedDecoderAcksAfterTheMaxDelay()
    {
        TestHome home;
        Environment environment;
        environment.set("OPENHU_VIDEO_ACK", "");
        environment.set("OPENHU_VIDEO_ACK_MAX_DELAY_MS", "200");
        environment.set("OPENHU_DEBUG_DECODE_DELAY_MS", "1000");
        PhoneEncoder encoder;
        Session session;

        const auto phone = runPhone(session, encoder, 2500, true);
        report("blocked decoder", phone, session.output->counters());
        QVERIFY(phone.pictures >= 5);
        QVERIFY(phone.ackDelayMaxMs >= 190);
        QVERIFY(phone.ackDelayMaxMs < 600);
        QCOMPARE(session.channel->acks.load(), phone.messages);
        // the acks sent early are not sent a second time when the decoder takes the message
        QTest::qWait(1500);
        QCOMPARE(session.channel->acks.load(), phone.messages);
    }
};

QTEST_MAIN(TestVideoAck)
#include "test_video_ack.moc"
