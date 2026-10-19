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
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <f1x/openauto/autoapp/Core/InstanceLock.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

InstanceLock::InstanceLock(const QString& path)
    : path_(path)
{
}

InstanceLock::~InstanceLock()
{
    if(fd_ >= 0)
    {
        ::close(fd_);
    }
}

QString InstanceLock::defaultPath()
{
    const QString dir = QFileInfo("/run/lock").isDir() ? QStringLiteral("/run/lock") : QDir::tempPath();
    return dir + "/openhu.lock";
}

bool InstanceLock::acquire()
{
    if(fd_ >= 0)
    {
        return true;
    }
    const QByteArray file = QFile::encodeName(path_);
    // Writable for every user (fchmod: the umask would take it away), so every new instance writes its pid. O_CREAT
    // only for a missing file: in /run/lock (sticky) fs.protected_regular refuses it for the file of another user.
    // A file that is only readable can still be locked (flock needs no write access).
    int fd = ::open(file.constData(), O_RDWR | O_CLOEXEC);
    if(fd < 0 && errno == ENOENT)
    {
        fd = ::open(file.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        if(fd >= 0)
        {
            ::fchmod(fd, 0666);
        }
    }
    const bool writable = fd >= 0;
    if(fd < 0 && errno == EACCES)
    {
        fd = ::open(file.constData(), O_RDONLY | O_CLOEXEC);
    }
    if(fd < 0)
    {
        error_ = QString("%1: %2").arg(path_, QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    if(::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        error_ = errno == EWOULDBLOCK ? QString() : QString("%1: %2").arg(path_, QString::fromLocal8Bit(std::strerror(errno)));
        ::close(fd);
        return false;
    }
    fd_ = fd;
    if(writable && ::ftruncate(fd_, 0) == 0)
    {
        // only information for the next one (holder()): the lock holds without it
        const QByteArray pid = QByteArray::number(qint64(::getpid())) + "\n";
        [[maybe_unused]] const auto written = ::pwrite(fd_, pid.constData(), pid.size(), 0);
    }
    return true;
}

qint64 InstanceLock::holder() const
{
    QFile file(path_);
    const qint64 pid = file.open(QIODevice::ReadOnly) ? file.readLine().trimmed().toLongLong() : 0;
    // EPERM: runs, as another user
    return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno == EPERM) ? pid : 0;
}

}
}
}
}
