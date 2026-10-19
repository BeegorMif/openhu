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

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QUrl>
#include <QUuid>
#include <f1x/openauto/autoapp/Core/ChildProcesses.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Plugins/AndroidAutoProviders.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginStore.hpp>
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
    connect(this, &PluginManager::pluginsChanged, this, &PluginManager::publishState);
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
    QStringList known;
    for(const auto& entry: plugins_)
    {
        known.append(entry.first);
    }
    plugins_.clear();
    bool stateChanged = false;

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
        QStringList ignored;
        plugin->androidAuto = parseAndroidAuto(manifest, &ignored);
        if(!ignored.isEmpty())
        {
            OPENAUTO_LOG(warning) << "[PluginManager] " << id.toStdString() << ": unknown in androidAuto, ignored: "
                                  << ignored.join(", ").toStdString();
        }
        // installed while the phone is connected: it may only start providing at the next connection
        if(!plugin->androidAuto.isEmpty() && !known.contains(id) && androidAutoConnected() && plugin->enabled)
        {
            OPENAUTO_LOG(info) << "[PluginManager] " << id.toStdString()
                               << " provides to Android Auto: installed disabled while the phone is connected";
            plugin->enabled = false;
            stateChanged = true;
        }
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

    this->startEnabled();

    if(stateChanged)
    {
        this->saveState();
    }
    emit pluginsChanged();
    // overlays shown before the reload are gone
    this->publishOverlays();
}

void PluginManager::stopAll()
{
    std::vector<Plugin*> plugins;
    for(auto& entry: plugins_)
    {
        plugins.push_back(entry.second.get());
    }
    this->stopProcesses(plugins);
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

QString PluginManager::changeBlocked(const QString& id) const
{
    auto it = plugins_.find(id);
    if(it == plugins_.end() || !androidAutoConnected())
    {
        return QString();
    }
    // also a plugin that a locked one requires (e.g. the CAN plugin under the vehicle plugin, #104)
    QStringList affected = this->dependents(id);
    affected.prepend(id);
    for(const auto& name: affected)
    {
        auto plugin = plugins_.find(name);
        if(plugin != plugins_.end() && !plugin->second->androidAuto.isEmpty())
        {
            return QStringLiteral("android auto connected");
        }
    }
    return QString();
}

bool PluginManager::androidAutoConnected()
{
    return core::EventBus::instance().last("projection.state").value("connected").toBool();
}

QString PluginManager::pluginForToken(const QString& token) const
{
    if(token.isEmpty())
    {
        return QString();
    }
    for(const auto& entry: plugins_)
    {
        if(entry.second->token == token)
        {
            return entry.first;
        }
    }
    return QString();
}

QVariantMap PluginManager::requiresOf(const QVariantMap& manifest)
{
    const QVariant value = manifest.value("requires");
    if(value.typeId() == QMetaType::QVariantList || value.typeId() == QMetaType::QStringList)
    {
        QVariantMap result;
        for(const auto& id: value.toStringList())
        {
            result.insert(id, QString());
        }
        return result;
    }
    return value.toMap();
}

QStringList PluginManager::recommendsOf(const QVariantMap& manifest)
{
    return manifest.value("recommends").toStringList();
}

QStringList PluginManager::dependents(const QString& id) const
{
    QStringList result;
    QStringList queue{id};
    while(!queue.isEmpty())
    {
        const QString current = queue.takeFirst();
        for(const auto& entry: plugins_)
        {
            if(entry.second->enabled && !result.contains(entry.first) && entry.first != id
               && requiresOf(entry.second->manifest).contains(current))
            {
                result.append(entry.first);
                queue.append(entry.first);
            }
        }
    }
    return result;
}

QVariantList PluginManager::missingRequirements(const QString& id) const
{
    QVariantList missing;
    auto it = plugins_.find(id);
    if(it == plugins_.end())
    {
        return missing;
    }
    const QVariantMap requirements = requiresOf(it->second->manifest);
    for(auto req = requirements.cbegin(); req != requirements.cend(); ++req)
    {
        const QString version = req.value().toString();
        auto required = plugins_.find(req.key());
        QString reason;
        if(required == plugins_.end())
        {
            reason = "notInstalled";
        }
        else if(!version.isEmpty()
                && PluginStore::compareVersions(required->second->manifest.value("version").toString(), version) < 0)
        {
            reason = "tooOld";
        }
        else if(!required->second->compatible)
        {
            reason = "incompatible";
        }
        else if(!required->second->enabled)
        {
            reason = "disabled";
        }
        if(!reason.isEmpty())
        {
            missing.append(QVariantMap{{"id", req.key()}, {"version", version}, {"reason", reason}});
        }
    }
    return missing;
}

void PluginManager::startEnabled()
{
    // required ones first: start what is ready, again until nothing changes (a cycle never starts)
    QStringList started;
    bool progress = true;
    while(progress)
    {
        progress = false;
        for(auto& entry: plugins_)
        {
            Plugin& plugin = *entry.second;
            if(!plugin.enabled || !plugin.compatible || started.contains(entry.first)
               || !this->missingRequirements(entry.first).isEmpty())
            {
                continue;
            }
            bool ready = true;
            for(const auto& required: requiresOf(plugin.manifest).keys())
            {
                ready = ready && started.contains(required);
            }
            if(ready)
            {
                this->startProcess(plugin);
                started.append(entry.first);
                progress = true;
            }
        }
    }
    for(const auto& entry: plugins_)
    {
        const QVariantList missing = this->missingRequirements(entry.first);
        if(entry.second->enabled && !missing.isEmpty())
        {
            QStringList names;
            for(const auto& item: missing)
            {
                names.append(item.toMap().value("id").toString() + " (" + item.toMap().value("reason").toString() + ")");
            }
            OPENAUTO_LOG(warning) << "[PluginManager] " << entry.first.toStdString() << " not started, requires "
                                  << names.join(", ").toStdString();
        }
    }
}

bool PluginManager::setEnabled(const QString& id, bool enabled)
{
    auto it = plugins_.find(id);
    if(it == plugins_.end() || !this->changeBlocked(id).isEmpty())
    {
        return false;
    }

    auto& plugin = *it->second;
    if(enabled)
    {
        plugin.enabled = true;
        plugin.restartDelay = 0;
        // what it requires comes along (unless it is locked by an Android Auto session, #123)
        QStringList queue = requiresOf(plugin.manifest).keys();
        while(!queue.isEmpty())
        {
            auto required = plugins_.find(queue.takeFirst());
            if(required != plugins_.end() && !required->second->enabled && this->changeBlocked(required->first).isEmpty())
            {
                OPENAUTO_LOG(info) << "[PluginManager] enabling " << required->first.toStdString() << ", required by "
                                   << id.toStdString();
                required->second->enabled = true;
                required->second->restartDelay = 0;
                queue.append(requiresOf(required->second->manifest).keys());
            }
        }
        this->startEnabled();
    }
    else
    {
        // what requires it stops too
        QStringList stopping = this->dependents(id);
        stopping.prepend(id);
        for(const auto& name: stopping)
        {
            auto entry = plugins_.find(name);
            if(entry == plugins_.end() || (name != id && !this->changeBlocked(name).isEmpty()))
            {
                continue;
            }
            this->setOverlayVisible(name, false);
            entry->second->enabled = false;
            this->stopProcess(*entry->second);
        }
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
        this->publishOverlays();
    }
    return true;
}

void PluginManager::publishOverlays()
{
    // Android Auto tells the phone that the projection is covered (video focus NATIVE, #76)
    QStringList overlays;
    for(const auto& entry: plugins_)
    {
        if(entry.second->overlayVisible)
        {
            overlays.append(entry.first);
        }
    }
    if(core::EventBus::instance().last("ui.overlay").value("overlays").toStringList() != overlays)
    {
        core::EventBus::instance().publish("ui.overlay", {{"overlays", overlays}});
    }
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
        this->publishOverlays();
    }
}

void PluginManager::setQmlCacheClearer(std::function<void()> clearer)
{
    qmlCacheClearer_ = std::move(clearer);
}

void PluginManager::clearQmlCache()
{
    if(qmlCacheClearer_)
    {
        qmlCacheClearer_();
    }
}

void PluginManager::dropQmlCache(QQmlEngine& engine)
{
    engine.clearComponentCache();
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
    // the plugin logs in with it, so its values count as its own (Android Auto providers, #123)
    plugin.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    environment.insert("OPENHU_PLUGIN_TOKEN", plugin.token);
    environment.insert("OPENHU_DATA_DIR", dataDir);
    // at the start; a switch later comes as the topic ui.language
    environment.insert("OPENHU_LANGUAGE", core::EventBus::instance().last("ui.language").value("language").toString());
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
    // own process group: stopping the plugin also ends what it started
    core::startChild(*plugin.process, command, arguments, core::ChildGroup::OwnGroup);
    OPENAUTO_LOG(info) << "[PluginManager] started " << id.toStdString() << ": " << command.toStdString() << " "
                       << arguments.join(' ').toStdString();
}

void PluginManager::stopProcess(Plugin& plugin)
{
    this->stopProcesses({&plugin});
}

// All at once with one deadline (OpenHU's shutdown, also under systemd's stop timeout): SIGTERM to each plugin's
// process group, 3 s for all, then SIGKILL and 1 s.
void PluginManager::stopProcesses(const std::vector<Plugin*>& plugins)
{
    std::vector<Plugin*> running;
    std::vector<qint64> groups;
    for(auto* plugin: plugins)
    {
        plugin->restartTimer.stop();
        if(plugin->process && plugin->process->state() != QProcess::NotRunning)
        {
            plugin->stopping = true;
            groups.push_back(core::terminateGroup(*plugin->process));
            running.push_back(plugin);
        }
    }

    const auto wait = [&running](int ms) {
        QDeadlineTimer deadline(ms);
        std::vector<Plugin*> left;
        for(auto* plugin: running)
        {
            if(!plugin->process->waitForFinished(int(std::max<qint64>(1, deadline.remainingTime()))))
            {
                left.push_back(plugin);
            }
        }
        running = left;
    };
    wait(3000);
    // also what a plugin started and left behind after its own end
    for(const qint64 group: groups)
    {
        core::killGroup(group);
    }
    for(auto* plugin: running)
    {
        plugin->process->kill();
    }
    wait(1000);
}

void PluginManager::onProcessFinished(const QString& id, int exitCode, QProcess::ExitStatus status)
{
    auto it = plugins_.find(id);
    if(it == plugins_.end())
    {
        return;
    }

    auto& plugin = *it->second;
    plugin.token.clear();
    // its vehicle data is not current any more
    core::EventBus::instance().releaseSource(core::EventBus::sourcePlugin(id));
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
    result["requires"] = requiresOf(plugin.manifest);
    result["recommends"] = recommendsOf(plugin.manifest);
    result["missing"] = this->missingRequirements(plugin.id);
    if(!plugin.androidAuto.isEmpty())
    {
        // enable, disable, update and remove wait for the phone to disconnect (changeBlocked)
        result["androidAuto"] = plugin.androidAuto;
    }
    return result;
}

void PluginManager::publishState()
{
    core::EventBus::instance().publish("plugins.state", {{"plugins", this->plugins()}});
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
