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

#include <f1x/openauto/autoapp/Api/CompanionBridge.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>
#include <QDBusObjectPath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace api
{

const QString CompanionBridge::cUuid = QStringLiteral("57b07c95-708d-46ae-a658-9f296dfd034c");
const QString CompanionBridge::cOrigin = QStringLiteral("openhu-companion");

namespace
{
constexpr int cAttempts = 6;
constexpr int cRetryMs = 5000;
const QString cProfilePath = QStringLiteral("/org/openhu/companion");

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

CompanionBridge::CompanionBridge(quint16 apiPort, std::function<QString()> token, QObject* parent)
    : QObject(parent)
    , apiPort_(apiPort)
    , token_(std::move(token))
    , profile_(cProfilePath)
    , retry_(new QTimer(this))
{
    connect(&profile_, &wireless::BluezProfile::connected, this, &CompanionBridge::adopt);
    connect(&profile_, &wireless::BluezProfile::disconnectRequested, this, &CompanionBridge::close);
    retry_->setInterval(cRetryMs);
    connect(retry_, &QTimer::timeout, this, &CompanionBridge::tryConnect);
}

CompanionBridge::~CompanionBridge()
{
    for(const QString& device: links_.keys())
    {
        this->close(device);
    }
}

QString CompanionBridge::start()
{
    // client role: OpenHU connects to the app's service (SDP finds its channel), nothing listens here
    const QString error = profile_.registerProfile(cUuid, {{"Name", "OpenHU companion"},
                                                           {"Role", "client"},
                                                           {"RequireAuthentication", true},
                                                           {"RequireAuthorization", false}});
    if(!error.isEmpty())
    {
        OPENAUTO_LOG(info) << "[CompanionBridge] no companion app: " << error.toStdString();
        return error;
    }
    connect(&core::EventBus::instance(), &core::EventBus::event, this,
            [this](const QString& topic, const QVariantMap& data) {
                if(topic == QLatin1String("bluetooth.phone"))
                {
                    this->connectTo(data.value("address").toString());
                }
                else if(topic == QLatin1String("projection.state") && !data.value("connected").toBool())
                {
                    phone_.clear();
                    retry_->stop();
                }
            });
    return QString();
}

void CompanionBridge::connectTo(const QString& address)
{
    phone_ = address.toUpper();
    attempts_ = 0;
    this->tryConnect();
    retry_->start();
}

void CompanionBridge::tryConnect()
{
    if(phone_.isEmpty() || !links_.isEmpty() || ++attempts_ > cAttempts)
    {
        retry_->stop();
        return;
    }
    const auto objects = wireless::bluez::managedObjects();
    for(auto it = objects.constBegin(); it != objects.constEnd(); ++it)
    {
        const QVariantMap device = it.value().value(QStringLiteral("org.bluez.Device1"));
        if(device.value("Address").toString().toUpper() == phone_)
        {
            // fails while the app's service is not up yet (or the app is not installed): tried again
            OPENAUTO_LOG(debug) << "[CompanionBridge] connecting to " << phone_.toStdString() << ", attempt "
                                << attempts_;
            wireless::bluez::callAsync(it.key().path(), QStringLiteral("org.bluez.Device1"),
                                       QStringLiteral("ConnectProfile"), {cUuid});
            return;
        }
    }
    retry_->stop();  // not paired with this head unit
}

void CompanionBridge::adopt(const QString& device, int fd)
{
    this->close(device);
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    Link& link = links_[device];
    link.fd = fd;
    link.readable = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(link.readable, &QSocketNotifier::activated, this, [this, device]() { this->onReadable(device); });
    link.writable = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    link.writable->setEnabled(false);
    connect(link.writable, &QSocketNotifier::activated, this, [this, device]() { this->flushOutput(device); });

    link.socket = new QWebSocket(cOrigin, QWebSocketProtocol::VersionLatest, this);
    connect(link.socket, &QWebSocket::connected, this, [this, device]() {
        auto it = links_.find(device);
        if(it == links_.end())
        {
            return;
        }
        it->socket->sendTextMessage(QString::fromUtf8(compact({{"type", "auth"}, {"token", token_()}})));
        for(const QByteArray& line: it->pending)
        {
            it->socket->sendTextMessage(QString::fromUtf8(line));
        }
        it->pending.clear();
    });
    connect(link.socket, &QWebSocket::textMessageReceived, this, [this, device](const QString& message) {
        if(forwardToPhone(message))
        {
            this->writeLine(device, message.toUtf8());
        }
    });
    connect(link.socket, &QWebSocket::disconnected, this, [this, device]() { this->close(device); });
    link.socket->open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(apiPort_)));
    OPENAUTO_LOG(info) << "[CompanionBridge] companion app connected: " << device.toStdString();
}

bool CompanionBridge::forwardToPhone(const QString& message)
{
    // cheap test first: most messages are not about Android Auto's media
    if(!message.contains(QLatin1String("androidauto")))
    {
        return true;
    }
    const QJsonObject object = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString topic = object.value("topic").toString();
    const bool media = topic == QLatin1String("media.current") || topic == QLatin1String("media.metadata")
                       || topic == QLatin1String("media.status");
    return !(media && object.value("data").toObject().value("source").toString() == QLatin1String("androidauto"));
}

void CompanionBridge::onReadable(const QString& device)
{
    auto it = links_.find(device);
    if(it == links_.end())
    {
        return;
    }
    char buffer[4096];
    for(;;)
    {
        const ssize_t n = ::read(it->fd, buffer, sizeof(buffer));
        if(n > 0)
        {
            it->input.append(buffer, qsizetype(n));
            continue;
        }
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;
        }
        this->close(device);  // end of the stream or an error
        return;
    }
    QList<QByteArray> lines;
    qsizetype newline;
    while((newline = it->input.indexOf('\n')) >= 0)
    {
        lines.append(it->input.left(newline).trimmed());
        it->input.remove(0, newline + 1);
    }
    if(it->input.size() > cMaxLine)
    {
        OPENAUTO_LOG(info) << "[CompanionBridge] line too long, closing " << device.toStdString();
        this->close(device);
        return;
    }
    for(const QByteArray& line: lines)
    {
        if(!line.isEmpty())
        {
            this->onLine(device, line);  // may close the link
        }
    }
}

void CompanionBridge::onLine(const QString& device, const QByteArray& line)
{
    if(this->onHello(device, line))
    {
        return;
    }
    auto it = links_.find(device);
    if(it == links_.end())
    {
        return;
    }
    if(it->socket->state() == QAbstractSocket::ConnectedState)
    {
        it->socket->sendTextMessage(QString::fromUtf8(line));
    }
    else
    {
        it->pending.append(line);
    }
}

bool CompanionBridge::onHello(const QString& device, const QByteArray& line)
{
    if(!line.contains("\"hello\""))
    {
        return false;
    }
    const QJsonObject hello = QJsonDocument::fromJson(line).object();
    auto it = links_.find(device);
    if(hello.value("type").toString() != QLatin1String("hello") || it == links_.end())
    {
        return false;
    }
    it->announced = true;
    OPENAUTO_LOG(info) << "[CompanionBridge] " << hello.value("app").toString().toStdString() << " "
                       << hello.value("version").toString().toStdString() << " (\""
                       << hello.value("label").toString().toStdString() << "\")";
    this->writeLine(device, compact({{"type", "hello"}, {"openhu", 1}}));
    core::EventBus::instance().publish("companion.state", {{"connected", true},
                                                           {"app", hello.value("app").toString()},
                                                           {"version", hello.value("version").toString()},
                                                           {"label", hello.value("label").toString()},
                                                           {"mediaLabels", hello.value("mediaLabels").toVariant()}});
    return true;
}

void CompanionBridge::writeLine(const QString& device, const QByteArray& line)
{
    auto it = links_.find(device);
    if(it == links_.end())
    {
        return;
    }
    it->output.append(line);
    it->output.append('\n');
    if(it->output.size() > cMaxOutput)
    {
        OPENAUTO_LOG(info) << "[CompanionBridge] the phone does not read, closing " << device.toStdString();
        this->close(device);
        return;
    }
    this->flushOutput(device);
}

void CompanionBridge::flushOutput(const QString& device)
{
    auto it = links_.find(device);
    if(it == links_.end())
    {
        return;
    }
    while(!it->output.isEmpty())
    {
        const ssize_t n = ::write(it->fd, it->output.constData(), size_t(it->output.size()));
        if(n > 0)
        {
            it->output.remove(0, qsizetype(n));
            continue;
        }
        if(n < 0 && errno == EINTR)
        {
            continue;
        }
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;  // the socket is full: on when it has room
        }
        this->close(device);
        return;
    }
    it->writable->setEnabled(!it->output.isEmpty());
}

void CompanionBridge::close(const QString& device)
{
    auto it = links_.find(device);
    if(it == links_.end())
    {
        return;
    }
    const Link link = *it;
    links_.erase(it);
    delete link.readable;
    delete link.writable;
    if(link.socket != nullptr)
    {
        link.socket->disconnect(this);
        link.socket->close();
        link.socket->deleteLater();
    }
    ::close(link.fd);
    OPENAUTO_LOG(info) << "[CompanionBridge] companion app disconnected: " << device.toStdString();
    if(link.announced)
    {
        core::EventBus::instance().publish("companion.state", {{"connected", false}});
    }
}

}
}
}
}
