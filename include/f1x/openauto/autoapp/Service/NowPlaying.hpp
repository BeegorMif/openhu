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
#include <QObject>
#include <QVariantMap>
#include <f1x/openauto/autoapp/Service/AudioFocusPolicy.hpp>

namespace f1x::openauto::autoapp::service
{

// What plays now: media.metadata and media.status are kept per source ("androidauto", "music", "radio", ...), and
// the kept topic media.current carries the metadata and status of the source that started last (the one with the
// media keys). media.metadata / media.status alone hold whatever any source published last: after a switch the
// new source's metadata would only come with its next track. Read by the home page and Bluetooth car audio.
class NowPlaying: public QObject
{
    Q_OBJECT

public:
    explicit NowPlaying(QObject* parent = nullptr);

    // a media.status / media.metadata event and who published it ("plugin:<id>", ...); true when media.current changed
    bool update(const QString& topic, const QVariantMap& data, const QString& publisher = QString());
    // {source, plugin, metadata, status}: plugin is the id of the plugin behind the source (its page), empty for
    // Android Auto and API clients; metadata/status are empty maps while the source has not published them
    QVariantMap current() const;

private:
    AudioFocusPolicy policy_;
    QString latest_;  // the source of the last status, while no source has started yet
    QHash<QString, QVariantMap> metadata_;
    QHash<QString, QVariantMap> statuses_;
    QHash<QString, QString> plugins_;
    QVariantMap published_;
};

}
