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


#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/VehicleSensors.hpp>
#include <f1x/openauto/autoapp/Plugins/AndroidAutoProviders.hpp>
#include <fstream>
#include <cmath>
#include <gps.h>

namespace f1x::openauto::autoapp::service::sensor {
  SensorService::SensorService(boost::asio::io_service &ioService,
                               aasdk::messenger::IMessenger::Pointer messenger)
      : strand_(ioService),
        timer_(ioService),
        channel_(std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(strand_, std::move(messenger))) {

  }

  void SensorService::start() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      if (gps_open("127.0.0.1", "2947", &this->gpsData_)) {
        OPENAUTO_LOG(warning) << "[SensorService] can't connect to GPSD.";
      } else {
        OPENAUTO_LOG(info) << "[SensorService] Connected to GPSD.";
        gps_stream(&this->gpsData_, WATCH_ENABLE | WATCH_JSON, NULL);
        this->gpsEnabled_ = true;
      }

      if (this->isNightMode()) {
        this->isNight = true;
      }
      this->sensorPolling();

      OPENAUTO_LOG(info) << "[SensorService] start()";
      channel_->receive(this->shared_from_this());

      // vehicle data from plugins and the API (#115), sent while the phone asked for it
      std::weak_ptr<SensorService> weak = this->shared_from_this();
      vehicleConnection_ = QObject::connect(&core::EventBus::instance(), &core::EventBus::event,
                                            [weak](const QString &topic, const QVariantMap &) {
        if (!topic.startsWith("vehicle.")) {
          return;
        }
        if (auto service = weak.lock()) {
          service->strand_.dispatch([service, topic]() { service->onVehicleTopic(topic); });
        }
      });
    });

  }

  void SensorService::stop() {
    // Set atomic flag first to prevent new timer callbacks from being scheduled
    this->stopPolling.store(true, std::memory_order_release);

    strand_.dispatch([this, self = this->shared_from_this()]() {
      // Cancel any pending timers to stop scheduling further polling callbacks immediately
      boost::system::error_code ec;
      this->timer_.cancel(ec);
      if (ec) {
        OPENAUTO_LOG(warning) << "[SensorService] timer cancel error: " << ec.message();
      }
      if (this->gpsEnabled_) {
        gps_stream(&this->gpsData_, WATCH_DISABLE, NULL);
        gps_close(&this->gpsData_);
        this->gpsEnabled_ = false;
      }

      QObject::disconnect(vehicleConnection_);
      OPENAUTO_LOG(info) << "[SensorService] stop()";
    });
  }

  void SensorService::pause() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[SensorService] pause()";
    });
  }

  void SensorService::resume() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[SensorService] resume()";
    });
  }

  void SensorService::fillFeatures(
      aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
    OPENAUTO_LOG(info) << "[SensorService] fillFeatures()";

    auto *service = response.add_channels();
    service->set_id(static_cast<uint32_t>(channel_->getId()));

    auto *sensorChannel = service->mutable_sensor_source_service();
    sensorChannel->add_sensors()->set_sensor_type(
        aap_protobuf::service::sensorsource::message::SensorType::SENSOR_DRIVING_STATUS_DATA);
    sensorChannel->add_sensors()->set_sensor_type(
        aap_protobuf::service::sensorsource::message::SensorType::SENSOR_LOCATION);
    sensorChannel->add_sensors()->set_sensor_type(
        aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE);
    // vehicle data (#115, #123): what an enabled plugin declares in its manifest (values may follow later, the
    // phone does not wait for them), and what an API client that is not a plugin publishes right now
    const QVariantMap pluginsState = core::EventBus::instance().last("plugins.state");
    for (const auto &sensor: vehicleSensors()) {
      const QString name = QString(sensor.topic).mid(8);
      const bool declared = plugins::androidAutoDeclared(pluginsState, "sensors", name);
      if (declared || vehicleSensorData(sensor, androidAutoTopic)) {
        OPENAUTO_LOG(info) << "[SensorService] " << sensor.topic << (declared ? " declared" : " known")
                           << ": announcing " << aap_protobuf::service::sensorsource::message::SensorType_Name(sensor.type);
        sensorChannel->add_sensors()->set_sensor_type(sensor.type);
      }
    }
    const QVariantMap info = androidAutoTopic("vehicle.info");
    for (const auto type: fuelTypes(info)) {
      sensorChannel->add_supported_fuel_types(type);
    }
    for (const auto type: evConnectorTypes(info)) {
      sensorChannel->add_supported_ev_connector_types(type);
    }
  }

  void SensorService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
    OPENAUTO_LOG(info) << "[SensorService] onChannelOpenRequest()";
    OPENAUTO_LOG(debug) << "[SensorService] Channel Id: " << request.service_id() << ", Priority: "
                        << request.priority();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    const aap_protobuf::shared::MessageStatus status = aap_protobuf::shared::MessageStatus::STATUS_SUCCESS;
    response.set_status(status);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendChannelOpenResponse(response, std::move(promise));

    channel_->receive(this->shared_from_this());
  }

  void SensorService::onSensorStartRequest(
      const aap_protobuf::service::sensorsource::message::SensorRequest &request) {
    OPENAUTO_LOG(info) << "[SensorService] onSensorStartRequest()";
    OPENAUTO_LOG(debug) << "[SensorService] Request Type: " << request.type();

    aap_protobuf::service::sensorsource::message::SensorStartResponseMessage response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    auto promise = aasdk::channel::SendPromise::defer(strand_);

    const auto type = request.type();
    if (type == aap_protobuf::service::sensorsource::message::SENSOR_DRIVING_STATUS_DATA) {
      drivingStatusRequested_ = true;
      sentDrivingStatus_.reset();
      promise->then(std::bind(&SensorService::sendDrivingStatus, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (type == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE) {
      promise->then(std::bind(&SensorService::sendNightData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (const VehicleSensor *sensor = findVehicleSensor(type)) {
      // min_update_period in ms, 0 = on change; continuous values at most every 250 ms (the polling)
      VehicleSensorState &state = vehicleState_[type];
      state = VehicleSensorState{};
      state.requested = true;
      state.period = std::chrono::milliseconds(std::max<int64_t>(request.min_update_period(), 0));
      promise->then([this, self = this->shared_from_this(), sensor]() { this->sendVehicleSensor(*sensor, true); },
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else {
      promise->then([]() {},
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    }

    channel_->sendSensorStartResponse(response, std::move(promise));
    channel_->receive(this->shared_from_this());
  }

  void SensorService::sendDrivingStatus() {
    const int status = drivingStatus(androidAutoTopic);
    if (status == sentDrivingStatus_) {
      return;
    }
    sentDrivingStatus_ = status;
    OPENAUTO_LOG(info) << "[SensorService] sendDrivingStatus(): " << status;
    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_driving_status_data()->set_status(status);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sendNightData() {
    OPENAUTO_LOG(info) << "[SensorService] sendNightData()";
    aap_protobuf::service::sensorsource::message::SensorBatch indication;

    if (SensorService::isNight) {
      OPENAUTO_LOG(info) << "[SensorService] Night Mode Triggered";
      indication.add_night_mode_data()->set_night_mode(true);
    } else {
      OPENAUTO_LOG(info) << "[SensorService] Day Mode Triggered";
      indication.add_night_mode_data()->set_night_mode(false);
    }

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
    if (this->firstRun) {
      this->firstRun = false;
      this->previous = this->isNight;
    }
  }

  const VehicleSensor *SensorService::findVehicleSensor(aap_protobuf::service::sensorsource::message::SensorType type) {
    for (const auto &sensor: vehicleSensors()) {
      if (sensor.type == type) {
        return &sensor;
      }
    }
    return nullptr;
  }

  QVariantMap SensorService::androidAutoTopic(const QString &topic) {
    const auto &bus = core::EventBus::instance();
    if (!plugins::androidAutoSourceAllowed(bus.last("plugins.state"), topic, bus.source(topic))) {
      return QVariantMap();
    }
    return bus.last(topic);
  }

  void SensorService::onVehicleTopic(const QString &topic) {
    // the simulated driving state replaces gear, parking brake and speed
    const bool simulate = topic == "vehicle.simulate";
    for (const auto &sensor: vehicleSensors()) {
      if (vehicleState_[sensor.type].requested && (simulate || topic == sensor.topic)) {
        this->sendVehicleSensor(sensor, false);
      }
    }
    if (drivingStatusRequested_) {
      this->sendDrivingStatus();
    }
  }

  void SensorService::sendVehicleSensor(const VehicleSensor &sensor, bool force) {
    VehicleSensorState &state = vehicleState_[sensor.type];
    const auto batch = vehicleSensorData(sensor, androidAutoTopic);
    // no usable value any more (e.g. "available": false from a plugin that lost the CAN bus): tell the phone once
    if (!batch) {
      if (!state.failed) {
        state.failed = true;
        state.sent.clear();
        this->sendSensorError(sensor.type, aap_protobuf::service::sensorsource::message::SensorErrorType::SENSOR_ERROR_TRANSIENT);
      }
      return;
    }
    if (state.failed) {
      state.failed = false;
      this->sendSensorError(sensor.type, aap_protobuf::service::sensorsource::message::SensorErrorType::SENSOR_OK);
    }
    const std::string data = batch->SerializeAsString();
    if (!force && data == state.sent) {
      return;
    }
    state.sent = data;
    state.sentAt = std::chrono::steady_clock::now();
    OPENAUTO_LOG(debug) << "[SensorService] " << sensor.topic << ": " << batch->ShortDebugString();

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(*batch, std::move(promise));
  }

  void SensorService::sendContinuousVehicleSensors() {
    // a car sends speed, rpm and heading all the time, plugins publish only changes
    const auto now = std::chrono::steady_clock::now();
    for (const auto &sensor: vehicleSensors()) {
      VehicleSensorState &state = vehicleState_[sensor.type];
      if (sensor.continuous && state.requested && !state.failed && now - state.sentAt >= state.period) {
        this->sendVehicleSensor(sensor, true);
      }
    }
  }

  void SensorService::sendSensorError(aap_protobuf::service::sensorsource::message::SensorType type,
                                      aap_protobuf::service::sensorsource::message::SensorErrorType error) {
    OPENAUTO_LOG(info) << "[SensorService] sendSensorError(): "
                       << aap_protobuf::service::sensorsource::message::SensorType_Name(type) << " "
                       << aap_protobuf::service::sensorsource::message::SensorErrorType_Name(error);
    aap_protobuf::service::sensorsource::message::SensorError message;
    message.set_sensor_type(type);
    message.set_sensor_error_type(error);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorError(message, std::move(promise));
  }

  void SensorService::sendGPSLocationData() {
    OPENAUTO_LOG(info) << "[SensorService] sendGPSLocationData()";
    aap_protobuf::service::sensorsource::message::SensorBatch indication;

    auto *locInd = indication.add_location_data();

    // epoch seconds
    // Note: set_timestamp() is deprecated but still needed for compatibility
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#if GPSD_API_MAJOR_VERSION >= 7
    locInd->set_timestamp(this->gpsData_.fix.time.tv_sec);
#else
    locInd->set_timestamp(this->gpsData_.fix.time);
#endif
    #pragma GCC diagnostic pop
    // degrees
    locInd->set_latitude_e7(this->gpsData_.fix.latitude * 1e7);
    locInd->set_longitude_e7(this->gpsData_.fix.longitude * 1e7);
    // meters
    auto accuracy = sqrt(pow(this->gpsData_.fix.epx, 2) + pow(this->gpsData_.fix.epy, 2));
    locInd->set_accuracy_e3(accuracy * 1e3);

    if (this->gpsData_.set & ALTITUDE_SET) {
      // meters above ellipsoid
      locInd->set_altitude_e2(this->gpsData_.fix.altitude * 1e2);
    }
    if (this->gpsData_.set & SPEED_SET) {
      // meters per second to knots
      locInd->set_speed_e3(this->gpsData_.fix.speed * 1.94384 * 1e3);
    }
    if (this->gpsData_.set & TRACK_SET) {
      // degrees
      locInd->set_bearing_e6(this->gpsData_.fix.track * 1e6);
    }

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sensorPolling() {
    OPENAUTO_LOG(debug) << "[SensorService] sensorPolling()";
    if (!this->stopPolling.load(std::memory_order_acquire)) {
      strand_.dispatch([this, self = this->shared_from_this()]() {
        // Check stopPolling again inside the dispatched handler to catch late stop() calls
        if (this->stopPolling.load(std::memory_order_acquire)) {
          OPENAUTO_LOG(info) << "[SensorService] sensorPolling() aborted due to stop.";
          return;
        }
        
        this->isNight = this->isNightMode();
        if (this->previous != this->isNight && !this->firstRun) {
          this->previous = this->isNight;
          this->sendNightData();
        }
        // only with a gpsd connection (reading an unopened gps_data_t crashes) and when data is waiting
        // (gps_read blocks otherwise)
        bool gpsDataAvailable = false;
        if (this->gpsEnabled_ && gps_waiting(&this->gpsData_, 0)) {
#if GPSD_API_MAJOR_VERSION >= 7
          gpsDataAvailable = gps_read(&this->gpsData_, NULL, 0) != -1;
#else
          gpsDataAvailable = gps_read(&this->gpsData_) != -1;
#endif
        }
        if ((gpsDataAvailable == true) &&
            (this->gpsData_.fix.mode == MODE_2D || this->gpsData_.fix.mode == MODE_3D) &&
            (this->gpsData_.set & TIME_SET) &&
            (this->gpsData_.set & LATLON_SET))
        {
          this->sendGPSLocationData();
        }
        this->sendContinuousVehicleSensors();

        // Final check before rescheduling to prevent race with stop()
        if (!this->stopPolling.load(std::memory_order_acquire)) {
          timer_.expires_from_now(boost::posix_time::milliseconds(250));
          timer_.async_wait(strand_.wrap(std::bind(&SensorService::sensorPolling, this->shared_from_this())));
        } else {
          OPENAUTO_LOG(info) << "[SensorService] sensorPolling() stopped, timer not rescheduled.";
        }
      });
    }
  }

  bool SensorService::isNightMode() {
    // Settings → Android Auto → Night mode; "auto": set via API/plugins ("daynight.set", e.g. from a light sensor
    // or the CAN bus)
    const QString mode = core::EventBus::instance().last("system.nightMode").value("mode").toString();
    if (mode == "day" || mode == "night") {
      return mode == "night";
    }
    return core::EventBus::instance().last("system.daynight").value("night").toBool();
  }

  void SensorService::onChannelError(const aasdk::error::Error &e) {
    // OPERATION_ABORTED is expected during shutdown when messenger stops
    if (e.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED) {
      OPENAUTO_LOG(debug) << "[SensorService] onChannelError(): " << e.what() << " (expected during stop)";
    } else {
      OPENAUTO_LOG(error) << "[SensorService] onChannelError(): " << e.what();
    }
    
    // Stop polling on any channel error to prevent hanging on exit
    if (!this->stopPolling.load(std::memory_order_acquire)) {
      OPENAUTO_LOG(info) << "[SensorService] onChannelError(): stopping sensor polling due to channel error";
      this->stop();
    }
  }
}



