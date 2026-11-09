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

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QProcessEnvironment>
#include <QUuid>
#include <f1x/openauto/autoapp/Core/ChildProcesses.hpp>
#include <f1x/openauto/autoapp/Wireless/Hotspot.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace
{

const char* cNmService = "org.freedesktop.NetworkManager";
const char* cNmSettingsPath = "/org/freedesktop/NetworkManager/Settings";
const char* cNmSettingsInterface = "org.freedesktop.NetworkManager.Settings";
const char* cNmConnectionInterface = "org.freedesktop.NetworkManager.Settings.Connection";

// Values come in through the environment, so they need no quoting.
const char* cPrepareScript = R"SH(
fail() { echo "ERROR:$1"; [ -n "${2:-}" ] && echo "DETAIL:$2"; exit 1; }
command -v nmcli >/dev/null || fail nmcli
[ "$(nmcli radio wifi 2>/dev/null)" = "enabled" ] || fail radio
[ -n "$HOTSPOT_COUNTRY" ] && iw reg set "$HOTSPOT_COUNTRY" 2>/dev/null
IF="$HOTSPOT_INTERFACE"
[ -n "$IF" ] || IF=$(nmcli -t -f DEVICE,TYPE device 2>/dev/null | awk -F: '$2=="wifi"{print $1; exit}')
[ -n "$IF" ] || fail device
nmcli connection delete openhu-hotspot >/dev/null 2>&1
echo "INTERFACE:$IF"
)SH";

const char* cUpScript = R"SH(
fail() { echo "ERROR:$1"; [ -n "${2:-}" ] && echo "DETAIL:$2"; exit 1; }
out=$(nmcli --wait 30 connection up uuid "$HOTSPOT_UUID" 2>&1) || fail up "$out"
IF="$HOTSPOT_INTERFACE"
echo "BSSID:$(cat /sys/class/net/$IF/address)"
echo "CHANNEL:$(iw dev "$IF" info 2>/dev/null | awk '/channel/{print $2; exit}')"
)SH";

void registerDBusTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<QVariantMap>();
        qDBusRegisterMetaType<QList<QVariantMap>>();
        qDBusRegisterMetaType<Hotspot::ProfileSettings>();
        return true;
    }();
    Q_UNUSED(registered);
}

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

Hotspot::ProfileSettings Hotspot::profileSettings(const configuration::WirelessSettings& settings, const QString& ssid,
                                                  const QString& uuid, const QString& interface, const QString& band,
                                                  int channel)
{
    ProfileSettings profile;
    profile["connection"] = {
        {"id", QStringLiteral("openhu-hotspot")},
        {"uuid", uuid},
        {"type", QStringLiteral("802-11-wireless")},
        {"interface-name", interface},
        {"autoconnect", false},
    };
    profile["802-11-wireless"] = {
        {"ssid", ssid.toUtf8()},
        {"mode", QStringLiteral("ap")},
        {"band", band},
        {"channel", quint32(channel)},
    };
    profile["802-11-wireless-security"] = {
        {"key-mgmt", QStringLiteral("wpa-psk")},
        {"psk", QString::fromStdString(settings.password)},
        {"proto", QStringList{"rsn"}},
        {"pairwise", QStringList{"ccmp"}},
        {"group", QStringList{"ccmp"}},
    };
    QVariantMap ipv4{{"method", QStringLiteral("shared")}};
    // validated by the settings: "address/prefix"
    const QString address = QString::fromStdString(settings.address);
    if(!address.isEmpty())
    {
        bool prefixOk = false;
        const uint prefix = address.section('/', 1).toUInt(&prefixOk);
        ipv4["address-data"] = QVariant::fromValue(QList<QVariantMap>{
            {{"address", address.section('/', 0, 0)}, {"prefix", quint32(prefixOk ? prefix : 24)}}});
    }
    if(!settings.dhcpRange.empty())
    {
        ipv4["shared-dhcp-range"] = QString::fromStdString(settings.dhcpRange).replace('-', ',');
    }
    profile["ipv4"] = ipv4;
    profile["ipv6"] = QVariantMap{{"method", QStringLiteral("disabled")}};
    return profile;
}

void Hotspot::start(const configuration::WirelessSettings& settings, const QString& ssid)
{
    this->killProcess();
    ++generation_;

    const QString band = QString::fromStdString(settings.band);
    settings_ = settings;
    ssid_ = ssid;
    uuid_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profilePath_.clear();
    bandSetting_ = band == "auto" ? "auto" : band != "2.4" ? "a" : "bg";
    address_ = Hotspot::addressOf(settings);
    interface_.clear();
    bssid_.clear();
    channel_ = 0;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    environment.insert("HOTSPOT_COUNTRY", QString::fromStdString(settings.country).toUpper());
    environment.insert("HOTSPOT_INTERFACE", QString::fromStdString(settings.interface));
    process_.setProcessEnvironment(environment);
    process_.setProcessChannelMode(QProcess::MergedChannels);

    state_ = State::Starting;
    error_.clear();
    errorCode_.clear();
    emit changed();

    OPENAUTO_LOG(info) << "[Hotspot] starting \"" << ssid.toStdString() << "\" (" << band.toStdString() << " GHz)";
    step_ = Step::Prepare;
    core::startChild(process_, "sh", {"-c", cPrepareScript});
}

void Hotspot::killProcess()
{
    if(process_.state() != QProcess::NotRunning)
    {
        // onFinished ignores the run it ends
        killing_ = true;
        process_.kill();
        process_.waitForFinished(1000);
        killing_ = false;
    }
}

void Hotspot::writeProfile(const QString& band, int channel)
{
    registerDBusTypes();
    band_ = band;
    const ProfileSettings profile = Hotspot::profileSettings(settings_, ssid_, uuid_, interface_, band, channel);
    const bool update = !profilePath_.isEmpty();
    QDBusMessage message = update
        ? QDBusMessage::createMethodCall(cNmService, profilePath_, cNmConnectionInterface, "Update")
        : QDBusMessage::createMethodCall(cNmService, cNmSettingsPath, cNmSettingsInterface, "AddConnection");
    message << QVariant::fromValue(profile);

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 10000), this);
    const int generation = generation_;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation, update]() {
        watcher->deleteLater();
        if(generation != generation_ || state_ != State::Starting)
        {
            return;
        }
        if(watcher->isError())
        {
            this->fail("add", watcher->error().name() + ": " + watcher->error().message());
            return;
        }
        if(!update)
        {
            profilePath_ = QDBusPendingReply<QDBusObjectPath>(*watcher).value().path();
        }
        this->runUp();
    });
}

void Hotspot::runUp()
{
    QProcessEnvironment environment = process_.processEnvironment();
    environment.insert("HOTSPOT_UUID", uuid_);
    environment.insert("HOTSPOT_INTERFACE", interface_);
    process_.setProcessEnvironment(environment);
    step_ = Step::Up;
    core::startChild(process_, "sh", {"-c", cUpScript});
}

void Hotspot::onFinished(int exitCode)
{
    const QStringList lines = QString::fromLocal8Bit(process_.readAll()).split('\n', Qt::SkipEmptyParts);
    QString code;
    QString detail;
    for(const QString& line: lines)
    {
        if(line.startsWith("INTERFACE:")) interface_ = line.mid(10);
        else if(line.startsWith("BSSID:")) bssid_ = line.mid(6).trimmed().toUpper();
        else if(line.startsWith("ERROR:")) code = line.mid(6);
        else if(line.startsWith("CHANNEL:")) channel_ = line.mid(8).trimmed().toInt();
        else if(line.startsWith("DETAIL:")) detail = line.mid(7).trimmed();
        else if(!detail.isEmpty()) detail += " " + line.trimmed();
    }
    if(killing_ || state_ != State::Starting)
    {
        return;
    }

    if(step_ == Step::Prepare)
    {
        if(exitCode == 0 && !interface_.isEmpty())
        {
            const int channel = settings_.channel > 0 ? settings_.channel : bandSetting_ == "bg" ? 6 : 36;
            // default 36: 5 GHz without radar detection (DFS), allowed for access points in most countries
            this->writeProfile(bandSetting_ == "auto" ? "a" : bandSetting_, bandSetting_ == "auto" ? 36 : channel);
        }
        else
        {
            this->fail(code, detail);
        }
        return;
    }

    if(exitCode == 0 && !bssid_.isEmpty())
    {
        state_ = State::On;
        OPENAUTO_LOG(info) << "[Hotspot] running on " << interface_.toStdString() << " (" << bssid_.toStdString()
                           << ", channel " << channel_ << ")";
        emit changed();
    }
    else if(bandSetting_ == "auto" && band_ == "a")
    {
        OPENAUTO_LOG(info) << "[Hotspot] 5 GHz failed, using 2.4 GHz: " << detail.toStdString();
        this->writeProfile("bg", 6);
    }
    else
    {
        this->fail(code.isEmpty() ? QString("up") : code, detail);
    }
}

void Hotspot::fail(const QString& code, const QString& detail)
{
    state_ = State::Error;
    errorCode_ = code;
    if(errorCode_ == "nmcli") error_ = qtTrId("wireless.hotspot.networkmanager_nmcli_missing");
    else if(errorCode_ == "radio") error_ = qtTrId("wireless.hotspot.wifi_switched_off");
    else if(errorCode_ == "device") error_ = qtTrId("wireless.hotspot.no_wifi_device_found");
    else if(errorCode_ == "up") error_ = qtTrId("wireless.hotspot.hotspot_does_not_start");
    else error_ = qtTrId("wireless.hotspot.hotspot_could_not_set");
    OPENAUTO_LOG(warning) << "[Hotspot] " << errorCode_.toStdString() << ": " << detail.toStdString();
    emit changed();
}

void Hotspot::stop()
{
    ++generation_;
    this->killProcess();
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
