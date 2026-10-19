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

#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <f1x/openauto/autoapp/Service/MediaSink/VideoMediaSinkService.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace mediasink {
          namespace {
            // Ack check and statistics
            constexpr int cAckCheckMs = 100;
            constexpr int cAckStatsMs = 10000;

            int envInt(const char *name, int fallback, int min) {
              const char *value = std::getenv(name);
              if (value == nullptr || *value == '\0') {
                return fallback;
              }
              char *end = nullptr;
              const long parsed = std::strtol(value, &end, 10);
              if (end == value || *end != '\0' || parsed < min || parsed > 100000) {
                OPENAUTO_LOG(warning) << "[VideoMediaSinkService] " << name << "=" << value << " is not valid, using "
                                      << fallback;
                return fallback;
              }
              return static_cast<int>(parsed);
            }

            double msSince(std::chrono::steady_clock::time_point then) {
              return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - then).count();
            }
          }

          VideoAckSettings VideoAckSettings::fromEnvironment() {
            VideoAckSettings settings;
            const char *mode = std::getenv("OPENHU_VIDEO_ACK");
            settings.deferred = mode == nullptr || std::strcmp(mode, "immediate") != 0;
            settings.maxUnacked = static_cast<uint32_t>(envInt("OPENHU_VIDEO_MAX_UNACKED", 1, 1));
            settings.maxDelayMs = envInt("OPENHU_VIDEO_ACK_MAX_DELAY_MS", settings.maxDelayMs, 1);
            return settings;
          }

          VideoMediaSinkService::VideoMediaSinkService(boost::asio::io_service &ioService,
                                                       aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
                                                       projection::IVideoOutput::Pointer videoOutput)
              : strand_(ioService), channel_(std::move(channel)), videoOutput_(std::move(videoOutput)), session_(-1),
                ackSettings_(VideoAckSettings::fromEnvironment()), ackTimer_(ioService) {

          }

          void VideoMediaSinkService::start() {
            // Before the first message: from now on a message is acknowledged once it left the output's queue (#22)
            if (ackSettings_.deferred) {
              std::weak_ptr<VideoMediaSinkService> weak = this->shared_from_this();
              deferredAcks_ = videoOutput_->setConsumedHandler([weak](int count) {
                if (auto service = weak.lock()) {
                  service->strand_.post([service, count]() { service->onMessagesConsumed(count); });
                }
              });
            }
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] start()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] video acks "
                                 << (deferredAcks_ ? "when the decoder takes the message" : "at once")
                                 << ", max_unacked " << ackSettings_.maxUnacked
                                 << (deferredAcks_ ? ", sent anyway after " + std::to_string(ackSettings_.maxDelayMs) + " ms"
                                                   : std::string());
              channel_->receive(this->shared_from_this());
              if (deferredAcks_) {
                this->scheduleAckCheck();
              }

              // The shell covers Android Auto (plugin overlay like the reverse camera, or a plugin page instead of
              // the Android Auto page): tell the phone with an unsolicited video focus indication (#76)
              auto &bus = core::EventBus::instance();
              focusPolicy_ = VideoFocusPolicy(bus.last("ui.page").value("page").toString(), bus.last("ui.overlay"));
              std::weak_ptr<VideoMediaSinkService> weak = this->shared_from_this();
              busConnection_ = QObject::connect(&bus, &core::EventBus::event,
                                                [weak](const QString &topic, const QVariantMap &data) {
                if (topic != "ui.page" && topic != "ui.overlay") {
                  return;
                }
                if (auto service = weak.lock()) {
                  service->strand_.dispatch([service, topic, data]() {
                    const auto mode = topic == "ui.page"
                                      ? service->focusPolicy_.setPage(data.value("page").toString())
                                      : service->focusPolicy_.setOverlays(data);
                    if (mode) {
                      service->sendVideoFocusIndication(*mode, true);
                    }
                  });
                }
              });
            });
          }

          void VideoMediaSinkService::stop() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] stop()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());
              QObject::disconnect(busConnection_);
              stopped_ = true;
              ackTimer_.cancel();
              videoOutput_->setConsumedHandler(nullptr);
              videoOutput_->stop();
              std::lock_guard<std::mutex> lock(ackMutex_);
              pendingAcks_.clear();
            });
          }

          void VideoMediaSinkService::pause() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] pause()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());
            });
          }

          void VideoMediaSinkService::resume() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] resume()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());

            });
          }

          void VideoMediaSinkService::fillFeatures(
              aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] fillFeatures()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                               << aasdk::messenger::channelIdToString(channel_->getId());

            auto *service = response.add_channels();
            service->set_id(static_cast<uint32_t>(channel_->getId()));

            auto *videoChannel = service->mutable_media_sink_service();

            videoChannel->set_available_type(
                aap_protobuf::service::media::shared::message::MediaCodecType::MEDIA_CODEC_VIDEO_H264_BP);
            videoChannel->set_available_while_in_call(true);


            auto *videoConfig1 = videoChannel->add_video_configs();
            videoConfig1->set_codec_resolution(videoOutput_->getVideoResolution());
            videoConfig1->set_frame_rate(videoOutput_->getVideoFPS());

            const auto &videoMargins = videoOutput_->getVideoMargins();
            videoConfig1->set_height_margin(videoMargins.height());
            videoConfig1->set_width_margin(videoMargins.width());
            videoConfig1->set_density(videoOutput_->getScreenDPI());
            if (videoOutput_->offerH265()) {
              videoConfig1->set_video_codec_type(
                  aap_protobuf::service::media::shared::message::MediaCodecType::MEDIA_CODEC_VIDEO_H265);
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] offering H.265";
            }

            OPENAUTO_LOG(info) << "[VideoMediaSinkService] getVideoResolution " << VideoCodecResolutionType_Name(videoOutput_->getVideoResolution());
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] getVideoFPS " << VideoFrameRateType_Name(videoOutput_->getVideoFPS());
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] width " << videoMargins.width();
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] height " << videoMargins.height();
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] getScreenDPI " << videoOutput_->getScreenDPI();
          }

          void
          VideoMediaSinkService::onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup &request) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onMediaChannelSetupRequest()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", Codec: "
                               << MediaCodecType_Name(request.type());


            videoOutput_->setCodec(request.type());
            auto status = videoOutput_->init()
                          ? aap_protobuf::service::media::shared::message::Config::STATUS_READY
                          : aap_protobuf::service::media::shared::message::Config::STATUS_WAIT;

            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] setup status: " << Config_Status_Name(status);

            aap_protobuf::service::media::shared::message::Config response;
            response.set_status(status);
            response.set_max_unacked(ackSettings_.maxUnacked);
            response.add_configuration_indices(0);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([this, self = this->shared_from_this()]() {
                            // as before #76 not marked unsolicited, although the phone did not ask for it
                            this->sendVideoFocusIndication(focusPolicy_.setup(), false);
                          },
                          std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                    std::placeholders::_1));

            channel_->sendChannelSetupResponse(response, std::move(promise));
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onChannelOpenRequest()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel Id: " << request.service_id() << ", Priority: "
                               << request.priority();

            const aap_protobuf::shared::MessageStatus status = videoOutput_->open()
                                                               ? aap_protobuf::shared::MessageStatus::STATUS_SUCCESS
                                                               : aap_protobuf::shared::MessageStatus::STATUS_INTERNAL_ERROR;

            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Status determined: "
                               << aap_protobuf::shared::MessageStatus_Name(status);

            aap_protobuf::service::control::message::ChannelOpenResponse response;
            response.set_status(status);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() {}, std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            channel_->sendChannelOpenResponse(response, std::move(promise));
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onMediaChannelStartIndication(
              const aap_protobuf::service::media::shared::message::Start &indication) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onMediaChannelStartIndication()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", session: "
                               << indication.session_id();

            session_ = indication.session_id();
            // within a connection: the stream comes back after OpenHU gave the video focus back (#88)
            videoOutput_->streamStarted();
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onMediaChannelStopIndication(
              const aap_protobuf::service::media::shared::message::Stop &indication) {
            OPENAUTO_LOG(info) << "[onMediaChannelStopIndication] onMediaChannelStopIndication()";
            OPENAUTO_LOG(info) << "[onMediaChannelStopIndication] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", session: " << session_;
            videoOutput_->streamStopped();
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType timestamp,
                                                                     const aasdk::common::DataConstBuffer &buffer) {
            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] onMediaWithTimestampIndication()";
            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", session: " << session_;

            if (deferredAcks_) {
              // acked in onMessagesConsumed(), possibly already from within write()
              {
                std::lock_guard<std::mutex> lock(ackMutex_);
                pendingAcks_.push_back(PendingAck{session_, Clock::now()});
                ackStats_.maxPending = std::max(ackStats_.maxPending, pendingAcks_.size());
              }
              videoOutput_->write(timestamp, buffer);
            } else {
              videoOutput_->write(timestamp, buffer);
              this->sendMediaAck(session_);
            }
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::sendMediaAck(int32_t session) {
            aap_protobuf::service::media::source::message::Ack indication;
            indication.set_session_id(session);
            indication.set_ack(1);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() {}, std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            channel_->sendMediaAckIndication(indication, std::move(promise));
          }

          void VideoMediaSinkService::onMessagesConsumed(int count) {
            if (stopped_) {
              return;
            }
            std::vector<int32_t> sessions;
            double delayMs = 0;
            size_t pending = 0;
            {
              std::lock_guard<std::mutex> lock(ackMutex_);
              for (int i = 0; i < count && !pendingAcks_.empty(); ++i) {
                const PendingAck &ack = pendingAcks_.front();
                if (!ack.acked) {
                  delayMs = msSince(ack.received);
                  sessions.push_back(ack.session);
                  ++ackStats_.acks;
                  ackStats_.delaySumMs += delayMs;
                  ackStats_.delayMaxMs = std::max(ackStats_.delayMaxMs, delayMs);
                }
                pendingAcks_.pop_front();
              }
              pending = pendingAcks_.size();
            }
            for (const auto session: sessions) {
              this->sendMediaAck(session);
            }
            if (!sessions.empty()) {
              // pending: messages still waiting in the decoder's queue (not acknowledged yet)
              OPENAUTO_LOG(debug) << "[VideoMediaSinkService] ack " << sessions.size() << " after " << delayMs
                                  << " ms, " << pending << " unacked in the queue";
            }
          }

          void VideoMediaSinkService::scheduleAckCheck() {
            std::weak_ptr<VideoMediaSinkService> weak = this->shared_from_this();
            ackTimer_.expires_after(std::chrono::milliseconds(cAckCheckMs));
            ackTimer_.async_wait(strand_.wrap([weak](const boost::system::error_code &error) {
              auto service = weak.lock();
              if (error || !service || service->stopped_) {
                return;
              }
              service->checkAcks();
              service->scheduleAckCheck();
            }));
          }

          void VideoMediaSinkService::checkAcks() {
            std::vector<int32_t> sessions;
            AckStats stats;
            bool logStats = false;
            {
              std::lock_guard<std::mutex> lock(ackMutex_);
              // The decoder is blocked (e.g. a hardware decoder that opens slowly): the phone must not wait forever.
              // The messages stay queued; if the queue overflows, the output drops until the next IDR frame.
              for (auto &ack: pendingAcks_) {
                if (!ack.acked && msSince(ack.received) >= ackSettings_.maxDelayMs) {
                  ack.acked = true;
                  sessions.push_back(ack.session);
                  ++ackStats_.early;
                }
              }
              if (msSince(ackStats_.since) >= cAckStatsMs) {
                stats = ackStats_;
                logStats = stats.acks > 0 || stats.early > 0;
                ackStats_ = AckStats();
              }
            }
            for (const auto session: sessions) {
              this->sendMediaAck(session);
            }
            // at most every 10 s, the statistics count all of them
            if (!sessions.empty() && (lastEarlyWarning_ == Clock::time_point() || msSince(lastEarlyWarning_) >= cAckStatsMs)) {
              lastEarlyWarning_ = Clock::now();
              OPENAUTO_LOG(warning) << "[VideoMediaSinkService] decoder blocked: " << sessions.size()
                                    << " video acks sent after " << ackSettings_.maxDelayMs
                                    << " ms without waiting for the decoder";
            }
            if (logStats) {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] video acks: " << stats.acks << ", delay "
                                 << (stats.acks > 0 ? stats.delaySumMs / stats.acks : 0) << " ms on average, "
                                 << stats.delayMaxMs << " ms max, up to " << stats.maxPending << " unacked, "
                                 << stats.early << " sent before the decoder took the message";
            }
          }

          void VideoMediaSinkService::onMediaIndication(const aasdk::common::DataConstBuffer &buffer) {
            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] onMediaIndication()";
            this->onMediaWithTimestampIndication(0, buffer);
          }

          void VideoMediaSinkService::onChannelError(const aasdk::error::Error &e) {
            // OPERATION_ABORTED is expected during shutdown when messenger stops
            if (e.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED) {
              OPENAUTO_LOG(debug) << "[VideoMediaSinkService] onChannelError(): " << e.what()
                                  << ", channel: " << aasdk::messenger::channelIdToString(channel_->getId())
                                  << " (expected during stop)";
            } else {
              OPENAUTO_LOG(error) << "[VideoMediaSinkService] onChannelError(): " << e.what()
                                  << ", channel: " << aasdk::messenger::channelIdToString(channel_->getId());
            }
          }

          void VideoMediaSinkService::onVideoFocusRequest(
              const aap_protobuf::service::media::video::message::VideoFocusRequestNotification &request) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onVideoFocusRequest()";
            // Note: disp_channel_id() is deprecated but still used for logging compatibility
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Display index: " << request.disp_channel_id() << ", focus mode: " << VideoFocusMode_Name(request.mode()) << ", focus reason: " << VideoFocusReason_Name(request.reason());
            #pragma GCC diagnostic pop

            // Answer with the resulting mode (oaa video.md, "Sequence: HU Switches to Native UI"): NATIVE for the
            // phone's NATIVE request (before #76 OpenHU answered PROJECTED), NATIVE also while the camera covers the
            // projection, even if the phone asks for PROJECTED.
            const auto mode = focusPolicy_.request(request.mode());
            if (request.mode() ==
                aap_protobuf::service::media::video::message::VideoFocusMode::VIDEO_FOCUS_NATIVE) {
              // "Exit" in Android Auto: the shell shows its home page
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Returning to OS.";
              core::EventBus::instance().emitEvent("projection.exit", {});
            }

            this->sendVideoFocusIndication(mode, false);
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::sendVideoFocusIndication(
              aap_protobuf::service::media::video::message::VideoFocusMode mode, bool unsolicited) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] sendVideoFocusIndication(): " << VideoFocusMode_Name(mode)
                               << (unsolicited ? ", unsolicited" : "");

            aap_protobuf::service::media::video::message::VideoFocusNotification videoFocusIndication;
            videoFocusIndication.set_focus(mode);
            videoFocusIndication.set_unsolicited(unsolicited);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() { }, std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            channel_->sendVideoFocusIndication(videoFocusIndication, std::move(promise));
          }
        }
      }
    }
  }
}