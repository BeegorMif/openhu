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

#include <aap_protobuf/service/carcontrol/message/VehiclePropertyId.pb.h>
#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Plugins/AndroidAutoProviders.hpp>
#include <f1x/openauto/autoapp/Service/CarControl/CarControlService.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/VehicleSensors.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace carcontrol {

          namespace {

            // the phone shows a write optimistically and falls back after 2 s without the new value
            constexpr int cResultTimeoutMs = 2000;

            // vehicle.controls as Android Auto may see it (#123): empty from a plugin that does not declare carControl
            QVariantMap controlsData() {
              const auto &bus = core::EventBus::instance();
              if (!plugins::androidAutoServiceSourceAllowed(bus.last("plugins.state"), "carControl",
                                                            bus.source("vehicle.controls"))) {
                return QVariantMap();
              }
              return bus.last("vehicle.controls");
            }

          }

          CarControlService::CarControlService(boost::asio::io_service &ioService,
                                               aasdk::messenger::IMessenger::Pointer messenger)
              : ioService_(ioService),
                strand_(ioService),
                channel_(std::make_shared<aasdk::channel::carcontrol::CarControlService>(strand_, std::move(messenger))),
                data_(controlsData()),
                controls_(parseControls(data_)) {
            for (const QString &ignored: controls_.ignored) {
              OPENAUTO_LOG(warning) << "[CarControlService] vehicle.controls: " << ignored.toStdString();
            }
          }

          bool CarControlService::available() {
            return !parseControls(controlsData()).controls.empty();
          }

          void CarControlService::start() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[CarControlService] start(), " << controls_.controls.size() << " controls";
              std::weak_ptr<CarControlService> weak = self;
              busConnection_ = QObject::connect(&core::EventBus::instance(), &core::EventBus::event,
                                                [weak](const QString &topic, const QVariantMap &data) {
                if (topic != "vehicle.controls" && topic != "vehicle.control.result") {
                  return;
                }
                auto service = weak.lock();
                if (!service) {
                  return;
                }
                if (topic == "vehicle.control.result") {
                  service->strand_.dispatch([service, data]() { service->onResult(data); });
                } else {
                  const QVariantMap controls = controlsData();
                  service->strand_.dispatch([service, controls]() { service->onControls(controls); });
                }
              });
              channel_->receive(this->shared_from_this());
            });
          }

          void CarControlService::stop() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[CarControlService] stop()";
              QObject::disconnect(busConnection_);
              for (auto &entry: pending_) {
                entry.second.timer->cancel();
              }
              pending_.clear();
            });
          }

          void CarControlService::pause() {
          }

          void CarControlService::resume() {
          }

          void CarControlService::fillFeatures(
              aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
            OPENAUTO_LOG(info) << "[CarControlService] fillFeatures()";
            auto *service = response.add_channels();
            service->set_id(static_cast<uint32_t>(channel_->getId()));
            *service->mutable_car_control_service() = descriptor(controls_);
          }

          void CarControlService::onChannelOpenRequest(
              const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
            OPENAUTO_LOG(info) << "[CarControlService] onChannelOpenRequest(), priority " << request.priority();
            aap_protobuf::service::control::message::ChannelOpenResponse response;
            response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
            channel_->sendChannelOpenResponse(response, this->promise());
            channel_->receive(this->shared_from_this());
          }

          void CarControlService::onChannelError(const aasdk::error::Error &e) {
            OPENAUTO_LOG(error) << "[CarControlService] onChannelError(): " << e.what();
          }

          void CarControlService::onRegisterListenersRequest(const msg::RegisterCarPropertyListenersRequest &request) {
            // the current values first, then the result (as the phone's own simulator does)
            msg::RegisterCarPropertyListenersResponse response;
            const auto units = unitsEvent(controls_);
            for (const auto &property: request.properties()) {
              const Key key{property.property_id(), property.area().has_global() ? 0 : areaBits(property.area())};
              auto *result = response.add_results();
              *result->mutable_property() = property;
              result->set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
              if (const Control *control = find(controls_, property)) {
                registered_.insert(key);
                this->sendValue(key, changeEvent(*control, entryFor(data_, *control)), true);
              } else if (units && property.property_id() == msg::HVAC_TEMPERATURE_DISPLAY_UNITS) {
                registered_.insert(key);
                this->sendValue(key, *units, true);
              } else {
                OPENAUTO_LOG(info) << "[CarControlService] register: unknown property " << property.property_id();
                result->set_status(aap_protobuf::shared::MessageStatus::STATUS_CAR_PROPERTY_LISTENER_REGISTRATION_FAILED);
              }
            }
            OPENAUTO_LOG(info) << "[CarControlService] registered " << request.properties_size() << " properties";
            channel_->sendRegisterListenersResponse(response, this->promise());
          }

          void CarControlService::onSetPropertyValueRequest(const msg::SetCarPropertyValueRequest &request) {
            const Control *control = find(controls_, request.property());
            if (control == nullptr || !control->writable) {
              OPENAUTO_LOG(info) << "[CarControlService] set: not writable " << request.property().property_id();
              this->respond(request.request_id(), false, 2);
              return;
            }
            // the plugin decides what it allows while driving
            const bool restricted = sensor::drivingStatus(sensor::SensorService::androidAutoTopic)
                                    != aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED;
            const QVariantMap event = setEvent(*control, request, restricted);
            OPENAUTO_LOG(info) << "[CarControlService] set " << control->name.toStdString() << " "
                               << control->area.toStdString() << " = " << event.value("value").toString().toStdString();

            auto timer = std::make_shared<boost::asio::deadline_timer>(ioService_);
            pending_[request.request_id()] = Pending{request.property(), timer};
            timer->expires_from_now(boost::posix_time::milliseconds(cResultTimeoutMs));
            timer->async_wait(strand_.wrap([this, self = this->shared_from_this(), id = request.request_id()](
                const boost::system::error_code &error) {
              if (!error && pending_.count(id)) {
                OPENAUTO_LOG(info) << "[CarControlService] no vehicle.control.result for " << id;
                this->respond(id, false, 1);
              }
            }));
            core::EventBus::instance().emitEvent("vehicle.control.set", event);
          }

          void CarControlService::onCarActionNotification(const msg::CarActionNotification &notification) {
            const QString action = notification.action().action_id() == msg::CAR_ACTION_LAUNCH_HVAC
                                   ? "launchHvac" : QString::number(notification.action().action_id());
            OPENAUTO_LOG(info) << "[CarControlService] action " << action.toStdString();
            core::EventBus::instance().emitEvent("vehicle.control.action", {{"action", action}});
          }

          void CarControlService::onControls(const QVariantMap &data) {
            data_ = data;
            // the structure stays as announced; a plugin that ended leaves {"available": false}
            for (const auto &control: controls_.controls) {
              const Key key{control.propertyId, control.areaBits};
              if (registered_.count(key)) {
                this->sendValue(key, changeEvent(control, entryFor(data_, control)), false);
              }
            }
          }

          void CarControlService::onResult(const QVariantMap &result) {
            const std::string id = result.value("requestId").toString().toStdString();
            if (!pending_.count(id)) {
              return;
            }
            const bool ok = result.value("ok", true).toBool();
            if (!ok) {
              OPENAUTO_LOG(info) << "[CarControlService] refused by the plugin: "
                                 << result.value("error").toString().toStdString();
            }
            this->respond(id, ok, ok ? 0 : 3);
          }

          void CarControlService::sendValue(const Key &key, const msg::CarPropertyChangeEvent &event, bool force) {
            const std::string data = event.SerializeAsString();
            if (!force && sent_[key] == data) {
              return;
            }
            sent_[key] = data;
            channel_->sendPropertyChangeEvent(event, this->promise());
          }

          void CarControlService::respond(const std::string &requestId, bool ok, int errorCode) {
            msg::SetCarPropertyValueResponse response;
            auto it = pending_.find(requestId);
            if (it != pending_.end()) {
              *response.mutable_property() = it->second.property;
              it->second.timer->cancel();
              pending_.erase(it);
            }
            response.set_status(ok ? aap_protobuf::shared::MessageStatus::STATUS_SUCCESS
                                   : aap_protobuf::shared::MessageStatus::STATUS_CAR_PROPERTY_SET_REQUEST_FAILED);
            response.set_request_id(requestId);
            response.set_error_code(errorCode);
            channel_->sendSetPropertyValueResponse(response, this->promise());
          }

          aasdk::channel::SendPromise::Pointer CarControlService::promise() {
            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() {}, std::bind(&CarControlService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            return promise;
          }

        }
      }
    }
  }
}
