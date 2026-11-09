/*
*  This file is part of OpenHU.
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

#include <QObject>
#include <QProcess>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Wireless/NmSettings.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace wireless
{

// WiFi access point for wireless Android Auto, set up through NetworkManager (connection "openhu-hotspot",
// IPv4 shared = DHCP for the phone). Needs nmcli and a WiFi card with AP mode.
// The profile is written over D-Bus (NmSettings), nmcli does the rest.
class Hotspot: public QObject
{
    Q_OBJECT

public:
    enum class State { Off, Starting, On, Error };

    // IP of OpenHU in the hotspot network (without prefix)
    static QString addressOf(const configuration::WirelessSettings& settings);
    // the hotspot profile; band "a" or "bg"
    static NmSettings::Profile profileSettings(const configuration::WirelessSettings& settings, const QString& ssid,
                                           const QString& uuid, const QString& interface, const QString& band,
                                               int channel);

    // nmSettings: NetworkManager over D-Bus (own one when null)
    explicit Hotspot(QObject* parent = nullptr, NmSettings* nmSettings = nullptr);
    ~Hotspot() override;

    void start(const configuration::WirelessSettings& settings, const QString& ssid);
    void stop();
    // "nmcli radio wifi on"
    void enableRadio();

    State state() const;
    QString stateName() const;
    // German message for the settings page, empty without error
    QString error() const;
    // the WiFi card is switched off (error() tells the user to turn it on)
    bool radioOff() const;
    QString interface() const;
    QString bssid() const;
    QString address() const;
    // 0 while not running
    int channel() const;

signals:
    void changed();

private:
    // prepare: checks, finds the WiFi device, removes the old profile; up: activates the profile
    enum class Step { Prepare, Up };

    void onFinished(int exitCode);
    // added (first try) or replaced (2.4 GHz fallback), then up
    void writeProfile(const QString& band, int channel);
    void runUp();
    void killProcess();
    void fail(const QString& code, const QString& detail);

    NmSettings* nmSettings_;
    QProcess process_;
    Step step_ = Step::Prepare;
    // stale D-Bus replies (after stop/start) are ignored
    int generation_ = 0;
    bool killing_ = false;
    configuration::WirelessSettings settings_;
    QString ssid_;
    QString uuid_;
    bool profileAdded_ = false;
    // "auto", "a" or "bg"
    QString bandSetting_;
    QString band_;
    State state_ = State::Off;
    QString error_;
    QString errorCode_;
    QString interface_;
    QString bssid_;
    QString address_;
    int channel_ = 0;
};

}
}
}
}
