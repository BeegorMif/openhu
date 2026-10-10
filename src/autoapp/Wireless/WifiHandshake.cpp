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

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <aap_protobuf/aaw/MessageId.pb.h>
#include <aap_protobuf/aaw/Status.pb.h>
#include <aap_protobuf/aaw/WifiConnectionStatus.pb.h>
#include <aap_protobuf/aaw/WifiInfoResponse.pb.h>
#include <aap_protobuf/aaw/WifiStartRequest.pb.h>
#include <aap_protobuf/aaw/WifiStartResponse.pb.h>
#include <f1x/openauto/autoapp/Wireless/WifiHandshake.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::wireless
{

namespace aaw = aap_protobuf::aaw;

WifiHandshake::WifiHandshake(int fd, Network network, QObject* parent)
    : QObject(parent)
    , fd_(fd)
    , network_(std::move(network))
    , notifier_(fd, QSocketNotifier::Read)
{
    ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL) | O_NONBLOCK);
    ::fcntl(fd_, F_SETFD, FD_CLOEXEC);
    connect(&notifier_, &QSocketNotifier::activated, this, &WifiHandshake::onReadable);

    aaw::WifiStartRequest request;
    request.set_ip_address(network_.address.toStdString());
    request.set_port(network_.port);
    this->send(aaw::MessageId::WIFI_START_REQUEST, request);
}

WifiHandshake::~WifiHandshake()
{
    notifier_.setEnabled(false);
    ::close(fd_);
}

void WifiHandshake::onReadable()
{
    char data[1024];
    const ssize_t size = ::read(fd_, data, sizeof(data));
    if(size <= 0)
    {
        if(size < 0 && (errno == EAGAIN || errno == EINTR))
        {
            return;
        }
        notifier_.setEnabled(false);
        emit closed();
        return;
    }
    buffer_.append(data, static_cast<int>(size));

    while(buffer_.size() >= 4)
    {
        const auto* bytes = reinterpret_cast<const uchar*>(buffer_.constData());
        const int length = (bytes[0] << 8) | bytes[1];
        const quint16 id = static_cast<quint16>((bytes[2] << 8) | bytes[3]);
        if(buffer_.size() < length + 4)
        {
            return;
        }
        const QByteArray payload = buffer_.mid(4, length);
        buffer_.remove(0, length + 4);
        this->handle(id, payload);
    }
}

void WifiHandshake::handle(quint16 id, const QByteArray& payload)
{
    switch(id)
    {
    case aaw::MessageId::WIFI_INFO_REQUEST:
    {
        OPENAUTO_LOG(info) << "[WifiHandshake] sending WiFi credentials (" << network_.ssid.toStdString() << ")";
        aaw::WifiInfoResponse response;
        response.set_ssid(network_.ssid.toStdString());
        response.set_password(network_.password.toStdString());
        response.set_bssid(network_.bssid.toStdString());
        // Phones expect 8 for WPA2 personal; the enum of aap_protobuf names this value WPA2_ENTERPRISE
        response.set_security_mode(static_cast<aap_protobuf::service::wifiprojection::message::WifiSecurityMode>(8));
        response.set_access_point_type(aap_protobuf::service::wifiprojection::message::AccessPointType::DYNAMIC);
        this->send(aaw::MessageId::WIFI_INFO_RESPONSE, response);
        emit progress("info", QString());
        break;
    }
    case aaw::MessageId::WIFI_START_RESPONSE:
    {
        aaw::WifiStartResponse response;
        response.ParseFromArray(payload.constData(), payload.size());
        OPENAUTO_LOG(info) << "[WifiHandshake] start response: " << aaw::Status_Name(response.status());
        break;
    }
    case aaw::MessageId::WIFI_CONNECTION_STATUS:
    {
        aaw::WifiConnectionStatus status;
        status.ParseFromArray(payload.constData(), payload.size());
        OPENAUTO_LOG(info) << "[WifiHandshake] connection status: " << aaw::Status_Name(status.status()) << " "
                           << status.error_message();
        if(status.status() == aaw::Status::STATUS_SUCCESS)
        {
            emit progress("connecting", QString());
        }
        else
        {
            emit progress("failed", QString::fromStdString(aaw::Status_Name(status.status())));
        }
        break;
    }
    default:
        OPENAUTO_LOG(info) << "[WifiHandshake] ignoring message " << id << " (" << payload.size() << " bytes)";
        break;
    }
}

void WifiHandshake::send(quint16 id, const google::protobuf::Message& message)
{
    const auto size = static_cast<int>(message.ByteSizeLong());
    QByteArray frame(size + 4, 0);
    frame[0] = static_cast<char>((size >> 8) & 0xff);
    frame[1] = static_cast<char>(size & 0xff);
    frame[2] = static_cast<char>((id >> 8) & 0xff);
    frame[3] = static_cast<char>(id & 0xff);
    message.SerializeToArray(frame.data() + 4, size);

    // a few bytes on a fresh socket: a short write does not happen in practice
    if(::write(fd_, frame.constData(), frame.size()) != frame.size())
    {
        OPENAUTO_LOG(warning) << "[WifiHandshake] write failed: " << errno;
    }
}

}
