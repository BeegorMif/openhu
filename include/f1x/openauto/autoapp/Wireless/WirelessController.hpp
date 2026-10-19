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

#include <map>
#include <memory>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Wireless/Bluez.hpp>
#include <f1x/openauto/autoapp/Wireless/Hotspot.hpp>
#include <f1x/openauto/autoapp/Wireless/WifiClient.hpp>
#include <f1x/openauto/autoapp/Wireless/WifiHandshake.hpp>

class QSocketNotifier;

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace bt
{
class BluetoothManager;
}

namespace wireless
{

// Wireless Android Auto (QML: "wireless"):
//   1. Bluetooth (BluetoothManager): Android Auto RFCOMM profile + headset profile registered at BlueZ,
//      phone paired once
//   2. WiFi hotspot through NetworkManager
//   3. OpenHU connects the phone (hands-free or headset gateway), the phone opens the Android Auto RFCOMM
//      channel, gets the hotspot credentials (WifiHandshake), joins the WiFi and connects to TCP port 5000 (App).
// Publishes "wireless.state" on the EventBus.
class WirelessController: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)

public:
    WirelessController(configuration::IConfiguration::Pointer configuration, bt::BluetoothManager& bluetooth,
                       WifiClient& wifi, QObject* parent = nullptr);
    ~WirelessController() override;

    QVariantMap state() const;
    // Network interface of the running hotspot, empty while there is none
    QString hotspotInterface() const;

    // Starts or stops everything according to the configuration (also after settings changes)
    Q_INVOKABLE void apply();
    void stop();

    // "Pair phone" button: switches wireless Android Auto on if needed, then makes OpenHU visible
    Q_INVOKABLE void pair();
    // use this phone for wireless Android Auto and connect it
    Q_INVOKABLE void connectDevice(const QString& address);
    Q_INVOKABLE void enableWifi();
    // Known WiFi networks (WifiClient). When the hotspot and the client share the only card, the "Always on"
    // hotspot makes room: for a search 5 minutes, for a network joined (by hand or by itself) as long as that
    // connection lasts. Never during wireless Android Auto; a phone that asks for it gets the hotspot back.
    // Empty when started, otherwise why not.
    Q_INVOKABLE QString searchNetworks();
    Q_INVOKABLE QString addNetwork(const QString& ssid, const QString& password, bool hidden = false);
    Q_INVOKABLE QString connectNetwork(const QString& uuid);

signals:
    void changed();

private:
    void startBluetooth();
    void stopBluetooth();
    void closeStaleHeadsets();
    void connectPhones();
    void onHandshakeConnection(const QString& device, int fd);
    void startHandshake(const QString& device, int fd);
    bool hotspotOnDemand() const;
    // on demand: hotspot down after a while without wireless session
    void updateIdle();
    void onHeadsetConnection(const QString& device, int fd);
    void closeHeadset(const QString& device);
    void setPhase(const QString& phase, const QString& detail = QString());
    void onProjectionState(bool active, bool endedByUser);
    // wireless session ended or the phone got stuck: reconnect Bluetooth so it starts over
    void restartPhoneLink();
    QString ssid() const;
    void publish();
    // the hotspot and the WiFi client would need the same card
    bool sharedCard() const;
    // wireless Android Auto needs a WiFi card and a Bluetooth adapter (until the cards are known: assumed there)
    bool wifiPresent() const;
    // the hotspot leaves the card to the client ("search" or "client"); empty when the card is free
    QString makeRoom(const QString& reason);
    // the hotspot back (the client left, the search is over, a phone asks)
    void endRoom();
    // after the hotspot left the card: the client may use it
    void whenCardFree(bool hotspotWasUp, std::function<void()> next);

    configuration::IConfiguration::Pointer configuration_;
    bt::BluetoothManager& bluetooth_;
    WifiClient& wifi_;
    bool running_ = false;
    QString bluetoothError_;
    Hotspot hotspot_;
    BluezProfile handshakeProfile_;
    BluezProfile headsetProfile_;
    QTimer connectTimer_;
    // no TCP connection after the WiFi handshake
    QTimer stuckTimer_;
    QString phaseDevicePath_;
    // phone waits on its RFCOMM channel until the on-demand hotspot is up
    QString pendingDevice_;
    int pendingFd_ = -1;
    QTimer idleTimer_;
    // the hotspot makes room for the client: "search" (searchTimer_) or "client" (until it leaves), "" not
    QString roomFor_;
    QTimer searchTimer_;
    bool projectionActive_ = false;
    bool usbProjection_ = false;
    // Android Auto was ended on the phone: wait until the phone starts it again or a phone is connected by hand
    bool endedByUser_ = false;
    QString phase_ = "idle";
    QString phaseDetail_;
    QString phaseDevice_;
    std::unique_ptr<WifiHandshake> handshake_;
    std::map<QString, std::pair<int, QSocketNotifier*>> headsets_;
    // false when another program (PipeWire) offers hands-free: then the phone is connected normally
    bool useHeadset_ = true;
};

}
}
}
}
