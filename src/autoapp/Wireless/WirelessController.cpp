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

#include <unistd.h>
#include <QDBusConnection>
#include <QRandomGenerator>
#include <QSocketNotifier>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Wireless/WirelessController.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace
{

const int cPairingSeconds = 120;
const int cConnectIntervalMs = 15000;
const quint16 cAndroidAutoPort = 5000;

QString generatePassword()
{
    // no 0/O/1/l: the password may have to be typed on a phone
    const QString alphabet = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString password;
    for(int i = 0; i < 16; ++i)
    {
        password += alphabet.at(QRandomGenerator::system()->bounded(alphabet.size()));
    }
    return password;
}

}

WirelessController::WirelessController(configuration::IConfiguration::Pointer configuration, QObject* parent)
    : QObject(parent)
    , configuration_(std::move(configuration))
    , handshakeProfile_("/org/openhu/androidauto")
    , headsetProfile_("/org/openhu/headset")
{
    bluez::registerTypes();

    connect(&hotspot_, &Hotspot::changed, this, [this]() {
        if(hotspot_.state() == Hotspot::State::On)
        {
            this->connectPhones();
        }
        this->publish();
    });
    connect(&handshakeProfile_, &BluezProfile::connected, this, &WirelessController::onHandshakeConnection);
    connect(&headsetProfile_, &BluezProfile::connected, this, &WirelessController::onHeadsetConnection);
    connect(&headsetProfile_, &BluezProfile::disconnectRequested, this, &WirelessController::closeHeadset);

    connect(&agent_, &PairingAgent::confirmationRequested, this, &WirelessController::onConfirmation);
    connect(&agent_, &PairingAgent::finished, this, [this]() {
        pairingDevice_.clear();
        pairingPasskey_.clear();
        core::EventBus::instance().publish("bluetooth.pairing", {{"active", false}});
        this->publish();
    });

    expectTimer_.setSingleShot(true);
    connect(&expectTimer_, &QTimer::timeout, this, [this]() {
        OPENAUTO_LOG(info) << "[WirelessController] pairing over the cable timed out";
        expectedAddress_.clear();
        expectPairStarted_ = false;
        this->stopDiscovery();
        this->updateAgent();
    });

    stuckTimer_.setSingleShot(true);
    stuckTimer_.setInterval(30000);
    connect(&stuckTimer_, &QTimer::timeout, this, [this]() {
        OPENAUTO_LOG(warning) << "[WirelessController] phone did not connect over WiFi, starting over";
        this->restartPhoneLink();
    });

    pairingTimer_.setSingleShot(true);
    connect(&pairingTimer_, &QTimer::timeout, this, [this]() { this->setPairing(false); });

    // the phone may be out of range or busy: keep asking until Android Auto runs
    connectTimer_.setInterval(cConnectIntervalMs);
    connect(&connectTimer_, &QTimer::timeout, this, &WirelessController::connectPhones);

    // BlueZ sends bursts of property changes, collect them
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(300);
    connect(&refreshTimer_, &QTimer::timeout, this, &WirelessController::refreshDevices);

    auto bus = QDBusConnection::systemBus();
    for(const char* name: {"InterfacesAdded", "InterfacesRemoved"})
    {
        bus.connect(bluez::cService, "/", "org.freedesktop.DBus.ObjectManager", name, this, SLOT(onBluezSignal()));
    }
    bus.connect(bluez::cService, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                SLOT(onBluezSignal()));

    connect(&core::EventBus::instance(), &core::EventBus::event, this, [this](const QString& topic, const QVariantMap& data) {
        if(topic == "projection.state")
        {
            this->onProjectionState(data.value("connected").toBool());
        }
        else if(topic == "projection.transport")
        {
            usbProjection_ = data.value("transport").toString() == "usb";
        }
        else if(topic == "bluetooth.expect")
        {
            this->expectPairing(data.value("address").toString());
        }
    });
}

WirelessController::~WirelessController()
{
    this->stop();
}

QString WirelessController::ssid() const
{
    const auto settings = configuration_->getWirelessSettings();
    if(!settings.ssid.empty())
    {
        return QString::fromStdString(settings.ssid);
    }
    const auto suffix = configuration_->getNameSuffix();
    return suffix.empty() ? QString("OpenHU") : QString::fromStdString("OpenHU " + suffix);
}

void WirelessController::apply()
{
    if(!configuration_->getWirelessProjectionEnabled())
    {
        this->stop();
        return;
    }

    auto settings = configuration_->getWirelessSettings();
    if(settings.password.size() < 8)
    {
        settings.password = generatePassword().toStdString();
        configuration_->setWirelessSettings(settings);
        configuration_->save();
    }

    OPENAUTO_LOG(info) << "[WirelessController] starting";
    running_ = true;
    this->startBluetooth();
    hotspot_.start(settings, this->ssid());
    this->publish();
}

void WirelessController::stop()
{
    if(!running_)
    {
        return;
    }
    OPENAUTO_LOG(info) << "[WirelessController] stopping";
    running_ = false;
    connectTimer_.stop();
    this->setPairing(false);
    handshake_.reset();
    this->stopBluetooth();
    hotspot_.stop();
    phase_ = "idle";
    this->publish();
}

bool WirelessController::findAdapter()
{
    adapterPath_.clear();
    const QString wanted = QString::fromStdString(configuration_->getBluetoothAdapterAddress()).toUpper();
    const BluezObjects objects = bluez::managedObjects();
    QStringList adapterUuids;
    for(auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        const QVariantMap adapter = it.value().value(bluez::cAdapter);
        if(adapter.isEmpty())
        {
            continue;
        }
        if(adapterPath_.isEmpty() || adapter.value("Address").toString().toUpper() == wanted)
        {
            adapterPath_ = it.key().path();
            adapterName_ = adapter.value("Alias").toString();
            adapterUuids = adapter.value("UUIDs").toStringList();
        }
    }
    if(adapterPath_.isEmpty())
    {
        bluetoothError_ = objects.isEmpty() ? "Bluetooth-Dienst (bluetoothd) nicht erreichbar."
                                            : "Kein Bluetooth-Adapter gefunden.";
        OPENAUTO_LOG(warning) << "[WirelessController] " << bluetoothError_.toStdString();
        return false;
    }
    adapterUuids_ = adapterUuids;
    return true;
}

void WirelessController::startBluetooth()
{
    bluetoothError_.clear();
    devicesLoaded_ = false;
    if(!this->findAdapter())
    {
        return;
    }
    const QStringList adapterUuids = adapterUuids_;

    QString error = bluez::setProperty(adapterPath_, bluez::cAdapter, "Powered", true);
    if(!error.isEmpty())
    {
        bluetoothError_ = "Bluetooth lässt sich nicht einschalten: " + error;
        return;
    }

    error = handshakeProfile_.registerProfile(bluez::cAndroidAutoUuid,
                                              {{"Name", "OpenHU Android Auto"},
                                               {"Role", "server"},
                                               // without a channel BlueZ publishes no RFCOMM entry and the phone
                                               // cannot connect (it waits at "Connecting to Android Auto")
                                               {"Channel", QVariant::fromValue(quint16(8))},
                                               {"RequireAuthentication", true},
                                               {"RequireAuthorization", false}});
    if(!error.isEmpty())
    {
        bluetoothError_ = "Android-Auto-Profil: " + error;
        return;
    }
    // The phone starts wireless Android Auto once a "car" is connected. With hands-free from PipeWire the phone
    // is connected normally; a second headset profile next to it makes the phone reject the connection.
    useHeadset_ = !adapterUuids.contains(bluez::cHandsfreeUuid, Qt::CaseInsensitive);
    if(useHeadset_)
    {
        error = headsetProfile_.registerProfile(bluez::cHeadsetUuid, {{"Name", "OpenHU Headset"}});
        if(!error.isEmpty())
        {
            OPENAUTO_LOG(info) << "[WirelessController] headset profile: " << error.toStdString();
            useHeadset_ = false;
        }
    }
    else
    {
        headsetProfile_.unregisterProfile();
    }
    OPENAUTO_LOG(info) << "[WirelessController] phone link via " << (useHeadset_ ? "headset profile" : "hands-free (system)");

    OPENAUTO_LOG(info) << "[WirelessController] Bluetooth ready on " << adapterPath_.toStdString();
    this->refreshDevices();
}

void WirelessController::stopBluetooth()
{
    for(auto it = headsets_.begin(); it != headsets_.end(); it = headsets_.begin())
    {
        this->closeHeadset(it->first);
    }
    handshakeProfile_.unregisterProfile();
    headsetProfile_.unregisterProfile();
}

void WirelessController::onBluezSignal()
{
    if(running_ || !expectedAddress_.isEmpty())
    {
        refreshTimer_.start();
    }
}

QString WirelessController::deviceName(const QString& path) const
{
    for(const auto& device: devices_)
    {
        const QVariantMap map = device.toMap();
        if(map.value("path").toString() == path)
        {
            return map.value("name").toString();
        }
    }
    const QVariantMap properties = bluez::managedObjects().value(QDBusObjectPath(path)).value(bluez::cDevice);
    return properties.value("Alias", properties.value("Address")).toString();
}

void WirelessController::refreshDevices()
{
    if(adapterPath_.isEmpty())
    {
        return;
    }
    if(!expectedAddress_.isEmpty() && !expectPairStarted_)
    {
        this->pairExpected();
    }

    QVariantList devices;
    QSet<QString> paired;
    const BluezObjects objects = bluez::managedObjects();
    for(auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        const QVariantMap device = it.value().value(bluez::cDevice);
        if(device.isEmpty() || !device.value("Paired").toBool()
           || device.value("Adapter").value<QDBusObjectPath>().path() != adapterPath_)
        {
            continue;
        }
        const QString path = it.key().path();
        const QString address = device.value("Address").toString();
        paired.insert(path);
        devices.append(QVariantMap{{"path", path},
                                   {"address", address},
                                   {"name", device.value("Alias", address).toString()},
                                   {"icon", device.value("Icon").toString()},
                                   {"connected", device.value("Connected").toBool()}});

        if(devicesLoaded_ && !paired_.contains(path))
        {
            // newly paired: trust it (reconnects without asking) and use it for wireless Android Auto
            OPENAUTO_LOG(info) << "[WirelessController] paired with " << address.toStdString();
            bluez::setProperty(path, bluez::cDevice, "Trusted", true);
            auto settings = configuration_->getWirelessSettings();
            settings.lastDevice = address.toStdString();
            configuration_->setWirelessSettings(settings);
            configuration_->save();
            if(pairing_)
            {
                this->setPairing(false);
            }
            QTimer::singleShot(2000, this, &WirelessController::connectPhones);
        }
    }

    // headset links of phones that are gone (no EOF seen on the socket)
    for(const auto& device: devices)
    {
        const QVariantMap map = device.toMap();
        if(!map.value("connected").toBool())
        {
            this->closeHeadset(map.value("path").toString());
        }
    }

    paired_ = paired;
    devicesLoaded_ = true;
    if(devices != devices_)
    {
        devices_ = devices;
        emit devicesChanged();
    }
}

void WirelessController::connectPhones()
{
    if(!running_ || projectionActive_ || handshake_ || adapterPath_.isEmpty()
       || hotspot_.state() != Hotspot::State::On)
    {
        return;
    }
    if(!connectTimer_.isActive())
    {
        connectTimer_.start();
    }

    // the last phone first, otherwise every paired phone
    const QString last = QString::fromStdString(configuration_->getWirelessSettings().lastDevice).toUpper();
    QVariantList candidates;
    for(const auto& device: devices_)
    {
        const QVariantMap map = device.toMap();
        if(map.value("address").toString().toUpper() == last)
        {
            candidates.prepend(map);
        }
        else if(map.value("icon").toString() == "phone")
        {
            candidates.append(map);
        }
    }

    for(const auto& candidate: candidates)
    {
        const QVariantMap map = candidate.toMap();
        const QString path = map.value("path").toString();
        if(headsets_.count(path) > 0)
        {
            continue;
        }
        if(!useHeadset_ && map.value("connected").toBool())
        {
            // connected through hands-free already, the phone decides when to start
            break;
        }
        OPENAUTO_LOG(info) << "[WirelessController] connecting " << map.value("name").toString().toStdString();
        if(useHeadset_)
        {
            bluez::callAsync(path, bluez::cDevice, "ConnectProfile", {bluez::cHeadsetGatewayUuid});
        }
        else
        {
            bluez::callAsync(path, bluez::cDevice, "Connect");
        }
        if(phase_ == "idle")
        {
            this->setPhase("waiting");
        }
        break;
    }
}

void WirelessController::onHeadsetConnection(const QString& device, int fd)
{
    this->closeHeadset(device);
    // keep the link open (the phone sees a connected car); data from the phone is read and dropped
    auto* notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, device, fd]() {
        char data[256];
        if(::read(fd, data, sizeof(data)) <= 0)
        {
            QTimer::singleShot(0, this, [this, device]() { this->closeHeadset(device); });
        }
    });
    headsets_[device] = {fd, notifier};
}

void WirelessController::closeHeadset(const QString& device)
{
    auto it = headsets_.find(device);
    if(it == headsets_.end())
    {
        return;
    }
    it->second.second->setEnabled(false);
    it->second.second->deleteLater();
    ::close(it->second.first);
    headsets_.erase(it);
}

void WirelessController::onHandshakeConnection(const QString& device, int fd)
{
    if(projectionActive_ && usbProjection_ && configuration_->getWirelessSettings().priority != "wifi")
    {
        // on the cable with USB priority: no WiFi credentials, the phone would switch to wireless
        OPENAUTO_LOG(info) << "[WirelessController] phone is on the cable, wireless setup skipped";
        ::close(fd);
        return;
    }
    if(hotspot_.state() != Hotspot::State::On)
    {
        OPENAUTO_LOG(warning) << "[WirelessController] phone connected, but the hotspot is not running";
        ::close(fd);
        return;
    }

    phaseDevice_ = this->deviceName(device);
    phaseDevicePath_ = device;
    stuckTimer_.start();
    auto settings = configuration_->getWirelessSettings();
    WifiHandshake::Network network{this->ssid(), QString::fromStdString(settings.password), hotspot_.bssid(),
                                   Hotspot::cAddress, cAndroidAutoPort};
    handshake_ = std::make_unique<WifiHandshake>(fd, network);
    connect(handshake_.get(), &WifiHandshake::progress, this, [this](const QString& step, const QString& detail) {
        if(step == "failed")
        {
            this->setPhase("error", detail);
        }
        else if(!projectionActive_)
        {
            this->setPhase(step == "info" ? "handshake" : "joining");
        }
    });
    connect(handshake_.get(), &WifiHandshake::closed, this, [this]() {
        // the phone closes the channel when it is done; deleted later, we are inside its signal
        handshake_.release()->deleteLater();
    });
    this->setPhase("handshake");

    // remember who uses wireless Android Auto
    const QVariantMap properties = bluez::managedObjects().value(QDBusObjectPath(device)).value(bluez::cDevice);
    const std::string address = properties.value("Address").toString().toStdString();
    if(!address.empty() && settings.lastDevice != address)
    {
        settings.lastDevice = address;
        configuration_->setWirelessSettings(settings);
        configuration_->save();
    }
}

void WirelessController::updateAgent()
{
    if(pairing_ || !expectedAddress_.isEmpty())
    {
        const QString error = agent_.registerAgent();
        if(!error.isEmpty())
        {
            OPENAUTO_LOG(warning) << "[WirelessController] pairing agent: " << error.toStdString();
        }
    }
    else
    {
        agent_.unregisterAgent();
    }
}

void WirelessController::expectPairing(const QString& address)
{
    if(adapterPath_.isEmpty() && !this->findAdapter())
    {
        return;
    }
    // the phone repeats its request every few seconds until the pairing is done
    if(expectedAddress_ == address.toUpper())
    {
        return;
    }
    OPENAUTO_LOG(info) << "[WirelessController] phone " << address.toStdString() << " pairs over the cable";
    expectedAddress_ = address.toUpper();
    expectPairStarted_ = false;
    expectTimer_.start(60000);
    bluez::setProperty(adapterPath_, bluez::cAdapter, "Powered", true);
    bluez::setProperty(adapterPath_, bluez::cAdapter, "Pairable", true);
    this->updateAgent();
    this->pairExpected();
}

void WirelessController::pairExpected()
{
    const BluezObjects objects = bluez::managedObjects();
    for(auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        const QVariantMap device = it.value().value(bluez::cDevice);
        if(device.value("Address").toString().toUpper() != expectedAddress_
           || device.value("Adapter").value<QDBusObjectPath>().path() != adapterPath_)
        {
            continue;
        }
        expectPairStarted_ = true;
        this->stopDiscovery();
        if(device.value("Paired").toBool())
        {
            return;
        }
        OPENAUTO_LOG(info) << "[WirelessController] pairing with " << expectedAddress_.toStdString();
        bluez::callAsync(it.key().path(), bluez::cDevice, "Pair");
        return;
    }

    // BlueZ only knows devices it has seen: search until the phone shows up
    if(!discovering_)
    {
        bluez::call(adapterPath_, bluez::cAdapter, "SetDiscoveryFilter", {QVariantMap{{"Transport", "bredr"}}});
        discovering_ = bluez::call(adapterPath_, bluez::cAdapter, "StartDiscovery").isEmpty();
        OPENAUTO_LOG(info) << "[WirelessController] searching the phone " << (discovering_ ? "" : "failed");
    }
}

void WirelessController::stopDiscovery()
{
    if(discovering_)
    {
        discovering_ = false;
        bluez::call(adapterPath_, bluez::cAdapter, "StopDiscovery");
    }
}

void WirelessController::onConfirmation(const QString& device, const QString& passkey)
{
    // /org/bluez/hci0/dev_E8_78_29_C0_E5_2D -> E8:78:29:C0:E5:2D
    const QString address = device.section('/', -1).mid(4).replace('_', ':').toUpper();
    if(!expectedAddress_.isEmpty() && address == expectedAddress_)
    {
        OPENAUTO_LOG(info) << "[WirelessController] accepting pairing of " << address.toStdString() << " (USB)";
        agent_.answer(true);
        core::EventBus::instance().emitEvent("bluetooth.passkey", {{"address", address}, {"passkey", passkey}});
        bluez::setProperty(device, bluez::cDevice, "Trusted", true);
        auto settings = configuration_->getWirelessSettings();
        settings.lastDevice = address.toStdString();
        configuration_->setWirelessSettings(settings);
        configuration_->save();
        expectedAddress_.clear();
        expectPairStarted_ = false;
        expectTimer_.stop();
        // unregistering right away would cancel the pairing that is still running
        QTimer::singleShot(5000, this, &WirelessController::updateAgent);
        return;
    }

    pairingDevice_ = this->deviceName(device);
    pairingPasskey_ = passkey;
    OPENAUTO_LOG(info) << "[WirelessController] pairing request from " << pairingDevice_.toStdString();
    core::EventBus::instance().publish("bluetooth.pairing",
                                       {{"active", true}, {"device", pairingDevice_}, {"passkey", passkey}});
    this->publish();
}

void WirelessController::onProjectionState(bool active)
{
    const bool wireless = phase_ == "handshake" || phase_ == "joining" || phase_ == "connected";
    projectionActive_ = active;
    if(active)
    {
        connectTimer_.stop();
        stuckTimer_.stop();
        if(phase_ != "idle")
        {
            this->setPhase("connected");
        }
        return;
    }
    if(!running_)
    {
        return;
    }
    if(wireless)
    {
        // The phone does not retry by itself after a failed or ended wireless session. Wait a moment:
        // when it was plugged in (USB takes over) Bluetooth stays connected.
        QTimer::singleShot(1500, this, [this]() {
            if(!projectionActive_)
            {
                this->restartPhoneLink();
            }
            else
            {
                handshake_.reset();
                this->setPhase("idle");
            }
        });
    }
    else
    {
        this->setPhase("idle");
        connectTimer_.start();
    }
}

void WirelessController::restartPhoneLink()
{
    stuckTimer_.stop();
    handshake_.reset();
    this->setPhase("idle");
    if(!running_)
    {
        return;
    }
    if(!phaseDevicePath_.isEmpty())
    {
        bluez::callAsync(phaseDevicePath_, bluez::cDevice, "Disconnect");
    }
    // connectPhones() connects it again after the disconnect went through
    QTimer::singleShot(4000, this, &WirelessController::connectPhones);
    connectTimer_.start();
}

void WirelessController::setPairing(bool enabled)
{
    if(enabled == pairing_)
    {
        return;
    }
    if(enabled && (!running_ || adapterPath_.isEmpty()))
    {
        return;
    }

    pairing_ = enabled;
    this->updateAgent();
    if(enabled)
    {
        bluez::setProperty(adapterPath_, bluez::cAdapter, "Pairable", true);
        bluez::setProperty(adapterPath_, bluez::cAdapter, "DiscoverableTimeout", quint32(cPairingSeconds));
        bluez::setProperty(adapterPath_, bluez::cAdapter, "Discoverable", true);
        pairingTimer_.start(cPairingSeconds * 1000);
    }
    else
    {
        pairingTimer_.stop();
        if(!adapterPath_.isEmpty())
        {
            bluez::setProperty(adapterPath_, bluez::cAdapter, "Discoverable", false);
        }
    }
    OPENAUTO_LOG(info) << "[WirelessController] pairing " << (enabled ? "on" : "off");
    this->publish();
}

void WirelessController::pair()
{
    if(!configuration_->getWirelessProjectionEnabled())
    {
        configuration_->setWirelessProjectionEnabled(true);
        configuration_->save();
    }
    if(!running_)
    {
        this->apply();
    }
    this->setPairing(true);
}

void WirelessController::confirmPairing(bool accept)
{
    OPENAUTO_LOG(info) << "[WirelessController] pairing " << (accept ? "accepted" : "rejected");
    agent_.answer(accept);
}

void WirelessController::connectDevice(const QString& address)
{
    auto settings = configuration_->getWirelessSettings();
    settings.lastDevice = address.toStdString();
    configuration_->setWirelessSettings(settings);
    configuration_->save();
    this->connectPhones();
}

void WirelessController::removeDevice(const QString& address)
{
    for(const auto& device: devices_)
    {
        const QVariantMap map = device.toMap();
        if(map.value("address").toString() == address)
        {
            this->closeHeadset(map.value("path").toString());
            bluez::call(adapterPath_, bluez::cAdapter, "RemoveDevice",
                        {QVariant::fromValue(QDBusObjectPath(map.value("path").toString()))});
        }
    }
    this->refreshDevices();
}

void WirelessController::enableWifi()
{
    hotspot_.enableRadio();
    this->apply();
}

void WirelessController::setPhase(const QString& phase, const QString& detail)
{
    phase_ = phase;
    phaseDetail_ = detail;
    this->publish();
}

QVariantMap WirelessController::state() const
{
    QString phaseText;
    if(phase_ == "waiting") phaseText = "Warte auf das Telefon …";
    else if(phase_ == "handshake") phaseText = "Telefon verbunden, übergebe WLAN-Zugang …";
    else if(phase_ == "joining") phaseText = "Telefon verbindet sich mit dem WLAN …";
    else if(phase_ == "connected") phaseText = "Android Auto läuft kabellos";
    else if(phase_ == "error") phaseText = "Telefon meldet: " + phaseDetail_;

    return {
        {"enabled", configuration_->getWirelessProjectionEnabled()},
        {"running", running_},
        {"hotspot", hotspot_.stateName()},
        {"hotspotError", hotspot_.error()},
        {"wifiOff", hotspot_.radioOff()},
        {"interface", hotspot_.interface()},
        {"ssid", this->ssid()},
        {"bluetooth", !running_ ? "off" : bluetoothError_.isEmpty() ? "on" : "error"},
        {"bluetoothError", bluetoothError_},
        {"adapter", adapterName_},
        {"pairing", pairing_},
        {"pairingDevice", pairingDevice_},
        {"pairingPasskey", pairingPasskey_},
        {"phase", phase_},
        {"phaseText", phaseText},
        {"device", phaseDevice_},
    };
}

QVariantList WirelessController::devices() const
{
    return devices_;
}

void WirelessController::publish()
{
    emit changed();
    core::EventBus::instance().publish("wireless.state", this->state());
}

}
