#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include "English.hpp"
#include <f1x/openauto/autoapp/Wireless/WifiClient.hpp>

using f1x::openauto::autoapp::wireless::NmSettings;
using f1x::openauto::autoapp::wireless::WifiClient;

namespace
{

// Answers like NetworkManager for: wlan0 in "Gar:age" (escaped by nmcli, joined before, autoconnect), a profile of the
// user's own (not listed), "Phone" (never joined, no autoconnect). Every call is logged to $FAKE_NM/calls;
// "connection up" of a new profile fails with a wrong password.
// Profiles are added and changed over D-Bus: FakeNmSettings logs these calls to the same file.
const char* cFakeNmcli = R"(#!/bin/sh
echo "$*" >> "$FAKE_NM/calls"
case "$*" in
    "monitor") exec sleep 600 ;;
    "-t -f CONNECTIVITY general") echo full ;;
    "-t -f DEVICE,TYPE device") if [ -n "$FAKE_NO_WIFI" ]; then echo eth0:ethernet; else printf 'wlan0:wifi\neth0:ethernet\n'; fi ;;
    "-t -f UUID,TYPE,DEVICE,STATE,NAME connection show --active") echo "u1:802-11-wireless:wlan0:activated:openhu-wifi-Gar\:age" ;;
    "-g 802-11-wireless.mode connection show u1") echo infrastructure ;;
    "-t -m multiline -f 802-11-wireless.ssid connection show u1") echo '802-11-wireless.ssid:Gar\:age' ;;
    "-t -m multiline -f IP4.ADDRESS device show wlan0") echo 'IP4.ADDRESS[1]:192.168.77.5/24' ;;
    "-t -f UUID,NAME connection show") printf 'u1:openhu-wifi-Gar\\:age\nu2:Home\nu3:openhu-wifi-Phone\n' ;;
    "-t -m multiline -f 802-11-wireless.ssid,802-11-wireless.hidden,connection.autoconnect-priority,802-11-wireless-security.key-mgmt,connection.autoconnect,connection.interface-name,connection.timestamp connection show u1")
        printf '802-11-wireless.ssid:Gar\\:age\n802-11-wireless.hidden:no\nconnection.autoconnect-priority:1\n802-11-wireless-security.key-mgmt:wpa-psk\nconnection.autoconnect:yes\nconnection.interface-name:\nconnection.timestamp:1791820800\n' ;;
    "-t -m multiline -f 802-11-wireless.ssid,802-11-wireless.hidden,connection.autoconnect-priority,802-11-wireless-security.key-mgmt,connection.autoconnect,connection.interface-name,connection.timestamp connection show u3")
        printf '802-11-wireless.ssid:Phone\n802-11-wireless.hidden:no\nconnection.autoconnect-priority:2\n802-11-wireless-security.key-mgmt:\nconnection.autoconnect:no\nconnection.interface-name:\nconnection.timestamp:0\n' ;;
    "-t -f SSID,SIGNAL,SECURITY device wifi list --rescan yes")
        printf 'Cafe:40:\nWork\\:shop:70:WPA2\nWork\\:shop:55:WPA2\n:30:WPA2\nPhone:20:WPA2\n' ;;
    "--wait 30 connection up u1"|"--wait 30 connection up u3") ;;
    "--wait 30 connection up "*) echo "Error: Connection activation failed: Secrets were required, but not provided"; exit 4 ;;
esac
exit 0
)";

// "setting.property=value" sorted, without the generated UUID
QString describe(const QVariantMap& values)
{
    QStringList parts;
    for(auto value = values.begin(); value != values.end(); ++value)
    {
        if(value.key() != "connection.uuid")
        {
            parts << value.key() + "=" + value.value().toString();
        }
    }
    return parts.join(' ');
}

class FakeNmSettings: public NmSettings
{
public:
    explicit FakeNmSettings(const QString& calls)
        : calls_(calls)
    {
    }

    void add(const Profile& profile, Done done) override
    {
        QVariantMap flat;
        for(auto setting = profile.begin(); setting != profile.end(); ++setting)
        {
            for(auto property = setting->begin(); property != setting->end(); ++property)
            {
                flat[setting.key() + "." + property.key()] = property.value();
            }
        }
        this->log("add " + describe(flat));
        QTimer::singleShot(0, this, [done]() { done({}); });
    }

    void modify(const QString& uuid, const QVariantMap& changes, Done done) override
    {
        this->log("modify " + uuid + " " + describe(changes));
        QTimer::singleShot(0, this, [done]() { done({}); });
    }

    void replace(const QString& uuid, const Profile&, Done done) override
    {
        this->log("replace " + uuid);
        QTimer::singleShot(0, this, [done]() { done({}); });
    }

private:
    void log(const QString& line)
    {
        QFile file(calls_);
        if(file.open(QIODevice::Append))
        {
            file.write((line + "\n").toUtf8());
        }
    }

    QString calls_;
};

}

class TestWifiClient: public QObject
{
    Q_OBJECT

private slots:
    // the messages are checked by their English text
    void initTestCase()
    {
        loadEnglish();
    }

    void init()
    {
        QVERIFY(dir_.isValid());
        QFile::remove(dir_.filePath("calls"));
        QFile script(dir_.filePath("nmcli"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(cFakeNmcli);
        script.close();
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("FAKE_NM", dir_.path().toLocal8Bit());
        qunsetenv("FAKE_NO_WIFI");
    }

    // a device without WiFi (a PC, a VM): known as such, the settings show why WiFi is missing
    void noWifiCard()
    {
        qputenv("FAKE_NO_WIFI", "1");
        FakeNmSettings nm(dir_.filePath("calls"));
        WifiClient wifi(dir_.filePath("nmcli"), nullptr, &nm);
        QSignalSpy devices(&wifi, &WifiClient::devicesChanged);
        QVERIFY(!wifi.ready());
        QTRY_VERIFY(wifi.ready());
        QVERIFY(wifi.devices().isEmpty());
        QVERIFY(!wifi.state().value("available").toBool());
        QCOMPARE(devices.count(), 1);
    }

    void terse()
    {
        QCOMPARE(WifiClient::unescape("Gar\\:age \\\\ x"), QString("Gar:age \\ x"));
        QCOMPARE(WifiClient::splitTerse("Work\\:shop:70:WPA2"), QStringList({"Work:shop", "70", "WPA2"}));
        QCOMPARE(WifiClient::splitTerse(":30:"), QStringList({"", "30", ""}));
    }

    void state()
    {
        FakeNmSettings nm(dir_.filePath("calls"));
        WifiClient wifi(dir_.filePath("nmcli"), nullptr, &nm);
        QTRY_VERIFY(wifi.state().value("available").toBool());
        const QVariantMap client = wifi.state().value("client").toMap();
        QCOMPARE(client.value("ssid").toString(), QString("Gar:age"));
        QCOMPARE(client.value("ip").toString(), QString("192.168.77.5"));
        QVERIFY(wifi.state().value("internet").toBool());

        // only OpenHU's own profiles, highest priority first
        const QVariantList networks = wifi.state().value("networks").toList();
        QCOMPARE(networks.size(), 2);
        QCOMPARE(networks.at(0).toMap().value("ssid").toString(), QString("Phone"));
        QVERIFY(!networks.at(0).toMap().value("secured").toBool());
        QVERIFY(!networks.at(0).toMap().value("joined").toBool());
        QCOMPARE(networks.at(1).toMap().value("ssid").toString(), QString("Gar:age"));
        QVERIFY(networks.at(1).toMap().value("active").toBool());
        QVERIFY(networks.at(1).toMap().value("joined").toBool());
        QCOMPARE(wifi.state().value("devices").toStringList(), QStringList({"wlan0"}));
        // the profiles are as they should be: nothing changed
        QVERIFY(!QString::fromUtf8(read(dir_.filePath("calls"))).contains("modify "));
    }

    // switched off: no profile joins by itself, the active one is left; a card of its own: all profiles bound to it
    void configure()
    {
        FakeNmSettings nm(dir_.filePath("calls"));
        WifiClient wifi(dir_.filePath("nmcli"), nullptr, &nm);
        QTRY_COMPARE(wifi.state().value("networks").toList().size(), 2);
        wifi.configure(false, "");
        QTRY_VERIFY(QString::fromUtf8(read(dir_.filePath("calls"))).contains("connection down u1"));
        QString calls = QString::fromUtf8(read(dir_.filePath("calls")));
        QVERIFY(calls.contains("modify u1 connection.autoconnect=false connection.interface-name=\n"));
        QVERIFY(!wifi.add("Cafe", "", false, {}).isEmpty());

        QFile::remove(dir_.filePath("calls"));
        wifi.configure(true, "wlan1");
        QTRY_VERIFY(QString::fromUtf8(read(dir_.filePath("calls"))).contains("modify u3"));
        calls = QString::fromUtf8(read(dir_.filePath("calls")));
        // only one that worked before joins by itself
        QVERIFY(calls.contains("modify u1 connection.autoconnect=true connection.interface-name=wlan1"));
        QVERIFY(calls.contains("modify u3 connection.autoconnect=false connection.interface-name=wlan1"));
    }

    void scan()
    {
        FakeNmSettings nm(dir_.filePath("calls"));
        WifiClient wifi(dir_.filePath("nmcli"), nullptr, &nm);
        QTRY_COMPARE(wifi.state().value("networks").toList().size(), 2);
        wifi.scan();
        QTRY_VERIFY(!wifi.state().value("found").toList().isEmpty());
        const QVariantList found = wifi.state().value("found").toList();
        // strongest first, one entry per name, hidden ones left out
        QCOMPARE(found.size(), 3);
        QCOMPARE(found.at(0).toMap().value("ssid").toString(), QString("Work:shop"));
        QCOMPARE(found.at(0).toMap().value("signal").toInt(), 70);
        QCOMPARE(found.at(1).toMap().value("ssid").toString(), QString("Cafe"));
        QVERIFY(!found.at(1).toMap().value("secured").toBool());
        QVERIFY(found.at(2).toMap().value("known").toBool());
    }

    void addWithWrongPassword()
    {
        FakeNmSettings nm(dir_.filePath("calls"));
        WifiClient wifi(dir_.filePath("nmcli"), nullptr, &nm);
        QSignalSpy errors(&wifi, &WifiClient::error);
        QTRY_COMPARE(wifi.state().value("networks").toList().size(), 2);
        QVERIFY(!wifi.add("Cafe", "short", false, {}).isEmpty());
        QVERIFY(!wifi.add("", "long-enough", false, {}).isEmpty());

        QString added;
        QCOMPARE(wifi.add("Work:shop", "long-enough", false, [&added](const QString& uuid) { added = uuid; }),
                 QString());
        QTRY_VERIFY(!added.isEmpty());
        wifi.connectTo(added);
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains("Work:shop"));
        const QString calls = QString::fromUtf8(read(dir_.filePath("calls")));
        // only the properties OpenHU sets (an older NetworkManager knows them all)
        QVERIFY(calls.contains("add 802-11-wireless-security.key-mgmt=wpa-psk 802-11-wireless-security.psk=long-enough "
                               "802-11-wireless.ssid=Work:shop connection.autoconnect=false "
                               "connection.autoconnect-priority=0 connection.id=openhu-wifi-Work:shop "
                               "connection.type=802-11-wireless\n"));
        // the new one last, the others keep their order (renumbered from the top)
        QVERIFY(calls.contains("modify u3 connection.autoconnect-priority=3"));
        QVERIFY(calls.contains("modify " + added + " connection.autoconnect-priority=1"));
        // a wrong password is not kept
        QTRY_VERIFY(QString::fromUtf8(read(dir_.filePath("calls"))).contains("connection delete " + added));
        QTRY_VERIFY(!wifi.state().value("busy").toBool());
    }

    void moveUp()
    {
        FakeNmSettings nm(dir_.filePath("calls"));
        WifiClient wifi(dir_.filePath("nmcli"), nullptr, &nm);
        QTRY_COMPARE(wifi.state().value("networks").toList().size(), 2);
        wifi.moveUp("u1");
        QTRY_VERIFY(QString::fromUtf8(read(dir_.filePath("calls"))).contains("modify u3 connection.autoconnect-priority=1"));
        QVERIFY(QString::fromUtf8(read(dir_.filePath("calls"))).contains("modify u1 connection.autoconnect-priority=2"));
    }

private:
    static QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    QTemporaryDir dir_;
};

QTEST_GUILESS_MAIN(TestWifiClient)
#include "test_wificlient.moc"
