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

#include <QQuickItem>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

// Invisible QML item placed over the Android Auto video. Mouse and touch input inside of it
// is forwarded to the phone; everything outside stays with the shell.
class AaSurface: public QQuickItem
{
    Q_OBJECT

public:
    explicit AaSurface(QQuickItem* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void touchEvent(QTouchEvent* event) override;

private:
    void forward(QEvent* event);
};

}
}
}
}
