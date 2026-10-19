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

#include <optional>
#include <QVariantMap>
#include <aap_protobuf/service/sensorsource/message/Gear.pb.h>

namespace f1x::openauto::autoapp::service::sensor {

  // "vehicle.gear" (published by a plugin, e.g. the CAN plugin) -> SENSOR_GEAR for Android Auto (#76).
  // `gear`: "park", "reverse", "neutral", "drive" or a number 0 (neutral) to 10; without it `reverse` (bool):
  // true = REVERSE, false = DRIVE (the CAN plugin's example profile only knows the reverse gear).
  // Nothing (no gear known): Android Auto gets no gear.
  inline std::optional<aap_protobuf::service::sensorsource::message::Gear> gearFromVehicle(const QVariantMap &data) {
    using aap_protobuf::service::sensorsource::message::Gear;
    const QVariant gear = data.value("gear");
    if (gear.isValid() && !gear.isNull()) {
      if (gear.typeId() == QMetaType::QString) {
        const QString name = gear.toString().trimmed().toLower();
        if (name == "park" || name == "p") {
          return Gear::GEAR_PARK;
        }
        if (name == "reverse" || name == "r") {
          return Gear::GEAR_REVERSE;
        }
        if (name == "neutral" || name == "n") {
          return Gear::GEAR_NEUTRAL;
        }
        if (name == "drive" || name == "d") {
          return Gear::GEAR_DRIVE;
        }
      }
      bool ok = false;
      const int number = gear.toInt(&ok);
      // a closed enum: values that do not exist are not sent (oaa docs/channels/sensor.md)
      if (ok && number >= 0 && number <= 10) {
        return static_cast<Gear>(number);
      }
      return std::nullopt;
    }
    if (data.contains("reverse")) {
      return data.value("reverse").toBool() ? Gear::GEAR_REVERSE : Gear::GEAR_DRIVE;
    }
    return std::nullopt;
  }

}
