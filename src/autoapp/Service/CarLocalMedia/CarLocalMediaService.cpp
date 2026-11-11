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

#include <QMetaObject>
#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Plugins/AndroidAutoProviders.hpp>
#include <f1x/openauto/autoapp/Service/CarLocalMedia/CarLocalMediaService.hpp>

namespace f1x::openauto::autoapp::service::carlocalmedia
{

namespace
{

// the publisher of a bus event ("plugin:<id>", "client:<n>") may feed the service; a plugin's name ("Music")
bool allowed(const QString& busSource, QString* name)
{
    const QVariantMap pluginsState = core::EventBus::instance().last("plugins.state");
    if(!plugins::androidAutoServiceSourceAllowed(pluginsState, "localMedia", busSource))
    {
        return false;
    }
    // API clients (allowed like for the other services) are named by their media source
    if(!busSource.startsWith(QLatin1String("plugin:")))
    {
        return true;
    }
    const QString id = busSource.mid(7);
    *name = id;
    for(const QVariant& entry: pluginsState.value("plugins").toList())
    {
        const QVariantMap plugin = entry.toMap();
        if(plugin.value("id").toString() == id)
        {
            *name = plugin.value("name", id).toString();
        }
    }
    return true;
}

}

CarLocalMediaService::CarLocalMediaService(boost::asio::io_service& ioService,
                                           aasdk::messenger::IMessenger::Pointer messenger)
    : strand_(ioService)
    , channel_(std::make_shared<aasdk::channel::carlocalmedia::CarLocalMediaService>(strand_, std::move(messenger)))
{
}

bool CarLocalMediaService::available()
{
    return plugins::androidAutoDeclared(core::EventBus::instance().last("plugins.state"), "services", "localMedia");
}

void CarLocalMediaService::start()
{
    std::weak_ptr<CarLocalMediaService> weak = this->shared_from_this();
    // the bus lives in the GUI thread: the state there, the protocol in the strand
    QMetaObject::invokeMethod(&core::EventBus::instance(), [weak]() {
        auto service = weak.lock();
        if(!service)
        {
            return;
        }
        service->busConnection_ = QObject::connect(
            &core::EventBus::instance(), &core::EventBus::event, [weak](const QString& topic, const QVariantMap& data) {
                if(auto service = weak.lock())
                {
                    service->onBusEvent(topic, data);
                }
            });
        // what the sources published before the connection
        const auto& bus = core::EventBus::instance();
        for(const QString topic:
            {QStringLiteral("companion.state"), QStringLiteral("media.metadata"), QStringLiteral("media.status")})
        {
            service->onBusEvent(topic, bus.last(topic));
        }
    });
    strand_.dispatch([this, self = this->shared_from_this()]() {
        OPENAUTO_LOG(info) << "[CarLocalMediaService] start()";
        channel_->receive(this->shared_from_this());
    });
}

void CarLocalMediaService::stop()
{
    auto self = this->shared_from_this();
    QMetaObject::invokeMethod(&core::EventBus::instance(), [self]() { QObject::disconnect(self->busConnection_); });
    strand_.dispatch([this, self]() {
        OPENAUTO_LOG(info) << "[CarLocalMediaService] stop()";
        open_ = false;
    });
}

void CarLocalMediaService::fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse& response)
{
    OPENAUTO_LOG(info) << "[CarLocalMediaService] fillFeatures()";
    auto* service = response.add_channels();
    service->set_id(static_cast<uint32_t>(channel_->getId()));
    service->mutable_car_local_media_service();
}

void CarLocalMediaService::onBusEvent(const QString& topic, const QVariantMap& data)
{
    LocalMediaState::Changes changes;
    if(topic == QLatin1String("companion.state"))
    {
        // the companion app shows the head unit's music itself: this player is shown stopped meanwhile
        changes = state_.setCompanion(data.value("connected").toBool());
    }
    else
    {
        const bool status = topic == QLatin1String("media.status");
        if(!status && topic != QLatin1String("media.metadata"))
        {
            return;
        }
        const QString source = data.value("source").toString();
        QString name = source;
        if(source.isEmpty() || source == QLatin1String("androidauto")
           || !allowed(core::EventBus::instance().source(topic), &name))
        {
            return;
        }
        changes = status ? state_.onStatus(source, name, data) : state_.onMetadata(source, data);
    }
    if(!changes.status && !changes.metadata)
    {
        return;
    }
    // serialized here: the cover is decoded and scaled in this thread, not in the strand
    const std::string statusBytes =
        changes.status ? playbackStatus(state_.shownStatus(), state_.name()).SerializeAsString() : std::string();
    const std::string metadataBytes =
        changes.metadata ? playbackMetadata(state_.metadata()).SerializeAsString() : std::string();
    const QString current = state_.current();
    strand_.dispatch([this, self = this->shared_from_this(), statusBytes, metadataBytes, current]() {
        this->update(statusBytes, metadataBytes, current);
    });
}

void CarLocalMediaService::update(const std::string& status, const std::string& metadata, const QString& current)
{
    if(current != current_)
    {
        OPENAUTO_LOG(info) << "[CarLocalMediaService] source: " << current.toStdString();
        current_ = current;
    }
    if(!status.empty())
    {
        status_ = status;
    }
    if(!metadata.empty())
    {
        metadata_ = metadata;
    }
    this->flush();
}

void CarLocalMediaService::flush()
{
    if(!open_)
    {
        return;
    }
    // the metadata first: with the status "playing" the phone shows the card right away
    if(!metadata_.empty() && metadata_ != sentMetadata_)
    {
        msg::CarLocalMediaPlaybackMetadata metadata;
        metadata.ParseFromString(metadata_);
        channel_->sendPlaybackMetadata(metadata, this->promise());
        sentMetadata_ = metadata_;
    }
    if(!status_.empty() && status_ != sentStatus_)
    {
        msg::CarLocalMediaPlaybackStatus status;
        status.ParseFromString(status_);
        channel_->sendPlaybackStatus(status, this->promise());
        sentStatus_ = status_;
    }
}

void CarLocalMediaService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request)
{
    OPENAUTO_LOG(info) << "[CarLocalMediaService] onChannelOpenRequest(), priority " << request.priority();
    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
    channel_->sendChannelOpenResponse(response, this->promise());
    open_ = true;
    sentStatus_.clear();
    sentMetadata_.clear();
    this->flush();
    channel_->receive(this->shared_from_this());
}

void CarLocalMediaService::onChannelError(const aasdk::error::Error& e)
{
    OPENAUTO_LOG(error) << "[CarLocalMediaService] onChannelError(): " << e.what();
}

void CarLocalMediaService::onPlaybackRequest(const msg::CarLocalMediaPlaybackRequest& request)
{
    const QString action = mediaAction(request.action());
    OPENAUTO_LOG(info) << "[CarLocalMediaService] " << action.toStdString() << " for " << current_.toStdString();
    if(action.isEmpty() || current_.isEmpty())
    {
        return;
    }
    // for this source, also while another one (Android Auto's music) owns the media keys
    const QString source = current_;
    QMetaObject::invokeMethod(&core::EventBus::instance(), [action, source]() {
        core::EventBus::instance().emitEvent("input.action",
                                             {{"action", action}, {"phase", "click"}, {"source", source}});
    });
}

aasdk::channel::SendPromise::Pointer CarLocalMediaService::promise()
{
    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, [self = this->shared_from_this()](const aasdk::error::Error& e) { self->onChannelError(e); });
    return promise;
}

}
