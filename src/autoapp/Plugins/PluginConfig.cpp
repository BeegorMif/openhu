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

#include <algorithm>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <f1x/openauto/autoapp/Plugins/PluginConfig.hpp>
#include <f1x/openauto/autoapp/Plugins/PluginManager.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

namespace
{

constexpr int cMaxKeyLength = 128;
const QStringList cTypes{"switch", "choice", "number", "text", "section"};

bool isNumber(const QVariant& value)
{
    switch(value.typeId())
    {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}

bool isString(const QVariant& value)
{
    return value.typeId() == QMetaType::QString;
}

// an integer from a JSON number (integral) or a text; false if it is none
bool toInteger(const QVariant& value, qint64* result)
{
    if(isNumber(value))
    {
        const double number = value.toDouble();
        if(!std::isfinite(number) || std::floor(number) != number || std::fabs(number) > 1e15)
        {
            return false;
        }
        *result = static_cast<qint64>(number);
        return true;
    }
    if(isString(value))
    {
        bool ok = false;
        *result = value.toString().trimmed().toLongLong(&ok);
        return ok;
    }
    return false;
}

// a choice option or value as text: 49.6 → "49.6", 20 → "20"
QString optionText(const QVariant& value)
{
    if(isNumber(value))
    {
        return QString::number(value.toDouble(), 'g', 15);
    }
    return value.toString();
}

QByteArray serialize(const QVariantMap& values)
{
    return QJsonDocument(QJsonObject::fromVariantMap(values)).toJson(QJsonDocument::Indented);
}

// JSON round trip: what a value looks like once it was stored (QVariant types of numbers, lists, maps)
QVariant normalized(const QVariant& value)
{
    return QJsonValue::fromVariant(value).toVariant();
}

}

QVariantList PluginConfig::parseSchema(const QVariantMap& manifest, QStringList* errors)
{
    QVariantList schema;
    const auto fail = [errors](int index, const QString& key, const QString& reason) {
        if(errors)
        {
            errors->append(QString("settings[%1]%2: %3").arg(index).arg(key.isEmpty() ? QString() : " " + key, reason));
        }
    };
    if(!manifest.contains("settings"))
    {
        return schema;
    }
    const QVariant settings = manifest.value("settings");
    if(settings.typeId() != QMetaType::QVariantList)
    {
        if(errors)
        {
            errors->append("settings: not a list");
        }
        return schema;
    }

    static const QRegularExpression keyPattern("^[A-Za-z0-9_]{1,64}$");
    QStringList keys;
    const QVariantList entries = settings.toList();
    for(int index = 0; index < entries.size(); ++index)
    {
        if(entries[index].typeId() != QMetaType::QVariantMap)
        {
            fail(index, QString(), "not an object");
            continue;
        }
        const QVariantMap source = entries[index].toMap();
        const QString key = source.value("key").toString();
        const QString type = source.value("type").toString();
        if(!keyPattern.match(key).hasMatch())
        {
            fail(index, key, "key missing or not letters, digits and _");
            continue;
        }
        if(keys.contains(key))
        {
            fail(index, key, "duplicate key");
            continue;
        }
        if(!cTypes.contains(type))
        {
            fail(index, key, "unknown type \"" + type + "\"");
            continue;
        }

        QVariantMap entry{{"key", key}, {"type", type}};
        for(const char* text: {"title", "description"})
        {
            if(source.contains(text))
            {
                entry.insert(text, source.value(text).toString());
            }
        }
        if(type == "section")
        {
            keys.append(key);
            schema.append(entry);
            continue;
        }

        const QString applies = source.value("applies", "now").toString();
        if(applies != "now" && applies != "restart")
        {
            fail(index, key, "applies must be now or restart");
            continue;
        }
        entry.insert("applies", applies);
        if(!source.contains("default"))
        {
            fail(index, key, "default missing");
            continue;
        }

        if(type == "choice")
        {
            QStringList options;
            for(const auto& option: source.value("options").toList())
            {
                if(isNumber(option) || isString(option))
                {
                    options.append(optionText(option));
                }
            }
            options.removeDuplicates();
            if(options.isEmpty() || options.size() != source.value("options").toList().size())
            {
                fail(index, key, "options must be a list of different texts");
                continue;
            }
            entry.insert("options", options);
        }
        else if(type == "number")
        {
            qint64 min = 0;
            qint64 max = 0;
            qint64 step = 1;
            if(!toInteger(source.value("min"), &min) || !toInteger(source.value("max"), &max) || min > max
               || !isNumber(source.value("min")) || !isNumber(source.value("max")))
            {
                fail(index, key, "min and max must be integers, min <= max");
                continue;
            }
            if(source.contains("step") && (!isNumber(source.value("step")) || !toInteger(source.value("step"), &step)
                                           || step < 1))
            {
                fail(index, key, "step must be an integer >= 1");
                continue;
            }
            entry.insert("min", min);
            entry.insert("max", max);
            entry.insert("step", step);
        }
        else if(type == "text")
        {
            qint64 maxLength = 256;
            if(source.contains("maxLength") && (!isNumber(source.value("maxLength"))
                                                || !toInteger(source.value("maxLength"), &maxLength)
                                                || maxLength < 1 || maxLength > cMaxTextLength))
            {
                fail(index, key, QString("maxLength must be 1 to %1").arg(cMaxTextLength));
                continue;
            }
            entry.insert("maxLength", maxLength);
            if(source.contains("placeholder"))
            {
                entry.insert("placeholder", source.value("placeholder").toString());
            }
        }

        // the default must be a valid value itself, and in its type (not as text)
        QVariant value;
        const QVariant defaultValue = source.value("default");
        const bool rightType = type == "switch" ? defaultValue.typeId() == QMetaType::Bool
                               : type == "number" ? isNumber(defaultValue)
                               : type == "choice" ? isNumber(defaultValue) || isString(defaultValue)
                                                  : isString(defaultValue);
        const QString error = rightType ? convert(entry, defaultValue, &value) : QString("wrong type");
        if(!error.isEmpty())
        {
            fail(index, key, "default: " + error);
            continue;
        }
        entry.insert("default", value);
        keys.append(key);
        schema.append(entry);
    }
    return schema;
}

QString PluginConfig::convert(const QVariantMap& entry, const QVariant& value, QVariant* result)
{
    const QString type = entry.value("type").toString();
    if(type == "switch")
    {
        if(value.typeId() == QMetaType::Bool)
        {
            *result = value.toBool();
            return QString();
        }
        qint64 number = 0;
        if(isNumber(value) && toInteger(value, &number) && (number == 0 || number == 1))
        {
            *result = number == 1;
            return QString();
        }
        const QString text = value.toString().trimmed().toLower();
        if(isString(value) && QStringList{"true", "on", "yes", "1"}.contains(text))
        {
            *result = true;
            return QString();
        }
        if(isString(value) && QStringList{"false", "off", "no", "0"}.contains(text))
        {
            *result = false;
            return QString();
        }
        return "expects true or false";
    }
    if(type == "number")
    {
        const qint64 min = entry.value("min").toLongLong();
        const qint64 max = entry.value("max").toLongLong();
        const qint64 step = std::max<qint64>(1, entry.value("step", 1).toLongLong());
        qint64 number = 0;
        if(!toInteger(value, &number))
        {
            return "expects a whole number";
        }
        if(number < min || number > max)
        {
            return QString("must be %1 to %2").arg(min).arg(max);
        }
        if((number - min) % step != 0)
        {
            return QString("must be %1 plus a multiple of %2").arg(min).arg(step);
        }
        *result = number;
        return QString();
    }
    if(type == "choice")
    {
        const QStringList options = entry.value("options").toStringList();
        const QString text = optionText(value);
        if(!(isNumber(value) || isString(value)) || !options.contains(text))
        {
            return "must be one of: " + options.join(", ");
        }
        *result = text;
        return QString();
    }
    if(type == "text")
    {
        if(!(isString(value) || isNumber(value)))
        {
            return "expects a text";
        }
        const QString text = isNumber(value) ? optionText(value) : value.toString();
        const qint64 maxLength = entry.value("maxLength", 256).toLongLong();
        if(text.size() > maxLength)
        {
            return QString("at most %1 characters").arg(maxLength);
        }
        *result = text;
        return QString();
    }
    return "not a setting";
}

QString PluginConfig::filePath(const QString& id)
{
    return PluginManager::baseDir() + "/plugin-data/" + id + "/config.json";
}

QString PluginConfig::topic(const QString& id)
{
    return "plugin.config." + id;
}

void PluginConfig::load(const QString& id, const QVariantList& schema)
{
    Store& store = stores_[id];
    store.schema = schema;
    this->read(id, store);
    this->publish(id, store, QStringList());
}

void PluginConfig::unload(const QString& id)
{
    stores_.erase(id);
}

void PluginConfig::clear()
{
    stores_.clear();
}

bool PluginConfig::contains(const QString& id) const
{
    return stores_.find(id) != stores_.end();
}

QVariantList PluginConfig::schema(const QString& id) const
{
    auto it = stores_.find(id);
    return it == stores_.end() ? QVariantList() : it->second.schema;
}

QVariantMap PluginConfig::entry(const QString& id, const QString& key) const
{
    for(const auto& item: this->schema(id))
    {
        const QVariantMap entry = item.toMap();
        if(entry.value("key").toString() == key)
        {
            return entry;
        }
    }
    return QVariantMap();
}

QVariantMap PluginConfig::values(const QString& id)
{
    auto it = stores_.find(id);
    if(it == stores_.end())
    {
        return QVariantMap();
    }
    this->reloadIfChanged(id, it->second);
    return this->valuesOf(it->second);
}

QVariantMap PluginConfig::valuesOf(const Store& store) const
{
    QVariantMap values = store.stored;
    for(const auto& item: store.schema)
    {
        const QVariantMap entry = item.toMap();
        const QString key = entry.value("key").toString();
        if(entry.value("type") == "section")
        {
            // a free key of the same name stays free
            continue;
        }
        // a stored value that no longer fits (an update narrowed a range) counts as the default; the file keeps it
        QVariant value;
        if(!values.contains(key) || !convert(entry, values.value(key), &value).isEmpty())
        {
            value = entry.value("default");
        }
        values.insert(key, value);
    }
    return values;
}

QString PluginConfig::set(const QString& id, const QVariantMap& changes, QStringList* changed)
{
    auto it = stores_.find(id);
    if(it == stores_.end())
    {
        return "unknown plugin: " + id;
    }
    Store& store = it->second;
    this->reloadIfChanged(id, store);

    QVariantMap stored = store.stored;
    for(auto change = changes.cbegin(); change != changes.cend(); ++change)
    {
        const QString& key = change.key();
        if(key.isEmpty() || key.size() > cMaxKeyLength)
        {
            return QString("key must be 1 to %1 characters").arg(cMaxKeyLength);
        }
        const QVariantMap entry = this->entry(id, key);
        if(entry.value("type") == "section")
        {
            return key + ": a section, not a setting";
        }
        if(entry.isEmpty())
        {
            stored.insert(key, normalized(change.value()));
            continue;
        }
        QVariant value;
        const QString error = convert(entry, change.value(), &value);
        if(!error.isEmpty())
        {
            return key + ": " + error;
        }
        stored.insert(key, value);
    }
    return this->write(id, store, stored, changed);
}

QString PluginConfig::remove(const QString& id, const QStringList& keys, QStringList* changed)
{
    auto it = stores_.find(id);
    if(it == stores_.end())
    {
        return "unknown plugin: " + id;
    }
    Store& store = it->second;
    this->reloadIfChanged(id, store);
    QVariantMap stored = store.stored;
    for(const auto& key: keys)
    {
        stored.remove(key);
    }
    return this->write(id, store, stored, changed);
}

QString PluginConfig::reset(const QString& id, QStringList* changed)
{
    QStringList keys;
    for(const auto& item: this->schema(id))
    {
        if(item.toMap().value("type") != "section")
        {
            keys.append(item.toMap().value("key").toString());
        }
    }
    return this->remove(id, keys, changed);
}

void PluginConfig::read(const QString& id, Store& store)
{
    const QString path = filePath(id);
    const QFileInfo info(path);
    store.stored.clear();
    store.modified = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    store.size = info.exists() ? info.size() : -1;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    const QByteArray data = file.readAll();
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if(document.isObject())
    {
        store.stored = document.object().toVariantMap();
        return;
    }
    if(!data.trimmed().isEmpty())
    {
        // kept for the user, the next change starts a new file
        OPENAUTO_LOG(warning) << "[PluginConfig] " << id.toStdString() << ": config.json is not a JSON object ("
                              << error.errorString().toStdString() << "), kept as config.json.broken";
        QFile::remove(path + ".broken");
        QFile::copy(path, path + ".broken");
    }
}

void PluginConfig::reloadIfChanged(const QString& id, Store& store)
{
    const QFileInfo info(filePath(id));
    const qint64 modified = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    const qint64 size = info.exists() ? info.size() : -1;
    if(modified == store.modified && size == store.size)
    {
        return;
    }
    OPENAUTO_LOG(info) << "[PluginConfig] " << id.toStdString() << ": config.json changed outside OpenHU, read again";
    const QVariantMap before = this->valuesOf(store);
    this->read(id, store);
    const QVariantMap after = this->valuesOf(store);
    QStringList changed;
    for(const auto& key: before.keys() + after.keys())
    {
        if(!changed.contains(key) && before.value(key) != after.value(key))
        {
            changed.append(key);
        }
    }
    changed.sort();
    this->publish(id, store, changed);
}

QString PluginConfig::write(const QString& id, Store& store, const QVariantMap& stored, QStringList* changed)
{
    if(changed)
    {
        changed->clear();
    }
    const QVariantMap before = this->valuesOf(store);
    if(stored == store.stored)
    {
        return QString();
    }
    const QByteArray data = serialize(stored);
    if(data.size() > cMaxBytes)
    {
        return QString("config too large (%1 bytes, at most %2)").arg(data.size()).arg(cMaxBytes);
    }

    const QString path = filePath(id);
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        return "cannot write " + path + ": " + file.errorString();
    }
    // what a reload would give (numbers as JSON has them)
    store.stored = QJsonDocument::fromJson(data).object().toVariantMap();
    const QFileInfo info(path);
    store.modified = info.lastModified().toMSecsSinceEpoch();
    store.size = info.size();

    const QVariantMap after = this->valuesOf(store);
    QStringList keys;
    for(const auto& key: before.keys() + after.keys())
    {
        if(!keys.contains(key) && (before.value(key) != after.value(key) || before.contains(key) != after.contains(key)))
        {
            keys.append(key);
        }
    }
    keys.sort();
    if(changed)
    {
        *changed = keys;
    }
    if(!keys.isEmpty())
    {
        this->publish(id, store, keys);
    }
    return QString();
}

void PluginConfig::publish(const QString& id, const Store& store, const QStringList& changed) const
{
    core::EventBus::instance().publish(topic(id), {{"values", this->valuesOf(store)}, {"changed", changed}});
}

}
}
}
}
