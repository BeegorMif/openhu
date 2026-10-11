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

#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QUrl>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

namespace
{

QVariantMap readJson(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        return QVariantMap();
    }
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

// File inside the plugin as URL for QML, empty if not set or missing.
QString fileUrl(const QString& dir, const QString& relative)
{
    if(relative.isEmpty() || !QFileInfo::exists(dir + "/" + relative))
    {
        return QString();
    }
    return QUrl::fromLocalFile(dir + "/" + relative).toString();
}

// Images are cached by URL: the modification time makes an updated icon a new URL.
QString imageUrl(const QString& dir, const QString& relative)
{
    const QString url = fileUrl(dir, relative);
    if(url.isEmpty())
    {
        return url;
    }
    return url + "?v=" + QString::number(QFileInfo(dir + "/" + relative).lastModified().toMSecsSinceEpoch());
}

constexpr int cMaxRestartDelayMs = 60000;

}

PluginManager::PluginManager(quint16 apiPort, QObject* parent)
    : QObject(parent)
    , apiPort_(apiPort)
{
}

PluginManager::~PluginManager()
{
    this->stopAll();
}

bool PluginManager::compatible(const QVariantMap& manifest)
{
    // without "api": plugins from before the versioning (Qt 5)
    return manifest.value("api", 1).toInt() == cApiVersion;
}

QString PluginManager::baseDir()
{
    return QDir::homePath() + "/.openhu";
}

QString PluginManager::pluginsDir()
{
    return baseDir() + "/plugins";
}

void PluginManager::load()
{
    this->stopAll();
    plugins_.clear();

    QDir().mkpath(pluginsDir());
    const QVariantMap state = this->loadState();

    const QDir dir(pluginsDir());
    for(const auto& entry: dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        const QString pluginDir = dir.filePath(entry);
        const QVariantMap manifest = readJson(pluginDir + "/manifest.json");
        const QString id = manifest.value("id").toString();
        if(id.isEmpty() || id != entry)
        {
            OPENAUTO_LOG(warning) << "[PluginManager] skipping " << pluginDir.toStdString()
                                  << ": manifest.json missing or id does not match the folder name";
            continue;
        }

        auto plugin = std::make_unique<Plugin>();
        plugin->id = id;
        plugin->dir = pluginDir;
        plugin->manifest = manifest;
        plugin->enabled = state.value(id).toMap().value("enabled", true).toBool();
        plugin->compatible = compatible(manifest);
        if(!plugin->compatible)
        {
            OPENAUTO_LOG(warning) << "[PluginManager] " << id.toStdString() << " is made for plugin API "
                                  << manifest.value("api", 1).toInt() << ", this OpenHU has " << cApiVersion
                                  << ": not loaded, update it in the store";
        }
        plugin->dock = state.value(id).toMap().contains("dock") ? state.value(id).toMap().value("dock").toBool() : -1;
        plugin->restartTimer.setSingleShot(true);
        connect(&plugin->restartTimer, &QTimer::timeout, this, [this, id]() {
            auto it = plugins_.find(id);
            if(it != plugins_.end() && it->second->enabled)
            {
                this->startProcess(*it->second);
            }
        });

        OPENAUTO_LOG(info) << "[PluginManager] plugin " << id.toStdString() << " "
                           << manifest.value("version").toString().toStdString()
                           << (plugin->enabled ? "" : " (disabled)");
        plugins_.emplace(id, std::move(plugin));
    }

    for(auto& entry: plugins_)
    {
        if(entry.second->enabled && entry.second->compatible)
        {
            this->startProcess(*entry.second);
        }
    }

    emit pluginsChanged();
    core::EventBus::instance().publish("plugins.state", {{"plugins", this->plugins()}});
}

void PluginManager::stopAll()
{
    for(auto& entry: plugins_)
    {
        this->stopProcess(*entry.second);
    }
}

QVariantList PluginManager::plugins() const
{
    QVariantList list;
    for(const auto& entry: plugins_)
    {
        list.append(this->toVariant(*entry.second));
    }
    return list;
}

QVariantMap PluginManager::plugin(const QString& id) const
{
    auto it = plugins_.find(id);
    return it == plugins_.end() ? QVariantMap() : this->toVariant(*it->second);
}

bool PluginManager::contains(const QString& id) const
{
    return plugins_.find(id) != plugins_.end();
}

bool PluginManager::setEnabled(const QString& id, bool enabled)
{
    auto it = plugins_.find(id);
    if(it == plugins_.end())
    {
        return false;
    }

    auto& plugin = *it->second;
    if(!enabled)
    {
        this->setOverlayVisible(id, false);
    }

    plugin.enabled = enabled;
    if(enabled)
    {
        plugin.restartDelay = 0;
        this->startProcess(plugin);
    }
    else
    {
        this->stopProcess(plugin);
    }

    this->saveState();
    emit pluginsChanged();
    return true;
}

bool PluginManager::setDock(const QString& id, bool dock)
{
    auto it = plugins_.find(id);
    if(it == plugins_.end())
    {
        return false;
    }

    if(it->second->dock != static_cast<int>(dock))
    {
        it->second->dock = dock;
        this->saveState();
        emit pluginsChanged();
    }
    return true;
}

bool PluginManager::restart(const QString& id)
{
    auto it = plugins_.find(id);
    if(it == plugins_.end() || !it->second->enabled)
    {
        return false;
    }

    this->stopProcess(*it->second);
    it->second->restartDelay = 0;
    this->startProcess(*it->second);
    return true;
}

bool PluginManager::setOverlayVisible(const QString& id, bool visible)
{
    auto it = plugins_.find(id);
    if(it == plugins_.end() || !it->second->enabled
       || it->second->manifest.value("ui").toMap().value("overlay").toString().isEmpty())
    {
        return false;
    }

    if(it->second->overlayVisible != visible)
    {
        it->second->overlayVisible = visible;
        emit overlayChanged(id, visible);
    }
    return true;
}

bool PluginManager::overlayVisible(const QString& id) const
{
    auto it = plugins_.find(id);
    return it != plugins_.end() && it->second->overlayVisible;
}

void PluginManager::unload(const QString& id)
{
    auto it = plugins_.find(id);
    if(it != plugins_.end())
    {
        this->stopProcess(*it->second);
        plugins_.erase(it);
        emit pluginsChanged();
    }
}

void PluginManager::setQmlCacheTrimmer(std::function<void()> trimmer)
{
    qmlCacheTrimmer_ = std::move(trimmer);
}

void PluginManager::trimQmlCache()
{
    if(qmlCacheTrimmer_)
    {
        qmlCacheTrimmer_();
    }
}

void PluginManager::startProcess(Plugin& plugin)
{
    if(!plugin.compatible)
    {
        return;
    }
    const QVariantMap process = plugin.manifest.value("process").toMap();
    const QString exec = process.value("exec").toString();
    if(exec.isEmpty() || (plugin.process && plugin.process->state() != QProcess::NotRunning))
    {
        return;
    }

    const QString program = QDir(plugin.dir).filePath(exec);
    if(!QFileInfo::exists(program))
    {
        OPENAUTO_LOG(error) << "[PluginManager] " << plugin.id.toStdString() << ": " << program.toStdString() << " not found";
        return;
    }

    // Scripts do not need the executable bit
    QString command = program;
    QStringList arguments = process.value("args").toStringList();
    if(!QFileInfo(program).isExecutable())
    {
        if(program.endsWith(".py"))
        {
            arguments.prepend(program);
            command = "python3";
        }
        else if(program.endsWith(".sh"))
        {
            arguments.prepend(program);
            command = "bash";
        }
    }

    const QString dataDir = baseDir() + "/plugin-data/" + plugin.id;
    const QString logDir = baseDir() + "/logs";
    QDir().mkpath(dataDir);
    QDir().mkpath(logDir);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("OPENHU_API_URL", QString("ws://127.0.0.1:%1").arg(apiPort_));
    environment.insert("OPENHU_PLUGIN_ID", plugin.id);
    environment.insert("OPENHU_PLUGIN_DIR", plugin.dir);
    environment.insert("OPENHU_DATA_DIR", dataDir);
    environment.insert("PYTHONUNBUFFERED", "1");

    plugin.process = std::make_unique<QProcess>();
    plugin.process->setProcessEnvironment(environment);
    plugin.process->setWorkingDirectory(plugin.dir);
    plugin.process->setStandardOutputFile(logDir + "/" + plugin.id + ".log", QIODevice::Append);
    plugin.process->setStandardErrorFile(logDir + "/" + plugin.id + ".log", QIODevice::Append);

    const QString id = plugin.id;
    connect(plugin.process.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, id](int exitCode, QProcess::ExitStatus status) { this->onProcessFinished(id, exitCode, status); });

    plugin.stopping = false;
    plugin.uptime.start();
    plugin.process->start(command, arguments);
    OPENAUTO_LOG(info) << "[PluginManager] started " << id.toStdString() << ": " << command.toStdString() << " "
                       << arguments.join(' ').toStdString();
}

void PluginManager::stopProcess(Plugin& plugin)
{
    plugin.restartTimer.stop();
    if(!plugin.process || plugin.process->state() == QProcess::NotRunning)
    {
        return;
    }

    plugin.stopping = true;
    plugin.process->terminate();
    if(!plugin.process->waitForFinished(3000))
    {
        plugin.process->kill();
        plugin.process->waitForFinished(1000);
    }
}

void PluginManager::onProcessFinished(const QString& id, int exitCode, QProcess::ExitStatus status)
{
    auto it = plugins_.find(id);
    if(it == plugins_.end())
    {
        return;
    }

    auto& plugin = *it->second;
    if(plugin.stopping)
    {
        // terminated by OpenHU (quit, disable, update): SIGTERM counts as CrashExit for Qt
        OPENAUTO_LOG(info) << "[PluginManager] " << id.toStdString() << " stopped";
        return;
    }
    OPENAUTO_LOG(info) << "[PluginManager] " << id.toStdString() << " exited (code " << exitCode
                       << (status == QProcess::CrashExit ? ", crashed" : "") << ")";
    if(!plugin.enabled)
    {
        return;
    }

    // restart: "on-failure" (default), "always" or "never"
    const QString policy = plugin.manifest.value("process").toMap().value("restart", "on-failure").toString();
    const bool failed = status == QProcess::CrashExit || exitCode != 0;
    if(policy == "never" || (policy == "on-failure" && !failed))
    {
        return;
    }

    // Back off for crash loops, start over once the process ran for a while.
    plugin.restartDelay = plugin.uptime.elapsed() > cMaxRestartDelayMs ? 1000
                          : std::min(cMaxRestartDelayMs, std::max(1000, plugin.restartDelay * 2));
    OPENAUTO_LOG(info) << "[PluginManager] restarting " << id.toStdString() << " in " << plugin.restartDelay << " ms";
    plugin.restartTimer.start(plugin.restartDelay);
}

QVariantMap PluginManager::toVariant(const Plugin& plugin) const
{
    const QVariantMap ui = plugin.manifest.value("ui").toMap();
    QVariantMap result;
    result["id"] = plugin.id;
    result["name"] = plugin.manifest.value("name", plugin.id);
    result["version"] = plugin.manifest.value("version");
    result["description"] = plugin.manifest.value("description");
    result["enabled"] = plugin.enabled;
    result["compatible"] = plugin.compatible;
    // QML of an incompatible plugin is not loaded
    result["page"] = plugin.compatible ? fileUrl(plugin.dir, ui.value("page").toString()) : QString();
    result["overlay"] = plugin.compatible ? fileUrl(plugin.dir, ui.value("overlay").toString()) : QString();
    result["icon"] = imageUrl(plugin.dir, ui.value("icon").toString());
    result["dock"] = plugin.dock >= 0 ? plugin.dock == 1 : ui.value("dock", false).toBool();
    result["overlayVisible"] = plugin.overlayVisible;
    result["running"] = plugin.process && plugin.process->state() != QProcess::NotRunning;
    result["permissions"] = plugin.manifest.value("permissions");
    return result;
}

void PluginManager::saveState() const
{
    QVariantMap state;
    for(const auto& entry: plugins_)
    {
        QVariantMap pluginState{{"enabled", entry.second->enabled}};
        if(entry.second->dock >= 0)
        {
            pluginState.insert("dock", entry.second->dock == 1);
        }
        state.insert(entry.first, pluginState);
    }

    QFile file(baseDir() + "/plugins.json");
    if(file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(state)).toJson());
    }
}

QVariantMap PluginManager::loadState() const
{
    return readJson(baseDir() + "/plugins.json");
}

}
}
}
}
