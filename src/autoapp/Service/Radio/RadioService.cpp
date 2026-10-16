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

#include <chrono>
#include <cmath>
#include <f1x/openauto/autoapp/Service/Radio/RadioService.hpp>
#include <f1x/openauto/autoapp/Service/Radio/RadioMapping.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace radio {

          namespace msg = aap_protobuf::service::radio::message;

          // the phone gives up after 6 seconds
          constexpr int cTuneTimeoutMs = 5000;
          // Android Auto sends the band scan action already when the FM tab is opened, not only for its refresh
          // button. A scan silences the radio (one stick): at most every 5 minutes while stations are known.
          constexpr auto cScanInterval = std::chrono::minutes(5);

          RadioService::RadioService(boost::asio::io_service &ioService, aasdk::messenger::IMessenger::Pointer messenger)
              : strand_(ioService),
                tuneTimer_(ioService),
                channel_(std::make_shared<aasdk::channel::radio::RadioService>(strand_, std::move(messenger))),
                state_(core::EventBus::instance().last("radio.state")),
                icons_(stationIcons(core::EventBus::instance().last("radio.logos"))) {
            this->setSlide(core::EventBus::instance().last("radio.slide"));
          }

          void RadioService::start() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[RadioService] start()";
              std::weak_ptr<RadioService> weak = self;
              busConnection_ = QObject::connect(&core::EventBus::instance(), &core::EventBus::event,
                                                [weak](const QString &topic, const QVariantMap &data) {
                if (topic != "radio.state" && topic != "radio.logos" && topic != "radio.slide") {
                  return;
                }
                if (auto service = weak.lock()) {
                  if (topic == "radio.slide") {
                    service->strand_.dispatch([service, data]() {
                      service->setSlide(data);
                      service->sendState(false);
                    });
                  } else if (topic == "radio.logos") {
                    service->strand_.dispatch([service, data]() { service->onLogos(data); });
                  } else {
                    service->strand_.dispatch([service, data]() { service->onState(data); });
                  }
                }
              });
              channel_->receive(this->shared_from_this());
            });
          }

          void RadioService::stop() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[RadioService] stop()";
              QObject::disconnect(busConnection_);
              tuneTimer_.cancel();
              open_ = false;
            });
          }

          void RadioService::pause() {
          }

          void RadioService::resume() {
          }

          void RadioService::fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
            OPENAUTO_LOG(info) << "[RadioService] fillFeatures()";
            auto *service = response.add_channels();
            service->set_id(static_cast<uint32_t>(channel_->getId()));
            fillService(*service->mutable_radio_service(), state_);
          }

          void RadioService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
            OPENAUTO_LOG(info) << "[RadioService] onChannelOpenRequest(), priority " << request.priority();
            aap_protobuf::service::control::message::ChannelOpenResponse response;
            response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
            channel_->sendChannelOpenResponse(response, this->promise());
            channel_->receive(this->shared_from_this());

            // the phone expects the whole state right away
            open_ = true;
            this->sendState(true);
          }

          void RadioService::onChannelError(const aasdk::error::Error &e) {
            OPENAUTO_LOG(error) << "[RadioService] onChannelError(): " << e.what();
          }

          void RadioService::onMuteRequest(const msg::RadioMuteRequest &request) {
            OPENAUTO_LOG(info) << "[RadioService] mute " << request.mute();
            this->command({{"cmd", request.mute() ? "stop" : "play"}});
            msg::RadioMuteResponse response;
            response.set_is_muted(request.mute());
            channel_->sendMuteResponse(response, this->promise());
          }

          void RadioService::onTuneRequest(const msg::RadioTuneRequest &request) {
            const QList<QVariantMap> commands = tuneCommands(request.program_selector(), state_);
            OPENAUTO_LOG(info) << "[RadioService] tune to "
                               << request.program_selector().primary_identifier().value() << " (type "
                               << request.program_selector().primary_identifier().type() << ")"
                               << (commands.isEmpty() ? ", not supported" : "");
            if (commands.isEmpty()) {
              msg::RadioTuneResponse response;
              response.set_status(msg::RADIO_TUNE_STATUS_FAILURE);
              channel_->sendTuneResponse(response, this->promise());
              return;
            }
            this->startTune(tuneTarget(commands, state_));
            for (const QVariantMap &command: commands) {
              this->command(command);
            }
          }

          void RadioService::onTuneDirectionRequest(const msg::RadioTuneDirectionRequest &request) {
            const int direction = request.direction() == msg::RADIO_TUNE_DIRECTION_DOWN ? -1 : 1;
            OPENAUTO_LOG(info) << "[RadioService] seek " << direction;
            this->startTune(tuneTarget({}, state_));
            this->command({{"cmd", "seek"}, {"value", direction}});
            if (!state_.value("playing").toBool()) {
              this->command({{"cmd", "play"}});
            }
          }

          void RadioService::onFavoriteToggleRequest(const msg::RadioFavoriteToggleRequest &request) {
            // applies to the current station; the answer (favorites and current station) follows with radio.state,
            // also when nothing changed (the phone's star stays wrong otherwise)
            OPENAUTO_LOG(info) << "[RadioService] favorite " << request.is_favorite() << " for "
                               << state_.value("frequency").toString().toStdString() << " "
                               << state_.value("band").toString().toStdString();
            favoriteReplyPending_ = true;
            if (request.is_favorite()) {
              this->command({{"cmd", "savePreset"}});
            } else {
              this->command({{"cmd", "removePreset"}, {"value", state_.value("frequency")}});
            }
          }

          void RadioService::onSearchRequest(const msg::RadioSearchRequest &request) {
            OPENAUTO_LOG(info) << "[RadioService] action " << request.action_id();
            if (request.action_id() == cScanDabAction) {
              // like FM, the phone may send it already when the DAB tab opens, and a DAB scan takes minutes: only
              // while no DAB station is known (later scans from the plugin page)
              if (!state_.value("bands").toMap().value("dab").toMap().value("stations").toList().isEmpty()) {
                OPENAUTO_LOG(info) << "[RadioService] DAB scan skipped, stations are known";
                return;
              }
              if (state_.value("band").toString() != "dab") {
                this->command({{"cmd", "band"}, {"value", "dab"}});
              }
              this->command({{"cmd", "scan"}});
              return;
            }
            if (request.action_id() != cScanFmAction) {
              return;
            }
            const auto now = std::chrono::steady_clock::now();
            const bool known = !state_.value("bands").toMap().value("fm").toMap().value("stations").toList().isEmpty();
            if (known && now - lastScan_ < cScanInterval) {
              OPENAUTO_LOG(info) << "[RadioService] scan skipped, the last one was less than 5 minutes ago";
              return;
            }
            lastScan_ = now;
            if (state_.value("band").toString() != "fm") {
              this->command({{"cmd", "band"}, {"value", "fm"}});
            }
            this->command({{"cmd", "scan"}});
          }

          void RadioService::onState(const QVariantMap &state) {
            state_ = state;
            if (!open_) {
              return;
            }
            if (pendingTune_.active) {
              if (!state.value("error").toString().isEmpty()) {
                this->finishTune(msg::RADIO_TUNE_STATUS_FAILURE);
              } else if (tuneArrived(pendingTune_.target, state)) {
                this->finishTune(msg::RADIO_TUNE_STATUS_SUCCESS);
              }
            }
            this->sendState(favoriteReplyPending_);
            favoriteReplyPending_ = false;
          }

          void RadioService::onLogos(const QVariantMap &logos) {
            // sent by the plugin only when a logo changed: scaled here once, not for every station list
            icons_ = stationIcons(logos);
            OPENAUTO_LOG(info) << "[RadioService] station logos: " << icons_.size();
            this->sendState(false);
          }

          void RadioService::setSlide(const QVariantMap &slide) {
            const uint64_t sid = dabSid(slide.value("station").toString());
            slideStation_ = sid > 0 ? dabKey(sid) : QString();
            // slideshow pictures are small (MOT SLS: 320x240, at most 50 KB); bigger ones are scaled
            slide_ = scaledIcon(slide.value("image").toString(), cSlideSize);
          }

          void RadioService::sendState(bool force) {
            if (!open_ || state_.isEmpty()) {
              return;
            }
            // The phone decides whether the current station is a favorite when the station info arrives, with the
            // favorites it knows by then: favorites and list first, and the station again after the favorites
            // changed (tested with a Fairphone 4: otherwise the star shows the old state).
            const auto favorites = favoriteList(state_, icons_);
            const bool favoritesChanged = force || favorites.SerializeAsString() != lastFavorites_;
            if (favoritesChanged) {
              lastFavorites_ = favorites.SerializeAsString();
              std::string list;
              for (const auto &favorite: favorites.favorites()) {
                list += " " + std::to_string(favorite.program_selector().primary_identifier().value());
              }
              OPENAUTO_LOG(info) << "[RadioService] favorites (kHz):" << list;
              channel_->sendFavoriteListNotification(favorites, this->promise());
            }
            const auto list = programList(state_, icons_);
            if (force || list.SerializeAsString() != lastProgramList_) {
              lastProgramList_ = list.SerializeAsString();
              OPENAUTO_LOG(info) << "[RadioService] station list: " << list.programs_size() << " stations, "
                                 << lastProgramList_.size() / 1024 << " KB";
              channel_->sendProgramListNotification(list, this->promise());
            }
            // the slideshow picture belongs to one DAB station
            const bool slideOfStation = state_.value("band").toString() == "dab" && slideStation_ == stationKey(state_);
            const auto info = programInfo(state_, icons_, slideOfStation ? slide_ : std::string());
            if (favoritesChanged || info.SerializeAsString() != lastProgramInfo_) {
              lastProgramInfo_ = info.SerializeAsString();
              channel_->sendProgramInfoNotification(info, this->promise());
            }
          }

          void RadioService::command(const QVariantMap &command) {
            core::EventBus::instance().emitEvent("radio.command", command);
          }

          void RadioService::startTune(const TuneTarget &target) {
            if (pendingTune_.active) {
              this->finishTune(msg::RADIO_TUNE_STATUS_CANCELLED);
            }
            pendingTune_ = {true, target};
            tuneTimer_.expires_from_now(boost::posix_time::milliseconds(cTuneTimeoutMs));
            tuneTimer_.async_wait(strand_.wrap([this, self = this->shared_from_this()](const boost::system::error_code &error) {
              if (!error && pendingTune_.active) {
                OPENAUTO_LOG(warning) << "[RadioService] the radio did not tune in time";
                this->finishTune(msg::RADIO_TUNE_STATUS_TIMEOUT);
              }
            }));
          }

          void RadioService::finishTune(msg::RadioTuneStatus status) {
            pendingTune_.active = false;
            tuneTimer_.cancel();
            msg::RadioTuneResponse response;
            response.set_status(status);
            channel_->sendTuneResponse(response, this->promise());
          }

          aasdk::channel::SendPromise::Pointer RadioService::promise() {
            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() {}, std::bind(&RadioService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            return promise;
          }

        }
      }
    }
  }
}
