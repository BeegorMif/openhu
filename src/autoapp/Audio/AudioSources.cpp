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


#include <algorithm>
#include <cmath>
#include <mutex>
#include <QRegularExpression>
#include <QtGlobal>
#include <QSet>
#include <f1x/openauto/autoapp/Audio/AudioSources.hpp>

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

const QString cSinkPrefix = QStringLiteral("openhu.");

AudioSource core(const QString& id, const char* textId, bool canPause)
{
    AudioSource source;
    source.id = id;
    source.sink = sinkName(id);
    source.textId = QString::fromLatin1(textId);
    source.canPause = canPause;
    return source;
}

bool validLocalId(const QString& id)
{
    static const QRegularExpression pattern("^[a-z0-9][a-z0-9_-]{0,31}$");
    return pattern.match(id).hasMatch();
}

QString envSuffix(const QString& localId)
{
    return localId.toUpper().replace('-', '_');
}

}

QString sinkName(const QString& sourceId)
{
    return cSinkPrefix + sourceId;
}

int clampLevel(int dB)
{
    return std::clamp(dB, cMinSourceLevel, cMaxSourceLevel);
}

double levelFactor(int dB)
{
    return std::pow(10.0, std::min(dB, 0) / 20.0);
}

double sourceFactor(const AudioSource& source, int userLevel)
{
    return levelFactor(clampLevel(source.baseLevel) + clampLevel(userLevel));
}

QList<AudioSource> coreSources()
{
    // Android Auto pauses media itself (audio focus); the others are short or must not be held back
    return {core("aa.media", QT_TRID_NOOP("settings.audio.source.aa_media"), true),
            core("aa.guidance", QT_TRID_NOOP("settings.audio.source.aa_guidance"), false),
            core("aa.call", QT_TRID_NOOP("settings.audio.source.aa_call"), false),
            core("aa.system", QT_TRID_NOOP("settings.audio.source.aa_system"), false)};
}

QList<AudioSource> pluginSources(const QString& pluginId, const QVariantMap& manifest)
{
    QList<AudioSource> sources;
    QSet<QString> seen;
    for(const QVariant& entry: manifest.value("audio").toMap().value("sources").toList())
    {
        const QVariantMap map = entry.toMap();
        const QString localId = map.value("id").toString();
        if(!validLocalId(localId) || seen.contains(localId))
        {
            continue;
        }
        seen.insert(localId);
        AudioSource source;
        source.id = pluginId + '.' + localId;
        source.sink = sinkName(source.id);
        source.plugin = pluginId;
        source.pluginName = manifest.value("name", pluginId).toString();
        source.localId = localId;
        source.name = map.value("name", localId).toString();
        source.textId = pluginId + ".audio." + localId;
        source.canPause = map.value("pause", false).toBool();
        source.baseLevel = clampLevel(map.value("level", 0).toInt());
        sources.append(source);
    }
    if(sources.isEmpty() && manifest.value("permissions").toStringList().contains("audio"))
    {
        AudioSource source;
        source.id = pluginId;
        source.sink = sinkName(pluginId);
        source.plugin = pluginId;
        source.pluginName = manifest.value("name", pluginId).toString();
        source.name = source.pluginName;
        source.textId = pluginId + ".name";
        source.baseLevel = clampLevel(manifest.value("audio").toMap().value("level", 0).toInt());
        sources.append(source);
    }
    return sources;
}

QList<QPair<QString, QString>> sinkEnvironment(const QList<AudioSource>& sources)
{
    QList<QPair<QString, QString>> environment;
    for(const AudioSource& source: sources)
    {
        environment.append({source.localId.isEmpty() ? QString("OPENHU_AUDIO_SINK")
                                                     : "OPENHU_AUDIO_SINK_" + envSuffix(source.localId),
                            source.sink});
    }
    if(!sources.isEmpty())
    {
        environment.append({"PULSE_SINK", sources.first().sink});
        environment.append({"PIPEWIRE_NODE", sources.first().sink});
    }
    return environment;
}

QStringList setControlArguments(const QString& sink, const QString& control, double value)
{
    return {"set-param", sink, "Props",
            QString("{ params = [ \"%1\" %2 ] }").arg(control).arg(value, 0, 'f', 4)};
}

namespace
{
std::mutex presentMutex;
QSet<QString> presentSinks;
}

void setPresentSinks(const QSet<QString>& sinks)
{
    std::lock_guard<std::mutex> lock(presentMutex);
    presentSinks = sinks;
}

bool isSinkPresent(const QString& sink)
{
    std::lock_guard<std::mutex> lock(presentMutex);
    return presentSinks.contains(sink);
}

QList<int> eqBands()
{
    return {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
}

QString filterChainConfig(const QList<AudioSource>& sources, const QHash<QString, int>& levels)
{
    const QList<int> bands = eqBands();
    QString nodes;
    QString links;
    for(int i = 0; i < bands.size(); ++i)
    {
        nodes += QString("          { type = builtin name = eq%1 label = bq_peaking "
                         "control = { \"Freq\" = %2 \"Q\" = 1.41 \"Gain\" = 0.0 } }\n")
                     .arg(i + 1)
                     .arg(bands[i]);
        if(i > 0)
        {
            links += QString("          { output = \"eq%1:Out\" input = \"eq%2:In\" }\n").arg(i).arg(i + 1);
        }
    }
    // after the EQ: the source level, then ducking
    links += QString("          { output = \"eq%1:Out\" input = \"level:In\" }\n").arg(bands.size());
    links += "          { output = \"level:Out\" input = \"duck:In\" }\n";

    QString config =
        "# Generated by OpenHU (audio sources, #154): one filter-chain sink per source. Do not edit.\n"
        "context.properties = { log.level = 2 }\n"
        "context.spa-libs = {\n"
        "  audio.convert.* = audioconvert/libspa-audioconvert\n"
        "  support.* = support/libspa-support\n"
        "}\n"
        "context.modules = [\n"
        "  { name = libpipewire-module-rt args = { } flags = [ ifexists nofail ] }\n"
        "  { name = libpipewire-module-protocol-native }\n"
        "  { name = libpipewire-module-client-node }\n"
        "  { name = libpipewire-module-adapter }\n";
    for(const AudioSource& source: sources)
    {
        config += QString(
                      "  { name = libpipewire-module-filter-chain\n"
                      "    args = {\n"
                      "      node.description = \"OpenHU %1\"\n"
                      "      media.name = \"OpenHU %1\"\n"
                      "      filter.graph = {\n"
                      "        nodes = [\n%2"
                      "        ]\n"
                      "        links = [\n%3"
                      "        ]\n"
                      "      }\n"
                      "      audio.channels = 2\n"
                      "      audio.position = [ FL FR ]\n"
                      // the sink. Lowest priority, so that the session manager never makes it the default output
                      "      capture.props = {\n"
                      "        node.name = \"%4\"\n"
                      "        media.class = Audio/Sink\n"
                      "        node.passive = true\n"
                      "        priority.session = 0\n"
                      "        priority.driver = 0\n"
                      "        openhu.source = \"%1\"\n"
                      "      }\n"
                      // the output follows the default sink
                      "      playback.props = {\n"
                      "        node.name = \"%4.output\"\n"
                      "        node.passive = true\n"
                      "        openhu.source = \"%1\"\n"
                      "      }\n"
                      "    }\n"
                      "  }\n")
                      .arg(source.id,
                           nodes
                               + QString("          { type = builtin name = level label = linear "
                                         "control = { \"Mult\" = %1 \"Add\" = 0.0 } }\n"
                                         "          { type = builtin name = duck label = linear "
                                         "control = { \"Mult\" = 1.0 \"Add\" = 0.0 } }\n")
                                     .arg(sourceFactor(source, levels.value(source.id, 0)), 0, 'f', 4),
                           links, source.sink);
    }
    config += "]\n";
    return config;
}

}
}
}
}
