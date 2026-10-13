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

#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <QCoreApplication>
#include <QThread>
#include <f1x/openauto/autoapp/Core/ChildProcesses.hpp>
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

std::vector<int>& inheritedDescriptors()
{
    static std::vector<int> descriptors;
    return descriptors;
}

bool onMainThread()
{
    const auto* application = QCoreApplication::instance();
    return application != nullptr && QThread::currentThread() == application->thread();
}

}

void protectInheritedDescriptors()
{
    auto& descriptors = inheritedDescriptors();
    descriptors.clear();
    DIR* dir = ::opendir("/proc/self/fd");
    if(dir == nullptr)
    {
        return;
    }
    const int own = ::dirfd(dir);
    while(const dirent* entry = ::readdir(dir))
    {
        char* end = nullptr;
        const long fd = std::strtol(entry->d_name, &end, 10);
        if(end == entry->d_name || *end != '\0' || fd <= STDERR_FILENO || fd == own)
        {
            continue;
        }
        const int flags = ::fcntl(int(fd), F_GETFD);
        if(flags >= 0 && !(flags & FD_CLOEXEC))
        {
            ::fcntl(int(fd), F_SETFD, flags | FD_CLOEXEC);
            descriptors.push_back(int(fd));
        }
    }
    ::closedir(dir);
}

bool isInheritedDescriptor(int fd)
{
    const auto& descriptors = inheritedDescriptors();
    return std::find(descriptors.begin(), descriptors.end(), fd) != descriptors.end();
}

void keepInheritedDescriptorsOnExec()
{
    for(const int fd: inheritedDescriptors())
    {
        const int flags = ::fcntl(fd, F_GETFD);
        if(flags >= 0)
        {
            ::fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
        }
    }
}

void startChild(QProcess& process, const QString& program, const QStringList& arguments, ChildGroup group)
{
    const bool bound = onMainThread();
    const pid_t parent = ::getpid();
    const bool ownGroup = group == ChildGroup::OwnGroup;
    // runs in the child between fork and exec: only async-signal-safe calls
    process.setChildProcessModifier([bound, parent, ownGroup]() {
        if(ownGroup)
        {
            ::setpgid(0, 0);
        }
        if(bound)
        {
            ::prctl(PR_SET_PDEATHSIG, SIGTERM);
            // OpenHU died before prctl: nobody would send the signal any more
            if(::getppid() != parent)
            {
                ::_exit(127);
            }
        }
    });
    if(!bound)
    {
        OPENAUTO_LOG(warning) << "[ChildProcesses] " << program.toStdString()
                              << " started outside the main thread: it does not end with OpenHU";
    }
    process.start(program, arguments);
}

qint64 terminateGroup(QProcess& process)
{
    const auto pid = pid_t(process.processId());
    if(pid <= 0 || process.state() == QProcess::NotRunning)
    {
        return 0;
    }
    // a child of its own group leads it (pgid = pid); the group lives while one of its processes does
    const pid_t group = ::getpgid(pid) == pid ? pid : 0;
    ::kill(group != 0 ? -group : pid, SIGTERM);
    return group;
}

void killGroup(qint64 group)
{
    if(group > 0)
    {
        ::kill(-pid_t(group), SIGKILL);
    }
}

}
}
}
}
