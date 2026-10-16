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

#include <cstdint>
#include <string>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <aap_protobuf/service/radio/RadioService.pb.h>
#include <aap_protobuf/service/radio/message/RadioProgramListNotification.pb.h>
#include <aap_protobuf/service/radio/message/RadioProgramInfoNotification.pb.h>
#include <aap_protobuf/service/radio/message/RadioFavoriteListNotification.pb.h>
#include <aap_protobuf/service/radio/message/RadioProgramSelector.pb.h>

// Radio plugin (radio.state / radio.command, openhu-plugins) <-> Android Auto radio channel v2 (openhu#75).
// Pure functions, no I/O: the RadioService sends and receives, these translate.
// Android Auto addresses AM/FM stations by frequency in kHz; the plugin uses MHz for FM and kHz for AM.

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{
namespace radio
{

// action id of the FM scan in the band group, sent back by the phone as RadioSearchRequest
extern const char* const cScanFmAction;

// Station logos of the plugin (radio.logos: {fm: {"96.2": data URL}, am: {...}}) as images for Android Auto, key
// "fm:96.2" / "am:1008". Scaled once when radio.logos changes, the station list is sent much more often.
using StationIcons = QHash<QString, std::string>;
constexpr int cStationIconSize = 128;
StationIcons stationIcons(const QVariantMap& logos);
// PNG/JPEG data URL -> image bytes that fit into size x size (PNG if scaled, else the original); empty if invalid
std::string scaledIcon(const QString& dataUrl, int size = cStationIconSize);

uint64_t toKHz(const QString& band, double frequency);

// the frequency is one of the plugin's favorites (presets) of that band
bool isFavorite(const QVariantMap& state, const QString& band, double frequency);

struct PluginFrequency
{
    QString band;       // "fm", "am" or empty if the frequency is in neither band
    double frequency;   // MHz (FM) or kHz (AM)
};
PluginFrequency fromKHz(uint64_t kHz);

// The radio service in the service announcement: v1 tuner list (field 1) and v2 band groups (field 2)
void fillService(aap_protobuf::service::radio::RadioService& service, const QVariantMap& state);

// with station_icon for the stations that have a logo
aap_protobuf::service::radio::message::RadioProgramListNotification programList(const QVariantMap& state,
                                                                                const StationIcons& icons = {});
aap_protobuf::service::radio::message::RadioProgramInfoNotification programInfo(const QVariantMap& state,
                                                                                const StationIcons& icons = {});
aap_protobuf::service::radio::message::RadioFavoriteListNotification favoriteList(const QVariantMap& state,
                                                                                  const StationIcons& icons = {});

// radio.command events ({cmd, value}) for a tune request; empty if the plugin cannot tune it (RDS PI, DAB, ...)
QList<QVariantMap> tuneCommands(const aap_protobuf::service::radio::message::RadioProgramSelector& selector,
                                const QVariantMap& state);

}
}
}
}
}
