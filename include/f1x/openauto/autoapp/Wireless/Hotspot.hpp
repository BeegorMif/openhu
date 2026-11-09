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

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QVariantMap>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>

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
// The profile is added over D-Bus (AddConnection) with only the properties OpenHU sets: nmcli also sends
// properties its own version knows, which an older NetworkManager rejects (nmcli 1.52 in the Docker image
// against NetworkManager 1.46 on the host: "802-11-wireless.mac-address-denylist: unknown property").
class Hotspot: public QObject
{
    Q_OBJECT

public:
    enum class State { Off, Starting, On, Error };
    // NetworkManager connection settings, D-Bus type a{sa{sv}}
    using ProfileSettings = QMap<QString, QVariantMap>;

    // IP of OpenHU in the hotspot network (without prefix)
    static QString addressOf(const configuration::WirelessSettings& settings);
    // the hotspot profile; band "a" or "bg"
    static ProfileSettings profileSettings(const configuration::WirelessSettings& settings, const QString& ssid,
                                           const QString& uuid, const QString& interface, const QString& band,
                                           int channel);

    explicit Hotspot(QObject* parent = nullptr);
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
    // AddConnection (first try) or Update (2.4 GHz fallback) over D-Bus, then up
    void writeProfile(const QString& band, int channel);
    void runUp();
    void killProcess();
    void fail(const QString& code, const QString& detail);

    QProcess process_;
    Step step_ = Step::Prepare;
    // stale D-Bus replies (after stop/start) are ignored
    int generation_ = 0;
    bool killing_ = false;
    configuration::WirelessSettings settings_;
    QString ssid_;
    QString uuid_;
    // D-Bus object of the added profile
    QString profilePath_;
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
