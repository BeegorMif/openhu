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

#include <string>
#include <QMetaObject>
#include <boost/asio.hpp>
#include <aasdk/Channel/CarLocalMedia/CarLocalMediaService.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <f1x/openauto/autoapp/Service/CarLocalMedia/CarLocalMediaMapping.hpp>

namespace f1x::openauto::autoapp::service::carlocalmedia
{

// The head unit's own media sources (music player, ...) in the phone's media card (openhu#147): title, artist and
// cover with play/pause/previous/next. Fed by media.status / media.metadata of plugins that declare the service
// "localMedia" in their manifest; the buttons go to that source as input.action with its "source". There is no
// browsing in Android Auto: the songs are chosen on OpenHU's own pages.
class CarLocalMediaService: public aasdk::channel::carlocalmedia::ICarLocalMediaServiceEventHandler,
                            public IService,
                            public std::enable_shared_from_this<CarLocalMediaService>
{
public:
    CarLocalMediaService(boost::asio::io_service& ioService, aasdk::messenger::IMessenger::Pointer messenger);

    // an enabled plugin declares "localMedia"
    static bool available();

    void start() override;
    void stop() override;
    void pause() override {}
    void resume() override {}
    void fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse& response) override;

    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override;
    void onChannelError(const aasdk::error::Error& e) override;
    void onPlaybackRequest(const msg::CarLocalMediaPlaybackRequest& request) override;

private:
    using std::enable_shared_from_this<CarLocalMediaService>::shared_from_this;

    // GUI thread: media.status / media.metadata of an allowed source
    void onBusEvent(const QString& topic, const QVariantMap& data);
    // strand: the latest status / metadata (serialized, empty: unchanged); sent once the channel is open and only
    // when it differs from what the phone has
    void update(const std::string& status, const std::string& metadata, const QString& current);
    void flush();
    aasdk::channel::SendPromise::Pointer promise();

    boost::asio::io_service::strand strand_;
    aasdk::channel::carlocalmedia::CarLocalMediaService::Pointer channel_;
    QMetaObject::Connection busConnection_;
    LocalMediaState state_;  // GUI thread
    // strand
    bool open_ = false;
    std::string status_;
    std::string metadata_;
    std::string sentStatus_;
    std::string sentMetadata_;
    QString current_;  // the source the buttons go to
};

}
