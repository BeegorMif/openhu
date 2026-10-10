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

#include <QByteArray>
#include <QObject>
#include <QSocketNotifier>
#include <google/protobuf/message.h>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace wireless
{

// Wireless Android Auto setup on the phone's RFCOMM connection: tells the phone where to connect
// (WifiStartRequest), answers the WifiInfoRequest with the hotspot credentials and reports the result.
// Frames: 2 bytes payload length, 2 bytes message id (big endian), protobuf payload.
class WifiHandshake: public QObject
{
    Q_OBJECT

public:
    struct Network
    {
        QString ssid;
        QString password;
        QString bssid;
        QString address;
        quint16 port;
    };

    // Takes ownership of fd
    WifiHandshake(int fd, Network network, QObject* parent = nullptr);
    ~WifiHandshake() override;

signals:
    // "info" (credentials sent), "connecting" (phone joins the WiFi), "failed" (detail = reason)
    void progress(const QString& step, const QString& detail);
    void closed();

private:
    void onReadable();
    void handle(quint16 id, const QByteArray& payload);
    void send(quint16 id, const google::protobuf::Message& message);

    int fd_;
    Network network_;
    QSocketNotifier notifier_;
    QByteArray buffer_;
};

}
}
}
}
