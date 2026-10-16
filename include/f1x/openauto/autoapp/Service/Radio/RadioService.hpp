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

#include <chrono>
#include <string>
#include <QMetaObject>
#include <QVariantMap>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <aasdk/Channel/Radio/RadioService.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <f1x/openauto/autoapp/Service/Radio/RadioMapping.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace radio {

          // The radio plugin as a media source in Android Auto (openhu#75): radio.state goes to the phone as
          // station list, current station and favorites, the phone's requests become radio.command events.
          // Created only while the plugin runs (ServiceFactory), the sound stays with the plugin.
          class RadioService :
              public aasdk::channel::radio::IRadioServiceEventHandler,
              public IService,
              public std::enable_shared_from_this<RadioService> {
          public:
            RadioService(boost::asio::io_service &ioService, aasdk::messenger::IMessenger::Pointer messenger);

            void start() override;
            void stop() override;
            void pause() override;
            void resume() override;
            void fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) override;

            void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) override;
            void onChannelError(const aasdk::error::Error &e) override;

            void onMuteRequest(const aap_protobuf::service::radio::message::RadioMuteRequest &request) override;
            void onTuneRequest(const aap_protobuf::service::radio::message::RadioTuneRequest &request) override;
            void onFavoriteToggleRequest(const aap_protobuf::service::radio::message::RadioFavoriteToggleRequest &request) override;
            void onTuneDirectionRequest(const aap_protobuf::service::radio::message::RadioTuneDirectionRequest &request) override;
            void onSearchRequest(const aap_protobuf::service::radio::message::RadioSearchRequest &request) override;

          private:
            using std::enable_shared_from_this<RadioService>::shared_from_this;

            void onState(const QVariantMap &state);
            void onLogos(const QVariantMap &logos);
            void setSlide(const QVariantMap &slide);
            // favorites, station list and current station; only what changed unless force
            void sendState(bool force);
            void command(const QVariantMap &command);

            // a tune or seek waits for radio.state with the new station, then answers the phone
            void startTune(const TuneTarget &target);
            void finishTune(aap_protobuf::service::radio::message::RadioTuneStatus status);
            aasdk::channel::SendPromise::Pointer promise();

            boost::asio::io_service::strand strand_;
            boost::asio::deadline_timer tuneTimer_;
            aasdk::channel::radio::RadioService::Pointer channel_;
            QMetaObject::Connection busConnection_;
            QVariantMap state_;
            // station logos (radio.logos), scaled for the phone
            StationIcons icons_;
            // DAB slideshow picture (radio.slide) and the station it belongs to
            static constexpr int cSlideSize = 400;
            std::string slide_;
            QString slideStation_;
            bool open_ = false;
            // a favorite request waits for radio.state, then favorites and station go out even if unchanged
            bool favoriteReplyPending_ = false;
            // start of the connection counts as a scan: opening the FM tab right away does not silence the radio
            std::chrono::steady_clock::time_point lastScan_ = std::chrono::steady_clock::now();
            std::string lastProgramList_;
            std::string lastProgramInfo_;
            std::string lastFavorites_;

            struct PendingTune {
              bool active = false;
              TuneTarget target;
            } pendingTune_;
          };

        }
      }
    }
  }
}
