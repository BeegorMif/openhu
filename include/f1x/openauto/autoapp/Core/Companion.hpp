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

// The companion app (openhu-companion, openhu#158): a media app in Android Auto that shows the head unit's own music
// and plays no sound on the phone. It announces itself over Bluetooth (CompanionBridge, companion.state).

// Whether Android Auto's media app (its label, media.status "app") is the connected companion app. Thread safe.
bool isCompanionApp(const QString& app);

// Whether the phone streams music now (androidauto.mediaAudio). The companion never does, so sound from the phone
// is another app, also while Android Auto still names the companion as its media app. Thread safe.
bool phoneMusicPlaying();

}
}
}
}
