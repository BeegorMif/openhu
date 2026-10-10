/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <thread>
#include <QApplication>
#include <QScreen>
#include <QDesktopWidget>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <aasdk/USB/USBHub.hpp>
#include <aasdk/USB/ConnectedAccessoriesEnumerator.hpp>
#include <aasdk/USB/AccessoryModeQueryChain.hpp>
#include <aasdk/USB/AccessoryModeQueryChainFactory.hpp>
#include <aasdk/USB/AccessoryModeQueryFactory.hpp>
#include <aasdk/TCP/TCPWrapper.hpp>
#include <boost/log/utility/setup.hpp>
#include <f1x/openauto/autoapp/App.hpp>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Configuration/RecentAddressesList.hpp>
#include <f1x/openauto/autoapp/Service/AndroidAutoEntityFactory.hpp>
#include <f1x/openauto/autoapp/Service/ServiceFactory.hpp>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/autoapp/UI/MainWindow.hpp>
#include <f1x/openauto/autoapp/UI/SettingsWindow.hpp>
#include <f1x/openauto/autoapp/UI/ConnectDialog.hpp>
#include <f1x/openauto/autoapp/UI/WarningDialog.hpp>
#include <f1x/openauto/autoapp/UI/UpdateDialog.hpp>
#include <f1x/openauto/autoapp/Api/ApiServer.hpp>
#include <f1x/openauto/autoapp/Api/CoreMethods.hpp>
#include <f1x/openauto/autoapp/Audio/VolumeController.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Input/ActionDispatcher.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginMethods.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginStore.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include <f1x/openauto/autoapp/Shell/AaSurface.hpp>
#include <f1x/openauto/autoapp/Shell/ShellController.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;

void startUSBWorkers(boost::asio::io_service& ioService, libusb_context* usbContext, ThreadPool& threadPool)
{
    auto usbWorker = [&ioService, usbContext]() {
        timeval libusbEventTimeout{180, 0};

        while(!ioService.stopped())
        {
            libusb_handle_events_timeout_completed(usbContext, &libusbEventTimeout, nullptr);
        }
    };

    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
}

void startIOServiceWorkers(boost::asio::io_service& ioService, ThreadPool& threadPool)
{
    auto ioServiceWorker = [&ioService]() {
        ioService.run();
    };

    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
}

void configureLogging() {
    const std::string logIni = "openauto-logs.ini";
    std::ifstream logSettings(logIni);
    if (logSettings.good()) {
        try {
            // For boost < 1.71 the severity types are not automatically parsed so lets register them.
            boost::log::register_simple_filter_factory<boost::log::trivial::severity_level>("Severity");
            boost::log::register_simple_formatter_factory<boost::log::trivial::severity_level, char>("Severity");
            boost::log::init_from_stream(logSettings);
        } catch (std::exception const & e) {
            OPENAUTO_LOG(warning) << "[OpenAuto] " << logIni << " was provided but was not valid.";
        }
    }
}

int main(int argc, char* argv[])
{
    configureLogging();

    libusb_context* usbContext;
    if(libusb_init(&usbContext) != 0)
    {
        OPENAUTO_LOG(error) << "[AutoApp] libusb_init failed.";
        return 1;
    }

    boost::asio::io_service ioService;
    boost::asio::io_service::work work(ioService);
    std::vector<std::thread> threadPool;
    startUSBWorkers(ioService, usbContext, threadPool);
    startIOServiceWorkers(ioService, threadPool);

    // Dark Material style for Qt Quick Controls in the shell and plugin pages
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "#4f9cf9");
    qputenv("QT_QUICK_CONTROLS_MATERIAL_BACKGROUND", "#192027");

    QApplication qApplication(argc, argv);
    int width = QApplication::desktop()->width();
    int height = QApplication::desktop()->height();

    for (QScreen *screen : qApplication.screens()) {
      OPENAUTO_LOG(info) << "[AutoApp] Screen name: " << screen->name().toStdString();
      OPENAUTO_LOG(info) << "[AutoApp] Screen geometry: " << screen->geometry().width(); // This includes position and size
      OPENAUTO_LOG(info) << "[AutoApp] Screen physical size: " << screen->physicalSize().width(); // Size in millimeters
    }

    QScreen *primaryScreen = QGuiApplication::primaryScreen();

    // Check if a primary screen was found
    if (primaryScreen) {
      // Get the geometry of the primary screen
      QRect screenGeometry = primaryScreen->geometry();
      width = screenGeometry.width();
      height = screenGeometry.height();
      OPENAUTO_LOG(info) << "[AutoApp] Using gemoetry from primary screen.";
    } else {
      OPENAUTO_LOG(info) << "[AutoApp] Unable to find primary screen, using default values.";
    }

    OPENAUTO_LOG(info) << "[AutoApp] Display width: " << width;
    OPENAUTO_LOG(info) << "[AutoApp] Display height: " << height;

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();
    autoapp::shell::AaBridge::instance().configure(configuration);
    const bool useShell = autoapp::shell::AaBridge::instance().enabled();
    OPENAUTO_LOG(info) << "[AutoApp] UI mode: " << (useShell ? "qml shell" : "legacy");

    autoapp::ui::SettingsWindow settingsWindow(configuration);
    //settingsWindow.setWindowFlags(Qt::WindowStaysOnTopHint);

    settingsWindow.setFixedSize(width, height);
    settingsWindow.adjustSize();

    autoapp::configuration::RecentAddressesList recentAddressesList(7);
    recentAddressesList.read();

    aasdk::tcp::TCPWrapper tcpWrapper;

    if (configuration->showCursor() == false) {
        qApplication.setOverrideCursor(Qt::BlankCursor);
    } else {
        qApplication.setOverrideCursor(Qt::ArrowCursor);
    }

    aasdk::usb::USBWrapper usbWrapper(usbContext);
    aasdk::usb::AccessoryModeQueryFactory queryFactory(usbWrapper, ioService);
    aasdk::usb::AccessoryModeQueryChainFactory queryChainFactory(usbWrapper, ioService, queryFactory);
    autoapp::service::ServiceFactory serviceFactory(ioService, configuration);
    autoapp::service::AndroidAutoEntityFactory androidAutoEntityFactory(ioService, configuration, serviceFactory);

    auto usbHub(std::make_shared<aasdk::usb::USBHub>(usbWrapper, ioService, queryChainFactory));
    auto connectedAccessoriesEnumerator(std::make_shared<aasdk::usb::ConnectedAccessoriesEnumerator>(usbWrapper, ioService, queryChainFactory));
    auto app = std::make_shared<autoapp::App>(ioService, usbWrapper, tcpWrapper, androidAutoEntityFactory, std::move(usbHub), std::move(connectedAccessoriesEnumerator));

    autoapp::shell::ShellController shellController;
    autoapp::audio::VolumeController volumeController;
    autoapp::input::ActionDispatcher actionDispatcher(volumeController, shellController);

    // WebSocket API for external tools and plugins (docs/api.md)
    autoapp::api::ApiServer apiServer;
    autoapp::api::registerCoreMethods(apiServer, volumeController, shellController, actionDispatcher);
    apiServer.listen(QHostAddress(QString::fromStdString(configuration->getApiListenAddress())), configuration->getApiPort());

    // Plugins (~/.openhu/plugins) and the store (~/.openhu/sources.list), docs/plugins.md
    autoapp::plugins::PluginManager pluginManager(configuration->getApiPort());
    autoapp::plugins::PluginStore pluginStore(pluginManager);
    autoapp::plugins::registerPluginMethods(apiServer, pluginManager, pluginStore);
    autoapp::plugins::QmlApi qmlApi(apiServer);
    pluginManager.load();

    // New QML shell: Android Auto is shown inside the main window next to the rest of the UI.
    QQmlApplicationEngine engine;

    // Legacy widget UI (General.UI=legacy)
    std::unique_ptr<autoapp::ui::MainWindow> mainWindow;
    std::unique_ptr<autoapp::ui::ConnectDialog> connectdialog;
    std::unique_ptr<autoapp::ui::WarningDialog> warningdialog;
    std::unique_ptr<autoapp::ui::UpdateDialog> updatedialog;

    if (useShell) {
        // QML module for the shell and plugin pages: import OpenHU 1.0
        qmlRegisterType<autoapp::shell::AaSurface>("OpenHU", 1, 0, "AaSurface");
        qmlRegisterSingletonType(QUrl("qrc:/shell/qml/module/Theme.qml"), "OpenHU", 1, 0, "Theme");
        qmlRegisterType(QUrl("qrc:/shell/qml/BusTopic.qml"), "OpenHU", 1, 0, "BusTopic");
        engine.rootContext()->setContextProperty("aaBridge", &autoapp::shell::AaBridge::instance());
        engine.rootContext()->setContextProperty("eventBus", &autoapp::core::EventBus::instance());
        engine.rootContext()->setContextProperty("shell", &shellController);
        engine.rootContext()->setContextProperty("volume", &volumeController);
        engine.rootContext()->setContextProperty("actions", &actionDispatcher);
        engine.rootContext()->setContextProperty("plugins", &pluginManager);
        engine.rootContext()->setContextProperty("store", &pluginStore);
        engine.rootContext()->setContextProperty("api", &qmlApi);
        // Keyboard -> actions (keymap.json); in legacy mode the InputDevice handles keys itself.
        actionDispatcher.installKeyboardHandler();

        QObject::connect(&shellController, &autoapp::shell::ShellController::settingsRequested, &settingsWindow, &autoapp::ui::SettingsWindow::showFullScreen);
        QObject::connect(&shellController, &autoapp::shell::ShellController::settingsRequested, &settingsWindow, &autoapp::ui::SettingsWindow::show_tab1);
        QObject::connect(&shellController, &autoapp::shell::ShellController::settingsRequested, &settingsWindow, &autoapp::ui::SettingsWindow::loadSystemValues);
        QObject::connect(&shellController, &autoapp::shell::ShellController::quitRequested, []() { std::exit(0); });

        pluginManager.setQmlCacheTrimmer([&engine]() { engine.trimComponentCache(); });

        engine.load(QUrl(QStringLiteral("qrc:/shell/qml/Main.qml")));
        if (engine.rootObjects().isEmpty()) {
            OPENAUTO_LOG(error) << "[AutoApp] Failed to load the QML shell.";
            std::exit(1); // worker threads are still running, a plain return would abort
        }

        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            window->setGeometry(0, 0, width, height);
            window->showFullScreen();
        }
    } else {
        mainWindow = std::make_unique<autoapp::ui::MainWindow>(configuration);
        //mainWindow->setWindowFlags(Qt::WindowStaysOnTopHint);

        connectdialog = std::make_unique<autoapp::ui::ConnectDialog>(ioService, tcpWrapper, recentAddressesList);
        //connectdialog->setWindowFlags(Qt::WindowStaysOnTopHint);
        connectdialog->move((width - 500)/2,(height-300)/2);

        warningdialog = std::make_unique<autoapp::ui::WarningDialog>();
        //warningdialog->setWindowFlags(Qt::WindowStaysOnTopHint);
        warningdialog->move((width - 500)/2,(height-300)/2);

        updatedialog = std::make_unique<autoapp::ui::UpdateDialog>();
        //updatedialog->setWindowFlags(Qt::WindowStaysOnTopHint);
        updatedialog->setFixedSize(500, 260);
        updatedialog->move((width - 500)/2,(height-260)/2);

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::exit, []() { system("touch /tmp/shutdown"); std::exit(0); });
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::reboot, []() { system("touch /tmp/reboot"); std::exit(0); });
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::showFullScreen);
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::show_tab1);
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::loadSystemValues);
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::openConnectDialog, connectdialog.get(), &autoapp::ui::ConnectDialog::loadClientList);
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::openConnectDialog, connectdialog.get(), &autoapp::ui::ConnectDialog::exec);
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::openUpdateDialog, updatedialog.get(), &autoapp::ui::UpdateDialog::updateCheck);
        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::openUpdateDialog, updatedialog.get(), &autoapp::ui::UpdateDialog::exec);

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraHide, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py Background &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera Background.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraShow, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py Foreground &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera Foreground.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraPosYUp, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py PosYUp &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera PosY up.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraPosYDown, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py PosYDown &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera PosY down.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraZoomPlus, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py ZoomPlus &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera Zoom plus.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraZoomMinus, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py ZoomMinus &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera Zoom minus.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraRecord, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py Record &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera Record.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraStop, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py Stop &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera Stop.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::cameraSave, [&qApplication]() {
            system("/opt/crankshaft/cameracontrol.py Save &");
            OPENAUTO_LOG(debug) << "[AutoApp] Camera Save.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::TriggerScriptNight, [&qApplication]() {
            system("/opt/crankshaft/service_daynight.sh app night");
            OPENAUTO_LOG(debug) << "[AutoApp] MainWindow Night.";
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::TriggerScriptDay, [&qApplication]() {
            system("/opt/crankshaft/service_daynight.sh app day");
            OPENAUTO_LOG(debug) << "[AutoApp] MainWindow Day.";
        });

        mainWindow->showFullScreen();
        mainWindow->setFixedSize(width, height);
        mainWindow->adjustSize();

        QObject::connect(connectdialog.get(), &autoapp::ui::ConnectDialog::connectionSucceed, [&app](auto socket) {
            app->start(std::move(socket));
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::TriggerAppStart, [&app]() {
            OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStart: Manual start android auto.";
            try {
                app->disableAutostartEntity = false;
                app->resume();
                app->waitForUSBDevice();
            } catch (...) {
                OPENAUTO_LOG(error) << "[AutoApp] TriggerAppStart: app->waitForUSBDevice();";
            }
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::TriggerAppStop, [&app]() {
            try {
                if (std::ifstream("/tmp/android_device")) {
                    OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStop: Manual stop usb android auto.";
                    app->disableAutostartEntity = true;
                    system("/usr/local/bin/autoapp_helper usbreset");
                    usleep(500000);
                    try {
                        app->stop();
                        //app->pause();
                    } catch (...) {
                        OPENAUTO_LOG(error) << "[AutoApp] TriggerAppStop: stop();";
                    }

                } else {
                    OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStop: Manual stop wifi android auto.";
                    try {
                        app->onAndroidAutoQuit();
                        //app->pause();
                    } catch (...) {
                        OPENAUTO_LOG(error) << "[Autoapp] TriggerAppStop: stop();";
                    }

                }
            } catch (...) {
                OPENAUTO_LOG(error) << "[AutoApp] Exception in manual stop android auto.";
            }
        });

        QObject::connect(mainWindow.get(), &autoapp::ui::MainWindow::CloseAllDialogs, [&settingsWindow, &connectdialog, &updatedialog, &warningdialog]() {
            settingsWindow.close();
            connectdialog->close();
            warningdialog->close();
            updatedialog->close();
            OPENAUTO_LOG(debug) << "[AutoApp] Close all possible open dialogs.";
        });

        if (configuration->hideWarning() == false) {
            warningdialog->show();
        }
    }

    app->waitForUSBDevice();

    auto result = qApplication.exec();

    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    libusb_exit(usbContext);
    return result;
}
