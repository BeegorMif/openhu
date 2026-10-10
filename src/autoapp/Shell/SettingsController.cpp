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

#include <unistd.h>
#include <vector>
#include <QCursor>
#include <QDir>
#include <QGuiApplication>
#include <f1x/openauto/autoapp/Shell/SettingsController.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include <f1x/openauto/Common/Log.hpp>

#ifndef OPENHU_VERSION
#define OPENHU_VERSION "dev"
#endif
#ifndef OPENHU_GIT
#define OPENHU_GIT ""
#endif

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

namespace
{

using Resolution = aap_protobuf::service::media::sink::message::VideoCodecResolutionType;
using FrameRate = aap_protobuf::service::media::sink::message::VideoFrameRateType;

const QString cReconnect = QStringLiteral("reconnect");
const QString cRestart = QStringLiteral("restart");

QString resolutionName(Resolution resolution)
{
    switch(resolution)
    {
    case Resolution::VIDEO_1280x720:
        return "1280x720";
    case Resolution::VIDEO_1920x1080:
        return "1920x1080";
    case Resolution::VIDEO_2560x1440:
        return "2560x1440";
    default:
        return "800x480";
    }
}

}

SettingsController::SettingsController(configuration::IConfiguration::Pointer configuration, int argc, char** argv,
                                       QObject* parent)
    : QObject(parent)
    , configuration_(std::move(configuration))
{
    for(int i = 0; i < argc; ++i)
    {
        arguments_.append(QString::fromLocal8Bit(argv[i]));
    }
}

QVariantMap SettingsController::values() const
{
    const QRect margins = configuration_->getVideoMargins();
    const std::string listen = configuration_->getApiListenAddress();

    return {
        {"general.driverSide", configuration_->getHandednessOfTrafficType()
                                   == configuration::HandednessOfTrafficType::RIGHT_HAND_DRIVE ? "right" : "left"},
        {"general.showCursor", configuration_->showCursor()},
        {"general.nameSuffix", QString::fromStdString(configuration_->getNameSuffix())},
        {"aa.resolution", configuration_->getVideoResolutionAuto() ? QString("auto")
                                                                    : resolutionName(configuration_->getVideoResolution())},
        {"aa.activeResolution", resolutionName(AaBridge::instance().resolution())},
        {"aa.fps", configuration_->getVideoFPS() == FrameRate::VIDEO_FPS_60 ? 60 : 30},
        {"aa.dpi", static_cast<int>(configuration_->getScreenDPI())},
        {"aa.marginWidth", margins.width()},
        {"aa.marginHeight", margins.height()},
        {"aa.touchscreen", configuration_->getTouchscreenEnabled()},
        {"aa.wireless", configuration_->getWirelessProjectionEnabled()},
        {"aa.audioMedia", configuration_->musicAudioChannelEnabled()},
        {"aa.audioGuidance", configuration_->guidanceAudioChannelEnabled()},
        {"aa.audioTelephony", configuration_->telephonyAudioChannelEnabled()},
        {"audio.backend", configuration_->getAudioOutputBackendType() == configuration::AudioOutputBackendType::QT
                              ? "qt" : "rtaudio"},
        {"bluetooth.adapter", configuration_->getBluetoothAdapterType() == configuration::BluetoothAdapterType::NONE
                                  ? "none" : "local"},
        {"bluetooth.address", QString::fromStdString(configuration_->getBluetoothAdapterAddress())},
        {"api.port", configuration_->getApiPort()},
        {"api.network", listen != "127.0.0.1" && listen != "localhost"},
    };
}

QStringList SettingsController::pending() const
{
    return pending_;
}

QVariantMap SettingsController::info() const
{
    return {
        {"version", QStringLiteral(OPENHU_VERSION)},
        {"git", QStringLiteral(OPENHU_GIT)},
        {"qt", QString::fromLatin1(qVersion())},
        {"dataDir", plugins::PluginManager::baseDir()},
        {"configFile", QDir::current().absoluteFilePath("openhu.ini")},
        {"vehicleId", QString::fromStdString(configuration_->getVehicleId())},
        {"apiUrl", QString("ws://%1:%2").arg(QString::fromStdString(configuration_->getApiListenAddress()))
                       .arg(configuration_->getApiPort())},
    };
}

QString SettingsController::set(const QString& key, const QVariant& value)
{
    if(key == "general.driverSide")
    {
        configuration_->setHandednessOfTrafficType(value.toString() == "right"
                                                       ? configuration::HandednessOfTrafficType::RIGHT_HAND_DRIVE
                                                       : configuration::HandednessOfTrafficType::LEFT_HAND_DRIVE);
        this->markPending(cReconnect);
    }
    else if(key == "general.showCursor")
    {
        configuration_->showCursor(value.toBool());
        this->applyCursor();
    }
    else if(key == "general.nameSuffix")
    {
        const QString suffix = value.toString().trimmed();
        if(suffix.size() > 24)
        {
            return "Höchstens 24 Zeichen";
        }
        configuration_->setNameSuffix(suffix.toStdString());
        this->markPending(cReconnect);
    }
    else if(key == "aa.resolution")
    {
        const QString name = value.toString();
        configuration_->setVideoResolutionAuto(name == "auto");
        if(name != "auto")
        {
            configuration_->setVideoResolution(name == "2560x1440" ? Resolution::VIDEO_2560x1440
                                               : name == "1920x1080" ? Resolution::VIDEO_1920x1080
                                               : name == "1280x720" ? Resolution::VIDEO_1280x720
                                                                    : Resolution::VIDEO_800x480);
        }
        this->markPending(cReconnect);
    }
    else if(key == "aa.fps")
    {
        configuration_->setVideoFPS(value.toInt() == 60 ? FrameRate::VIDEO_FPS_60 : FrameRate::VIDEO_FPS_30);
        this->markPending(cReconnect);
    }
    else if(key == "aa.dpi")
    {
        const int dpi = value.toInt();
        if(dpi < 80 || dpi > 400)
        {
            return "DPI zwischen 80 und 400";
        }
        configuration_->setScreenDPI(static_cast<size_t>(dpi));
        this->markPending(cReconnect);
    }
    else if(key == "aa.marginWidth" || key == "aa.marginHeight")
    {
        const int margin = value.toInt();
        if(margin < 0 || margin > 600)
        {
            return "Rand zwischen 0 und 600 Pixel";
        }
        QRect margins = configuration_->getVideoMargins();
        if(key == "aa.marginWidth")
        {
            margins.setWidth(margin & ~1);
        }
        else
        {
            margins.setHeight(margin & ~1);
        }
        configuration_->setVideoMargins(margins);
        this->markPending(cReconnect);
    }
    else if(key == "aa.touchscreen")
    {
        configuration_->setTouchscreenEnabled(value.toBool());
        this->markPending(cReconnect);
    }
    else if(key == "aa.wireless")
    {
        configuration_->setWirelessProjectionEnabled(value.toBool());
        this->markPending(cRestart);
    }
    else if(key == "aa.audioMedia")
    {
        configuration_->setMusicAudioChannelEnabled(value.toBool());
        this->markPending(cReconnect);
    }
    else if(key == "aa.audioGuidance")
    {
        configuration_->setGuidanceAudioChannelEnabled(value.toBool());
        this->markPending(cReconnect);
    }
    else if(key == "aa.audioTelephony")
    {
        configuration_->setTelephonyAudioChannelEnabled(value.toBool());
        this->markPending(cReconnect);
    }
    else if(key == "audio.backend")
    {
        configuration_->setAudioOutputBackendType(value.toString() == "qt" ? configuration::AudioOutputBackendType::QT
                                                                           : configuration::AudioOutputBackendType::RTAUDIO);
        this->markPending(cReconnect);
    }
    else if(key == "bluetooth.adapter")
    {
        configuration_->setBluetoothAdapterType(value.toString() == "local" ? configuration::BluetoothAdapterType::LOCAL
                                                                            : configuration::BluetoothAdapterType::NONE);
        this->markPending(cRestart);
    }
    else if(key == "bluetooth.address")
    {
        configuration_->setBluetoothAdapterAddress(value.toString().trimmed().toStdString());
        this->markPending(cRestart);
    }
    else if(key == "api.port")
    {
        const int port = value.toInt();
        if(port < 1024 || port > 65535)
        {
            return "Port zwischen 1024 und 65535";
        }
        configuration_->setApiPort(static_cast<uint16_t>(port));
        this->markPending(cRestart);
    }
    else if(key == "api.network")
    {
        configuration_->setApiListenAddress(value.toBool() ? "0.0.0.0" : "127.0.0.1");
        this->markPending(cRestart);
    }
    else
    {
        return "Unbekannte Einstellung: " + key;
    }

    configuration_->save();
    emit changed();
    return QString();
}

void SettingsController::clearPending(const QString& what)
{
    if(pending_.removeAll(what) > 0)
    {
        emit changed();
    }
}

void SettingsController::markPending(const QString& what)
{
    if(!pending_.contains(what))
    {
        pending_.append(what);
    }
}

void SettingsController::applyCursor() const
{
    QGuiApplication::restoreOverrideCursor();
    if(!configuration_->showCursor())
    {
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
    }
}

void SettingsController::restart()
{
    OPENAUTO_LOG(info) << "[SettingsController] restarting";
    configuration_->save();
    emit aboutToRestart();

    std::vector<QByteArray> storage;
    std::vector<char*> argv;
    for(const auto& argument: arguments_)
    {
        storage.push_back(argument.toLocal8Bit());
    }
    for(auto& argument: storage)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    // Sockets (API, wireless Android Auto), USB and plugin pipes are not close-on-exec: close everything
    // except stdin/stdout/stderr, otherwise the new process cannot bind its ports again.
    const long maxFd = ::sysconf(_SC_OPEN_MAX);
    for(int fd = 3; fd < (maxFd > 0 ? maxFd : 1024); ++fd)
    {
        ::close(fd);
    }

    ::execv("/proc/self/exe", argv.data());
    OPENAUTO_LOG(error) << "[SettingsController] restart failed";
}

}
}
}
}
