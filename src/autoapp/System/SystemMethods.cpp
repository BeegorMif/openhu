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

#include <f1x/openauto/autoapp/System/SystemMethods.hpp>

namespace f1x::openauto::autoapp::system
{

namespace
{

void requireOs(const SystemController& system)
{
    if(!system.available())
    {
        throw api::ApiError("not running on OpenHU OS");
    }
}

QVariant require(const QVariantMap& params, const QString& name)
{
    if(!params.contains(name))
    {
        throw api::ApiError("missing parameter: " + name.toStdString());
    }
    return params.value(name);
}

}

void registerSystemMethods(api::ApiServer& server, SystemController& system)
{
    server.registerMethod("system.os", [&system](const QVariantMap&) -> QVariant {
        return QVariantMap{{"openhuOs", system.available()}, {"version", system.osVersion()},
                           {"boot", system.bootStatus()}};
    });

    server.registerMethod("system.update.state", [&system](const QVariantMap&) -> QVariant {
        requireOs(system);
        return system.update();
    });
    server.registerMethod("system.update.check", [&system](const QVariantMap&) -> QVariant {
        requireOs(system);
        system.checkForUpdate();
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("system.update.install", [&system](const QVariantMap&) -> QVariant {
        requireOs(system);
        const QString error = system.installUpdate();
        if(!error.isEmpty())
        {
            throw api::ApiError(error.toStdString());
        }
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("system.update.cancel", [&system](const QVariantMap&) -> QVariant {
        requireOs(system);
        system.cancelUpdate();
        return QVariantMap();
    });

    // the password only in the shell: the API has no login yet (#23)
    server.registerMethod("system.ssh.state", [&system](const QVariantMap&) -> QVariant {
        requireOs(system);
        return system.ssh();
    });
    server.registerMethod("system.ssh.enable", [&system](const QVariantMap& params) -> QVariant {
        requireOs(system);
        system.setSshEnabled(params.value("enabled", true).toBool());
        return QVariantMap{{"queued", true}};
    });

    server.registerMethod("system.ethernet.state", [&system](const QVariantMap&) -> QVariant {
        requireOs(system);
        return system.ethernet();
    });
    server.registerMethod("system.ethernet.setMode", [&system](const QVariantMap& params) -> QVariant {
        requireOs(system);
        const QString error = system.setEthernetMode(require(params, "mode").toString());
        if(!error.isEmpty())
        {
            throw api::ApiError(error.toStdString());
        }
        return QVariantMap{{"queued", true}};
    });

    server.registerMethod("system.time.state", [&system](const QVariantMap&) -> QVariant {
        requireOs(system);
        return system.time();
    });
    server.registerMethod("system.time.zones", [](const QVariantMap&) -> QVariant {
        return SystemController::timeZones();
    });
    server.registerMethod("system.time.setZone", [&system](const QVariantMap& params) -> QVariant {
        requireOs(system);
        const QString zone = require(params, "zone").toString();
        if(!SystemController::timeZones().contains(zone))
        {
            throw api::ApiError("unknown time zone: " + zone.toStdString());
        }
        system.setTimeZone(zone);
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("system.time.setNtp", [&system](const QVariantMap& params) -> QVariant {
        requireOs(system);
        system.setNtp(params.value("enabled", true).toBool());
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("system.time.set", [&system](const QVariantMap& params) -> QVariant {
        requireOs(system);
        const QString error = system.setTime(require(params, "time").toLongLong());
        if(!error.isEmpty())
        {
            throw api::ApiError(error.toStdString());
        }
        return QVariantMap{{"queued", true}};
    });
}

}
