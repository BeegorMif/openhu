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

#include <chrono>
#include <optional>
#include <QStringList>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

// Decides when the wait for a USB phone has to be armed again (openhu#80): a phone is on the cable (MTP or
// already in accessory mode), but no Android Auto session starts. Every change of the connected phones (plugged
// in, switched to accessory mode) restarts the time, so the normal path (USB hub) gets its chance first. After
// maxAttempts re-arms without a session it gives up until the phones are unplugged or a session started.
class UsbWatchdog
{
public:
    using Clock = std::chrono::steady_clock;
    enum class Action
    {
        None,
        Rearm,  // arm the USB wait again (new hotplug registration, reports the connected devices)
        GiveUp  // once: maxAttempts reached
    };

    explicit UsbWatchdog(Clock::duration timeout = std::chrono::seconds(15), int maxAttempts = 3);

    // devices: androidUsbDevices(); session: an Android Auto session runs (USB or wireless)
    Action update(const QStringList& devices, bool session, Clock::time_point now);

    int attempts() const { return attempts_; }
    int maxAttempts() const { return maxAttempts_; }
    // how long the current devices are waiting (since the last change or re-arm)
    Clock::duration waiting(Clock::time_point now) const;

private:
    Clock::duration timeout_;
    int maxAttempts_;
    QStringList devices_;
    std::optional<Clock::time_point> since_;
    int attempts_ = 0;
    bool gaveUp_ = false;
};

}
}
}
}
