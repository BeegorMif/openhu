#include <QFile>
#include <QtTest>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include "TestHome.hpp"

using f1x::openauto::autoapp::configuration::Configuration;

namespace
{

void writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

}

class TestConfiguration: public QObject
{
    Q_OBJECT

private slots:
    void defaultsWithoutFile()
    {
        TestHome home;
        Configuration config;

        QCOMPARE(config.getApiPort(), uint16_t(5520));
        QCOMPARE(config.getApiListenAddress(), std::string("127.0.0.1"));
        QVERIFY(!config.getApiAllowHotspot());
        QVERIFY(!config.getWirelessProjectionEnabled());
        QCOMPARE(config.getWirelessSettings().band, std::string("5"));
        QCOMPARE(config.getCarAudioSettings().pin, std::string("0000"));
        QCOMPARE(config.getVideoDecoder(), std::string("auto"));
        // without name suffix the vehicle id of OpenAuto, so paired phones keep recognizing the head unit
        QCOMPARE(config.getVehicleId(), std::string("2024110822150988"));
        QVERIFY(!QFile::exists(home.path("openhu.ini")));
    }

    void saveAndLoad()
    {
        TestHome home;
        {
            Configuration config;
            config.setApiPort(6000);
            config.setApiListenAddress("0.0.0.0");
            config.setNameSuffix("Golf");
            config.showCursor(true);
            config.setVideoMargins(QRect(0, 0, 40, 20));
            config.setVideoDecoder("h264_v4l2m2m");
            auto wireless = config.getWirelessSettings();
            wireless.ssid = "Car";
            wireless.channel = 44;
            config.setWirelessSettings(wireless);
            auto carAudio = config.getCarAudioSettings();
            carAudio.enabled = true;
            carAudio.device = "AA:BB:CC:DD:EE:FF";
            config.setCarAudioSettings(carAudio);
            config.save();
        }
        QVERIFY(QFile::exists(home.path("openhu.ini")));

        Configuration config;
        QCOMPARE(config.getApiPort(), uint16_t(6000));
        QCOMPARE(config.getApiListenAddress(), std::string("0.0.0.0"));
        QCOMPARE(config.getNameSuffix(), std::string("Golf"));
        QVERIFY(config.showCursor());
        QCOMPARE(config.getVideoMargins(), QRect(0, 0, 40, 20));
        QCOMPARE(config.getVideoDecoder(), std::string("h264_v4l2m2m"));
        QCOMPARE(config.getWirelessSettings().ssid, std::string("Car"));
        QCOMPARE(config.getWirelessSettings().channel, 44);
        QVERIFY(config.getCarAudioSettings().enabled);
        QCOMPARE(config.getCarAudioSettings().device, std::string("AA:BB:CC:DD:EE:FF"));
        // the suffix changes the vehicle id (different head unit for the phone)
        QVERIFY(config.getVehicleId() != "2024110822150988");
        QCOMPARE(config.getVehicleId().size(), size_t(16));
    }

    void resetRestoresDefaults()
    {
        TestHome home;
        Configuration config;
        config.setApiPort(6000);
        config.reset();
        QCOMPARE(config.getApiPort(), uint16_t(5520));
    }

    void readsLegacyOpenAutoIni()
    {
        TestHome home;
        writeFile(home.path("openauto.ini"), "[General]\nShowCursor=true\n[Api]\nPort=6100\n");

        Configuration config;
        QVERIFY(config.showCursor());
        QCOMPARE(config.getApiPort(), uint16_t(6100));

        // saving writes openhu.ini, the old file stays untouched
        config.save();
        QVERIFY(QFile::exists(home.path("openhu.ini")));
        QVERIFY(QFile::exists(home.path("openauto.ini")));
    }

    void openhuIniWinsOverLegacy()
    {
        TestHome home;
        writeFile(home.path("openauto.ini"), "[Api]\nPort=6100\n");
        writeFile(home.path("openhu.ini"), "[Api]\nPort=6200\n");

        Configuration config;
        QCOMPARE(config.getApiPort(), uint16_t(6200));
    }

    void brokenFileGivesDefaults()
    {
        TestHome home;
        writeFile(home.path("openhu.ini"), "[Api]\nPort=not-a-number\n");

        Configuration config;
        QCOMPARE(config.getApiPort(), uint16_t(5520));
    }
};

QTEST_GUILESS_MAIN(TestConfiguration)
#include "test_configuration.moc"
