/*
*  This file is part of OpenHU.
*  Copyright (C) 2026 Leon Strauß
*
*  OpenHU is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.
*
*  OpenHU is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with OpenHU. If not, see <https://www.gnu.org/licenses/>.
*/


#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QQmlEngine>
#include <QTranslator>
#include <f1x/openauto/autoapp/Shell/Translations.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::shell
{

Translations::Translations(const QString& qmDir, QObject* parent)
    : QObject(parent)
    , qmDir_(qmDir)
{
}

Translations::~Translations()
{
    for(const auto& translator: translators_)
    {
        QCoreApplication::removeTranslator(translator.get());
    }
}

QStringList Translations::available()
{
    return {QStringLiteral("en"), QStringLiteral("de")};
}

QVariantList Translations::languages()
{
    QVariantList languages;
    for(const QString& language: available())
    {
        // QLocale("en") is American English; the language alone is enough here
        QString name = language == QLatin1String("en") ? QStringLiteral("English") : QLocale(language).nativeLanguageName();
        if(!name.isEmpty())
        {
            name[0] = name[0].toUpper();
        }
        languages.append(QVariantMap{{"value", language}, {"label", name}});
    }
    return languages;
}

QString Translations::systemLanguage(const QLocale& locale)
{
    const QString name = QLocale::languageToCode(locale.language());
    return available().contains(name) ? name : available().first();
}

QString Translations::resolve(const QString& configured)
{
    return available().contains(configured) ? configured : systemLanguage();
}

void Translations::setLanguage(const QString& language)
{
    const QString resolved = resolve(language);
    const bool changed = resolved != language_;
    language_ = resolved;
    this->reload();
    if(changed)
    {
        OPENAUTO_LOG(info) << "[Translations] language " << language_.toStdString();
        emit languageChanged(language_);
    }
}

void Translations::setEngine(QQmlEngine* engine)
{
    engine_ = engine;
}

void Translations::setPluginsDir(const QString& dir)
{
    pluginsDir_ = dir;
}

void Translations::reload()
{
    for(const auto& translator: translators_)
    {
        QCoreApplication::removeTranslator(translator.get());
    }
    translators_.clear();

    // every language is a file, English too (the code has IDs only); plugins come last, so they are asked first
    QStringList files{qmDir_ + "/openhu_" + language_ + ".qm"};
    if(!pluginsDir_.isEmpty())
    {
        const QDir plugins(pluginsDir_);
        for(const QString& plugin: plugins.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            const QString file = plugins.filePath(plugin + "/i18n/" + language_ + ".qm");
            if(QFile::exists(file))
            {
                files << file;
            }
        }
    }
    for(const QString& file: files)
    {
        auto translator = std::make_unique<QTranslator>();
        if(!translator->load(file))
        {
            OPENAUTO_LOG(warning) << "[Translations] cannot load " << file.toStdString();
            continue;
        }
        QCoreApplication::installTranslator(translator.get());
        translators_.push_back(std::move(translator));
    }

    QLocale::setDefault(QLocale(language_));
    if(engine_ != nullptr)
    {
        engine_->retranslate();
    }
    core::EventBus::instance().publish("ui.language", {{"language", language_}, {"available", available()}});
}

}
