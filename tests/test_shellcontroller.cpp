#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <f1x/openauto/autoapp/Shell/ProjectionPage.hpp>
#include <f1x/openauto/autoapp/Shell/ShellController.hpp>

using f1x::openauto::autoapp::shell::ProjectionPage;
using f1x::openauto::autoapp::shell::ShellController;

// OpenHU OS is recognised by its os-release: the settings then reboot and power off the device. Which page the shell
// shows when Android Auto starts and stops (#87).
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

    void projectionTakesTheScreen()
    {
        ProjectionPage pages;
        QCOMPARE(pages.videoChanged("home", true), QString("aa"));
        QCOMPARE(pages.videoChanged("aa", false), QString("home"));
        QCOMPARE(pages.videoChanged("settings", true), QString("aa"));
        QCOMPARE(pages.videoChanged("settings", false), QString("settings"));
        QVERIFY(!pages.waiting());
    }

    // Reverse gear, camera page; the phone connects: the camera stays, reverse ends: Android Auto
    void cameraPageKeepsTheScreen()
    {
        ProjectionPage pages;
        QCOMPARE(pages.videoChanged("plugin:org.openhu.camera", true), QString("plugin:org.openhu.camera"));
        QVERIFY(pages.waiting());
        // the camera page again (camera.show while shown): nothing changes
        QCOMPARE(pages.navigate("plugin:org.openhu.camera", "plugin:org.openhu.camera"),
                 QString("plugin:org.openhu.camera"));
        QVERIFY(pages.waiting());
        // the camera returns to its previous page
        QCOMPARE(pages.navigate("plugin:org.openhu.camera", "home"), QString("aa"));
        QVERIFY(!pages.waiting());
        // later navigations are not touched
        QCOMPARE(pages.navigate("aa", "plugin:org.openhu.camera"), QString("plugin:org.openhu.camera"));
        QCOMPARE(pages.navigate("plugin:org.openhu.camera", "aa"), QString("aa"));
    }

    // Android Auto restarts its stream while the camera page replaced it: the camera stays, then Android Auto
    void streamRestartUnderTheCamera()
    {
        ProjectionPage pages;
        QCOMPARE(pages.videoChanged("aa", true), QString("aa"));
        QCOMPARE(pages.videoChanged("plugin:org.openhu.camera", false), QString("plugin:org.openhu.camera"));
        QCOMPARE(pages.videoChanged("plugin:org.openhu.camera", true), QString("plugin:org.openhu.camera"));
        QCOMPARE(pages.navigate("plugin:org.openhu.camera", "aa"), QString("aa"));
    }

    void waitingEnds()
    {
        // the user chooses a page (or "Exit" inside Android Auto)
        ProjectionPage pages;
        pages.videoChanged("plugin:org.openhu.camera", true);
        pages.chosenByUser();
        QCOMPARE(pages.navigate("plugin:org.openhu.camera", "home"), QString("home"));

        // the projection ends before the camera
        pages.videoChanged("plugin:org.openhu.camera", true);
        QCOMPARE(pages.videoChanged("plugin:org.openhu.camera", false), QString("plugin:org.openhu.camera"));
        QVERIFY(!pages.waiting());
        QCOMPARE(pages.navigate("plugin:org.openhu.camera", "home"), QString("home"));
    }

    void shellControllerForwards()
    {
        ShellController shell;
        QCOMPARE(shell.pageOnVideo("plugin:org.openhu.camera", true), QString("plugin:org.openhu.camera"));
        QCOMPARE(shell.pageOnNavigate("plugin:org.openhu.camera", "settings/wifi"), QString("aa"));
        shell.pageOnVideo("plugin:org.openhu.camera", true);
        shell.pageChosenByUser();
        QCOMPARE(shell.pageOnNavigate("plugin:org.openhu.camera", "home"), QString("home"));
    }
};

QTEST_GUILESS_MAIN(TestShellController)
#include "test_shellcontroller.moc"
