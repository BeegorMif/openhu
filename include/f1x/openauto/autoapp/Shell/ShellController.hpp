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

#pragma once

#include <QObject>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Shell/ProjectionPage.hpp>

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
    // Version of OpenHU OS (IMAGE_VERSION) when OpenHU runs on it, otherwise empty. There the settings restart and
    // power off the device instead of only OpenHU (systemd starts OpenHU again right after it quits).
    Q_PROPERTY(QString osVersion READ osVersion CONSTANT)

public:
    using QObject::QObject;

    Q_INVOKABLE void quit() { emit quitRequested(); }
    Q_INVOKABLE void restart() { emit restartRequested(); }
    // The whole device, through logind (OpenHU OS allows it the openhu user); OpenHU ends on its SIGTERM
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void powerOff();

    QString osVersion() const { return osVersion_; }
    // IMAGE_VERSION of an os-release file with IMAGE_ID=openhu-os, otherwise empty
    static QString readOsVersion(const QString& path = QStringLiteral("/usr/lib/os-release"));
    // Main.qml reports page changes -> "ui.page" {page}
    Q_INVOKABLE void setCurrentPage(const QString& page)
    {
        core::EventBus::instance().publish("ui.page", {{"page", page}});
    }

    // Requested through the API (ui.navigate, ui.toast) and the settings (errors), handled in Main.qml.
    Q_INVOKABLE void navigate(const QString& page) { emit navigateRequested(page); }
    Q_INVOKABLE void toast(const QString& text, int durationMs) { emit toastRequested(text, durationMs); }

    // Page to show when the projection starts or stops, and instead of a ui.navigate target (ProjectionPage, #87)
    Q_INVOKABLE QString pageOnVideo(const QString& page, bool active) { return projectionPage_.videoChanged(page, active); }
    Q_INVOKABLE QString pageOnNavigate(const QString& page, const QString& target)
    {
        return projectionPage_.navigate(page, target);
    }
    // The user chose a page (dock, home page, toast) or "Exit" inside Android Auto
    Q_INVOKABLE void pageChosenByUser() { projectionPage_.chosenByUser(); }

signals:
    void quitRequested();
    void restartRequested();
    void navigateRequested(const QString& page);
    void toastRequested(const QString& text, int durationMs);

private:
    void callLogind(const char* method);

    QString osVersion_ = readOsVersion();
    ProjectionPage projectionPage_;
};

}
}
}
}
