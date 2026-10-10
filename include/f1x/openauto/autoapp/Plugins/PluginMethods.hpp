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

#include <QObject>
#include <QVariantMap>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginStore.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

// plugin.*, ui.overlay.*, store.* (see docs/api.md)
void registerPluginMethods(api::ApiServer& server, PluginManager& manager, PluginStore& store);

// API for QML plugins (context property "oa"): oa.request("volume.set", {value: 40})
// returns {result: ...} or {error: "..."}.
class QmlApi: public QObject
{
    Q_OBJECT

public:
    explicit QmlApi(api::ApiServer& server, QObject* parent = nullptr);

    Q_INVOKABLE QVariantMap request(const QString& method, const QVariantMap& params = QVariantMap());
    Q_INVOKABLE void publish(const QString& topic, const QVariantMap& data);
    // One-shot event, e.g. a command for the plugin's process: oa.emit("radio.command", {cmd: "tune", frequency: 101.3})
    Q_INVOKABLE void emitEvent(const QString& topic, const QVariantMap& data);

private:
    api::ApiServer& server_;
};

}
}
}
}
