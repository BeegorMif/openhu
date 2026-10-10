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

#include <map>
#include <memory>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Wireless/Bluez.hpp>
#include <f1x/openauto/autoapp/Wireless/Hotspot.hpp>
#include <f1x/openauto/autoapp/Wireless/WifiHandshake.hpp>

class QSocketNotifier;

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace wireless
{

// Wireless Android Auto (QML: "wireless"):
//   1. Bluetooth: Android Auto RFCOMM profile + headset profile registered at BlueZ, phone paired once
//   2. WiFi hotspot through NetworkManager
//   3. OpenHU connects to the phone's headset gateway, the phone opens the Android Auto RFCOMM channel,
//      gets the hotspot credentials (WifiHandshake), joins the WiFi and connects to TCP port 5000 (App).
// Publishes "wireless.state" and "bluetooth.pairing" on the EventBus.
class WirelessController: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    explicit WirelessController(configuration::IConfiguration::Pointer configuration, QObject* parent = nullptr);
    ~WirelessController() override;

    QVariantMap state() const;
    QVariantList devices() const;

    // Starts or stops everything according to the configuration (also after settings changes)
    Q_INVOKABLE void apply();
    void stop();

    // Makes the adapter visible for 2 minutes and accepts pairing requests (confirmed in the shell)
    Q_INVOKABLE void setPairing(bool enabled);
    // "Pair phone" button: switches wireless Android Auto on if needed, then starts pairing mode
    Q_INVOKABLE void pair();
    Q_INVOKABLE void confirmPairing(bool accept);
    Q_INVOKABLE void connectDevice(const QString& address);
    Q_INVOKABLE void removeDevice(const QString& address);
    Q_INVOKABLE void enableWifi();

signals:
    void changed();
    void devicesChanged();

private slots:
    // BlueZ object/property changes (connected by name, needs a slot)
    void onBluezSignal();

private:
    void startBluetooth();
    void stopBluetooth();
    void refreshDevices();
    void connectPhones();
    void onHandshakeConnection(const QString& device, int fd);
    void onHeadsetConnection(const QString& device, int fd);
    void closeHeadset(const QString& device);
    void setPhase(const QString& phase, const QString& detail = QString());
    bool findAdapter();
    void updateAgent();
    void expectPairing(const QString& address);
    // pairing over the cable is started by OpenHU: find the phone (discovery) and call Pair()
    void pairExpected();
    void stopDiscovery();
    void onConfirmation(const QString& device, const QString& passkey);
    void onProjectionState(bool active);
    // wireless session ended or the phone got stuck: reconnect Bluetooth so it starts over
    void restartPhoneLink();
    QString ssid() const;
    QString deviceName(const QString& path) const;
    void publish();

    configuration::IConfiguration::Pointer configuration_;
    bool running_ = false;
    QString adapterPath_;
    QString adapterName_;
    QStringList adapterUuids_;
    QString bluetoothError_;
    Hotspot hotspot_;
    BluezProfile handshakeProfile_;
    BluezProfile headsetProfile_;
    PairingAgent agent_;
    bool pairing_ = false;
    QTimer pairingTimer_;
    QString pairingDevice_;
    QString pairingPasskey_;
    QTimer connectTimer_;
    // pairing over the USB cable: this phone is accepted without asking
    QString expectedAddress_;
    QTimer expectTimer_;
    bool expectPairStarted_ = false;
    bool discovering_ = false;
    // no TCP connection after the WiFi handshake
    QTimer stuckTimer_;
    QString phaseDevicePath_;
    QTimer refreshTimer_;
    bool projectionActive_ = false;
    bool usbProjection_ = false;
    QString phase_ = "idle";
    QString phaseDetail_;
    QString phaseDevice_;
    std::unique_ptr<WifiHandshake> handshake_;
    std::map<QString, std::pair<int, QSocketNotifier*>> headsets_;
    QVariantList devices_;
    QSet<QString> paired_;
    bool devicesLoaded_ = false;
    // false when another program (PipeWire) offers hands-free: then the phone is connected normally
    bool useHeadset_ = true;
};

}
}
}
}
