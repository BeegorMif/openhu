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

#include <QHash>
#include <QString>
#include <QVariantMap>
#include <aap_protobuf/service/carlocalmedia/message/CarLocalMediaPlaybackStatus.pb.h>
#include <aap_protobuf/service/carlocalmedia/message/CarLocalMediaPlaybackMetadata.pb.h>
#include <aap_protobuf/service/carlocalmedia/message/CarLocalMediaPlaybackAction.pb.h>

namespace f1x::openauto::autoapp::service::carlocalmedia
{

namespace msg = aap_protobuf::service::carlocalmedia::message;

// the largest cover the phone gets (px per side); larger ones are scaled down (JPEG)
constexpr int cMaxCoverSize = 512;

// media.status of a local source -> the phone's status: playing/paused/stopped, the source's name, the position and
// the buttons (play, pause, previous, next)
msg::CarLocalMediaPlaybackStatus playbackStatus(const QVariantMap& status, const QString& name);
// media.metadata -> title, artist, album, duration and the cover (data URL -> image bytes, at most cMaxCoverSize)
msg::CarLocalMediaPlaybackMetadata playbackMetadata(const QVariantMap& metadata);
// the image bytes of a data URL, scaled down to maxSize per side when larger; empty when it is no image
std::string coverBytes(const QString& dataUrl, int maxSize = cMaxCoverSize);
// a button in the phone's media card -> the media action for the source ("media.play", ...)
QString mediaAction(msg::CarLocalMediaPlaybackAction action);

// Which local source the phone shows: the one that started playing last (like the media keys), else the first that
// reported. Only sources of plugins that declare "localMedia" are passed in (CarLocalMediaService).
class LocalMediaState
{
public:
    struct Changes
    {
        bool status = false;
        bool metadata = false;
    };

    Changes onStatus(const QString& source, const QString& name, const QVariantMap& status);
    Changes onMetadata(const QString& source, const QVariantMap& metadata);

    // The companion app (openhu#158) shows the head unit's music in Android Auto itself: while it is connected the
    // phone gets this player stopped and no updates (kept, sent again when the app is gone)
    Changes setCompanion(bool connected);
    bool companion() const { return companion_; }

    QString current() const { return current_; }
    QString name() const { return names_.value(current_); }
    QVariantMap status() const { return statuses_.value(current_); }
    // what the phone gets: the status, stopped while the companion app is connected
    QVariantMap shownStatus() const;
    QVariantMap metadata() const { return metadata_.value(current_); }

private:
    QString current_;
    bool companion_ = false;
    QHash<QString, QString> names_;
    QHash<QString, QVariantMap> statuses_;
    QHash<QString, QVariantMap> metadata_;
};

}
