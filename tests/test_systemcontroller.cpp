#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/System/SystemController.hpp>

using f1x::openauto::autoapp::core::EventBus;
using f1x::openauto::autoapp::system::SystemController;

namespace
{

// Stand-ins for the OpenHU OS helpers (run with /bin/sh instead of pkexec), state files in $FAKE_RUN
const char* cFakeUpdate = R"(
case "$1" in
    check) echo '{"current": "1", "newest": "2", "available": true}' ;;
    start) printf '{"state": "installing", "phase": "root", "percent": 42, "from": "1", "version": "%s"}' "$3" \
               > "$FAKE_RUN/update.json"
           echo "$*" > "$FAKE_RUN/start-args"
           echo '{"started": true}' ;;
    cancel) echo '{"state": "cancelled", "from": "1", "version": "2"}' > "$FAKE_RUN/update.json"
            echo '{"cancelled": true}' ;;
esac
)";

const char* cFakeSsh = R"(
state() { printf '{"enabled": %s, "running": %s, "password": %s, "keys": false}\n' "$1" "$1" "$2" | tee "$FAKE_RUN/ssh.json"; }
case "$1" in
    status) cat "$FAKE_RUN/ssh.json" 2>/dev/null || state false false ;;
    password) read -r pw; printf '%s' "$pw" > "$FAKE_RUN/password"; state false true ;;
    enable) [ -f "$FAKE_RUN/password" ] || { echo '{"error": "set a password first"}'; exit 1; }; state true true ;;
    disable) state false true ;;
esac
)";

const char* cFakeTime = R"(
zone=$(cat "$FAKE_RUN/zone" 2>/dev/null || echo Europe/Berlin)
ntp=$(cat "$FAKE_RUN/ntp" 2>/dev/null || echo true)
case "$1" in
    zone) [ -f "/usr/share/zoneinfo/$2" ] || { echo '{"error": "unknown time zone"}'; exit 1; }
          zone=$2; echo "$zone" > "$FAKE_RUN/zone" ;;
    ntp) [ "$2" = on ] && ntp=true || ntp=false; echo $ntp > "$FAKE_RUN/ntp" ;;
    set) [ $ntp = false ] || { echo '{"error": "switch the time from the internet off first"}'; exit 1; }
         echo "$2" > "$FAKE_RUN/set-time" ;;
esac
printf '{"zone": "%s", "ntp": %s, "synchronized": false, "time": 0}\n' "$zone" "$ntp"
)";

}

class TestSystemController: public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(helpers_.isValid() && run_.isValid());
        write(helpers_.filePath("openhu-update"), cFakeUpdate);
        write(helpers_.filePath("openhu-ssh"), cFakeSsh);
        write(helpers_.filePath("openhu-time"), cFakeTime);
        QFile::remove(run_.filePath("update.json"));
        QFile::remove(run_.filePath("usb-update.json"));
        QFile::remove(run_.filePath("start-args"));
        QFile::remove(run_.filePath("ssh.json"));
        QFile::remove(run_.filePath("password"));
        for(const char* name: {"zone", "ntp", "set-time"})
        {
            QFile::remove(run_.filePath(name));
        }
        qputenv("FAKE_RUN", run_.path().toLocal8Bit());
        EventBus::instance().publish("projection.state", {{"connected", false}});
    }

    // Docker or a .deb elsewhere: nothing runs, nothing to install
    void notOnOpenHuOs()
    {
        SystemController system(QString(), nullptr, "/bin/sh", helpers_.path(), run_.path());
        QVERIFY(!system.available());
        QCOMPARE(system.update().value("state").toString(), QString("idle"));
        QVERIFY(!system.installUpdate().isEmpty());
        QVERIFY(!system.setSshPassword("long-enough").isEmpty());
        QVERIFY(!system.setTime(1791820800).isEmpty());
        QVERIFY(system.time().isEmpty());
    }

    void bootStatus()
    {
        write(run_.filePath("boot-status.json"), R"({"state": "rollback", "running": "1", "failed": "2"})");
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        QCOMPARE(system.bootStatus().value("state").toString(), QString("rollback"));
        QCOMPARE(system.bootStatus().value("failed").toString(), QString("2"));
        QFile::remove(run_.filePath("boot-status.json"));
    }

    // the first boot after an update: OpenHU starts before the boot check marks the version good
    void bootStatusTrialBecomesUpdated()
    {
        write(run_.filePath("boot-status.json"), R"({"state": "trial", "running": "2", "attempt": 1, "tries": 3})");
        SystemController system("2", nullptr, "/bin/sh", helpers_.path(), run_.path());
        QSignalSpy changed(&system, &SystemController::bootStatusChanged);
        QCOMPARE(system.bootStatus().value("state").toString(), QString("trial"));
        write(run_.filePath("boot-status.json"), R"({"state": "updated", "running": "2", "attempt": 1, "tries": 3})");
        QTRY_COMPARE_WITH_TIMEOUT(system.bootStatus().value("state").toString(), QString("updated"), 5000);
        QCOMPARE(changed.count(), 1);
        QFile::remove(run_.filePath("boot-status.json"));
    }

    void checkInstallAndProgress()
    {
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        system.checkForUpdate();
        QTRY_COMPARE(system.update().value("newest").toString(), QString("2"));
        QVERIFY(system.update().value("available").toBool());
        QVERIFY(!system.update().value("busy").toBool());

        QCOMPARE(system.installUpdate(), QString());
        QTRY_COMPARE(system.update().value("percent").toInt(), 42);
        QCOMPARE(system.update().value("state").toString(), QString("installing"));
        QCOMPARE(read(run_.filePath("start-args")).trimmed(), QByteArray("start --version 2 --json"));
        QVERIFY(!system.installUpdate().isEmpty());  // already running

        // the background update finishes: the progress timer picks it up
        write(run_.filePath("update.json"), R"({"state": "done", "percent": 100, "from": "1", "version": "2"})");
        QTRY_COMPARE(system.update().value("state").toString(), QString("done"));
        QVERIFY(!system.update().contains("phase"));
    }

    void cancel()
    {
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        QCOMPARE(system.installUpdate(), QString());
        QTRY_COMPARE(system.update().value("state").toString(), QString("installing"));
        system.cancelUpdate();
        QTRY_COMPARE(system.update().value("state").toString(), QString("cancelled"));
    }

    // an update reboots the device: not while a phone is projecting
    void blockedDuringAndroidAuto()
    {
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        EventBus::instance().publish("projection.state", {{"connected", true}});
        QVERIFY(system.update().value("blocked").toBool());
        QVERIFY(!system.installUpdate().isEmpty());
        EventBus::instance().publish("projection.state", {{"connected", false}});
        QVERIFY(!system.update().value("blocked").toBool());
    }

    // a stick plugged in before OpenHU started: offered without a toast
    void usbUpdateAtStart()
    {
        replace(run_.filePath("usb-update.json"), R"({"current": "1", "version": "3", "newer": true, "path": "/run/media/sda1"})");
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        QSignalSpy found(&system, &SystemController::usbUpdateFound);
        QCOMPARE(system.update().value("usb").toMap().value("version").toString(), QString("3"));
        QCOMPARE(system.update().value("offer").toMap().value("source").toString(), QString("usb"));
        QCOMPARE(found.count(), 0);
    }

    // openhu-update scan writes usb-update.json when a stick comes or goes
    void usbUpdatePluggedIn()
    {
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        QSignalSpy found(&system, &SystemController::usbUpdateFound);
        QVERIFY(!system.update().contains("offer"));

        // an older version on the stick: shown, not offered
        replace(run_.filePath("usb-update.json"), R"({"current": "1", "version": "0.9", "newer": false, "path": "/run/media/sda1"})");
        QTRY_VERIFY(system.update().contains("usb"));
        QVERIFY(!system.update().contains("offer"));
        QCOMPARE(found.count(), 0);

        replace(run_.filePath("usb-update.json"), R"({"current": "1", "version": "3", "newer": true, "path": "/run/media/sdb1"})");
        QTRY_COMPARE(found.count(), 1);
        QCOMPARE(found.first().first().toString(), QString("3"));

        // newer than the version online: the stick's is installed
        system.checkForUpdate();
        QTRY_VERIFY(system.update().value("checked").toBool());
        QCOMPARE(system.update().value("offer").toMap().value("version").toString(), QString("3"));
        QCOMPARE(system.installUpdate(), QString());
        QTRY_COMPARE(read(run_.filePath("start-args")).trimmed(), QByteArray("start --version 3 --json"));
        system.cancelUpdate();
        QTRY_COMPARE(system.update().value("state").toString(), QString("cancelled"));

        // pulled out: the online version is offered again
        QFile::remove(run_.filePath("usb-update.json"));
        QTRY_VERIFY(!system.update().contains("usb"));
        QCOMPARE(system.update().value("offer").toMap().value("version").toString(), QString("2"));
    }

    void ssh()
    {
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        QSignalSpy errors(&system, &SystemController::sshError);
        QTRY_VERIFY(system.ssh().contains("enabled"));
        QVERIFY(!system.ssh().value("password").toBool());

        // without a password the helper refuses, the switch stays off
        system.setSshEnabled(true);
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(errors.first().first().toString(), QString("set a password first"));
        QTRY_VERIFY(!system.ssh().value("busy").toBool());
        QVERIFY(!system.ssh().value("enabled").toBool());

        QVERIFY(!system.setSshPassword("short").isEmpty());
        QVERIFY(!system.setSshPassword("long\nenough").isEmpty());
        QCOMPARE(system.setSshPassword("long-enough"), QString());
        QTRY_VERIFY(system.ssh().value("password").toBool());
        QCOMPARE(read(run_.filePath("password")), QByteArray("long-enough"));  // through stdin

        system.setSshEnabled(true);
        QTRY_VERIFY(system.ssh().value("enabled").toBool());
        QVERIFY(system.ssh().value("addresses").canConvert<QStringList>());
    }

    void timeZones()
    {
        const QStringList zones = SystemController::timeZones();
        QVERIFY(zones.contains("Europe/Berlin"));
        QVERIFY(zones.contains("America/Argentina/Salta"));
        QCOMPARE(zones.last(), QString("UTC"));
        QVERIFY(!zones.contains("Etc/GMT+1"));
        QVERIFY(!zones.contains("CET"));
    }

    void timeZoneAndClock()
    {
        SystemController system("1", nullptr, "/bin/sh", helpers_.path(), run_.path());
        QSignalSpy errors(&system, &SystemController::timeError);
        QTRY_COMPARE(system.time().value("zone").toString(), QString("Europe/Berlin"));
        QVERIFY(system.time().value("ntp").toBool());
        // Berlin is UTC+1 or UTC+2
        QVERIFY(system.time().value("offset").toInt() == 3600 || system.time().value("offset").toInt() == 7200);

        system.setTimeZone("Asia/Tokyo");
        QTRY_COMPARE(system.time().value("zone").toString(), QString("Asia/Tokyo"));
        QCOMPARE(system.time().value("offset").toInt(), 9 * 3600);
        QVERIFY(!system.time().value("busy").toBool());

        // refused by the helper: a toast, the zone stays
        system.setTimeZone("Nowhere/Atlantis");
        QTRY_COMPARE(errors.count(), 1);
        QTRY_VERIFY(!system.time().value("busy").toBool());
        QCOMPARE(system.time().value("zone").toString(), QString("Asia/Tokyo"));

        // by hand only without the time from the internet
        QVERIFY(!system.setTime(1791820800).isEmpty());
        system.setNtp(false);
        QTRY_VERIFY(!system.time().value("ntp").toBool());
        QCOMPARE(system.setTime(1791820800), QString());
        QTRY_COMPARE(read(run_.filePath("set-time")).trimmed(), QByteArray("1791820800"));
    }

private:
    static void write(const QString& path, const QByteArray& content)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    // like openhu-update: a new file renamed over the old one (the directory watcher sees it)
    static void replace(const QString& path, const QByteArray& content)
    {
        write(path + ".new", content);
        QFile::remove(path);
        QVERIFY(QFile::rename(path + ".new", path));
    }

    static QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    QTemporaryDir helpers_;
    QTemporaryDir run_;
};

QTEST_GUILESS_MAIN(TestSystemController)
#include "test_systemcontroller.moc"
