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

#include <functional>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace f1x::openauto::autoapp::wireless
{

// Known WiFi networks (QML: "wifi"): OpenHU goes online through them (home WiFi, a workshop, a phone's tethering)
// for updates, remote management and plugins. They are NetworkManager profiles named "openhu-wifi-<ssid>"; only these
// are listed and changed (in the container NetworkManager is the host's).
// A new network is joined once by hand (connectTo); NetworkManager joins it by itself afterwards (autoconnect), so
// without a network that worked once nothing is tried. Switched off (configure), no profile is joined.
// Whether the hotspot has to make room on the same card is up to WirelessController.
// Publishes "wifi.state".
class WifiClient: public QObject
{
    Q_OBJECT
    // available (NetworkManager with a WiFi card), enabled, devices (WiFi cards), client {uuid, ssid, ip, metered,
    // device} while connected, internet, networks [{uuid, ssid, priority, secured, hidden, active, joined}] (highest
    // priority first; joined: connected at least once), found [{ssid, signal, security, secured, known}] after a
    // search, scanning, busy, message (what happened last), hotspotDevice (the card of OpenHU's hotspot)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)

public:
    static constexpr const char* cPrefix = "openhu-wifi-";

    explicit WifiClient(const QString& nmcli = QStringLiteral("nmcli"), QObject* parent = nullptr);
    ~WifiClient() override;

    QVariantMap state() const;
    bool enabled() const { return enabled_; }
    QStringList devices() const { return devices_; }
    bool connected() const { return !client_.isEmpty(); }
    QString hotspotDevice() const { return hotspotDevice_; }

    // Wireless.Client and Wireless.ClientInterface (empty: any free card); the profiles follow
    void configure(bool enabled, const QString& interface);

    Q_INVOKABLE void refresh();
    // networks in range (the card must not run the hotspot)
    void scan();
    // a WPA password (8 to 63 characters) or empty for an open network; an existing one with this name is
    // replaced. added gets the new profile (to join it). Empty when accepted.
    QString add(const QString& ssid, const QString& password, bool hidden, std::function<void(const QString&)> added);
    // joins a saved network now; a wrong password of a network that never worked removes it again
    void connectTo(const QString& uuid, std::function<void(bool)> done = {});
    Q_INVOKABLE void remove(const QString& uuid);
    // one place higher in the order NetworkManager tries them
    Q_INVOKABLE void moveUp(const QString& uuid);

    // "a\:b" -> "a:b": nmcli's terse output escapes ':' and '\'
    static QString unescape(const QString& value);
    // splits one line of nmcli's terse output at the unescaped ':'
    static QStringList splitTerse(const QString& line);

signals:
    void changed();
    // joined or left a WiFi network
    void clientChanged(bool connected);
    // shown as a toast
    void error(const QString& message);

private:
    // exit code (-1 when it did not run) and the output
    void run(const QString& program, const QStringList& args, std::function<void(int, const QString&)> done);
    void nmcli(const QStringList& args, std::function<void(int, const QString&)> done);
    // sh -c script with nmcli as $1
    void script(const char* script, const QStringList& args, std::function<void(int, const QString&)> done);
    void parseState(const QString& output);
    void parseScan(const QString& output);
    void setPriorities(const QStringList& uuids);
    // autoconnect and card of the profiles as configured; false when they are right already
    bool syncProfiles();
    void startMonitor();
    void publish();

    QString nmcli_;
    bool enabled_ = true;
    QString interface_;
    QVariantMap client_;
    QVariantList networks_;
    QVariantList found_;
    QStringList devices_;
    QString hotspotDevice_;
    // nmcli there at all; available_: and a WiFi card
    bool nmcliFound_ = false;
    bool available_ = false;
    bool internet_ = false;
    bool scanning_ = false;
    bool busy_ = false;
    bool refreshing_ = false;
    bool refreshAgain_ = false;
    QString message_;
    // the last changes syncProfiles made: not again when NetworkManager did not take them (no endless loop)
    QStringList lastSync_;
    // names of the profiles added in this run (connectTo before a refresh lists them)
    QMap<QString, QString> addedNames_;
    QProcess monitor_;
    QTimer refreshTimer_;
    QTimer pollTimer_;
};

}
