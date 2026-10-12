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


#include <algorithm>
#include <QDir>
#include <QStandardPaths>
#include <f1x/openauto/autoapp/Wireless/WifiClient.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace
{

// $1 is nmcli. One line per value: "KEY:value", values as nmcli prints them (terse, escaped).
const char* cStateScript = R"SH(
nmcli="$1"
echo "CONNECTIVITY:$("$nmcli" -t -f CONNECTIVITY general 2>/dev/null)"
"$nmcli" -t -f DEVICE,TYPE device 2>/dev/null | while IFS=: read -r dev type; do
    [ "$type" = wifi ] && echo "DEVICE:$dev"
done
"$nmcli" -t -f UUID,TYPE,DEVICE,STATE,NAME connection show --active 2>/dev/null | while IFS=: read -r uuid type dev state name; do
    [ "$type" = 802-11-wireless ] || continue
    # OpenHU's hotspot has the card; other access points are no client either
    if [ "$("$nmcli" -g 802-11-wireless.mode connection show "$uuid" 2>/dev/null)" = ap ]; then
        [ "$name" = openhu-hotspot ] && echo "HOTSPOT:$dev"
        continue
    fi
    # a client only once it has its address
    [ "$state" = activated ] || continue
    echo "CLIENT.uuid:$uuid"
    echo "CLIENT.device:$dev"
    "$nmcli" -t -m multiline -f 802-11-wireless.ssid connection show "$uuid" 2>/dev/null | sed 's/^/CLIENT./'
    "$nmcli" -t -m multiline -f IP4.ADDRESS device show "$dev" 2>/dev/null | sed 's/^/CLIENT./'
done
"$nmcli" -t -f UUID,NAME connection show 2>/dev/null | while IFS=: read -r uuid name; do
    case "$name" in openhu-wifi-*) ;; *) continue ;; esac
    echo "NET:$uuid"
    "$nmcli" -t -m multiline -f 802-11-wireless.ssid,802-11-wireless.hidden,connection.autoconnect-priority,802-11-wireless-security.key-mgmt,connection.autoconnect,connection.interface-name,connection.timestamp connection show "$uuid" 2>/dev/null
done
)SH";

// $1 nmcli, then per profile: uuid, autoconnect (yes/no), card ("" for any), down (yes: leave it now)
const char* cSyncScript = R"SH(
nmcli="$1"; shift
while [ $# -ge 4 ]; do
    "$nmcli" connection modify "$1" connection.autoconnect "$2" connection.interface-name "$3" || exit 1
    [ "$4" = yes ] && "$nmcli" connection down "$1"
    shift 4
done
exit 0
)SH";

// $1 nmcli, then pairs of uuid and priority
const char* cPriorityScript = R"SH(
nmcli="$1"; shift
while [ $# -ge 2 ]; do
    "$nmcli" connection modify "$1" connection.autoconnect-priority "$2" || exit 1
    shift 2
done
)SH";

QString valueAfterColon(const QString& line)
{
    return WifiClient::unescape(line.section(':', 1));
}

}

WifiClient::WifiClient(const QString& nmcli, QObject* parent)
    : QObject(parent)
    , nmcli_(nmcli)
{
    nmcliFound_ = !QStandardPaths::findExecutable(nmcli_).isEmpty();

    // NetworkManager reports every change (nmcli monitor); a burst of them is one refresh
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(400);
    connect(&refreshTimer_, &QTimer::timeout, this, &WifiClient::refresh);
    // connectivity and addresses come without a monitor line now and then
    pollTimer_.setInterval(30000);
    connect(&pollTimer_, &QTimer::timeout, this, &WifiClient::refresh);

    connect(&monitor_, &QProcess::readyRead, this, [this]() {
        monitor_.readAll();
        refreshTimer_.start();
    });
    connect(&monitor_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this]() {
        QTimer::singleShot(5000, this, &WifiClient::startMonitor);  // NetworkManager restarted
    });

    pollTimer_.start();
    if(nmcliFound_)
    {
        this->startMonitor();
        this->refresh();
    }
    else
    {
        QTimer::singleShot(0, this, &WifiClient::readSysfs);
    }
}

WifiClient::~WifiClient()
{
    monitor_.disconnect(this);
    if(monitor_.state() != QProcess::NotRunning)
    {
        monitor_.kill();
        monitor_.waitForFinished(1000);
    }
}

void WifiClient::startMonitor()
{
    if(monitor_.state() == QProcess::NotRunning)
    {
        monitor_.start(nmcli_, {"monitor"});
    }
}

QVariantMap WifiClient::state() const
{
    return {{"available", available_}, {"ready", ready_}, {"enabled", enabled_}, {"devices", devices_}, {"client", client_},
            {"internet", internet_}, {"networks", networks_}, {"found", found_}, {"scanning", scanning_},
            {"busy", busy_}, {"message", message_}, {"hotspotDevice", hotspotDevice_}};
}

void WifiClient::configure(bool enabled, const QString& interface)
{
    if(enabled == enabled_ && interface == interface_)
    {
        return;
    }
    OPENAUTO_LOG(info) << "[WifiClient] " << (enabled ? "on" : "off")
                       << (interface.isEmpty() ? std::string() : ", card " + interface.toStdString());
    enabled_ = enabled;
    interface_ = interface;
    lastSync_.clear();
    if(!enabled_)
    {
        found_.clear();
        message_.clear();
    }
    if(!this->syncProfiles())
    {
        this->publish();
    }
}

QString WifiClient::unescape(const QString& value)
{
    QString result;
    result.reserve(value.size());
    for(int i = 0; i < value.size(); ++i)
    {
        if(value[i] == '\\' && i + 1 < value.size())
        {
            ++i;
        }
        result += value[i];
    }
    return result;
}

QStringList WifiClient::splitTerse(const QString& line)
{
    QStringList fields;
    QString field;
    for(int i = 0; i < line.size(); ++i)
    {
        if(line[i] == '\\' && i + 1 < line.size())
        {
            field += line[++i];
        }
        else if(line[i] == ':')
        {
            fields << field;
            field.clear();
        }
        else
        {
            field += line[i];
        }
    }
    fields << field;
    return fields;
}

void WifiClient::readSysfs()
{
    QStringList devices;
    const QDir net("/sys/class/net");
    for(const QString& name: net.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if(QDir(net.filePath(name + "/wireless")).exists())
        {
            devices << name;
        }
    }
    this->setDevices(devices);
}

void WifiClient::setDevices(const QStringList& devices)
{
    const bool changed = !ready_ || devices != devices_;
    devices_ = devices;
    available_ = !devices.isEmpty();
    ready_ = true;
    if(changed)
    {
        OPENAUTO_LOG(info) << "[WifiClient] WiFi cards: "
                           << (devices.isEmpty() ? std::string("none") : devices.join(", ").toStdString());
        this->publish();
        emit devicesChanged();
    }
}

void WifiClient::refresh()
{
    if(!nmcliFound_)
    {
        this->readSysfs();
        return;
    }
    if(refreshing_)
    {
        refreshAgain_ = true;
        return;
    }
    refreshing_ = true;
    this->script(cStateScript, {}, [this](int, const QString& output) {
        refreshing_ = false;
        this->parseState(output);
        if(refreshAgain_)
        {
            refreshAgain_ = false;
            this->refresh();
        }
    });
}

void WifiClient::parseState(const QString& output)
{
    QVariantMap client;
    QVariantList networks;
    QVariantMap network;
    QString hotspotDevice;
    QStringList devices;
    bool internet = false;
    auto finishNetwork = [&]() {
        if(!network.isEmpty())
        {
            network.insert("active", network.value("uuid") == client.value("uuid"));
            networks << network;
            network.clear();
        }
    };

    for(const QString& line: output.split('\n', Qt::SkipEmptyParts))
    {
        const QString key = line.section(':', 0, 0);
        if(key == "CONNECTIVITY") internet = line.section(':', 1) == "full";
        else if(key == "DEVICE") devices << line.section(':', 1);
        else if(key == "HOTSPOT") hotspotDevice = line.section(':', 1);
        else if(key == "CLIENT.uuid" && client.isEmpty()) client.insert("uuid", line.section(':', 1));
        else if(key == "CLIENT.device" && !client.contains("device")) client.insert("device", line.section(':', 1));
        else if(key == "CLIENT.802-11-wireless.ssid" && !client.contains("ssid")) client.insert("ssid", valueAfterColon(line));
        else if(key.startsWith("CLIENT.IP4.ADDRESS") && !client.contains("ip")) client.insert("ip", line.section(':', 1).section('/', 0, 0));
        else if(key == "NET")
        {
            finishNetwork();
            network.insert("uuid", line.section(':', 1));
        }
        else if(key == "802-11-wireless.ssid") network.insert("ssid", valueAfterColon(line));
        else if(key == "802-11-wireless.hidden") network.insert("hidden", line.section(':', 1) == "yes");
        else if(key == "connection.autoconnect-priority") network.insert("priority", line.section(':', 1).toInt());
        else if(key == "802-11-wireless-security.key-mgmt") network.insert("secured", !line.section(':', 1).isEmpty());
        else if(key == "connection.autoconnect") network.insert("autoconnect", line.section(':', 1) == "yes");
        else if(key == "connection.interface-name") network.insert("interface", valueAfterColon(line));
        // the last time it was connected: 0 never
        else if(key == "connection.timestamp") network.insert("joined", line.section(':', 1).toLongLong() > 0);
    }
    finishNetwork();
    std::stable_sort(networks.begin(), networks.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("priority").toInt() > b.toMap().value("priority").toInt();
    });

    // the known ones among the found ones
    QStringList known;
    for(const QVariant& entry: networks)
    {
        known << entry.toMap().value("ssid").toString();
    }
    for(QVariant& entry: found_)
    {
        QVariantMap found = entry.toMap();
        found.insert("known", known.contains(found.value("ssid").toString()));
        entry = found;
    }

    const bool changed = client != client_ || networks != networks_ || internet != internet_
                         || hotspotDevice != hotspotDevice_;
    const bool joinedOrLeft = client.isEmpty() != client_.isEmpty();
    if(client.value("ssid") != client_.value("ssid"))
    {
        OPENAUTO_LOG(info) << "[WifiClient] "
                           << (client.isEmpty() ? std::string("no known network")
                                                : "joined " + client.value("ssid").toString().toStdString() + " ("
                                                      + client.value("ip").toString().toStdString() + ")");
    }
    client_ = client;
    networks_ = networks;
    internet_ = internet;
    hotspotDevice_ = hotspotDevice;
    this->setDevices(devices);
    if(joinedOrLeft)
    {
        emit clientChanged(!client_.isEmpty());
    }
    if(this->syncProfiles())
    {
        return;  // refreshed and published afterwards
    }
    if(changed)
    {
        this->publish();
    }
}

bool WifiClient::syncProfiles()
{
    QStringList args;
    for(const QVariant& entry: networks_)
    {
        const QVariantMap network = entry.toMap();
        // joined by itself only when switched on and after it worked once
        const bool autoconnect = enabled_ && network.value("joined").toBool();
        const bool leave = !enabled_ && network.value("active").toBool();
        if(network.value("autoconnect").toBool() != autoconnect || network.value("interface").toString() != interface_
           || leave)
        {
            args << network.value("uuid").toString() << (autoconnect ? "yes" : "no") << interface_
                 << (leave ? "yes" : "no");
        }
    }
    if(args.isEmpty() || args == lastSync_)
    {
        return false;
    }
    lastSync_ = args;
    this->script(cSyncScript, args, [this](int exitCode, const QString& output) {
        if(exitCode != 0)
        {
            OPENAUTO_LOG(warning) << "[WifiClient] profiles: " << output.trimmed().toStdString();
        }
        this->refresh();
    });
    return true;
}

void WifiClient::scan()
{
    if(!available_ || !enabled_ || scanning_)
    {
        return;
    }
    scanning_ = true;
    this->publish();
    QStringList args{"-t", "-f", "SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "yes"};
    if(!interface_.isEmpty())
    {
        args << "ifname" << interface_;
    }
    this->nmcli(args,
              [this](int, const QString& output) {
                  scanning_ = false;
                  this->parseScan(output);
                  this->publish();
              });
}

void WifiClient::parseScan(const QString& output)
{
    QStringList known;
    for(const QVariant& entry: networks_)
    {
        known << entry.toMap().value("ssid").toString();
    }
    // the strongest entry of every name (several access points of one network), hidden ones have none
    QMap<QString, QVariantMap> best;
    for(const QString& line: output.split('\n', Qt::SkipEmptyParts))
    {
        const QStringList fields = WifiClient::splitTerse(line);
        if(fields.size() < 3 || fields[0].isEmpty())
        {
            continue;
        }
        const int signal = fields[1].toInt();
        if(best.contains(fields[0]) && best[fields[0]].value("signal").toInt() >= signal)
        {
            continue;
        }
        const QString security = fields[2].trimmed();
        best[fields[0]] = {{"ssid", fields[0]}, {"signal", signal}, {"security", security},
                           {"secured", !security.isEmpty() && security != "--"}, {"known", known.contains(fields[0])},
                           // WPA3 only: SAE instead of a pre-shared key
                           {"sae", security.contains("WPA3") && !security.contains("WPA2")}};
    }
    QVariantList found;
    for(const QVariantMap& entry: best)
    {
        found << entry;
    }
    std::sort(found.begin(), found.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("signal").toInt() > b.toMap().value("signal").toInt();
    });
    found_ = found;
}

QString WifiClient::add(const QString& ssid, const QString& password, bool hidden,
                        std::function<void(const QString&)> added)
{
    if(!available_)
    {
        return tr("No WiFi card");
    }
    if(!enabled_)
    {
        return tr("Connecting to WiFi networks is switched off");
    }
    if(ssid.isEmpty() || ssid.toUtf8().size() > 32)
    {
        return tr("The name has 1 to 32 characters");
    }
    if(!password.isEmpty() && (password.size() < 8 || password.size() > 63))
    {
        return tr("The password has 8 to 63 characters");
    }
    if(busy_)
    {
        return tr("Please wait a moment");
    }

    bool sae = false;
    for(const QVariant& entry: found_)
    {
        if(entry.toMap().value("ssid") == ssid)
        {
            sae = entry.toMap().value("sae").toBool();
        }
    }
    // the same name again: the new password replaces the old profile
    QStringList replaced;
    QStringList order;
    for(const QVariant& entry: networks_)
    {
        const QVariantMap network = entry.toMap();
        (network.value("ssid") == ssid ? replaced : order) << network.value("uuid").toString();
    }

    // autoconnect only once it worked (syncProfiles)
    QStringList args{"connection", "add", "type", "wifi", "con-name", cPrefix + ssid, "ssid", ssid,
                     "connection.autoconnect", "no", "connection.autoconnect-priority", "0"};
    if(!interface_.isEmpty())
    {
        args << "connection.interface-name" << interface_;
    }
    if(!password.isEmpty())
    {
        args << "wifi-sec.key-mgmt" << (sae ? "sae" : "wpa-psk") << "wifi-sec.psk" << password;
    }
    if(hidden)
    {
        args << "802-11-wireless.hidden" << "yes";
    }

    OPENAUTO_LOG(info) << "[WifiClient] adding " << ssid.toStdString();
    busy_ = true;
    message_.clear();
    this->publish();
    auto addNew = [this, args, ssid, order, added]() {
        this->nmcli(args, [this, ssid, order, added](int exitCode, const QString& output) {
            busy_ = false;
            const QString uuid = output.section('(', 1).section(')', 0, 0);
            if(exitCode != 0 || uuid.isEmpty())
            {
                emit error(tr("%1 could not be saved: %2").arg(ssid, output.trimmed()));
                this->refresh();
                return;
            }
            message_ = tr("%1 saved.").arg(ssid);
            addedNames_.insert(uuid, ssid);
            // the new one last: networks added before keep their place
            this->setPriorities(QStringList(order) << uuid);
            if(added)
            {
                added(uuid);
            }
        });
    };
    if(replaced.isEmpty())
    {
        addNew();
    }
    else
    {
        this->nmcli(QStringList{"connection", "delete"} + replaced, [addNew](int, const QString&) { addNew(); });
    }
    return {};
}

void WifiClient::connectTo(const QString& uuid, std::function<void(bool)> done)
{
    QString ssid = addedNames_.value(uuid);
    bool joined = false;
    for(const QVariant& entry: networks_)
    {
        if(entry.toMap().value("uuid") == uuid)
        {
            ssid = entry.toMap().value("ssid").toString();
            joined = entry.toMap().value("joined").toBool();
        }
    }
    OPENAUTO_LOG(info) << "[WifiClient] joining " << ssid.toStdString();
    busy_ = true;
    message_ = tr("Connecting to %1 …").arg(ssid);
    this->publish();
    QStringList args{"--wait", "30", "connection", "up", uuid};
    if(!interface_.isEmpty())
    {
        args << "ifname" << interface_;
    }
    this->nmcli(args, [this, ssid, uuid, joined, done](int exitCode, const QString& output) {
        busy_ = false;
        if(exitCode == 0)
        {
            message_ = tr("Connected to %1.").arg(ssid);
        }
        else if(!joined && (output.contains("Secrets were required") || output.contains("802-1X supplicant")))
        {
            // a wrong password would only be tried again and again
            this->nmcli({"connection", "delete", uuid}, [this](int, const QString&) { this->refresh(); });
            message_.clear();
            emit error(tr("Could not connect to %1: wrong password?").arg(ssid));
        }
        else
        {
            message_ = tr("Could not connect to %1 (not in range?).").arg(ssid);
            OPENAUTO_LOG(info) << "[WifiClient] " << ssid.toStdString() << ": " << output.trimmed().toStdString();
        }
        this->refresh();
        if(done)
        {
            done(exitCode == 0);
        }
    });
}

void WifiClient::remove(const QString& uuid)
{
    for(const QVariant& entry: networks_)
    {
        if(entry.toMap().value("uuid") == uuid)
        {
            OPENAUTO_LOG(info) << "[WifiClient] removing " << entry.toMap().value("ssid").toString().toStdString();
            this->nmcli({"connection", "delete", uuid}, [this](int, const QString&) { this->refresh(); });
            return;
        }
    }
}

void WifiClient::moveUp(const QString& uuid)
{
    QStringList order;
    for(const QVariant& entry: networks_)
    {
        order << entry.toMap().value("uuid").toString();
    }
    const int index = order.indexOf(uuid);
    if(index > 0)
    {
        order.swapItemsAt(index, index - 1);
        this->setPriorities(order);
    }
}

void WifiClient::setPriorities(const QStringList& uuids)
{
    // first = highest; NetworkManager tries the highest first
    QStringList args;
    for(int i = 0; i < uuids.size(); ++i)
    {
        args << uuids[i] << QString::number(uuids.size() - i);
    }
    this->script(cPriorityScript, args, [this](int, const QString&) { this->refresh(); });
}

void WifiClient::nmcli(const QStringList& args, std::function<void(int, const QString&)> done)
{
    this->run(nmcli_, args, std::move(done));
}

void WifiClient::script(const char* script, const QStringList& args, std::function<void(int, const QString&)> done)
{
    this->run(QStringLiteral("sh"), QStringList{"-c", QString::fromLatin1(script), "sh", nmcli_} + args, std::move(done));
}

void WifiClient::run(const QString& program, const QStringList& args, std::function<void(int, const QString&)> done)
{
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    process->setProcessEnvironment(environment);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [process, done](int exitCode, QProcess::ExitStatus status) {
                process->deleteLater();
                done(status == QProcess::NormalExit ? exitCode : -1, QString::fromUtf8(process->readAll()));
            });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
        {
            process->deleteLater();
            done(-1, QString());
        }
    });
    process->start(program, args);
}

void WifiClient::publish()
{
    emit changed();
    core::EventBus::instance().publish("wifi.state", this->state());
}

}
