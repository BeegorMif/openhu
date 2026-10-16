// The wait for a USB phone (openhu#80): App arms the USB hub again on every outcome that is not a USB session, and
// the USB watchdog arms it again when a phone sits on the cable without a session.
#include <QtTest>
#include <chrono>
#include <aasdk/TCP/TCPWrapper.hpp>
#include <aasdk/USB/USBWrapper.hpp>
#include <f1x/openauto/autoapp/App.hpp>
#include <f1x/openauto/autoapp/Core/UsbWatchdog.hpp>

using namespace std::chrono_literals;
using f1x::openauto::autoapp::App;
using f1x::openauto::autoapp::core::UsbWatchdog;
namespace service = f1x::openauto::autoapp::service;

namespace
{

// Behaves like aasdk's USBHub towards its caller: start() replaces (rejects) a waiting promise, cancel() rejects it.
class FakeHub: public aasdk::usb::IUSBHub
{
public:
    void start(Promise::Pointer promise) override
    {
        ++starts;
        if(pending)
        {
            pending->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
        }
        pending = std::move(promise);
    }

    void cancel() override
    {
        ++cancels;
        if(pending)
        {
            pending->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
            pending.reset();
        }
    }

    Promise::Pointer take()
    {
        auto promise = std::move(pending);
        pending.reset();
        return promise;
    }

    int starts = 0;
    int cancels = 0;
    Promise::Pointer pending;
};

class FakeEnumerator: public aasdk::usb::IConnectedAccessoriesEnumerator
{
public:
    void enumerate(Promise::Pointer promise) override
    {
        ++enumerations;
        promise->resolve(false);
    }
    void cancel() override {}

    int enumerations = 0;
};

class FakeEntity: public service::IAndroidAutoEntity
{
public:
    void start(service::IAndroidAutoEntityEventHandler&) override { ++starts; }
    void stop() override { ++stops; }
    void pause() override {}
    void resume() override {}

    int starts = 0;
    int stops = 0;
};

class FakeFactory: public service::IAndroidAutoEntityFactory
{
public:
    service::IAndroidAutoEntity::Pointer create(aasdk::usb::IAOAPDevice::Pointer) override
    {
        ++usbSessions;
        return std::make_shared<FakeEntity>();
    }
    service::IAndroidAutoEntity::Pointer create(aasdk::tcp::ITCPEndpoint::Pointer) override
    {
        ++wirelessSessions;
        return std::make_shared<FakeEntity>();
    }

    int usbSessions = 0;
    int wirelessSessions = 0;
};

// One App with fakes; the io_service runs in the test thread (pump), so the counters need no locks.
struct Rig
{
    Rig()
        : usbWrapper(nullptr)
        , hub(std::make_shared<FakeHub>())
        , enumerator(std::make_shared<FakeEnumerator>())
        , app(std::make_shared<App>(io, usbWrapper, tcpWrapper, factory, hub, enumerator, 0))
    {
        // no real USB devices in the tests
        app->setUsbWatchdog([] { return QStringList(); }, 1h, UsbWatchdog());
    }

    ~Rig()
    {
        app->stop();
        pump(50ms);
        // the App ends before the io_service: pending handlers are dropped, not run
        app.reset();
    }

    void pump(std::chrono::milliseconds duration)
    {
        io.restart();
        io.run_for(duration);
    }

    void startWirelessSession()
    {
        app->start(std::make_shared<boost::asio::ip::tcp::socket>(io));
        pump(50ms);
    }

    boost::asio::io_service io;
    aasdk::usb::USBWrapper usbWrapper;
    aasdk::tcp::TCPWrapper tcpWrapper;
    FakeFactory factory;
    std::shared_ptr<FakeHub> hub;
    std::shared_ptr<FakeEnumerator> enumerator;
    std::shared_ptr<App> app;
};

}

class TestUsbWait: public QObject
{
    Q_OBJECT

private slots:
    // --- core::UsbWatchdog ---

    void watchdogWaitsForTimeout()
    {
        UsbWatchdog watchdog(10s, 3);
        const auto t0 = UsbWatchdog::Clock::now();
        const QStringList phone{"18d1:2d00@3-5"};
        QCOMPARE(watchdog.update(phone, false, t0), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, false, t0 + 9s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, false, t0 + 10s), UsbWatchdog::Action::Rearm);
        QCOMPARE(watchdog.attempts(), 1);
        // the time starts again with the re-arm
        QCOMPARE(watchdog.update(phone, false, t0 + 15s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, false, t0 + 20s), UsbWatchdog::Action::Rearm);
    }

    void watchdogGivesUpOnce()
    {
        UsbWatchdog watchdog(10s, 2);
        const auto t0 = UsbWatchdog::Clock::now();
        const QStringList phone{"18d1:4ee1@3-4"};
        watchdog.update(phone, false, t0);
        QCOMPARE(watchdog.update(phone, false, t0 + 10s), UsbWatchdog::Action::Rearm);
        QCOMPARE(watchdog.update(phone, false, t0 + 20s), UsbWatchdog::Action::Rearm);
        QCOMPARE(watchdog.update(phone, false, t0 + 30s), UsbWatchdog::Action::GiveUp);
        QCOMPARE(watchdog.update(phone, false, t0 + 40s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, false, t0 + 400s), UsbWatchdog::Action::None);
        // unplugged and plugged in again: new attempts
        QCOMPARE(watchdog.update({}, false, t0 + 401s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, false, t0 + 402s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, false, t0 + 412s), UsbWatchdog::Action::Rearm);
    }

    void watchdogDeviceChangeRestartsTime()
    {
        // MTP device switches to accessory mode (another id): the hub gets its chance first, attempts stay
        UsbWatchdog watchdog(10s, 3);
        const auto t0 = UsbWatchdog::Clock::now();
        watchdog.update({"18d1:4ee1@3-4"}, false, t0);
        QCOMPARE(watchdog.update({"18d1:4ee1@3-4"}, false, t0 + 10s), UsbWatchdog::Action::Rearm);
        QCOMPARE(watchdog.update({"18d1:2d00@3-5"}, false, t0 + 12s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update({"18d1:2d00@3-5"}, false, t0 + 21s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update({"18d1:2d00@3-5"}, false, t0 + 22s), UsbWatchdog::Action::Rearm);
        QCOMPARE(watchdog.attempts(), 2);
    }

    void watchdogQuietDuringSession()
    {
        UsbWatchdog watchdog(10s, 3);
        const auto t0 = UsbWatchdog::Clock::now();
        const QStringList phone{"18d1:2d00@3-5"};
        watchdog.update(phone, false, t0);
        QCOMPARE(watchdog.update(phone, false, t0 + 10s), UsbWatchdog::Action::Rearm);
        QCOMPARE(watchdog.update(phone, true, t0 + 11s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, true, t0 + 100s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.attempts(), 0);
        // session over, phone still there: counts from now
        QCOMPARE(watchdog.update(phone, false, t0 + 101s), UsbWatchdog::Action::None);
        QCOMPARE(watchdog.update(phone, false, t0 + 111s), UsbWatchdog::Action::Rearm);
    }

    // --- App ---

    void startupArmsOnlyTheHub()
    {
        // two request chains to the same phone at once (hub registration + enumerator) were part of openhu#80
        Rig rig;
        rig.app->waitForUSBDevice();
        rig.pump(50ms);
        QCOMPARE(rig.hub->starts, 1);
        QVERIFY(rig.hub->pending != nullptr);
        QCOMPARE(rig.enumerator->enumerations, 0);
    }

    void hubErrorArmsAgain()
    {
        Rig rig;
        rig.app->setUsbRetryDelay(20ms);
        rig.app->waitForUSBDevice();
        rig.pump(50ms);
        rig.hub->take()->reject(aasdk::error::Error(aasdk::error::ErrorCode::USB_LIST_DEVICES));
        rig.pump(150ms);
        QCOMPARE(rig.hub->starts, 2);
        QCOMPARE(rig.hub->cancels, 1);
        QVERIFY(rig.hub->pending != nullptr);
    }

    void replacedWaitDoesNotArmAgain()
    {
        Rig rig;
        rig.app->setUsbRetryDelay(20ms);
        rig.app->waitForUSBDevice();
        rig.pump(50ms);
        // cancelled (stop, replaced): no new wait from the error handler
        rig.hub->cancel();
        rig.pump(150ms);
        QCOMPARE(rig.hub->starts, 1);
    }

    void deviceDuringWirelessSessionArmsAgain()
    {
        // Wireless.Priority = wifi: the phone on the cable is not used, but the hub must keep waiting
        Rig rig;
        rig.app->setPreferUsb(false);
        rig.app->waitForUSBDevice();
        rig.pump(50ms);
        rig.startWirelessSession();
        QCOMPARE(rig.factory.wirelessSessions, 1);

        rig.hub->take()->resolve(aasdk::usb::DeviceHandle());
        rig.pump(50ms);
        QCOMPARE(rig.factory.usbSessions, 0);
        QCOMPARE(rig.hub->starts, 2);
        QVERIFY(rig.hub->pending != nullptr);
    }

    void watchdogArmsAgainWhenAccessoryWaits()
    {
        // openhu#80: the phone switched to accessory mode, but the accessory never reached the App
        Rig rig;
        rig.app->setUsbWatchdog([] { return QStringList{"18d1:2d00@3-5"}; }, 10ms, UsbWatchdog(100ms, 2));
        rig.app->waitForUSBDevice();
        rig.pump(700ms);
        // two re-arms with a new hotplug registration (cancel + start), then it gives up
        QCOMPARE(rig.hub->cancels, 2);
        QCOMPARE(rig.hub->starts, 3);
        QVERIFY(rig.hub->pending != nullptr);
    }

    void watchdogQuietWhileSessionRuns()
    {
        Rig rig;
        rig.app->setPreferUsb(false);
        rig.app->setUsbWatchdog([] { return QStringList{"18d1:2d00@3-5"}; }, 10ms, UsbWatchdog(100ms, 2));
        rig.app->waitForUSBDevice();
        rig.pump(20ms);
        rig.startWirelessSession();
        rig.pump(500ms);
        QCOMPARE(rig.hub->cancels, 0);
        QCOMPARE(rig.hub->starts, 1);
    }
};

QTEST_GUILESS_MAIN(TestUsbWait)
#include "test_usbwait.moc"
