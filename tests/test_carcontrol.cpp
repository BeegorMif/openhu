// Car controls (openhu#121): vehicle.controls of a plugin <-> the Android Auto car control channel. The layout follows
// what worked with a phone (Fairphone 4, Android Auto 17.9, see aasdk's CarControlProtocol.ut.cpp).
#include <QtTest>
#include <aap_protobuf/service/carcontrol/message/VehiclePropertyId.pb.h>
#include <f1x/openauto/autoapp/Plugins/AndroidAutoProviders.hpp>
#include <f1x/openauto/autoapp/Service/CarControl/CarControlMapping.hpp>

namespace cc = f1x::openauto::autoapp::service::carcontrol;
namespace msg = aap_protobuf::service::carcontrol::message;
namespace plugins = f1x::openauto::autoapp::plugins;

namespace
{

QVariantMap entry(const QString& name, const QString& area, const QVariant& value, double min = 0, double max = 3)
{
    QVariantMap result{{"name", name}, {"value", value}, {"min", min}, {"max", max}};
    if(!area.isEmpty())
    {
        result.insert("area", area);
    }
    return result;
}

// the controls of the spike that showed the climate rail
QVariantMap climate()
{
    QVariantMap driver = entry("temperature", "driver", 21.5, 16, 28);
    driver.insert("step", 0.5);
    QVariantMap passenger = entry("temperature", "passenger", 22.0, 16, 28);
    passenger.insert("step", 0.5);
    return {{"properties", QVariantList{driver, passenger, entry("seatHeating", "driver", 0), entry("seatHeating", "passenger", 2),
                                        entry("steeringWheelHeating", "", 1), entry("ac", "front", true),
                                        entry("hvacPower", "driver", true), entry("hvacPower", "passenger", true),
                                        entry("ignition", "", true)}}};
}

const msg::CarPropertyConfig* config(const aap_protobuf::service::carcontrol::CarControlService& service, int32_t id)
{
    for(const auto& config: service.property_configs())
    {
        if(config.property_id() == id)
        {
            return &config;
        }
    }
    return nullptr;
}

}

class TestCarControl: public QObject
{
    Q_OBJECT

private slots:
    void parse()
    {
        QVariantMap data = climate();
        QVariantList properties = data.value("properties").toList();
        properties.append(entry("warpDrive", "", 1));
        properties.append(entry("defrost", "driver", true));          // a window, not a seat
        properties.append(entry("seatHeating", "driver", 1));         // twice
        properties.append(entry("fanSpeed", "front", 1, 5, 5));       // max not above min
        data.insert("properties", properties);
        const cc::Controls controls = cc::parseControls(data);
        QCOMPARE(int(controls.controls.size()), 9);
        QCOMPARE(controls.ignored.size(), 4);

        const cc::Control& driver = controls.controls[0];
        QCOMPARE(driver.propertyId, int32_t(msg::HVAC_TEMPERATURE_SET));
        QCOMPARE(driver.areaType, msg::CAR_AREA_TYPE_SEAT);
        QCOMPARE(driver.areaBits, uint32_t(msg::SEAT_ROW_1_LEFT));
        QVERIFY(driver.writable && driver.widget);
        QCOMPARE(controls.controls[4].areaType, msg::CAR_AREA_TYPE_GLOBAL);
        QCOMPARE(controls.controls[5].areaBits, uint32_t(msg::SEAT_ROW_1_LEFT | msg::SEAT_ROW_1_RIGHT));
        // dependencies are read only and have no widget
        QVERIFY(!controls.controls[6].writable && !controls.controls[6].widget);

        // right-hand drive
        data.insert("driverSide", "right");
        QCOMPARE(cc::parseControls(data).controls[0].areaBits, uint32_t(msg::SEAT_ROW_1_RIGHT));
    }

    void descriptor()
    {
        const auto service = cc::descriptor(cc::parseControls(climate()));
        // one config per property plus the display units
        QCOMPARE(service.property_configs_size(), 7);

        const auto* temperature = config(service, msg::HVAC_TEMPERATURE_SET);
        QVERIFY(temperature != nullptr);
        QCOMPARE(temperature->value_type(), msg::CAR_PROPERTY_VALUE_TYPE_FLOAT);
        QCOMPARE(temperature->access(), msg::CAR_PROPERTY_ACCESS_READ_WRITE);
        QCOMPARE(temperature->area_type(), msg::CAR_AREA_TYPE_SEAT);
        QCOMPARE(temperature->area_ids_size(), 2);
        QCOMPARE(temperature->area_configs_size(), 2);
        QCOMPARE(temperature->area_configs(0).max_value().float_value(), 28.0f);
        QCOMPARE(temperature->area_configs(0).min_value().float_value(), 16.0f);
        // VHAL tenths: 16-28 °C in 0.5, 61-82 °F in 1
        const QList<int> array(temperature->config_array().begin(), temperature->config_array().end());
        QCOMPARE(array, QList<int>({160, 280, 5, 610, 820, 10}));

        const auto* units = config(service, msg::HVAC_TEMPERATURE_DISPLAY_UNITS);
        QVERIFY(units != nullptr);
        QCOMPARE(units->area_type(), msg::CAR_AREA_TYPE_GLOBAL);
        QVERIFY(units->area_ids(0).has_global());
        QCOMPARE(config(service, msg::HVAC_POWER_ON)->config_array(0), int(msg::HVAC_TEMPERATURE_SET));
        QCOMPARE(config(service, msg::HVAC_POWER_ON)->access(), msg::CAR_PROPERTY_ACCESS_READ);
        QCOMPARE(config(service, msg::HVAC_AC_ON)->area_configs(0).max_value().bool_value(), true);

        // one PRIMARY group, widgets only, sides from the areas
        QCOMPARE(service.controls_size(), 1);
        const auto& group = service.controls(0).control_group();
        QCOMPARE(group.group_type(), msg::CAR_CONTROL_GROUP_TYPE_PRIMARY);
        QCOMPARE(group.controls_size(), 6);
        QCOMPARE(group.controls(0).side_affinity(), msg::CAR_CONTROL_SIDE_AFFINITY_DRIVER);
        QCOMPARE(group.controls(1).side_affinity(), msg::CAR_CONTROL_SIDE_AFFINITY_PASSENGER);
        QCOMPARE(group.controls(4).side_affinity(), msg::CAR_CONTROL_SIDE_AFFINITY_DRIVER);  // steering wheel
        QVERIFY(!group.controls(5).has_side_affinity());                                      // A/C for both
    }

    void values()
    {
        const cc::Controls controls = cc::parseControls(climate());
        const cc::Control& temperature = controls.controls[0];

        // on the phone's grid, otherwise it shows OFF
        auto event = cc::changeEvent(temperature, entry("temperature", "driver", 21.4));
        QCOMPARE(event.status(), msg::CAR_PROPERTY_STATUS_AVAILABLE);
        QCOMPARE(event.value().float_value(), 21.5f);
        QCOMPARE(cc::changeEvent(temperature, entry("temperature", "driver", 40)).value().float_value(), 28.0f);
        QCOMPARE(event.property().area().seats(0), msg::SEAT_ROW_1_LEFT);

        QVariantMap off = entry("temperature", "driver", 21.5);
        off.insert("available", false);
        QCOMPARE(cc::changeEvent(temperature, off).status(), msg::CAR_PROPERTY_STATUS_UNAVAILABLE);
        QVERIFY(!cc::changeEvent(temperature, off).has_value());
        QCOMPARE(cc::changeEvent(temperature, QVariantMap()).status(), msg::CAR_PROPERTY_STATUS_UNAVAILABLE);

        QCOMPARE(cc::changeEvent(controls.controls[3], entry("seatHeating", "passenger", 7)).value().int32_value(), 3);
        QCOMPARE(cc::changeEvent(controls.controls[5], entry("ac", "front", true)).value().bool_value(), true);
        // ignition on = VHAL ON (4)
        QCOMPARE(cc::changeEvent(controls.controls[8], entry("ignition", "", true)).value().int32_value(), 4);
        QCOMPARE(cc::changeEvent(controls.controls[8], entry("ignition", "", false)).value().int32_value(), 2);

        QCOMPARE(cc::unitsEvent(controls)->value().int32_value(), 48);
        QVariantMap fahrenheit = climate();
        fahrenheit.insert("units", "fahrenheit");
        QCOMPARE(cc::unitsEvent(cc::parseControls(fahrenheit))->value().int32_value(), 49);
        QVERIFY(!cc::unitsEvent(cc::Controls()));

        // the entry of a control in vehicle.controls (the steering wheel has no area: global)
        QCOMPARE(cc::entryFor(climate(), controls.controls[3]).value("value").toInt(), 2);
        QCOMPARE(cc::entryFor(climate(), controls.controls[4]).value("value").toInt(), 1);
    }

    void requests()
    {
        const cc::Controls controls = cc::parseControls(climate());
        // as the phone sent it: passenger temperature to 21.0
        msg::SetCarPropertyValueRequest request;
        *request.mutable_property() = cc::property(msg::HVAC_TEMPERATURE_SET, msg::CAR_AREA_TYPE_SEAT,
                                                   msg::SEAT_ROW_1_RIGHT);
        request.mutable_value()->set_float_value(21.0f);
        request.set_request_id("e8c20e00-d3d6-4bd1-94a0-7c16972207ae");
        const cc::Control* control = cc::find(controls, request.property());
        QVERIFY(control != nullptr);
        QCOMPARE(control->area, QString("passenger"));
        const QVariantMap event = cc::setEvent(*control, request, true);
        QCOMPARE(event.value("name").toString(), QString("temperature"));
        QCOMPARE(event.value("area").toString(), QString("passenger"));
        QCOMPARE(event.value("value").toDouble(), 21.0);
        QCOMPARE(event.value("requestId").toString(), QString("e8c20e00-d3d6-4bd1-94a0-7c16972207ae"));
        QCOMPARE(event.value("restricted").toBool(), true);

        // the global steering wheel heating and an area that is not there
        QVERIFY(cc::find(controls, cc::property(msg::HVAC_STEERING_WHEEL_HEAT, msg::CAR_AREA_TYPE_GLOBAL, 0)) != nullptr);
        QVERIFY(cc::find(controls, cc::property(msg::HVAC_TEMPERATURE_SET, msg::CAR_AREA_TYPE_SEAT,
                                                msg::SEAT_ROW_2_LEFT)) == nullptr);
    }

    void sources()
    {
        const QVariantMap state{{"plugins", QVariantList{
            QVariantMap{{"id", "vehicle"}, {"enabled", true}, {"compatible", true},
                        {"androidAuto", QVariantMap{{"services", QStringList{"carControl"}}}}},
            QVariantMap{{"id", "can"}, {"enabled", true}, {"compatible", true}}}}};
        QVERIFY(plugins::androidAutoServiceSourceAllowed(state, "carControl", "plugin:vehicle"));
        QVERIFY(!plugins::androidAutoServiceSourceAllowed(state, "carControl", "plugin:can"));
        QVERIFY(plugins::androidAutoServiceSourceAllowed(state, "carControl", "client:2"));
    }
};

QTEST_GUILESS_MAIN(TestCarControl)
#include "test_carcontrol.moc"
