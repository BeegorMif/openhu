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

#include <QMouseEvent>
#include <QTouchEvent>
#include <f1x/openauto/autoapp/Shell/AaSurface.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

AaSurface::AaSurface(QQuickItem* parent)
    : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptTouchEvents(true);
}

void AaSurface::mousePressEvent(QMouseEvent* event)
{
    this->forward(event);
    event->accept();
}

void AaSurface::mouseMoveEvent(QMouseEvent* event)
{
    this->forward(event);
    event->accept();
}

void AaSurface::mouseReleaseEvent(QMouseEvent* event)
{
    this->forward(event);
    event->accept();
}

void AaSurface::touchEvent(QTouchEvent* event)
{
    this->forward(event);
    event->accept();
}

void AaSurface::forward(QEvent* event)
{
    AaBridge::instance().forwardPointerEvent(event, QSizeF(width(), height()));
}

}
}
}
}
