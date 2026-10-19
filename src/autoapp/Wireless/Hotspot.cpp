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

#include <QProcessEnvironment>
#include <f1x/openauto/autoapp/Core/ChildProcesses.hpp>
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
# optional values are validated by OpenHU (no spaces), so plain word splitting is fine
EXTRA=""
[ -n "$HOTSPOT_DHCP_RANGE" ] && EXTRA="$EXTRA ipv4.shared-dhcp-range $HOTSPOT_DHCP_RANGE"
# $1 band (a/bg), $2 channel
up() {
    nmcli connection delete openhu-hotspot >/dev/null 2>&1
    out=$(nmcli connection add type wifi ifname "$IF" con-name openhu-hotspot autoconnect no ssid "$HOTSPOT_SSID" \
        802-11-wireless.mode ap 802-11-wireless.band "$1" 802-11-wireless.channel "$2" \
        ipv4.method shared ipv4.addresses "$HOTSPOT_ADDRESS" ipv6.method disabled \
        wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$HOTSPOT_PASSWORD" \
        wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp $EXTRA 2>&1) || fail add "$out"
    out=$(nmcli --wait 30 connection up openhu-hotspot 2>&1)
}
if [ "$HOTSPOT_BAND" = "auto" ]; then
    up a 36 || { echo "FALLBACK:$out"; up bg 6; } || fail up "$out"
else
    up "$HOTSPOT_BAND" "$HOTSPOT_CHANNEL" || fail up "$out"
fi
echo "INTERFACE:$IF"
echo "BSSID:$(cat /sys/class/net/$IF/address)"
echo "CHANNEL:$(iw dev "$IF" info 2>/dev/null | awk '/channel/{print $2; exit}')"
)SH";

}


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

    const QString band = QString::fromStdString(settings.band);
    const bool band5 = band != "2.4";
    address_ = Hotspot::addressOf(settings);
    channel_ = 0;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    environment.insert("HOTSPOT_COUNTRY", QString::fromStdString(settings.country).toUpper());
    environment.insert("HOTSPOT_INTERFACE", QString::fromStdString(settings.interface));
    environment.insert("HOTSPOT_SSID", ssid);
    environment.insert("HOTSPOT_PASSWORD", QString::fromStdString(settings.password));
    environment.insert("HOTSPOT_BAND", band == "auto" ? "auto" : band5 ? "a" : "bg");
    // default 36: 5 GHz without radar detection (DFS), allowed for access points in most countries
    environment.insert("HOTSPOT_CHANNEL", QString::number(settings.channel > 0 ? settings.channel : band5 ? 36 : 6));
    environment.insert("HOTSPOT_ADDRESS", QString::fromStdString(settings.address));
    environment.insert("HOTSPOT_DHCP_RANGE", QString::fromStdString(settings.dhcpRange).replace('-', ','));
    process_.setProcessEnvironment(environment);
    process_.setProcessChannelMode(QProcess::MergedChannels);

    state_ = State::Starting;
    error_.clear();
    errorCode_.clear();
    emit changed();

    OPENAUTO_LOG(info) << "[Hotspot] starting \"" << ssid.toStdString() << "\" (" << band.toStdString() << " GHz)";
    core::startChild(process_, "sh", {"-c", cStartScript});
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
        else if(line.startsWith("CHANNEL:")) channel_ = line.mid(8).trimmed().toInt();
        else if(line.startsWith("FALLBACK:"))
            OPENAUTO_LOG(info) << "[Hotspot] 5 GHz failed, using 2.4 GHz: " << line.mid(9).toStdString();
        else if(line.startsWith("DETAIL:")) detail = line.mid(7).trimmed();
        else if(!detail.isEmpty()) detail += " " + line.trimmed();
    }

    if(exitCode == 0 && !bssid_.isEmpty())
    {
        state_ = State::On;
        OPENAUTO_LOG(info) << "[Hotspot] running on " << interface_.toStdString() << " (" << bssid_.toStdString()
                           << ", channel " << channel_ << ")";
    }
    else
    {
        state_ = State::Error;
        if(errorCode_ == "nmcli") error_ = qtTrId("wireless.hotspot.networkmanager_nmcli_missing");
        else if(errorCode_ == "radio") error_ = qtTrId("wireless.hotspot.wifi_switched_off");
        else if(errorCode_ == "device") error_ = qtTrId("wireless.hotspot.no_wifi_device_found");
        else if(errorCode_ == "up") error_ = qtTrId("wireless.hotspot.hotspot_does_not_start");
        else error_ = qtTrId("wireless.hotspot.hotspot_could_not_set");
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

QString Hotspot::address() const
{
    return address_;
}

int Hotspot::channel() const
{
    return channel_;
}

QString Hotspot::addressOf(const configuration::WirelessSettings& settings)
{
    const QString address = QString::fromStdString(settings.address).section('/', 0, 0);
    return address.isEmpty() ? QString("10.42.0.1") : address;
}

}
