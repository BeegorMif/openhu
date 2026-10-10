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

#include <cmath>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include <f1x/openauto/autoapp/Projection/InputDevice.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

namespace
{

QSize resolutionSize(aap_protobuf::service::media::sink::message::VideoCodecResolutionType resolution)
{
    switch(resolution)
    {
    case aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_1280x720:
        return QSize(1280, 720);
    case aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_1920x1080:
        return QSize(1920, 1080);
    default:
        return QSize(800, 480);
    }
}

// Margins must be even, the phone centers the UI inside the video frame.
int evenFloor(double value)
{
    return std::max(0, static_cast<int>(std::floor(value / 2.0)) * 2);
}

}

AaBridge& AaBridge::instance()
{
    static AaBridge bridge;
    return bridge;
}

void AaBridge::configure(configuration::IConfiguration::Pointer configuration)
{
    enabled_ = configuration->getUiMode() != "legacy";

    {
        std::lock_guard<std::mutex> lock(mutex_);
        videoSize_ = resolutionSize(configuration->getVideoResolution());
        const QRect margins = configuration->getVideoMargins();
        configuredMargins_ = QSize(margins.width(), margins.height());
    }

    this->updateMargins();
}

bool AaBridge::enabled() const
{
    return enabled_;
}

QObject* AaBridge::mediaObject() const
{
    return mediaPlayer_.data();
}

void AaBridge::setMediaPlayer(QMediaPlayer* player)
{
    if(mediaPlayer_ != player)
    {
        mediaPlayer_ = player;
        emit mediaObjectChanged();
    }
}

void AaBridge::releaseMediaPlayer(QMediaPlayer* player)
{
    if(mediaPlayer_ == player)
    {
        mediaPlayer_ = nullptr;
        emit mediaObjectChanged();
    }
}

bool AaBridge::videoActive() const
{
    return videoActive_;
}

void AaBridge::setVideoActive(bool active)
{
    if(videoActive_ != active)
    {
        videoActive_ = active;
        emit videoActiveChanged();
        core::EventBus::instance().publish("projection.video", {{"active", active}});
    }
}

bool AaBridge::surfaceVisible() const
{
    return surfaceVisible_;
}

void AaBridge::setSurfaceVisible(bool visible)
{
    if(surfaceVisible_ != visible)
    {
        surfaceVisible_ = visible;
        emit surfaceVisibleChanged();
    }
}

QSize AaBridge::videoSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return videoSize_;
}

QRect AaBridge::contentRect() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return QRect(margins_.width() / 2, margins_.height() / 2,
                 videoSize_.width() - margins_.width(), videoSize_.height() - margins_.height());
}

QRect AaBridge::videoMargins() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return QRect(0, 0, margins_.width(), margins_.height());
}

void AaBridge::setViewportSize(int width, int height)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        viewportSize_ = QSize(width, height);
    }

    this->updateMargins();
}

void AaBridge::updateMargins()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if(!configuredMargins_.isNull() || !enabled_ || viewportSize_.isEmpty())
        {
            // Margins from openhu.ini win; without a known viewport the full frame is used.
            margins_ = configuredMargins_;
        }
        else
        {
            const double viewportAspect = static_cast<double>(viewportSize_.width()) / viewportSize_.height();
            const double videoAspect = static_cast<double>(videoSize_.width()) / videoSize_.height();

            if(viewportAspect >= videoAspect)
            {
                margins_ = QSize(0, evenFloor(videoSize_.height() - videoSize_.width() / viewportAspect));
            }
            else
            {
                margins_ = QSize(evenFloor(videoSize_.width() - videoSize_.height() * viewportAspect), 0);
            }
        }

        OPENAUTO_LOG(info) << "[AaBridge] viewport " << viewportSize_.width() << "x" << viewportSize_.height()
                           << ", video " << videoSize_.width() << "x" << videoSize_.height()
                           << ", margins " << margins_.width() << "x" << margins_.height();
    }

    emit geometryChanged();
}

void AaBridge::attachInputDevice(projection::InputDevice* device)
{
    std::lock_guard<std::mutex> lock(inputMutex_);
    inputDevice_ = device;
}

void AaBridge::detachInputDevice(projection::InputDevice* device)
{
    std::lock_guard<std::mutex> lock(inputMutex_);
    if(inputDevice_ == device)
    {
        inputDevice_ = nullptr;
    }
}

void AaBridge::forwardPointerEvent(QEvent* event, const QSizeF& surfaceSize)
{
    std::lock_guard<std::mutex> lock(inputMutex_);
    if(inputDevice_ != nullptr && !surfaceSize.isEmpty())
    {
        inputDevice_->handleSurfaceEvent(event, surfaceSize, this->contentRect());
    }
}

bool AaBridge::sendButton(const projection::ButtonEvent& event)
{
    std::lock_guard<std::mutex> lock(inputMutex_);
    return inputDevice_ != nullptr && inputDevice_->injectButton(event);
}

}
}
}
}
