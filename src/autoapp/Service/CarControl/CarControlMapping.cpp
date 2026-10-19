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

#include <algorithm>
#include <cmath>
#include <aap_protobuf/service/carcontrol/message/VehiclePropertyId.pb.h>
#include <f1x/openauto/autoapp/Service/CarControl/CarControlMapping.hpp>

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

namespace
{

struct Known
{
    const char* name;
    int32_t propertyId;
    ValueKind kind;
    msg::CarAreaType areaType;
    bool widget;
};

// Android Auto 17.9 has rail widgets for these (aasdk VehiclePropertyId); the others are dependencies of the rail
const std::vector<Known>& known()
{
    static const std::vector<Known> list{
        {"temperature", msg::HVAC_TEMPERATURE_SET, ValueKind::Float, msg::CAR_AREA_TYPE_SEAT, true},
        {"seatHeating", msg::HVAC_SEAT_TEMPERATURE, ValueKind::Int, msg::CAR_AREA_TYPE_SEAT, true},
        {"steeringWheelHeating", msg::HVAC_STEERING_WHEEL_HEAT, ValueKind::Int, msg::CAR_AREA_TYPE_GLOBAL, true},
        {"ac", msg::HVAC_AC_ON, ValueKind::Bool, msg::CAR_AREA_TYPE_SEAT, true},
        {"fanSpeed", msg::HVAC_FAN_SPEED, ValueKind::Int, msg::CAR_AREA_TYPE_SEAT, true},
        {"recirculation", msg::HVAC_RECIRC_ON, ValueKind::Bool, msg::CAR_AREA_TYPE_SEAT, true},
        {"dual", msg::HVAC_DUAL_ON, ValueKind::Bool, msg::CAR_AREA_TYPE_SEAT, true},
        {"defrost", msg::HVAC_DEFROSTER, ValueKind::Bool, msg::CAR_AREA_TYPE_WINDOW, true},
        {"auto", msg::HVAC_AUTO_ON, ValueKind::Bool, msg::CAR_AREA_TYPE_SEAT, false},
        {"maxAc", msg::HVAC_MAX_AC_ON, ValueKind::Bool, msg::CAR_AREA_TYPE_SEAT, false},
        // the temperature is shown only while HVAC power is on (when the car has the property)
        {"hvacPower", msg::HVAC_POWER_ON, ValueKind::Bool, msg::CAR_AREA_TYPE_SEAT, false},
        // the controls are available only while the ignition is on (true = VHAL ON 4, false = OFF 2)
        {"ignition", msg::IGNITION_STATE, ValueKind::Int, msg::CAR_AREA_TYPE_GLOBAL, false},
    };
    return list;
}

const Known* knownControl(const QString& name)
{
    for(const auto& entry: known())
    {
        if(name == QLatin1String(entry.name))
        {
            return &entry;
        }
    }
    return nullptr;
}

QString defaultArea(msg::CarAreaType type)
{
    return type == msg::CAR_AREA_TYPE_GLOBAL ? "global" : type == msg::CAR_AREA_TYPE_WINDOW ? "windshield" : "front";
}

// area name -> bits of the area type, nullopt if the name does not fit it
std::optional<uint32_t> areaBitsOf(const QString& area, msg::CarAreaType type, bool driverLeft)
{
    const uint32_t left = msg::SEAT_ROW_1_LEFT;
    const uint32_t right = msg::SEAT_ROW_1_RIGHT;
    if(type == msg::CAR_AREA_TYPE_GLOBAL)
    {
        return area == "global" ? std::optional<uint32_t>(0) : std::nullopt;
    }
    if(type == msg::CAR_AREA_TYPE_WINDOW)
    {
        if(area == "windshield")
        {
            return msg::WINDOW_FRONT_WINDSHIELD;
        }
        if(area == "rearWindow")
        {
            return msg::WINDOW_REAR_WINDSHIELD;
        }
        return std::nullopt;
    }
    if(area == "driver")
    {
        return driverLeft ? left : right;
    }
    if(area == "passenger")
    {
        return driverLeft ? right : left;
    }
    if(area == "front")
    {
        return left | right;
    }
    if(area == "rearLeft")
    {
        return msg::SEAT_ROW_2_LEFT;
    }
    if(area == "rearRight")
    {
        return msg::SEAT_ROW_2_RIGHT;
    }
    if(area == "rear")
    {
        return msg::SEAT_ROW_2_LEFT | msg::SEAT_ROW_2_RIGHT;
    }
    return std::nullopt;
}

QString areaOf(const QVariantMap& entry, const Known& control)
{
    const QString area = entry.value("area").toString();
    return area.isEmpty() ? defaultArea(control.areaType) : area;
}

void setArea(msg::CarAreaId* id, msg::CarAreaType type, uint32_t bits)
{
    if(type == msg::CAR_AREA_TYPE_GLOBAL)
    {
        id->set_global(msg::GLOBAL_AREA);
        return;
    }
    for(uint32_t bit = 1; bit != 0 && bit <= bits; bit <<= 1)
    {
        if(!(bits & bit))
        {
            continue;
        }
        if(type == msg::CAR_AREA_TYPE_WINDOW)
        {
            id->add_windows(static_cast<msg::VehicleAreaWindow>(bit));
        }
        else
        {
            id->add_seats(static_cast<msg::VehicleAreaSeat>(bit));
        }
    }
}

void setValue(msg::CarPropertyValue* value, ValueKind kind, double v)
{
    if(kind == ValueKind::Float)
    {
        value->set_float_value(static_cast<float>(v));
    }
    else if(kind == ValueKind::Bool)
    {
        value->set_bool_value(v != 0);
    }
    else
    {
        value->set_int32_value(static_cast<int32_t>(std::lround(v)));
    }
}

msg::CarPropertyValueType valueType(ValueKind kind)
{
    return kind == ValueKind::Float ? msg::CAR_PROPERTY_VALUE_TYPE_FLOAT
           : kind == ValueKind::Bool ? msg::CAR_PROPERTY_VALUE_TYPE_BOOL : msg::CAR_PROPERTY_VALUE_TYPE_INT32;
}

// the phone builds its temperature list by adding the step to the minimum, as float
float onGrid(const Control& control, double value)
{
    const double clamped = std::min(control.max, std::max(control.min, value));
    const long steps = std::lround((clamped - control.min) / control.step);
    float result = static_cast<float>(control.min);
    for(long i = 0; i < steps; ++i)
    {
        result += static_cast<float>(control.step);
    }
    return result;
}

int tenths(double value)
{
    return static_cast<int>(std::lround(value * 10));
}

}

const QStringList& controlNames()
{
    static const QStringList names = []() {
        QStringList list;
        for(const auto& entry: known())
        {
            list.append(entry.name);
        }
        return list;
    }();
    return names;
}

Controls parseControls(const QVariantMap& data)
{
    Controls result;
    result.fahrenheit = data.value("units").toString() == "fahrenheit";
    const bool driverLeft = data.value("driverSide").toString() != "right";
    for(const auto& item: data.value("properties").toList())
    {
        const QVariantMap entry = item.toMap();
        const QString name = entry.value("name").toString();
        const Known* control = knownControl(name);
        if(control == nullptr)
        {
            result.ignored.append("unknown control " + name);
            continue;
        }
        const QString area = areaOf(entry, *control);
        const auto bits = areaBitsOf(area, control->areaType, driverLeft);
        if(!bits)
        {
            result.ignored.append(name + ": area " + area + " does not fit");
            continue;
        }
        bool duplicate = false;
        for(const auto& existing: result.controls)
        {
            duplicate = duplicate || (existing.propertyId == control->propertyId && existing.areaBits == *bits);
        }
        if(duplicate)
        {
            result.ignored.append(name + ": area " + area + " twice");
            continue;
        }

        Control parsed;
        parsed.name = name;
        parsed.area = area;
        parsed.propertyId = control->propertyId;
        parsed.kind = control->kind;
        parsed.areaType = control->areaType;
        parsed.areaBits = *bits;
        parsed.widget = control->widget;
        parsed.writable = entry.value("writable", control->widget).toBool();
        if(control->kind == ValueKind::Bool)
        {
            parsed.min = 0;
            parsed.max = 1;
        }
        else if(control->propertyId == msg::IGNITION_STATE)
        {
            parsed.min = 0;
            parsed.max = 7;
        }
        else
        {
            parsed.min = entry.value("min", 0).toDouble();
            parsed.max = entry.value("max", control->kind == ValueKind::Float ? 0 : 3).toDouble();
            parsed.step = entry.value("step", control->kind == ValueKind::Float ? 0.5 : 1).toDouble();
            if(parsed.step <= 0 || parsed.max <= parsed.min)
            {
                result.ignored.append(name + ": min, max or step invalid");
                continue;
            }
        }
        result.controls.push_back(parsed);
    }
    return result;
}

msg::CarProperty property(int32_t propertyId, msg::CarAreaType areaType, uint32_t bits)
{
    msg::CarProperty result;
    result.set_property_id(propertyId);
    setArea(result.mutable_area(), areaType, bits);
    return result;
}

uint32_t areaBits(const msg::CarAreaId& area)
{
    uint32_t bits = 0;
    for(const int bit: area.seats())
    {
        bits |= static_cast<uint32_t>(bit);
    }
    for(const int bit: area.windows())
    {
        bits |= static_cast<uint32_t>(bit);
    }
    for(const int bit: area.doors())
    {
        bits |= static_cast<uint32_t>(bit);
    }
    for(const int bit: area.wheels())
    {
        bits |= static_cast<uint32_t>(bit);
    }
    for(const int bit: area.mirrors())
    {
        bits |= static_cast<uint32_t>(bit);
    }
    return bits;
}

aap_protobuf::service::carcontrol::CarControlService descriptor(const Controls& controls)
{
    aap_protobuf::service::carcontrol::CarControlService result;
    const Control* temperature = nullptr;
    for(const auto& control: controls.controls)
    {
        if(control.propertyId == msg::HVAC_TEMPERATURE_SET && temperature == nullptr)
        {
            temperature = &control;
        }
    }

    // one config per property, every area of it in area_ids and area_configs
    std::vector<int32_t> done;
    for(const auto& control: controls.controls)
    {
        if(std::find(done.begin(), done.end(), control.propertyId) != done.end())
        {
            continue;
        }
        done.push_back(control.propertyId);
        auto* config = result.add_property_configs();
        config->set_property_id(control.propertyId);
        config->set_value_type(valueType(control.kind));
        bool writable = false;
        for(const auto& area: controls.controls)
        {
            if(area.propertyId != control.propertyId)
            {
                continue;
            }
            writable = writable || area.writable;
            setArea(config->add_area_ids(), area.areaType, area.areaBits);
            auto* areaConfig = config->add_area_configs();
            setArea(areaConfig->mutable_area(), area.areaType, area.areaBits);
            setValue(areaConfig->mutable_max_value(), area.kind, area.max);
            setValue(areaConfig->mutable_min_value(), area.kind, area.min);
        }
        config->set_access(writable ? msg::CAR_PROPERTY_ACCESS_READ_WRITE : msg::CAR_PROPERTY_ACCESS_READ);
        config->set_area_type(control.areaType);
        if(control.propertyId == msg::HVAC_TEMPERATURE_SET)
        {
            // VHAL: min, max, step in Celsius and Fahrenheit (tenths); without it the rail crashes
            const double minF = std::round(control.min * 1.8 + 32);
            const double maxF = std::round(control.max * 1.8 + 32);
            for(const int value: {tenths(control.min), tenths(control.max), tenths(control.step), tenths(minF),
                                  tenths(maxF), 10})
            {
                config->add_config_array(value);
            }
        }
        else if(control.propertyId == msg::HVAC_POWER_ON && temperature != nullptr)
        {
            // VHAL: the properties that need HVAC power
            config->add_config_array(msg::HVAC_TEMPERATURE_SET);
        }
    }
    if(temperature != nullptr)
    {
        auto* units = result.add_property_configs();
        units->set_property_id(msg::HVAC_TEMPERATURE_DISPLAY_UNITS);
        units->set_value_type(msg::CAR_PROPERTY_VALUE_TYPE_INT32);
        units->set_access(msg::CAR_PROPERTY_ACCESS_READ);
        units->set_area_type(msg::CAR_AREA_TYPE_GLOBAL);
        setArea(units->add_area_ids(), msg::CAR_AREA_TYPE_GLOBAL, 0);
        auto* areaConfig = units->add_area_configs();
        setArea(areaConfig->mutable_area(), msg::CAR_AREA_TYPE_GLOBAL, 0);
        areaConfig->mutable_max_value()->set_int32_value(49);
        areaConfig->mutable_min_value()->set_int32_value(48);
    }

    // the rail shows only the PRIMARY group, split by side
    auto* group = result.add_controls();
    group->set_enabled(true);
    auto* primary = group->mutable_control_group();
    primary->set_group_type(msg::CAR_CONTROL_GROUP_TYPE_PRIMARY);
    for(const auto& control: controls.controls)
    {
        if(!control.widget)
        {
            continue;
        }
        auto* entry = primary->add_controls();
        *entry->mutable_property_control()->mutable_property() = property(control.propertyId, control.areaType,
                                                                          control.areaBits);
        entry->set_enabled(true);
        if(control.area == "driver" || control.areaType == msg::CAR_AREA_TYPE_GLOBAL)
        {
            entry->set_side_affinity(msg::CAR_CONTROL_SIDE_AFFINITY_DRIVER);
        }
        else if(control.area == "passenger")
        {
            entry->set_side_affinity(msg::CAR_CONTROL_SIDE_AFFINITY_PASSENGER);
        }
    }
    return result;
}

msg::CarPropertyChangeEvent changeEvent(const Control& control, const QVariantMap& entry)
{
    msg::CarPropertyChangeEvent event;
    *event.mutable_property() = property(control.propertyId, control.areaType, control.areaBits);
    const QVariant value = entry.value("value");
    if(!entry.value("available", true).toBool() || !value.isValid() || value.isNull())
    {
        event.set_status(msg::CAR_PROPERTY_STATUS_UNAVAILABLE);
        return event;
    }
    event.set_status(msg::CAR_PROPERTY_STATUS_AVAILABLE);
    if(control.propertyId == msg::IGNITION_STATE && value.typeId() == QMetaType::Bool)
    {
        event.mutable_value()->set_int32_value(value.toBool() ? 4 : 2);
    }
    else if(control.kind == ValueKind::Float)
    {
        event.mutable_value()->set_float_value(onGrid(control, value.toDouble()));
    }
    else if(control.kind == ValueKind::Bool)
    {
        event.mutable_value()->set_bool_value(value.toBool());
    }
    else
    {
        const double clamped = std::min(control.max, std::max(control.min, value.toDouble()));
        event.mutable_value()->set_int32_value(static_cast<int32_t>(std::lround(clamped)));
    }
    return event;
}

std::optional<msg::CarPropertyChangeEvent> unitsEvent(const Controls& controls)
{
    for(const auto& control: controls.controls)
    {
        if(control.propertyId == msg::HVAC_TEMPERATURE_SET)
        {
            msg::CarPropertyChangeEvent event;
            *event.mutable_property() = property(msg::HVAC_TEMPERATURE_DISPLAY_UNITS, msg::CAR_AREA_TYPE_GLOBAL, 0);
            event.set_status(msg::CAR_PROPERTY_STATUS_AVAILABLE);
            event.mutable_value()->set_int32_value(controls.fahrenheit ? 49 : 48);
            return event;
        }
    }
    return std::nullopt;
}

const Control* find(const Controls& controls, const msg::CarProperty& property)
{
    const uint32_t bits = property.area().has_global() ? 0 : areaBits(property.area());
    for(const auto& control: controls.controls)
    {
        if(control.propertyId == property.property_id() && control.areaBits == bits)
        {
            return &control;
        }
    }
    return nullptr;
}

QVariantMap entryFor(const QVariantMap& data, const Control& control)
{
    for(const auto& item: data.value("properties").toList())
    {
        const QVariantMap entry = item.toMap();
        const Known* known = knownControl(entry.value("name").toString());
        if(known != nullptr && entry.value("name").toString() == control.name && areaOf(entry, *known) == control.area)
        {
            return entry;
        }
    }
    return QVariantMap();
}

QVariantMap setEvent(const Control& control, const msg::SetCarPropertyValueRequest& request, bool restricted)
{
    QVariant value;
    if(control.kind == ValueKind::Float)
    {
        // the phone's float (21.5f) as the decimal the plugin expects
        value = std::round(static_cast<double>(request.value().float_value()) * 100) / 100;
    }
    else if(control.kind == ValueKind::Bool)
    {
        value = request.value().bool_value();
    }
    else
    {
        value = request.value().int32_value();
    }
    return {{"name", control.name}, {"area", control.area}, {"value", value},
            {"requestId", QString::fromStdString(request.request_id())}, {"restricted", restricted}};
}

}
}
}
}
}
