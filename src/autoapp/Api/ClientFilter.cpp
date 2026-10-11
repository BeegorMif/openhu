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

#include <f1x/openauto/autoapp/Api/ClientFilter.hpp>

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

// IPv4 clients of a dual-stack socket arrive as ::ffff:a.b.c.d
QHostAddress normalized(const QHostAddress& peer)
{
    bool isIpv4 = false;
    const quint32 ipv4 = peer.toIPv4Address(&isIpv4);
    return isIpv4 ? QHostAddress(ipv4) : peer;
}

}

QHostAddress listenAddress(const QString& configured)
{
    const QString address = configured.trimmed();
    return address == "::" ? QHostAddress(QHostAddress::Any) : QHostAddress(address);
}

bool isHotspotPeer(const QHostAddress& peer, const HotspotNetwork& hotspot)
{
    const QHostAddress address = normalized(peer);

    if(address.protocol() == QAbstractSocket::IPv4Protocol)
    {
        const auto subnet = QHostAddress::parseSubnet(hotspot.subnet);
        if(subnet.second >= 0 && address.isInSubnet(subnet))
        {
            return true;
        }
    }

    // fe80::... carries the interface it came from
    if(address.isLinkLocal() && !address.scopeId().isEmpty() && !hotspot.interfaceName.isEmpty())
    {
        return address.scopeId() == hotspot.interfaceName;
    }

    for(const auto& entry: hotspot.addresses)
    {
        if(entry.ip().protocol() == address.protocol() && entry.prefixLength() >= 0
           && address.isInSubnet(entry.ip(), entry.prefixLength()))
        {
            return true;
        }
    }
    return false;
}

bool clientAllowed(const QHostAddress& peer, bool blockHotspot, const HotspotNetwork& hotspot)
{
    const QHostAddress address = normalized(peer);
    if(address.isLoopback() || !blockHotspot)
    {
        return true;
    }
    if(isHotspotPeer(address, hotspot))
    {
        return false;
    }
    // IPv6 addresses of the hotspot are only known from its interface
    return address.protocol() == QAbstractSocket::IPv4Protocol || !hotspot.interfaceName.isEmpty();
}

}
}
}
}
