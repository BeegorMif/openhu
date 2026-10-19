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

#include <f1x/openauto/autoapp/Api/CoreMethods.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Core/UsbReset.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace api
{

namespace
{

QVariant require(const QVariantMap& params, const QString& name)
{
    if(!params.contains(name))
    {
        throw ApiError("missing parameter: " + name.toStdString());
    }
    return params.value(name);
}

QVariantMap volumeState(const audio::VolumeController& volume)
{
    return {{"volume", volume.volume()}, {"muted", volume.muted()}};
}

}

void registerCoreMethods(ApiServer& server, audio::VolumeController& volume, shell::ShellController& shell,
                         input::ActionDispatcher& actions)
{
    auto& bus = core::EventBus::instance();

    server.registerMethod("system.ping", [](const QVariantMap&) -> QVariant {
        return QVariantMap{{"pong", true}};
    });
    server.registerMethod("system.methods", [&server](const QVariantMap&) -> QVariant {
        return server.methods();
    });

    server.registerMethod("bus.publish", [&bus](const QVariantMap& params) -> QVariant {
        const QString topic = require(params, "topic").toString();
        if(topic.isEmpty())
        {
            throw ApiError("empty topic");
        }
        bus.publish(topic, params.value("data").toMap(), params.value(ApiServer::cSource).toString());
        return QVariantMap();
    });
    server.registerMethod("bus.emit", [&bus](const QVariantMap& params) -> QVariant {
        const QString topic = require(params, "topic").toString();
        if(topic.isEmpty())
        {
            throw ApiError("empty topic");
        }
        bus.emitEvent(topic, params.value("data").toMap());
        return QVariantMap();
    });
    server.registerMethod("bus.get", [&bus](const QVariantMap& params) -> QVariant {
        return bus.last(require(params, "topic").toString());
    });
    server.registerMethod("bus.topics", [&bus](const QVariantMap&) -> QVariant {
        QStringList topics = bus.snapshot().keys();
        topics.sort();
        return topics;
    });

    server.registerMethod("volume.get", [&volume](const QVariantMap&) -> QVariant {
        return volumeState(volume);
    });
    server.registerMethod("volume.set", [&volume](const QVariantMap& params) -> QVariant {
        volume.setVolume(require(params, "value").toInt());
        return volumeState(volume);
    });
    server.registerMethod("volume.change", [&volume](const QVariantMap& params) -> QVariant {
        volume.changeVolume(require(params, "delta").toInt());
        return volumeState(volume);
    });
    server.registerMethod("volume.mute", [&volume](const QVariantMap& params) -> QVariant {
        // without "muted" the state is toggled
        if(params.contains("muted"))
        {
            volume.setMuted(params.value("muted").toBool());
        }
        else
        {
            volume.toggleMute();
        }
        return volumeState(volume);
    });

    server.registerMethod("ui.navigate", [&shell](const QVariantMap& params) -> QVariant {
        shell.navigate(require(params, "page").toString());
        return QVariantMap();
    });
    server.registerMethod("ui.toast", [&shell](const QVariantMap& params) -> QVariant {
        shell.toast(require(params, "text").toString(), params.value("duration", 3000).toInt());
        return QVariantMap();
    });

    server.registerMethod("input.action", [&actions](const QVariantMap& params) -> QVariant {
        const QString phase = params.value("phase", "click").toString();
        if(phase != "click" && phase != "press" && phase != "release")
        {
            throw ApiError("phase must be click, press or release");
        }
        return QVariantMap{{"handled", actions.trigger(require(params, "action").toString(), phase)}};
    });
    server.registerMethod("input.actions", [](const QVariantMap&) -> QVariant {
        return input::ActionDispatcher::actions();
    });
    server.registerMethod("input.keymap", [&actions](const QVariantMap&) -> QVariant {
        return actions.keymap();
    });

    server.registerMethod("projection.reconnect", [](const QVariantMap&) -> QVariant {
        return QVariantMap{{"reset", core::resetAndroidUsbDevices()}};
    });

    server.registerMethod("daynight.set", [&bus](const QVariantMap& params) -> QVariant {
        bus.publish("system.daynight", {{"night", require(params, "night").toBool()}});
        return QVariantMap();
    });
}

}
}
}
}
