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

#include <QRandomGenerator>
#include <QUrlQuery>
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

// "HTTP://Host:8080/" and "http://host:8080" are the same origin
QString normalizedOrigin(const QString& origin)
{
    QString text = origin.trimmed().toLower();
    while(text.endsWith('/'))
    {
        text.chop(1);
    }
    return text;
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

bool isLoopbackPeer(const QHostAddress& peer)
{
    return normalized(peer).isLoopback();
}

QString generateToken()
{
    QByteArray bytes(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()), bytes.size() / sizeof(quint32));
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool tokenMatches(const QString& given, const QString& expected)
{
    const QByteArray a = given.toUtf8();
    const QByteArray b = expected.toUtf8();
    if(b.isEmpty() || a.size() != b.size())
    {
        return false;
    }
    // every byte compared, so the time does not tell how much of the token was right
    unsigned char difference = 0;
    for(qsizetype i = 0; i < a.size(); ++i)
    {
        difference |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    }
    return difference == 0;
}

QString handshakeToken(const QNetworkRequest& request)
{
    const QString authorization = QString::fromLatin1(request.rawHeader("Authorization")).trimmed();
    if(authorization.startsWith("Bearer ", Qt::CaseInsensitive))
    {
        return authorization.mid(7).trimmed();
    }
    return QUrlQuery(request.url()).queryItemValue("token", QUrl::FullyDecoded);
}

QStringList parseOrigins(const QString& configured)
{
    QStringList origins;
    for(const QString& entry: configured.split(',', Qt::SkipEmptyParts))
    {
        const QString origin = normalizedOrigin(entry);
        if(!origin.isEmpty())
        {
            origins << origin;
        }
    }
    return origins;
}

bool originAllowed(const QString& origin, const QStringList& allowed)
{
    return origin.isEmpty() || allowed.contains("*") || allowed.contains(normalizedOrigin(origin));
}

}
}
}
}
