// On-screen keyboard (Keyboard.qml): no greyed-out language key with the one active layout (#38)

#include <QFile>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

namespace
{
// all items below root (the keys of a layout are created by Loaders, so walk the visual tree)
void collect(QQuickItem* item, QList<QQuickItem*>& out)
{
    out.append(item);
    for(QQuickItem* child : item->childItems())
    {
        collect(child, out);
    }
}

QList<QQuickItem*> keysOfType(QQuickItem* root, const char* type)
{
    QList<QQuickItem*> all;
    collect(root, all);
    QList<QQuickItem*> keys;
    for(QQuickItem* item : all)
    {
        // QML types are named like SpaceKey_QMLTYPE_12
        if(QByteArray(item->metaObject()->className()).startsWith(type))
        {
            keys.append(item);
        }
    }
    return keys;
}

// The window of the test. The builtin style is imported here: without it the panel had no style (and no height)
// in this test, unlike in the shell.
const char* windowQml = R"(
import QtQuick
import QtQuick.VirtualKeyboard
import QtQuick.VirtualKeyboard.Settings
import QtQuick.VirtualKeyboard.Styles.Builtin
Window {
    width: 1280; height: 720; visible: true
    property alias field: field
    property alias keyboard: keyboard
    property var settings: VirtualKeyboardSettings
    function showAllFunctionKeys(all) {
        VirtualKeyboardSettings.visibleFunctionKeys = all ? QtVirtualKeyboard.KeyboardFunctionKeys.All
                                                          : QtVirtualKeyboard.KeyboardFunctionKeys.Hide
    }
    TextInput { id: field; width: 200; height: 40 }
    Loader { id: keyboard; source: KEYBOARD }
}
)";
}

class KeyboardTest: public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if(qgetenv("QT_IM_MODULE") != "qtvirtualkeyboard")
        {
            QSKIP("Qt Virtual Keyboard plugin not installed");
        }
        QByteArray qml(windowQml);
        qml.replace("KEYBOARD", "\"file://" OPENHU_SHELL_QML_DIR "/Keyboard.qml\"");
        engine_.loadData(qml);
        QVERIFY(!engine_.rootObjects().isEmpty());
        window_ = qobject_cast<QQuickWindow*>(engine_.rootObjects().first());
        QVERIFY(window_);
        QTRY_VERIFY(panel());
        // focus a text field: the panel becomes active and loads the layout
        window_->requestActivate();
        QVERIFY(QMetaObject::invokeMethod(field(), "forceActiveFocus"));
        QTRY_VERIFY(panel()->property("active").toBool());
        // slid in (isVisible() of the keys is false while the panel is hidden)
        QTRY_VERIFY(panel()->isVisible());
    }

    void languageKeyHidden_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("locale");
        QTest::newRow("English") << "en" << "en_US";
        QTest::newRow("German") << "de" << "de_DE";
    }

    void languageKeyHidden()
    {
        QFETCH(QString, language);
        QFETCH(QString, locale);
        panel()->setProperty("language", language);
        QTRY_COMPARE(settings()->property("locale").toString(), locale);
        // the layout of the locale is loaded and has a language key, which is not shown
        QTRY_VERIFY(!keysOfType(panel(), "ChangeLanguageKey").isEmpty());
        QTRY_VERIFY(!keysOfType(panel(), "SpaceKey").isEmpty());
        // the hide key stays
        const auto hideKeys = keysOfType(panel(), "HideKeyboardKey");
        QVERIFY(!hideKeys.isEmpty());
        QTRY_VERIFY(hideKeys.first()->isVisible());
        for(QQuickItem* key : keysOfType(panel(), "ChangeLanguageKey"))
        {
            QVERIFY(!key->isVisible());
        }

        // the space bar gets (part of) the width of the language key
        QQuickItem* space = keysOfType(panel(), "SpaceKey").first();
        QTRY_VERIFY(space->width() > 0);
        const qreal widthWithout = space->width();
        QVERIFY(QMetaObject::invokeMethod(window_, "showAllFunctionKeys", Q_ARG(QVariant, true)));
        QTRY_VERIFY(keysOfType(panel(), "ChangeLanguageKey").first()->isVisible());
        QTRY_VERIFY(space->width() < widthWithout);
        // back to what Keyboard.qml sets
        QVERIFY(QMetaObject::invokeMethod(window_, "showAllFunctionKeys", Q_ARG(QVariant, false)));
        QTRY_COMPARE(space->width(), widthWithout);
    }

private:
    QObject* field() const
    {
        return window_->property("field").value<QObject*>();
    }

    QQuickItem* panel() const
    {
        return window_->property("keyboard").value<QQuickItem*>()->property("item").value<QQuickItem*>();
    }

    QObject* settings() const
    {
        return window_->property("settings").value<QObject*>();
    }

    QQmlApplicationEngine engine_;
    QQuickWindow* window_ = nullptr;
};

int main(int argc, char* argv[])
{
    // as autoapp.cpp: the input method only when the plugin exists
    if(QFile::exists(QLibraryInfo::path(QLibraryInfo::PluginsPath)
                     + "/platforminputcontexts/libqtvirtualkeyboardplugin.so"))
    {
        qputenv("QT_IM_MODULE", "qtvirtualkeyboard");
    }
    QGuiApplication app(argc, argv);
    KeyboardTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_keyboard.moc"
