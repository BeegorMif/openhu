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

#include <f1x/openauto/autoapp/Plugins/PluginMethods.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

namespace
{

QString requireString(const QVariantMap& params, const QString& name)
{
    const QString value = params.value(name).toString();
    if(value.isEmpty())
    {
        throw api::ApiError("missing parameter: " + name.toStdString());
    }
    return value;
}

void check(bool ok, const QString& message)
{
    if(!ok)
    {
        throw api::ApiError(message.toStdString());
    }
}

void checkError(const QString& error)
{
    if(!error.isEmpty())
    {
        throw api::ApiError(error.toStdString());
    }
}

}

void registerPluginMethods(api::ApiServer& server, PluginManager& manager, PluginStore& store)
{
    server.registerMethod("plugin.list", [&manager](const QVariantMap&) -> QVariant {
        return manager.plugins();
    });
    server.registerMethod("plugin.enable", [&manager](const QVariantMap& params) -> QVariant {
        const QString id = requireString(params, "id");
        check(manager.setEnabled(id, true), "unknown plugin: " + id);
        return manager.plugin(id);
    });
    server.registerMethod("plugin.disable", [&manager](const QVariantMap& params) -> QVariant {
        const QString id = requireString(params, "id");
        check(manager.setEnabled(id, false), "unknown plugin: " + id);
        return manager.plugin(id);
    });
    server.registerMethod("plugin.setDock", [&manager](const QVariantMap& params) -> QVariant {
        const QString id = requireString(params, "id");
        check(params.contains("dock"), "missing parameter: dock");
        check(manager.setDock(id, params.value("dock").toBool()), "unknown plugin: " + id);
        return manager.plugin(id);
    });
    server.registerMethod("plugin.restart", [&manager](const QVariantMap& params) -> QVariant {
        const QString id = requireString(params, "id");
        check(manager.restart(id), "unknown or disabled plugin: " + id);
        return manager.plugin(id);
    });

    server.registerMethod("ui.overlay.show", [&manager](const QVariantMap& params) -> QVariant {
        const QString id = requireString(params, "id");
        check(manager.setOverlayVisible(id, true), "plugin without overlay or disabled: " + id);
        return QVariantMap();
    });
    server.registerMethod("ui.overlay.hide", [&manager](const QVariantMap& params) -> QVariant {
        const QString id = requireString(params, "id");
        check(manager.setOverlayVisible(id, false), "plugin without overlay or disabled: " + id);
        return QVariantMap();
    });

    // Store operations run in the background, progress via the "store.state" topic.
    server.registerMethod("store.list", [&store](const QVariantMap&) -> QVariant {
        return store.packages();
    });
    server.registerMethod("store.refresh", [&store](const QVariantMap&) -> QVariant {
        store.refresh();
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("store.install", [&store](const QVariantMap& params) -> QVariant {
        store.install(requireString(params, "id"));
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("store.update", [&store](const QVariantMap& params) -> QVariant {
        // without id: all updates
        if(params.contains("id"))
        {
            store.install(requireString(params, "id"));
        }
        else
        {
            store.updateAll();
        }
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("store.remove", [&store](const QVariantMap& params) -> QVariant {
        store.remove(requireString(params, "id"));
        return QVariantMap{{"queued", true}};
    });
    server.registerMethod("store.sources.list", [&store](const QVariantMap&) -> QVariant {
        return store.sources();
    });
    server.registerMethod("store.sources.add", [&store](const QVariantMap& params) -> QVariant {
        checkError(store.addSource(requireString(params, "type"), requireString(params, "location"),
                                   params.value("branch").toString()));
        return store.sources();
    });
    server.registerMethod("store.sources.remove", [&store](const QVariantMap& params) -> QVariant {
        check(params.contains("index"), "missing parameter: index");
        checkError(store.removeSource(params.value("index").toInt()));
        return store.sources();
    });
}

QmlApi::QmlApi(api::ApiServer& server, QObject* parent)
    : QObject(parent)
    , server_(server)
{
}

QVariantMap QmlApi::request(const QString& method, const QVariantMap& params)
{
    try
    {
        return {{"result", server_.call(method, params)}};
    }
    catch(const std::exception& e)
    {
        OPENAUTO_LOG(warning) << "[QmlApi] " << method.toStdString() << ": " << e.what();
        return {{"error", QString::fromStdString(e.what())}};
    }
}

void QmlApi::publish(const QString& topic, const QVariantMap& data)
{
    core::EventBus::instance().publish(topic, data);
}

void QmlApi::emitEvent(const QString& topic, const QVariantMap& data)
{
    core::EventBus::instance().emitEvent(topic, data);
}

}
}
}
}
