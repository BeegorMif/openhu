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

#include <f1x/openauto/autoapp/Projection/IBluetoothDevice.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

// Bluetooth adapter as seen by the Android Auto Bluetooth channel: its address is sent to the phone,
// which then pairs with it (pairing over the USB cable). Reads BlueZ over D-Bus.
class BluezBluetoothDevice: public IBluetoothDevice
{
public:
    // empty address: first adapter
    explicit BluezBluetoothDevice(std::string adapterAddress);

    void stop() override;
    bool isPaired(const std::string& address) const override;
    std::string getAdapterAddress() const override;
    bool isAvailable() const override;

private:
    std::string adapterAddress_;
};

}
}
}
}
