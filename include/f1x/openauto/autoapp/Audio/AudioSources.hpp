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

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace audio
{

// A source of sound with its own level (#154): every source plays into its own PipeWire filter-chain sink
// ("openhu.<id>"), whose output stream goes to the default sink. The chain is: a flat EQ for the EQ plugin
// ("eq1".."eq10"), the source level ("level", a linear gain) and ducking ("duck", a linear gain). Both gains are
// controls of the graph, not node volumes: PipeWire 1.4 drops a volume set while the node is suspended (no stream
// plays), the graph's controls keep their value, and their start values are in the config.
struct AudioSource
{
    QString id;       // aa.media, org.openhu.radio.fm, org.openhu.media
    QString sink;     // openhu.aa.media
    QString plugin;   // the plugin it belongs to, empty for OpenHU's own (Android Auto)
    QString pluginName;  // the plugin's English name (manifest), translated by the shell as "<plugin>.name"
    QString localId;  // id in the plugin's manifest ("fm"), empty for OpenHU's own and a plugin's default source
    QString name;     // English name (manifest), empty for OpenHU's own (the shell translates "textId")
    QString textId;   // translation: settings.audio.source.aa_media, org.openhu.radio.audio.fm
    bool canPause = false;  // pauses for real on request; a source that cannot is muted instead
};

// Levels are attenuation only, in dB: no gain above 100 %, nothing clips
constexpr int cMinSourceLevel = -20;
constexpr int cMaxSourceLevel = 0;
int clampLevel(int dB);
// linear amplitude factor of a level (-6 dB = 0.501), as pactl takes it ("0.501")
double levelFactor(int dB);

// the sink of a source: "openhu.<id>"
QString sinkName(const QString& sourceId);

// Android Auto's outputs: media, guidance, call, system
QList<AudioSource> coreSources();
// The sources a plugin declares ("audio": {"sources": [{"id", "name", "pause"}]}); a plugin with the "audio"
// permission and no list gets one source named after the plugin. Ids: a-z, 0-9, "-", "_", the first wins.
QList<AudioSource> pluginSources(const QString& pluginId, const QVariantMap& manifest);

// Environment for a plugin's process: OPENHU_AUDIO_SINK_<ID> per source (OPENHU_AUDIO_SINK for the default one),
// and PULSE_SINK / PIPEWIRE_NODE for the first source, so that pacat, mpv and others play there without options
QList<QPair<QString, QString>> sinkEnvironment(const QList<AudioSource>& sources);

// Center frequencies of the flat EQ in each chain (Hz), bands "eq1".."eq10"
QList<int> eqBands();

// PipeWire config that creates the chains of these sources, for "pipewire -c <file>" (like PipeWire's
// filter-chain.conf): the process connects to the running PipeWire, and the sinks exist while it runs.
// levels {source id: dB}: the start value of each chain's level
QString filterChainConfig(const QList<AudioSource>& sources, const QHash<QString, int>& levels = {});

// Arguments for pw-cli that set a control of a source's chain ("level:Mult" or "duck:Mult") to a linear factor
QStringList setControlArguments(const QString& sink, const QString& control, double value);

// The sinks AudioGraph found last, for any thread. A stream is opened only on a sink that exists: Qt 6.8 waits
// forever for a stream on a missing sink (QPulseAudioSink::open() waits for PA_STREAM_READY, also after
// PA_STREAM_FAILED), in the GUI thread, until the watchdog ends OpenHU.
void setPresentSinks(const QSet<QString>& sinks);
bool isSinkPresent(const QString& sink);

}
}
}
}
