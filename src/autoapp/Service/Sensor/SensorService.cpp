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


#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/VehicleGear.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/SimulatedDriving.hpp>
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

      // the gear from a plugin (CAN bus) and the simulated driving state, sent while the phone asked for them
      std::weak_ptr<SensorService> weak = this->shared_from_this();
      gearConnection_ = QObject::connect(&core::EventBus::instance(), &core::EventBus::event,
                                         [weak](const QString &topic, const QVariantMap &) {
        if (topic != "vehicle.gear" && topic != "vehicle.simulate") {
          return;
        }
        const bool simulate = topic == "vehicle.simulate";
        if (auto service = weak.lock()) {
          service->strand_.dispatch([service, simulate]() {
            if (simulate) {
              service->sendSimulatedDriving();
            } else if (service->gearRequested_) {
              service->sendGearData();
            }
          });
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

      QObject::disconnect(gearConnection_);
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
    // the gear only when a plugin publishes it (reverse camera, #76): Android Auto would wait for it otherwise
    const bool simulated = simulatedDriving(core::EventBus::instance().last("vehicle.simulate")).has_value();
    if (simulated || gearFromVehicle(core::EventBus::instance().last("vehicle.gear"))) {
      OPENAUTO_LOG(info) << "[SensorService] gear known: announcing SENSOR_GEAR";
      sensorChannel->add_sensors()->set_sensor_type(
          aap_protobuf::service::sensorsource::message::SensorType::SENSOR_GEAR);
    }
    // a simulated driving state (test): also the parking brake and the speed
    if (simulated) {
      OPENAUTO_LOG(info) << "[SensorService] driving state simulated: announcing SENSOR_PARKING_BRAKE, SENSOR_SPEED";
      sensorChannel->add_sensors()->set_sensor_type(
          aap_protobuf::service::sensorsource::message::SensorType::SENSOR_PARKING_BRAKE);
      sensorChannel->add_sensors()->set_sensor_type(
          aap_protobuf::service::sensorsource::message::SensorType::SENSOR_SPEED);
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

    if (request.type() == aap_protobuf::service::sensorsource::message::SENSOR_DRIVING_STATUS_DATA) {
      drivingStatusRequested_ = true;
      promise->then(std::bind(&SensorService::sendDrivingStatus, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE) {
      promise->then(std::bind(&SensorService::sendNightData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_GEAR) {
      gearRequested_ = true;
      sentGear_.reset();
      promise->then(std::bind(&SensorService::sendGearData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_PARKING_BRAKE) {
      parkingBrakeRequested_ = true;
      promise->then(std::bind(&SensorService::sendParkingBrakeData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_SPEED) {
      speedRequested_ = true;
      promise->then(std::bind(&SensorService::sendSpeedData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else {
      promise->then([]() {},
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    }

    channel_->sendSensorStartResponse(response, std::move(promise));
    channel_->receive(this->shared_from_this());
  }

  void SensorService::sendDrivingStatus() {
    // unrestricted unless a driving state is simulated
    const auto simulated = simulatedDriving(core::EventBus::instance().last("vehicle.simulate"));
    const int status = simulated ? simulated->drivingStatus
                                 : aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED;
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

  void SensorService::sendGearData() {
    const auto simulated = simulatedDriving(core::EventBus::instance().last("vehicle.simulate"));
    const auto gear = simulated ? std::optional(simulated->gear)
                                : gearFromVehicle(core::EventBus::instance().last("vehicle.gear"));
    if (!gear || gear == sentGear_) {
      return;
    }
    sentGear_ = gear;
    OPENAUTO_LOG(info) << "[SensorService] sendGearData(): " << aap_protobuf::service::sensorsource::message::Gear_Name(*gear);

    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_gear_data()->set_gear(*gear);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sendParkingBrakeData() {
    const auto simulated = simulatedDriving(core::EventBus::instance().last("vehicle.simulate"));
    if (!simulated) {
      return;
    }
    OPENAUTO_LOG(info) << "[SensorService] sendParkingBrakeData(): " << simulated->parkingBrake;
    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_parking_brake_data()->set_parking_brake(simulated->parkingBrake);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sendSpeedData() {
    const auto simulated = simulatedDriving(core::EventBus::instance().last("vehicle.simulate"));
    if (!simulated) {
      return;
    }
    OPENAUTO_LOG(debug) << "[SensorService] sendSpeedData(): " << simulated->speedE3;
    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_speed_data()->set_speed_e3(simulated->speedE3);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sendSimulatedDriving() {
    // the sensors the phone asked for; the parking brake and the speed only exist while a state is simulated
    if (drivingStatusRequested_) {
      this->sendDrivingStatus();
    }
    if (gearRequested_) {
      this->sendGearData();
    }
    if (parkingBrakeRequested_) {
      this->sendParkingBrakeData();
    }
    if (speedRequested_) {
      this->sendSpeedData();
    }
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
        // a car sends its speed all the time
        if (this->speedRequested_) {
          this->sendSpeedData();
        }

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



