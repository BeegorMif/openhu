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

#pragma once

#include <string>
#include <QByteArray>
#include <QString>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

// Image bytes from the phone (album art, turn icons) as a data URL usable directly in QML and browsers.
inline QString imageDataUrl(const std::string& bytes)
{
    if(bytes.empty())
    {
        return QString();
    }

    const QByteArray data = QByteArray::fromStdString(bytes);
    const char* mime = data.startsWith("\x89PNG") ? "image/png" : "image/jpeg";
    return QStringLiteral("data:%1;base64,%2").arg(QLatin1String(mime), QString::fromLatin1(data.toBase64()));
}

// Protobuf enum name ("TURN_NORMAL_LEFT") in API style ("turn_normal_left").
inline QString enumName(const std::string& name)
{
    return QString::fromStdString(name).toLower();
}

}
}
}
}
