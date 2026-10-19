/*
*  This file is part of OpenHU, based on the openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <functional>
#include <memory>
#include <QRect>
#include <aasdk/Common/Data.hpp>
#include <aasdk/Messenger/Timestamp.hpp>
#include <aap_protobuf/service/media/sink/message/VideoFrameRateType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

class IVideoOutput
{
public:
    typedef std::shared_ptr<IVideoOutput> Pointer;

    IVideoOutput() = default;
    virtual ~IVideoOutput() = default;

    virtual bool open() = 0;
    virtual bool init() = 0;
    virtual void write(aasdk::messenger::Timestamp::ValueType timestamp, const aasdk::common::DataConstBuffer& buffer) = 0;
    virtual void stop() = 0;
    // The phone stops/starts the video stream (MediaChannelStop/StartIndication). Within one connection this
    // happens when OpenHU takes the video focus (NATIVE: reverse camera, "Exit") and gives it back (#83, #88):
    // the output keeps running and keeps its last picture.
    virtual void streamStopped() {}
    virtual void streamStarted() {}

    // Flow control (#22): an output that accepts a handler calls it once for every message given to write(), as soon
    // as the message has left its queue (taken by the decoder, or dropped). The video service acknowledges the
    // message to the phone only then, so the phone waits (max_unacked) instead of OpenHU's queue growing. The
    // handler may be called from any thread, also from within write(); count is the number of messages. An output
    // that returns false here has no queue worth waiting for: the service acknowledges every message at once.
    using ConsumedHandler = std::function<void(int count)>;
    virtual bool setConsumedHandler(ConsumedHandler) { return false; }

    // H.265 (#116): offered to the phone when true (one config with video_codec_type H265 switches the whole channel,
    // a phone without an H.265 encoder shows an error and stays on H.264); the phone's choice comes in the setup
    // message, before init()
    virtual bool offerH265() const { return false; }
    virtual void setCodec(aap_protobuf::service::media::shared::message::MediaCodecType) {}

    virtual aap_protobuf::service::media::sink::message::VideoFrameRateType getVideoFPS() const = 0;
    virtual aap_protobuf::service::media::sink::message::VideoCodecResolutionType getVideoResolution() const = 0;
    virtual size_t getScreenDPI() const = 0;
    virtual QRect getVideoMargins() const = 0;

};

}
}
}
}
