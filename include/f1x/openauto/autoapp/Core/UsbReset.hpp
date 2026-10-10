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

#include <QStringList>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

// Resets connected phones on USB, as if they were unplugged and plugged in again: the phone
// re-enumerates and Android Auto connects again. Devices in Android accessory mode are reset;
// without one, phones of common Android vendors (not with accessoryOnly). Returns "vendor:product" of each
// reset device.
QStringList resetAndroidUsbDevices(bool accessoryOnly = false);

}
}
}
}
