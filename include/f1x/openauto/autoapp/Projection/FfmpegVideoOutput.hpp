/*
*  This file is part of openauto project.
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <QObject>
#include <QPointer>
#include <QVideoFrame>
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
class FfmpegVideoOutput: public QObject, public VideoOutput, boost::noncopyable
{
    Q_OBJECT

public:
    // sink: for tests, nullptr uses the shell's sink (AaBridge)
    explicit FfmpegVideoOutput(configuration::IConfiguration::Pointer configuration, QVideoSink* sink = nullptr);
    ~FfmpegVideoOutput() override;

    bool open() override;
    bool init() override;
    void write(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void stop() override;

    // Name of the running FFmpeg decoder, empty before init(). Thread safe.
    std::string decoderName() const;

private:
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
    QVideoFrame toVideoFrame(const AVFrame* frame);
    void present(QVideoFrame frame);
    void logStats(bool force);
    std::vector<std::string> decoderCandidates() const;

    QPointer<QVideoSink> testSink_;
    const bool useTestSink_;

    // Input queue between write() (aasdk threads) and the decoder thread
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<QByteArray> queue_;
    size_t queuedBytes_ = 0;
    bool running_ = false;
    std::thread thread_;
    std::string decoderName_;

    // Decoder thread only
    AVCodecContext* codec_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* sws_ = nullptr;
    bool hardware_ = false;
    int packetsWithoutFrame_ = 0;
    Stats stats_;
    int64_t statsSince_ = 0;

    // Newest decoded frame waiting for the GUI thread (older ones are replaced, never queued)
    std::mutex frameMutex_;
    QVideoFrame pendingFrame_;
    bool presentScheduled_ = false;
};

}
}
}
}
