/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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
    std::string getNameSuffix() const override;
    void setNameSuffix(const std::string& value) override;
    std::string getVehicleId() const override;

    uint16_t getApiPort() const override;
    void setApiPort(uint16_t value) override;
    std::string getApiListenAddress() const override;
    void setApiListenAddress(const std::string& value) override;
    bool getApiAllowHotspot() const override;
    void setApiAllowHotspot(bool value) override;

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
    AudioOutputBackendType getAudioOutputBackendType() const override;
    void setAudioOutputBackendType(AudioOutputBackendType value) override;

    QString getParamFromFile(QString fileName, QString searchString) const override;

private:
    HandednessOfTrafficType handednessOfTrafficType_;
    bool showCursor_;
    std::string nameSuffix_;
    bool videoResolutionAuto_;
    std::string videoDecoder_;
    uint16_t apiPort_;
    std::string apiListenAddress_;
    bool apiAllowHotspot_;
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
    AudioOutputBackendType audioOutputBackendType_;
};

}
}
}
}
