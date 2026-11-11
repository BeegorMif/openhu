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
#include <QVariantList>
#include <QVariantMap>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

// The companion app (openhu-companion, openhu#158) and its extension apps: the local player is a media app in Android
// Auto that shows the head unit's own music and plays no sound on the phone. The companion announces itself over
// Bluetooth (CompanionBridge, companion.state) with the names Android Auto shows for its media extensions.

// Whether Android Auto's media app (its label, media.status "app") is the companion or one of its media extensions
// (companion.state "label" / "mediaLabels"). Thread safe.
bool isCompanionApp(const QString& app);

// The manifest field "companion" {package, name, topics, commands}: the plugin's extension app on the phone, null
// map when the field is missing or has no package.
QVariantMap companionOf(const QVariantMap& manifest);

// "companion.extensions" {extensions: [{plugin, package, name, topics, commands}]} from plugins.state "plugins":
// the enabled plugins that want an app. The companion gives an app only its plugin's topics and commands.
QVariantMap companionExtensions(const QVariantList& plugins);

// Whether the phone streams music now (androidauto.mediaAudio). The companion never does, so sound from the phone
// is another app, also while Android Auto still names the companion as its media app. Thread safe.
bool phoneMusicPlaying();

}
}
}
}
