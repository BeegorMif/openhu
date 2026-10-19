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

#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Bluetooth/BluetoothManager.hpp>
#include <f1x/openauto/autoapp/Bluetooth/CarAudio.hpp>
#include <f1x/openauto/autoapp/Wireless/WifiClient.hpp>
#include <f1x/openauto/autoapp/Wireless/WirelessController.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace wireless
{

// wireless.*, wifi.*, bluetooth.*, caraudio.* (see docs/api.md)
void registerWirelessMethods(api::ApiServer& server, WirelessController& wireless, WifiClient& wifi,
                             bt::BluetoothManager& bluetooth, bt::CarAudio& carAudio);

}
}
}
}
