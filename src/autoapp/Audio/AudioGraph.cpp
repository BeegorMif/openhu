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


#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <f1x/openauto/autoapp/Audio/AudioGraph.hpp>
#include <f1x/openauto/autoapp/Audio/OutputRouter.hpp>
#include <f1x/openauto/autoapp/Core/ChildProcesses.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace audio
{

namespace
{

constexpr int cMaxRestartMs = 30000;

void run(QObject* parent, const QString& program, const QStringList& arguments)
{
    auto* process = new QProcess(parent);
    QObject::connect(process, &QProcess::finished, process, [process, program, arguments]() {
        if(process->exitStatus() != QProcess::NormalExit || process->exitCode() != 0)
        {
            OPENAUTO_LOG(warning) << "[AudioGraph] " << program.toStdString() << " " << arguments.join(' ').toStdString()
                                  << " failed: "
                                  << process->readAllStandardError().trimmed().toStdString();
        }
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
        {
            process->deleteLater();
        }
    });
    core::startChild(*process, program, arguments);
}

QString configText(const QList<AudioSource>& sources)
{
    return sources.isEmpty() ? QString() : filterChainConfig(sources);
}

}

AudioGraph::AudioGraph(QHash<QString, int> levels, std::function<void(const QHash<QString, int>&)> saveLevels,
                       QObject* parent)
    : QObject(parent)
    , levels_(std::move(levels))
    , saveLevels_(std::move(saveLevels))
{
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    configDir_ = (runtime.isEmpty() ? QDir::tempPath() : runtime) + "/openhu-audio";
    pollTimer_.setInterval(500);
    connect(&pollTimer_, &QTimer::timeout, this, &AudioGraph::list);
    subscribeTimer_.setSingleShot(true);
    subscribeTimer_.setInterval(1000);
    connect(&subscribeTimer_, &QTimer::timeout, this, &AudioGraph::subscribe);

    auto own = std::make_unique<Group>();
    own->sources = coreSources();
    groups_[QString()] = std::move(own);
}

AudioGraph::~AudioGraph()
{
    this->stop();
}

void AudioGraph::setConfigDir(const QString& dir)
{
    configDir_ = dir;
}

void AudioGraph::setTimings(int pollMs, int restartMs)
{
    pollTimer_.setInterval(pollMs);
    restartMs_ = restartMs;
}

void AudioGraph::start()
{
    if(started_)
    {
        return;
    }
    started_ = true;
    stopped_ = false;
    this->subscribe();
    // the sinks that exist already decide which groups need a process of their own (onListed)
    this->list();
    pollTimer_.start();
    this->publish();
}

void AudioGraph::stop()
{
    stopped_ = true;
    started_ = false;
    pollTimer_.stop();
    subscribeTimer_.stop();
    if(subscriber_)
    {
        subscriber_->disconnect(this);
        subscriber_->kill();
        subscriber_->waitForFinished(500);
        subscriber_.reset();
    }
    for(auto& entry: groups_)
    {
        this->stopGroup(*entry.second);
    }
    this->setPresent({});
    listed_ = false;
}

void AudioGraph::setPluginSources(const QHash<QString, QList<AudioSource>>& sources)
{
    bool changedAny = false;
    // gone: plugins removed, disabled, or without sound now
    for(auto it = groups_.begin(); it != groups_.end();)
    {
        if(!it->first.isEmpty() && sources.value(it->first).isEmpty())
        {
            this->stopGroup(*it->second);
            it = groups_.erase(it);
            changedAny = true;
        }
        else
        {
            ++it;
        }
    }
    for(auto it = sources.cbegin(); it != sources.cend(); ++it)
    {
        if(it.key().isEmpty() || it.value().isEmpty())
        {
            continue;
        }
        auto existing = groups_.find(it.key());
        if(existing != groups_.end() && configText(existing->second->sources) == configText(it.value()))
        {
            // same sinks; names or pause support may have changed
            existing->second->sources = it.value();
            continue;
        }
        if(existing != groups_.end())
        {
            this->stopGroup(*existing->second);
        }
        auto group = std::make_unique<Group>();
        group->sources = it.value();
        groups_[it.key()] = std::move(group);
        changedAny = true;
    }
    if(changedAny && started_ && listed_)
    {
        this->ensureGroups();
        pollTimer_.start();
    }
    this->publish();
}

void AudioGraph::ensureGroups()
{
    for(auto& entry: groups_)
    {
        Group& group = *entry.second;
        if(group.external || (group.process && group.process->state() != QProcess::NotRunning)
           || group.restartTimer.isActive())
        {
            continue;
        }
        bool allPresent = !group.sources.isEmpty();
        for(const AudioSource& source: group.sources)
        {
            allPresent = allPresent && present_.contains(source.sink);
        }
        if(allPresent)
        {
            // shipped by the OS (or left by another OpenHU): used as they are
            group.external = true;
            OPENAUTO_LOG(info) << "[AudioGraph] sinks of " << (entry.first.isEmpty() ? "OpenHU" : entry.first.toStdString())
                               << " exist already, using them";
            continue;
        }
        this->startGroup(entry.first, group);
    }
}

void AudioGraph::startGroup(const QString& name, Group& group)
{
    const QString program = QStandardPaths::findExecutable("pipewire");
    if(program.isEmpty())
    {
        if(!warnedMissing_)
        {
            warnedMissing_ = true;
            OPENAUTO_LOG(warning) << "[AudioGraph] pipewire not found: no own sinks for the audio sources, "
                                     "everything plays on the default output without levels";
        }
        return;
    }
    QDir().mkpath(configDir_);
    const QString path = configDir_ + "/" + (name.isEmpty() ? QString("openhu") : name) + ".conf";
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        OPENAUTO_LOG(error) << "[AudioGraph] cannot write " << path.toStdString();
        return;
    }
    // the current levels are the chains' start values: right from the first sample
    file.write(filterChainConfig(group.sources, levels_).toUtf8());
    file.close();

    group.stopping = false;
    group.complete = false;
    group.process = std::make_unique<QProcess>();
    group.process->setProcessChannelMode(QProcess::MergedChannels);
    connect(group.process.get(), &QProcess::finished, this, [this, name]() { this->onGroupFinished(name); });
    connect(group.process.get(), &QProcess::errorOccurred, this, [this, name](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
        {
            this->onGroupFinished(name);
        }
    });
    QStringList sinks;
    for(const AudioSource& source: group.sources)
    {
        sinks << source.sink;
    }
    OPENAUTO_LOG(info) << "[AudioGraph] creating " << sinks.join(", ").toStdString();
    core::startChild(*group.process, program, {"-c", path});
}

void AudioGraph::stopGroup(Group& group)
{
    group.restartTimer.stop();
    group.external = false;
    if(!group.process)
    {
        return;
    }
    group.stopping = true;
    if(group.process->state() != QProcess::NotRunning)
    {
        group.process->terminate();
        if(!group.process->waitForFinished(1000))
        {
            group.process->kill();
            group.process->waitForFinished(500);
        }
    }
    group.process.reset();
}

void AudioGraph::onGroupFinished(const QString& name)
{
    auto it = groups_.find(name);
    if(it == groups_.end())
    {
        return;
    }
    Group& group = *it->second;
    if(group.stopping || stopped_ || !group.process)
    {
        return;
    }
    const QString output = QString::fromUtf8(group.process->readAll()).trimmed().right(500);
    group.process.release()->deleteLater();
    group.restartDelay = std::min(cMaxRestartMs, std::max(restartMs_, group.restartDelay * 2));
    OPENAUTO_LOG(warning) << "[AudioGraph] pipewire for " << (name.isEmpty() ? "OpenHU" : name.toStdString())
                          << " ended, again in " << group.restartDelay << " ms: " << output.toStdString();
    QSet<QString> present = present_;
    for(const AudioSource& source: group.sources)
    {
        present.remove(source.sink);
    }
    this->setPresent(present);
    group.restartTimer.setSingleShot(true);
    group.restartTimer.disconnect();
    connect(&group.restartTimer, &QTimer::timeout, this, [this, name]() {
        auto again = groups_.find(name);
        if(again != groups_.end() && !stopped_)
        {
            this->startGroup(name, *again->second);
            pollTimer_.start();
        }
    });
    group.restartTimer.start(group.restartDelay);
    pollTimer_.start();
    this->publish();
}

void AudioGraph::restartGroup(const QString& name, Group& group)
{
    // the process lost PipeWire (it restarted): it does not end by itself and does not reconnect. Ended here,
    // onGroupFinished() starts it again.
    OPENAUTO_LOG(warning) << "[AudioGraph] sinks of " << (name.isEmpty() ? "OpenHU" : name.toStdString())
                          << " are gone (PipeWire restarted?), creating them again";
    group.complete = false;
    QProcess* process = group.process.get();
    process->terminate();
    QTimer::singleShot(1000, process, [process]() { process->kill(); });
}

void AudioGraph::subscribe()
{
    if(stopped_ || !started_)
    {
        return;
    }
    // sinks that come and go (also all at once: PipeWire restarted) are listed right away
    subscriber_ = std::make_unique<QProcess>();
    QProcess* process = subscriber_.get();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        bool sinks = false;
        while(process->canReadLine())
        {
            const QByteArray line = process->readLine();
            // "Event 'remove' on sink #83"; not the frequent sink-input events
            sinks = sinks || line.contains(" sink ") || line.contains(" server");
        }
        if(sinks)
        {
            this->list();
        }
    });
    // pipewire-pulse restarted (or no pactl): again later, the list catches what changed meanwhile
    const auto ended = [this, process]() {
        if(stopped_ || subscriber_.get() != process)
        {
            return;
        }
        subscriber_.release()->deleteLater();
        subscribeTimer_.start();
        this->list();
    };
    connect(process, &QProcess::finished, this, ended);
    connect(process, &QProcess::errorOccurred, this, [ended](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
        {
            ended();
        }
    });
    core::startChild(*subscriber_, "pactl", {"subscribe"});
}

void AudioGraph::setPresent(const QSet<QString>& sinks)
{
    present_ = sinks;
    audio::setPresentSinks(sinks);
}

void AudioGraph::list()
{
    if(stopped_)
    {
        return;
    }
    if(listing_)
    {
        listAgain_ = true;
        return;
    }
    listing_ = true;
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process]() {
        listing_ = false;
        const QByteArray output = process->readAllStandardOutput();
        const bool ok = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
        process->deleteLater();
        if(ok)
        {
            this->onListed(output);
        }
        if(listAgain_)
        {
            listAgain_ = false;
            this->list();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
        {
            listing_ = false;
            listAgain_ = false;
            process->deleteLater();
        }
    });
    core::startChild(*process, "pactl", {"list", "short", "sinks"});
}

void AudioGraph::onListed(const QByteArray& output)
{
    if(stopped_)
    {
        return;
    }
    QSet<QString> now;
    for(const Sink& sink: parseShortSinks(output))
    {
        now.insert(sink.name);
    }
    const QSet<QString> before = present_;
    this->setPresent(now);
    if(!listed_)
    {
        listed_ = true;
        this->ensureGroups();
    }

    for(auto& entry: groups_)
    {
        Group& group = *entry.second;
        if(!group.process || group.process->state() != QProcess::Running)
        {
            continue;
        }
        bool allPresent = true;
        for(const AudioSource& source: group.sources)
        {
            allPresent = allPresent && present_.contains(source.sink);
        }
        if(allPresent && !group.complete)
        {
            group.complete = true;
            group.restartDelay = 0;
        }
        else if(!allPresent && group.complete)
        {
            this->restartGroup(entry.first, group);
        }
    }

    bool allPresent = true;
    bool changedAny = false;
    for(const AudioSource& source: this->sourceList())
    {
        const bool there = present_.contains(source.sink);
        allPresent = allPresent && there;
        if(there && !before.contains(source.sink))
        {
            this->applyLevel(source);
            emit sinkReady(source.sink);
            changedAny = true;
        }
        else if(!there && before.contains(source.sink))
        {
            changedAny = true;
        }
    }
    if(allPresent)
    {
        pollTimer_.stop();
    }
    if(changedAny)
    {
        this->publish();
    }
}

void AudioGraph::applyLevel(const AudioSource& source)
{
    run(this, "pw-cli", setControlArguments(source.sink, "level:Mult", levelFactor(this->level(source.id))));
}

QList<AudioSource> AudioGraph::sourceList() const
{
    QList<AudioSource> result;
    for(const auto& entry: groups_)
    {
        result.append(entry.second->sources);
    }
    return result;
}

const AudioSource* AudioGraph::find(const QString& id) const
{
    for(const auto& entry: groups_)
    {
        for(const AudioSource& source: entry.second->sources)
        {
            if(source.id == id)
            {
                return &source;
            }
        }
    }
    return nullptr;
}

int AudioGraph::level(const QString& id) const
{
    return clampLevel(levels_.value(id, 0));
}

bool AudioGraph::setLevel(const QString& id, int dB)
{
    const AudioSource* source = this->find(id);
    if(source == nullptr)
    {
        return false;
    }
    const int level = clampLevel(dB);
    if(level == 0)
    {
        levels_.remove(id);
    }
    else
    {
        levels_.insert(id, level);
    }
    if(saveLevels_)
    {
        saveLevels_(levels_);
    }
    if(present_.contains(source->sink))
    {
        this->applyLevel(*source);
    }
    this->publish();
    return true;
}

QVariantList AudioGraph::sources() const
{
    QVariantList result;
    for(const AudioSource& source: this->sourceList())
    {
        result.append(QVariantMap{{"id", source.id},
                                  {"name", source.name},
                                  {"textId", source.textId},
                                  {"sink", source.sink},
                                  {"plugin", source.plugin},
                                  {"pluginName", source.pluginName},
                                  {"level", this->level(source.id)},
                                  {"canPause", source.canPause},
                                  {"present", present_.contains(source.sink)}});
    }
    return result;
}

void AudioGraph::publish()
{
    core::EventBus::instance().publish("audio.sources", {{"sources", this->sources()},
                                                         {"min", cMinSourceLevel},
                                                         {"max", cMaxSourceLevel}});
    emit changed();
}

}
}
}
}
