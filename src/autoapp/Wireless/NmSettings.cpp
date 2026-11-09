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

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <f1x/openauto/autoapp/Wireless/NmSettings.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace
{

const char* cService = "org.freedesktop.NetworkManager";
const char* cSettingsPath = "/org/freedesktop/NetworkManager/Settings";
const char* cSettingsInterface = "org.freedesktop.NetworkManager.Settings";
const char* cConnectionInterface = "org.freedesktop.NetworkManager.Settings.Connection";
const int cTimeoutMs = 10000;

QString errorText(const QDBusError& error)
{
    return error.name() + ": " + error.message();
}

// calls and hands the finished call to reply (on the thread of context)
void call(QObject* context, const QDBusMessage& message, std::function<void(QDBusPendingCallWatcher&)> reply)
{
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, cTimeoutMs), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, reply]() {
        watcher->deleteLater();
        reply(*watcher);
    });
}

}

NmSettings::NmSettings(QObject* parent)
    : QObject(parent)
{
    NmSettings::registerTypes();
}

void NmSettings::registerTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<QVariantMap>();
        qDBusRegisterMetaType<QList<QVariantMap>>();
        qDBusRegisterMetaType<Profile>();
        return true;
    }();
    Q_UNUSED(registered);
}

NmSettings::Profile NmSettings::applied(Profile profile, const QVariantMap& changes)
{
    for(auto change = changes.begin(); change != changes.end(); ++change)
    {
        const QString setting = change.key().section('.', 0, 0);
        const QString property = change.key().section('.', 1);
        if(change.value().isValid())
        {
            profile[setting][property] = change.value();
        }
        else if(profile.contains(setting))
        {
            profile[setting].remove(property);
        }
    }
    return profile;
}

void NmSettings::add(const Profile& profile, Done done)
{
    QDBusMessage message = QDBusMessage::createMethodCall(cService, cSettingsPath, cSettingsInterface, "AddConnection");
    message << QVariant::fromValue(profile);
    call(this, message, [done](QDBusPendingCallWatcher& watcher) {
        done(watcher.isError() ? errorText(watcher.error()) : QString());
    });
}

void NmSettings::find(const QString& uuid, std::function<void(const QString& error, const QString& path)> found)
{
    QDBusMessage message =
        QDBusMessage::createMethodCall(cService, cSettingsPath, cSettingsInterface, "GetConnectionByUuid");
    message << uuid;
    call(this, message, [found](QDBusPendingCallWatcher& watcher) {
        if(watcher.isError())
        {
            found(errorText(watcher.error()), {});
            return;
        }
        found({}, QDBusPendingReply<QDBusObjectPath>(watcher).value().path());
    });
}

void NmSettings::modify(const QString& uuid, const QVariantMap& changes, Done done)
{
    this->find(uuid, [this, changes, done](const QString& error, const QString& path) {
        if(!error.isEmpty())
        {
            done(error);
            return;
        }
        call(this, QDBusMessage::createMethodCall(cService, path, cConnectionInterface, "GetSettings"),
             [this, path, changes, done](QDBusPendingCallWatcher& watcher) {
                 if(watcher.isError())
                 {
                     done(errorText(watcher.error()));
                     return;
                 }
                 const Profile profile = NmSettings::applied(QDBusPendingReply<Profile>(watcher).value(), changes);
                 QDBusMessage update = QDBusMessage::createMethodCall(cService, path, cConnectionInterface, "Update");
                 update << QVariant::fromValue(profile);
                 call(this, update, [done](QDBusPendingCallWatcher& updated) {
                     done(updated.isError() ? errorText(updated.error()) : QString());
                 });
             });
    });
}

void NmSettings::replace(const QString& uuid, const Profile& profile, Done done)
{
    this->find(uuid, [this, profile, done](const QString& error, const QString& path) {
        if(!error.isEmpty())
        {
            done(error);
            return;
        }
        QDBusMessage update = QDBusMessage::createMethodCall(cService, path, cConnectionInterface, "Update");
        update << QVariant::fromValue(profile);
        call(this, update, [done](QDBusPendingCallWatcher& watcher) {
            done(watcher.isError() ? errorText(watcher.error()) : QString());
        });
    });
}

}
