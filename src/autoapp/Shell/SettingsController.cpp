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
#include <QRegularExpression>
#include <QGuiApplication>
#include <f1x/openauto/autoapp/Api/ClientFilter.hpp>
#include <f1x/openauto/autoapp/Shell/SettingsController.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
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
    this->publishNightMode();
}

void SettingsController::publishNightMode() const
{
    core::EventBus::instance().publish("system.nightMode",
                                       {{"mode", QString::fromStdString(configuration_->getNightMode())}});
}

QVariantMap SettingsController::keys()
{
    // applies: "now", "reconnect" (next Android Auto connection) or "restart" (of OpenHU)
    const auto choice = [](const QVariantList& choices, const QString& applies) {
        return QVariantMap{{"type", "choice"}, {"choices", choices}, {"applies", applies}};
    };
    const auto number = [](int min, int max, const QString& applies) {
        return QVariantMap{{"type", "int"}, {"min", min}, {"max", max}, {"applies", applies}};
    };
    const auto flag = [](const QString& applies) { return QVariantMap{{"type", "bool"}, {"applies", applies}}; };
    const auto text = [](const QString& applies, int maxLength = 0, bool secret = false) {
        QVariantMap key{{"type", "string"}, {"applies", applies}};
        if(maxLength > 0)
        {
            key.insert("maxLength", maxLength);
        }
        if(secret)
        {
            key.insert("secret", true);
        }
        return key;
    };
    QVariantList languages;
    for(const QString& language: Translations::available())
    {
        languages << language;
    }

    return {
        {"general.driverSide", choice({"left", "right"}, cReconnect)},
        {"general.showCursor", flag("now")},
        {"general.onScreenKeyboard", flag("now")},
        {"general.language", choice(languages, "now")},
        {"general.nameSuffix", text(cReconnect, 24)},
        {"aa.resolution", choice({"auto", "800x480", "1280x720", "1920x1080", "2560x1440"}, cReconnect)},
        {"aa.activeResolution", QVariantMap{{"type", "string"}, {"readOnly", true}}},
        {"aa.nightMode", choice({"auto", "day", "night"}, "now")},
        {"aa.fps", choice({30, 60}, cReconnect)},
        {"aa.dpi", number(80, 400, cReconnect)},
        {"aa.marginWidth", number(0, 600, cReconnect)},
        {"aa.marginHeight", number(0, 600, cReconnect)},
        {"aa.touchscreen", flag(cReconnect)},
        {"aa.wireless", flag("now")},
        {"aa.audioMedia", flag(cReconnect)},
        {"aa.audioGuidance", flag(cReconnect)},
        {"aa.audioTelephony", flag(cReconnect)},
        {"wireless.ssid", text("now", 32)},
        {"wireless.password", text("now", 63, true)},
        {"wireless.band", choice({"auto", "2.4", "5"}, "now")},
        {"wireless.channel", number(0, 165, "now")},
        {"wireless.country", text("now", 2)},
        {"wireless.priority", choice({"usb", "wifi"}, "now")},
        {"wireless.hotspot", choice({"always", "demand"}, "now")},
        {"wireless.interface", text("now", 15)},
        {"wireless.client", flag("now")},
        {"wireless.clientInterface", text("now", 15)},
        {"wireless.address", text("now")},
        {"wireless.dhcpRange", text("now")},
        {"audio.backend", choice({"rtaudio", "qt"}, cReconnect)},
        {"bluetooth.adapter", choice({"none", "local"}, cRestart)},
        {"bluetooth.address", text(cRestart)},
        {"carAudio.enabled", flag("now")},
        {"carAudio.pin", text("now", 16, true)},
        {"api.port", number(1024, 65535, cRestart)},
        {"api.network", flag(cRestart)},
        {"api.hotspot", flag("now")},
        // for clients from the network and browsers; shown in the shell, a new one only from there
        {"api.token", QVariantMap{{"type", "string"}, {"secret", true}, {"readOnly", true}, {"applies", "now"}}},
        {"api.origins", text("now")},
    };
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
        {"aa.nightMode", QString::fromStdString(configuration_->getNightMode())},
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
        {"wireless.interface", QString::fromStdString(wireless.interface)},
        {"wireless.client", wireless.client},
        {"wireless.clientInterface", QString::fromStdString(wireless.clientInterface)},
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
        {"api.token", QString::fromStdString(configuration_->getApiToken())},
        {"api.origins", QString::fromStdString(configuration_->getApiAllowedOrigins())},
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
            return qtTrId("settings.error.unknown_language");
        }
        configuration_->setLanguage(language.toStdString());
        emit languageChanged(language);
    }
    else if(key == "general.nameSuffix")
    {
        const QString suffix = value.toString().trimmed();
        if(suffix.size() > 24)
        {
            return qtTrId("settings.error.most_24_characters");
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
    else if(key == "aa.nightMode")
    {
        const QString mode = value.toString();
        configuration_->setNightMode(mode == "day" || mode == "night" ? mode.toStdString() : "auto");
        this->publishNightMode();
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
            return qtTrId("settings.error.dpi_between_80_400");
        }
        configuration_->setScreenDPI(static_cast<size_t>(dpi));
        this->markPending(cReconnect);
    }
    else if(key == "aa.marginWidth" || key == "aa.marginHeight")
    {
        const int margin = value.toInt();
        if(margin < 0 || margin > 600)
        {
            return qtTrId("settings.error.margin_between_0_600");
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
                return qtTrId("settings.error.most_32_characters");
            }
            wireless.ssid = text.toStdString();
        }
        else if(key == "wireless.password")
        {
            if(text.size() < 8 || text.size() > 63)
            {
                return qtTrId("settings.error.password_8_63_characters");
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
                return qtTrId("settings.error.2_4_ghz_channel");
            }
            if(channel != 0 && wireless.band == "5" && !valid5)
            {
                return qtTrId("settings.error.5_ghz_channel_36");
            }
            if(channel != 0 && wireless.band == "auto")
            {
                return qtTrId("settings.error.fixed_channel_only_fixed");
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
                return qtTrId("settings.error.address_netmask_e_g");
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
                    return qtTrId("settings.error.range_hotspot_network_e");
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
        else if(key == "wireless.client")
        {
            wireless.client = value.toBool();
        }
        else if(key == "wireless.interface" || key == "wireless.clientInterface")
        {
            // empty: automatic; otherwise a network device name like wlan0 or wlx00c0ca123456
            static const QRegularExpression name("^[A-Za-z0-9_.-]{1,15}$");
            if(!text.isEmpty() && !name.match(text).hasMatch())
            {
                return qtTrId("settings.error.not_wifi_card").arg(text);
            }
            (key == "wireless.interface" ? wireless.interface : wireless.clientInterface) = text.toStdString();
        }
        else if(key == "wireless.country")
        {
            if(text.size() != 2)
            {
                return qtTrId("settings.error.country_code_2_letters");
            }
            wireless.country = text.toUpper().toStdString();
        }
        else
        {
            return qtTrId("settings.error.unknown_setting").arg(key);
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
            return qtTrId("settings.error.port_between_1024_65535");
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
                return qtTrId("settings.error.pin_1_16_characters");
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
    else if(key == "api.origins")
    {
        configuration_->setApiAllowedOrigins(api::parseOrigins(value.toString()).join(',').toStdString());
        QMetaObject::invokeMethod(this, &SettingsController::apiAccessChanged, Qt::QueuedConnection);
    }
    else if(key == "api.network")
    {
        configuration_->setApiListenAddress(value.toBool() ? "0.0.0.0" : "127.0.0.1");
        this->markPending(cRestart);
    }
    else
    {
        return qtTrId("settings.error.unknown_setting").arg(key);
    }

    this->saved(key);
    return QString();
}

void SettingsController::regenerateApiToken()
{
    OPENAUTO_LOG(info) << "[SettingsController] new API token";
    configuration_->setApiToken(api::generateToken().toStdString());
    emit apiAccessChanged();
    this->saved("api.token");
}

void SettingsController::saved(const QString& key)
{
    configuration_->save();
    emit changed();
    // for the API (settings.changed); secrets without their value, the topic reaches clients from the network too
    QVariantMap event{{"key", key}, {"pending", pending_}};
    if(!keys().value(key).toMap().value("secret").toBool())
    {
        event.insert("value", this->values().value(key));
    }
    core::EventBus::instance().publish("settings.changed", event);
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
