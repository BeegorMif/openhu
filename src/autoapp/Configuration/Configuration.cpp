/*
*  This file is part of OpenHU, based on the openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <algorithm>
#include <fstream>
#include <boost/property_tree/ini_parser.hpp>
#include <QCryptographicHash>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::configuration
{

namespace
{

const std::string cConfigFileName = "openhu.ini";
// read when openhu.ini does not exist yet (configuration of OpenAuto)
const std::string cLegacyConfigFileName = "openauto.ini";

// Key names are the ones of OpenAuto, so existing configurations keep working.
const std::string cHandednessKey = "General.HandednessOfTrafficType";
const std::string cShowCursorKey = "General.ShowCursor";
const std::string cNameSuffixKey = "General.NameSuffix";
const std::string cLanguageKey = "General.Language";
const std::string cNightModeKey = "General.NightMode";
const std::string cVideoResolutionAutoKey = "Video.AutoResolution";
const std::string cVideoDecoderKey = "Video.Decoder";
const std::string cApiPortKey = "Api.Port";
const std::string cApiListenKey = "Api.Listen";
const std::string cApiAllowHotspotKey = "Api.AllowHotspot";
const std::string cDebugEnabledKey = "Debug.Enabled";
const std::string cVideoCodecKey = "Video.Codec";
const std::string cApiTokenKey = "Api.Token";
const std::string cApiAllowedOriginsKey = "Api.AllowedOrigins";
const std::string cVideoFPSKey = "Video.FPS";
const std::string cVideoResolutionKey = "Video.Resolution";
const std::string cVideoScreenDPIKey = "Video.ScreenDPI";
const std::string cVideoMarginWidthKey = "Video.MarginWidth";
const std::string cVideoMarginHeightKey = "Video.MarginHeight";
const std::string cTouchscreenKey = "Input.EnableTouchscreen";
const std::string cOnScreenKeyboardKey = "Input.OnScreenKeyboard";
const std::string cBluetoothAdapterTypeKey = "Bluetooth.AdapterType";
const std::string cBluetoothAdapterAddressKey = "Bluetooth.AdapterAddress";
const std::string cWirelessProjectionKey = "Bluetooth.WirelessProjectionEnabled";
const std::string cWirelessSsidKey = "Wireless.Ssid";
const std::string cWirelessPasswordKey = "Wireless.Password";
const std::string cWirelessBandKey = "Wireless.Band";
const std::string cWirelessCountryKey = "Wireless.Country";
const std::string cWirelessInterfaceKey = "Wireless.Interface";
const std::string cWirelessLastDeviceKey = "Wireless.LastDevice";
const std::string cWirelessPriorityKey = "Wireless.Priority";
const std::string cWirelessHotspotKey = "Wireless.Hotspot";
const std::string cWirelessChannelKey = "Wireless.Channel";
const std::string cWirelessAddressKey = "Wireless.Address";
const std::string cWirelessDhcpRangeKey = "Wireless.DhcpRange";
const std::string cWirelessClientKey = "Wireless.Client";
const std::string cWirelessClientInterfaceKey = "Wireless.ClientInterface";
const std::string cCarAudioEnabledKey = "CarAudio.Enabled";
const std::string cCarAudioDeviceKey = "CarAudio.Device";
const std::string cCarAudioPinKey = "CarAudio.Pin";
const std::string cAudioMediaKey = "AudioChannel.MediaEnabled";
const std::string cAudioGuidanceKey = "AudioChannel.GuidanceEnabled";
const std::string cAudioSystemKey = "AudioChannel.SystemEnabled";
const std::string cAudioTelephonyKey = "AudioChannel.TelephonyEnabled";
const std::string cRadioAndroidAutoKey = "Radio.AndroidAuto";
const std::string cAudioDuckLevelKey = "Audio.DuckLevel";

}

Configuration::Configuration()
{
    this->load();
}

void Configuration::load()
{
    this->reset();

    const bool useLegacy = !std::ifstream(cConfigFileName).good() && std::ifstream(cLegacyConfigFileName).good();
    const std::string fileName = useLegacy ? cLegacyConfigFileName : cConfigFileName;
    try
    {
        if(!std::ifstream(fileName).good())
        {
            throw boost::property_tree::ptree_error(cConfigFileName + " not found");
        }

        boost::property_tree::ptree ini;
        boost::property_tree::ini_parser::read_ini(fileName, ini);

        handednessOfTrafficType_ = static_cast<HandednessOfTrafficType>(
            ini.get<uint32_t>(cHandednessKey, static_cast<uint32_t>(handednessOfTrafficType_)));
        showCursor_ = ini.get<bool>(cShowCursorKey, showCursor_);
        nameSuffix_ = ini.get<std::string>(cNameSuffixKey, nameSuffix_);
        language_ = ini.get<std::string>(cLanguageKey, language_);
        nightMode_ = ini.get<std::string>(cNightModeKey, nightMode_);
        apiPort_ = ini.get<uint16_t>(cApiPortKey, apiPort_);
        apiListenAddress_ = ini.get<std::string>(cApiListenKey, apiListenAddress_);
        apiAllowHotspot_ = ini.get<bool>(cApiAllowHotspotKey, apiAllowHotspot_);
        debugEnabled_ = ini.get<bool>(cDebugEnabledKey, debugEnabled_);
        videoCodec_ = ini.get<std::string>(cVideoCodecKey, videoCodec_);
        apiToken_ = ini.get<std::string>(cApiTokenKey, apiToken_);
        apiAllowedOrigins_ = ini.get<std::string>(cApiAllowedOriginsKey, apiAllowedOrigins_);

        videoFPS_ = static_cast<aap_protobuf::service::media::sink::message::VideoFrameRateType>(
            ini.get<uint32_t>(cVideoFPSKey, videoFPS_));
        videoResolution_ = static_cast<aap_protobuf::service::media::sink::message::VideoCodecResolutionType>(
            ini.get<uint32_t>(cVideoResolutionKey, videoResolution_));
        videoResolutionAuto_ = ini.get<bool>(cVideoResolutionAutoKey, videoResolutionAuto_);
        videoDecoder_ = ini.get<std::string>(cVideoDecoderKey, videoDecoder_);
        screenDPI_ = ini.get<size_t>(cVideoScreenDPIKey, screenDPI_);
        videoMargins_ = QRect(0, 0, ini.get<int32_t>(cVideoMarginWidthKey, 0), ini.get<int32_t>(cVideoMarginHeightKey, 0));
        enableTouchscreen_ = ini.get<bool>(cTouchscreenKey, enableTouchscreen_);
        onScreenKeyboard_ = ini.get<bool>(cOnScreenKeyboardKey, onScreenKeyboard_);

        bluetoothAdapterType_ = static_cast<BluetoothAdapterType>(
            ini.get<uint32_t>(cBluetoothAdapterTypeKey, static_cast<uint32_t>(bluetoothAdapterType_)));
        bluetoothAdapterAddress_ = ini.get<std::string>(cBluetoothAdapterAddressKey, bluetoothAdapterAddress_);
        wirelessProjectionEnabled_ = ini.get<bool>(cWirelessProjectionKey, wirelessProjectionEnabled_);
        wireless_.ssid = ini.get<std::string>(cWirelessSsidKey, wireless_.ssid);
        wireless_.password = ini.get<std::string>(cWirelessPasswordKey, wireless_.password);
        wireless_.band = ini.get<std::string>(cWirelessBandKey, wireless_.band);
        wireless_.country = ini.get<std::string>(cWirelessCountryKey, wireless_.country);
        wireless_.interface = ini.get<std::string>(cWirelessInterfaceKey, wireless_.interface);
        wireless_.lastDevice = ini.get<std::string>(cWirelessLastDeviceKey, wireless_.lastDevice);
        wireless_.priority = ini.get<std::string>(cWirelessPriorityKey, wireless_.priority);
        wireless_.hotspot = ini.get<std::string>(cWirelessHotspotKey, wireless_.hotspot);
        wireless_.channel = ini.get<int>(cWirelessChannelKey, wireless_.channel);
        wireless_.address = ini.get<std::string>(cWirelessAddressKey, wireless_.address);
        wireless_.dhcpRange = ini.get<std::string>(cWirelessDhcpRangeKey, wireless_.dhcpRange);
        wireless_.client = ini.get<bool>(cWirelessClientKey, wireless_.client);
        wireless_.clientInterface = ini.get<std::string>(cWirelessClientInterfaceKey, wireless_.clientInterface);
        carAudio_.enabled = ini.get<bool>(cCarAudioEnabledKey, carAudio_.enabled);
        carAudio_.device = ini.get<std::string>(cCarAudioDeviceKey, carAudio_.device);
        carAudio_.pin = ini.get<std::string>(cCarAudioPinKey, carAudio_.pin);

        audioChannelEnabledMedia_ = ini.get<bool>(cAudioMediaKey, audioChannelEnabledMedia_);
        audioChannelEnabledGuidance_ = ini.get<bool>(cAudioGuidanceKey, audioChannelEnabledGuidance_);
        audioChannelEnabledSystem_ = ini.get<bool>(cAudioSystemKey, audioChannelEnabledSystem_);
        audioChannelEnabledTelephony_ = ini.get<bool>(cAudioTelephonyKey, audioChannelEnabledTelephony_);
        radioServiceEnabled_ = ini.get<bool>(cRadioAndroidAutoKey, radioServiceEnabled_);
        duckLevel_ = std::clamp(ini.get<int>(cAudioDuckLevelKey, duckLevel_), 0, 100);
    }
    catch(const boost::property_tree::ptree_error& e)
    {
        OPENAUTO_LOG(warning) << "[Configuration] failed to read " << fileName << ": " << e.what() << ", using defaults";
        this->reset();
    }
}

void Configuration::reset()
{
    handednessOfTrafficType_ = HandednessOfTrafficType::LEFT_HAND_DRIVE;
    showCursor_ = false;
    nameSuffix_ = "";
    language_ = "";
    nightMode_ = "auto";
    videoResolutionAuto_ = true;
    videoDecoder_ = "auto";
    apiPort_ = 5520;
    apiListenAddress_ = "127.0.0.1";
    apiAllowHotspot_ = false;
    debugEnabled_ = false;
    videoCodec_ = "auto";
    apiToken_ = "";
    apiAllowedOrigins_ = "*";
    videoFPS_ = aap_protobuf::service::media::sink::message::VideoFrameRateType::VIDEO_FPS_30;
    videoResolution_ = aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_800x480;
    screenDPI_ = 140;
    videoMargins_ = QRect(0, 0, 0, 0);
    enableTouchscreen_ = true;
    onScreenKeyboard_ = true;
    bluetoothAdapterType_ = BluetoothAdapterType::NONE;
    bluetoothAdapterAddress_ = "";
    // off by default: it turns the WiFi card into a hotspot
    wirelessProjectionEnabled_ = false;
    wireless_ = WirelessSettings{"", "", "5", "DE", "", "", "usb", "always", 0, "10.42.0.1/24", "", true, ""};
    carAudio_ = CarAudioSettings{false, "", "0000"};
    audioChannelEnabledMedia_ = true;
    audioChannelEnabledGuidance_ = true;
    audioChannelEnabledSystem_ = true;
    audioChannelEnabledTelephony_ = true;
    radioServiceEnabled_ = true;
    duckLevel_ = 20;
}

void Configuration::save()
{
    boost::property_tree::ptree ini;
    ini.put<uint32_t>(cHandednessKey, static_cast<uint32_t>(handednessOfTrafficType_));
    ini.put<bool>(cShowCursorKey, showCursor_);
    ini.put<std::string>(cNameSuffixKey, nameSuffix_);
    ini.put<std::string>(cLanguageKey, language_);
    ini.put<std::string>(cNightModeKey, nightMode_);
    ini.put<bool>(cVideoResolutionAutoKey, videoResolutionAuto_);
    ini.put<std::string>(cVideoDecoderKey, videoDecoder_);
    ini.put<uint16_t>(cApiPortKey, apiPort_);
    ini.put<std::string>(cApiListenKey, apiListenAddress_);
    ini.put<bool>(cApiAllowHotspotKey, apiAllowHotspot_);
    ini.put<bool>(cDebugEnabledKey, debugEnabled_);
    ini.put<std::string>(cVideoCodecKey, videoCodec_);
    ini.put<std::string>(cApiTokenKey, apiToken_);
    ini.put<std::string>(cApiAllowedOriginsKey, apiAllowedOrigins_);
    ini.put<uint32_t>(cVideoFPSKey, static_cast<uint32_t>(videoFPS_));
    ini.put<uint32_t>(cVideoResolutionKey, static_cast<uint32_t>(videoResolution_));
    ini.put<size_t>(cVideoScreenDPIKey, screenDPI_);
    ini.put<int32_t>(cVideoMarginWidthKey, videoMargins_.width());
    ini.put<int32_t>(cVideoMarginHeightKey, videoMargins_.height());
    ini.put<bool>(cTouchscreenKey, enableTouchscreen_);
    ini.put<bool>(cOnScreenKeyboardKey, onScreenKeyboard_);
    ini.put<uint32_t>(cBluetoothAdapterTypeKey, static_cast<uint32_t>(bluetoothAdapterType_));
    ini.put<std::string>(cBluetoothAdapterAddressKey, bluetoothAdapterAddress_);
    ini.put<bool>(cWirelessProjectionKey, wirelessProjectionEnabled_);
    ini.put<std::string>(cWirelessSsidKey, wireless_.ssid);
    ini.put<std::string>(cWirelessPasswordKey, wireless_.password);
    ini.put<std::string>(cWirelessBandKey, wireless_.band);
    ini.put<std::string>(cWirelessCountryKey, wireless_.country);
    ini.put<std::string>(cWirelessInterfaceKey, wireless_.interface);
    ini.put<std::string>(cWirelessLastDeviceKey, wireless_.lastDevice);
    ini.put<std::string>(cWirelessPriorityKey, wireless_.priority);
    ini.put<std::string>(cWirelessHotspotKey, wireless_.hotspot);
    ini.put<int>(cWirelessChannelKey, wireless_.channel);
    ini.put<std::string>(cWirelessAddressKey, wireless_.address);
    ini.put<std::string>(cWirelessDhcpRangeKey, wireless_.dhcpRange);
    ini.put<bool>(cWirelessClientKey, wireless_.client);
    ini.put<std::string>(cWirelessClientInterfaceKey, wireless_.clientInterface);
    ini.put<bool>(cCarAudioEnabledKey, carAudio_.enabled);
    ini.put<std::string>(cCarAudioDeviceKey, carAudio_.device);
    ini.put<std::string>(cCarAudioPinKey, carAudio_.pin);
    ini.put<bool>(cAudioMediaKey, audioChannelEnabledMedia_);
    ini.put<bool>(cAudioGuidanceKey, audioChannelEnabledGuidance_);
    ini.put<bool>(cAudioSystemKey, audioChannelEnabledSystem_);
    ini.put<bool>(cAudioTelephonyKey, audioChannelEnabledTelephony_);
    ini.put<bool>(cRadioAndroidAutoKey, radioServiceEnabled_);
    ini.put<int>(cAudioDuckLevelKey, duckLevel_);

    try
    {
        boost::property_tree::ini_parser::write_ini(cConfigFileName, ini);
    }
    catch(const boost::property_tree::ini_parser_error& e)
    {
        OPENAUTO_LOG(error) << "[Configuration] failed to write " << cConfigFileName << ": " << e.what();
    }
}

void Configuration::setHandednessOfTrafficType(HandednessOfTrafficType value)
{
    handednessOfTrafficType_ = value;
}

HandednessOfTrafficType Configuration::getHandednessOfTrafficType() const
{
    return handednessOfTrafficType_;
}

void Configuration::showCursor(bool value)
{
    showCursor_ = value;
}

bool Configuration::showCursor() const
{
    return showCursor_;
}

void Configuration::setOnScreenKeyboard(bool value)
{
    onScreenKeyboard_ = value;
}

bool Configuration::onScreenKeyboard() const
{
    return onScreenKeyboard_;
}

std::string Configuration::getNightMode() const
{
    return nightMode_;
}

void Configuration::setNightMode(const std::string& value)
{
    nightMode_ = value;
}

std::string Configuration::getLanguage() const
{
    return language_;
}

void Configuration::setLanguage(const std::string& value)
{
    language_ = value;
}

std::string Configuration::getNameSuffix() const
{
    return nameSuffix_;
}

void Configuration::setNameSuffix(const std::string& value)
{
    nameSuffix_ = value;
}

std::string Configuration::getVehicleId() const
{
    // without suffix the id OpenAuto always used, so existing phones keep recognizing the head unit
    if(nameSuffix_.empty())
    {
        return "2024110822150988";
    }
    const QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString("OpenHU|" + nameSuffix_),
                                                     QCryptographicHash::Sha1);
    return hash.toHex().left(16).toStdString();
}

bool Configuration::getVideoResolutionAuto() const
{
    return videoResolutionAuto_;
}

void Configuration::setVideoResolutionAuto(bool value)
{
    videoResolutionAuto_ = value;
}

std::string Configuration::getVideoDecoder() const
{
    return videoDecoder_;
}

void Configuration::setVideoDecoder(const std::string& value)
{
    videoDecoder_ = value;
}

uint16_t Configuration::getApiPort() const
{
    return apiPort_;
}

void Configuration::setApiPort(uint16_t value)
{
    apiPort_ = value;
}

std::string Configuration::getApiListenAddress() const
{
    return apiListenAddress_;
}

void Configuration::setApiListenAddress(const std::string& value)
{
    apiListenAddress_ = value;
}

bool Configuration::getDebugEnabled() const
{
    return debugEnabled_;
}

void Configuration::setDebugEnabled(bool value)
{
    debugEnabled_ = value;
}

std::string Configuration::getVideoCodec() const
{
    return videoCodec_;
}

void Configuration::setVideoCodec(const std::string& value)
{
    videoCodec_ = value;
}

bool Configuration::getApiAllowHotspot() const
{
    return apiAllowHotspot_;
}

void Configuration::setApiAllowHotspot(bool value)
{
    apiAllowHotspot_ = value;
}

std::string Configuration::getApiToken() const
{
    return apiToken_;
}

void Configuration::setApiToken(const std::string& value)
{
    apiToken_ = value;
}

std::string Configuration::getApiAllowedOrigins() const
{
    return apiAllowedOrigins_;
}

void Configuration::setApiAllowedOrigins(const std::string& value)
{
    apiAllowedOrigins_ = value;
}

aap_protobuf::service::media::sink::message::VideoFrameRateType Configuration::getVideoFPS() const
{
    return videoFPS_;
}

void Configuration::setVideoFPS(aap_protobuf::service::media::sink::message::VideoFrameRateType value)
{
    videoFPS_ = value;
}

aap_protobuf::service::media::sink::message::VideoCodecResolutionType Configuration::getVideoResolution() const
{
    return videoResolution_;
}

void Configuration::setVideoResolution(aap_protobuf::service::media::sink::message::VideoCodecResolutionType value)
{
    videoResolution_ = value;
}

size_t Configuration::getScreenDPI() const
{
    return screenDPI_;
}

void Configuration::setScreenDPI(size_t value)
{
    screenDPI_ = value;
}

void Configuration::setVideoMargins(QRect value)
{
    videoMargins_ = value;
}

QRect Configuration::getVideoMargins() const
{
    return videoMargins_;
}

bool Configuration::getTouchscreenEnabled() const
{
    return enableTouchscreen_;
}

void Configuration::setTouchscreenEnabled(bool value)
{
    enableTouchscreen_ = value;
}

BluetoothAdapterType Configuration::getBluetoothAdapterType() const
{
    return bluetoothAdapterType_;
}

void Configuration::setBluetoothAdapterType(BluetoothAdapterType value)
{
    bluetoothAdapterType_ = value;
}

std::string Configuration::getBluetoothAdapterAddress() const
{
    return bluetoothAdapterAddress_;
}

void Configuration::setBluetoothAdapterAddress(const std::string& value)
{
    bluetoothAdapterAddress_ = value;
}

bool Configuration::getWirelessProjectionEnabled() const
{
    return wirelessProjectionEnabled_;
}

void Configuration::setWirelessProjectionEnabled(bool value)
{
    wirelessProjectionEnabled_ = value;
}

WirelessSettings Configuration::getWirelessSettings() const
{
    return wireless_;
}

void Configuration::setWirelessSettings(const WirelessSettings& value)
{
    wireless_ = value;
}

CarAudioSettings Configuration::getCarAudioSettings() const
{
    return carAudio_;
}

void Configuration::setCarAudioSettings(const CarAudioSettings& value)
{
    carAudio_ = value;
}

bool Configuration::musicAudioChannelEnabled() const
{
    return audioChannelEnabledMedia_;
}

void Configuration::setMusicAudioChannelEnabled(bool value)
{
    audioChannelEnabledMedia_ = value;
}

bool Configuration::guidanceAudioChannelEnabled() const
{
    return audioChannelEnabledGuidance_;
}

void Configuration::setGuidanceAudioChannelEnabled(bool value)
{
    audioChannelEnabledGuidance_ = value;
}

bool Configuration::systemAudioChannelEnabled() const
{
    return audioChannelEnabledSystem_;
}

void Configuration::setSystemAudioChannelEnabled(bool value)
{
    audioChannelEnabledSystem_ = value;
}

bool Configuration::telephonyAudioChannelEnabled() const
{
    return audioChannelEnabledTelephony_;
}

void Configuration::setTelephonyAudioChannelEnabled(bool value)
{
    audioChannelEnabledTelephony_ = value;
}

bool Configuration::radioServiceEnabled() const
{
    return radioServiceEnabled_;
}

void Configuration::setRadioServiceEnabled(bool value)
{
    radioServiceEnabled_ = value;
}

int Configuration::duckLevel() const
{
    return duckLevel_;
}

void Configuration::setDuckLevel(int value)
{
    duckLevel_ = std::clamp(value, 0, 100);
}

QString Configuration::getParamFromFile(QString fileName, QString searchString) const
{
    std::ifstream file(fileName.toStdString());
    const std::string key = searchString.toStdString() + "=";
    std::string line;
    while(std::getline(file, line))
    {
        if(!line.empty() && line[0] != '#' && line.compare(0, key.size(), key) == 0)
        {
            return QString::fromStdString(line.substr(key.size())).remove('"').trimmed();
        }
    }
    return QString();
}

}
