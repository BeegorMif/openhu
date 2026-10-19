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

#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace bt
{

// Media player registered at BlueZ (org.bluez.Media1.RegisterPlayer, MPRIS player interface): BlueZ shows
// title/artist/position on the car radio (AVRCP) and forwards its buttons as method calls -> command().
class MediaPlayer: public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus)
    Q_PROPERTY(bool Shuffle READ shuffle)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MediaPlayer(QObject* parent = nullptr);
    ~MediaPlayer() override;

    // Returns an error message, empty on success
    QString registerAt(const QString& adapterPath);
    void unregister();
    bool registered() const;

    // status: "Playing", "Paused", "Stopped"; position and length in seconds
    void update(const QString& status, const QString& title, const QString& artist, const QString& album,
                double length, double position);

    QString playbackStatus() const { return status_; }
    QString loopStatus() const { return "None"; }
    bool shuffle() const { return false; }
    double rate() const { return 1.0; }
    QVariantMap metadata() const { return metadata_; }
    qlonglong position() const { return position_; }
    bool canControl() const { return true; }
    bool canSeek() const { return false; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath& track, qlonglong position);

signals:
    // "next", "previous", "pause", "playpause", "stop", "play"
    void command(const QString& name);

private:
    QVariantMap properties() const;

    QString adapterPath_;
    QString status_ = "Stopped";
    QVariantMap metadata_;
    qlonglong position_ = 0;
};

}
}
}
}
