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

#include <algorithm>
#include <QMouseEvent>
#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Projection/IInputDeviceEventHandler.hpp>
#include <f1x/openauto/autoapp/Projection/InputDevice.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

using PointerAction = aap_protobuf::service::inputsource::message::PointerAction;

InputDevice::InputDevice(configuration::IConfiguration::Pointer configuration, const QRect& videoGeometry)
    : configuration_(std::move(configuration))
    , videoGeometry_(videoGeometry)
    , eventHandler_(nullptr)
    , nextTouchPointId_(0)
{
}

void InputDevice::start(IInputDeviceEventHandler& eventHandler)
{
    {
        std::lock_guard<decltype(mutex_)> lock(mutex_);
        OPENAUTO_LOG(info) << "[InputDevice] start()";
        eventHandler_ = &eventHandler;
    }

    // Outside of mutex_: AaBridge locks its own mutex before calling into us.
    shell::AaBridge::instance().attachInputDevice(this);
}

void InputDevice::stop()
{
    shell::AaBridge::instance().detachInputDevice(this);

    std::lock_guard<decltype(mutex_)> lock(mutex_);
    OPENAUTO_LOG(info) << "[InputDevice] stop()";
    eventHandler_ = nullptr;
}

IInputDevice::ButtonCodes InputDevice::getSupportedButtonCodes() const
{
    // Everything the actions (keyboard, steering wheel, CAN, API) can produce
    using KeyCode = aap_protobuf::service::media::sink::message::KeyCode;
    return {KeyCode::KEYCODE_HOME, KeyCode::KEYCODE_BACK, KeyCode::KEYCODE_CALL, KeyCode::KEYCODE_ENDCALL,
            KeyCode::KEYCODE_DPAD_UP, KeyCode::KEYCODE_DPAD_DOWN, KeyCode::KEYCODE_DPAD_LEFT,
            KeyCode::KEYCODE_DPAD_RIGHT, KeyCode::KEYCODE_DPAD_CENTER, KeyCode::KEYCODE_SEARCH,
            KeyCode::KEYCODE_MEDIA_PLAY_PAUSE, KeyCode::KEYCODE_MEDIA_NEXT, KeyCode::KEYCODE_MEDIA_PREVIOUS,
            KeyCode::KEYCODE_MEDIA_PLAY, KeyCode::KEYCODE_MEDIA_PAUSE, KeyCode::KEYCODE_ROTARY_CONTROLLER,
            KeyCode::KEYCODE_MEDIA, KeyCode::KEYCODE_NAVIGATION, KeyCode::KEYCODE_TEL};
}

bool InputDevice::hasTouchscreen() const
{
    return configuration_->getTouchscreenEnabled();
}

QRect InputDevice::getTouchscreenGeometry() const
{
    return videoGeometry_;
}

bool InputDevice::injectButton(const ButtonEvent& event)
{
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    if(eventHandler_ == nullptr)
    {
        return false;
    }

    const auto buttonCodes = this->getSupportedButtonCodes();
    if(std::find(buttonCodes.begin(), buttonCodes.end(), event.code) == buttonCodes.end())
    {
        OPENAUTO_LOG(warning) << "[InputDevice] key code not announced to the phone: " << event.code;
        return false;
    }

    eventHandler_->onButtonEvent(event);
    return true;
}

void InputDevice::handleSurfaceEvent(QEvent* event, const QSizeF& surfaceSize, const QRect& contentRect)
{
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    if(eventHandler_ == nullptr || !configuration_->getTouchscreenEnabled())
    {
        return;
    }

    surfaceSize_ = surfaceSize;
    surfaceContentRect_ = contentRect;

    switch(event->type())
    {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        this->handleTouchEvent(static_cast<QTouchEvent*>(event));
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        this->handleMouseEvent(event);
        break;
    default:
        break;
    }
}

void InputDevice::mapToVideo(const QPointF& pos, uint32_t& x, uint32_t& y) const
{
    const double fx = std::clamp(pos.x() / surfaceSize_.width(), 0.0, 1.0);
    const double fy = std::clamp(pos.y() / surfaceSize_.height(), 0.0, 1.0);
    x = static_cast<uint32_t>(surfaceContentRect_.x() + fx * (surfaceContentRect_.width() - 1));
    y = static_cast<uint32_t>(surfaceContentRect_.y() + fy * (surfaceContentRect_.height() - 1));
}

void InputDevice::handleMouseEvent(QEvent* event)
{
    // Mouse as single finger (desktop/development)
    PointerAction type;
    switch(event->type())
    {
    case QEvent::MouseButtonPress:
        type = PointerAction::ACTION_DOWN;
        break;
    case QEvent::MouseButtonRelease:
        type = PointerAction::ACTION_UP;
        break;
    default:
        type = PointerAction::ACTION_MOVED;
        break;
    }

    auto* mouse = static_cast<QMouseEvent*>(event);
    if(event->type() != QEvent::MouseButtonRelease && !mouse->buttons().testFlag(Qt::LeftButton))
    {
        return;
    }

    TouchEvent touch;
    touch.type = type;
    touch.actionIndex = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    this->mapToVideo(mouse->position(), x, y);
    touch.pointers.push_back({x, y, 0});
    eventHandler_->onTouchEvent(touch);
}

void InputDevice::handleTouchEvent(QTouchEvent* touchEvent)
{
    const auto& touchPoints = touchEvent->points();
    if(touchPoints.isEmpty())
    {
        return;
    }

    // The pointer that changed state (pressed or released), if any
    int changedIndex = -1;
    QEventPoint::State changedState = QEventPoint::Stationary;
    for(int i = 0; i < touchPoints.size(); ++i)
    {
        if(touchPoints[i].state() == QEventPoint::Pressed || touchPoints[i].state() == QEventPoint::Released)
        {
            changedIndex = i;
            changedState = touchPoints[i].state();
            break;
        }
    }

    TouchEvent event;
    event.actionIndex = 0;
    switch(touchEvent->type())
    {
    case QEvent::TouchBegin:
        event.type = PointerAction::ACTION_DOWN;
        break;
    case QEvent::TouchEnd:
        event.type = PointerAction::ACTION_UP;
        event.actionIndex = changedIndex >= 0 ? changedIndex : 0;
        break;
    case QEvent::TouchCancel:
        // Android Auto has no cancel, all fingers up
        event.type = PointerAction::ACTION_UP;
        touchPointIdMap_.clear();
        nextTouchPointId_ = 0;
        break;
    default:  // TouchUpdate
        if(changedState == QEventPoint::Pressed)
        {
            event.type = PointerAction::ACTION_POINTER_DOWN;
            event.actionIndex = changedIndex;
        }
        else if(changedState == QEventPoint::Released)
        {
            event.type = PointerAction::ACTION_POINTER_UP;
            event.actionIndex = changedIndex;
        }
        else
        {
            event.type = PointerAction::ACTION_MOVED;
        }
        break;
    }

    for(const auto& qtPoint: touchPoints)
    {
        // Released points only belong to UP events
        if(qtPoint.state() == QEventPoint::Released && event.type != PointerAction::ACTION_UP
           && event.type != PointerAction::ACTION_POINTER_UP)
        {
            continue;
        }

        TouchPoint point;
        this->translateTouchPoint(qtPoint, point);
        event.pointers.push_back(point);

        if(qtPoint.state() == QEventPoint::Released)
        {
            touchPointIdMap_.erase(qtPoint.id());
        }
    }

    if(touchEvent->type() == QEvent::TouchEnd)
    {
        touchPointIdMap_.clear();
        nextTouchPointId_ = 0;
    }

    if(!event.pointers.empty())
    {
        eventHandler_->onTouchEvent(event);
    }
}

void InputDevice::translateTouchPoint(const QEventPoint& qtPoint, TouchPoint& ourPoint)
{
    if(touchPointIdMap_.find(qtPoint.id()) == touchPointIdMap_.end())
    {
        touchPointIdMap_[qtPoint.id()] = nextTouchPointId_++;
    }
    ourPoint.pointerId = touchPointIdMap_[qtPoint.id()];
    this->mapToVideo(qtPoint.position(), ourPoint.x, ourPoint.y);
}

}
}
}
}
