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

#include <QObject>

#include <aasdk/Channel/MediaSink/Audio/Channel/MediaAudioChannel.hpp>
#include <aasdk/Channel/MediaSink/Audio/Channel/SystemAudioChannel.hpp>
#include <aasdk/Channel/MediaSink/Audio/Channel/GuidanceAudioChannel.hpp>
#include <aasdk/Channel/MediaSink/Audio/Channel/TelephonyAudioChannel.hpp>

#include <f1x/openauto/autoapp/Service/ServiceFactory.hpp>

#include <f1x/openauto/autoapp/Service/MediaSink/VideoService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/MediaAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/GuidanceAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/SystemAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/TelephonyAudioService.hpp>

#include <f1x/openauto/autoapp/Service/MediaSource/MicrophoneMediaSourceService.hpp>

#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <f1x/openauto/autoapp/Service/Bluetooth/BluetoothService.hpp>
#include <f1x/openauto/autoapp/Service/InputSource/InputSourceService.hpp>
#include <f1x/openauto/autoapp/Service/NavigationStatus/NavigationStatusService.hpp>
#include <f1x/openauto/autoapp/Service/MediaPlaybackStatus/MediaPlaybackStatusService.hpp>
#include <f1x/openauto/autoapp/Service/PhoneStatus/PhoneStatusService.hpp>
#include <f1x/openauto/autoapp/Projection/FfmpegVideoOutput.hpp>
#include <f1x/openauto/autoapp/Projection/RtAudioOutput.hpp>
#include <f1x/openauto/autoapp/Projection/QtAudioOutput.hpp>
#include <f1x/openauto/autoapp/Projection/QtAudioInput.hpp>
#include <f1x/openauto/autoapp/Projection/InputDevice.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include <f1x/openauto/autoapp/Projection/BluezBluetoothDevice.hpp>
#include <f1x/openauto/autoapp/Projection/DummyBluetoothDevice.hpp>

namespace f1x::openauto::autoapp::service {

  ServiceFactory::ServiceFactory(boost::asio::io_service &ioService,
                                 configuration::IConfiguration::Pointer configuration)
      : ioService_(ioService), configuration_(std::move(configuration)) {

  }

  ServiceList ServiceFactory::create(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] create()";
    ServiceList serviceList;

    this->createMediaSinkServices(serviceList, messenger);
    this->createMediaSourceServices(serviceList, messenger);
    serviceList.emplace_back(this->createSensorService(messenger));
    serviceList.emplace_back(this->createInputService(messenger));
    // Status channels: navigation, now playing and calls -> EventBus/API (nav.*, media.*, phone.*)
    serviceList.emplace_back(std::make_shared<navigationstatus::NavigationStatusService>(ioService_, messenger));
    serviceList.emplace_back(std::make_shared<mediaplaybackstatus::MediaPlaybackStatusService>(ioService_, messenger));
    serviceList.emplace_back(std::make_shared<phonestatus::PhoneStatusService>(ioService_, messenger));
    if (configuration_->getWirelessProjectionEnabled()
        || configuration_->getBluetoothAdapterType() == configuration::BluetoothAdapterType::LOCAL)
    {
        // Tells the phone the Bluetooth address of OpenHU: it pairs over the cable (wireless Android Auto, calls)
        serviceList.emplace_back(this->createBluetoothService(messenger));
    }

    return serviceList;
  }

  IService::Pointer ServiceFactory::createBluetoothService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createBluetoothService()";
    projection::IBluetoothDevice::Pointer bluetoothDevice;
    bluetoothDevice = std::make_shared<projection::BluezBluetoothDevice>(configuration_->getBluetoothAdapterAddress());
    if (!bluetoothDevice->isAvailable()) {
      OPENAUTO_LOG(info) << "[ServiceFactory] no Bluetooth adapter";
      bluetoothDevice = std::make_shared<projection::DummyBluetoothDevice>();
    }

    return std::make_shared<bluetooth::BluetoothService>(ioService_, messenger, std::move(bluetoothDevice));
  }

  IService::Pointer ServiceFactory::createInputService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createInputService()";
    const QSize videoSize = shell::AaBridge::instance().videoSize();
    QRect videoGeometry(QPoint(0, 0), videoSize);
    OPENAUTO_LOG(info) << "[ServiceFactory] Resolution " << videoSize.width() << "x" << videoSize.height();

    // Touches arrive from the shell's AaSurface already mapped to video coordinates
    projection::IInputDevice::Pointer inputDevice(
        std::make_shared<projection::InputDevice>(configuration_, std::move(videoGeometry)));

    return std::make_shared<inputsource::InputSourceService>(ioService_, messenger, std::move(inputDevice));
  }

  void ServiceFactory::createMediaSinkServices(ServiceList &serviceList,
                                               aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createMediaSinkServices()";
    if (configuration_->musicAudioChannelEnabled()) {
      OPENAUTO_LOG(info) << "[ServiceFactory] Media Audio Channel enabled";
      auto mediaAudioOutput =
          configuration_->getAudioOutputBackendType() == configuration::AudioOutputBackendType::RTAUDIO ?
          std::make_shared<projection::RtAudioOutput>(2, 16, 48000) :
          projection::IAudioOutput::Pointer(new projection::QtAudioOutput(2, 16, 48000),
                                            std::bind(&QObject::deleteLater, std::placeholders::_1));

      serviceList.emplace_back(
          std::make_shared<mediasink::MediaAudioService>(ioService_, messenger, std::move(mediaAudioOutput)));
    }

    if (configuration_->guidanceAudioChannelEnabled()) {
      OPENAUTO_LOG(info) << "[ServiceFactory] Guidance Audio Channel enabled";
      auto guidanceAudioOutput =
          configuration_->getAudioOutputBackendType() == configuration::AudioOutputBackendType::RTAUDIO ?
          std::make_shared<projection::RtAudioOutput>(1, 16, 16000) :
          projection::IAudioOutput::Pointer(new projection::QtAudioOutput(1, 16, 16000),
                                            std::bind(&QObject::deleteLater, std::placeholders::_1));

      serviceList.emplace_back(
          std::make_shared<mediasink::GuidanceAudioService>(ioService_, messenger,
                                                            std::move(guidanceAudioOutput)));
    }

    /* TODO: This also causes a problem - suspect not actually enabled yet in AA, or removed due to preference of Bluetooth.
    if (configuration_->telephonyAudioChannelEnabled()) {
      OPENAUTO_LOG(info) << "[ServiceFactory] Telephony Audio Channel enabled";
      auto telephonyAudioOutput =
          configuration_->getAudioOutputBackendType() == configuration::AudioOutputBackendType::RTAUDIO ?
          std::make_shared<projection::RtAudioOutput>(1, 16, 16000) :
          projection::IAudioOutput::Pointer(new projection::QtAudioOutput(1, 16, 16000),
                                            std::bind(&QObject::deleteLater, std::placeholders::_1));

      serviceList.emplace_back(
          std::make_shared<mediasink::TelephonyAudioService>(ioService_, messenger,
                                                             std::move(telephonyAudioOutput)));
    }
*/
    /*
     * No Need to Check for systemAudioChannelEnabled - MUST be enabled by default.
     */

    OPENAUTO_LOG(info) << "[ServiceFactory] System Audio Channel enabled";
    auto systemAudioOutput =
        configuration_->getAudioOutputBackendType() == configuration::AudioOutputBackendType::RTAUDIO ?
        std::make_shared<projection::RtAudioOutput>(1, 16, 16000) :
        projection::IAudioOutput::Pointer(new projection::QtAudioOutput(1, 16, 16000),
                                          std::bind(&QObject::deleteLater, std::placeholders::_1));

    serviceList.emplace_back(
        std::make_shared<mediasink::SystemAudioService>(ioService_, messenger, std::move(systemAudioOutput)));

    projection::IVideoOutput::Pointer videoOutput(new projection::FfmpegVideoOutput(configuration_),
                                                  std::bind(&QObject::deleteLater, std::placeholders::_1));

    OPENAUTO_LOG(info) << "[ServiceFactory] Video Channel enabled";
    serviceList.emplace_back(
        std::make_shared<mediasink::VideoService>(ioService_, messenger, std::move(videoOutput)));
  }

  void ServiceFactory::createMediaSourceServices(f1x::openauto::autoapp::service::ServiceList &serviceList,
                                                 aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createMediaSourceServices()";
    projection::IAudioInput::Pointer audioInput(new projection::QtAudioInput(1, 16, 16000),
                                                std::bind(&QObject::deleteLater, std::placeholders::_1));
    serviceList.emplace_back(std::make_shared<mediasource::MicrophoneMediaSourceService>(ioService_, messenger,
                                                                                         std::move(audioInput)));
  }

  IService::Pointer ServiceFactory::createSensorService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createSensorService()";
    return std::make_shared<sensor::SensorService>(ioService_, messenger);
  }

}



