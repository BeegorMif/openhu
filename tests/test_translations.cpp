#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Shell/Translations.hpp>

using f1x::openauto::autoapp::core::EventBus;
using f1x::openauto::autoapp::shell::Translations;

// OPENHU_QM_DIR: translations/*.ts compiled by the build, OPENHU_TEST_QM: tests/data/plugin_de.ts
class TestTranslations: public QObject
{
    Q_OBJECT

private slots:
    void systemLanguage_data()
    {
        QTest::addColumn<QLocale>("locale");
        QTest::addColumn<QString>("language");
        QTest::newRow("German") << QLocale(QLocale::German, QLocale::Austria) << "de";
        QTest::newRow("English") << QLocale(QLocale::English, QLocale::UnitedKingdom) << "en";
        // no locale (C.UTF-8 in Docker, OpenHU OS): English
        QTest::newRow("C") << QLocale::c() << "en";
        // no translation: English
        QTest::newRow("French") << QLocale(QLocale::French, QLocale::France) << "en";
    }

    void systemLanguage()
    {
        QFETCH(QLocale, locale);
        QFETCH(QString, language);
        QCOMPARE(Translations::systemLanguage(locale), language);
    }

    void resolve()
    {
        QCOMPARE(Translations::resolve("en"), QString("en"));
        QCOMPARE(Translations::resolve("de"), QString("de"));
        QCOMPARE(Translations::resolve(""), Translations::systemLanguage());
        QCOMPARE(Translations::resolve("xx"), Translations::systemLanguage());
    }

    void languages()
    {
        const QVariantList languages = Translations::languages();
        QCOMPARE(languages.size(), Translations::available().size());
        QCOMPARE(languages.at(0).toMap().value("label").toString(), QString("English"));
        QCOMPARE(languages.at(1).toMap().value("value").toString(), QString("de"));
        QCOMPARE(languages.at(1).toMap().value("label").toString(), QString("Deutsch"));
    }

    void switchLanguage()
    {
        QVERIFY(QFile::exists(OPENHU_QM_DIR "/openhu_de.qm"));
        QVERIFY(QFile::exists(OPENHU_QM_DIR "/openhu_en.qm"));
        Translations translations(OPENHU_QM_DIR);
        QSignalSpy changed(&translations, &Translations::languageChanged);

        translations.setLanguage("de");
        QCOMPARE(qtTrId("settings.general.language"), QString("Sprache"));
        QCOMPARE(qtTrId("settings.error.port_between_1024_65535"), QString("Port zwischen 1024 und 65535"));
        QCOMPARE(QLocale().language(), QLocale::German);
        QCOMPARE(EventBus::instance().last("ui.language").value("language").toString(), QString("de"));

        // English is a translation file like every other one
        translations.setLanguage("en");
        QCOMPARE(qtTrId("settings.general.language"), QString("Language"));
        // an ID without a translation shows as itself: easy to spot
        QCOMPARE(qtTrId("settings.general.no_such_text"), QString("settings.general.no_such_text"));
        QCOMPARE(QLocale().language(), QLocale::English);
        QCOMPARE(changed.count(), 2);

        translations.setLanguage("en");  // unchanged: no signal
        QCOMPARE(changed.count(), 2);
    }

    // <plugins>/<id>/i18n/<language>.qm, read again when a plugin was installed
    void pluginTranslation()
    {
        QTemporaryDir plugins;
        Translations translations(OPENHU_QM_DIR);
        translations.setPluginsDir(plugins.path());
        translations.setLanguage("de");
        QCOMPARE(qtTrId("org.example.speed.main.top_speed"), QString("org.example.speed.main.top_speed"));

        QVERIFY(QDir().mkpath(plugins.filePath("org.example.speed/i18n")));
        QVERIFY(QFile::copy(OPENHU_TEST_QM, plugins.filePath("org.example.speed/i18n/de.qm")));
        translations.reload();
        QCOMPARE(qtTrId("org.example.speed.main.top_speed"), QString("Höchstgeschwindigkeit"));
        QCOMPARE(qtTrId("settings.general.language"), QString("Sprache"));

        translations.setLanguage("en");
        QCOMPARE(qtTrId("org.example.speed.main.top_speed"), QString("org.example.speed.main.top_speed"));
    }

    // dock and store: the plugin's translation of "<id>.name", otherwise the manifest's name; follows a switch
    void pluginName()
    {
        QTemporaryDir plugins;
        QVERIFY(QDir().mkpath(plugins.filePath("org.example.speed/i18n")));
        QVERIFY(QFile::copy(OPENHU_TEST_QM, plugins.filePath("org.example.speed/i18n/de.qm")));

        QQmlEngine engine;
        Translations translations(OPENHU_QM_DIR);
        translations.setEngine(&engine);
        translations.setPluginsDir(plugins.path());
        translations.setLanguage("en");

        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"PluginText.js\" as PluginText\n"
                          "QtObject {\n"
                          "    property var plugin: ({id: \"org.example.speed\", name: \"Speed\", description: \"Top speed\"})\n"
                          "    property string name: PluginText.name(plugin)\n"
                          "    property string description: PluginText.description(plugin)\n"
                          "    property string other: PluginText.name({id: \"org.example.other\"})\n"
                          "}\n",
                          QUrl::fromLocalFile(OPENHU_SHELL_QML_DIR "/Test.qml"));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("name").toString(), QString("Speed"));
        QCOMPARE(object->property("other").toString(), QString("org.example.other"));

        translations.setLanguage("de");
        QCOMPARE(object->property("name").toString(), QString("Tempo"));
        // no translation of the description: the manifest's text
        QCOMPARE(object->property("description").toString(), QString("Top speed"));

        translations.setLanguage("en");
        QCOMPARE(object->property("name").toString(), QString("Speed"));
    }
};

QTEST_GUILESS_MAIN(TestTranslations)
#include "test_translations.moc"
