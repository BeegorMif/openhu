#include <QVideoFrame>
#include <QVideoSink>
#include <QtTest>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/autoapp/Projection/FfmpegVideoOutput.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include "TestHome.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

using f1x::openauto::autoapp::configuration::Configuration;
using f1x::openauto::autoapp::projection::FfmpegVideoOutput;
using f1x::openauto::autoapp::shell::AaBridge;

namespace
{

constexpr int cWidth = 320;
constexpr int cHeight = 240;

// H.264 access units like the phone sends them: flat grey frames (luma = value), first one an IDR with SPS/PPS
QList<QByteArray> encode(int frames, int luma)
{
    QList<QByteArray> packets;
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if(codec == nullptr)
    {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if(codec == nullptr)
    {
        return packets;
    }

    AVCodecContext* context = avcodec_alloc_context3(codec);
    context->width = cWidth;
    context->height = cHeight;
    context->pix_fmt = AV_PIX_FMT_YUV420P;
    context->time_base = {1, 30};
    context->framerate = {30, 1};
    context->gop_size = 600;
    context->max_b_frames = 0;
    av_opt_set(context->priv_data, "preset", "ultrafast", 0);
    av_opt_set(context->priv_data, "tune", "zerolatency", 0);
    if(avcodec_open2(context, codec, nullptr) < 0)
    {
        avcodec_free_context(&context);
        return packets;
    }

    AVFrame* frame = av_frame_alloc();
    frame->format = context->pix_fmt;
    frame->width = cWidth;
    frame->height = cHeight;
    av_frame_get_buffer(frame, 0);
    AVPacket* packet = av_packet_alloc();

    auto drain = [&]() {
        while(avcodec_receive_packet(context, packet) == 0)
        {
            packets.append(QByteArray(reinterpret_cast<const char*>(packet->data), packet->size));
            av_packet_unref(packet);
        }
    };

    for(int i = 0; i < frames; ++i)
    {
        av_frame_make_writable(frame);
        for(int y = 0; y < cHeight; ++y)
        {
            memset(frame->data[0] + y * frame->linesize[0], luma, cWidth);
        }
        for(int plane = 1; plane < 3; ++plane)
        {
            for(int y = 0; y < cHeight / 2; ++y)
            {
                memset(frame->data[plane] + y * frame->linesize[plane], 128, cWidth / 2);
            }
        }
        frame->pts = i;
        avcodec_send_frame(context, frame);
        drain();
    }
    avcodec_send_frame(context, nullptr);
    drain();

    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&context);
    return packets;
}

// The phone sends SPS/PPS as a message of its own before the first frame
QList<QByteArray> splitCodecConfig(QList<QByteArray> packets)
{
    const QByteArray first = packets.first();
    for(int i = 0; i + 4 < first.size(); ++i)
    {
        const bool startCode = first[i] == 0 && first[i + 1] == 0 && first[i + 2] == 1;
        if(startCode && (first[i + 3] & 0x1f) == 5)
        {
            const int start = i > 0 && first[i - 1] == 0 ? i - 1 : i;
            packets.replace(0, first.mid(start));
            packets.prepend(first.left(start));
            break;
        }
    }
    return packets;
}

void write(FfmpegVideoOutput& output, const QByteArray& data)
{
    output.write(0, aasdk::common::DataConstBuffer(reinterpret_cast<const uint8_t*>(data.constData()), data.size()));
}

int centerLuma(QVideoFrame frame)
{
    if(!frame.map(QVideoFrame::ReadOnly))
    {
        return -1;
    }
    const int value = frame.bits(0)[(cHeight / 2) * frame.bytesPerLine(0) + cWidth / 2];
    frame.unmap();
    return value;
}

}

class TestVideo: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if(encode(1, 100).isEmpty())
        {
            QSKIP("FFmpeg has no H.264 encoder to create the test stream");
        }
    }

    void decodesTheStream()
    {
        TestHome home;
        auto configuration = std::make_shared<Configuration>();
        configuration->setVideoDecoder("software");
        QVideoSink sink;
        QSignalSpy frames(&sink, &QVideoSink::videoFrameChanged);

        FfmpegVideoOutput output(configuration, &sink);
        QVERIFY(output.open());
        write(output, QByteArray(100, 'x'));  // before init(): ignored
        QVERIFY(output.init());
        QTRY_COMPARE(output.decoderName(), std::string("h264"));
        QTRY_VERIFY(AaBridge::instance().videoActive());

        const auto packets = splitCodecConfig(encode(10, 100));
        QVERIFY(packets.size() >= 10);
        for(const auto& packet: packets)
        {
            write(output, packet);
        }

        QTRY_VERIFY(frames.count() >= 1);
        const QVideoFrame frame = sink.videoFrame();
        QCOMPARE(frame.size(), QSize(cWidth, cHeight));
        QCOMPARE(frame.pixelFormat(), QVideoFrameFormat::Format_YUV420P);
        QVERIFY(qAbs(centerLuma(frame) - 100) <= 4);

        output.stop();
        QTRY_VERIFY(!AaBridge::instance().videoActive());
    }

    void newSessionAfterStop()
    {
        TestHome home;
        auto configuration = std::make_shared<Configuration>();  // "auto": falls back to software here
        QVideoSink sink;
        FfmpegVideoOutput output(configuration, &sink);

        QVERIFY(output.init());
        for(const auto& packet: splitCodecConfig(encode(5, 60)))
        {
            write(output, packet);
        }
        QTRY_VERIFY(sink.videoFrame().isValid());
        QVERIFY(qAbs(centerLuma(sink.videoFrame()) - 60) <= 4);
        output.stop();

        // the phone reconnects: a new stream with a new IDR frame
        QVERIFY(output.init());
        for(const auto& packet: splitCodecConfig(encode(5, 180)))
        {
            write(output, packet);
        }
        QTRY_VERIFY(qAbs(centerLuma(sink.videoFrame()) - 180) <= 4);
        QCOMPARE(output.decoderName(), std::string("h264"));
    }

    void garbageDoesNotCrash()
    {
        TestHome home;
        QVideoSink sink;
        FfmpegVideoOutput output(std::make_shared<Configuration>(), &sink);
        QVERIFY(output.init());
        for(int i = 0; i < 50; ++i)
        {
            // without start codes, and as broken pictures (NAL type 1) that reach the decoder
            write(output, QByteArray(1000 + i, char(i)));
            write(output, QByteArray("\x00\x00\x01\x41", 4) + QByteArray(500 + i, char(i * 7)));
        }
        QTest::qWait(200);
        output.stop();
        QVERIFY(!sink.videoFrame().isValid());
    }
};

QTEST_MAIN(TestVideo)
#include "test_video.moc"
