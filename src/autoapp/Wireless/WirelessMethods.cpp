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

void registerWirelessMethods(api::ApiServer& server, WirelessController& wireless, bt::BluetoothManager& bluetooth,
                             bt::CarAudio& carAudio)
{
    server.registerMethod("caraudio.state", [&carAudio](const QVariantMap&) -> QVariant {
        return carAudio.state();
    });
    server.registerMethod("caraudio.select", [&carAudio](const QVariantMap& params) -> QVariant {
        const QString address = params.value("address").toString();
        if(address.isEmpty())
        {
            throw api::ApiError("missing parameter: address");
        }
        carAudio.selectDevice(address);
        return carAudio.state();
    });
    server.registerMethod("caraudio.forget", [&carAudio](const QVariantMap&) -> QVariant {
        carAudio.forgetDevice();
        return carAudio.state();
    });
    server.registerMethod("wireless.state", [&wireless](const QVariantMap&) -> QVariant {
        return wireless.state();
    });
    server.registerMethod("bluetooth.state", [&bluetooth](const QVariantMap&) -> QVariant {
        return bluetooth.state();
    });
    server.registerMethod("bluetooth.devices", [&bluetooth](const QVariantMap&) -> QVariant {
        return bluetooth.devices();
    });
    server.registerMethod("bluetooth.pairing", [&bluetooth](const QVariantMap& params) -> QVariant {
        bluetooth.setPairing(params.value("enabled", true).toBool());
        return QVariantMap{{"pairing", bluetooth.state().value("pairing")}};
    });
    server.registerMethod("bluetooth.confirm", [&bluetooth](const QVariantMap& params) -> QVariant {
        bluetooth.confirmPairing(params.value("accept").toBool());
        return QVariantMap();
    });
    server.registerMethod("bluetooth.search", [&bluetooth](const QVariantMap& params) -> QVariant {
        if(params.value("enabled", true).toBool())
        {
            bluetooth.startSearch(params.value("seconds", 30).toInt());
        }
        else
        {
            bluetooth.stopSearch();
        }
        return QVariantMap{{"discovered", bluetooth.discovered()}};
    });
    server.registerMethod("bluetooth.pair", [&bluetooth](const QVariantMap& params) -> QVariant {
        const QString address = params.value("address").toString();
        if(address.isEmpty())
        {
            throw api::ApiError("missing parameter: address");
        }
        bluetooth.pairWith(address, params.value("pin").toString());
        return QVariantMap();
    });
    server.registerMethod("bluetooth.remove", [&bluetooth](const QVariantMap& params) -> QVariant {
        bluetooth.removeDevice(params.value("address").toString());
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
