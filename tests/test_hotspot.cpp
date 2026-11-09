#include <QDBusMetaType>
#include <QtTest>
#include <f1x/openauto/autoapp/Wireless/Hotspot.hpp>

using f1x::openauto::autoapp::configuration::WirelessSettings;
using f1x::openauto::autoapp::wireless::Hotspot;
using f1x::openauto::autoapp::wireless::NmSettings;

class TestHotspot: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        NmSettings::registerTypes();
    }

    // Only the properties OpenHU sets: an older NetworkManager must know all of them (no nmcli defaults such as
    // 802-11-wireless.mac-address-denylist, unknown before NetworkManager 1.48).
    void profileHasOnlyOwnProperties()
    {
        WirelessSettings settings{"", "secret pass", "auto", "DE", "", "", "usb", "always", 0, "10.42.0.1/24",
                                  "10.42.0.10-10.42.0.50", true, ""};
        const auto profile = Hotspot::profileSettings(settings, "OpenHU Ä", "uuid-1", "wlan0", "a", 36);

        QCOMPARE(profile.keys(), (QStringList{"802-11-wireless", "802-11-wireless-security", "connection", "ipv4",
                                              "ipv6"}));
        QCOMPARE(profile["connection"]["id"].toString(), QString("openhu-hotspot"));
        QCOMPARE(profile["connection"]["uuid"].toString(), QString("uuid-1"));
        QCOMPARE(profile["connection"]["interface-name"].toString(), QString("wlan0"));
        QCOMPARE(profile["connection"]["autoconnect"].toBool(), false);
        QCOMPARE(profile["802-11-wireless"].keys(), (QStringList{"band", "channel", "mode", "ssid"}));
        QCOMPARE(profile["802-11-wireless"]["ssid"].toByteArray(), QString("OpenHU Ä").toUtf8());
        QCOMPARE(profile["802-11-wireless"]["channel"].metaType(), QMetaType::fromType<quint32>());
        QCOMPARE(profile["802-11-wireless-security"]["psk"].toString(), QString("secret pass"));
        QCOMPARE(profile["ipv4"]["method"].toString(), QString("shared"));
        QCOMPARE(profile["ipv4"]["shared-dhcp-range"].toString(), QString("10.42.0.10,10.42.0.50"));
        const auto addresses = profile["ipv4"]["address-data"].value<QList<QVariantMap>>();
        QCOMPARE(addresses.size(), 1);
        QCOMPARE(addresses[0]["address"].toString(), QString("10.42.0.1"));
        QCOMPARE(addresses[0]["prefix"].toUInt(), 24u);
        QCOMPARE(profile["ipv6"]["method"].toString(), QString("disabled"));
    }

    void emptyOptionalValuesAreLeftOut()
    {
        WirelessSettings settings{"", "pw", "2.4", "", "", "", "usb", "always", 0, "", "", true, ""};
        const auto profile = Hotspot::profileSettings(settings, "OpenHU", "uuid-2", "wlan1", "bg", 6);
        QCOMPARE(profile["ipv4"].keys(), QStringList{"method"});
        QCOMPARE(profile["802-11-wireless"]["band"].toString(), QString("bg"));
    }

    // the D-Bus types NetworkManager expects
    void marshalsAsNetworkManagerTypes()
    {
        QCOMPARE(QDBusMetaType::typeToSignature(QMetaType::fromType<NmSettings::Profile>()), "a{sa{sv}}");
        QCOMPARE(QDBusMetaType::typeToSignature(QMetaType::fromType<QList<QVariantMap>>()), "aa{sv}");
        QCOMPARE(QDBusMetaType::typeToSignature(QMetaType::fromType<QByteArray>()), "ay");
        QCOMPARE(QDBusMetaType::typeToSignature(QMetaType::fromType<QStringList>()), "as");
    }
};

QTEST_GUILESS_MAIN(TestHotspot)
#include "test_hotspot.moc"
