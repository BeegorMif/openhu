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


#include <QAudioDevice>
#include <QFile>
#include <QMediaDevices>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <f1x/openauto/autoapp/Audio/AudioGraph.hpp>
#include <f1x/openauto/autoapp/Audio/AudioSources.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Projection/QtAudioOutput.hpp>

using namespace f1x::openauto::autoapp::audio;

namespace
{

// pactl replacement in front of PATH: the sinks are the file "sinks"
const char* cFakePactl = R"(#!/bin/sh
dir=$(dirname "$0")
case "$1" in
    list) [ -f "$dir/sinks" ] && cat "$dir/sinks" ;;
esac
exit 0
)";

// pw-cli replacement: control changes go to "log"
const char* cFakePwCli = R"(#!/bin/sh
echo "$@" >> "$(dirname "$0")/log"
exit 0
)";

// pipewire replacement: "creates" the sinks of the config (lines in "sinks") while it runs, removes them when it
// ends; with the file "crash" it ends at once (once)
const char* cFakePipewire = R"SH(#!/bin/sh
dir=$(dirname "$0")
conf="$2"
echo "$conf" >> "$dir/pipewire.log"
if [ -f "$dir/crash" ]; then rm "$dir/crash"; exit 1; fi
names=$(grep 'node.name = "' "$conf" | grep -v '\.output"' | sed 's/.*"\(.*\)".*/\1/')
for n in $names; do printf '7\t%s\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n' "$n" >> "$dir/sinks"; done
cleanup() {
    for n in $names; do grep -v "	$n	" "$dir/sinks" > "$dir/sinks.tmp"; mv "$dir/sinks.tmp" "$dir/sinks"; done
    exit 0
}
trap cleanup TERM
while :; do sleep 0.05; done
)SH";

QVariantMap manifest(const QByteArray& json)
{
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

}

class TestAudioSources: public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        dir_ = std::make_unique<QTemporaryDir>();
        QVERIFY(dir_->isValid());
        write("pactl", cFakePactl, true);
        write("pipewire", cFakePipewire, true);
        write("pw-cli", cFakePwCli, true);
        oldPath_ = qgetenv("PATH");
        qputenv("PATH", dir_->path().toLocal8Bit() + ":" + oldPath_);
    }

    void cleanup()
    {
        qputenv("PATH", oldPath_);
        dir_.reset();
    }

    void levelsAreAttenuationOnly()
    {
        QCOMPARE(clampLevel(5), 0);
        QCOMPARE(clampLevel(-30), -20);
        QCOMPARE(clampLevel(-6), -6);
        QVERIFY(qAbs(levelFactor(-6) - 0.501187) < 1e-5);
        QCOMPARE(levelFactor(0), 1.0);
        QCOMPARE(levelFactor(3), 1.0);
    }

    void coreSourcesAreAndroidAuto()
    {
        const auto sources = coreSources();
        QCOMPARE(sources.size(), 4);
        QCOMPARE(sources[0].id, QString("aa.media"));
        QCOMPARE(sources[0].sink, QString("openhu.aa.media"));
        QCOMPARE(sources[0].textId, QString("settings.audio.source.aa_media"));
        QVERIFY(sources[0].canPause);
        QCOMPARE(sources[1].sink, QString("openhu.aa.guidance"));
        QVERIFY(!sources[1].canPause);
        QCOMPARE(sinkName("aa.call"), QString("openhu.aa.call"));
    }

    void pluginDeclaresSources()
    {
        const auto sources = pluginSources("org.openhu.radio", manifest(R"({
            "name": "Radio", "permissions": ["audio"],
            "audio": {"sources": [
                {"id": "fm", "name": "FM"},
                {"id": "dab", "name": "DAB", "pause": true},
                {"id": "fm", "name": "twice"},
                {"id": "Bad Id", "name": "invalid"},
                {"name": "no id"}
            ]}})"));
        QCOMPARE(sources.size(), 2);
        QCOMPARE(sources[0].id, QString("org.openhu.radio.fm"));
        QCOMPARE(sources[0].sink, QString("openhu.org.openhu.radio.fm"));
        QCOMPARE(sources[0].plugin, QString("org.openhu.radio"));
        QCOMPARE(sources[0].pluginName, QString("Radio"));
        QCOMPARE(sources[0].localId, QString("fm"));
        QCOMPARE(sources[0].name, QString("FM"));
        QCOMPARE(sources[0].textId, QString("org.openhu.radio.audio.fm"));
        QVERIFY(!sources[0].canPause);
        QVERIFY(sources[1].canPause);
    }

    void audioPermissionGivesDefaultSource()
    {
        const auto sources = pluginSources("org.openhu.media", manifest(R"({"name": "Music", "permissions": ["audio", "usb"]})"));
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources[0].id, QString("org.openhu.media"));
        QCOMPARE(sources[0].sink, QString("openhu.org.openhu.media"));
        QCOMPARE(sources[0].name, QString("Music"));
        QCOMPARE(sources[0].textId, QString("org.openhu.media.name"));
        QVERIFY(sources[0].localId.isEmpty());

        QVERIFY(pluginSources("org.openhu.can", manifest(R"({"name": "CAN", "permissions": ["can"]})")).isEmpty());
    }

    void environmentNamesTheSinks()
    {
        const auto radio = sinkEnvironment(pluginSources("org.openhu.radio", manifest(R"({
            "audio": {"sources": [{"id": "fm"}, {"id": "dab-plus"}]}})")));
        const QList<QPair<QString, QString>> expected{
            {"OPENHU_AUDIO_SINK_FM", "openhu.org.openhu.radio.fm"},
            {"OPENHU_AUDIO_SINK_DAB_PLUS", "openhu.org.openhu.radio.dab-plus"},
            {"PULSE_SINK", "openhu.org.openhu.radio.fm"},
            {"PIPEWIRE_NODE", "openhu.org.openhu.radio.fm"}};
        QCOMPARE(radio, expected);

        const auto music = sinkEnvironment(pluginSources("org.openhu.media", manifest(R"({"permissions": ["audio"]})")));
        QCOMPARE(music.first(), (QPair<QString, QString>{"OPENHU_AUDIO_SINK", "openhu.org.openhu.media"}));
        QVERIFY(sinkEnvironment({}).isEmpty());
    }

    void configHasOneChainPerSource()
    {
        const QString config = filterChainConfig(coreSources());
        QCOMPARE(config.count("libpipewire-module-filter-chain"), 4);
        QVERIFY(config.contains("node.name = \"openhu.aa.media\""));
        QVERIFY(config.contains("node.name = \"openhu.aa.media.output\""));
        QVERIFY(config.contains("media.class = Audio/Sink"));
        // flat EQ: 10 bands, 9 links between them, then level and ducking, per chain
        QCOMPARE(config.count("label = bq_peaking"), 40);
        QCOMPARE(config.count("output = \"eq9:Out\" input = \"eq10:In\""), 4);
        QCOMPARE(config.count("output = \"eq10:Out\" input = \"level:In\""), 4);
        QCOMPARE(config.count("output = \"level:Out\" input = \"duck:In\""), 4);
        QCOMPARE(config.count("name = level label = linear control = { \"Mult\" = 1.0000"), 4);
        QVERIFY(filterChainConfig(coreSources(), {{"aa.call", -20}})
                    .contains("name = level label = linear control = { \"Mult\" = 0.1000"));
        QCOMPARE(setControlArguments("openhu.aa.media", "duck:Mult", 0.2),
                 (QStringList{"set-param", "openhu.aa.media", "Props", "{ params = [ \"duck:Mult\" 0.2000 ] }"}));
        QVERIFY(config.contains("\"Freq\" = 1000 \"Q\" = 1.41 \"Gain\" = 0.0"));
        QVERIFY(!config.contains('%'));
    }

    // No sinks yet: OpenHU's own process creates them, the levels are set when they appear
    void createsSinksAndSetsLevels()
    {
        QHash<QString, int> saved;
        AudioGraph graph({{"aa.media", -6}}, [&saved](const QHash<QString, int>& levels) { saved = levels; });
        graph.setConfigDir(dir_->filePath("conf"));
        graph.setTimings(30, 50);
        QSignalSpy ready(&graph, &AudioGraph::sinkReady);
        graph.start();
        QTRY_COMPARE(ready.count(), 4);
        QVERIFY(QFile::exists(dir_->filePath("conf/openhu.conf")));
        QTRY_VERIFY(log().contains(level("openhu.aa.media", "0.5012")));
        QVERIFY(log().contains(level("openhu.aa.guidance", "1.0000")));
        // the start values are in the config too
        QVERIFY(readFile("conf/openhu.conf").contains("label = linear control = { \"Mult\" = 0.5012"));
        for(const QVariant& source: graph.sources())
        {
            QVERIFY(source.toMap().value("present").toBool());
        }
        const QVariantList published = f1x::openauto::autoapp::core::EventBus::instance().last("audio.sources")
                                           .value("sources").toList();
        QCOMPARE(published.size(), 4);
        QCOMPARE(published.first().toMap().value("level").toInt(), -6);

        // a change is applied at once and stored; 0 dB is not stored
        QVERIFY(graph.setLevel("aa.guidance", -30));
        QCOMPARE(graph.level("aa.guidance"), -20);
        QTRY_VERIFY(log().contains(level("openhu.aa.guidance", "0.1000")));
        QCOMPARE(saved.value("aa.guidance"), -20);
        QVERIFY(graph.setLevel("aa.media", 0));
        QVERIFY(!saved.contains("aa.media"));
        QVERIFY(!graph.setLevel("nothing", -3));

        graph.stop();
        QVERIFY(!readFile("sinks").contains("openhu.aa.media"));
    }

    // Sinks shipped by the OS: used as they are, no process of our own
    void usesExistingSinks()
    {
        QByteArray sinks;
        for(const AudioSource& source: coreSources())
        {
            sinks += "3\t" + source.sink.toUtf8() + "\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n";
        }
        write("sinks", sinks);
        AudioGraph graph({{"aa.system", -10}}, nullptr);
        graph.setConfigDir(dir_->filePath("conf"));
        graph.setTimings(30, 50);
        graph.start();
        QTRY_VERIFY(log().contains(level("openhu.aa.system", "0.3162")));
        QVERIFY(!QFile::exists(dir_->filePath("pipewire.log")));
    }

    // A plugin's sources get their own process; it ends when the plugin's sources go away
    void pluginSinksComeAndGo()
    {
        AudioGraph graph({}, nullptr);
        graph.setConfigDir(dir_->filePath("conf"));
        graph.setTimings(30, 50);
        graph.start();
        QTRY_VERIFY(readFile("sinks").contains("openhu.aa.media"));

        const auto radio = pluginSources("org.openhu.radio", manifest(R"({"audio": {"sources": [{"id": "fm"}, {"id": "dab"}]}})"));
        graph.setPluginSources({{"org.openhu.radio", radio}});
        QTRY_VERIFY(readFile("sinks").contains("openhu.org.openhu.radio.dab"));
        QVERIFY(QFile::exists(dir_->filePath("conf/org.openhu.radio.conf")));
        QTRY_COMPARE(graph.sources().size(), 6);
        QTRY_VERIFY(graph.sources().last().toMap().value("present").toBool());

        // the same sources again: nothing restarts
        const int starts = readFile("pipewire.log").count('\n');
        graph.setPluginSources({{"org.openhu.radio", radio}});
        QTest::qWait(150);
        QCOMPARE(readFile("pipewire.log").count('\n'), starts);

        graph.setPluginSources({});
        QCOMPARE(graph.sources().size(), 4);
        QVERIFY(!readFile("sinks").contains("openhu.org.openhu.radio"));
        QVERIFY(readFile("sinks").contains("openhu.aa.media"));
        graph.stop();
    }

    void restartsAfterCrash()
    {
        write("crash", "");
        AudioGraph graph({}, nullptr);
        graph.setConfigDir(dir_->filePath("conf"));
        graph.setTimings(30, 50);
        QSignalSpy ready(&graph, &AudioGraph::sinkReady);
        graph.start();
        QTRY_COMPARE(ready.count(), 4);
        QCOMPARE(readFile("pipewire.log").count('\n'), 2);
        graph.stop();
    }

    // With OPENHU_TEST_PULSE=1, PipeWire and pipewire-pulse reachable (the host's, docker/dev.sh test mounts them):
    // the real chains appear, Qt sees them under the sink's name, the level arrives, and they go away at the end
    void realPipeWire()
    {
        if(qgetenv("OPENHU_TEST_PULSE") != "1")
        {
            QSKIP("OPENHU_TEST_PULSE=1 for the test with the real PipeWire");
        }
        qputenv("PATH", oldPath_);
        AudioGraph graph({{"aa.media", -6}}, nullptr);
        graph.setConfigDir(dir_->filePath("conf"));
        QSignalSpy ready(&graph, &AudioGraph::sinkReady);
        graph.start();
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 4, 10000);

        const auto pactl = [](const QStringList& arguments) {
            QProcess process;
            process.start("pactl", arguments);
            process.waitForFinished(3000);
            return QString::fromUtf8(process.readAllStandardOutput());
        };
        // the level is the chain's control (pw-cli enum-params: the name, then its value)
        const auto control = [](const QString& sink, const QString& name) {
            QProcess process;
            process.start("pw-cli", {"enum-params", sink, "Props"});
            process.waitForFinished(3000);
            const QStringList lines = QString::fromUtf8(process.readAllStandardOutput()).split('\n');
            for(int i = 0; i + 1 < lines.size(); ++i)
            {
                if(lines[i].contains('"' + name + '"'))
                {
                    return lines[i + 1].trimmed();
                }
            }
            return QString();
        };
        QTRY_COMPARE_WITH_TIMEOUT(control("openhu.aa.media", "level:Mult"), QString("Float 0.501200"), 5000);
        QCOMPARE(control("openhu.aa.guidance", "level:Mult"), QString("Float 1.000000"));
        QVERIFY(graph.setLevel("aa.guidance", -20));
        QTRY_COMPARE_WITH_TIMEOUT(control("openhu.aa.guidance", "level:Mult"), QString("Float 0.100000"), 5000);

        // the Android Auto output opens its stream on the sink
        QTRY_VERIFY_WITH_TIMEOUT(
            f1x::openauto::autoapp::projection::QtAudioOutput::deviceFor("openhu.aa.media", QMediaDevices::audioOutputs(),
                                                                         QAudioDevice()).id() == "openhu.aa.media",
            5000);

        graph.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!pactl({"list", "short", "sinks"}).contains("openhu.aa.media"), 5000);
    }

private:
    void write(const QString& name, const QByteArray& content, bool executable = false)
    {
        QFile file(dir_->filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
        file.close();
        if(executable)
        {
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
    }

    QByteArray readFile(const QString& name) const
    {
        QFile file(dir_->filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    QByteArray log() const { return readFile("log"); }
    static QByteArray level(const char* sink, const char* value)
    {
        return QByteArray("set-param ") + sink + " Props { params = [ \"level:Mult\" " + value + " ] }";
    }

    std::unique_ptr<QTemporaryDir> dir_;
    QByteArray oldPath_;
};

QTEST_MAIN(TestAudioSources)
#include "test_audiosources.moc"
