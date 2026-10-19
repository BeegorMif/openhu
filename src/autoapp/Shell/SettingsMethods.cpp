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

#include <f1x/openauto/autoapp/Shell/SettingsMethods.hpp>

namespace f1x::openauto::autoapp::shell
{

namespace
{

QString requireKey(const QVariantMap& params)
{
    const QString key = params.value("key").toString();
    if(key.isEmpty())
    {
        throw api::ApiError("missing parameter: key");
    }
    if(!SettingsController::keys().contains(key))
    {
        throw api::ApiError("unknown setting: " + key.toStdString());
    }
    return key;
}

bool local(const QVariantMap& params)
{
    return params.value(api::ApiServer::cLocal).toBool();
}

// The value in the key's type: the command line sends text ("60", "on")
QVariant convert(const QString& key, const QVariantMap& schema, const QVariant& value)
{
    const QString type = schema.value("type").toString();
    const QString text = value.toString().trimmed();
    if(type == "bool")
    {
        if(value.typeId() == QMetaType::Bool)
        {
            return value;
        }
        static const QStringList yes{"true", "1", "on", "yes"};
        static const QStringList no{"false", "0", "off", "no"};
        if(yes.contains(text.toLower()) || no.contains(text.toLower()))
        {
            return yes.contains(text.toLower());
        }
        throw api::ApiError(key.toStdString() + ": true or false");
    }
    if(type == "int")
    {
        bool ok = false;
        const int number = text.toInt(&ok);
        if(!ok)
        {
            throw api::ApiError(key.toStdString() + ": a number");
        }
        return number;
    }
    if(type == "choice")
    {
        QStringList names;
        for(const QVariant& choice: schema.value("choices").toList())
        {
            if(choice.toString() == text)
            {
                return choice;
            }
            names << choice.toString();
        }
        throw api::ApiError(key.toStdString() + ": one of " + names.join(", ").toStdString());
    }
    return text;
}

}

void registerSettingsMethods(api::ApiServer& server, SettingsController& settings)
{
    // the debug keys only while the debug mode is on (#120)
    server.registerMethod("settings.keys", [&settings](const QVariantMap&) -> QVariant {
        QVariantMap keys = SettingsController::keys();
        if(!settings.debugEnabled())
        {
            for(auto it = keys.begin(); it != keys.end();)
            {
                it = it.value().toMap().value("debug").toBool() ? keys.erase(it) : std::next(it);
            }
        }
        return keys;
    });
    // secrets only for clients on this device
    server.registerMethod("settings.list", [&settings](const QVariantMap& params) -> QVariant {
        QVariantMap values = settings.values();
        if(!local(params))
        {
            const QVariantMap keys = SettingsController::keys();
            for(auto it = keys.cbegin(); it != keys.cend(); ++it)
            {
                if(it.value().toMap().value("secret").toBool())
                {
                    values.remove(it.key());
                }
            }
        }
        return QVariantMap{{"values", values}, {"pending", settings.pending()}};
    });
    server.registerMethod("settings.get", [&settings](const QVariantMap& params) -> QVariant {
        const QString key = requireKey(params);
        if(!local(params) && SettingsController::keys().value(key).toMap().value("secret").toBool())
        {
            throw api::ApiError(key.toStdString() + " can only be read on the device itself");
        }
        return QVariantMap{{"key", key}, {"value", settings.values().value(key)}};
    });
    server.registerMethod("settings.set", [&settings](const QVariantMap& params) -> QVariant {
        const QString key = requireKey(params);
        if(!params.contains("value"))
        {
            throw api::ApiError("missing parameter: value");
        }
        const QVariantMap schema = SettingsController::keys().value(key).toMap();
        if(schema.value("readOnly").toBool())
        {
            throw api::ApiError(key.toStdString() + " is read-only");
        }
        const QString error = settings.set(key, convert(key, schema, params.value("value")));
        if(!error.isEmpty())
        {
            throw api::ApiError(error.toStdString());
        }
        QVariantMap result{{"key", key}, {"pending", settings.pending()}, {"applies", schema.value("applies")}};
        if(!schema.value("secret").toBool() || local(params))
        {
            result.insert("value", settings.values().value(key));
        }
        return result;
    });
}

}
