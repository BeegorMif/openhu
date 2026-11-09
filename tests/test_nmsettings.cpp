#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QUuid>
#include <QtTest>
#include <f1x/openauto/autoapp/Wireless/Hotspot.hpp>
#include <f1x/openauto/autoapp/Wireless/NmSettings.hpp>
#include <f1x/openauto/autoapp/Wireless/WifiClient.hpp>

using f1x::openauto::autoapp::configuration::WirelessSettings;
using f1x::openauto::autoapp::wireless::Hotspot;
using f1x::openauto::autoapp::wireless::NmSettings;
using f1x::openauto::autoapp::wireless::WifiClient;

// Against a real NetworkManager on the system bus; skipped without one (CI). Writes and removes test profiles, so
// only run it against a throwaway NetworkManager, e.g. one in a container (an older version than the nmcli of the
// image shows that no property unknown to it is sent).
class TestNmSettings: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if(qEnvironmentVariableIsEmpty("OPENHU_NM_TEST"))
        {
            QSKIP("set OPENHU_NM_TEST=1 to write test profiles to the NetworkManager on the system bus");
        }
        auto* bus = QDBusConnection::systemBus().interface();
        if(bus == nullptr || !bus->isServiceRegistered("org.freedesktop.NetworkManager"))
        {
            QSKIP("no NetworkManager on the system bus");
        }
        NmSettings::registerTypes();
    }

    void cleanup()
    {
        for(const QString& uuid: created_)
        {
            const QString path = this->pathOf(uuid);
            if(!path.isEmpty())
            {
                QDBusConnection::systemBus().call(QDBusMessage::createMethodCall(
                    "org.freedesktop.NetworkManager", path, "org.freedesktop.NetworkManager.Settings.Connection",
                    "Delete"));
            }
        }
        created_.clear();
    }

    void hotspotProfile()
    {
        NmSettings nm;
        // no DHCP range: ipv4.shared-dhcp-range needs NetworkManager 1.48
        WirelessSettings settings{"", "hotspot-pass", "auto", "DE", "", "", "usb", "always", 0, "10.42.0.1/24", "",
                                  true, ""};
        const QString uuid = this->newUuid();
        QCOMPARE(this->wait([&](NmSettings::Done done) {
                     nm.add(Hotspot::profileSettings(settings, "OpenHU Test", uuid, "wlan0", "a", 36), done);
                 }),
                 QString());
        auto profile = this->settingsOf(uuid);
        QCOMPARE(profile["802-11-wireless"]["mode"].toString(), QString("ap"));
        QCOMPARE(profile["802-11-wireless"]["band"].toString(), QString("a"));
        QCOMPARE(profile["ipv4"]["method"].toString(), QString("shared"));

        // the 2.4 GHz fallback
        QCOMPARE(this->wait([&](NmSettings::Done done) {
                     nm.replace(uuid, Hotspot::profileSettings(settings, "OpenHU Test", uuid, "wlan0", "bg", 6), done);
                 }),
                 QString());
        profile = this->settingsOf(uuid);
        QCOMPARE(profile["802-11-wireless"]["band"].toString(), QString("bg"));
        QCOMPARE(profile["802-11-wireless"]["channel"].toUInt(), 6u);
        QCOMPARE(this->pskOf(uuid), QString("hotspot-pass"));
    }

    // a network of WifiClient: changed later without its password getting lost
    void clientProfileModified()
    {
        NmSettings nm;
        const QString uuid = this->newUuid();
        QCOMPARE(this->wait([&](NmSettings::Done done) {
                     nm.add(WifiClient::profileFor(uuid, "Test Net", "client-pass", true, false, "wlan0"), done);
                 }),
                 QString());
        QCOMPARE(this->wait([&](NmSettings::Done done) {
                     nm.modify(uuid,
                               {{"connection.autoconnect", true},
                                {"connection.autoconnect-priority", qint32(4)},
                                {"connection.interface-name", QVariant()}},
                               done);
                 }),
                 QString());
        const auto profile = this->settingsOf(uuid);
        QCOMPARE(profile["connection"]["id"].toString(), QString("openhu-wifi-Test Net"));
        // GetSettings leaves out default values (autoconnect: true)
        QCOMPARE(profile["connection"].value("autoconnect", true).toBool(), true);
        QCOMPARE(profile["connection"]["autoconnect-priority"].toInt(), 4);
        QVERIFY(!profile["connection"].contains("interface-name"));
        QCOMPARE(profile["802-11-wireless"]["hidden"].toBool(), true);
        QCOMPARE(this->pskOf(uuid), QString("client-pass"));
    }

    void unknownUuid()
    {
        NmSettings nm;
        QVERIFY(!this->wait([&](NmSettings::Done done) {
                         nm.modify(QUuid::createUuid().toString(QUuid::WithoutBraces),
                                   {{"connection.autoconnect", true}}, done);
                     }).isEmpty());
    }

private:
    QString newUuid()
    {
        created_ << QUuid::createUuid().toString(QUuid::WithoutBraces);
        return created_.last();
    }

    QString wait(std::function<void(NmSettings::Done)> call)
    {
        bool finished = false;
        QString result;
        call([&](const QString& error) {
            result = error;
            finished = true;
        });
        if(!QTest::qWaitFor([&]() { return finished; }, 15000))
        {
            return "timeout";
        }
        return result;
    }

    QString pathOf(const QString& uuid)
    {
        QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.NetworkManager",
                                                              "/org/freedesktop/NetworkManager/Settings",
                                                              "org.freedesktop.NetworkManager.Settings",
                                                              "GetConnectionByUuid");
        message << uuid;
        const QDBusReply<QDBusObjectPath> reply = QDBusConnection::systemBus().call(message);
        return reply.isValid() ? reply.value().path() : QString();
    }

    NmSettings::Profile settingsOf(const QString& uuid)
    {
        const QDBusReply<NmSettings::Profile> reply = QDBusConnection::systemBus().call(
            QDBusMessage::createMethodCall("org.freedesktop.NetworkManager", this->pathOf(uuid),
                                           "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings"));
        return reply.value();
    }

    QString pskOf(const QString& uuid)
    {
        QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.NetworkManager", this->pathOf(uuid),
                                                              "org.freedesktop.NetworkManager.Settings.Connection",
                                                              "GetSecrets");
        message << QString("802-11-wireless-security");
        const QDBusReply<NmSettings::Profile> reply = QDBusConnection::systemBus().call(message);
        return reply.value()["802-11-wireless-security"]["psk"].toString();
    }

    QStringList created_;
};

QTEST_GUILESS_MAIN(TestNmSettings)
#include "test_nmsettings.moc"
