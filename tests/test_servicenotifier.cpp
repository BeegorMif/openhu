#include <cstddef>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/ServiceNotifier.hpp>

using f1x::openauto::autoapp::core::ServiceNotifier;
using namespace std::chrono_literals;

// systemd's side of the notify socket: a bound datagram socket, at a path or "@name" (abstract)
class FakeNotifySocket
{
public:
    explicit FakeNotifySocket(const QByteArray& address)
        : address_(address)
    {
        fd_ = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, address.constData(), address.size());
        socklen_t length = offsetof(sockaddr_un, sun_path) + address.size();
        if(address.startsWith('@'))
        {
            addr.sun_path[0] = '\0';
        }
        else
        {
            ::unlink(address.constData());
        }
        bound_ = ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), length) == 0;
    }
    ~FakeNotifySocket()
    {
        ::close(fd_);
        if(!address_.startsWith('@'))
        {
            ::unlink(address_.constData());
        }
    }

    bool bound() const { return bound_; }
    QByteArray address() const { return address_; }

    // the next datagram, empty when none arrives within the timeout
    QByteArray receive(std::chrono::milliseconds timeout = 1000ms)
    {
        pollfd pfd{fd_, POLLIN, 0};
        if(::poll(&pfd, 1, int(timeout.count())) != 1)
        {
            return {};
        }
        char buffer[512];
        const ssize_t size = ::recv(fd_, buffer, sizeof(buffer), 0);
        return size > 0 ? QByteArray(buffer, size) : QByteArray();
    }

    // like receive(), but runs the event loop (the ping timer) while waiting
    QByteArray receiveWithEvents(std::chrono::milliseconds timeout = 1000ms)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while(elapsed.elapsed() < timeout.count())
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            const QByteArray datagram = receive(5ms);
            if(!datagram.isEmpty())
            {
                return datagram;
            }
        }
        return {};
    }

private:
    QByteArray address_;
    int fd_ = -1;
    bool bound_ = false;
};

class TestServiceNotifier: public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(dir_.isValid());
    }

    // container or a plain service: no socket, nothing happens
    void disabledWithoutSocket()
    {
        ServiceNotifier notifier(QByteArray(), 10s);
        QVERIFY(!notifier.enabled());
        QVERIFY(!notifier.send("READY=1"));
        notifier.ready();
        notifier.prepareRestart();
    }

    void readyThenPingsEveryHalfWatchdog()
    {
        FakeNotifySocket systemd(dir_.filePath("notify").toLocal8Bit());
        QVERIFY(systemd.bound());
        ServiceNotifier notifier(systemd.address(), 200ms);
        QCOMPARE(notifier.pingInterval(), 100ms);

        notifier.ready();
        QCOMPARE(systemd.receive(), QByteArray("READY=1\nWATCHDOG_USEC=200000"));

        QElapsedTimer elapsed;
        elapsed.start();
        QCOMPARE(systemd.receiveWithEvents(), QByteArray("WATCHDOG=1"));
        QCOMPARE(systemd.receiveWithEvents(), QByteArray("WATCHDOG=1"));
        // two pings, 100 ms apart (coarse timer: 5 %)
        QVERIFY2(elapsed.elapsed() >= 180, qPrintable(QString::number(elapsed.elapsed())));
    }

    // the point of the timer in the GUI thread: while it is blocked, systemd hears nothing
    void blockedThreadMissesPings()
    {
        FakeNotifySocket systemd(dir_.filePath("notify").toLocal8Bit());
        QVERIFY(systemd.bound());
        ServiceNotifier notifier(systemd.address(), 100ms);
        notifier.ready();
        QCOMPARE(systemd.receive(), QByteArray("READY=1\nWATCHDOG_USEC=100000"));

        QThread::msleep(300);
        QCOMPARE(systemd.receive(0ms), QByteArray());
        // running again: pings again
        QCOMPARE(systemd.receiveWithEvents(), QByteArray("WATCHDOG=1"));
    }

    void readyWithoutWatchdog()
    {
        FakeNotifySocket systemd(dir_.filePath("notify").toLocal8Bit());
        QVERIFY(systemd.bound());
        ServiceNotifier notifier(systemd.address(), 0us);
        QCOMPARE(notifier.pingInterval(), 0ms);
        notifier.ready();
        QCOMPARE(systemd.receive(), QByteArray("READY=1"));
        QCOMPARE(systemd.receiveWithEvents(300ms), QByteArray());
    }

    // systemd's default is an abstract socket ("@/org/freedesktop/systemd1/notify/...")
    void abstractSocket()
    {
        const QByteArray name = "@openhu-test-notify-" + QByteArray::number(qint64(::getpid()));
        FakeNotifySocket systemd(name);
        QVERIFY(systemd.bound());
        ServiceNotifier notifier(name, 0us);
        QVERIFY(notifier.send("READY=1"));
        QCOMPARE(systemd.receive(), QByteArray("READY=1"));
    }

    void unsupportedOrMissingSocket()
    {
        QVERIFY(!ServiceNotifier("relative/notify", 0us).send("READY=1"));
        QVERIFY(!ServiceNotifier("vsock:2:1234", 0us).send("READY=1"));
        QVERIFY(!ServiceNotifier("/" + QByteArray(200, 'x'), 0us).send("READY=1"));
        QVERIFY(!ServiceNotifier(dir_.filePath("nobody-listens").toLocal8Bit(), 0us).send("READY=1"));
    }

    // children (plugins) do not inherit the socket; the restart in place gets it back with a longer watchdog
    void environmentAndRestart()
    {
        FakeNotifySocket systemd(dir_.filePath("notify").toLocal8Bit());
        QVERIFY(systemd.bound());
        qputenv("NOTIFY_SOCKET", systemd.address());
        qputenv("WATCHDOG_USEC", "10000000");
        qputenv("WATCHDOG_PID", "1");

        ServiceNotifier notifier;
        QVERIFY(notifier.enabled());
        QCOMPARE(notifier.watchdog(), 10s);
        QCOMPARE(notifier.pingInterval(), 5s);
        QVERIFY(!qEnvironmentVariableIsSet("NOTIFY_SOCKET"));
        QVERIFY(!qEnvironmentVariableIsSet("WATCHDOG_USEC"));
        QVERIFY(!qEnvironmentVariableIsSet("WATCHDOG_PID"));

        notifier.ready();
        QCOMPARE(systemd.receive(), QByteArray("READY=1\nWATCHDOG_USEC=10000000"));

        notifier.prepareRestart();
        QCOMPARE(systemd.receive(), QByteArray("WATCHDOG=1\nWATCHDOG_USEC=60000000"));
        QCOMPARE(qgetenv("NOTIFY_SOCKET"), systemd.address());
        QCOMPARE(qgetenv("WATCHDOG_USEC"), QByteArray("10000000"));

        // the new process: READY=1 sets the configured watchdog time again
        ServiceNotifier restarted;
        restarted.ready();
        QCOMPARE(systemd.receive(), QByteArray("READY=1\nWATCHDOG_USEC=10000000"));
        QVERIFY(!qEnvironmentVariableIsSet("NOTIFY_SOCKET"));
    }

    void invalidWatchdogEnvironment()
    {
        qputenv("NOTIFY_SOCKET", "/run/notify");
        qputenv("WATCHDOG_USEC", "soon");
        ServiceNotifier notifier;
        QVERIFY(notifier.enabled());
        QCOMPARE(notifier.watchdog(), 0us);

        qunsetenv("NOTIFY_SOCKET");
        qputenv("WATCHDOG_USEC", "10000000");
        ServiceNotifier withoutSocket;
        QVERIFY(!withoutSocket.enabled());
        QVERIFY(!qEnvironmentVariableIsSet("WATCHDOG_USEC"));
    }

private:
    QTemporaryDir dir_;
};

QTEST_GUILESS_MAIN(TestServiceNotifier)
#include "test_servicenotifier.moc"
