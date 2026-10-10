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

#include <QObject>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

// Actions of the QML shell that need the C++ side (exposed to QML as "shell").
class ShellController: public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    Q_INVOKABLE void openSettings() { emit settingsRequested(); }
    Q_INVOKABLE void quit() { emit quitRequested(); }

    // Requested through the API (ui.navigate, ui.toast), handled in Main.qml.
    void navigate(const QString& page) { emit navigateRequested(page); }
    void toast(const QString& text, int durationMs) { emit toastRequested(text, durationMs); }

signals:
    void settingsRequested();
    void quitRequested();
    void navigateRequested(const QString& page);
    void toastRequested(const QString& text, int durationMs);
};

}
}
}
}
