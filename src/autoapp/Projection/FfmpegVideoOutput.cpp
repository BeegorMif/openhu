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

#include <algorithm>
#include <chrono>
#include <cstring>
#include <QCoreApplication>
#include <QThread>
#include <QVideoFrameFormat>
#include <f1x/openauto/autoapp/Projection/FfmpegVideoOutput.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include <f1x/openauto/Common/Log.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

namespace
{

// The phone sends a few frames per message at most; this much backlog means the decoder cannot keep up.
constexpr size_t cMaxQueuedBytes = 8 * 1024 * 1024;
// A hardware decoder that has not produced a frame after this many messages is replaced by software.
constexpr int cHardwareProbePackets = 60;
constexpr int64_t cStatsIntervalMs = 10000;
// SPS/PPS are a few bytes; more without a picture is not a codec configuration
constexpr qsizetype cMaxConfigBytes = 64 * 1024;

int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// True if the access unit contains a coded picture (NAL types 1-5), false for SPS/PPS/SEI only.
bool containsPicture(const QByteArray& data)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    for(qsizetype i = 0; i + 3 < data.size(); ++i)
    {
        if(bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1)
        {
            const int type = bytes[i + 3] & 0x1f;
            if(type >= 1 && type <= 5)
            {
                return true;
            }
            i += 2;
        }
    }
    return false;
}

// In the GUI thread, waiting for it when called from another thread.
template<typename Function>
void runInGuiThread(Function function)
{
    if(QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        function();
    }
    else
    {
        QMetaObject::invokeMethod(QCoreApplication::instance(), function, Qt::BlockingQueuedConnection);
    }
}

}

FfmpegVideoOutput::FfmpegVideoOutput(configuration::IConfiguration::Pointer configuration, QVideoSink* sink)
    : VideoOutput(std::move(configuration))
    , testSink_(sink)
    , useTestSink_(sink != nullptr)
{
    // deleteLater() and the frame hand-over run in the GUI thread
    this->moveToThread(QCoreApplication::instance()->thread());

    static std::once_flag logLevel;
    std::call_once(logLevel, []() { av_log_set_level(AV_LOG_ERROR); });

    // New session: resolution/margins from the settings apply from now on
    runInGuiThread([this]() { shell::AaBridge::instance().configure(configuration_); });
}

FfmpegVideoOutput::~FfmpegVideoOutput()
{
    this->stop();
}

bool FfmpegVideoOutput::open()
{
    return true;
}

bool FfmpegVideoOutput::init()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(running_)
        {
            return true;
        }
        running_ = true;
        queue_.clear();
        queuedBytes_ = 0;
    }
    thread_ = std::thread(&FfmpegVideoOutput::decodeLoop, this);

    const void* self = this;
    QMetaObject::invokeMethod(&shell::AaBridge::instance(), [self]() {
        shell::AaBridge::instance().setVideoOutputActive(self, true);
    }, Qt::QueuedConnection);
    return true;
}

void FfmpegVideoOutput::write(uint64_t, const aasdk::common::DataConstBuffer& buffer)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(!running_)
    {
        return;
    }

    if(queuedBytes_ + buffer.size > cMaxQueuedBytes)
    {
        OPENAUTO_LOG(warning) << "[FfmpegVideoOutput] decoder too slow, dropping " << queue_.size() << " queued messages";
        queue_.clear();
        queuedBytes_ = 0;
    }
    queue_.emplace_back(reinterpret_cast<const char*>(buffer.cdata), static_cast<qsizetype>(buffer.size));
    queuedBytes_ += buffer.size;
    wake_.notify_one();
}

void FfmpegVideoOutput::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!running_ && !thread_.joinable())
        {
            return;
        }
        running_ = false;
        queue_.clear();
        queuedBytes_ = 0;
        wake_.notify_one();
    }
    if(thread_.joinable())
    {
        thread_.join();
    }

    const void* self = this;
    QMetaObject::invokeMethod(&shell::AaBridge::instance(), [self]() {
        shell::AaBridge::instance().setVideoOutputActive(self, false);
    }, Qt::QueuedConnection);
}

std::string FfmpegVideoOutput::decoderName() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return decoderName_;
}

std::vector<std::string> FfmpegVideoOutput::decoderCandidates() const
{
    const std::string configured = configuration_->getVideoDecoder();
    if(configured == "software")
    {
        return {"h264"};
    }
    if(configured.empty() || configured == "auto")
    {
#if defined(__aarch64__) || defined(__arm__)
        return {"h264_v4l2m2m", "h264"};
#else
        return {"h264"};
#endif
    }
    return configured == "h264" ? std::vector<std::string>{"h264"} : std::vector<std::string>{configured, "h264"};
}

bool FfmpegVideoOutput::openDecoder(const std::string& name)
{
    const AVCodec* codec = avcodec_find_decoder_by_name(name.c_str());
    if(codec == nullptr)
    {
        OPENAUTO_LOG(info) << "[FfmpegVideoOutput] decoder " << name << " not available";
        return false;
    }

    codec_ = avcodec_alloc_context3(codec);
    // Show every frame as soon as it is decoded: no frame threading (adds a frame of latency per thread)
    codec_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codec_->thread_type = FF_THREAD_SLICE;
    codec_->thread_count = 0;

    if(avcodec_open2(codec_, codec, nullptr) < 0)
    {
        OPENAUTO_LOG(info) << "[FfmpegVideoOutput] decoder " << name << " cannot be opened";
        avcodec_free_context(&codec_);
        return false;
    }

    hardware_ = name != "h264";
    packetsWithoutFrame_ = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        decoderName_ = name;
    }
    OPENAUTO_LOG(info) << "[FfmpegVideoOutput] decoder " << name << (hardware_ ? "" : " (software)");
    return true;
}

void FfmpegVideoOutput::closeDecoder()
{
    avcodec_free_context(&codec_);
    sws_freeContext(sws_);
    sws_ = nullptr;
}

void FfmpegVideoOutput::decodeLoop()
{
    for(const auto& name: this->decoderCandidates())
    {
        if(this->openDecoder(name))
        {
            break;
        }
    }
    if(codec_ == nullptr)
    {
        OPENAUTO_LOG(error) << "[FfmpegVideoOutput] no H.264 decoder, Android Auto shows no picture";
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        queue_.clear();
        return;
    }

    frame_ = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    stats_ = Stats();
    statsSince_ = nowMs();
    // Messages until the first picture, replayed into the software decoder if the hardware one fails
    std::vector<QByteArray> history;
    bool pictureSeen = false;
    // The phone sends SPS/PPS as a message of its own: decoded together with the next picture
    QByteArray config;

    while(true)
    {
        QByteArray data;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this]() { return !running_ || !queue_.empty(); });
            if(!running_)
            {
                break;
            }
            stats_.maxBacklog = std::max(stats_.maxBacklog, queue_.size());
            data = std::move(queue_.front());
            queue_.pop_front();
            queuedBytes_ -= data.size();
        }

        if(!containsPicture(data) && config.size() + data.size() <= cMaxConfigBytes)
        {
            config.append(data);
            continue;
        }
        if(!config.isEmpty())
        {
            data.prepend(config);
            config.clear();
        }

        if(!pictureSeen && hardware_)
        {
            history.push_back(data);
        }

        packet->data = reinterpret_cast<uint8_t*>(data.data());
        packet->size = static_cast<int>(data.size());
        const int framesBefore = stats_.frames;
        this->decode(packet);
        if(stats_.frames > framesBefore)
        {
            pictureSeen = true;
            history.clear();
        }

        if(!pictureSeen && hardware_ && packetsWithoutFrame_ >= cHardwareProbePackets)
        {
            OPENAUTO_LOG(warning) << "[FfmpegVideoOutput] " << this->decoderName() << " shows no picture, switching to software";
            this->closeDecoder();
            if(!this->openDecoder("h264"))
            {
                break;
            }
            for(auto& old: history)
            {
                packet->data = reinterpret_cast<uint8_t*>(old.data());
                packet->size = static_cast<int>(old.size());
                this->decode(packet);
            }
            history.clear();
        }

        this->logStats(false);
    }

    this->logStats(true);
    av_packet_free(&packet);
    av_frame_free(&frame_);
    this->closeDecoder();
}

void FfmpegVideoOutput::decode(AVPacket* packet)
{
    const auto started = std::chrono::steady_clock::now();
    const int sent = avcodec_send_packet(codec_, packet);
    if(sent < 0 && sent != AVERROR(EAGAIN))
    {
        ++stats_.errors;
    }

    bool gotFrame = false;
    while(avcodec_receive_frame(codec_, frame_) == 0)
    {
        gotFrame = true;
        ++stats_.frames;
        this->present(this->toVideoFrame(frame_));
        av_frame_unref(frame_);
    }
    packetsWithoutFrame_ = gotFrame ? 0 : packetsWithoutFrame_ + 1;
    stats_.decodeMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

QVideoFrame FfmpegVideoOutput::toVideoFrame(const AVFrame* frame)
{
    const int width = frame->width;
    const int height = frame->height;
    const auto pixelFormat = static_cast<AVPixelFormat>(frame->format);
    const bool nv12 = pixelFormat == AV_PIX_FMT_NV12;
    const bool yuv420 = pixelFormat == AV_PIX_FMT_YUV420P || pixelFormat == AV_PIX_FMT_YUVJ420P;

    QVideoFrameFormat format(QSize(width, height), nv12 ? QVideoFrameFormat::Format_NV12 : QVideoFrameFormat::Format_YUV420P);
    format.setColorRange(pixelFormat == AV_PIX_FMT_YUVJ420P || frame->color_range == AVCOL_RANGE_JPEG
                             ? QVideoFrameFormat::ColorRange_Full : QVideoFrameFormat::ColorRange_Video);
    format.setColorSpace(frame->colorspace == AVCOL_SPC_BT709 ? QVideoFrameFormat::ColorSpace_BT709
                                                              : QVideoFrameFormat::ColorSpace_BT601);

    QVideoFrame result(format);
    if(!result.map(QVideoFrame::WriteOnly))
    {
        ++stats_.errors;
        return QVideoFrame();
    }

    if(nv12 || yuv420)
    {
        const int planes = nv12 ? 2 : 3;
        for(int plane = 0; plane < planes; ++plane)
        {
            const int rows = plane == 0 ? height : (height + 1) / 2;
            const int rowBytes = plane == 0 ? width : (nv12 ? 2 : 1) * ((width + 1) / 2);
            const int bytes = std::min(rowBytes, result.bytesPerLine(plane));
            uchar* target = result.bits(plane);
            const uint8_t* source = frame->data[plane];
            for(int row = 0; row < rows; ++row)
            {
                std::memcpy(target + row * result.bytesPerLine(plane), source + row * frame->linesize[plane], bytes);
            }
        }
    }
    else
    {
        // Other decoder output (e.g. 4:2:2): convert to YUV420P
        sws_ = sws_getCachedContext(sws_, width, height, pixelFormat, width, height, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
        uint8_t* targets[4] = {result.bits(0), result.bits(1), result.bits(2), nullptr};
        int strides[4] = {result.bytesPerLine(0), result.bytesPerLine(1), result.bytesPerLine(2), 0};
        if(sws_ == nullptr || sws_scale(sws_, frame->data, frame->linesize, 0, height, targets, strides) <= 0)
        {
            ++stats_.errors;
            result.unmap();
            return QVideoFrame();
        }
    }

    result.unmap();
    return result;
}

void FfmpegVideoOutput::present(QVideoFrame frame)
{
    if(!frame.isValid())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        if(presentScheduled_)
        {
            // The GUI thread has not shown the previous frame yet: show only the newest one
            ++stats_.dropped;
            pendingFrame_ = std::move(frame);
            return;
        }
        pendingFrame_ = std::move(frame);
        presentScheduled_ = true;
    }

    QMetaObject::invokeMethod(this, [this]() {
        QVideoFrame next;
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            next = std::move(pendingFrame_);
            pendingFrame_ = QVideoFrame();
            presentScheduled_ = false;
        }
        QVideoSink* sink = useTestSink_ ? testSink_.data() : shell::AaBridge::instance().videoSink();
        if(sink != nullptr)
        {
            sink->setVideoFrame(next);
        }
    }, Qt::QueuedConnection);
}

void FfmpegVideoOutput::logStats(bool force)
{
    const int64_t now = nowMs();
    const int64_t elapsed = now - statsSince_;
    if(!force && elapsed < cStatsIntervalMs)
    {
        return;
    }
    if(stats_.frames > 0 || stats_.errors > 0)
    {
        OPENAUTO_LOG(info) << "[FfmpegVideoOutput] " << this->decoderName() << ": "
                           << (elapsed > 0 ? stats_.frames * 1000.0 / elapsed : 0) << " fps, "
                           << (stats_.frames > 0 ? stats_.decodeMs / stats_.frames : 0) << " ms/frame decoding, "
                           << stats_.dropped << " frames replaced before display, " << stats_.errors << " errors, "
                           << "backlog up to " << stats_.maxBacklog << " messages";
    }
    stats_ = Stats();
    statsSince_ = now;
}

}
}
}
}
