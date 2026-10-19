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

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/usbdevice_fs.h>
#include <QDir>
#include <QFile>
#include <f1x/openauto/autoapp/Core/UsbReset.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

namespace
{

struct UsbDevice
{
    QString vendor;
    QString product;
    int bus;
    int number;
};

QString readAttribute(const QString& device, const QString& name)
{
    QFile file(device + "/" + name);
    return file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll()).trimmed() : QString();
}

QList<UsbDevice> usbDevices()
{
    QList<UsbDevice> devices;
    const QDir sysfs("/sys/bus/usb/devices");
    for(const auto& entry: sysfs.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        const QString path = sysfs.filePath(entry);
        const QString vendor = readAttribute(path, "idVendor");
        if(!vendor.isEmpty())
        {
            devices.append({vendor, readAttribute(path, "idProduct"), readAttribute(path, "busnum").toInt(),
                            readAttribute(path, "devnum").toInt()});
        }
    }
    return devices;
}

bool isAccessory(const UsbDevice& device)
{
    // Google vendor id with the Android Open Accessory product ids 0x2d00-0x2d05
    const int product = device.product.toInt(nullptr, 16);
    return device.vendor == "18d1" && product >= 0x2d00 && product <= 0x2d05;
}

bool isAndroidVendor(const UsbDevice& device)
{
    // Phone makers only; chipset vendors (MediaTek, Qualcomm) also make WiFi/BT adapters.
    static const QStringList vendors{"18d1", "04e8", "22b8", "2717", "12d1", "0bb4", "1004",
                                     "2a70", "19d2", "0fce", "2ae5"};
    return vendors.contains(device.vendor);
}

bool reset(const UsbDevice& device)
{
    const QString path = QString("/dev/bus/usb/%1/%2").arg(device.bus, 3, 10, QChar('0')).arg(device.number, 3, 10, QChar('0'));
    const int fd = ::open(path.toLocal8Bit().constData(), O_WRONLY);
    if(fd < 0)
    {
        OPENAUTO_LOG(error) << "[UsbReset] cannot open " << path.toStdString() << ": " << std::strerror(errno);
        return false;
    }

    // An accessory leaves the bus during the reset, ENODEV is the expected result then.
    const bool ok = ::ioctl(fd, USBDEVFS_RESET, 0) == 0 || errno == ENODEV;
    if(!ok)
    {
        OPENAUTO_LOG(error) << "[UsbReset] reset of " << path.toStdString() << " failed: " << std::strerror(errno);
    }
    ::close(fd);
    return ok;
}

}

QStringList androidUsbDevices()
{
    QStringList result;
    for(const auto& device: usbDevices())
    {
        if(isAccessory(device) || isAndroidVendor(device))
        {
            result.append(QString("%1:%2@%3-%4").arg(device.vendor, device.product).arg(device.bus).arg(device.number));
        }
    }
    result.sort();
    return result;
}

QStringList resetAndroidUsbDevices(bool accessoryOnly)
{
    const auto devices = usbDevices();

    QList<UsbDevice> targets;
    for(const auto& device: devices)
    {
        if(isAccessory(device))
        {
            targets.append(device);
        }
    }
    if(targets.isEmpty() && !accessoryOnly)
    {
        for(const auto& device: devices)
        {
            if(isAndroidVendor(device))
            {
                targets.append(device);
            }
        }
    }

    QStringList result;
    for(const auto& device: targets)
    {
        OPENAUTO_LOG(info) << "[UsbReset] resetting " << device.vendor.toStdString() << ":" << device.product.toStdString();
        if(reset(device))
        {
            result.append(device.vendor + ":" + device.product);
        }
    }
    return result;
}

}
}
}
}
