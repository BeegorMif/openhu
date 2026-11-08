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

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>
#include <f1x/openauto/autoapp/Wireless/Bluez.hpp>

class QSocketNotifier;
class QTimer;
class QWebSocket;

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace api
{

// The API over Bluetooth for the companion app (openhu-companion, openhu#158).
//
// The app listens on an RFCOMM service (cUuid). When Android Auto starts, the phone tells OpenHU its Bluetooth address
// (bluetooth.phone), and the bridge connects to the app on exactly that phone (BlueZ ConnectProfile, a few attempts:
// the app's service starts with the session). The link carries the WebSocket API's JSON messages, one per line.
// Each link becomes a client of the local ApiServer with the origin cOrigin, so not a local client. The bridge
// logs it in with the token: the bonded link to the phone of the session is the trust.
//
// The app's first line is its hello ({"type":"hello","app","version","label","protocol","features"}). The bridge
// answers it and publishes companion.state ({connected, app, version, label}; {connected: false} when the link ends).
//
// RFCOMM carries roughly 100–200 KB/s. Writes never block (an output buffer per link, which is dropped above
// cMaxOutput). Android Auto's own media state (source androidauto) is not sent: the app does not show it, and it
// comes every second with the full cover.
class CompanionBridge: public QObject
{
    Q_OBJECT

public:
    static const QString cUuid;
    static const QString cOrigin;
    static constexpr int cMaxLine = 4 * 1024 * 1024;
    static constexpr int cMaxOutput = 8 * 1024 * 1024;

    CompanionBridge(quint16 apiPort, std::function<QString()> token, QObject* parent = nullptr);
    ~CompanionBridge() override;

    // Registers the profile with BlueZ and follows the Android Auto phone. Returns an error message, empty on success.
    QString start();

    // A link to the app (from BlueZ, or a socket pair in the tests). The bridge owns fd from now on.
    void adopt(const QString& device, int fd);

    // Whether an API message goes to the app (not Android Auto's media state)
    static bool forwardToPhone(const QString& message);

private:
    struct Link
    {
        int fd = -1;
        QSocketNotifier* readable = nullptr;
        QSocketNotifier* writable = nullptr;  // on while output waits for room in the socket
        QWebSocket* socket = nullptr;
        QByteArray input;               // from the phone, up to the next newline
        QByteArray output;              // to the phone, not written yet
        QList<QByteArray> pending;      // lines before the WebSocket is open
        bool announced = false;         // its hello came
    };

    void connectTo(const QString& address);
    void tryConnect();
    void onReadable(const QString& device);
    void onLine(const QString& device, const QByteArray& line);
    bool onHello(const QString& device, const QByteArray& line);
    void writeLine(const QString& device, const QByteArray& line);
    void flushOutput(const QString& device);
    void close(const QString& device);

    quint16 apiPort_;
    std::function<QString()> token_;
    wireless::BluezProfile profile_;
    QHash<QString, Link> links_;
    QString phone_;  // the Android Auto phone, empty without a session
    int attempts_ = 0;
    QTimer* retry_;
};

}
}
}
}
