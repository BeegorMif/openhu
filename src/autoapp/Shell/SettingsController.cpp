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
#include <QHostAddress>
#include <QGuiApplication>
#include <f1x/openauto/autoapp/Shell/SettingsController.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include <f1x/openauto/autoapp/Shell/Translations.hpp>
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
    const auto wireless = configuration_->getWirelessSettings();

    return {
        {"general.driverSide", configuration_->getHandednessOfTrafficType()
                                   == configuration::HandednessOfTrafficType::RIGHT_HAND_DRIVE ? "right" : "left"},
        {"general.showCursor", configuration_->showCursor()},
        {"general.onScreenKeyboard", configuration_->onScreenKeyboard()},
        {"general.language", Translations::resolve(QString::fromStdString(configuration_->getLanguage()))},
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
        {"wireless.ssid", QString::fromStdString(wireless.ssid)},
        {"wireless.password", QString::fromStdString(wireless.password)},
        {"wireless.band", QString::fromStdString(wireless.band)},
        {"wireless.country", QString::fromStdString(wireless.country)},
        {"wireless.priority", wireless.priority == "wifi" ? "wifi" : "usb"},
        {"wireless.hotspot", wireless.hotspot == "demand" ? "demand" : "always"},
        {"wireless.channel", wireless.channel},
        {"wireless.address", QString::fromStdString(wireless.address)},
        {"wireless.dhcpRange", QString::fromStdString(wireless.dhcpRange)},
        {"aa.audioMedia", configuration_->musicAudioChannelEnabled()},
        {"aa.audioGuidance", configuration_->guidanceAudioChannelEnabled()},
        {"aa.audioTelephony", configuration_->telephonyAudioChannelEnabled()},
        {"audio.backend", configuration_->getAudioOutputBackendType() == configuration::AudioOutputBackendType::QT
                              ? "qt" : "rtaudio"},
        {"bluetooth.adapter", configuration_->getBluetoothAdapterType() == configuration::BluetoothAdapterType::NONE
                                  ? "none" : "local"},
        {"bluetooth.address", QString::fromStdString(configuration_->getBluetoothAdapterAddress())},
        {"carAudio.enabled", configuration_->getCarAudioSettings().enabled},
        {"carAudio.pin", QString::fromStdString(configuration_->getCarAudioSettings().pin)},
        {"api.port", configuration_->getApiPort()},
        {"api.network", listen != "127.0.0.1" && listen != "localhost"},
        {"api.hotspot", configuration_->getApiAllowHotspot()},
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
        {"languages", Translations::languages()},
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
    else if(key == "general.onScreenKeyboard")
    {
        configuration_->setOnScreenKeyboard(value.toBool());
    }
    else if(key == "general.language")
    {
        const QString language = value.toString();
        if(!Translations::available().contains(language))
        {
            return tr("Unknown language");
        }
        configuration_->setLanguage(language.toStdString());
        emit languageChanged(language);
    }
    else if(key == "general.nameSuffix")
    {
        const QString suffix = value.toString().trimmed();
        if(suffix.size() > 24)
        {
            return tr("At most 24 characters");
        }
        configuration_->setNameSuffix(suffix.toStdString());
        this->markPending(cReconnect);
        // the default WiFi name contains the suffix
        if(configuration_->getWirelessSettings().ssid.empty())
        {
            QMetaObject::invokeMethod(this, &SettingsController::wirelessChanged, Qt::QueuedConnection);
        }
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
            return tr("DPI between 80 and 400");
        }
        configuration_->setScreenDPI(static_cast<size_t>(dpi));
        this->markPending(cReconnect);
    }
    else if(key == "aa.marginWidth" || key == "aa.marginHeight")
    {
        const int margin = value.toInt();
        if(margin < 0 || margin > 600)
        {
            return tr("Margin between 0 and 600 pixels");
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
        QMetaObject::invokeMethod(this, &SettingsController::wirelessChanged, Qt::QueuedConnection);
    }
    else if(key.startsWith("wireless."))
    {
        auto wireless = configuration_->getWirelessSettings();
        const QString text = value.toString().trimmed();
        if(key == "wireless.ssid")
        {
            if(text.toUtf8().size() > 32)
            {
                return tr("At most 32 characters");
            }
            wireless.ssid = text.toStdString();
        }
        else if(key == "wireless.password")
        {
            if(text.size() < 8 || text.size() > 63)
            {
                return tr("Password: 8 to 63 characters");
            }
            wireless.password = text.toStdString();
        }
        else if(key == "wireless.band")
        {
            wireless.band = text == "2.4" || text == "auto" ? text.toStdString() : "5";
            // a fixed channel belongs to one band
            wireless.channel = 0;
        }
        else if(key == "wireless.channel")
        {
            const int channel = value.toInt();
            const bool valid5 = (channel >= 36 && channel <= 64 && channel % 4 == 0) || channel == 100
                                || (channel > 100 && channel <= 144 && channel % 4 == 0)
                                || (channel >= 149 && channel <= 165 && (channel - 149) % 4 == 0);
            if(channel != 0 && wireless.band == "2.4" && (channel < 1 || channel > 13))
            {
                return tr("2.4 GHz: channel 1 to 13");
            }
            if(channel != 0 && wireless.band == "5" && !valid5)
            {
                return tr("5 GHz: channel 36, 40, 44, 48 (without radar detection) or 52–64, 100–144, 149–165");
            }
            if(channel != 0 && wireless.band == "auto")
            {
                return tr("A fixed channel only with a fixed band (5 or 2.4 GHz)");
            }
            wireless.channel = channel;
        }
        else if(key == "wireless.address")
        {
            const QHostAddress host(text.section('/', 0, 0));
            bool prefixOk = false;
            const int prefix = text.section('/', 1).toInt(&prefixOk);
            const quint32 hostBits = prefixOk && prefix >= 8 && prefix <= 30 ? host.toIPv4Address() & ~(~0u << (32 - prefix)) : 0;
            // not the network or broadcast address of its subnet
            if(host.protocol() != QAbstractSocket::IPv4Protocol || !text.contains('/') || !prefixOk || prefix < 8
               || prefix > 30 || hostBits == 0 || hostBits == ~(~0u << (32 - prefix)))
            {
                return tr("Address with netmask, e.g. 10.42.0.1/24");
            }
            wireless.address = text.toStdString();
            // the DHCP range belongs to the old network
            wireless.dhcpRange.clear();
        }
        else if(key == "wireless.dhcpRange")
        {
            if(!text.isEmpty())
            {
                const QStringList parts = text.split('-');
                const auto subnet = QHostAddress::parseSubnet(QString::fromStdString(wireless.address));
                QHostAddress from(parts.value(0).trimmed());
                QHostAddress to(parts.value(1).trimmed());
                if(parts.size() != 2 || from.protocol() != QAbstractSocket::IPv4Protocol
                   || to.protocol() != QAbstractSocket::IPv4Protocol || !from.isInSubnet(subnet) || !to.isInSubnet(subnet)
                   || from.toIPv4Address() > to.toIPv4Address())
                {
                    return tr("Range in the hotspot network, e.g. 10.42.0.10-10.42.0.254");
                }
                wireless.dhcpRange = (from.toString() + "-" + to.toString()).toStdString();
            }
            else
            {
                wireless.dhcpRange.clear();
            }
        }
        else if(key == "wireless.hotspot")
        {
            wireless.hotspot = text == "demand" ? "demand" : "always";
        }
        else if(key == "wireless.priority")
        {
            wireless.priority = text == "wifi" ? "wifi" : "usb";
        }
        else if(key == "wireless.country")
        {
            if(text.size() != 2)
            {
                return tr("Country code with 2 letters, e.g. DE");
            }
            wireless.country = text.toUpper().toStdString();
        }
        else
        {
            return tr("Unknown setting: %1").arg(key);
        }
        configuration_->setWirelessSettings(wireless);
        QMetaObject::invokeMethod(this, &SettingsController::wirelessChanged, Qt::QueuedConnection);
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
            return tr("Port between 1024 and 65535");
        }
        configuration_->setApiPort(static_cast<uint16_t>(port));
        this->markPending(cRestart);
    }
    else if(key == "carAudio.enabled" || key == "carAudio.pin")
    {
        auto carAudio = configuration_->getCarAudioSettings();
        if(key == "carAudio.enabled")
        {
            carAudio.enabled = value.toBool();
        }
        else
        {
            const QString pin = value.toString().trimmed();
            if(pin.isEmpty() || pin.size() > 16)
            {
                return tr("PIN with 1 to 16 characters, mostly 0000 or 1234");
            }
            carAudio.pin = pin.toStdString();
        }
        configuration_->setCarAudioSettings(carAudio);
        QMetaObject::invokeMethod(this, &SettingsController::carAudioChanged, Qt::QueuedConnection);
    }
    else if(key == "api.hotspot")
    {
        // checked for every new connection, no restart needed
        configuration_->setApiAllowHotspot(value.toBool());
    }
    else if(key == "api.network")
    {
        configuration_->setApiListenAddress(value.toBool() ? "0.0.0.0" : "127.0.0.1");
        this->markPending(cRestart);
    }
    else
    {
        return tr("Unknown setting: %1").arg(key);
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

void SettingsController::refresh()
{
    emit changed();
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
