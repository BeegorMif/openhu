/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <map>
#include <mutex>
#include <QEvent>
#include <QRect>
#include <QSizeF>
#include <QTouchEvent>
#include <boost/noncopyable.hpp>
#include <f1x/openauto/autoapp/Projection/IInputDevice.hpp>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

// Input of one Android Auto session: touches from the shell's AaSurface and keys from the
// ActionDispatcher (keyboard, steering wheel, API), both passed on through the AaBridge.
class InputDevice: public IInputDevice, boost::noncopyable
{
public:
    InputDevice(configuration::IConfiguration::Pointer configuration, const QRect& videoGeometry);

    void start(IInputDeviceEventHandler& eventHandler) override;
    void stop() override;
    ButtonCodes getSupportedButtonCodes() const override;
    bool hasTouchscreen() const override;
    QRect getTouchscreenGeometry() const override;

    // Pointer event in AaSurface item coordinates. contentRect is the part of the video frame
    // showing the Android Auto UI; the surface is mapped onto it.
    void handleSurfaceEvent(QEvent* event, const QSizeF& surfaceSize, const QRect& contentRect);
    // False without a running session or for a key the phone was not told about.
    bool injectButton(const ButtonEvent& event);

private:
    void handleMouseEvent(QEvent* event);
    void handleTouchEvent(QTouchEvent* touchEvent);
    void translateTouchPoint(const QEventPoint& qtPoint, TouchPoint& ourPoint);
    void mapToVideo(const QPointF& pos, uint32_t& x, uint32_t& y) const;

    configuration::IConfiguration::Pointer configuration_;
    QRect videoGeometry_;
    IInputDeviceEventHandler* eventHandler_;
    std::mutex mutex_;
    std::map<int, uint32_t> touchPointIdMap_; // Qt touch ids -> sequential pointer ids
    uint32_t nextTouchPointId_;
    QSizeF surfaceSize_;
    QRect surfaceContentRect_;
};

}
}
}
}
