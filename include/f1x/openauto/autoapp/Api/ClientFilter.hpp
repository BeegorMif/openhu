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

#pragma once

#include <QHostAddress>
#include <QList>
#include <QNetworkAddressEntry>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>

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

// A client on this device (also IPv4 loopback of a dual-stack socket, ::ffff:127.0.0.1)
bool isLoopbackPeer(const QHostAddress& peer);

// API token (Api.Token): 32 random bytes, base64url without padding
QString generateToken();
// Constant-time comparison; an empty expected token matches nothing
bool tokenMatches(const QString& given, const QString& expected);
// Token from the WebSocket handshake: "Authorization: Bearer <token>" or "?token=<token>", empty without one
QString handshakeToken(const QNetworkRequest& request);

// Api.AllowedOrigins: "*" (default) or a comma separated list of origins ("http://192.168.1.10:8080")
QStringList parseOrigins(const QString& configured);
// Whether a client with this Origin header may connect; clients without one (not a browser) always may
bool originAllowed(const QString& origin, const QStringList& allowed);

}
}
}
}
