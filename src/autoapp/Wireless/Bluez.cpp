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
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <f1x/openauto/autoapp/Wireless/Bluez.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace bluez
{

const QString cService = QStringLiteral("org.bluez");
const QString cAdapter = QStringLiteral("org.bluez.Adapter1");
const QString cDevice = QStringLiteral("org.bluez.Device1");
const QString cAndroidAutoUuid = QStringLiteral("4de17a00-52cb-11e6-bdf4-0800200c9a66");
const QString cHeadsetUuid = QStringLiteral("00001108-0000-1000-8000-00805f9b34fb");
const QString cHeadsetGatewayUuid = QStringLiteral("00001112-0000-1000-8000-00805f9b34fb");
const QString cHandsfreeUuid = QStringLiteral("0000111e-0000-1000-8000-00805f9b34fb");
const QString cAudioSinkUuid = QStringLiteral("0000110b-0000-1000-8000-00805f9b34fb");

void registerTypes()
{
    qDBusRegisterMetaType<BluezInterfaces>();
    qDBusRegisterMetaType<BluezObjects>();
}

BluezObjects managedObjects()
{
    QDBusMessage message = QDBusMessage::createMethodCall(cService, "/", "org.freedesktop.DBus.ObjectManager",
                                                          "GetManagedObjects");
    QDBusReply<BluezObjects> reply = QDBusConnection::systemBus().call(message, QDBus::Block, 5000);
    if(!reply.isValid())
    {
        OPENAUTO_LOG(warning) << "[Bluez] GetManagedObjects: " << reply.error().message().toStdString();
        return {};
    }
    return reply.value();
}

QString call(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(cService, path, interface, method);
    message.setArguments(arguments);
    const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 10000);
    return reply.type() == QDBusMessage::ErrorMessage ? reply.errorMessage() : QString();
}

QString setProperty(const QString& path, const QString& interface, const QString& name, const QVariant& value)
{
    return call(path, "org.freedesktop.DBus.Properties", "Set",
                {interface, name, QVariant::fromValue(QDBusVariant(value))});
}

void callAsync(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(cService, path, interface, method);
    message.setArguments(arguments);
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 30000));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [path, method](QDBusPendingCallWatcher* call) {
        if(call->isError())
        {
            OPENAUTO_LOG(info) << "[Bluez] " << method.toStdString() << " " << path.toStdString() << ": "
                               << call->error().message().toStdString();
        }
        call->deleteLater();
    });
}

}

BluezProfile::BluezProfile(const QString& path, QObject* parent)
    : QObject(parent)
    , path_(path)
{
}

BluezProfile::~BluezProfile()
{
    this->unregisterProfile();
}

QString BluezProfile::registerProfile(const QString& uuid, const QVariantMap& options)
{
    if(registered_)
    {
        return QString();
    }

    auto bus = QDBusConnection::systemBus();
    if(!bus.registerObject(path_, this, QDBusConnection::ExportAllSlots))
    {
        return tr("D-Bus object %1 is taken").arg(path_);
    }

    const QString error = bluez::call("/org/bluez", "org.bluez.ProfileManager1", "RegisterProfile",
                                      {QVariant::fromValue(QDBusObjectPath(path_)), uuid, options});
    if(!error.isEmpty())
    {
        bus.unregisterObject(path_);
        return error;
    }
    registered_ = true;
    return QString();
}

void BluezProfile::unregisterProfile()
{
    if(!registered_)
    {
        return;
    }
    registered_ = false;
    bluez::call("/org/bluez", "org.bluez.ProfileManager1", "UnregisterProfile",
                {QVariant::fromValue(QDBusObjectPath(path_))});
    QDBusConnection::systemBus().unregisterObject(path_);
}

void BluezProfile::Release()
{
    registered_ = false;
}

void BluezProfile::NewConnection(const QDBusObjectPath& device, const QDBusUnixFileDescriptor& fd, const QVariantMap&)
{
    // QDBusUnixFileDescriptor closes its copy, the receiver gets its own
    const int own = ::dup(fd.fileDescriptor());
    OPENAUTO_LOG(info) << "[BluezProfile] " << path_.toStdString() << ": connection from " << device.path().toStdString();
    if(own >= 0)
    {
        emit connected(device.path(), own);
    }
}

void BluezProfile::RequestDisconnection(const QDBusObjectPath& device)
{
    emit disconnectRequested(device.path());
}

namespace
{
const QString cAgentPath = QStringLiteral("/org/openhu/agent");
}

PairingAgent::PairingAgent(QObject* parent)
    : QObject(parent)
{
}

PairingAgent::~PairingAgent()
{
    this->unregisterAgent();
}

QString PairingAgent::registerAgent()
{
    if(registered_)
    {
        return QString();
    }
    auto bus = QDBusConnection::systemBus();
    if(!bus.registerObject(cAgentPath, this, QDBusConnection::ExportAllSlots))
    {
        return tr("D-Bus object %1 is taken").arg(cAgentPath);
    }
    QString error = bluez::call("/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
                                {QVariant::fromValue(QDBusObjectPath(cAgentPath)), QStringLiteral("DisplayYesNo")});
    if(error.isEmpty())
    {
        error = bluez::call("/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
                            {QVariant::fromValue(QDBusObjectPath(cAgentPath))});
    }
    if(!error.isEmpty())
    {
        bus.unregisterObject(cAgentPath);
        return error;
    }
    registered_ = true;
    return QString();
}

void PairingAgent::unregisterAgent()
{
    if(pending_.type() == QDBusMessage::MethodCallMessage)
    {
        this->answer(false);
    }
    if(!registered_)
    {
        return;
    }
    registered_ = false;
    bluez::call("/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent",
                {QVariant::fromValue(QDBusObjectPath(cAgentPath))});
    QDBusConnection::systemBus().unregisterObject(cAgentPath);
}

void PairingAgent::answer(bool accept)
{
    if(pending_.type() != QDBusMessage::MethodCallMessage)
    {
        return;
    }
    QDBusConnection::systemBus().send(accept ? pending_.createReply()
                                             : pending_.createErrorReply("org.bluez.Error.Rejected", "Rejected"));
    pending_ = QDBusMessage();
    emit finished();
}

void PairingAgent::ask(const QDBusObjectPath& device, const QString& passkey)
{
    // a second request replaces an unanswered one
    this->answer(false);
    setDelayedReply(true);
    pending_ = message();
    emit confirmationRequested(device.path(), passkey);
}

void PairingAgent::Release()
{
    registered_ = false;
}

void PairingAgent::setPinProvider(std::function<QString(const QString& device)> provider)
{
    pinProvider_ = std::move(provider);
}

QString PairingAgent::RequestPinCode(const QDBusObjectPath& device)
{
    const QString pin = pinProvider_ ? pinProvider_(device.path()) : QString();
    if(pin.isEmpty())
    {
        sendErrorReply("org.bluez.Error.Rejected", "PIN entry not supported");
    }
    return pin;
}

void PairingAgent::DisplayPinCode(const QDBusObjectPath&, const QString&)
{
}

quint32 PairingAgent::RequestPasskey(const QDBusObjectPath&)
{
    sendErrorReply("org.bluez.Error.Rejected", "Passkey entry not supported");
    return 0;
}

void PairingAgent::DisplayPasskey(const QDBusObjectPath&, quint32, quint16)
{
}

void PairingAgent::RequestConfirmation(const QDBusObjectPath& device, quint32 passkey)
{
    this->ask(device, QString("%1").arg(passkey, 6, 10, QChar('0')));
}

void PairingAgent::RequestAuthorization(const QDBusObjectPath& device)
{
    this->ask(device, QString());
}

void PairingAgent::AuthorizeService(const QDBusObjectPath&, const QString&)
{
    // only asked for devices that are paired but not trusted: they were confirmed already
}

void PairingAgent::Cancel()
{
    pending_ = QDBusMessage();
    emit finished();
}

}
