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


#include <f1x/openauto/autoapp/Wireless/WirelessMethods.hpp>

namespace f1x::openauto::autoapp::wireless
{

void registerWirelessMethods(api::ApiServer& server, WirelessController& wireless)
{
    server.registerMethod("wireless.state", [&wireless](const QVariantMap&) -> QVariant {
        return wireless.state();
    });
    server.registerMethod("bluetooth.devices", [&wireless](const QVariantMap&) -> QVariant {
        return wireless.devices();
    });
    server.registerMethod("bluetooth.pairing", [&wireless](const QVariantMap& params) -> QVariant {
        wireless.setPairing(params.value("enabled", true).toBool());
        return QVariantMap{{"pairing", wireless.state().value("pairing")}};
    });
    server.registerMethod("bluetooth.confirm", [&wireless](const QVariantMap& params) -> QVariant {
        wireless.confirmPairing(params.value("accept").toBool());
        return QVariantMap();
    });
    server.registerMethod("bluetooth.connect", [&wireless](const QVariantMap& params) -> QVariant {
        const QString address = params.value("address").toString();
        if(address.isEmpty())
        {
            throw api::ApiError("missing parameter: address");
        }
        wireless.connectDevice(address);
        return QVariantMap();
    });
}

}
