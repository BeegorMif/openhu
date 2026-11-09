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

#include <f1x/openauto/autoapp/Core/Companion.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

bool isCompanionApp(const QString& app)
{
    const QVariantMap state = EventBus::instance().last(QStringLiteral("companion.state"));
    return state.value("connected").toBool() && !app.isEmpty()
           && (app == state.value("label").toString() || state.value("mediaLabels").toStringList().contains(app));
}

QVariantMap companionOf(const QVariantMap& manifest)
{
    const QVariantMap companion = manifest.value("companion").toMap();
    const QString package = companion.value("package").toString();
    if(package.isEmpty())
    {
        return {};
    }
    return {{"package", package},
            {"name", companion.value("name", manifest.value("name", manifest.value("id"))).toString()},
            {"topics", companion.value("topics").toStringList()},
            {"commands", companion.value("commands").toStringList()}};
}

QVariantMap companionExtensions(const QVariantList& plugins)
{
    QVariantList extensions;
    for(const auto& entry: plugins)
    {
        const QVariantMap plugin = entry.toMap();
        QVariantMap companion = plugin.value("companion").toMap();
        if(companion.isEmpty() || !plugin.value("enabled").toBool())
        {
            continue;
        }
        companion.insert("plugin", plugin.value("id"));
        extensions.append(companion);
    }
    return {{"extensions", extensions}};
}

bool phoneMusicPlaying()
{
    return EventBus::instance().last(QStringLiteral("androidauto.mediaAudio")).value("playing").toBool();
}

}
}
}
}
