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

#include <algorithm>
#include <memory>
#include <utility>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <f1x/openauto/autoapp/Audio/DuckingController.hpp>
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

// a stream counts as lowered by an earlier run when every channel is clearly below what it had before (0.5 %)
constexpr quint32 cTolerance = 328;

bool below(const QList<quint32>& volumes, const QList<quint32>& original)
{
    if(volumes.size() != original.size() || volumes.isEmpty())
    {
        return false;
    }
    for(int i = 0; i < volumes.size(); ++i)
    {
        if(volumes[i] + cTolerance >= original[i])
        {
            return false;
        }
    }
    return true;
}

QString describe(const SinkInput& input)
{
    return QStringLiteral("#%1 (%2/%3, %4 pid %5)")
        .arg(input.index)
        .arg(input.appName, input.mediaName, input.binary.isEmpty() ? QStringLiteral("?") : input.binary)
        .arg(input.pid);
}

QStringList volumeArguments(quint32 index, const QList<quint32>& volumes)
{
    QStringList arguments{QStringLiteral("set-sink-input-volume"), QString::number(index)};
    for(const quint32 volume: volumes)
    {
        arguments << QString::number(volume);
    }
    return arguments;
}

}

AndroidAutoAudio& AndroidAutoAudio::instance()
{
    // never destroyed (Android Auto threads may still report while the process ends), lives in the GUI thread
    static AndroidAutoAudio* hub = []() {
        auto* object = new AndroidAutoAudio;
        if(auto* application = QCoreApplication::instance())
        {
            object->moveToThread(application->thread());
        }
        return object;
    }();
    return *hub;
}

void AndroidAutoAudio::reportFocusRequest(int type)
{
    QMetaObject::invokeMethod(this, [this, type]() { emit focusRequested(type); }, Qt::QueuedConnection);
}

void AndroidAutoAudio::reportGuidance(bool playing)
{
    QMetaObject::invokeMethod(this, [this, playing]() { emit guidanceChanged(playing); }, Qt::QueuedConnection);
}

void AndroidAutoAudio::reportMicrophone(bool open)
{
    QMetaObject::invokeMethod(this, [this, open]() { emit microphoneChanged(open); }, Qt::QueuedConnection);
}

void AndroidAutoAudio::reportSessionEnded()
{
    QMetaObject::invokeMethod(this, [this]() { emit sessionEnded(); }, Qt::QueuedConnection);
}

DuckingController::DuckingController(std::function<int()> duckLevel, QObject* parent)
    : QObject(parent)
    , duckLevel_(std::move(duckLevel))
    , ownPid_(QCoreApplication::applicationPid())
    , ownBinary_(QFileInfo(QCoreApplication::applicationFilePath()).fileName())
    , stateFile_(QDir::homePath() + "/.openhu/audio-duck.json")
{
    fadeTimer_.setInterval(50);
    connect(&fadeTimer_, &QTimer::timeout, this, &DuckingController::onFadeStep);
    holdTimer_.setSingleShot(true);
    // gap between two prompts: stay down instead of coming up for a moment
    holdTimer_.setInterval(400);
    connect(&holdTimer_, &QTimer::timeout, this, &DuckingController::onHoldExpired);
    listTimer_.setSingleShot(true);
    listTimer_.setInterval(50);
    connect(&listTimer_, &QTimer::timeout, this, &DuckingController::list);
    connect(&subscribe_, &QProcess::readyReadStandardOutput, this, &DuckingController::onSubscribeOutput);
}

DuckingController::~DuckingController()
{
    this->restoreNow();
}

void DuckingController::start()
{
    auto& hub = AndroidAutoAudio::instance();
    connect(&hub, &AndroidAutoAudio::focusRequested, this, &DuckingController::onFocusRequest, Qt::UniqueConnection);
    connect(&hub, &AndroidAutoAudio::guidanceChanged, this, &DuckingController::onGuidance, Qt::UniqueConnection);
    connect(&hub, &AndroidAutoAudio::microphoneChanged, this, &DuckingController::onMicrophone, Qt::UniqueConnection);
    connect(&hub, &AndroidAutoAudio::sessionEnded, this, &DuckingController::onSessionEnded, Qt::UniqueConnection);
    this->publish();
    this->loadState();
    if(!pending_.isEmpty())
    {
        OPENAUTO_LOG(info) << "[DuckingController] " << pending_.size()
                           << " stream(s) were lowered when OpenHU ended last time, restoring them when they appear";
        this->startSubscribe();
        this->list();
    }
}

DuckTarget DuckingController::target() const
{
    return applied_;
}

double DuckingController::loudness() const
{
    return loudness_;
}

void DuckingController::setOwnProcess(qint64 pid, const QString& binary)
{
    ownPid_ = pid;
    ownBinary_ = binary;
}

void DuckingController::setStateFile(const QString& path)
{
    stateFile_ = path;
}

void DuckingController::setTimings(int holdMs, int fadeDownMs, int fadeUpMs, int stepMs)
{
    holdTimer_.setInterval(holdMs);
    fadeDownMs_ = fadeDownMs;
    fadeUpMs_ = fadeUpMs;
    fadeTimer_.setInterval(stepMs);
}

void DuckingController::setSourceFilter(std::function<bool(const SinkInput&)> filter)
{
    sourceFilter_ = std::move(filter);
}

void DuckingController::onFocusRequest(int type)
{
    const FocusRequest request = focusRequestFromProtocol(type);
    policy_.focusRequested(request);
    const char* meaning = request == FocusRequest::GainTransient ? "local sources muted during guidance"
                          : request == FocusRequest::Release      ? "local sources back after guidance"
                                                                  : "local sources lowered during guidance";
    OPENAUTO_LOG(info) << "[DuckingController] audio focus request " << focusRequestName(request) << " (" << type
                       << "): " << meaning << (policy_.guidance() ? ", guidance playing" : "");
    this->update();
}

void DuckingController::onGuidance(bool playing)
{
    if(playing == policy_.guidance())
    {
        return;
    }
    playing ? policy_.guidanceStarted() : policy_.guidanceStopped();
    OPENAUTO_LOG(info) << "[DuckingController] guidance channel " << (playing ? "started" : "stopped")
                       << " (last focus request " << focusRequestName(policy_.focus()) << ")";
    this->update();
}

void DuckingController::onMicrophone(bool open)
{
    if(open == policy_.microphone())
    {
        return;
    }
    policy_.microphoneChanged(open);
    OPENAUTO_LOG(info) << "[DuckingController] microphone " << (open ? "opened" : "closed") << " (last focus request "
                       << focusRequestName(policy_.focus()) << ")";
    this->update();
}

void DuckingController::onSessionEnded()
{
    if(policy_.guidance() || policy_.focus() != FocusRequest::None)
    {
        OPENAUTO_LOG(info) << "[DuckingController] Android Auto session ended";
    }
    policy_.sessionEnded();
    this->update();
}

void DuckingController::update()
{
    if(duckLevel_)
    {
        policy_.setDuckLevel(duckLevel_());
    }
    const DuckTarget target = policy_.target();
    if(target.active)
    {
        holdTimer_.stop();
        if(target != applied_)
        {
            OPENAUTO_LOG(info) << "[DuckingController] ducking local sources to " << target.level << " % ("
                               << target.reason.toStdString() << ")";
            applied_ = target;
            restoring_ = false;
            this->publish();
            this->startSubscribe();
            this->list();
            const double loudness = target.level / 100.0;
            this->fadeTo(loudness, loudness < loudness_ ? fadeDownMs_ : fadeUpMs_);
        }
        return;
    }
    if(applied_.active && !holdTimer_.isActive())
    {
        holdTimer_.start();
    }
}

void DuckingController::onHoldExpired()
{
    if(policy_.target().active || !applied_.active)
    {
        return;
    }
    OPENAUTO_LOG(info) << "[DuckingController] restoring local sources (" << streams_.size() << " stream(s))";
    applied_ = DuckTarget();
    restoring_ = true;
    this->publish();
    this->fadeTo(1.0, fadeUpMs_);
}

void DuckingController::publish()
{
    core::EventBus::instance().publish(
        "audio.duck", {{"active", applied_.active}, {"level", applied_.level}, {"reason", applied_.reason}});
}

void DuckingController::fadeTo(double loudness, int durationMs)
{
    fadeFrom_ = loudness_;
    fadeTo_ = loudness;
    fadeMs_ = std::max(durationMs, 1);
    fadeClock_.restart();
    if(!fadeTimer_.isActive())
    {
        fadeTimer_.start();
    }
    this->onFadeStep();
}

void DuckingController::onFadeStep()
{
    // linear in loudness like Android's ducking ramp
    const double t = std::min(1.0, double(fadeClock_.elapsed()) / fadeMs_);
    loudness_ = fadeFrom_ + (fadeTo_ - fadeFrom_) * t;
    if(t >= 1.0)
    {
        loudness_ = fadeTo_;
        fadeTimer_.stop();
    }
    this->applyAll();
    if(!fadeTimer_.isActive())
    {
        if(applied_.active)
        {
            emit settled();
        }
        else
        {
            this->finishRestore();
        }
    }
}

void DuckingController::finishRestore()
{
    if(!restoring_ || fadeTimer_.isActive())
    {
        return;
    }
    // the last volumes must reach the audio server before the streams are forgotten
    for(const Stream& stream: std::as_const(streams_))
    {
        if(stream.running)
        {
            return;
        }
    }
    restoring_ = false;
    const bool hadStreams = !streams_.isEmpty();
    streams_.clear();
    if(hadStreams)
    {
        this->writeState();
    }
    OPENAUTO_LOG(info) << "[DuckingController] local sources restored";
    this->stopSubscribeIfIdle();
    emit settled();
}

void DuckingController::applyAll()
{
    for(auto it = streams_.keyBegin(); it != streams_.keyEnd(); ++it)
    {
        this->apply(*it);
    }
}

void DuckingController::apply(quint32 index)
{
    auto it = streams_.find(index);
    if(it == streams_.end())
    {
        return;
    }
    if(stopped_)
    {
        return;
    }
    if(it->running)
    {
        it->dirty = true;  // sent when the running pactl ended: never two at once for a stream (order)
        return;
    }
    const QList<quint32> volumes = scaledVolumes(it->original, loudness_);
    if(volumes == it->lastSet)
    {
        return;
    }
    it->lastSet = volumes;
    auto* process = new QProcess(this);
    it->running = process;
    const auto done = [this, process, index](bool ok) {
        if(!ok)
        {
            // the stream ended in the meantime: nothing to restore
            OPENAUTO_LOG(debug) << "[DuckingController] pactl set-sink-input-volume " << index << " failed: "
                                << process->readAllStandardError().trimmed().toStdString();
        }
        process->deleteLater();
        if(stopped_)
        {
            return;  // restoreNow() takes over
        }
        auto stream = streams_.find(index);
        if(stream != streams_.end())
        {
            stream->running = nullptr;
            if(stream->dirty)
            {
                stream->dirty = false;
                this->apply(index);
            }
        }
        this->finishRestore();
    };
    connect(process, &QProcess::finished, this,
            [done](int exitCode, QProcess::ExitStatus status) { done(status == QProcess::NormalExit && exitCode == 0); });
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
        {
            done(false);
        }
    });
    core::startChild(*process, "pactl", volumeArguments(index, volumes));
}

void DuckingController::list()
{
    if(listing_)
    {
        listAgain_ = true;
        return;
    }
    listing_ = true;
    struct Result
    {
        QByteArray sinkInputs;
        QByteArray clients;
        int left = 2;
    };
    auto result = std::make_shared<Result>();
    const auto run = [this, result](const QStringList& arguments, QByteArray Result::*target) {
        auto* process = new QProcess(this);
        const auto done = [this, process, result, target]() {
            (*result).*target = process->readAllStandardOutput();
            process->deleteLater();
            if(--result->left == 0)
            {
                listing_ = false;
                this->onListed(result->sinkInputs, result->clients);
                if(listAgain_)
                {
                    listAgain_ = false;
                    this->list();
                }
            }
        };
        connect(process, &QProcess::finished, this, done);
        connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
            if(error == QProcess::FailedToStart)
            {
                done();
            }
        });
        core::startChild(*process, "pactl", arguments);
    };
    run({"-f", "json", "list", "sink-inputs"}, &Result::sinkInputs);
    run({"-f", "json", "list", "clients"}, &Result::clients);
}

void DuckingController::onListed(const QByteArray& sinkInputs, const QByteArray& clients)
{
    if(!sinkInputs.trimmed().startsWith('['))
    {
        if(!warnedJson_)
        {
            warnedJson_ = true;
            OPENAUTO_LOG(warning) << "[DuckingController] no stream list from \"pactl -f json list sink-inputs\" "
                                     "(no audio server, or pactl older than 16): local sources are not lowered";
        }
        return;
    }
    const QList<SinkInput> inputs = parseSinkInputs(sinkInputs, clients);
    // new streams are lowered while ducked (not while coming back up)
    const bool engaged = applied_.active;
    bool changed = false;
    QSet<quint32> present;
    for(const SinkInput& input: inputs)
    {
        if(!isLocalSource(input, ownPid_, ownBinary_) || (sourceFilter_ && !sourceFilter_(input)))
        {
            continue;
        }
        present.insert(input.index);
        auto pending = pending_.find(input.key());
        const bool lowered = pending != pending_.end() && below(input.volumes, pending->original);
        if(engaged && !streams_.contains(input.index))
        {
            Stream stream;
            stream.original = lowered ? pending->original : input.volumes;
            stream.lastSet = input.volumes;
            stream.key = input.key();
            streams_.insert(input.index, stream);
            OPENAUTO_LOG(info) << "[DuckingController] local source " << describe(input).toStdString()
                               << (lowered ? ", lowered by an earlier run" : "");
            this->apply(input.index);
            changed = true;
        }
        else if(lowered)
        {
            OPENAUTO_LOG(info) << "[DuckingController] " << describe(input).toStdString()
                               << " was left lowered by an earlier run, restoring its volume";
            auto* process = new QProcess(this);
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
            core::startChild(*process, "pactl", volumeArguments(input.index, pending->original));
        }
        if(pending != pending_.end())
        {
            // first stream of that application after the restart decides, later ones are new
            pending_.erase(pending);
            changed = true;
        }
    }
    for(auto it = streams_.begin(); it != streams_.end();)
    {
        if(!present.contains(it.key()) && !it->running)
        {
            // ended while lowered (radio switched off): the audio server gives its next stream the lowered volume
            if(it->lastSet != it->original)
            {
                pending_.insert(it->key, Pending{it->original});
            }
            it = streams_.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    if(changed)
    {
        this->writeState();
    }
    this->finishRestore();
    this->stopSubscribeIfIdle();
}

bool DuckingController::engaged() const
{
    return applied_.active || restoring_ || fadeTimer_.isActive() || holdTimer_.isActive() || !streams_.isEmpty();
}

void DuckingController::startSubscribe()
{
    if(subscribe_.state() == QProcess::NotRunning)
    {
        core::startChild(subscribe_, "pactl", {"subscribe"});
    }
}

void DuckingController::stopSubscribeIfIdle()
{
    if(this->engaged() || !pending_.isEmpty())
    {
        return;
    }
    if(subscribe_.state() != QProcess::NotRunning)
    {
        subscribe_.kill();
        subscribe_.waitForFinished(500);
    }
}

void DuckingController::onSubscribeOutput()
{
    bool added = false;
    while(subscribe_.canReadLine())
    {
        // "Event 'new' on sink-input #42": a stream that starts while ducked is lowered too; 'remove': one that ends
        const QByteArray line = subscribe_.readLine();
        if(line.contains("'new' on sink-input") || line.contains("'remove' on sink-input"))
        {
            added = true;
        }
    }
    if(added)
    {
        listTimer_.start();
    }
}

void DuckingController::restoreNow()
{
    holdTimer_.stop();
    fadeTimer_.stop();
    listTimer_.stop();
    stopped_ = true;  // no more volume changes from finishing pactl calls
    // a finishing pactl call must not change the streams while they are restored here
    const QHash<quint32, Stream> streams = std::exchange(streams_, {});
    const bool hadStreams = !streams.isEmpty();
    for(auto it = streams.begin(); it != streams.end(); ++it)
    {
        if(it->running)
        {
            it->running->waitForFinished(500);
        }
        if(loudness_ >= 1.0 && it->lastSet == it->original && !it->dirty)
        {
            continue;
        }
        QProcess process;
        core::startChild(process, "pactl", volumeArguments(it.key(), it->original));
        process.waitForFinished(1000);
    }
    streams_.clear();
    loudness_ = 1.0;
    restoring_ = false;
    stopped_ = false;
    if(applied_.active)
    {
        applied_ = DuckTarget();
        policy_.sessionEnded();
        this->publish();
    }
    if(hadStreams)
    {
        OPENAUTO_LOG(info) << "[DuckingController] local sources restored (OpenHU ends)";
        this->writeState();
    }
    if(subscribe_.state() != QProcess::NotRunning)
    {
        subscribe_.kill();
        subscribe_.waitForFinished(500);
    }
}

void DuckingController::loadState()
{
    QFile file(stateFile_);
    if(!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    const QJsonArray streams = QJsonDocument::fromJson(file.readAll()).object().value("streams").toArray();
    for(const QJsonValue& value: streams)
    {
        const QJsonObject stream = value.toObject();
        Pending pending;
        for(const QJsonValue& volume: stream.value("original").toArray())
        {
            pending.original.append(quint32(volume.toDouble()));
        }
        const QString key = stream.value("app").toString() + QLatin1Char('\n') + stream.value("media").toString();
        if(!pending.original.isEmpty())
        {
            pending_.insert(key, pending);
        }
    }
}

void DuckingController::writeState() const
{
    QJsonArray streams;
    const auto add = [&streams](const QString& key, const QList<quint32>& original) {
        QJsonArray volumes;
        for(const quint32 volume: original)
        {
            volumes.append(qint64(volume));
        }
        streams.append(QJsonObject{{"app", key.section('\n', 0, 0)}, {"media", key.section('\n', 1)},
                                   {"original", volumes}});
    };
    for(const Stream& stream: streams_)
    {
        add(stream.key, stream.original);
    }
    for(auto it = pending_.begin(); it != pending_.end(); ++it)
    {
        bool known = false;
        for(const Stream& stream: streams_)
        {
            known = known || stream.key == it.key();
        }
        if(!known)
        {
            add(it.key(), it->original);
        }
    }
    if(streams.isEmpty())
    {
        QFile::remove(stateFile_);
        return;
    }
    QDir().mkpath(QFileInfo(stateFile_).absolutePath());
    QSaveFile file(stateFile_);
    if(file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(QJsonObject{{"streams", streams}}).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

}
}
}
}
