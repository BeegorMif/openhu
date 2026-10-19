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

#include <functional>
#include <map>
#include <memory>
#include <vector>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

class QQmlEngine;

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

// Plugins live in ~/.openhu/plugins/<id>/ with a manifest.json (see docs/plugins.md):
//   ui.page / ui.overlay  QML shown in the shell, ui.dock adds a dock button
//   process.exec          background process (any language) that talks to the API
//   androidAuto           sensors/services the plugin provides to Android Auto (#123, AndroidAutoProviders.hpp)
//   requires/recommends   other plugins it needs (with a minimum version) or works with (#104)
class PluginManager: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)

public:
    PluginManager(quint16 apiPort, QObject* parent = nullptr);
    ~PluginManager() override;

    // "api" in manifest.json: plugins of another version (e.g. QML for Qt 5 = api 1) are listed but not loaded
    static constexpr int cApiVersion = 2;
    static bool compatible(const QVariantMap& manifest);

    static QString baseDir();
    static QString pluginsDir();

    // Reads all manifests and (re)starts the processes of enabled plugins.
    void load();
    void stopAll();

    QVariantList plugins() const;
    QVariantMap plugin(const QString& id) const;
    bool contains(const QString& id) const;

    Q_INVOKABLE bool setEnabled(const QString& id, bool enabled);
    // Show the plugin's page in the dock (overrides "ui.dock" of the manifest)
    Q_INVOKABLE bool setDock(const QString& id, bool dock);
    Q_INVOKABLE bool restart(const QString& id);
    Q_INVOKABLE bool setOverlayVisible(const QString& id, bool visible);
    Q_INVOKABLE bool overlayVisible(const QString& id) const;

    // Plugins that provide to Android Auto cannot be enabled, disabled, updated or removed while the phone is
    // connected (the services and sensors are fixed for a connection, #123): the reason, empty if allowed.
    QString changeBlocked(const QString& id) const;
    static bool androidAutoConnected();
    // the plugin whose process got this token (OPENHU_PLUGIN_TOKEN), empty if none
    QString pluginForToken(const QString& token) const;

    // Plugins (#104): "requires": {"id": "minimum version"} (or a list of ids), "recommends": ["id", ...]
    static QVariantMap requiresOf(const QVariantMap& manifest);
    static QStringList recommendsOf(const QVariantMap& manifest);
    // the enabled plugins that require this one (directly or through others)
    QStringList dependents(const QString& id) const;
    // why a plugin is not started: [{id, version, reason: notInstalled|tooOld|disabled|incompatible}]
    QVariantList missingRequirements(const QString& id) const;

    // Stops a plugin before its files are replaced or removed.
    void unload(const QString& id);

    // QML caches loaded files by URL: without dropping that cache an updated plugin keeps showing its old pages
    // (and every other QML file of it) until OpenHU restarts. The shell sets dropQmlCache(engine) here; the store
    // calls it once the plugin's pages are gone, before the new files are loaded.
    void setQmlCacheClearer(std::function<void()> clearer);
    void clearQmlCache();
    // QQmlEngine::trimComponentCache() keeps files whose compiled code the JavaScript heap still refers to (Qt 6.8,
    // also after collectGarbage(), which is only one incremental step). clearComponentCache() runs a full garbage
    // collection and empties the cache; objects that exist keep their types, singletons their instance.
    static void dropQmlCache(QQmlEngine& engine);

signals:
    // Installed/enabled set changed (QML rebuilds plugin pages)
    void pluginsChanged();
    void overlayChanged(const QString& id, bool visible);

private:
    struct Plugin
    {
        QString id;
        QString dir;
        QVariantMap manifest;
        QVariantMap androidAuto;  // parsed "androidAuto", empty without
        QString token;            // of the running process
        bool enabled = true;
        bool compatible = true;
        int dock = -1;  // -1: manifest, 0/1: user setting
        bool overlayVisible = false;
        std::unique_ptr<QProcess> process;
        QTimer restartTimer;
        QElapsedTimer uptime;
        int restartDelay = 0;
        bool stopping = false;
    };

    void startProcess(Plugin& plugin);
    // the enabled plugins whose requirements are met, required ones first
    void startEnabled();
    void stopProcess(Plugin& plugin);
    void stopProcesses(const std::vector<Plugin*>& plugins);
    void onProcessFinished(const QString& id, int exitCode, QProcess::ExitStatus status);
    QVariantMap toVariant(const Plugin& plugin) const;
    void saveState() const;
    // "ui.overlay" {overlays: ids of the plugins whose overlay is shown}
    void publishOverlays();
    // "plugins.state" {plugins: [...]}: the services read the enabled plugins and their androidAuto sections
    void publishState();
    QVariantMap loadState() const;

    quint16 apiPort_;
    std::function<void()> qmlCacheClearer_;
    std::map<QString, std::unique_ptr<Plugin>> plugins_;
};

}
}
}
}
