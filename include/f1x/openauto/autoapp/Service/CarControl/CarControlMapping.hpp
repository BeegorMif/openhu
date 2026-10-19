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

#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <aap_protobuf/service/carcontrol/CarControlService.pb.h>
#include <aap_protobuf/service/carcontrol/message/CarProperty.pb.h>
#include <aap_protobuf/service/carcontrol/message/CarPropertyChangeEvent.pb.h>
#include <aap_protobuf/service/carcontrol/message/SetCarPropertyValueRequest.pb.h>

// Car controls from a plugin (vehicle.controls / vehicle.control.set, docs/api.md) <-> the Android Auto car control
// channel (openhu#121, aasdk#40). Pure functions, no I/O: the CarControlService sends and receives, these translate.
// The phone shows them in the climate rail of its "hero" layout, which needs a video resolution above 720p.

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{
namespace carcontrol
{

namespace msg = aap_protobuf::service::carcontrol::message;

enum class ValueKind { Int, Bool, Float };

// One control of the plugin, e.g. {"name": "temperature", "area": "driver", "min": 16, "max": 28, "step": 0.5}
struct Control
{
    QString name;      // temperature, seatHeating, ...
    QString area;      // driver, passenger, front, global, ...
    int32_t propertyId = 0;
    ValueKind kind = ValueKind::Int;
    msg::CarAreaType areaType = msg::CAR_AREA_TYPE_GLOBAL;
    uint32_t areaBits = 0;
    double min = 0;
    double max = 0;
    double step = 1;
    bool writable = true;
    bool widget = false;   // has a widget in the rail (otherwise only a dependency, e.g. hvacPower)
};

// What vehicle.controls describes; the structure is fixed for a connection, values change
struct Controls
{
    std::vector<Control> controls;
    bool fahrenheit = false;
    QStringList ignored;  // entries that were left out, for the log
};

// the names a plugin can use (docs/api.md)
const QStringList& controlNames();

// vehicle.controls -> controls; unknown names, areas that do not fit the control and duplicates are left out
Controls parseControls(const QVariantMap& data);

// the descriptor for the service discovery (Service field 15): property configs and one PRIMARY control group;
// adds HVAC_TEMPERATURE_DISPLAY_UNITS when there is a temperature
aap_protobuf::service::carcontrol::CarControlService descriptor(const Controls& controls);

msg::CarProperty property(int32_t propertyId, msg::CarAreaType areaType, uint32_t areaBits);
// the area bits of a CarAreaId (OR of its list), 0 for the global area
uint32_t areaBits(const msg::CarAreaId& area);

// value of one control from vehicle.controls (the entry's "value"/"available") as a change event; the temperature
// is put on the phone's grid (min + n * step), otherwise the phone shows OFF
msg::CarPropertyChangeEvent changeEvent(const Control& control, const QVariantMap& entry);
// the display units property (48 Celsius, 49 Fahrenheit), when descriptor() added it
std::optional<msg::CarPropertyChangeEvent> unitsEvent(const Controls& controls);

// the control a request of the phone is about, nullptr if unknown
const Control* find(const Controls& controls, const msg::CarProperty& property);
// the entry of vehicle.controls for a control (empty if missing)
QVariantMap entryFor(const QVariantMap& data, const Control& control);

// vehicle.control.set for the plugin: {name, area, value, requestId, restricted}
QVariantMap setEvent(const Control& control, const msg::SetCarPropertyValueRequest& request, bool restricted);

}
}
}
}
}
