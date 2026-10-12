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
#include <QRegularExpression>
#include <f1x/openauto/autoapp/Audio/VolumeController.hpp>
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
const QString cSink = QStringLiteral("@DEFAULT_SINK@");
}

VolumeController::VolumeController(QObject* parent)
    : QObject(parent)
{
    // Several change events arrive per volume step, read the state once they settled.
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(100);
    connect(&refreshTimer_, &QTimer::timeout, this, &VolumeController::refresh);

    connect(&subscribe_, &QProcess::readyReadStandardOutput, this, &VolumeController::onSubscribeOutput);
    subscribe_.start("pactl", {"subscribe"});

    this->refresh();
    this->publish();
}

VolumeController::~VolumeController()
{
    this->stop();
}

void VolumeController::stop()
{
    if(subscribe_.state() != QProcess::NotRunning)
    {
        subscribe_.kill();
        subscribe_.waitForFinished(500);
    }
}

int VolumeController::volume() const
{
    return volume_;
}

bool VolumeController::muted() const
{
    return muted_;
}

void VolumeController::setVolume(int percent)
{
    percent = std::clamp(percent, 0, 100);
    this->runPactl({"set-sink-volume", cSink, QString::number(percent) + "%"});
    if(muted_ && percent > 0)
    {
        this->runPactl({"set-sink-mute", cSink, "0"});
    }
    this->refresh();
}

void VolumeController::changeVolume(int delta)
{
    this->setVolume(volume_ + delta);
}

void VolumeController::setMuted(bool muted)
{
    this->runPactl({"set-sink-mute", cSink, muted ? "1" : "0"});
    this->refresh();
}

void VolumeController::toggleMute()
{
    this->setMuted(!muted_);
}

void VolumeController::refresh()
{
    QProcess volumeProcess;
    volumeProcess.start("pactl", {"get-sink-volume", cSink});
    volumeProcess.waitForFinished(1000);
    const QString volumeOutput = QString::fromUtf8(volumeProcess.readAllStandardOutput());

    QProcess muteProcess;
    muteProcess.start("pactl", {"get-sink-mute", cSink});
    muteProcess.waitForFinished(1000);
    const QString muteOutput = QString::fromUtf8(muteProcess.readAllStandardOutput());

    // "Volume: front-left: 26214 /  40% / -23.88 dB, ..." -> first percentage
    const auto match = QRegularExpression("(\\d+)%").match(volumeOutput);
    if(!match.hasMatch())
    {
        return;  // no audio server (yet)
    }
    const int volume = match.captured(1).toInt();
    const bool muted = muteOutput.contains("yes");

    const bool first = !known_;
    known_ = true;
    if(first || volume != volume_ || muted != muted_)
    {
        volume_ = volume;
        muted_ = muted;
        emit changed();
        if(!first)
        {
            emit adjusted();
        }
        this->publish();
    }
}

void VolumeController::runPactl(const QStringList& arguments)
{
    QProcess process;
    process.start("pactl", arguments);
    if(!process.waitForFinished(1000) || process.exitCode() != 0)
    {
        OPENAUTO_LOG(warning) << "[VolumeController] pactl " << arguments.join(' ').toStdString() << " failed: "
                              << process.readAllStandardError().toStdString();
    }
}

void VolumeController::onSubscribeOutput()
{
    while(subscribe_.canReadLine())
    {
        if(subscribe_.readLine().contains("on sink"))
        {
            refreshTimer_.start();
        }
    }
}

void VolumeController::publish()
{
    core::EventBus::instance().publish("audio.volume", {{"volume", volume_}, {"muted", muted_}});
}

}
}
}
}
