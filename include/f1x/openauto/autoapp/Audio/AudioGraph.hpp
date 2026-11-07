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

#include <functional>
#include <map>
#include <memory>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <f1x/openauto/autoapp/Audio/AudioSources.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace audio
{

// The sinks of the audio sources (#154) and their levels. Each group of sources (OpenHU's own, each plugin's) is a
// "pipewire -c <generated config>" child process that connects to the running PipeWire; its sinks exist while it
// runs. Sinks that exist already with these names (shipped by the OS) are used as they are, without a process.
// The level of a source is the "level" control of its chain: the start value in the config, changes with pw-cli
// (also when a sink appears, for sinks the OS created).
// Publishes "audio.sources" {sources: [{id, name, textId, sink, plugin, pluginName, level, canPause, present}]}.
class AudioGraph: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)

public:
    // levels: the stored levels {source id: dB}, saveLevels stores them (configuration)
    AudioGraph(QHash<QString, int> levels, std::function<void(const QHash<QString, int>&)> saveLevels,
               QObject* parent = nullptr);
    ~AudioGraph() override;

    // Creates OpenHU's own sinks and those of the plugins set so far
    void start();
    // Ends the processes (shutdown, restart): their sinks go away
    void stop();

    // The plugins' sources {plugin id: sources}; a plugin whose sources changed gets a new process
    void setPluginSources(const QHash<QString, QList<AudioSource>>& sources);

    QVariantList sources() const;
    QList<AudioSource> sourceList() const;
    int level(const QString& id) const;
    // dB, clamped to cMinSourceLevel..cMaxSourceLevel; false for an unknown source
    Q_INVOKABLE bool setLevel(const QString& id, int dB);

    // Tests: where the configs are written, the delays in ms
    void setConfigDir(const QString& dir);
    void setTimings(int pollMs, int restartMs);

signals:
    void changed();
    // a sink appeared (its level is set)
    void sinkReady(const QString& sink);

private:
    struct Group
    {
        QList<AudioSource> sources;
        std::unique_ptr<QProcess> process;
        QTimer restartTimer;
        int restartDelay = 0;
        bool external = false;  // the sinks existed already: no process of our own
        bool stopping = false;
    };

    void ensureGroups();
    void startGroup(const QString& name, Group& group);
    void stopGroup(Group& group);
    void onGroupFinished(const QString& name);
    void list();
    void onListed(const QByteArray& output);
    void applyLevel(const AudioSource& source);
    void publish();
    const AudioSource* find(const QString& id) const;

    QHash<QString, int> levels_;
    std::function<void(const QHash<QString, int>&)> saveLevels_;
    std::map<QString, std::unique_ptr<Group>> groups_;  // "" = OpenHU's own
    QSet<QString> present_;                             // sinks seen in the last list
    bool listed_ = false;                               // the sinks were listed once (external ones known)
    bool listing_ = false;
    bool started_ = false;
    bool stopped_ = false;
    bool warnedMissing_ = false;
    QString configDir_;
    QTimer pollTimer_;
    int restartMs_ = 1000;
};

}
}
}
}
