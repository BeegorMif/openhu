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

#include <QProcess>
#include <QStringList>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

// Descriptors OpenHU inherited from whoever started it, e.g. the pipe cage watches: cage ends (and systemd restarts
// the session) only when every holder of its write end closed it. OpenHU keeps them, its children must not get
// them, or a crashed OpenHU stays a zombie under a running cage while pactl, nmcli or a plugin hold the pipe.
// At the start of main(): every inherited descriptor above stderr becomes close-on-exec.
void protectInheritedDescriptors();
bool isInheritedDescriptor(int fd);
// OpenHU's own restart (execv): the inherited descriptors stay open for the new OpenHU (it protects them again)
void keepInheritedDescriptorsOnExec();

// Child processes end with OpenHU instead of running on (a plugin twice after a crash): SIGTERM when OpenHU dies
// (PR_SET_PDEATHSIG). The kernel sends it when the thread that started the child ends, so only children started
// from the main thread get it; the others start without (a worker thread ending would kill them early).
// OwnGroup: the child leads its own process group, so stopping it can reach everything it started
// (terminateGroup/killGroup); without it, SIGTERM on OpenHU's death reaches only the child itself.
enum class ChildGroup
{
    Shared,
    OwnGroup
};
void startChild(QProcess& process, const QString& program, const QStringList& arguments,
                ChildGroup group = ChildGroup::Shared);
// SIGTERM to the process group of a running child started with OwnGroup (to the child alone otherwise); returns
// the group for killGroup (0: not running or no own group)
qint64 terminateGroup(QProcess& process);
// SIGKILL to what is left of that group, also when the child itself already ended
void killGroup(qint64 group);

}
}
}
}
