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

#include <QObject>
#include <QKeyEvent>
#include <QTouchEvent>
#include <QRect>
#include <QSizeF>
#include <map>
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

class InputDevice: public QObject, public IInputDevice, boost::noncopyable
{
    Q_OBJECT

public:
    InputDevice(QObject& parent, configuration::IConfiguration::Pointer configuration, const QRect& touchscreenGeometry, const QRect& videoGeometry);

    void start(IInputDeviceEventHandler& eventHandler) override;
    void stop() override;
    ButtonCodes getSupportedButtonCodes() const override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    bool hasTouchscreen() const override;
    QRect getTouchscreenGeometry() const override;

    // Pointer event from the shell's Android Auto surface (item coordinates). contentRect is the part
    // of the video frame showing the Android Auto UI; the surface is mapped onto it.
    void handleSurfaceEvent(QEvent* event, const QSizeF& surfaceSize, const QRect& contentRect);
    // Button event from the ActionDispatcher (keyboard, steering wheel, API). False without session.
    bool injectButton(const ButtonEvent& event);

private:
    void setVideoGeometry();
    bool handleKeyEvent(QEvent* event, QKeyEvent* key);
    void dispatchKeyEvent(ButtonEvent event);
    bool handleTouchEvent(QEvent* event);
    bool handleMultiTouchEvent(QTouchEvent* touchEvent);
    void translateTouchPoint(const QTouchEvent::TouchPoint& qtPoint, TouchPoint& ourPoint);
    void mapToDisplay(const QPointF& pos, uint32_t& x, uint32_t& y) const;

    QObject& parent_;
    configuration::IConfiguration::Pointer configuration_;
    QRect touchscreenGeometry_;
    QRect displayGeometry_;
    IInputDeviceEventHandler* eventHandler_;
    std::mutex mutex_;
    std::map<int, uint32_t> touchPointIdMap_; // Maps Qt touch IDs to our sequential IDs
    uint32_t nextTouchPointId_;
    bool surfaceMode_;
    QSizeF surfaceSize_;
    QRect surfaceContentRect_;
};

}
}
}
}
