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

#include <optional>
#include <QString>
#include <QVariantMap>
#include <aap_protobuf/service/media/video/message/VideoFocusMode.pb.h>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{
namespace mediasink
{

// Who has the screen: Android Auto (VIDEO_FOCUS_PROJECTED) or the head unit's own UI (VIDEO_FOCUS_NATIVE), see
// open-android-auto docs/channels/video.md, "Video Focus -- State Machine" (#76).
//
// NATIVE while
// - a plugin overlay (e.g. the reverse camera, "ui.overlay") is shown above everything, including Android Auto,
// - a plugin page replaced the Android Auto page (camera in "page" mode: "aa" -> "plugin:<id>"), until the Android
//   Auto page is shown again; also a plugin page shown when the session starts: the shell keeps it instead of
//   switching to Android Auto (#87, the phone connects or restarts its stream while the camera page is shown),
// - the phone asked for NATIVE ("Exit" in Android Auto, the shell shows its home page), until the user opens the
//   Android Auto page again.
// PROJECTED otherwise. Leaving Android Auto for the home or settings page by hand does not count: the phone keeps
// streaming there as before (switching back shows the picture at once).
//
// The head unit tells the phone about its own changes with an unsolicited indication, answers the phone's requests
// with the resulting mode (a request for PROJECTED while the camera covers the projection gets NATIVE) and sends
// nothing twice.
class VideoFocusPolicy
{
public:
    using Mode = aap_protobuf::service::media::video::message::VideoFocusMode;

    explicit VideoFocusPolicy(const QString& page = QString(), const QVariantMap& overlays = QVariantMap())
        : page_(page), overlay_(overlayShown(overlays)), pageCovers_(page.startsWith(QLatin1String("plugin:")))
    {
    }

    // "ui.overlay" {overlays: [plugin ids]}
    static bool overlayShown(const QVariantMap& data)
    {
        return !data.value("overlays").toList().isEmpty();
    }

    // Mode to send after the channel setup (the first indication of the session)
    Mode setup()
    {
        sent_ = this->mode();
        return *sent_;
    }

    // The phone's VideoFocusRequest: the mode to answer with
    Mode request(Mode requested)
    {
        if(requested == Mode::VIDEO_FOCUS_NATIVE)
        {
            phoneNative_ = true;
        }
        else if(requested == Mode::VIDEO_FOCUS_PROJECTED || requested == Mode::VIDEO_FOCUS_PROJECTED_NO_INPUT_FOCUS)
        {
            phoneNative_ = false;
        }
        // NATIVE_TRANSIENT: OpenHU has no native overlay of the phone's kind, the projection stays as it is
        sent_ = this->mode();
        return *sent_;
    }

    // "ui.page" {page}: the mode to send unsolicited, if it changed
    std::optional<Mode> setPage(const QString& page)
    {
        if(page == QLatin1String("aa"))
        {
            pageCovers_ = false;
            phoneNative_ = false;
        }
        else if(page_ == QLatin1String("aa") && page.startsWith(QLatin1String("plugin:")))
        {
            pageCovers_ = true;
        }
        page_ = page;
        return this->changed();
    }

    // "ui.overlay": the mode to send unsolicited, if it changed
    std::optional<Mode> setOverlays(const QVariantMap& data)
    {
        overlay_ = overlayShown(data);
        return this->changed();
    }

    Mode mode() const
    {
        return overlay_ || pageCovers_ || phoneNative_ ? Mode::VIDEO_FOCUS_NATIVE : Mode::VIDEO_FOCUS_PROJECTED;
    }

private:
    std::optional<Mode> changed()
    {
        // nothing before the channel setup: the setup sends the current mode
        if(!sent_ || *sent_ == this->mode())
        {
            return std::nullopt;
        }
        sent_ = this->mode();
        return sent_;
    }

    QString page_;
    bool overlay_ = false;
    bool pageCovers_ = false;
    bool phoneNative_ = false;
    std::optional<Mode> sent_;
};

}
}
}
}
}
