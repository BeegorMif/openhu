#include <QMouseEvent>
#include <QtTest>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/autoapp/Projection/IInputDeviceEventHandler.hpp>
#include <f1x/openauto/autoapp/Projection/InputDevice.hpp>
#include "TestHome.hpp"

using f1x::openauto::autoapp::configuration::Configuration;
using f1x::openauto::autoapp::projection::ButtonEvent;
using f1x::openauto::autoapp::projection::IInputDeviceEventHandler;
using f1x::openauto::autoapp::projection::InputDevice;
using f1x::openauto::autoapp::projection::TouchEvent;
using PointerAction = aap_protobuf::service::inputsource::message::PointerAction;

namespace
{

class Recorder: public IInputDeviceEventHandler
{
public:
    void onButtonEvent(const ButtonEvent&) override {}
    void onTouchEvent(const TouchEvent& event) override { touches.push_back(event); }

    std::vector<TouchEvent> touches;
};

}

class TestInputDevice: public QObject
{
    Q_OBJECT

private slots:
    // Shell area 1176x748 for a 1280x720 video: the phone draws into the content rect 1132x720 (margins 148x0)
    // and expects touch coordinates relative to that content, it adds the margins itself.
    void touchIsRelativeToTheContent_data()
    {
        QTest::addColumn<QPointF>("surfacePos");
        QTest::addColumn<int>("x");
        QTest::addColumn<int>("y");
        QTest::newRow("top left") << QPointF(0, 0) << 0 << 0;
        QTest::newRow("bottom right") << QPointF(1176, 748) << 1131 << 719;
        QTest::newRow("center") << QPointF(588, 374) << 565 << 359;
        // the zoom button of Google Maps on the VM display (1280x800, shell offset 104/52)
        QTest::newRow("zoom button") << QPointF(110, 638) << 105 << 613;
    }

    void touchIsRelativeToTheContent()
    {
        QFETCH(QPointF, surfacePos);
        QFETCH(int, x);
        QFETCH(int, y);

        TestHome home;
        auto configuration = std::make_shared<Configuration>();
        configuration->setTouchscreenEnabled(true);
        InputDevice device(configuration, QRect(0, 0, 1280, 720));
        Recorder recorder;
        device.start(recorder);

        QMouseEvent press(QEvent::MouseButtonPress, surfacePos, surfacePos, Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        device.handleSurfaceEvent(&press, QSizeF(1176, 748), QRect(74, 0, 1132, 720));
        device.stop();

        QCOMPARE(recorder.touches.size(), size_t(1));
        const TouchEvent& touch = recorder.touches.front();
        QCOMPARE(touch.type, PointerAction::ACTION_DOWN);
        QCOMPARE(touch.pointers.size(), size_t(1));
        QCOMPARE(int(touch.pointers.front().x), x);
        QCOMPARE(int(touch.pointers.front().y), y);
        // the phone was told about the whole frame
        QCOMPARE(device.getTouchscreenGeometry(), QRect(0, 0, 1280, 720));
    }
};

QTEST_MAIN(TestInputDevice)
#include "test_inputdevice.moc"
