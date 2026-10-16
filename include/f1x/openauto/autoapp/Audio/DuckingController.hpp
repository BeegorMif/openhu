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
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <f1x/openauto/autoapp/Audio/DuckPolicy.hpp>
#include <f1x/openauto/autoapp/Audio/SinkInputs.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace audio
{

// What the Android Auto services report for ducking. Thread safe (the services run in the io_service threads), the
// signals arrive queued in the GUI thread.
class AndroidAutoAudio: public QObject
{
    Q_OBJECT

public:
    static AndroidAutoAudio& instance();

    // AudioFocusRequestType of the phone's request (protocol number)
    void reportFocusRequest(int type);
    // MediaChannelStartIndication / StopIndication (or the service stopped) on the guidance channel
    void reportGuidance(bool playing);
    // the Android Auto session ended (disconnect)
    void reportSessionEnded();

signals:
    void focusRequested(int type);
    void guidanceChanged(bool playing);
    void sessionEnded();

private:
    AndroidAutoAudio() = default;
};

// Lowers the local sources (streams of other processes, e.g. the radio plugin's pacat) while Android Auto plays on
// its guidance channel, and restores them afterwards (#93), with pactl like VolumeController, but without blocking
// the GUI thread. DuckPolicy decides the level, this class applies it with short fades: volumes relative to the
// stream's own volume before ducking, which comes back at the end, when the session ends and when OpenHU exits.
// The ducked streams are written to a file: after a crash the audio server would give a new stream of the same
// application the lowered volume it remembered, so a stream that starts below its remembered original gets it back.
// Publishes "audio.duck" {active, level, reason}.
class DuckingController: public QObject
{
    Q_OBJECT

public:
    // duckLevel: % for navigation prompts (Settings -> Audio), read whenever ducking starts
    explicit DuckingController(std::function<int()> duckLevel, QObject* parent = nullptr);
    ~DuckingController() override;

    // Starts listening to AndroidAutoAudio and finishes what a crashed run left behind
    void start();
    // Shutdown/restart: original volumes back at once (blocking, a few pactl calls)
    void restoreNow();

    DuckTarget target() const;
    // loudness factor applied right now (1 = unchanged)
    double loudness() const;

    // Tests: process that counts as OpenHU, the state file and timings in ms
    void setOwnProcess(qint64 pid, const QString& binary);
    void setStateFile(const QString& path);
    void setTimings(int holdMs, int fadeDownMs, int fadeUpMs, int stepMs);
    // only streams it accepts (tests on a real audio server: not the desktop's streams)
    void setSourceFilter(std::function<bool(const SinkInput&)> filter);

public slots:
    void onFocusRequest(int type);
    void onGuidance(bool playing);
    void onSessionEnded();

signals:
    // a fade reached its end (tests)
    void settled();

private:
    struct Stream
    {
        QList<quint32> original;
        QList<quint32> lastSet;
        QString key;
        QPointer<QProcess> running;
        bool dirty = false;
    };
    struct Pending
    {
        QList<quint32> original;
    };

    void update();
    void onHoldExpired();
    void finishRestore();
    void publish();
    void fadeTo(double loudness, int durationMs);
    void onFadeStep();
    void applyAll();
    void apply(quint32 index);
    void list();
    void onListed(const QByteArray& sinkInputs, const QByteArray& clients);
    void startSubscribe();
    void stopSubscribeIfIdle();
    void onSubscribeOutput();
    bool engaged() const;
    void loadState();
    void writeState() const;

    std::function<int()> duckLevel_;
    DuckPolicy policy_;
    DuckTarget applied_;
    qint64 ownPid_ = 0;
    QString ownBinary_;
    QString stateFile_;
    std::function<bool(const SinkInput&)> sourceFilter_;

    QHash<quint32, Stream> streams_;
    QHash<QString, Pending> pending_;

    double loudness_ = 1.0;
    double fadeFrom_ = 1.0;
    double fadeTo_ = 1.0;
    int fadeMs_ = 0;
    QElapsedTimer fadeClock_;
    QTimer fadeTimer_;
    QTimer holdTimer_;
    int fadeDownMs_ = 250;
    int fadeUpMs_ = 500;

    QProcess subscribe_;
    QTimer listTimer_;
    bool listing_ = false;
    bool listAgain_ = false;
    bool warnedJson_ = false;
    // fading back up after ducking, the streams are forgotten at the end
    bool restoring_ = false;
    bool stopped_ = false;
};

}
}
}
}
