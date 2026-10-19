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

#include <QString>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

// Only one OpenHU per system: a second one would fight over the phone on USB, the API port and the display.
// flock on <dir>/openhu.lock (/run/lock, else the temp directory), for every user. The file holds the pid of the
// holder; each new instance overwrites it (the file is writable for every user).
// Whether the old one still runs decides the kernel: it drops the lock when the process ends, also after a crash or
// kill -9, so a new instance takes over right away. A pid alone could belong to another process by then. The
// descriptor is close-on-exec: the restart of OpenHU (execv) takes the lock again, plugins do not inherit it.
class InstanceLock
{
public:
    explicit InstanceLock(const QString& path = defaultPath());
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    // false when another process holds the lock (or the file cannot be opened: error())
    bool acquire();
    // pid of the process holding the lock as written in the file, 0 when unknown or that process does not run
    qint64 holder() const;
    QString error() const { return error_; }
    QString path() const { return path_; }

    static QString defaultPath();

private:
    QString path_;
    QString error_;
    int fd_ = -1;
};

}
}
}
}
