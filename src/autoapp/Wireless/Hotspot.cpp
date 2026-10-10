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

#include <QProcessEnvironment>
#include <f1x/openauto/autoapp/Wireless/Hotspot.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace
{

// Values come in through the environment, so SSID and password need no quoting.
const char* cStartScript = R"SH(
fail() { echo "ERROR:$1"; [ -n "${2:-}" ] && echo "DETAIL:$2"; exit 1; }
command -v nmcli >/dev/null || fail nmcli
[ "$(nmcli radio wifi 2>/dev/null)" = "enabled" ] || fail radio
[ -n "$HOTSPOT_COUNTRY" ] && iw reg set "$HOTSPOT_COUNTRY" 2>/dev/null
IF="$HOTSPOT_INTERFACE"
[ -n "$IF" ] || IF=$(nmcli -t -f DEVICE,TYPE device 2>/dev/null | awk -F: '$2=="wifi"{print $1; exit}')
[ -n "$IF" ] || fail device
nmcli connection delete openhu-hotspot >/dev/null 2>&1
out=$(nmcli connection add type wifi ifname "$IF" con-name openhu-hotspot autoconnect no ssid "$HOTSPOT_SSID" \
    802-11-wireless.mode ap 802-11-wireless.band "$HOTSPOT_BAND" 802-11-wireless.channel "$HOTSPOT_CHANNEL" \
    ipv4.method shared ipv4.addresses 10.42.0.1/24 ipv6.method disabled \
    wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$HOTSPOT_PASSWORD" \
    wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp 2>&1) || fail add "$out"
out=$(nmcli --wait 30 connection up openhu-hotspot 2>&1) || fail up "$out"
echo "INTERFACE:$IF"
echo "BSSID:$(cat /sys/class/net/$IF/address)"
)SH";

}

const QString Hotspot::cAddress = QStringLiteral("10.42.0.1");

Hotspot::Hotspot(QObject* parent)
    : QObject(parent)
{
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) { this->onFinished(exitCode); });
}

Hotspot::~Hotspot()
{
    this->stop();
}

void Hotspot::start(const configuration::WirelessSettings& settings, const QString& ssid)
{
    if(process_.state() != QProcess::NotRunning)
    {
        process_.kill();
        process_.waitForFinished(1000);
    }

    const bool band5 = settings.band != "2.4";
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    environment.insert("HOTSPOT_COUNTRY", QString::fromStdString(settings.country).toUpper());
    environment.insert("HOTSPOT_INTERFACE", QString::fromStdString(settings.interface));
    environment.insert("HOTSPOT_SSID", ssid);
    environment.insert("HOTSPOT_PASSWORD", QString::fromStdString(settings.password));
    environment.insert("HOTSPOT_BAND", band5 ? "a" : "bg");
    // 36: 5 GHz without radar detection (DFS), allowed for access points in most countries
    environment.insert("HOTSPOT_CHANNEL", band5 ? "36" : "6");
    process_.setProcessEnvironment(environment);
    process_.setProcessChannelMode(QProcess::MergedChannels);

    state_ = State::Starting;
    error_.clear();
    errorCode_.clear();
    emit changed();

    OPENAUTO_LOG(info) << "[Hotspot] starting \"" << ssid.toStdString() << "\" (" << (band5 ? "5" : "2.4") << " GHz)";
    process_.start("sh", {"-c", cStartScript});
}

void Hotspot::onFinished(int exitCode)
{
    const QStringList lines = QString::fromLocal8Bit(process_.readAll()).split('\n', Qt::SkipEmptyParts);
    QString detail;
    for(const QString& line: lines)
    {
        if(line.startsWith("INTERFACE:")) interface_ = line.mid(10);
        else if(line.startsWith("BSSID:")) bssid_ = line.mid(6).trimmed().toUpper();
        else if(line.startsWith("ERROR:")) errorCode_ = line.mid(6);
        else if(line.startsWith("DETAIL:")) detail = line.mid(7).trimmed();
        else if(!detail.isEmpty()) detail += " " + line.trimmed();
    }

    if(exitCode == 0 && !bssid_.isEmpty())
    {
        state_ = State::On;
        OPENAUTO_LOG(info) << "[Hotspot] running on " << interface_.toStdString() << " (" << bssid_.toStdString() << ")";
    }
    else
    {
        state_ = State::Error;
        if(errorCode_ == "nmcli") error_ = "NetworkManager (nmcli) fehlt.";
        else if(errorCode_ == "radio") error_ = "WLAN ist ausgeschaltet.";
        else if(errorCode_ == "device") error_ = "Kein WLAN-Gerät gefunden.";
        else if(errorCode_ == "up") error_ = "Hotspot startet nicht. Kann die WLAN-Karte Access-Point auf diesem Band? Ist das Land richtig?";
        else error_ = "Hotspot konnte nicht eingerichtet werden.";
        OPENAUTO_LOG(warning) << "[Hotspot] " << errorCode_.toStdString() << ": " << detail.toStdString();
    }
    emit changed();
}

void Hotspot::stop()
{
    if(process_.state() != QProcess::NotRunning)
    {
        process_.kill();
        process_.waitForFinished(1000);
    }
    if(state_ == State::On || state_ == State::Starting)
    {
        QProcess::execute("nmcli", {"connection", "down", "openhu-hotspot"});
        OPENAUTO_LOG(info) << "[Hotspot] stopped";
    }
    if(state_ != State::Off)
    {
        state_ = State::Off;
        error_.clear();
        errorCode_.clear();
        emit changed();
    }
}

void Hotspot::enableRadio()
{
    QProcess::execute("nmcli", {"radio", "wifi", "on"});
}

Hotspot::State Hotspot::state() const
{
    return state_;
}

QString Hotspot::stateName() const
{
    switch(state_)
    {
    case State::Starting: return "starting";
    case State::On: return "on";
    case State::Error: return "error";
    default: return "off";
    }
}

QString Hotspot::error() const
{
    return error_;
}

bool Hotspot::radioOff() const
{
    return state_ == State::Error && errorCode_ == "radio";
}

QString Hotspot::interface() const
{
    return interface_;
}

QString Hotspot::bssid() const
{
    return bssid_;
}

}
