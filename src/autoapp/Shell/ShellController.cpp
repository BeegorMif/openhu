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

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <f1x/openauto/autoapp/Shell/ShellController.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

QString ShellController::readOsVersion(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }

    QString id;
    QString version;
    while(!file.atEnd())
    {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const qsizetype separator = line.indexOf('=');
        if(separator <= 0)
        {
            continue;
        }
        QString value = line.mid(separator + 1);
        if(value.size() >= 2 && (value.startsWith('"') || value.startsWith('\'')) && value.endsWith(value.front()))
        {
            value = value.mid(1, value.size() - 2);
        }
        const QString key = line.left(separator);
        if(key == "IMAGE_ID")
        {
            id = value;
        }
        else if(key == "IMAGE_VERSION")
        {
            version = value;
        }
    }
    return id == "openhu-os" ? version : QString();
}

void ShellController::reboot()
{
    this->callLogind("Reboot");
}

void ShellController::powerOff()
{
    this->callLogind("PowerOff");
}

void ShellController::callLogind(const char* method)
{
    OPENAUTO_LOG(info) << "[ShellController] " << method << " through logind";
    QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1",
                                                       "org.freedesktop.login1.Manager", method);
    call << false;  // not interactive: allowed by polkit without asking
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 5000);
    if(reply.type() == QDBusMessage::ErrorMessage)
    {
        OPENAUTO_LOG(error) << "[ShellController] " << method << " failed: " << reply.errorMessage().toStdString();
        this->toast(QString("%1 nicht möglich: %2").arg(QString(method) == "Reboot" ? "Neustart" : "Ausschalten",
                                                       reply.errorMessage()), 5000);
    }
}

}
}
}
}
