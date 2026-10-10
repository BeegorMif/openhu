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

#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <functional>
#include <QMap>
#include <QObject>
#include <QVariantMap>

// org.freedesktop.DBus.ObjectManager.GetManagedObjects: a{oa{sa{sv}}}
using BluezInterfaces = QMap<QString, QVariantMap>;
using BluezObjects = QMap<QDBusObjectPath, BluezInterfaces>;
Q_DECLARE_METATYPE(BluezInterfaces)
Q_DECLARE_METATYPE(BluezObjects)

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace wireless
{

// BlueZ over the system D-Bus (works in the container with the host's /run/dbus/system_bus_socket).
namespace bluez
{

extern const QString cService;
extern const QString cAdapter;
extern const QString cDevice;
// Android Auto wireless setup (RFCOMM)
extern const QString cAndroidAutoUuid;
// Headset profile: connecting the phone's audio gateway makes it start wireless Android Auto
extern const QString cHeadsetUuid;
extern const QString cHeadsetGatewayUuid;
extern const QString cHandsfreeUuid;
extern const QString cAudioSinkUuid;

void registerTypes();
BluezObjects managedObjects();
// Returns an error message, empty on success
QString setProperty(const QString& path, const QString& interface, const QString& name, const QVariant& value);
QString call(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments = {});
// Fire and forget (connecting a device takes seconds), errors are logged
void callAsync(const QString& path, const QString& interface, const QString& method, const QVariantList& arguments = {});

}

// org.bluez.Profile1: BlueZ hands over the RFCOMM socket of every connection.
class BluezProfile: public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Profile1")

public:
    BluezProfile(const QString& path, QObject* parent = nullptr);
    ~BluezProfile() override;

    // Exports the object and registers the profile, returns an error message
    QString registerProfile(const QString& uuid, const QVariantMap& options);
    void unregisterProfile();

public slots:
    void Release();
    void NewConnection(const QDBusObjectPath& device, const QDBusUnixFileDescriptor& fd, const QVariantMap& properties);
    void RequestDisconnection(const QDBusObjectPath& device);

signals:
    // fd is a duplicate owned by the receiver
    void connected(const QString& device, int fd);
    void disconnectRequested(const QString& device);

private:
    QString path_;
    bool registered_ = false;
};

// org.bluez.Agent1 for pairing: the passkey is confirmed in the shell (DisplayYesNo).
class PairingAgent: public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    explicit PairingAgent(QObject* parent = nullptr);
    ~PairingAgent() override;

    // Registers as default agent (only while pairing mode is on, so a desktop agent keeps working otherwise)
    QString registerAgent();
    void unregisterAgent();
    void answer(bool accept);
    // PIN for legacy pairing (old car radios), empty = reject
    void setPinProvider(std::function<QString(const QString& device)> provider);

public slots:
    void Release();
    QString RequestPinCode(const QDBusObjectPath& device);
    void DisplayPinCode(const QDBusObjectPath& device, const QString& pincode);
    quint32 RequestPasskey(const QDBusObjectPath& device);
    void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 entered);
    void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey);
    void RequestAuthorization(const QDBusObjectPath& device);
    void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
    void Cancel();

signals:
    // passkey is empty for a plain "pair with this device?" request
    void confirmationRequested(const QString& device, const QString& passkey);
    void finished();

private:
    void ask(const QDBusObjectPath& device, const QString& passkey);

    bool registered_ = false;
    QDBusMessage pending_;
    std::function<QString(const QString& device)> pinProvider_;
};

}
}
}
}
