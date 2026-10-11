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

#include <QHostAddress>
#include <QList>
#include <QNetworkAddressEntry>
#include <QString>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace api
{

// The wireless hotspot of OpenHU as the API client filter sees it
struct HotspotNetwork
{
    QString subnet;                          // configured address with prefix, "10.42.0.1/24"
    QString interfaceName;                   // empty while the hotspot is not up
    QList<QNetworkAddressEntry> addresses;   // all addresses of that interface, IPv4 and IPv6
};

// Listen address from the configuration: "::" means all addresses, IPv4 and IPv6 (QHostAddress("::") alone
// would only accept IPv6).
QHostAddress listenAddress(const QString& configured);

// True if the peer belongs to the hotspot network (IPv4 subnet, any address of the hotspot interface, or a
// link-local address scoped to it).
bool isHotspotPeer(const QHostAddress& peer, const HotspotNetwork& hotspot);

// Whether an API client may connect. Loopback always may; with blockHotspot (wireless projection on and
// Api.AllowHotspot=false) clients of the hotspot may not, and neither may IPv6 clients while the hotspot
// interface is unknown, because they cannot be told apart.
bool clientAllowed(const QHostAddress& peer, bool blockHotspot, const HotspotNetwork& hotspot);

}
}
}
}
