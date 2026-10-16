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

// Radio plugin state <-> Android Auto radio channel (openhu#75)

#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <f1x/openauto/autoapp/Service/Radio/RadioMapping.hpp>

using namespace f1x::openauto::autoapp::service::radio;
namespace msg = aap_protobuf::service::radio::message;

namespace
{

// radio.state of the plugin (radio 2.2: "bands" with both bands)
QVariantMap state()
{
    const QVariantMap fm{{"min", 87.5}, {"max", 108.0}, {"step", 0.1}, {"unit", "MHz"},
                         {"stations", QVariantList{101.9, 88.6, 96.2}}, {"presets", QVariantList{96.2, 104.9}},
                         {"names", QVariantMap{{"96.2", "NDR 2"}, {"88.6", "NDR INFO"}}}};
    const QVariantMap am{{"min", 531}, {"max", 1602}, {"step", 9}, {"unit", "kHz"},
                         {"stations", QVariantList{}}, {"presets", QVariantList{1008}}, {"names", QVariantMap{}}};
    return {{"band", "fm"}, {"frequency", 96.2}, {"playing", true}, {"rdsAvailable", true},
            {"rds", QVariantMap{{"ps", "NDR 2"}, {"rt", "Dreamer - Ozzy Osbourne"}}},
            {"bands", QVariantMap{{"fm", fm}, {"am", am}}}};
}

// image of the given size as a data URL, like the plugin's radio.logos
QString dataUrl(int width, int height, const char* format = "PNG", QByteArray* bytes = nullptr)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(255, 183, 77));
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    if(bytes)
    {
        *bytes = data;
    }
    return QStringLiteral("data:image/%1;base64,").arg(QString(format).toLower()) + QString::fromLatin1(data.toBase64());
}

QImage decoded(const std::string& bytes)
{
    QImage image;
    image.loadFromData(QByteArray::fromStdString(bytes));
    return image;
}

msg::RadioProgramSelector selector(msg::RadioIdentifierType type, uint64_t value)
{
    msg::RadioProgramSelector selector;
    selector.mutable_primary_identifier()->set_type(type);
    selector.mutable_primary_identifier()->set_value(value);
    return selector;
}

}

class TestRadio: public QObject
{
    Q_OBJECT

private slots:
    void frequencies()
    {
        QCOMPARE(toKHz("fm", 96.2), uint64_t(96200));
        QCOMPARE(toKHz("fm", 101.9), uint64_t(101900));
        QCOMPARE(toKHz("am", 1008), uint64_t(1008));
        QCOMPARE(fromKHz(101900).band, QString("fm"));
        QCOMPARE(fromKHz(101900).frequency, 101.9);
        QCOMPARE(fromKHz(87600).frequency, 87.6);
        QCOMPARE(fromKHz(549).band, QString("am"));
        QCOMPARE(fromKHz(549).frequency, 549.0);
        // DAB band III, long wave below 150 kHz: not for this plugin
        QVERIFY(fromKHz(200000).band.isEmpty());
        QVERIFY(fromKHz(100).band.isEmpty());
    }

    void serviceAnnouncement()
    {
        aap_protobuf::service::radio::RadioService service;
        fillService(service, state());

        QCOMPARE(service.radio_properties_size(), 2);
        const auto& fm = service.radio_properties(0);
        QCOMPARE(fm.type(), msg::FM_RADIO);
        QCOMPARE(fm.channel_range(0).min(), 87500);
        QCOMPARE(fm.channel_range(0).max(), 108000);
        QCOMPARE(fm.channel_spacing(), 100);
        QCOMPARE(fm.rds(), msg::RDS);
        QVERIFY(fm.mute_capability());
        const auto& am = service.radio_properties(1);
        QCOMPARE(am.type(), msg::AM_RADIO);
        QCOMPARE(am.radio_id(), 1);
        QCOMPARE(am.channel_range(0).min(), 531);
        QCOMPARE(am.channel_spacing(), 9);

        QCOMPARE(service.bands().groups_size(), 2);
        QCOMPARE(service.bands().groups(0).band_types(0), msg::RADIO_BAND_TYPE_FM);
        QCOMPARE(QString::fromStdString(service.bands().groups(0).band_scan_action()), QString(cScanFmAction));
        QVERIFY(!service.bands().groups(0).hide_empty_group());
        QCOMPARE(service.bands().groups(1).band_types(0), msg::RADIO_BAND_TYPE_AM);
        QVERIFY(service.bands().groups(1).hide_empty_group());
        QVERIFY(!service.bands().groups(1).has_band_scan_action());
    }

    void serviceWithoutBands()
    {
        // a plugin before 2.2 has no "bands": nothing to announce but an empty service
        aap_protobuf::service::radio::RadioService service;
        fillService(service, {{"band", "fm"}, {"frequency", 96.2}});
        QCOMPARE(service.radio_properties_size(), 0);
        QVERIFY(!service.has_bands());
    }

    void programList()
    {
        const auto list = f1x::openauto::autoapp::service::radio::programList(state());
        // stations and presets of both bands, sorted, each once
        QCOMPARE(list.programs_size(), 5);
        const uint64_t expected[] = {88600, 96200, 101900, 104900, 1008};
        for(int i = 0; i < 5; ++i)
        {
            const auto& identifier = list.programs(i).program_selector().primary_identifier();
            QCOMPARE(identifier.type(), msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY);
            QCOMPARE(identifier.value(), expected[i]);
        }
        QCOMPARE(list.programs(0).metadata().station_name(), std::string("NDR INFO"));
        QCOMPARE(list.programs(1).metadata().station_name(), std::string("NDR 2"));
        // no remembered name: the phone shows the frequency
        QVERIFY(!list.programs(2).has_metadata());
    }

    void favorites()
    {
        const auto favorites = favoriteList(state());
        QCOMPARE(favorites.favorites_size(), 3);
        QCOMPARE(favorites.favorites(0).program_selector().primary_identifier().value(), uint64_t(96200));
        QCOMPARE(favorites.favorites(1).program_selector().primary_identifier().value(), uint64_t(104900));
        QCOMPARE(favorites.favorites(2).program_selector().primary_identifier().value(), uint64_t(1008));
    }

    void currentStation()
    {
        auto info = programInfo(state());
        QCOMPARE(info.program_info().program_selector().primary_identifier().value(), uint64_t(96200));
        QCOMPARE(info.program_info().metadata().station_name(), std::string("NDR 2"));
        QCOMPARE(info.program_info().metadata().song_metadata().title(), std::string("Dreamer - Ozzy Osbourne"));
        // not twice: Android Auto shows the station text below the title
        QVERIFY(!info.program_info().metadata().has_message());
        QVERIFY(!info.is_muted());
        // field 3 (has_audio_focus) is the favorite heart in Android Auto: 96.2 is a preset
        QVERIFY(info.has_audio_focus());

        // stopped, no RDS yet: remembered name, muted
        QVariantMap stopped = state();
        stopped["playing"] = false;
        stopped["frequency"] = 88.6;
        stopped["rds"] = QVariantMap();
        info = programInfo(stopped);
        QCOMPARE(info.program_info().metadata().station_name(), std::string("NDR INFO"));
        QVERIFY(!info.program_info().metadata().has_message());
        QVERIFY(info.is_muted());
        // 88.6 is no preset
        QVERIFY(!info.has_audio_focus());
    }

    void stationIcons()
    {
        // big logo: scaled to fit 128x128, aspect ratio kept
        const QImage big = decoded(scaledIcon(dataUrl(400, 200)));
        QCOMPARE(big.size(), QSize(128, 64));
        // small enough: the original bytes
        QByteArray small;
        const QString smallUrl = dataUrl(100, 100, "PNG", &small);
        QCOMPARE(scaledIcon(smallUrl), small.toStdString());
        QByteArray jpeg;
        const QString jpegUrl = dataUrl(64, 48, "JPEG", &jpeg);
        QCOMPARE(scaledIcon(jpegUrl), jpeg.toStdString());
        // not an image
        QVERIFY(scaledIcon("data:image/png;base64,AAAA").empty());
        QVERIFY(scaledIcon("file:///logo.png").empty());
        QVERIFY(scaledIcon("").empty());

        const QVariantMap logos{{"fm", QVariantMap{{"96.2", dataUrl(300, 300)}, {"88.6", dataUrl(64, 64)},
                                                   {"101.9", "data:image/png;base64,AAAA"}}},
                                {"am", QVariantMap{{"1008", dataUrl(32, 32, "JPEG")}}}};
        const StationIcons icons = f1x::openauto::autoapp::service::radio::stationIcons(logos);
        QCOMPARE(icons.size(), 3);
        QCOMPARE(decoded(icons.value("fm:96.2")).size(), QSize(128, 128));
        QVERIFY(icons.contains("fm:88.6"));
        QVERIFY(icons.contains("am:1008"));
        QVERIFY(!icons.contains("fm:101.9"));

        // in the station list, the favorites and the current station
        const auto list = f1x::openauto::autoapp::service::radio::programList(state(), icons);
        QCOMPARE(list.programs(0).metadata().station_icon().image_data(), icons.value("fm:88.6"));
        QCOMPARE(list.programs(1).metadata().station_icon().image_data(), icons.value("fm:96.2"));
        QVERIFY(!list.programs(2).metadata().has_station_icon());
        QCOMPARE(list.programs(4).metadata().station_icon().image_data(), icons.value("am:1008"));
        const auto favorites = favoriteList(state(), icons);
        QCOMPARE(favorites.favorites(0).metadata().station_icon().image_data(), icons.value("fm:96.2"));
        QVERIFY(!favorites.favorites(1).metadata().has_station_icon());
        const auto info = programInfo(state(), icons);
        QCOMPARE(info.program_info().metadata().station_icon().image_data(), icons.value("fm:96.2"));
        QCOMPARE(info.program_info().metadata().station_name(), std::string("NDR 2"));
        // without logos no icon
        QVERIFY(!programInfo(state()).program_info().metadata().has_station_icon());
    }

    void tune()
    {
        // same band, playing: only tune
        auto commands = tuneCommands(selector(msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY, 101900), state());
        QCOMPARE(commands.size(), 1);
        QCOMPARE(commands[0].value("cmd").toString(), QString("tune"));
        QCOMPARE(commands[0].value("value").toDouble(), 101.9);

        // other band, stopped: band, tune, play
        QVariantMap stopped = state();
        stopped["playing"] = false;
        commands = tuneCommands(selector(msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY, 1008), stopped);
        QCOMPARE(commands.size(), 3);
        QCOMPARE(commands[0], (QVariantMap{{"cmd", "band"}, {"value", "am"}}));
        QCOMPARE(commands[1].value("value").toDouble(), 1008.0);
        QCOMPARE(commands[2].value("cmd").toString(), QString("play"));

        // RDS PI with the frequency as secondary identifier
        auto withSecondary = selector(msg::RADIO_IDENTIFIER_TYPE_RDS_PI, 0xD3C2);
        auto* secondary = withSecondary.add_secondary_identifiers();
        secondary->set_type(msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY);
        secondary->set_value(88600);
        commands = tuneCommands(withSecondary, state());
        QCOMPARE(commands.size(), 1);
        QCOMPARE(commands[0].value("value").toDouble(), 88.6);

        // only RDS PI or DAB: the plugin cannot tune it (openhu-plugins#7)
        QVERIFY(tuneCommands(selector(msg::RADIO_IDENTIFIER_TYPE_RDS_PI, 0xD3C2), state()).isEmpty());
        QVERIFY(tuneCommands(selector(msg::RADIO_IDENTIFIER_TYPE_DAB_SID_EXT, 0x1D3C2), state()).isEmpty());
        QVERIFY(tuneCommands(selector(msg::RADIO_IDENTIFIER_TYPE_AMFM_FREQUENCY, 200000), state()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRadio)
#include "test_radio.moc"
