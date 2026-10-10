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
#include <QWebSocket>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
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

ApiServer::ApiServer(QObject* parent)
    : QObject(parent)
    , server_(QStringLiteral("OpenAuto"), QWebSocketServer::NonSecureMode)
{
    connect(&server_, &QWebSocketServer::newConnection, this, &ApiServer::onNewConnection);
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
    return it.value()(params);
}

void ApiServer::onNewConnection()
{
    while(QWebSocket* socket = server_.nextPendingConnection())
    {
        OPENAUTO_LOG(info) << "[ApiServer] client connected: " << socket->peerAddress().toString().toStdString();
        clients_.insert(socket, Client());
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
    auto& client = clients_[socket];

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
            response["result"] = this->call(request.value("method").toString(), request.value("params").toMap());
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
