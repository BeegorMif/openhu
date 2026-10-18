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

#include <optional>
#include <QVariantMap>
#include <aap_protobuf/service/sensorsource/message/DrivingStatus.pb.h>
#include <aap_protobuf/service/sensorsource/message/Gear.pb.h>

namespace f1x::openauto::autoapp::service::sensor {

  // A driving state for tests, set through the API ("vehicle.simulate", not saved): what a car would send in it. It
  // replaces "vehicle.gear" and the always unrestricted driving status while it is set.
  struct SimulatedDriving {
    aap_protobuf::service::sensorsource::message::Gear gear;
    bool parkingBrake;
    // m/s × 1000 (SpeedData.speed_e3)
    int speedE3;
    // DrivingStatus bits
    int drivingStatus;
  };

  // "vehicle.simulate" `state`: "park", "drive", "reverse"; anything else (or "off"): no simulation
  inline std::optional<SimulatedDriving> simulatedDriving(const QVariantMap &data) {
    using aap_protobuf::service::sensorsource::message::Gear;
    using aap_protobuf::service::sensorsource::message::DrivingStatus;
    // what a car restricts while it moves
    constexpr int restricted = DrivingStatus::DRIVE_STATUS_NO_VIDEO | DrivingStatus::DRIVE_STATUS_NO_KEYBOARD_INPUT
                               | DrivingStatus::DRIVE_STATUS_NO_VOICE_INPUT | DrivingStatus::DRIVE_STATUS_NO_CONFIG
                               | DrivingStatus::DRIVE_STATUS_LIMIT_MESSAGE_LEN;
    const QString state = data.value("state").toString();
    if (state == "park") {
      return SimulatedDriving{Gear::GEAR_PARK, true, 0, DrivingStatus::DRIVE_STATUS_UNRESTRICTED};
    }
    if (state == "drive") {
      // 50 km/h
      return SimulatedDriving{Gear::GEAR_DRIVE, false, 13889, restricted};
    }
    if (state == "reverse") {
      // 5 km/h
      return SimulatedDriving{Gear::GEAR_REVERSE, false, 1389, restricted};
    }
    return std::nullopt;
  }

}
