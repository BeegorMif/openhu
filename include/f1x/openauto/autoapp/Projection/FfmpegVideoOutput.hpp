/*
*  This file is part of OpenHU.
*  Copyright (C) 2026 Leon Strauß
*
*  OpenHU is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.
*
*  OpenHU is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with OpenHU. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <QObject>
#include <QPointer>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <boost/noncopyable.hpp>
#include <f1x/openauto/autoapp/Projection/VideoOutput.hpp>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

// Android Auto video: every message of the video channel is one H.264 access unit (or the codec
// configuration SPS/PPS). They are decoded with FFmpeg (libavcodec) in a thread of their own and the
// newest frame goes to the QVideoSink of the shell's VideoOutput (AaBridge::videoSink()).
//
// Decoder (Video.Decoder in openhu.ini): "auto" tries the hardware decoder of the platform first
// (h264_v4l2m2m on ARM) and falls back to software, "software" is FFmpeg's h264, any other value is
// the name of an FFmpeg decoder.
//
// The sink keeps the last picture while the phone stops and restarts the stream within a connection
// (video focus NATIVE/PROJECTED, #88). After a (re)start, frames the decoder marks as corrupt or concealed (and after
// a restart blank ones: the phone sends black frames before its UI) are held back until the first good one (at most
// cMaxHoldMs), and the first picture is logged; with OPENHU_LOG=debug
// the first messages and frames after a start are logged in detail (NAL types, mean luma, change to the previous
// frame) to find what the phone sends.
//
// Flow control (#22): with a consumed handler (setConsumedHandler) every message is reported once it has left the
// queue, and the video service acknowledges it to the phone only then. The phone, which waits for the ack
// (max_unacked), so sends no faster than the decoder takes the messages, and the queue stays short. If the queue
// still overflows (a phone that does not wait), everything up to the next IDR frame is dropped as before.
// OPENHU_DEBUG_DECODE_DELAY_MS=<ms> simulates a slow decoder: the decoder thread waits that long before each picture.
class FfmpegVideoOutput: public QObject, public VideoOutput, boost::noncopyable
{
    Q_OBJECT

public:
    // sink: for tests, nullptr uses the shell's sink (AaBridge)
    explicit FfmpegVideoOutput(configuration::IConfiguration::Pointer configuration, QVideoSink* sink = nullptr);
    ~FfmpegVideoOutput() override;

    bool open() override;
    bool init() override;
    void setCodec(aap_protobuf::service::media::shared::message::MediaCodecType codec) override;
    bool offerH265() const override;
    void write(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void stop() override;
    void streamStopped() override;
    void streamStarted() override;
    bool setConsumedHandler(ConsumedHandler handler) override;

    // Counters since init(), thread safe (tests and the simulation)
    struct Counters
    {
        int messages = 0;          // written
        int decodedFrames = 0;     // frames out of the decoder
        int droppedMessages = 0;   // dropped after an overflow (until the next IDR frame)
        size_t maxQueued = 0;      // most messages waiting in the queue at once
    };
    Counters counters() const;
    // Queue limit, cMaxQueuedBytes by default; for tests
    void setMaxQueuedBytes(size_t bytes);

    // Name of the running FFmpeg decoder, empty before init(). Thread safe.
    std::string decoderName() const;

private:
    // A message of the video channel; streamStartMs >= 0 marks the first one after a MediaChannelStartIndication
    struct Message
    {
        QByteArray data;
        int64_t streamStartMs = -1;
        bool restart = false;
    };

    // Decoder thread: what happens after a stream (re)start
    struct StreamStart
    {
        bool active = false;      // within cDiagnosticMs after the start
        bool restart = false;     // the phone had stopped the stream in this connection
        int64_t startedMs = 0;
        int messages = 0;
        int frames = 0;
        int held = 0;
        bool shown = false;       // first picture handed to the sink
        bool idrSeen = false;
    };

    struct Stats
    {
        int frames = 0;
        int presented = 0;
        int dropped = 0;
        int errors = 0;
        double decodeMs = 0;
        size_t maxBacklog = 0;
    };

    void decodeLoop();
    bool openDecoder(const std::string& name);
    void closeDecoder();
    void decode(AVPacket* packet);
    // Hands all decoded frames to present(), true if there was one
    bool receiveFrames();
    // The decoder thread ends without stop(): no more input, video inactive, error in the shell
    void failed(const QString& error);
    // Reports messages that left the queue to the consumed handler; never with mutex_ held
    void consumed(int count);
    // write() with mutex_ held: queues the message, returns how many messages left the queue (dropped)
    int enqueue(const aasdk::common::DataConstBuffer& buffer);
    // Decides whether a decoded frame is shown while a stream (re)starts, logs the diagnostics
    bool showAfterStart(const AVFrame* frame);
    void logMessageAfterStart(const QByteArray& data);
    QVideoFrame toVideoFrame(const AVFrame* frame);
    void present(QVideoFrame frame);
    void logStats(bool force);
    // "video.stats" once a second while the debug video overlay is on (#120)
    void publishOverlayStats();
    std::vector<std::string> decoderCandidates() const;
    // "hevc" or "h264"
    std::string softwareDecoder() const;

    QPointer<QVideoSink> testSink_;
    const bool useTestSink_;

    // Input queue between write() (aasdk threads) and the decoder thread
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Message> queue_;
    size_t queuedBytes_ = 0;
    bool running_ = false;
    // After an overflow everything up to the next IDR frame is dropped (P-frames without their references)
    bool waitForKeyframe_ = false;
    int droppedMessages_ = 0;
    // MediaChannelStop/StartIndication: the next message written starts a stream
    bool streamStopped_ = false;
    int64_t pendingStartMs_ = -1;
    bool pendingRestart_ = false;
    std::thread thread_;
    std::string decoderName_;
    ConsumedHandler consumedHandler_;
    size_t maxQueuedBytes_;
    Counters counters_;

    // Decoder thread only
    AVCodecContext* codec_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* sws_ = nullptr;
    bool hardware_ = false;
    // H.265 stream (setCodec), else H.264
    std::atomic<bool> hevc_{false};
    int packetsWithoutFrame_ = 0;
    Stats stats_;
    int64_t statsSince_ = 0;
    struct OverlayStats {
        int frames = 0;
        double decodeMs = 0;
        int width = 0;
        int height = 0;
    } overlay_;
    int64_t overlaySince_ = 0;
    StreamStart streamStart_;
    bool debugLog_ = false;
    // OPENHU_DEBUG_DECODE_DELAY_MS: simulated slow decoder
    int decodeDelayMs_ = 0;
    // Subsampled luma of the last decoded frame, only with debugLog_
    std::vector<uint8_t> lastLuma_;
    // The first picture after a restart is on its way to the sink: the shell stops waiting once it is there
    std::atomic<bool> restartPicturePending_{false};
    QVideoFrameFormat lastFormat_;

    // Newest decoded frame waiting for the GUI thread (older ones are replaced, never queued)
    std::mutex frameMutex_;
    QVideoFrame pendingFrame_;
    bool presentScheduled_ = false;
};

}
}
}
}
