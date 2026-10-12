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


#include <QProcess>
#include <f1x/openauto/autoapp/Audio/VolumeController.hpp>
#include <f1x/openauto/autoapp/Bluetooth/BluetoothManager.hpp>
#include <f1x/openauto/autoapp/Bluetooth/CarAudio.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Input/ActionDispatcher.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::bt
{

namespace
{

QString pactl(const QStringList& arguments)
{
    QProcess process;
    process.start("pactl", arguments);
    process.waitForFinished(3000);
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

}

CarAudio::CarAudio(configuration::IConfiguration::Pointer configuration, BluetoothManager& bluetooth,
                   input::ActionDispatcher& actions, audio::VolumeController& volume, QObject* parent)
    : QObject(parent)
    , configuration_(std::move(configuration))
    , bluetooth_(bluetooth)
    , actions_(actions)
    , volume_(volume)
{
    connect(&bluetooth_, &BluetoothManager::devicesChanged, this, &CarAudio::onDevicesChanged);
    connect(&bluetooth_, &BluetoothManager::devicePaired, this, [this](const QString& address) {
        if(running_ && address.compare(QString::fromStdString(configuration_->getCarAudioSettings().device),
                                       Qt::CaseInsensitive) == 0)
        {
            bluetooth_.connectDevice(address);
        }
    });
    connect(&player_, &MediaPlayer::command, this, &CarAudio::onCommand);

    sinkTimer_.setInterval(1000);
    connect(&sinkTimer_, &QTimer::timeout, this, &CarAudio::findSink);

    // the radio is switched on with the ignition: keep trying while it is off
    reconnectTimer_.setInterval(20000);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this]() {
        const auto settings = configuration_->getCarAudioSettings();
        if(running_ && !connected_ && !settings.device.empty())
        {
            bluetooth_.connectDevice(QString::fromStdString(settings.device));
        }
    });

    connect(&core::EventBus::instance(), &core::EventBus::event, this, [this](const QString& topic, const QVariantMap& data) {
        if(topic == "media.metadata")
        {
            metadata_ = data;
        }
        else if(topic == "media.status")
        {
            mediaStatus_ = data;
        }
        else if(topic == "phone.status")
        {
            callState_ = data.value("callState").toString();
            call_ = data.value("calls").toList().value(0).toMap();
        }
        else
        {
            return;
        }
        this->updatePlayer();
    });
}

CarAudio::~CarAudio()
{
    this->stop();
}

void CarAudio::apply()
{
    const auto settings = configuration_->getCarAudioSettings();
    if(!settings.enabled || settings.device.empty())
    {
        this->stop();
        this->publish();
        return;
    }
    if(!bluetooth_.ensureAdapter())
    {
        error_ = bluetooth_.error();
        this->publish();
        return;
    }

    running_ = true;
    error_.clear();
    const QString registerError = player_.registerAt(bluetooth_.adapterPath());
    if(!registerError.isEmpty())
    {
        OPENAUTO_LOG(warning) << "[CarAudio] media player: " << registerError.toStdString();
    }
    this->updatePlayer();

    const QString address = QString::fromStdString(settings.device);
    if(bluetooth_.device(address).isEmpty())
    {
        bluetooth_.pairWith(address, QString::fromStdString(settings.pin));
    }
    else
    {
        bluetooth_.connectDevice(address);
    }
    reconnectTimer_.start();
    this->onDevicesChanged();
    this->publish();
}

void CarAudio::stop()
{
    if(!running_)
    {
        return;
    }
    running_ = false;
    reconnectTimer_.stop();
    sinkTimer_.stop();
    player_.unregister();
    if(connected_)
    {
        bluetooth_.disconnectDevice(QString::fromStdString(configuration_->getCarAudioSettings().device));
        this->onDisconnected();
    }
}

void CarAudio::selectDevice(const QString& address)
{
    OPENAUTO_LOG(info) << "[CarAudio] radio " << address.toStdString();
    bluetooth_.stopSearch();
    auto settings = configuration_->getCarAudioSettings();
    settings.device = address.toUpper().toStdString();
    settings.enabled = true;
    configuration_->setCarAudioSettings(settings);
    configuration_->save();
    this->stop();
    this->apply();
}

void CarAudio::forgetDevice()
{
    auto settings = configuration_->getCarAudioSettings();
    const QString address = QString::fromStdString(settings.device);
    this->stop();
    settings.device.clear();
    settings.enabled = false;
    configuration_->setCarAudioSettings(settings);
    configuration_->save();
    if(!address.isEmpty())
    {
        bluetooth_.removeDevice(address);
    }
    this->publish();
}

void CarAudio::onDevicesChanged()
{
    if(!running_)
    {
        return;
    }
    const QVariantMap device = bluetooth_.device(QString::fromStdString(configuration_->getCarAudioSettings().device));
    const bool connected = device.value("connected").toBool();
    if(connected && !connected_)
    {
        this->onConnected();
    }
    else if(!connected && connected_)
    {
        this->onDisconnected();
    }
}

void CarAudio::onConnected()
{
    connected_ = true;
    OPENAUTO_LOG(info) << "[CarAudio] radio connected";
    sinkAttempts_ = 0;
    sinkTimer_.start();
    this->findSink();
    this->publish();
}

void CarAudio::onDisconnected()
{
    connected_ = false;
    sinkTimer_.stop();
    OPENAUTO_LOG(info) << "[CarAudio] radio disconnected";
    this->restoreSink();
    this->publish();
}

void CarAudio::findSink()
{
    // PipeWire: bluez_output.E8:78:29:C0:E5:2D (older: bluez_output.E8_78_29_C0_E5_2D.1),
    // PulseAudio: bluez_sink.E8_78_29_C0_E5_2D.a2dp_sink
    const QString id = QString::fromStdString(configuration_->getCarAudioSettings().device).toUpper().replace(':', '_');
    for(const QString& line: pactl({"list", "short", "sinks"}).split('\n', Qt::SkipEmptyParts))
    {
        const QString name = line.section('\t', 1, 1);
        if(name.startsWith("bluez") && name.toUpper().replace(':', '_').contains(id))
        {
            sinkTimer_.stop();
            if(name != sink_)
            {
                const QString current = pactl({"get-default-sink"});
                if(!current.startsWith("bluez"))
                {
                    previousSink_ = current;
                }
                sink_ = name;
                pactl({"set-default-sink", sink_});
                OPENAUTO_LOG(info) << "[CarAudio] output " << sink_.toStdString();
                volume_.refresh();
                error_.clear();
                this->publish();
            }
            return;
        }
    }
    if(++sinkAttempts_ >= 20)
    {
        sinkTimer_.stop();
        error_ = tr("Connected, but no audio output found (A2DP active in PipeWire/PulseAudio?).");
        OPENAUTO_LOG(warning) << "[CarAudio] no sink for the radio";
        this->publish();
    }
}

void CarAudio::restoreSink()
{
    if(sink_.isEmpty())
    {
        return;
    }
    if(!previousSink_.isEmpty() && pactl({"get-default-sink"}) == sink_)
    {
        pactl({"set-default-sink", previousSink_});
        volume_.refresh();
    }
    sink_.clear();
}

void CarAudio::onCommand(const QString& name)
{
    OPENAUTO_LOG(info) << "[CarAudio] radio button: " << name.toStdString() << " (call: " << callState_.toStdString() << ")";
    const bool toggle = name == "play" || name == "pause" || name == "playpause" || name == "stop";
    if(toggle && callState_ == "incoming")
    {
        actions_.trigger("call.answer");
    }
    else if(toggle && (callState_ == "in_call" || callState_ == "on_hold"))
    {
        actions_.trigger("call.end");
    }
    else if(name == "next")
    {
        actions_.trigger("media.next");
    }
    else if(name == "previous")
    {
        actions_.trigger("media.previous");
    }
    else if(name == "play")
    {
        actions_.trigger("media.play");
    }
    else if(name == "pause" || name == "stop")
    {
        actions_.trigger("media.pause");
    }
    else
    {
        actions_.trigger("media.play_pause");
    }
}

void CarAudio::updatePlayer()
{
    if(callState_ == "incoming" || callState_ == "in_call" || callState_ == "on_hold")
    {
        const QString caller = call_.value("name").toString().isEmpty() ? call_.value("number").toString()
                                                                        : call_.value("name").toString();
        player_.update("Playing", (callState_ == "incoming" ? tr("Call: %1") : tr("Talking: %1")).arg(caller),
                       call_.value("number").toString(), QString(), 0, call_.value("duration").toDouble());
        return;
    }
    const QString state = mediaStatus_.value("state").toString();
    player_.update(state == "playing" ? "Playing" : state == "paused" ? "Paused" : "Stopped",
                   metadata_.value("title").toString(), metadata_.value("artist").toString(),
                   metadata_.value("album").toString(), metadata_.value("duration").toDouble(),
                   mediaStatus_.value("position").toDouble());
}

QVariantMap CarAudio::state() const
{
    const auto settings = configuration_->getCarAudioSettings();
    const QString address = QString::fromStdString(settings.device);
    const QVariantMap device = bluetooth_.device(address);
    QString status;
    if(!settings.enabled || address.isEmpty()) status = tr("Off");
    else if(!error_.isEmpty()) status = error_;
    else if(connected_ && !sink_.isEmpty()) status = tr("Sound plays through %1").arg(device.value("name", address).toString());
    else if(connected_) status = tr("Connected, setting up the audio output …");
    else if(device.isEmpty()) status = tr("Pairing …");
    else status = tr("Waiting for %1 (radio on?)").arg(device.value("name", address).toString());

    return {
        {"enabled", settings.enabled},
        {"device", address},
        {"name", device.value("name", address)},
        {"paired", !device.isEmpty()},
        {"connected", connected_},
        {"sink", sink_},
        {"error", error_},
        {"status", status},
    };
}

void CarAudio::publish()
{
    emit changed();
    core::EventBus::instance().publish("caraudio.state", this->state());
}

}
