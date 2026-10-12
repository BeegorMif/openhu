#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <f1x/openauto/autoapp/Shell/ShellController.hpp>

using f1x::openauto::autoapp::shell::ShellController;

// OpenHU OS is recognised by its os-release: the settings then reboot and power off the device
class TestShellController: public QObject
{
    Q_OBJECT

private slots:
    void osVersion_data()
    {
        QTest::addColumn<QByteArray>("osRelease");
        QTest::addColumn<QString>("version");
        QTest::newRow("OpenHU OS") << QByteArray("PRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\nID=debian\n"
                                                 "IMAGE_ID=openhu-os\nIMAGE_VERSION=2026.10.12.2\n")
                                   << "2026.10.12.2";
        QTest::newRow("quoted") << QByteArray("IMAGE_VERSION=\"2026.10.12.3\"\nIMAGE_ID='openhu-os'\n") << "2026.10.12.3";
        QTest::newRow("other image") << QByteArray("IMAGE_ID=something\nIMAGE_VERSION=1\n") << "";
        QTest::newRow("plain Debian") << QByteArray("ID=debian\nVERSION_ID=\"13\"\n") << "";
    }

    void osVersion()
    {
        QFETCH(QByteArray, osRelease);
        QFETCH(QString, version);
        QTemporaryDir dir;
        QFile file(dir.filePath("os-release"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(osRelease);
        file.close();
        QCOMPARE(ShellController::readOsVersion(file.fileName()), version);
    }

    void missingFile()
    {
        QCOMPARE(ShellController::readOsVersion("/nonexistent/os-release"), QString());
    }
};

QTEST_GUILESS_MAIN(TestShellController)
#include "test_shellcontroller.moc"
