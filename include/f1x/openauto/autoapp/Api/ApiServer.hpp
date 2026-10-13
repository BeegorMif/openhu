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

#include <functional>
#include <stdexcept>
#include <QHash>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <QWebSocketServer>

class QWebSocket;
class QWebSocketCorsAuthenticator;

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace api
{

// Thrown by methods to answer a request with an error.
class ApiError: public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// WebSocket/JSON API (see docs/api.md):
//   -> {"type":"auth","token":"..."}                           <- {"type":"auth","authenticated":true}
//   -> {"type":"subscribe","topics":["nav.*","media.*"]}       <- {"type":"event","topic":"nav.turn","data":{...}}
//   -> {"type":"request","id":1,"method":"volume.set","params":{"value":40}}
//   <- {"type":"response","id":1,"result":{...}}  or  {"type":"response","id":1,"error":"..."}
class ApiServer: public QObject
{
    Q_OBJECT

public:
    using Method = std::function<QVariant(const QVariantMap& params)>;
    // Set by the server in every method's params: true for clients on this device (loopback, not a browser) and
    // in-process callers, false for clients from a network and browsers. A client cannot set it.
    static constexpr const char* cLocal = "_local";

    explicit ApiServer(QObject* parent = nullptr);
    ~ApiServer() override;

    bool listen(const QHostAddress& address, quint16 port);
    // Decides per new connection whether a client may connect (e.g. not from the wireless hotspot)
    void setClientFilter(std::function<bool(const QHostAddress&)> filter);
    // Token (Api.Token) that clients from the network and browsers (Origin header) have to send; clients on this
    // device without an Origin header (plugins, tools) need none. A new token disconnects the clients that logged
    // in with the old one. While it is empty, such clients cannot log in.
    void setToken(const QString& token);
    // Origins browsers may connect from (Api.AllowedOrigins), "*" for any
    void setAllowedOrigins(const QStringList& origins);
    void registerMethod(const QString& name, Method method);
    QStringList methods() const;

    // Same as a request over the socket for in-process callers. Throws ApiError.
    QVariant call(const QString& method, const QVariantMap& params = QVariantMap());

private:
    struct Client
    {
        QStringList topics;
        bool local = false;          // on this device and not a browser: no token needed, gets secrets
        bool authenticated = false;  // local or logged in with the token
    };

    void onOriginAuthentication(QWebSocketCorsAuthenticator* authenticator);
    void onNewConnection();
    void onAuth(QWebSocket* socket, Client& client, const QVariantMap& request);
    void refuse(QWebSocket* socket, const QString& reason);
    void onTextMessage(QWebSocket* socket, const QString& message);
    void onDisconnected(QWebSocket* socket);
    void onBusEvent(const QString& topic, const QVariantMap& data);
    void send(QWebSocket* socket, const QVariantMap& message);
    static bool subscribed(const Client& client, const QString& topic);

    QWebSocketServer server_;
    QHash<QWebSocket*, Client> clients_;
    QMap<QString, Method> methods_;
    std::function<bool(const QHostAddress&)> clientFilter_;
    QString token_;
    QStringList allowedOrigins_{QStringLiteral("*")};
};

}
}
}
}
