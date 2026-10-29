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

#include <memory>
#include <string>
#include <QRect>
#include <QString>
#include <aap_protobuf/service/media/sink/message/VideoFrameRateType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <f1x/openauto/autoapp/Configuration/BluetoothAdapterType.hpp>
#include <f1x/openauto/autoapp/Configuration/HandednessOfTrafficType.hpp>

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
    // known WiFi networks (wireless::WifiClient): joined while the hotspot does not need the card
    bool client;
    std::string clientInterface;  // empty: any free WiFi device
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
    virtual void setHideStatusBar(bool value) = 0;
    virtual bool hideStatusBar() const = 0;
    // on-screen keyboard for text fields (touchscreen only head units); off with a real keyboard
    virtual void setOnScreenKeyboard(bool value) = 0;
    virtual bool onScreenKeyboard() const = 0;
    // Night mode of Android Auto: "auto" (from plugins/API, daynight.set), "day" or "night"
    virtual std::string getNightMode() const = 0;
    virtual void setNightMode(const std::string& value) = 0;
    // Language of the shell ("de", "en"); empty: from the system locale (shell::Translations)
    virtual std::string getLanguage() const = 0;
    virtual void setLanguage(const std::string& value) = 0;
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
    // [Debug] Enabled: shows the debug settings (#120)
    virtual bool getDebugEnabled() const = 0;
    virtual void setDebugEnabled(bool value) = 0;
    // [Video] Codec: auto, h264 or h265 (#116); a debug setting
    virtual std::string getVideoCodec() const = 0;
    virtual void setVideoCodec(const std::string& value) = 0;
    // Token for API clients from the network and browsers; generated at the first start (openhu#23)
    virtual std::string getApiToken() const = 0;
    virtual void setApiToken(const std::string& value) = 0;
    // Origins browsers may connect from: "*" (default) or a comma separated list ("http://192.168.1.10:8080")
    virtual std::string getApiAllowedOrigins() const = 0;
    virtual void setApiAllowedOrigins(const std::string& value) = 0;

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

    // Radio plugin as a media source in Android Auto (radio channel, openhu#75)
    virtual bool radioServiceEnabled() const = 0;
    virtual void setRadioServiceEnabled(bool value) = 0;

    // Loudness of local sources (radio) during Android Auto navigation prompts in %, 0-100 (openhu#93)
    virtual int duckLevel() const = 0;
    virtual void setDuckLevel(int value) = 0;

    // "key=value" from a system file (wireless Android Auto reads the hotspot SSID/password from hostapd.conf)
    virtual QString getParamFromFile(QString fileName, QString searchString) const = 0;
};

}
}
}
}
