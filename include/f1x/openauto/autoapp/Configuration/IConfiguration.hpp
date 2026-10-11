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

#include <memory>
#include <string>
#include <QRect>
#include <QString>
#include <aap_protobuf/service/media/sink/message/VideoFrameRateType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <f1x/openauto/autoapp/Configuration/BluetoothAdapterType.hpp>
#include <f1x/openauto/autoapp/Configuration/HandednessOfTrafficType.hpp>
#include <f1x/openauto/autoapp/Configuration/AudioOutputBackendType.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace configuration
{

// Wireless Android Auto: WiFi hotspot (NetworkManager) the phone joins after the Bluetooth handshake
struct WirelessSettings
{
    std::string ssid;       // empty: "OpenHU" + name suffix
    std::string password;   // generated on first start
    std::string band;       // "5", "2.4" (GHz) or "auto" (5, 2.4 when 5 GHz fails)
    std::string country;    // regulatory domain, e.g. "DE" (5 GHz needs it)
    std::string interface;  // empty: first WiFi device
    std::string lastDevice; // Bluetooth address of the last phone, connected first
    std::string priority;   // phone on the cable and wireless: "usb" (default) or "wifi" wins
    std::string hotspot;    // "always" (default) or "demand": started when a phone asks for it
    // advanced
    int channel;            // 0: 36 (5 GHz) / 6 (2.4 GHz)
    std::string address;    // OpenHU in the hotspot (gateway for the phone), "10.42.0.1/24"
    std::string dhcpRange;  // "10.42.0.10-10.42.0.254", empty: NetworkManager default
    // (no DNS setting: NetworkManager's shared mode always hands out itself as DNS forwarder)
};

// Sound to a Bluetooth car radio (A2DP), its buttons control OpenHU (AVRCP)
struct CarAudioSettings
{
    bool enabled;
    std::string device;     // Bluetooth address of the radio
    std::string pin;        // legacy pairing of old radios, "0000"
};

// Settings in openhu.ini (edited on the shell's settings page).
class IConfiguration
{
public:
    typedef std::shared_ptr<IConfiguration> Pointer;

    virtual ~IConfiguration() = default;

    virtual void load() = 0;
    virtual void reset() = 0;
    virtual void save() = 0;

    // General
    virtual void setHandednessOfTrafficType(HandednessOfTrafficType value) = 0;
    virtual HandednessOfTrafficType getHandednessOfTrafficType() const = 0;
    virtual void showCursor(bool value) = 0;
    virtual bool showCursor() const = 0;
    // Shown on the phone as "OpenHU <suffix>", to tell several head units apart
    virtual std::string getNameSuffix() const = 0;
    virtual void setNameSuffix(const std::string& value) = 0;
    // Derived from the name suffix: the phone keeps its settings per vehicle id, so differently named
    // head units are separate cars and the same name stays the same car (also after reinstalling)
    virtual std::string getVehicleId() const = 0;

    // WebSocket API (docs/api.md)
    virtual uint16_t getApiPort() const = 0;
    virtual void setApiPort(uint16_t value) = 0;
    virtual std::string getApiListenAddress() const = 0;
    virtual void setApiListenAddress(const std::string& value) = 0;
    // API clients on the wireless hotspot (10.42.0.0/24); API devices normally use Ethernet
    virtual bool getApiAllowHotspot() const = 0;
    virtual void setApiAllowHotspot(bool value) = 0;

    // Android Auto video and input
    virtual aap_protobuf::service::media::sink::message::VideoFrameRateType getVideoFPS() const = 0;
    virtual void setVideoFPS(aap_protobuf::service::media::sink::message::VideoFrameRateType value) = 0;
    virtual aap_protobuf::service::media::sink::message::VideoCodecResolutionType getVideoResolution() const = 0;
    virtual void setVideoResolution(aap_protobuf::service::media::sink::message::VideoCodecResolutionType value) = 0;
    // true: the shell picks the resolution matching its Android Auto area (getVideoResolution() is ignored)
    virtual bool getVideoResolutionAuto() const = 0;
    virtual void setVideoResolutionAuto(bool value) = 0;
    // H.264 decoder: "auto" (hardware where available, else software), "software" or an FFmpeg decoder name
    virtual std::string getVideoDecoder() const = 0;
    virtual void setVideoDecoder(const std::string& value) = 0;
    virtual size_t getScreenDPI() const = 0;
    virtual void setScreenDPI(size_t value) = 0;
    // 0x0 = margins computed by the shell
    virtual void setVideoMargins(QRect value) = 0;
    virtual QRect getVideoMargins() const = 0;
    virtual bool getTouchscreenEnabled() const = 0;
    virtual void setTouchscreenEnabled(bool value) = 0;

    // Bluetooth and wireless Android Auto
    virtual BluetoothAdapterType getBluetoothAdapterType() const = 0;
    virtual void setBluetoothAdapterType(BluetoothAdapterType value) = 0;
    virtual std::string getBluetoothAdapterAddress() const = 0;
    virtual void setBluetoothAdapterAddress(const std::string& value) = 0;
    virtual bool getWirelessProjectionEnabled() const = 0;
    virtual void setWirelessProjectionEnabled(bool value) = 0;
    virtual WirelessSettings getWirelessSettings() const = 0;
    virtual void setWirelessSettings(const WirelessSettings& value) = 0;
    virtual CarAudioSettings getCarAudioSettings() const = 0;
    virtual void setCarAudioSettings(const CarAudioSettings& value) = 0;

    // Android Auto audio channels and output
    virtual bool musicAudioChannelEnabled() const = 0;
    virtual void setMusicAudioChannelEnabled(bool value) = 0;
    virtual bool guidanceAudioChannelEnabled() const = 0;
    virtual void setGuidanceAudioChannelEnabled(bool value) = 0;
    virtual bool systemAudioChannelEnabled() const = 0;
    virtual void setSystemAudioChannelEnabled(bool value) = 0;
    virtual bool telephonyAudioChannelEnabled() const = 0;
    virtual void setTelephonyAudioChannelEnabled(bool value) = 0;
    virtual AudioOutputBackendType getAudioOutputBackendType() const = 0;
    virtual void setAudioOutputBackendType(AudioOutputBackendType value) = 0;

    // "key=value" from a system file (wireless Android Auto reads the hotspot SSID/password from hostapd.conf)
    virtual QString getParamFromFile(QString fileName, QString searchString) const = 0;
};

}
}
}
}
