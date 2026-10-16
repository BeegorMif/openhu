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

#include <QVariantMap>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{

// Who plays: Android Auto music or a local media source (a plugin that publishes media.status with its own "source",
// e.g. the radio). When a local source starts, the head unit takes the audio focus from the phone, which pauses its
// music (#79). Only starts count: after the radio started, the phone still reports "playing" for a moment until the
// focus loss arrives; those reports must not count as Android Auto taking over (or the radio's next status update would
// take the focus again).
class AudioFocusPolicy
{
public:
    explicit AudioFocusPolicy(const QVariantMap& lastStatus = QVariantMap())
    {
        this->update(lastStatus);
        // the source of the last status owns the keys, even if it does not play right now
        const QString source = lastStatus.value("source").toString();
        if(owner_.isEmpty() && !source.isEmpty())
        {
            owner_ = source;
        }
    }

    // true: a local source has just started playing, take the focus from the phone
    bool update(const QVariantMap& status)
    {
        const QString source = status.value("source").toString();
        const bool playing = status.value("state").toString() == QLatin1String("playing");
        if(source.isEmpty() || source == QLatin1String("androidauto"))
        {
            if(playing && !androidAutoPlaying_)
            {
                // Android Auto music starts: the local source stops (the radio does by itself)
                localSource_.clear();
                owner_ = QStringLiteral("androidauto");
            }
            androidAutoPlaying_ = playing;
            return false;
        }
        if(!playing)
        {
            if(localSource_ == source)
            {
                localSource_.clear();
            }
            return false;
        }
        const bool started = localSource_ != source;
        localSource_ = source;
        if(started)
        {
            owner_ = source;
        }
        return started;
    }

    // Source that started last ("androidauto", a local source like "radio", or empty): it gets the media keys, also
    // while it is paused/stopped (play resumes it)
    QString owner() const
    {
        return owner_;
    }

private:
    QString owner_;
    bool androidAutoPlaying_ = false;
    QString localSource_;   // local source that plays, empty if none
};

}
}
}
}
