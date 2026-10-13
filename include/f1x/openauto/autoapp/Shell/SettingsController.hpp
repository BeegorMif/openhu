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
#include <QStringList>
#include <QVariantMap>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

// Backend of the settings page (QML: "settings"). Values are flat keys ("aa.resolution", "api.port", ...),
// every change is saved to openhu.ini right away. Changes that only apply to the next Android Auto
// connection or after a restart are listed in "pending".
class SettingsController: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QStringList pending READ pending NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)

public:
    SettingsController(configuration::IConfiguration::Pointer configuration, int argc, char** argv,
                       QObject* parent = nullptr);

    QVariantMap values() const;
    // Per key of values(): type (bool, int, string, choice), choices, min/max, maxLength, applies (now, reconnect,
    // restart), secret (passwords, not readable from the network), readOnly. For the API (settings.keys).
    static QVariantMap keys();
    QStringList pending() const;
    QVariantMap info() const;

    // Returns an error message, empty on success. Publishes "settings.changed" {key, value, pending}.
    Q_INVOKABLE QString set(const QString& key, const QVariant& value);
    Q_INVOKABLE void clearPending(const QString& what);
    // Values were changed elsewhere (e.g. wireless.pair() switched wireless on)
    Q_INVOKABLE void refresh();
    // New API token (Settings → Network): clients that logged in with the old one are disconnected
    Q_INVOKABLE void regenerateApiToken();

    // Restarts OpenHU in place (same process id, works under systemd and in the container).
    Q_INVOKABLE void restart();

    void applyCursor() const;
    // "system.nightMode" {mode: auto/day/night}: Android Auto's night mode (SensorService)
    void publishNightMode() const;

signals:
    void changed();
    // Right before restart(): stop child processes (plugins, pactl)
    void aboutToRestart();
    // Wireless Android Auto switched or hotspot settings changed (applied right away)
    void wirelessChanged();
    // car radio output switched or its PIN changed
    void carAudioChanged();
    // general.language chosen (applied by shell::Translations)
    void languageChanged(const QString& language);
    // API token or allowed origins changed (applied by api::ApiServer)
    void apiAccessChanged();

private:
    void markPending(const QString& what);
    // saves openhu.ini and publishes "settings.changed"
    void saved(const QString& key);

    configuration::IConfiguration::Pointer configuration_;
    QStringList arguments_;
    QStringList pending_;
};

}
}
}
}
