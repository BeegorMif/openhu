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
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

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

    // Stops a plugin before its files are replaced or removed.
    void unload(const QString& id);

    // QML caches loaded files by URL; the shell drops cached plugin QML after an update with this.
    void setQmlCacheTrimmer(std::function<void()> trimmer);
    void trimQmlCache();

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
    void stopProcess(Plugin& plugin);
    void onProcessFinished(const QString& id, int exitCode, QProcess::ExitStatus status);
    QVariantMap toVariant(const Plugin& plugin) const;
    void saveState() const;
    QVariantMap loadState() const;

    quint16 apiPort_;
    std::function<void()> qmlCacheTrimmer_;
    std::map<QString, std::unique_ptr<Plugin>> plugins_;
};

}
}
}
}
