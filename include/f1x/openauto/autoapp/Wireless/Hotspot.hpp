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

#pragma once

#include <QObject>
#include <QProcess>
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
class Hotspot: public QObject
{
    Q_OBJECT

public:
    enum class State { Off, Starting, On, Error };

    // IP of OpenHU in the hotspot network (without prefix)
    static QString addressOf(const configuration::WirelessSettings& settings);

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
    void onFinished(int exitCode);

    QProcess process_;
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
