/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Service/NavigationStatus/NavigationStatusService.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Core/DataUrl.hpp>
#include <QVariantList>
#include <fstream>
#include <QString>

namespace f1x::openauto::autoapp::service::navigationstatus {

  NavigationStatusService::NavigationStatusService(boost::asio::io_service &ioService,
                                                   aasdk::messenger::IMessenger::Pointer messenger)
      : strand_(ioService),
        timer_(ioService),
        channel_(std::make_shared<aasdk::channel::navigationstatus::NavigationStatusService>(strand_,
                                                                                             std::move(messenger))) {

  }

  void NavigationStatusService::start() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[NavigationStatusService] start()";
      channel_->receive(this->shared_from_this());
    });
  }

  void NavigationStatusService::stop() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[NavigationStatusService] stop()";
    });
  }

  void NavigationStatusService::pause() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[NavigationStatusService] pause()";
    });
  }

  void NavigationStatusService::resume() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[NavigationStatusService] resume()";
    });
  }

  void NavigationStatusService::fillFeatures(
      aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
    OPENAUTO_LOG(info) << "[NavigationStatusService] fillFeatures()";

    auto *service = response.add_channels();
    service->set_id(static_cast<uint32_t>(channel_->getId()));

    // Both fields are required, without them the service discovery response cannot be serialized.
    auto *navigationStatus = service->mutable_navigation_status_service();
    // Maneuvers as enums, the shell draws its own arrows
    navigationStatus->set_minimum_interval_ms(500);
    navigationStatus->set_type(aap_protobuf::service::navigationstatus::NavigationStatusService::ENUM);
  }

  void NavigationStatusService::onChannelOpenRequest(
      const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
    OPENAUTO_LOG(info) << "[NavigationStatusService] onChannelOpenRequest()";
    OPENAUTO_LOG(info) << "[NavigationStatusService] Channel Id: " << request.service_id() << ", Priority: "
                       << request.priority();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    const aap_protobuf::shared::MessageStatus status = aap_protobuf::shared::MessageStatus::STATUS_SUCCESS;
    response.set_status(status);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&NavigationStatusService::onChannelError, this->shared_from_this(),
                                     std::placeholders::_1));
    channel_->sendChannelOpenResponse(response, std::move(promise));

    channel_->receive(this->shared_from_this());
  }

  // All navigation data is published in one format, whether the phone uses the deprecated
  // turn/distance events or the current state/position messages (see docs/api.md).

  void NavigationStatusService::onStatusUpdate(
      const aap_protobuf::service::navigationstatus::message::NavigationStatus &navStatus) {
    using Status = aap_protobuf::service::navigationstatus::message::NavigationStatus;
    QString state;
    switch (navStatus.status()) {
      case Status::ACTIVE: state = "active"; break;
      case Status::INACTIVE: state = "inactive"; break;
      case Status::REROUTING: state = "rerouting"; break;
      default: state = "unavailable"; break;
    }
    core::EventBus::instance().publish("nav.status", {{"state", state}});
    channel_->receive(this->shared_from_this());
  }

  void NavigationStatusService::onTurnEvent(
      const aap_protobuf::service::navigationstatus::message::NavigationNextTurnEvent &turnEvent) {
    using TurnEvent = aap_protobuf::service::navigationstatus::message::NavigationNextTurnEvent;
    QVariantMap turn;
    turn["road"] = QString::fromStdString(turnEvent.road());
    turn["maneuver"] = core::enumName(TurnEvent::NextTurnEnum_Name(turnEvent.event()));
    turn["side"] = turnEvent.turn_side() == TurnEvent::LEFT ? "left"
                   : turnEvent.turn_side() == TurnEvent::RIGHT ? "right" : "unspecified";
    turn["roundaboutExit"] = turnEvent.turn_number();
    turn["turnAngle"] = turnEvent.turn_angle();
    turn["image"] = core::imageDataUrl(turnEvent.image());
    core::EventBus::instance().publish("nav.turn", turn);
    channel_->receive(this->shared_from_this());
  }

  void NavigationStatusService::onDistanceEvent(
      const aap_protobuf::service::navigationstatus::message::NavigationNextTurnDistanceEvent &distanceEvent) {
    using DistanceEvent = aap_protobuf::service::navigationstatus::message::NavigationNextTurnDistanceEvent;
    QVariantMap distance;
    distance["meters"] = distanceEvent.distance_meters();
    distance["seconds"] = distanceEvent.time_to_turn_seconds();
    distance["display"] = QString::number(distanceEvent.display_distance_e3() / 1000.0);
    distance["unit"] = core::enumName(DistanceEvent::DistanceUnits_Name(distanceEvent.display_distance_unit()));
    core::EventBus::instance().publish("nav.distance", distance);
    channel_->receive(this->shared_from_this());
  }

  void NavigationStatusService::onNavigationState(
      const aap_protobuf::service::navigationstatus::message::NavigationState &state) {
    using Maneuver = aap_protobuf::service::navigationstatus::message::NavigationManeuver;
    using LaneDirection = aap_protobuf::service::navigationstatus::message::NavigationLane::LaneDirection;

    if (state.steps_size() > 0) {
      const auto &step = state.steps(0);
      QVariantMap turn;
      turn["road"] = QString::fromStdString(step.road().name());
      turn["maneuver"] = core::enumName(Maneuver::NavigationType_Name(step.maneuver().type()));
      const QString maneuver = turn["maneuver"].toString();
      turn["side"] = maneuver.endsWith("left") ? "left" : maneuver.endsWith("right") ? "right" : "unspecified";
      turn["roundaboutExit"] = step.maneuver().roundabout_exit_number();
      turn["turnAngle"] = step.maneuver().roundabout_exit_angle();
      turn["image"] = QString();

      QVariantList lanes;
      for (const auto &lane : step.lanes()) {
        QVariantList directions;
        bool highlighted = false;
        for (const auto &direction : lane.lane_directions()) {
          directions.append(core::enumName(LaneDirection::Shape_Name(direction.shape())));
          highlighted = highlighted || direction.is_highlighted();
        }
        lanes.append(QVariantMap{{"directions", directions}, {"highlighted", highlighted}});
      }
      turn["lanes"] = lanes;

      if (step.cue().alternate_text_size() > 0) {
        turn["cue"] = QString::fromStdString(step.cue().alternate_text(0));
      }
      core::EventBus::instance().publish("nav.turn", turn);
    }

    if (state.destinations_size() > 0) {
      destinationAddress_ = state.destinations(0).address();
      QVariantMap destination = core::EventBus::instance().last("nav.destination");
      destination["address"] = QString::fromStdString(destinationAddress_);
      core::EventBus::instance().publish("nav.destination", destination);
    }

    channel_->receive(this->shared_from_this());
  }

  void NavigationStatusService::onCurrentPosition(
      const aap_protobuf::service::navigationstatus::message::NavigationCurrentPosition &position) {
    using Distance = aap_protobuf::service::navigationstatus::message::NavigationDistance;

    if (position.has_step_distance()) {
      const auto &stepDistance = position.step_distance();
      QVariantMap distance;
      distance["meters"] = stepDistance.distance().meters();
      distance["seconds"] = static_cast<qlonglong>(stepDistance.time_to_step_seconds());
      distance["display"] = QString::fromStdString(stepDistance.distance().display_value());
      distance["unit"] = core::enumName(Distance::DistanceUnits_Name(stepDistance.distance().display_units()));
      core::EventBus::instance().publish("nav.distance", distance);
    }

    if (position.destination_distances_size() > 0) {
      const auto &destinationDistance = position.destination_distances(0);
      QVariantMap destination;
      destination["address"] = QString::fromStdString(destinationAddress_);
      destination["meters"] = destinationDistance.distance().meters();
      destination["display"] = QString::fromStdString(destinationDistance.distance().display_value());
      destination["unit"] = core::enumName(Distance::DistanceUnits_Name(destinationDistance.distance().display_units()));
      destination["seconds"] = static_cast<qlonglong>(destinationDistance.time_to_arrival_seconds());
      destination["eta"] = QString::fromStdString(destinationDistance.estimated_time_at_arrival());
      core::EventBus::instance().publish("nav.destination", destination);
    }

    if (position.has_current_road()) {
      core::EventBus::instance().publish("nav.road", {{"name", QString::fromStdString(position.current_road().name())}});
    }

    channel_->receive(this->shared_from_this());
  }


  void NavigationStatusService::onChannelError(const aasdk::error::Error &e) {
    OPENAUTO_LOG(error) << "[NavigationStatusService] onChannelError(): " << e.what();
  }
}



