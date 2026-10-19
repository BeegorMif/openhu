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
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

// The "androidAuto" section of a plugin manifest (#123, docs/plugins.md): what the plugin provides to Android Auto.
// Only these plugins feed Android Auto; OpenHU announces what an enabled one declares at the connect.
//   "androidAuto": {"sensors": ["speed", "gear"], "services": ["radio"]}

// sensor names = the vehicle.* topics without the prefix ("speed" = vehicle.speed, "info" = vehicle.info)
inline const QStringList& androidAutoSensorNames()
{
    static const QStringList names{"speed", "parkingBrake", "gear", "fuel", "odometer", "engine", "climate",
                                   "compass", "drivingStatus", "info"};
    return names;
}

inline const QStringList& androidAutoServiceNames()
{
    static const QStringList names{"radio", "carControl"};
    return names;
}

// The section with unknown names left out; what was left out goes to *ignored
inline QVariantMap parseAndroidAuto(const QVariantMap& manifest, QStringList* ignored = nullptr)
{
    if(!manifest.contains("androidAuto"))
    {
        return QVariantMap();
    }
    const QVariantMap section = manifest.value("androidAuto").toMap();
    const auto known = [ignored](const QVariant& value, const QStringList& names, const QString& kind) {
        QStringList result;
        for(const auto& name: value.toStringList())
        {
            if(names.contains(name))
            {
                result.append(name);
            }
            else if(ignored != nullptr)
            {
                ignored->append(kind + " " + name);
            }
        }
        return result;
    };
    return {{"sensors", known(section.value("sensors"), androidAutoSensorNames(), "sensor")},
            {"services", known(section.value("services"), androidAutoServiceNames(), "service")}};
}

// --- from "plugins.state" (the plugin list with "enabled", "compatible" and the parsed "androidAuto") ---

// the androidAuto sections of the enabled plugins by id
inline QVariantMap androidAutoProviders(const QVariantMap& pluginsState)
{
    QVariantMap providers;
    for(const auto& entry: pluginsState.value("plugins").toList())
    {
        const QVariantMap plugin = entry.toMap();
        if(plugin.value("enabled").toBool() && plugin.value("compatible").toBool() && plugin.contains("androidAuto"))
        {
            providers.insert(plugin.value("id").toString(), plugin.value("androidAuto"));
        }
    }
    return providers;
}

// an enabled plugin declares this sensor or service ("sensors"/"services")
inline bool androidAutoDeclared(const QVariantMap& pluginsState, const QString& kind, const QString& name)
{
    const QVariantMap providers = androidAutoProviders(pluginsState);
    for(auto it = providers.cbegin(); it != providers.cend(); ++it)
    {
        if(it.value().toMap().value(kind).toStringList().contains(name))
        {
            return true;
        }
    }
    return false;
}

// May the data of an Android Auto service (e.g. vehicle.controls for "carControl") come from this source? A plugin
// only if it is enabled and declares the service; API clients that are not plugins and OpenHU itself: yes.
inline bool androidAutoServiceSourceAllowed(const QVariantMap& pluginsState, const QString& service, const QString& source)
{
    if(!source.startsWith(QLatin1String("plugin:")))
    {
        return true;
    }
    return androidAutoProviders(pluginsState).value(source.mid(7)).toMap().value("services").toStringList().contains(service);
}

// May a value of this vehicle.* topic from this source (EventBus::source) go to Android Auto?
// - a plugin: only if it is enabled and declares the sensor
// - an API client that is not a plugin, a QML page or OpenHU itself (empty): yes (decided in #123)
// - vehicle.simulate (test topic, #119): from anyone
inline bool androidAutoSourceAllowed(const QVariantMap& pluginsState, const QString& topic, const QString& source)
{
    if(topic == QLatin1String("vehicle.simulate") || !source.startsWith(QLatin1String("plugin:")))
    {
        return true;
    }
    const QString name = topic.startsWith(QLatin1String("vehicle.")) ? topic.mid(8) : topic;
    return androidAutoProviders(pluginsState).value(source.mid(7)).toMap().value("sensors").toStringList().contains(name);
}

}
}
}
}
