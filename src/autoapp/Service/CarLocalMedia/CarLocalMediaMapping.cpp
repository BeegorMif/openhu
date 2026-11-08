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

#include <QBuffer>
#include <QImage>
#include <f1x/openauto/autoapp/Service/CarLocalMedia/CarLocalMediaMapping.hpp>

namespace f1x::openauto::autoapp::service::carlocalmedia
{

msg::CarLocalMediaPlaybackStatus playbackStatus(const QVariantMap& status, const QString& name)
{
    msg::CarLocalMediaPlaybackStatus result;
    const QString state = status.value("state").toString();
    // values as the phone maps them (openhu#147): 2 is the only one that brings the source to the media card's front
    result.set_playback_state(state == QLatin1String("playing") ? msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_PLAYING
                              : state == QLatin1String("paused") ? msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_PAUSED
                                                                 : msg::CAR_LOCAL_MEDIA_PLAYBACK_STATE_STOPPED);
    if(!name.isEmpty())
    {
        result.set_media_source(name.toStdString());
    }
    result.set_playback_position(std::max(0, qRound(status.value("position").toDouble())));
    for(const auto action: {msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PLAY, msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PAUSE,
                            msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PREVIOUS, msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_NEXT})
    {
        result.add_actions(action);
    }
    return result;
}

std::string coverBytes(const QString& dataUrl, int maxSize)
{
    const int comma = dataUrl.indexOf(',');
    if(!dataUrl.startsWith(QLatin1String("data:image/")) || comma < 0
       || !dataUrl.left(comma).endsWith(QLatin1String(";base64")))
    {
        return std::string();
    }
    const QByteArray bytes = QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1());
    QImage image;
    if(!image.loadFromData(bytes))
    {
        return std::string();
    }
    if(image.width() <= maxSize && image.height() <= maxSize)
    {
        return bytes.toStdString();
    }
    QByteArray scaled;
    QBuffer buffer(&scaled);
    buffer.open(QIODevice::WriteOnly);
    image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "JPEG", 90);
    return scaled.toStdString();
}

msg::CarLocalMediaPlaybackMetadata playbackMetadata(const QVariantMap& metadata)
{
    msg::CarLocalMediaPlaybackMetadata result;
    result.set_song(metadata.value("title").toString().toStdString());
    result.set_artist(metadata.value("artist").toString().toStdString());
    result.set_album(metadata.value("album").toString().toStdString());
    const double duration = metadata.value("duration").toDouble();
    if(duration > 0)
    {
        result.set_duration_seconds(uint32_t(qRound(duration)));
    }
    const std::string cover = coverBytes(metadata.value("cover").toString());
    if(!cover.empty())
    {
        result.set_album_art(cover);
    }
    return result;
}

QString mediaAction(msg::CarLocalMediaPlaybackAction action)
{
    switch(action)
    {
    case msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PLAY:
        return QStringLiteral("media.play");
    case msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PAUSE:
    case msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_STOP:
        return QStringLiteral("media.pause");
    case msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_PREVIOUS:
        return QStringLiteral("media.previous");
    case msg::CAR_LOCAL_MEDIA_PLAYBACK_ACTION_NEXT:
        return QStringLiteral("media.next");
    }
    return QString();
}

LocalMediaState::Changes LocalMediaState::onStatus(const QString& source, const QString& name,
                                                   const QVariantMap& status)
{
    Changes changes;
    if(source.isEmpty())
    {
        return changes;
    }
    statuses_.insert(source, status);
    names_.insert(source, name);
    const bool started = status.value("state").toString() == QLatin1String("playing");
    if(current_ != source && (current_.isEmpty() || started))
    {
        // another source: its metadata as well
        current_ = source;
        changes.metadata = metadata_.contains(source);
    }
    changes.status = current_ == source;
    return changes;
}

LocalMediaState::Changes LocalMediaState::onMetadata(const QString& source, const QVariantMap& metadata)
{
    Changes changes;
    if(source.isEmpty())
    {
        return changes;
    }
    metadata_.insert(source, metadata);
    changes.metadata = current_ == source;
    return changes;
}

}
