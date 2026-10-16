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
#include <QBuffer>
#include <QImage>
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
const char* const cScanDabAction = "scan_dab";

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

QString iconKey(const QString& band, double frequency)
{
    return band + ":" + nameKey(band, frequency);
}

// Band III channels (ETSI EN 300 401) -> centre frequency in kHz, like the plugin's dablib.DAB_CHANNELS
uint64_t dabChannelKHz(const QString& channel)
{
    static const QHash<QString, uint64_t> channels{
        {"5A", 174928}, {"5B", 176640}, {"5C", 178352}, {"5D", 180064}, {"6A", 181936}, {"6B", 183648},
        {"6C", 185360}, {"6D", 187072}, {"7A", 188928}, {"7B", 190640}, {"7C", 192352}, {"7D", 194064},
        {"8A", 195936}, {"8B", 197648}, {"8C", 199360}, {"8D", 201072}, {"9A", 202928}, {"9B", 204640},
        {"9C", 206352}, {"9D", 208064}, {"10A", 209936}, {"10B", 211648}, {"10C", 213360}, {"10D", 215072},
        {"11A", 216928}, {"11B", 218640}, {"11C", 220352}, {"11D", 222064}, {"12A", 223936}, {"12B", 225648},
        {"12C", 227360}, {"12D", 229072}, {"13A", 230784}, {"13B", 232496}, {"13C", 234208}, {"13D", 235776},
        {"13E", 237488}, {"13F", 239200}};
    return channels.value(channel.toUpper(), 0);
}

uint64_t hexValue(const QString& text)
{
    bool ok = false;
    const uint64_t value = text.toULongLong(&ok, 16);
    return ok ? value : 0;
}

// A DAB station of the plugin (radio 2.7): bands.dab.services[key] = {label, channel, ensemble, ecc, ...}
void fillDabProgram(msg::RadioProgramInfo* program, const QString& key, const QVariantMap& service,
                    const QString& name, const StationIcons& icons)
{
    // Android's ProgramSelector DAB_SID_EXT: SId in bits 0-15, ECC in bits 16-23, SCIdS (0) in bits 24-27. The
    // encoding inside Android Auto is not documented (open-android-auto names only the type), to be checked.
    const uint64_t sid = dabSid(key);
    auto* identifier = program->mutable_program_selector()->mutable_primary_identifier();
    identifier->set_type(msg::RADIO_IDENTIFIER_TYPE_DAB_SID_EXT);
    identifier->set_value(sid | ((hexValue(service.value("ecc").toString()) & 0xFF) << 16));
    const uint64_t ensemble = hexValue(service.value("ensemble").toString());
    if(ensemble > 0)
    {
        auto* secondary = program->mutable_program_selector()->add_secondary_identifiers();
        secondary->set_type(msg::RADIO_IDENTIFIER_TYPE_DAB_ENSEMBLE);
        secondary->set_value(ensemble);
    }
    const uint64_t kHz = dabChannelKHz(service.value("channel").toString());
    if(kHz > 0)
    {
        auto* secondary = program->mutable_program_selector()->add_secondary_identifiers();
        secondary->set_type(msg::RADIO_IDENTIFIER_TYPE_DAB_FREQUENCY);
        secondary->set_value(kHz);
    }
    auto* metadata = program->mutable_metadata();
    const QString label = name.isEmpty() ? service.value("label").toString() : name;
    metadata->set_station_name((label.isEmpty() ? key : label).toStdString());
    if(!service.value("label").toString().isEmpty())
    {
        metadata->set_service_name(service.value("label").toString().toStdString());
    }
    const auto icon = icons.constFind("dab:" + key);
    if(icon != icons.constEnd())
    {
        metadata->mutable_station_icon()->set_image_data(*icon);
    }
}

QVariantMap dabServiceOf(const QVariantMap& state, const QString& key)
{
    return bandOf(state, "dab").value("services").toMap().value(key).toMap();
}

// DAB stations of the given lists in the plugin's order (channel, name), each once
QStringList dabStations(const QVariantMap& state, const QStringList& listNames)
{
    QStringList keys;
    for(const QString& listName: listNames)
    {
        for(const QVariant& value: bandOf(state, "dab").value(listName).toList())
        {
            const uint64_t sid = dabSid(value.toString());
            if(sid > 0 && !keys.contains(dabKey(sid)))
            {
                keys << dabKey(sid);
            }
        }
    }
    return keys;
}

QString dabName(const QVariantMap& state, const QString& key)
{
    return bandOf(state, "dab").value("names").toMap().value(key).toString();
}

QString piString(uint64_t pi)
{
    return "0x" + QString::number(pi, 16).toUpper().rightJustified(4, '0');
}

void fillProgram(msg::RadioProgramInfo* program, const QString& band, double frequency, const QString& name,
                 const StationIcons& icons, const QString& pi = QString())
{
    auto* identifier = program->mutable_program_selector()->mutable_primary_identifier();
    identifier->set_type(msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY);
    identifier->set_value(toKHz(band, frequency));
    // the programme behind the frequency (Android's ProgramSelector: RDS PI as secondary identifier of AM/FM)
    bool ok = false;
    const uint64_t code = pi.toULongLong(&ok, 16);
    if(ok && code > 0 && code <= 0xFFFF)
    {
        auto* secondary = program->mutable_program_selector()->add_secondary_identifiers();
        secondary->set_type(msg::RADIO_IDENTIFIER_TYPE_RDS_PI);
        secondary->set_value(code);
    }
    // without a name the phone shows the frequency itself
    if(!name.isEmpty())
    {
        program->mutable_metadata()->set_station_name(name.toStdString());
    }
    const auto icon = icons.constFind(iconKey(band, frequency));
    if(icon != icons.constEnd())
    {
        program->mutable_metadata()->mutable_station_icon()->set_image_data(*icon);
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

std::string scaledIcon(const QString& dataUrl, int size)
{
    // data:image/png;base64,....
    const int comma = dataUrl.indexOf(',');
    if(!dataUrl.startsWith("data:image/") || comma < 0 || !dataUrl.left(comma).endsWith(";base64"))
    {
        return {};
    }
    const QByteArray bytes = QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1());
    QImage image;
    if(!image.loadFromData(bytes))
    {
        return {};
    }
    const bool known = bytes.startsWith("\x89PNG") || bytes.startsWith("\xff\xd8\xff");
    if(known && image.width() <= size && image.height() <= size)
    {
        return bytes.toStdString();
    }
    QByteArray scaled;
    QBuffer buffer(&scaled);
    buffer.open(QIODevice::WriteOnly);
    image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "PNG");
    return scaled.toStdString();
}

uint64_t dabSid(const QString& key)
{
    return hexValue(key);
}

QString dabKey(uint64_t sid)
{
    const int digits = sid > 0xFFFF ? 8 : 4;
    return "0x" + QString::number(sid, 16).toUpper().rightJustified(digits, '0');
}

QString stationKey(const QVariantMap& state)
{
    const QString band = state.value("band").toString();
    if(band == "dab")
    {
        const uint64_t sid = dabSid(state.value("frequency").toString());
        return sid > 0 ? dabKey(sid) : QString();
    }
    return nameKey(band, state.value("frequency").toDouble());
}

StationIcons stationIcons(const QVariantMap& logos)
{
    StationIcons icons;
    const QVariantMap dabLogos = logos.value("dab").toMap();
    for(auto logo = dabLogos.constBegin(); logo != dabLogos.constEnd(); ++logo)
    {
        const std::string icon = scaledIcon(logo.value().toString());
        const uint64_t sid = dabSid(logo.key());
        if(!icon.empty() && sid > 0)
        {
            icons.insert("dab:" + dabKey(sid), icon);
        }
    }
    for(const QString& band: cBands)
    {
        const QVariantMap bandLogos = logos.value(band).toMap();
        for(auto logo = bandLogos.constBegin(); logo != bandLogos.constEnd(); ++logo)
        {
            const std::string icon = scaledIcon(logo.value().toString());
            if(!icon.empty())
            {
                // the key as the plugin wrote it ("96.2"), normalised like the station keys
                icons.insert(iconKey(band, logo.key().toDouble()), icon);
            }
        }
    }
    return icons;
}

QString piOf(const QVariantMap& state, const QString& band, double frequency)
{
    return bandOf(state, band).value("pis").toMap().value(nameKey(band, frequency)).toString();
}

QString currentPi(const QVariantMap& state)
{
    const QString rds = state.value("rds").toMap().value("pi").toString();
    return !rds.isEmpty() ? rds
                          : piOf(state, state.value("band").toString(), state.value("frequency").toDouble());
}

bool isFavorite(const QVariantMap& state, const QString& band, double frequency)
{
    if(band == "dab")
    {
        // DAB presets are service ids, there is no frequency: the station that plays
        const QString key = stationKey(state);
        for(const QVariant& preset: bandOf(state, band).value("presets").toList())
        {
            if(!key.isEmpty() && dabKey(dabSid(preset.toString())) == key)
            {
                return true;
            }
        }
        return false;
    }
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

    // DAB (radio 2.7) only with a decoder (welle-cli) on the head unit; no v1 tuner, it is frequency based
    if(state.value("dabAvailable").toBool() && !bandOf(state, "dab").isEmpty())
    {
        auto* group = service.mutable_bands()->add_groups();
        group->add_band_types(msg::RADIO_BAND_TYPE_DAB);
        group->set_hide_empty_group(false);
        group->set_band_scan_action(cScanDabAction);
    }
}

msg::RadioProgramListNotification programList(const QVariantMap& state, const StationIcons& icons)
{
    msg::RadioProgramListNotification notification;
    forEachStation(state, {"stations", "presets"}, [&](const QString& band, double frequency) {
        fillProgram(notification.add_programs(), band, frequency, nameOf(state, band, frequency), icons,
                    piOf(state, band, frequency));
    });
    for(const QString& key: dabStations(state, {"stations", "presets"}))
    {
        fillDabProgram(notification.add_programs(), key, dabServiceOf(state, key), dabName(state, key), icons);
    }
    return notification;
}

msg::RadioFavoriteListNotification favoriteList(const QVariantMap& state, const StationIcons& icons)
{
    msg::RadioFavoriteListNotification notification;
    forEachStation(state, {"presets"}, [&](const QString& band, double frequency) {
        fillProgram(notification.add_favorites(), band, frequency, nameOf(state, band, frequency), icons,
                    piOf(state, band, frequency));
    });
    for(const QString& key: dabStations(state, {"presets"}))
    {
        fillDabProgram(notification.add_favorites(), key, dabServiceOf(state, key), dabName(state, key), icons);
    }
    return notification;
}

msg::RadioProgramInfoNotification programInfo(const QVariantMap& state, const StationIcons& icons,
                                              const std::string& picture)
{
    msg::RadioProgramInfoNotification notification;
    const QString band = state.value("band").toString();
    const double frequency = state.value("frequency").toDouble();
    const QVariantMap rds = state.value("rds").toMap();
    const QString ps = rds.value("ps").toString().trimmed();
    if(band == "dab")
    {
        // DAB: service label as rds.ps, DLS as rds.rt, the slideshow picture as album art
        const QString key = stationKey(state);
        fillDabProgram(notification.mutable_program_info(), key, dabServiceOf(state, key),
                       ps.isEmpty() ? dabName(state, key) : ps, icons);
        if(!picture.empty())
        {
            notification.mutable_program_info()->mutable_metadata()->mutable_song_metadata()->mutable_album_art()
                ->set_image_data(picture);
        }
    }
    else
    {
        fillProgram(notification.mutable_program_info(), band, frequency,
                    ps.isEmpty() ? nameOf(state, band, frequency) : ps, icons, currentPi(state));
    }

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

TuneTarget tuneTarget(const QList<QVariantMap>& commands, const QVariantMap& state)
{
    TuneTarget target;
    target.band = state.value("band").toString();
    target.from = stationKey(state);
    for(const QVariantMap& command: commands)
    {
        const QString cmd = command.value("cmd").toString();
        if(cmd == "band")
        {
            target.band = command.value("value").toString();
        }
        else if(cmd == "tune")
        {
            const QVariant value = command.value("value");
            target.station = target.band == "dab" ? dabKey(dabSid(value.toString())) : nameKey(target.band, value.toDouble());
        }
        else if(cmd == "tunePi")
        {
            target.band = "fm";
            target.pi = command.value("value").toString();
        }
    }
    return target;
}

bool tuneArrived(const TuneTarget& target, const QVariantMap& state)
{
    const bool sameBand = state.value("band").toString() == target.band;
    if(!target.pi.isEmpty())
    {
        return sameBand && currentPi(state).compare(target.pi, Qt::CaseInsensitive) == 0;
    }
    if(target.station.isEmpty())
    {
        return stationKey(state) != target.from;
    }
    return sameBand && stationKey(state) == target.station;
}

QList<QVariantMap> tuneCommands(const msg::RadioProgramSelector& selector, const QVariantMap& state)
{
    // the frequency (primary identifier or one of the secondary ones), else the RDS PI
    PluginFrequency target{QString(), 0};
    uint64_t pi = 0;
    uint64_t sid = 0;
    std::vector<msg::RadioProgramIdentifier> identifiers{selector.primary_identifier()};
    identifiers.insert(identifiers.end(), selector.secondary_identifiers().begin(), selector.secondary_identifiers().end());
    for(const auto& identifier: identifiers)
    {
        if(identifier.type() == msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY && target.band.isEmpty())
        {
            target = fromKHz(identifier.value());
        }
        else if(identifier.type() == msg::RADIO_IDENTIFIER_TYPE_RDS_PI && pi == 0 && identifier.value() <= 0xFFFF)
        {
            pi = identifier.value();
        }
        else if(identifier.type() == msg::RADIO_IDENTIFIER_TYPE_DAB_SID_EXT && sid == 0)
        {
            // as sent in the station list: SId in the low 16 bits (ECC, SCIdS above)
            sid = identifier.value() & 0xFFFF;
        }
        else if(identifier.type() == msg::RADIO_IDENTIFIER_TYPE_DAB_DMB_SID_EXT && sid == 0)
        {
            // Android's DAB_DMB_SID_EXT: 32 bit SId in the low bits
            sid = identifier.value() & 0xFFFFFFFF;
        }
    }
    if(target.band.isEmpty() && pi == 0 && sid == 0)
    {
        return {};
    }

    QList<QVariantMap> commands;
    if(target.band.isEmpty() && sid != 0)
    {
        // a DAB service of the plugin (radio 2.7)
        if(state.value("band").toString() != "dab")
        {
            commands << QVariantMap{{"cmd", "band"}, {"value", "dab"}};
        }
        commands << QVariantMap{{"cmd", "tune"}, {"value", dabKey(sid)}};
    }
    else if(target.band.isEmpty())
    {
        // the plugin (radio 2.6) switches to FM and picks the frequency of the programme itself
        commands << QVariantMap{{"cmd", "tunePi"}, {"value", piString(pi)}};
    }
    else
    {
        if(state.value("band").toString() != target.band)
        {
            commands << QVariantMap{{"cmd", "band"}, {"value", target.band}};
        }
        commands << QVariantMap{{"cmd", "tune"}, {"value", target.frequency}};
    }
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
