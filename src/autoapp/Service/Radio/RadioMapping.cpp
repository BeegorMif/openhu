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

#include <algorithm>
#include <cmath>
#include <vector>
#include <QStringList>
#include <f1x/openauto/autoapp/Service/Radio/RadioMapping.hpp>

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

namespace msg = aap_protobuf::service::radio::message;

const char* const cScanFmAction = "scan_fm";

namespace
{

const QStringList cBands{"fm", "am"};

QVariantMap bandOf(const QVariantMap& state, const QString& band)
{
    return state.value("bands").toMap().value(band).toMap();
}

// "96.2" / "1008": key of the station names the plugin remembers (radiolib.frequency_key)
QString nameKey(const QString& band, double frequency)
{
    return band == "fm" ? QString::number(frequency, 'f', 1) : QString::number(std::lround(frequency));
}

QString nameOf(const QVariantMap& state, const QString& band, double frequency)
{
    return bandOf(state, band).value("names").toMap().value(nameKey(band, frequency)).toString();
}

QList<double> frequencies(const QVariant& list)
{
    QList<double> result;
    for(const QVariant& value: list.toList())
    {
        result << value.toDouble();
    }
    return result;
}

void fillProgram(msg::RadioProgramInfo* program, const QString& band, double frequency, const QString& name)
{
    auto* identifier = program->mutable_program_selector()->mutable_primary_identifier();
    identifier->set_type(msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY);
    identifier->set_value(toKHz(band, frequency));
    // without a name the phone shows the frequency itself
    if(!name.isEmpty())
    {
        program->mutable_metadata()->set_station_name(name.toStdString());
    }
}

// stations of both bands, each frequency once, in band order
template<typename Callback>
void forEachStation(const QVariantMap& state, const QStringList& listNames, Callback callback)
{
    for(const QString& band: cBands)
    {
        QList<double> all;
        for(const QString& listName: listNames)
        {
            all << frequencies(bandOf(state, band).value(listName));
        }
        std::sort(all.begin(), all.end());
        double previous = -1;
        for(double frequency: all)
        {
            if(std::abs(frequency - previous) > 1e-6)
            {
                callback(band, frequency);
            }
            previous = frequency;
        }
    }
}

}

bool isFavorite(const QVariantMap& state, const QString& band, double frequency)
{
    for(double preset: frequencies(bandOf(state, band).value("presets")))
    {
        if(std::abs(preset - frequency) < 1e-6)
        {
            return true;
        }
    }
    return false;
}

uint64_t toKHz(const QString& band, double frequency)
{
    return static_cast<uint64_t>(std::llround(band == "fm" ? frequency * 1000 : frequency));
}

PluginFrequency fromKHz(uint64_t kHz)
{
    // Android Auto's own classification: AM 150-30000 kHz, FM 60-110 MHz
    if(kHz > 150 && kHz <= 30000)
    {
        return {"am", static_cast<double>(kHz)};
    }
    if(kHz > 60000 && kHz < 110000)
    {
        return {"fm", std::round(kHz / 100.0) / 10.0};
    }
    return {QString(), 0};
}

void fillService(aap_protobuf::service::radio::RadioService& service, const QVariantMap& state)
{
    const bool rds = state.value("rdsAvailable").toBool();
    int radioId = 0;
    for(const QString& band: cBands)
    {
        const QVariantMap spec = bandOf(state, band);
        if(spec.isEmpty())
        {
            continue;
        }
        const bool fm = band == "fm";
        const auto spacing = static_cast<int32_t>(toKHz(band, spec.value("step").toDouble()));

        // v1: one tuner per band (Android Auto up to 16.1)
        auto* properties = service.add_radio_properties();
        properties->set_radio_id(radioId++);
        properties->set_type(fm ? msg::FM_RADIO : msg::AM_RADIO);
        auto* range = properties->add_channel_range();
        range->set_min(static_cast<int32_t>(toKHz(band, spec.value("min").toDouble())));
        range->set_max(static_cast<int32_t>(toKHz(band, spec.value("max").toDouble())));
        properties->add_channel_spacings(spacing);
        properties->set_channel_spacing(spacing);
        properties->set_region(aap_protobuf::service::radio::RADIO_REGION_ITU_1);
        if(fm)
        {
            properties->set_rds(rds ? msg::RDS : msg::NO_RDS);
        }
        properties->set_mute_capability(true);

        // v2: a browse node per band (16.2+); the scan is offered for FM, the plugin scans only FM
        auto* group = service.mutable_bands()->add_groups();
        group->add_band_types(fm ? msg::RADIO_BAND_TYPE_FM : msg::RADIO_BAND_TYPE_AM);
        group->set_hide_empty_group(!fm);
        if(fm)
        {
            group->set_band_scan_action(cScanFmAction);
        }
    }
}

msg::RadioProgramListNotification programList(const QVariantMap& state)
{
    msg::RadioProgramListNotification notification;
    forEachStation(state, {"stations", "presets"}, [&](const QString& band, double frequency) {
        fillProgram(notification.add_programs(), band, frequency, nameOf(state, band, frequency));
    });
    return notification;
}

msg::RadioFavoriteListNotification favoriteList(const QVariantMap& state)
{
    msg::RadioFavoriteListNotification notification;
    forEachStation(state, {"presets"}, [&](const QString& band, double frequency) {
        fillProgram(notification.add_favorites(), band, frequency, nameOf(state, band, frequency));
    });
    return notification;
}

msg::RadioProgramInfoNotification programInfo(const QVariantMap& state)
{
    msg::RadioProgramInfoNotification notification;
    const QString band = state.value("band").toString();
    const double frequency = state.value("frequency").toDouble();
    const QVariantMap rds = state.value("rds").toMap();
    const QString ps = rds.value("ps").toString().trimmed();
    fillProgram(notification.mutable_program_info(), band, frequency, ps.isEmpty() ? nameOf(state, band, frequency) : ps);

    // RDS radiotext as the title (the format "artist - title" is not reliable enough to split). Not also as
    // station text: Android Auto shows both, the same line twice.
    const QString radiotext = rds.value("rt").toString().trimmed();
    if(!radiotext.isEmpty())
    {
        notification.mutable_program_info()->mutable_metadata()->mutable_song_metadata()->set_title(radiotext.toStdString());
    }
    notification.set_is_muted(!state.value("playing").toBool());
    // Field 3 is called has_audio_focus in aasdk/open-android-auto, but Android Auto shows it as the favorite heart
    // of the current station (tested with a Fairphone 4: set while playing, every station had a filled heart).
    notification.set_has_audio_focus(isFavorite(state, band, frequency));
    return notification;
}

QList<QVariantMap> tuneCommands(const msg::RadioProgramSelector& selector, const QVariantMap& state)
{
    // the frequency: primary identifier or one of the secondary ones
    PluginFrequency target{QString(), 0};
    std::vector<msg::RadioProgramIdentifier> identifiers{selector.primary_identifier()};
    identifiers.insert(identifiers.end(), selector.secondary_identifiers().begin(), selector.secondary_identifiers().end());
    for(const auto& identifier: identifiers)
    {
        if(identifier.type() == msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY)
        {
            target = fromKHz(identifier.value());
            break;
        }
    }
    if(target.band.isEmpty())
    {
        return {};
    }

    QList<QVariantMap> commands;
    if(state.value("band").toString() != target.band)
    {
        commands << QVariantMap{{"cmd", "band"}, {"value", target.band}};
    }
    commands << QVariantMap{{"cmd", "tune"}, {"value", target.frequency}};
    // a station chosen in Android Auto should be heard
    if(!state.value("playing").toBool())
    {
        commands << QVariantMap{{"cmd", "play"}};
    }
    return commands;
}

}
}
}
}
}
