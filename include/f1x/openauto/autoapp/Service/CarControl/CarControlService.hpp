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

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <QMetaObject>
#include <QVariantMap>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <aasdk/Channel/CarControl/CarControlService.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <f1x/openauto/autoapp/Service/CarControl/CarControlMapping.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace carcontrol {

          // Car controls in Android Auto (openhu#121): the controls a plugin describes in vehicle.controls go to the
          // phone's climate rail, the phone's changes become vehicle.control.set events for the plugin. Created only
          // when vehicle.controls has controls at the connect (ServiceFactory): the structure is fixed for a
          // connection, the values follow vehicle.controls.
          class CarControlService :
              public aasdk::channel::carcontrol::ICarControlServiceEventHandler,
              public IService,
              public std::enable_shared_from_this<CarControlService> {
          public:
            CarControlService(boost::asio::io_service &ioService, aasdk::messenger::IMessenger::Pointer messenger);

            // vehicle.controls from a source that may feed Android Auto, with at least one control
            static bool available();

            void start() override;
            void stop() override;
            void pause() override;
            void resume() override;
            void fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) override;

            void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) override;
            void onChannelError(const aasdk::error::Error &e) override;
            void onRegisterListenersRequest(const msg::RegisterCarPropertyListenersRequest &request) override;
            void onSetPropertyValueRequest(const msg::SetCarPropertyValueRequest &request) override;
            void onCarActionNotification(const msg::CarActionNotification &notification) override;

          private:
            using std::enable_shared_from_this<CarControlService>::shared_from_this;
            using Key = std::pair<int32_t, uint32_t>;  // property id, area bits

            void onControls(const QVariantMap &data);
            void onResult(const QVariantMap &result);
            // the value of a registered property, only when it changed unless force
            void sendValue(const Key &key, const msg::CarPropertyChangeEvent &event, bool force);
            void respond(const std::string &requestId, bool ok, int errorCode);
            aasdk::channel::SendPromise::Pointer promise();

            boost::asio::io_service &ioService_;
            boost::asio::io_service::strand strand_;
            aasdk::channel::carcontrol::CarControlService::Pointer channel_;
            QMetaObject::Connection busConnection_;
            QVariantMap data_;
            Controls controls_;
            std::set<Key> registered_;
            std::map<Key, std::string> sent_;

            // writes wait for vehicle.control.result of the plugin, at most cResultTimeoutMs
            struct Pending {
              msg::CarProperty property;
              std::shared_ptr<boost::asio::deadline_timer> timer;
            };
            std::map<std::string, Pending> pending_;
          };

        }
      }
    }
  }
}
