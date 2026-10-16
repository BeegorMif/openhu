#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtTest>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>

using f1x::openauto::autoapp::plugins::PluginManager;

// The QML of an updated plugin: the shell loads plugin pages from disk by URL and QML caches them by URL (#105)
class TestPluginQml: public QObject
{
    Q_OBJECT

private:
    static void write(const QString& path, const QByteArray& text)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
    }

    // a plugin page with a second file of the plugin, like the camera's Main.qml with CameraView.qml
    void writePlugin(const QString& version)
    {
        write(plugin_.filePath("Main.qml"), "import QtQuick\nimport TestShell 1.0\n"
                                            "Item { property string version: view.version; property real scale: Theme.scale; View { id: view } }\n");
        write(plugin_.filePath("View.qml"), "import QtQuick\nItem { property string version: \"" + version.toUtf8() + "\" }\n");
    }

    std::unique_ptr<QObject> create(QQmlEngine& engine, const QString& path)
    {
        QQmlComponent component(&engine, QUrl::fromLocalFile(path));
        std::unique_ptr<QObject> object(component.create());
        if(!object)
        {
            qWarning() << component.errorString();
        }
        return object;
    }

    QTemporaryDir shell_;
    QTemporaryDir plugin_;

private slots:
    void updatedPluginIsLoadedAgain()
    {
        // a singleton registered by URL like the shell's Theme, and a shell object bound to it
        write(shell_.filePath("Theme.qml"), "pragma Singleton\nimport QtQml\nQtObject { property real scale: 1 }\n");
        qmlRegisterSingletonType(QUrl::fromLocalFile(shell_.filePath("Theme.qml")), "TestShell", 1, 0, "Theme");
        write(shell_.filePath("Shell.qml"), "import QtQuick\nimport TestShell 1.0\n"
                                            "Item { property real scale: Theme.scale * 2; Component.onCompleted: Theme.scale = 0.5 }\n");

        QQmlEngine engine;
        auto shell = create(engine, shell_.filePath("Shell.qml"));
        QVERIFY(shell);
        QCOMPARE(shell->property("scale").toReal(), 1.0);  // Theme.scale 0.5 * 2

        writePlugin("1.0.0");
        {
            auto page = create(engine, plugin_.filePath("Main.qml"));
            QVERIFY(page);
            QCOMPARE(page->property("version").toString(), QString("1.0.0"));
        }

        // the store: page gone, files replaced, cache dropped, page loaded again
        writePlugin("1.1.0");
        PluginManager::dropQmlCache(engine);
        auto page = create(engine, plugin_.filePath("Main.qml"));
        QVERIFY(page);
        QCOMPARE(page->property("version").toString(), QString("1.1.0"));

        // the shell is untouched: one Theme instance for the shell and the new page, the shell's bindings still work
        QCOMPARE(page->property("scale").toReal(), 0.5);
        auto theme = engine.singletonInstance<QObject*>("TestShell", "Theme");
        QVERIFY(theme);
        theme->setProperty("scale", 2.0);
        QCOMPARE(page->property("scale").toReal(), 2.0);
        QCOMPARE(shell->property("scale").toReal(), 4.0);
    }
};

QTEST_MAIN(TestPluginQml)
#include "test_pluginqml.moc"
