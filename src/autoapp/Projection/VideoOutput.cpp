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
#include <f1x/openauto/autoapp/Projection/VideoOutput.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace projection {

        VideoOutput::VideoOutput(configuration::IConfiguration::Pointer configuration)
            : configuration_(std::move(configuration)) {

        }

        aap_protobuf::service::media::sink::message::VideoFrameRateType VideoOutput::getVideoFPS() const {
          return configuration_->getVideoFPS();
        }

        aap_protobuf::service::media::sink::message::VideoCodecResolutionType VideoOutput::getVideoResolution() const {
          return shell::AaBridge::instance().resolution();
        }

        size_t VideoOutput::getScreenDPI() const {
          return configuration_->getScreenDPI();
        }

        QRect VideoOutput::getVideoMargins() const {
          // configured margins or computed for the shell's Android Auto area
          return shell::AaBridge::instance().videoMargins();
        }

      }
    }
  }
}
