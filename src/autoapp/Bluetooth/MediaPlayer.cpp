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
#include <f1x/openauto/autoapp/Bluetooth/MediaPlayer.hpp>
#include <f1x/openauto/autoapp/Wireless/Bluez.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::bt
{

namespace
{
const QString cPath = QStringLiteral("/org/openhu/player");
const QString cInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
}

MediaPlayer::MediaPlayer(QObject* parent)
    : QObject(parent)
{
    metadata_ = {{"mpris:trackid", QVariant::fromValue(QDBusObjectPath("/org/openhu/track/0"))},
                 {"xesam:title", QString("OpenHU")}};
}

MediaPlayer::~MediaPlayer()
{
    this->unregister();
}

QVariantMap MediaPlayer::properties() const
{
    return {{"PlaybackStatus", status_}, {"LoopStatus", this->loopStatus()}, {"Shuffle", false},
            {"Rate", 1.0}, {"Metadata", metadata_}, {"Position", position_},
            {"CanGoNext", true}, {"CanGoPrevious", true}, {"CanPlay", true}, {"CanPause", true},
            {"CanSeek", false}, {"CanControl", true}};
}

QString MediaPlayer::registerAt(const QString& adapterPath)
{
    if(!adapterPath_.isEmpty())
    {
        return QString();
    }
    auto bus = QDBusConnection::systemBus();
    if(!bus.registerObject(cPath, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties))
    {
        return qtTrId("bluetooth.d_bus_object_taken").arg(cPath);
    }
    const QString error = wireless::bluez::call(adapterPath, "org.bluez.Media1", "RegisterPlayer",
                                                {QVariant::fromValue(QDBusObjectPath(cPath)), this->properties()});
    if(!error.isEmpty())
    {
        bus.unregisterObject(cPath);
        return error;
    }
    adapterPath_ = adapterPath;
    OPENAUTO_LOG(info) << "[MediaPlayer] registered at " << adapterPath.toStdString();
    return QString();
}

void MediaPlayer::unregister()
{
    if(adapterPath_.isEmpty())
    {
        return;
    }
    wireless::bluez::call(adapterPath_, "org.bluez.Media1", "UnregisterPlayer",
                          {QVariant::fromValue(QDBusObjectPath(cPath))});
    QDBusConnection::systemBus().unregisterObject(cPath);
    adapterPath_.clear();
}

bool MediaPlayer::registered() const
{
    return !adapterPath_.isEmpty();
}

void MediaPlayer::update(const QString& status, const QString& title, const QString& artist, const QString& album,
                         double length, double position)
{
    QVariantMap changed;
    if(status != status_)
    {
        status_ = status;
        changed["PlaybackStatus"] = status_;
    }

    QVariantMap metadata{{"xesam:title", title.isEmpty() ? QString("OpenHU") : title}};
    if(!artist.isEmpty())
    {
        metadata["xesam:artist"] = QStringList{artist};
    }
    if(!album.isEmpty())
    {
        metadata["xesam:album"] = album;
    }
    if(length > 0)
    {
        metadata["mpris:length"] = qlonglong(length * 1000000);
    }
    // a new track id makes the radio refresh its display
    const bool newTrack = metadata.value("xesam:title") != metadata_.value("xesam:title")
                          || metadata.value("xesam:artist") != metadata_.value("xesam:artist");
    static qlonglong track = 0;
    metadata["mpris:trackid"] = newTrack ? QVariant::fromValue(QDBusObjectPath(QString("/org/openhu/track/%1").arg(++track)))
                                         : metadata_.value("mpris:trackid");
    if(metadata != metadata_)
    {
        metadata_ = metadata;
        changed["Metadata"] = metadata_;
    }

    const qlonglong newPosition = qlonglong(position * 1000000);
    if(qAbs(newPosition - position_) >= 1000000)
    {
        changed["Position"] = newPosition;
    }
    position_ = newPosition;

    if(changed.isEmpty() || adapterPath_.isEmpty())
    {
        return;
    }
    QDBusMessage signal = QDBusMessage::createSignal(cPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal.setArguments({cInterface, changed, QStringList()});
    QDBusConnection::systemBus().send(signal);
}

void MediaPlayer::Next()
{
    emit command("next");
}

void MediaPlayer::Previous()
{
    emit command("previous");
}

void MediaPlayer::Pause()
{
    emit command("pause");
}

void MediaPlayer::PlayPause()
{
    emit command("playpause");
}

void MediaPlayer::Stop()
{
    emit command("stop");
}

void MediaPlayer::Play()
{
    emit command("play");
}

void MediaPlayer::Seek(qlonglong)
{
}

void MediaPlayer::SetPosition(const QDBusObjectPath&, qlonglong)
{
}

}
