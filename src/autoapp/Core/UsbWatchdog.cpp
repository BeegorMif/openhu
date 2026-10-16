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

#include <f1x/openauto/autoapp/Core/UsbWatchdog.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

UsbWatchdog::UsbWatchdog(Clock::duration timeout, int maxAttempts)
    : timeout_(timeout)
    , maxAttempts_(maxAttempts)
{
}

UsbWatchdog::Action UsbWatchdog::update(const QStringList& devices, bool session, Clock::time_point now)
{
    if(session || devices.isEmpty())
    {
        // a session started or the phones are gone: start over
        devices_ = devices;
        since_.reset();
        attempts_ = 0;
        gaveUp_ = false;
        return Action::None;
    }

    if(devices != devices_ || !since_)
    {
        // plugged in or switched to accessory mode (new device id): the hub reacts to that by itself
        devices_ = devices;
        since_ = now;
        return Action::None;
    }

    if(now - *since_ < timeout_)
    {
        return Action::None;
    }

    if(attempts_ >= maxAttempts_)
    {
        if(gaveUp_)
        {
            return Action::None;
        }
        gaveUp_ = true;
        return Action::GiveUp;
    }

    ++attempts_;
    since_ = now;
    return Action::Rearm;
}

UsbWatchdog::Clock::duration UsbWatchdog::waiting(Clock::time_point now) const
{
    return since_ ? now - *since_ : Clock::duration::zero();
}

}
}
}
}
