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


#include <f1x/openauto/autoapp/Projection/BluezBluetoothDevice.hpp>
#include <f1x/openauto/autoapp/Wireless/Bluez.hpp>

namespace f1x::openauto::autoapp::projection
{

namespace bluez = wireless::bluez;

BluezBluetoothDevice::BluezBluetoothDevice(std::string adapterAddress)
{
    const QString wanted = QString::fromStdString(adapterAddress).toUpper();
    const BluezObjects objects = bluez::managedObjects();
    for(auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        const QString address = it.value().value(bluez::cAdapter).value("Address").toString().toUpper();
        if(!address.isEmpty() && (adapterAddress_.empty() || address == wanted))
        {
            adapterAddress_ = address.toStdString();
        }
    }
}

void BluezBluetoothDevice::stop()
{
}

bool BluezBluetoothDevice::isPaired(const std::string& address) const
{
    const QString wanted = QString::fromStdString(address).toUpper();
    const BluezObjects objects = bluez::managedObjects();
    for(auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        const QVariantMap device = it.value().value(bluez::cDevice);
        if(device.value("Address").toString().toUpper() == wanted)
        {
            return device.value("Paired").toBool();
        }
    }
    return false;
}

std::string BluezBluetoothDevice::getAdapterAddress() const
{
    return adapterAddress_;
}

bool BluezBluetoothDevice::isAvailable() const
{
    return !adapterAddress_.empty();
}

}
