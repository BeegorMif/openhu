/*
*  This file is part of OpenHU.
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

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>
#include <QStringList>
#include <QVariantMap>
#include <aap_protobuf/service/sensorsource/message/DrivingStatus.pb.h>
#include <aap_protobuf/service/sensorsource/message/EvConnectorType.pb.h>
#include <aap_protobuf/service/sensorsource/message/FuelType.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorBatch.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>
#include <f1x/openauto/autoapp/Service/Sensor/SimulatedDriving.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/VehicleGear.hpp>

namespace f1x::openauto::autoapp::service::sensor {

  // Vehicle data from plugins and API clients -> Android Auto sensors (#115). OpenHU reads no vehicle data itself:
  // the "vehicle.*" topics are the contract (docs/api.md). Which sources may feed Android Auto and what is announced:
  // Plugins/AndroidAutoProviders.hpp (#123).

  using SensorBatch = aap_protobuf::service::sensorsource::message::SensorBatch;
  using SensorType = aap_protobuf::service::sensorsource::message::SensorType;
  // the last value of a topic (EventBus::last, a map in the tests)
  using TopicLookup = std::function<QVariantMap(const QString &)>;

  // every restriction a car applies while it moves
  constexpr int kFullyRestricted = aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_VIDEO
      | aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_KEYBOARD_INPUT
      | aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_VOICE_INPUT
      | aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_CONFIG
      | aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_LIMIT_MESSAGE_LEN;
  // below this the car stands (Android Auto's own limit is 0.5 m/s = 1.8 km/h)
  constexpr double kParkedKmh = 2.0;

  // A topic without a value, or with "available": false (a plugin lost its source, e.g. the CAN bus): no data
  inline bool topicAvailable(const QVariantMap &data) {
    return !data.isEmpty() && data.value("available", true).toBool();
  }

  inline std::optional<double> number(const QVariantMap &data, const QString &field) {
    const QVariant value = data.value(field);
    if (!value.isValid() || value.isNull()) {
      return std::nullopt;
    }
    bool ok = false;
    const double result = value.toDouble(&ok);
    return ok && std::isfinite(result) ? std::optional(result) : std::nullopt;
  }

  inline std::optional<bool> flag(const QVariantMap &data, const QString &field) {
    const QVariant value = data.value(field);
    if (!value.isValid() || value.isNull()) {
      return std::nullopt;
    }
    return value.toBool();
  }

  inline int32_t scaled(double value, double factor) {
    return static_cast<int32_t>(std::lround(value * factor));
  }

  // The value of a vehicle topic as Android Auto gets it: a simulated driving state ("vehicle.simulate", #119)
  // replaces gear, parking brake, speed and driving status
  inline QVariantMap vehicleTopic(const QString &topic, const TopicLookup &last) {
    if (const auto simulated = simulatedDriving(last("vehicle.simulate"))) {
      if (topic == "vehicle.gear") {
        using aap_protobuf::service::sensorsource::message::Gear;
        return {{"gear", simulated->gear == Gear::GEAR_PARK ? "park"
                         : simulated->gear == Gear::GEAR_REVERSE ? "reverse" : "drive"}};
      }
      if (topic == "vehicle.parkingBrake") {
        return {{"engaged", simulated->parkingBrake}};
      }
      if (topic == "vehicle.speed") {
        return {{"kmh", simulated->kmh}};
      }
      if (topic == "vehicle.drivingStatus") {
        return {{"status", simulated->drivingStatus}};
      }
    }
    return last(topic);
  }

  // --- one function per sensor: false = no usable value in the topic ---

  inline bool fillSpeed(const QVariantMap &data, SensorBatch &batch) {
    const auto kmh = number(data, "kmh");
    if (!kmh) {
      return false;
    }
    auto *speed = batch.add_speed_data();
    speed->set_speed_e3(scaled(*kmh / 3.6, 1000));
    if (const auto cruise = flag(data, "cruise")) {
      speed->set_cruise_engaged(*cruise);
    }
    return true;
  }

  inline bool fillParkingBrake(const QVariantMap &data, SensorBatch &batch) {
    const auto engaged = flag(data, "engaged");
    if (!engaged) {
      return false;
    }
    batch.add_parking_brake_data()->set_parking_brake(*engaged);
    return true;
  }

  inline bool fillGear(const QVariantMap &data, SensorBatch &batch) {
    const auto gear = gearFromVehicle(data);
    if (!gear) {
      return false;
    }
    batch.add_gear_data()->set_gear(*gear);
    return true;
  }

  // level in % of a full tank, range in km (units on the wire unverified)
  inline bool fillFuel(const QVariantMap &data, SensorBatch &batch) {
    const auto percent = number(data, "percent");
    const auto range = number(data, "rangeKm");
    const auto low = flag(data, "low");
    if (!percent && !range && !low) {
      return false;
    }
    auto *fuel = batch.add_fuel_data();
    if (percent) {
      fuel->set_fuel_level(scaled(std::clamp(*percent, 0.0, 100.0), 1));
    }
    if (range) {
      fuel->set_range(scaled(*range, 1));
    }
    if (low) {
      fuel->set_low_fuel_warning(*low);
    }
    return true;
  }

  inline bool fillOdometer(const QVariantMap &data, SensorBatch &batch) {
    const auto km = number(data, "km");
    if (!km) {
      return false;
    }
    auto *odometer = batch.add_odometer_data();
    odometer->set_kms_e1(scaled(*km, 10));
    if (const auto trip = number(data, "tripKm")) {
      odometer->set_trip_kms_e1(scaled(*trip, 10));
    }
    return true;
  }

  inline bool fillRpm(const QVariantMap &data, SensorBatch &batch) {
    const auto rpm = number(data, "rpm");
    if (!rpm) {
      return false;
    }
    batch.add_rpm_data()->set_rpm_e3(scaled(*rpm, 1000));
    return true;
  }

  // outside temperature in °C, pressure in kPa
  inline bool fillEnvironment(const QVariantMap &data, SensorBatch &batch) {
    const auto temperature = number(data, "outsideTemp");
    const auto pressure = number(data, "pressureKpa");
    if (!temperature && !pressure) {
      return false;
    }
    auto *environment = batch.add_environment_data();
    if (temperature) {
      environment->set_temperature_e3(scaled(*temperature, 1000));
    }
    if (pressure) {
      environment->set_pressure_e3(scaled(*pressure, 1000));
    }
    return true;
  }

  // heading in degrees (0 = north, clockwise), pitch and roll in degrees
  inline bool fillCompass(const QVariantMap &data, SensorBatch &batch) {
    const auto heading = number(data, "heading");
    if (!heading) {
      return false;
    }
    auto *compass = batch.add_compass_data();
    compass->set_bearing_e6(scaled(std::fmod(std::fmod(*heading, 360.0) + 360.0, 360.0), 1e6));
    if (const auto pitch = number(data, "pitch")) {
      compass->set_pitch_e6(scaled(*pitch, 1e6));
    }
    if (const auto roll = number(data, "roll")) {
      compass->set_roll_e6(scaled(*roll, 1e6));
    }
    return true;
  }

  struct VehicleSensor {
    SensorType type;
    const char *topic;
    // continuous values (speed, rpm, heading) are repeated at the requested rate, the others go on every change
    bool continuous;
    bool (*fill)(const QVariantMap &, SensorBatch &);
  };

  inline const std::vector<VehicleSensor> &vehicleSensors() {
    static const std::vector<VehicleSensor> sensors{
        {SensorType::SENSOR_SPEED, "vehicle.speed", true, fillSpeed},
        {SensorType::SENSOR_PARKING_BRAKE, "vehicle.parkingBrake", false, fillParkingBrake},
        {SensorType::SENSOR_GEAR, "vehicle.gear", false, fillGear},
        {SensorType::SENSOR_FUEL, "vehicle.fuel", false, fillFuel},
        {SensorType::SENSOR_ODOMETER, "vehicle.odometer", false, fillOdometer},
        {SensorType::SENSOR_RPM, "vehicle.engine", true, fillRpm},
        {SensorType::SENSOR_ENVIRONMENT_DATA, "vehicle.climate", false, fillEnvironment},
        {SensorType::SENSOR_COMPASS, "vehicle.compass", true, fillCompass},
    };
    return sensors;
  }

  // The sensor's value from its topic, nullopt without a usable one
  inline std::optional<SensorBatch> vehicleSensorData(const VehicleSensor &sensor, const TopicLookup &last) {
    const QVariantMap data = vehicleTopic(sensor.topic, last);
    SensorBatch batch;
    if (!topicAvailable(data) || !sensor.fill(data, batch)) {
      return std::nullopt;
    }
    return batch;
  }

  // "vehicle.drivingStatus" from a plugin: `restricted` (bool), or `restrictions` (list of "video", "keyboard",
  // "voice", "config", "messages"), or `status` (the bits)
  inline std::optional<int> drivingStatusFromVehicle(const QVariantMap &data) {
    using aap_protobuf::service::sensorsource::message::DrivingStatus;
    if (!topicAvailable(data)) {
      return std::nullopt;
    }
    if (const auto status = number(data, "status")) {
      return static_cast<int>(*status) & kFullyRestricted;
    }
    if (data.contains("restrictions")) {
      int bits = 0;
      for (const QString &name: data.value("restrictions").toStringList()) {
        bits |= name == "video" ? DrivingStatus::DRIVE_STATUS_NO_VIDEO
              : name == "keyboard" ? DrivingStatus::DRIVE_STATUS_NO_KEYBOARD_INPUT
              : name == "voice" ? DrivingStatus::DRIVE_STATUS_NO_VOICE_INPUT
              : name == "config" ? DrivingStatus::DRIVE_STATUS_NO_CONFIG
              : name == "messages" ? DrivingStatus::DRIVE_STATUS_LIMIT_MESSAGE_LEN : 0;
      }
      return bits;
    }
    if (const auto restricted = flag(data, "restricted")) {
      return *restricted ? kFullyRestricted : 0;
    }
    return std::nullopt;
  }

  // The driving status for Android Auto (safety, #115):
  // 1. "vehicle.drivingStatus" from a plugin (or the simulated state) wins.
  // 2. Otherwise unrestricted only while parked: parking brake on or gear P, and slower than 2 km/h (or no speed
  //    known). Everything else with vehicle data, also standing at a traffic light, gets every restriction.
  // 3. Without parking brake, gear and speed: unrestricted (head units without vehicle data, as before).
  // A gear known only as `reverse` (CAN profiles that see the reverse light) says nothing about parking: not counted.
  inline int drivingStatus(const TopicLookup &last) {
    if (const auto status = drivingStatusFromVehicle(vehicleTopic("vehicle.drivingStatus", last))) {
      return *status;
    }
    const QVariantMap brakeData = vehicleTopic("vehicle.parkingBrake", last);
    const QVariantMap gearData = vehicleTopic("vehicle.gear", last);
    const QVariantMap speedData = vehicleTopic("vehicle.speed", last);
    const auto brake = topicAvailable(brakeData) ? flag(brakeData, "engaged") : std::nullopt;
    const auto gear = topicAvailable(gearData) && gearData.contains("gear") ? gearFromVehicle(gearData) : std::nullopt;
    const auto kmh = topicAvailable(speedData) ? number(speedData, "kmh") : std::nullopt;
    if (!brake && !gear && !kmh) {
      return aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED;
    }
    const bool parked = (brake.value_or(false)
                         || gear == aap_protobuf::service::sensorsource::message::Gear::GEAR_PARK)
                        && (!kmh || std::abs(*kmh) < kParkedKmh);
    return parked ? aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED
                  : kFullyRestricted;
  }

  // "vehicle.info" `fuelTypes`: "petrol", "diesel" or the protocol's names ("lpg", "cng", "electric", "hydrogen",
  // "e85", "biodiesel", ...); unknown names are left out
  inline std::vector<aap_protobuf::service::sensorsource::message::FuelType> fuelTypes(const QVariantMap &info) {
    using aap_protobuf::service::sensorsource::message::FuelType;
    std::vector<FuelType> types;
    for (const QString &name: info.value("fuelTypes").toStringList()) {
      const QString key = name.trimmed().toLower();
      FuelType type = FuelType::FUEL_TYPE_UNKNOWN;
      if (key == "petrol" || key == "gasoline") {
        type = FuelType::FUEL_TYPE_UNLEADED;
      } else if (key == "diesel") {
        type = FuelType::FUEL_TYPE_DIESEL_1;
      } else if (!aap_protobuf::service::sensorsource::message::FuelType_Parse(
                     ("FUEL_TYPE_" + key.toUpper()).toStdString(), &type)) {
        continue;
      }
      if (type != FuelType::FUEL_TYPE_UNKNOWN && std::find(types.begin(), types.end(), type) == types.end()) {
        types.push_back(type);
      }
    }
    return types;
  }

  // "vehicle.info" `evConnectors`: "type1", "type2", "ccs1", "ccs2", "chademo", "gbt", "tesla" or the protocol's
  // names ("j1772", "mennekes", "combo_2", ...); unknown names are left out
  inline std::vector<aap_protobuf::service::sensorsource::message::EvConnectorType>
  evConnectorTypes(const QVariantMap &info) {
    using aap_protobuf::service::sensorsource::message::EvConnectorType;
    std::vector<EvConnectorType> types;
    for (const QString &name: info.value("evConnectors").toStringList()) {
      QString key = name.trimmed().toLower();
      key = key == "type1" ? "j1772" : key == "type2" ? "mennekes" : key == "ccs1" ? "combo_1"
            : key == "ccs2" ? "combo_2" : key == "tesla" ? "tesla_supercharger" : key;
      EvConnectorType type = EvConnectorType::EV_CONNECTOR_TYPE_UNKNOWN;
      if (!aap_protobuf::service::sensorsource::message::EvConnectorType_Parse(
              ("EV_CONNECTOR_TYPE_" + key.toUpper()).toStdString(), &type)) {
        continue;
      }
      if (type != EvConnectorType::EV_CONNECTOR_TYPE_UNKNOWN
          && std::find(types.begin(), types.end(), type) == types.end()) {
        types.push_back(type);
      }
    }
    return types;
  }

}
