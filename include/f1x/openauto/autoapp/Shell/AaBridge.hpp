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

#include <mutex>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QVideoSink>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Projection/InputEvent.hpp>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{
class InputDevice;
}

namespace shell
{

// Connects the Android Auto projection (video decoder, input device) with the QML shell.
// The video is rendered by a QML VideoOutput (its videoSink is set here) and touch input comes from an AaSurface item,
// so the projection lives inside the main window instead of a separate fullscreen window.
class AaBridge: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVideoSink* videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(bool videoActive READ videoActive NOTIFY videoActiveChanged)
    Q_PROPERTY(bool surfaceVisible READ surfaceVisible WRITE setSurfaceVisible NOTIFY surfaceVisibleChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY geometryChanged)
    Q_PROPERTY(QRect contentRect READ contentRect NOTIFY geometryChanged)

public:
    static AaBridge& instance();

    // Called once at startup (GUI thread).
    void configure(configuration::IConfiguration::Pointer configuration);

    // Sink of the shell's VideoOutput (AaPage.qml), decoded frames go there. GUI thread.
    QVideoSink* videoSink() const;
    void setVideoSink(QVideoSink* sink);

    bool videoActive() const;
    // The video output of a session starts/stops showing. A late-stopped output of an earlier session
    // does not hide the current one. GUI thread.
    void setVideoOutputActive(const void* output, bool active);

    bool surfaceVisible() const;
    void setSurfaceVisible(bool visible);

    // Resolution announced to the phone (configured, or matching the Android Auto area). Thread safe.
    aap_protobuf::service::media::sink::message::VideoCodecResolutionType resolution() const;
    QSize videoSize() const;
    // Part of the video frame that contains the Android Auto UI (video frame minus margins).
    QRect contentRect() const;
    // Total margins (width/height) announced to the phone. Thread safe.
    QRect videoMargins() const;

    // Size of the area the shell reserves for Android Auto. Used to compute margins so the
    // projection is not distorted. Takes effect on the next connection.
    Q_INVOKABLE void setViewportSize(int width, int height);

    void attachInputDevice(projection::InputDevice* device);
    void detachInputDevice(projection::InputDevice* device);
    // Mouse/touch event in AaSurface item coordinates.
    void forwardPointerEvent(QEvent* event, const QSizeF& surfaceSize);
    // Key/button to the phone. False when no Android Auto session is running.
    bool sendButton(const projection::ButtonEvent& event);

signals:
    void videoSinkChanged();
    void videoActiveChanged();
    void surfaceVisibleChanged();
    void geometryChanged();

private:
    AaBridge() = default;
    void updateMargins();
    void setVideoActive(bool active);

    QPointer<QVideoSink> videoSink_;
    const void* activeOutput_ = nullptr;
    bool videoActive_ = false;
    bool surfaceVisible_ = false;

    mutable std::mutex mutex_;
    QSize videoSize_{800, 480};
    bool autoResolution_ = true;
    aap_protobuf::service::media::sink::message::VideoCodecResolutionType configuredResolution_ =
        aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_800x480;
    aap_protobuf::service::media::sink::message::VideoCodecResolutionType resolution_ =
        aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_800x480;
    QSize configuredMargins_;
    QSize viewportSize_;
    QSize margins_;

    std::mutex inputMutex_;
    projection::InputDevice* inputDevice_ = nullptr;
};

}
}
}
}
