#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
        Translations translations(OPENHU_QM_DIR);
        QSignalSpy changed(&translations, &Translations::languageChanged);

        translations.setLanguage("de");
        QCOMPARE(QCoreApplication::translate("SettingsPage", "Language"), QString("Sprache"));
        QCOMPARE(QCoreApplication::translate("f1x::openauto::autoapp::shell::SettingsController", "Port between 1024 and 65535"),
                 QString("Port zwischen 1024 und 65535"));
        QCOMPARE(QLocale().language(), QLocale::German);
        QCOMPARE(EventBus::instance().last("ui.language").value("language").toString(), QString("de"));

        translations.setLanguage("en");
        QCOMPARE(QCoreApplication::translate("SettingsPage", "Language"), QString("Language"));
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
        QCOMPARE(QCoreApplication::translate("Main", "Top speed"), QString("Top speed"));

        QVERIFY(QDir().mkpath(plugins.filePath("org.example.speed/i18n")));
        QVERIFY(QFile::copy(OPENHU_TEST_QM, plugins.filePath("org.example.speed/i18n/de.qm")));
        translations.reload();
        QCOMPARE(QCoreApplication::translate("Main", "Top speed"), QString("Höchstgeschwindigkeit"));
        QCOMPARE(QCoreApplication::translate("SettingsPage", "Language"), QString("Sprache"));

        translations.setLanguage("en");
        QCOMPARE(QCoreApplication::translate("Main", "Top speed"), QString("Top speed"));
    }
};

QTEST_GUILESS_MAIN(TestTranslations)
#include "test_translations.moc"
