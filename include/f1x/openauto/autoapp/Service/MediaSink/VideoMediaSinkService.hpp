/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <aasdk/Messenger/IMessenger.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkService.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <f1x/openauto/autoapp/Projection/IVideoOutput.hpp>
#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/VideoFocusPolicy.hpp>
#include <QMetaObject>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <boost/asio/steady_timer.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace mediasink {

          // Flow control of the video channel (#22), from the environment (debugging knobs, see docs/troubleshooting.md):
          // OPENHU_VIDEO_ACK=immediate acknowledges every message as soon as it is queued (the behaviour before #22),
          // OPENHU_VIDEO_MAX_UNACKED is the max_unacked sent to the phone, OPENHU_VIDEO_ACK_MAX_DELAY_MS the longest
          // an ack is held back before it is sent anyway.
          struct VideoAckSettings {
            bool deferred = true;
            uint32_t maxUnacked = 1;
            int maxDelayMs = 500;
            static VideoAckSettings fromEnvironment();
          };

          class VideoMediaSinkService :
              public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler,
              public IService,
              public std::enable_shared_from_this<VideoMediaSinkService> {
          public:
              typedef std::shared_ptr<VideoMediaSinkService> Pointer;

            // General Constructor
            VideoMediaSinkService(boost::asio::io_service& ioService,
                                  aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
                                  projection::IVideoOutput::Pointer videoOutput);

            void start() override;
            void stop() override;
            void pause() override;
            void resume() override;
            void fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) override;

            void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) override;

            void onMediaChannelSetupRequest(
                const aap_protobuf::service::media::shared::message::Setup &request) override;

            void onMediaChannelStartIndication(
                const aap_protobuf::service::media::shared::message::Start &indication) override;

            void onMediaChannelStopIndication(
                const aap_protobuf::service::media::shared::message::Stop &indication) override;

            void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType timestamp,
                                                const aasdk::common::DataConstBuffer &buffer) override;

            void onMediaIndication(const aasdk::common::DataConstBuffer &buffer) override;

            void onChannelError(const aasdk::error::Error &e) override;

            void onVideoFocusRequest(const aap_protobuf::service::media::video::message::VideoFocusRequestNotification &request) override;
            // unsolicited: the head unit changed the focus by itself (camera overlay shown/hidden, #76), not an
            // answer to a VideoFocusRequest of the phone
            void sendVideoFocusIndication(aap_protobuf::service::media::video::message::VideoFocusMode mode,
                                          bool unsolicited);
          protected:
            using std::enable_shared_from_this<VideoMediaSinkService>::shared_from_this;
            using Clock = std::chrono::steady_clock;
            // A message the phone sent that is not acknowledged yet (deferred acks): it waits in the video output's queue
            struct PendingAck {
              int32_t session;
              Clock::time_point received;
              bool acked = false;  // sent early because the decoder was blocked for too long
            };
            struct AckStats {
              int acks = 0;
              int early = 0;
              double delaySumMs = 0;
              double delayMaxMs = 0;
              size_t maxPending = 0;
              Clock::time_point since = Clock::now();
            };
            void sendMediaAck(int32_t session);
            // strand: the video output took 'count' messages from its queue
            void onMessagesConsumed(int count);
            // strand: acks held back longer than maxDelayMs are sent anyway, statistics every 10 s
            void scheduleAckCheck();
            void checkAcks();
            boost::asio::io_service::strand strand_;
            aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel_;
            projection::IVideoOutput::Pointer videoOutput_;
            int32_t session_;
            // strand only
            VideoFocusPolicy focusPolicy_;
            QMetaObject::Connection busConnection_;
            const VideoAckSettings ackSettings_;
            // true: acked when the video output took the message from its queue, false: acked at once
            std::atomic<bool> deferredAcks_{false};
            std::mutex ackMutex_;
            std::deque<PendingAck> pendingAcks_;  // ackMutex_
            AckStats ackStats_;                   // ackMutex_
            boost::asio::steady_timer ackTimer_;  // strand
            bool stopped_ = false;                // strand
            Clock::time_point lastEarlyWarning_;  // strand
          };
        }
      }
    }
  }
}