/*
*  This file is part of openauto project.
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


#pragma once

#include <memory>
#include <vector>
#include <QLocale>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class QQmlEngine;
class QTranslator;

namespace f1x::openauto::autoapp::shell
{

// Language of the shell. The code has text IDs (qsTrId/qtTrId, "settings.general.language"); every language,
// English too, is translations/openhu_<language>.ts, compiled into the resources (:/i18n/openhu_<language>.qm).
// An ID without a translation shows as the ID. Plugins bring their own as <plugin>/i18n/<language>.qm with IDs
// that start with the plugin id (docs/plugins.md). Switching retranslates the QML engine at once (every binding that called qsTrId), sets the default QLocale
// (dates, numbers) and publishes "ui.language".
class Translations: public QObject
{
    Q_OBJECT

public:
    explicit Translations(const QString& qmDir = QStringLiteral(":/i18n"), QObject* parent = nullptr);
    ~Translations() override;

    // the languages with a translation file; the first one is the default
    static QStringList available();
    // [{value: "de", label: "Deutsch"}, ...] for the choice in the settings, each named in itself
    static QVariantList languages();
    // the system locale if there is a translation for it, otherwise (also without a locale, C/POSIX) English
    static QString systemLanguage(const QLocale& locale = QLocale::system());
    // a configured language (General.Language) or, when empty or unknown, systemLanguage()
    static QString resolve(const QString& configured);

    QString language() const { return language_; }
    void setLanguage(const QString& language);
    // retranslated on every switch
    void setEngine(QQmlEngine* engine);
    // <dir>/<plugin>/i18n/<language>.qm, read again by reload() (plugin installed or updated)
    void setPluginsDir(const QString& dir);
    void reload();

signals:
    void languageChanged(const QString& language);

private:
    QString qmDir_;
    QString pluginsDir_;
    QString language_;
    QQmlEngine* engine_ = nullptr;
    std::vector<std::unique_ptr<QTranslator>> translators_;
};

}
