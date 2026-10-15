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

uint64_t toKHz(const QString& band, double frequency);

struct PluginFrequency
{
    QString band;       // "fm", "am" or empty if the frequency is in neither band
    double frequency;   // MHz (FM) or kHz (AM)
};
PluginFrequency fromKHz(uint64_t kHz);

// The radio service in the service announcement: v1 tuner list (field 1) and v2 band groups (field 2)
void fillService(aap_protobuf::service::radio::RadioService& service, const QVariantMap& state);

aap_protobuf::service::radio::message::RadioProgramListNotification programList(const QVariantMap& state);
aap_protobuf::service::radio::message::RadioProgramInfoNotification programInfo(const QVariantMap& state);
aap_protobuf::service::radio::message::RadioFavoriteListNotification favoriteList(const QVariantMap& state);

// radio.command events ({cmd, value}) for a tune request; empty if the plugin cannot tune it (RDS PI, DAB, ...)
QList<QVariantMap> tuneCommands(const aap_protobuf::service::radio::message::RadioProgramSelector& selector,
                                const QVariantMap& state);

}
}
}
}
}
