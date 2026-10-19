/*
*  This file is part of OpenHU, based on the openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Service/Bluetooth/BluetoothService.hpp>

namespace f1x::openauto::autoapp::service::bluetooth {

  BluetoothService::BluetoothService(boost::asio::io_service &ioService,
                                     aasdk::messenger::IMessenger::Pointer messenger,
                                     projection::IBluetoothDevice::Pointer bluetoothDevice)
      : strand_(ioService),
        channel_(std::make_shared<aasdk::channel::bluetooth::BluetoothService>(strand_, std::move(messenger))),
        bluetoothDevice_(std::move(bluetoothDevice)) {

  }

  void BluetoothService::start() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[BluetoothService] start()";
      channel_->receive(this->shared_from_this());
    });
  }

  void BluetoothService::stop() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[BluetoothService] stop()";
      QObject::disconnect(passkeyConnection_);
      bluetoothDevice_->stop();
    });
  }

  void BluetoothService::pause() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[BluetoothService] pause()";
    });
  }

  void BluetoothService::resume() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[BluetoothService] resume()";
    });
  }

  void BluetoothService::fillFeatures(
      aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
    OPENAUTO_LOG(info) << "[BluetoothService] fillFeatures()";

    auto *service = response.add_channels();
    service->set_id(static_cast<uint32_t>(channel_->getId()));

    auto bluetooth = service->mutable_bluetooth_service();

    if (bluetoothDevice_->isAvailable()) {
      OPENAUTO_LOG(info) << "[BluetoothService] Local Address: " << bluetoothDevice_->getAdapterAddress();

      // TODO: Also need to re-establish Bluetooth
      // If the HU wants the MD to skip the Bluetooth Pairing and Connection process, the HU can declare its address as SKIP_THIS_BLUETOOTH
      bluetooth->set_car_address(bluetoothDevice_->getAdapterAddress());

      // AAP supports bth PIN and Numeric Comparison as pairing methods.
      bluetooth->add_supported_pairing_methods(
          aap_protobuf::service::bluetooth::message::BluetoothPairingMethod::BLUETOOTH_PAIRING_PIN);
      bluetooth->add_supported_pairing_methods(
          aap_protobuf::service::bluetooth::message::BluetoothPairingMethod::BLUETOOTH_PAIRING_NUMERIC_COMPARISON);
    } else {
      OPENAUTO_LOG(info) << "[BluetoothService] Bluetooth Not Available ";
      bluetooth->set_car_address("");
      bluetooth->add_supported_pairing_methods(aap_protobuf::service::bluetooth::message::BluetoothPairingMethod::BLUETOOTH_PAIRING_UNAVAILABLE);
    }
  }

  void
  BluetoothService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
    OPENAUTO_LOG(info) << "[BluetoothService] onChannelOpenRequest()";
    OPENAUTO_LOG(debug) << "[BluetoothService] Channel Id: " << request.service_id() << ", Priority: "
                        << request.priority();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    const aap_protobuf::shared::MessageStatus status = aap_protobuf::shared::MessageStatus::STATUS_SUCCESS;
    response.set_status(status);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&BluetoothService::onChannelError, this->shared_from_this(),
                                     std::placeholders::_1));
    channel_->sendChannelOpenResponse(response, std::move(promise));
    channel_->receive(this->shared_from_this());
  }

  void BluetoothService::onBluetoothPairingRequest(
      const aap_protobuf::service::bluetooth::message::BluetoothPairingRequest &request) {
    OPENAUTO_LOG(info) << "[BluetoothService] onBluetoothPairingRequest()";
    OPENAUTO_LOG(info) << "[BluetoothService] Phone Address: " << request.phone_address();

    aap_protobuf::service::bluetooth::message::BluetoothPairingResponse response;

    const auto isPaired = bluetoothDevice_->isPaired(request.phone_address());
    if (isPaired) {
      OPENAUTO_LOG(info) << "[BluetoothService] Phone is Already Paired";
    } else {
      OPENAUTO_LOG(info) << "[BluetoothService] Phone is Not Paired";
    }

    /*
     * The HU must always sent a STATUS_SUCCESS response,
     * or STATUS_BLUETOOTH_PAIRING_DELAYED if:
     *    there's a delay in allowing bluetooth
     *    the HU is already engaged in a bluetooth call
     */
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
    response.set_already_paired(isPaired);

    if (!isPaired) {
      // The phone pairs with OpenHU next: the wireless controller accepts exactly this phone (it is on the cable)
      // and reports the passkey, which goes back to the phone so it can confirm by itself.
      phoneAddress_ = request.phone_address();
      QObject::disconnect(passkeyConnection_);
      std::weak_ptr<BluetoothService> weak = this->shared_from_this();
      passkeyConnection_ = QObject::connect(&core::EventBus::instance(), &core::EventBus::event,
                                            [weak](const QString& topic, const QVariantMap& data) {
        if (topic != "bluetooth.passkey") {
          return;
        }
        if (auto self = weak.lock()) {
          const std::string address = data.value("address").toString().toUpper().toStdString();
          const std::string passkey = data.value("passkey").toString().toStdString();
          self->strand_.dispatch([self, address, passkey]() {
            if (QString::fromStdString(self->phoneAddress_).toUpper().toStdString() == address) {
              self->sendBluetoothAuthenticationData(passkey);
            }
          });
        }
      });
      core::EventBus::instance().emitEvent("bluetooth.expect", {{"address", QString::fromStdString(phoneAddress_)}});
    }

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&BluetoothService::onChannelError, this->shared_from_this(),
                                     std::placeholders::_1));
    channel_->sendBluetoothPairingResponse(response, std::move(promise));
    channel_->receive(this->shared_from_this());
  }

  void BluetoothService::sendBluetoothAuthenticationData(const std::string& passkey) {
    OPENAUTO_LOG(info) << "[BluetoothService] sendBluetoothAuthenticationData()";
    aap_protobuf::service::bluetooth::message::BluetoothAuthenticationData data;
    data.set_auth_data(passkey);
    data.set_pairing_method(
        aap_protobuf::service::bluetooth::message::BluetoothPairingMethod::BLUETOOTH_PAIRING_NUMERIC_COMPARISON);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {}, std::bind(&BluetoothService::onChannelError, this->shared_from_this(),
                                     std::placeholders::_1));
    channel_->sendBluetoothAuthenticationData(data, std::move(promise));
  }

  void BluetoothService::onBluetoothAuthenticationResult(
      const aap_protobuf::service::bluetooth::message::BluetoothAuthenticationResult &request) {
    OPENAUTO_LOG(info) << "[BluetoothService] onBluetoothAuthenticationResult()";
    OPENAUTO_LOG(info) << "[BluetoothService] AuthData " << request.status();
    aap_protobuf::service::bluetooth::message::BluetoothPairingResponse response;

    channel_->receive(this->shared_from_this());
  }

  void BluetoothService::onChannelError(const aasdk::error::Error &e) {
    // OPERATION_ABORTED is expected during shutdown when messenger stops
    if (e.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED) {
      OPENAUTO_LOG(debug) << "[BluetoothService] onChannelError(): " << e.what() << " (expected during stop)";
    } else {
      OPENAUTO_LOG(error) << "[BluetoothService] onChannelError(): " << e.what();
    }
  }
}



