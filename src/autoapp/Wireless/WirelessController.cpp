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
#include <f1x/openauto/autoapp/Bluetooth/BluetoothManager.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Wireless/WirelessController.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace
{

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

WirelessController::WirelessController(configuration::IConfiguration::Pointer configuration,
                                       bt::BluetoothManager& bluetooth, QObject* parent)
    : QObject(parent)
    , configuration_(std::move(configuration))
    , bluetooth_(bluetooth)
    , handshakeProfile_("/org/openhu/androidauto")
    , headsetProfile_("/org/openhu/headset")
{
    connect(&hotspot_, &Hotspot::changed, this, [this]() {
        if(pendingFd_ >= 0 && hotspot_.state() == Hotspot::State::On)
        {
            const int fd = pendingFd_;
            pendingFd_ = -1;
            this->startHandshake(pendingDevice_, fd);
        }
        else if(pendingFd_ >= 0 && hotspot_.state() == Hotspot::State::Error)
        {
            ::close(pendingFd_);
            pendingFd_ = -1;
            this->setPhase("error", hotspot_.error());
        }
        else if(hotspot_.state() == Hotspot::State::On)
        {
            this->connectPhones();
        }
        this->updateIdle();
        this->publish();
    });

    idleTimer_.setSingleShot(true);
    idleTimer_.setInterval(120000);
    connect(&idleTimer_, &QTimer::timeout, this, [this]() {
        OPENAUTO_LOG(info) << "[WirelessController] hotspot not used, switching it off";
        hotspot_.stop();
    });
    connect(&handshakeProfile_, &BluezProfile::connected, this, &WirelessController::onHandshakeConnection);
    connect(&headsetProfile_, &BluezProfile::connected, this, &WirelessController::onHeadsetConnection);
    connect(&headsetProfile_, &BluezProfile::disconnectRequested, this, &WirelessController::closeHeadset);

    // a newly paired phone (pairing dialog or over the cable) is used for wireless Android Auto
    connect(&bluetooth_, &bt::BluetoothManager::devicePaired, this, [this](const QString& address) {
        if(bluetooth_.device(address).value("icon").toString() != "phone")
        {
            return;
        }
        auto settings = configuration_->getWirelessSettings();
        settings.lastDevice = address.toStdString();
        configuration_->setWirelessSettings(settings);
        configuration_->save();
        QTimer::singleShot(2000, this, &WirelessController::connectPhones);
    });
    connect(&bluetooth_, &bt::BluetoothManager::devicesChanged, this, &WirelessController::closeStaleHeadsets);

    stuckTimer_.setSingleShot(true);
    stuckTimer_.setInterval(30000);
    connect(&stuckTimer_, &QTimer::timeout, this, [this]() {
        OPENAUTO_LOG(warning) << "[WirelessController] phone did not connect over WiFi, starting over";
        this->restartPhoneLink();
    });

    // the phone may be out of range or busy: keep asking until Android Auto runs
    connectTimer_.setInterval(cConnectIntervalMs);
    connect(&connectTimer_, &QTimer::timeout, this, &WirelessController::connectPhones);

    connect(&core::EventBus::instance(), &core::EventBus::event, this, [this](const QString& topic, const QVariantMap& data) {
        if(topic == "projection.state")
        {
            this->onProjectionState(data.value("connected").toBool(), data.value("endedByUser").toBool());
        }
        else if(topic == "projection.transport")
        {
            usbProjection_ = data.value("transport").toString() == "usb";
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
    if(!this->hotspotOnDemand())
    {
        hotspot_.start(settings, this->ssid());
    }
    else if(!(projectionActive_ && !usbProjection_))
    {
        // started when a phone asks for it (also after switching from "always")
        hotspot_.stop();
        this->connectPhones();
    }
    this->publish();
}

bool WirelessController::hotspotOnDemand() const
{
    return configuration_->getWirelessSettings().hotspot == "demand";
}

void WirelessController::updateIdle()
{
    const bool inUse = (projectionActive_ && !usbProjection_) || handshake_ || pendingFd_ >= 0;
    if(running_ && this->hotspotOnDemand() && hotspot_.state() == Hotspot::State::On && !inUse)
    {
        if(!idleTimer_.isActive())
        {
            idleTimer_.start();
        }
    }
    else
    {
        idleTimer_.stop();
    }
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
    idleTimer_.stop();
    bluetooth_.setPairing(false);
    handshake_.reset();
    if(pendingFd_ >= 0)
    {
        ::close(pendingFd_);
        pendingFd_ = -1;
    }
    this->stopBluetooth();
    hotspot_.stop();
    phase_ = "idle";
    this->publish();
}

void WirelessController::startBluetooth()
{
    bluetoothError_.clear();
    if(!bluetooth_.ensureAdapter())
    {
        bluetoothError_ = bluetooth_.error();
        return;
    }
    const QStringList adapterUuids = bluetooth_.adapterUuids();

    QString error = handshakeProfile_.registerProfile(bluez::cAndroidAutoUuid,
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

    OPENAUTO_LOG(info) << "[WirelessController] Bluetooth ready";
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

void WirelessController::closeStaleHeadsets()
{
    // headset links of phones that are gone (no EOF seen on the socket)
    for(const auto& device: bluetooth_.devices())
    {
        const QVariantMap map = device.toMap();
        if(!map.value("connected").toBool())
        {
            this->closeHeadset(map.value("path").toString());
        }
    }
}

void WirelessController::connectPhones()
{
    if(!running_ || projectionActive_ || endedByUser_ || handshake_ || bluetooth_.adapterPath().isEmpty()
       || (hotspot_.state() != Hotspot::State::On && !this->hotspotOnDemand()))
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
    for(const auto& device: bluetooth_.devices())
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
        if(hotspot_.state() == Hotspot::State::Error && !this->hotspotOnDemand())
        {
            OPENAUTO_LOG(warning) << "[WirelessController] phone connected, but the hotspot is not running";
            ::close(fd);
            return;
        }
        // on demand (or still starting): the phone waits on the channel until the hotspot is up
        if(pendingFd_ >= 0)
        {
            ::close(pendingFd_);
        }
        pendingFd_ = fd;
        pendingDevice_ = device;
        phaseDevice_ = bluetooth_.deviceName(device);
        if(hotspot_.state() != Hotspot::State::Starting)
        {
            hotspot_.start(configuration_->getWirelessSettings(), this->ssid());
        }
        this->setPhase("hotspot");
        return;
    }
    this->startHandshake(device, fd);
}

void WirelessController::startHandshake(const QString& device, int fd)
{
    phaseDevice_ = bluetooth_.deviceName(device);
    phaseDevicePath_ = device;
    stuckTimer_.start();
    auto settings = configuration_->getWirelessSettings();
    WifiHandshake::Network network{this->ssid(), QString::fromStdString(settings.password), hotspot_.bssid(),
                                   hotspot_.address(), cAndroidAutoPort};
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
        this->updateIdle();
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

void WirelessController::onProjectionState(bool active, bool endedByUser)
{
    const bool wireless = phase_ == "handshake" || phase_ == "joining" || phase_ == "connected";
    projectionActive_ = active;
    // after the current event: transport and handshake state are updated by then
    QTimer::singleShot(0, this, &WirelessController::updateIdle);
    if(active)
    {
        endedByUser_ = false;
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
    if(endedByUser)
    {
        // like in a car: ended on the phone means off until the phone starts it again
        OPENAUTO_LOG(info) << "[WirelessController] Android Auto ended on the phone, not reconnecting";
        endedByUser_ = true;
        connectTimer_.stop();
        stuckTimer_.stop();
        handshake_.reset();
        this->setPhase("idle");
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
    this->updateIdle();
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
    bluetooth_.setPairing(true);
}

void WirelessController::connectDevice(const QString& address)
{
    const bool endedByUser = endedByUser_;
    endedByUser_ = false;
    auto settings = configuration_->getWirelessSettings();
    settings.lastDevice = address.toStdString();
    configuration_->setWirelessSettings(settings);
    configuration_->save();

    const QVariantMap device = bluetooth_.device(address);
    if(endedByUser && device.value("connected").toBool())
    {
        // Bluetooth stayed connected: the phone only starts Android Auto again after it reconnects
        bluez::callAsync(device.value("path").toString(), bluez::cDevice, "Disconnect");
        QTimer::singleShot(4000, this, &WirelessController::connectPhones);
        return;
    }
    this->connectPhones();
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
    else if(phase_ == "hotspot") phaseText = "Telefon meldet sich, WLAN-Hotspot startet …";
    else if(phase_ == "handshake") phaseText = "Telefon verbunden, übergebe WLAN-Zugang …";
    else if(phase_ == "joining") phaseText = "Telefon verbindet sich mit dem WLAN …";
    else if(phase_ == "connected") phaseText = "Android Auto läuft kabellos";
    else if(phase_ == "error") phaseText = "Telefon meldet: " + phaseDetail_;

    return {
        {"enabled", configuration_->getWirelessProjectionEnabled()},
        {"running", running_},
        {"hotspot", hotspot_.stateName()},
        {"hotspotMode", this->hotspotOnDemand() ? "demand" : "always"},
        {"hotspotError", hotspot_.error()},
        {"wifiOff", hotspot_.radioOff()},
        {"interface", hotspot_.interface()},
        {"channel", hotspot_.channel()},
        {"ssid", this->ssid()},
        {"bluetooth", !running_ ? "off" : bluetoothError_.isEmpty() ? "on" : "error"},
        {"bluetoothError", bluetoothError_},
        {"phase", phase_},
        {"phaseText", phaseText},
        {"device", phaseDevice_},
    };
}

void WirelessController::publish()
{
    emit changed();
    core::EventBus::instance().publish("wireless.state", this->state());
}

}
