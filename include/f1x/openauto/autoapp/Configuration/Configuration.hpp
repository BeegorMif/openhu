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

#pragma once

#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace configuration
{

class Configuration: public IConfiguration
{
public:
    Configuration();

    void load() override;
    void reset() override;
    void save() override;

    void setHandednessOfTrafficType(HandednessOfTrafficType value) override;
    HandednessOfTrafficType getHandednessOfTrafficType() const override;
    void showCursor(bool value) override;
    bool showCursor() const override;
    void setHideStatusBar(bool value) override;
    bool hideStatusBar() const override;
    void setOnScreenKeyboard(bool value) override;
    bool onScreenKeyboard() const override;
    std::string getNightMode() const override;
    void setNightMode(const std::string& value) override;
    std::string getLanguage() const override;
    void setLanguage(const std::string& value) override;
    std::string getNameSuffix() const override;
    void setNameSuffix(const std::string& value) override;
    std::string getVehicleId() const override;

    uint16_t getApiPort() const override;
    void setApiPort(uint16_t value) override;
    std::string getApiListenAddress() const override;
    void setApiListenAddress(const std::string& value) override;
    bool getApiAllowHotspot() const override;
    void setApiAllowHotspot(bool value) override;
    bool getDebugEnabled() const override;
    void setDebugEnabled(bool value) override;
    std::string getVideoCodec() const override;
    void setVideoCodec(const std::string& value) override;
    std::string getApiToken() const override;
    void setApiToken(const std::string& value) override;
    std::string getApiAllowedOrigins() const override;
    void setApiAllowedOrigins(const std::string& value) override;

    aap_protobuf::service::media::sink::message::VideoFrameRateType getVideoFPS() const override;
    void setVideoFPS(aap_protobuf::service::media::sink::message::VideoFrameRateType value) override;
    aap_protobuf::service::media::sink::message::VideoCodecResolutionType getVideoResolution() const override;
    void setVideoResolution(aap_protobuf::service::media::sink::message::VideoCodecResolutionType value) override;
    bool getVideoResolutionAuto() const override;
    void setVideoResolutionAuto(bool value) override;
    std::string getVideoDecoder() const override;
    void setVideoDecoder(const std::string& value) override;
    size_t getScreenDPI() const override;
    void setScreenDPI(size_t value) override;
    void setVideoMargins(QRect value) override;
    QRect getVideoMargins() const override;
    bool getTouchscreenEnabled() const override;
    void setTouchscreenEnabled(bool value) override;

    BluetoothAdapterType getBluetoothAdapterType() const override;
    void setBluetoothAdapterType(BluetoothAdapterType value) override;
    std::string getBluetoothAdapterAddress() const override;
    void setBluetoothAdapterAddress(const std::string& value) override;
    bool getWirelessProjectionEnabled() const override;
    void setWirelessProjectionEnabled(bool value) override;
    WirelessSettings getWirelessSettings() const override;
    void setWirelessSettings(const WirelessSettings& value) override;
    CarAudioSettings getCarAudioSettings() const override;
    void setCarAudioSettings(const CarAudioSettings& value) override;

    bool musicAudioChannelEnabled() const override;
    void setMusicAudioChannelEnabled(bool value) override;
    bool guidanceAudioChannelEnabled() const override;
    void setGuidanceAudioChannelEnabled(bool value) override;
    bool systemAudioChannelEnabled() const override;
    void setSystemAudioChannelEnabled(bool value) override;
    bool telephonyAudioChannelEnabled() const override;
    void setTelephonyAudioChannelEnabled(bool value) override;
    bool radioServiceEnabled() const override;
    void setRadioServiceEnabled(bool value) override;
    int duckLevel() const override;
    void setDuckLevel(int value) override;

    QString getParamFromFile(QString fileName, QString searchString) const override;

private:
    HandednessOfTrafficType handednessOfTrafficType_;
    bool showCursor_;
    bool hideStatusBar_;
    bool onScreenKeyboard_;
    std::string nameSuffix_;
    std::string language_;
    std::string nightMode_;
    bool videoResolutionAuto_;
    std::string videoDecoder_;
    uint16_t apiPort_;
    std::string apiListenAddress_;
    bool apiAllowHotspot_;
    bool debugEnabled_;
    std::string videoCodec_;
    std::string apiToken_;
    std::string apiAllowedOrigins_;
    aap_protobuf::service::media::sink::message::VideoFrameRateType videoFPS_;
    aap_protobuf::service::media::sink::message::VideoCodecResolutionType videoResolution_;
    size_t screenDPI_;
    QRect videoMargins_;
    bool enableTouchscreen_;
    BluetoothAdapterType bluetoothAdapterType_;
    std::string bluetoothAdapterAddress_;
    bool wirelessProjectionEnabled_;
    WirelessSettings wireless_;
    CarAudioSettings carAudio_;
    bool audioChannelEnabledMedia_;
    bool audioChannelEnabledGuidance_;
    bool audioChannelEnabledSystem_;
    bool audioChannelEnabledTelephony_;
    bool radioServiceEnabled_;
    int duckLevel_;
};

}
}
}
}
