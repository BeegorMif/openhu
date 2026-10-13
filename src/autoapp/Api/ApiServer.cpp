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

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketCorsAuthenticator>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Api/ClientFilter.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace api
{

namespace
{

// a client that needs the token and does not send it in time is disconnected
constexpr int cLoginTimeoutMs = 10000;

}

ApiServer::ApiServer(QObject* parent)
    : QObject(parent)
    , server_(QStringLiteral("OpenHU"), QWebSocketServer::NonSecureMode)
{
    connect(&server_, &QWebSocketServer::newConnection, this, &ApiServer::onNewConnection);
    connect(&server_, &QWebSocketServer::originAuthenticationRequired, this, &ApiServer::onOriginAuthentication);
    connect(&core::EventBus::instance(), &core::EventBus::event, this, &ApiServer::onBusEvent);
}

ApiServer::~ApiServer()
{
    server_.close();
    qDeleteAll(clients_.keys());
}

bool ApiServer::listen(const QHostAddress& address, quint16 port)
{
    if(!server_.listen(address, port))
    {
        OPENAUTO_LOG(error) << "[ApiServer] cannot listen on " << address.toString().toStdString() << ":" << port
                            << ": " << server_.errorString().toStdString();
        return false;
    }

    OPENAUTO_LOG(info) << "[ApiServer] listening on ws://" << address.toString().toStdString() << ":" << port;
    return true;
}

void ApiServer::registerMethod(const QString& name, Method method)
{
    methods_.insert(name, std::move(method));
}

QStringList ApiServer::methods() const
{
    return methods_.keys();
}

QVariant ApiServer::call(const QString& method, const QVariantMap& params)
{
    auto it = methods_.find(method);
    if(it == methods_.end())
    {
        throw ApiError("unknown method: " + method.toStdString());
    }
    if(!params.contains(cLocal))
    {
        QVariantMap local = params;
        local.insert(cLocal, true);
        return it.value()(local);
    }
    return it.value()(params);
}

void ApiServer::setClientFilter(std::function<bool(const QHostAddress&)> filter)
{
    clientFilter_ = std::move(filter);
}

void ApiServer::setToken(const QString& token)
{
    if(token == token_)
    {
        return;
    }
    token_ = token;
    // logged in with the old token; collected first, closing may remove them from clients_ right away
    QList<QWebSocket*> loggedIn;
    for(auto it = clients_.begin(); it != clients_.end(); ++it)
    {
        if(it.value().authenticated && !it.value().local)
        {
            it.value().authenticated = false;
            loggedIn << it.key();
        }
    }
    for(QWebSocket* socket: loggedIn)
    {
        this->refuse(socket, "token changed");
    }
}

void ApiServer::setAllowedOrigins(const QStringList& origins)
{
    allowedOrigins_ = origins;
}

void ApiServer::onOriginAuthentication(QWebSocketCorsAuthenticator* authenticator)
{
    const bool allowed = originAllowed(authenticator->origin(), allowedOrigins_);
    if(!allowed)
    {
        OPENAUTO_LOG(info) << "[ApiServer] origin refused: " << authenticator->origin().toStdString();
    }
    authenticator->setAllowed(allowed);
}

void ApiServer::onNewConnection()
{
    while(QWebSocket* socket = server_.nextPendingConnection())
    {
        if(clientFilter_ && !clientFilter_(socket->peerAddress()))
        {
            OPENAUTO_LOG(info) << "[ApiServer] client refused: " << socket->peerAddress().toString().toStdString();
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "not allowed from this network");
            socket->deleteLater();
            continue;
        }
        // browsers send an Origin header: a web page must not get the rights of a program on this device
        Client client;
        client.local = isLoopbackPeer(socket->peerAddress()) && socket->origin().isEmpty();
        client.authenticated = client.local;
        const QString token = handshakeToken(socket->request());
        if(!client.authenticated && !token.isEmpty())
        {
            if(!tokenMatches(token, token_))
            {
                this->refuse(socket, "invalid token");
                socket->deleteLater();
                continue;
            }
            client.authenticated = true;
        }

        OPENAUTO_LOG(info) << "[ApiServer] client connected: " << socket->peerAddress().toString().toStdString()
                           << (client.local ? "" : client.authenticated ? " (token)" : " (waiting for the token)");
        clients_.insert(socket, client);
        if(!client.authenticated)
        {
            QTimer::singleShot(cLoginTimeoutMs, socket, [this, socket]() {
                auto it = clients_.find(socket);
                if(it != clients_.end() && !it.value().authenticated)
                {
                    this->refuse(socket, "no token");
                }
            });
        }
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString& message) {
            this->onTextMessage(socket, message);
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket]() { this->onDisconnected(socket); });
    }
}

void ApiServer::onTextMessage(QWebSocket* socket, const QString& message)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if(!document.isObject())
    {
        this->send(socket, {{"type", "error"}, {"error", "invalid JSON: " + parseError.errorString()}});
        return;
    }

    const QVariantMap request = document.object().toVariantMap();
    const QString type = request.value("type", request.contains("method") ? "request" : "").toString();
    auto clientIt = clients_.find(socket);
    if(clientIt == clients_.end())
    {
        return;
    }
    auto& client = clientIt.value();

    if(type == "auth")
    {
        this->onAuth(socket, client, request);
        return;
    }
    if(!client.authenticated)
    {
        const QString error = QStringLiteral("authentication required: send the API token (docs/api.md)");
        if(type == "request")
        {
            this->send(socket, {{"type", "response"}, {"id", request.value("id")}, {"error", error}});
        }
        else
        {
            this->send(socket, {{"type", "error"}, {"error", error}});
        }
        return;
    }

    if(type == "subscribe")
    {
        const QStringList topics = request.value("topics").toStringList();
        for(const auto& topic: topics)
        {
            if(!client.topics.contains(topic))
            {
                client.topics.append(topic);
            }
        }

        // Current state right away, so clients do not have to wait for the next change.
        const auto snapshot = core::EventBus::instance().snapshot();
        for(auto it = snapshot.cbegin(); it != snapshot.cend(); ++it)
        {
            for(const auto& topic: topics)
            {
                if(core::EventBus::matches(topic, it.key()))
                {
                    this->send(socket, {{"type", "event"}, {"topic", it.key()}, {"data", it.value()}});
                    break;
                }
            }
        }
    }
    else if(type == "unsubscribe")
    {
        for(const auto& topic: request.value("topics").toStringList())
        {
            client.topics.removeAll(topic);
        }
    }
    else if(type == "request")
    {
        QVariantMap response{{"type", "response"}, {"id", request.value("id")}};
        try
        {
            QVariantMap params = request.value("params").toMap();
            params.insert(cLocal, client.local);
            response["result"] = this->call(request.value("method").toString(), params);
        }
        catch(const std::exception& e)
        {
            response["error"] = QString::fromStdString(e.what());
        }
        this->send(socket, response);
    }
    else
    {
        this->send(socket, {{"type", "error"}, {"error", "unknown message type: " + type}});
    }
}

void ApiServer::onAuth(QWebSocket* socket, Client& client, const QVariantMap& request)
{
    if(!client.authenticated)
    {
        if(!tokenMatches(request.value("token").toString(), token_))
        {
            this->send(socket, {{"type", "error"}, {"error", "invalid token"}});
            this->refuse(socket, "invalid token");
            return;
        }
        client.authenticated = true;
        OPENAUTO_LOG(info) << "[ApiServer] client logged in: " << socket->peerAddress().toString().toStdString();
    }
    this->send(socket, {{"type", "auth"}, {"authenticated", true}});
}

void ApiServer::refuse(QWebSocket* socket, const QString& reason)
{
    OPENAUTO_LOG(info) << "[ApiServer] client refused (" << reason.toStdString()
                       << "): " << socket->peerAddress().toString().toStdString();
    socket->close(QWebSocketProtocol::CloseCodePolicyViolated, reason);
}

void ApiServer::onDisconnected(QWebSocket* socket)
{
    clients_.remove(socket);
    socket->deleteLater();
}

void ApiServer::onBusEvent(const QString& topic, const QVariantMap& data)
{
    for(auto it = clients_.cbegin(); it != clients_.cend(); ++it)
    {
        if(subscribed(it.value(), topic))
        {
            this->send(it.key(), {{"type", "event"}, {"topic", topic}, {"data", data}});
        }
    }
}

void ApiServer::send(QWebSocket* socket, const QVariantMap& message)
{
    socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(message)).toJson(QJsonDocument::Compact)));
}

bool ApiServer::subscribed(const Client& client, const QString& topic)
{
    for(const auto& pattern: client.topics)
    {
        if(core::EventBus::matches(pattern, topic))
        {
            return true;
        }
    }
    return false;
}

}
}
}
}
