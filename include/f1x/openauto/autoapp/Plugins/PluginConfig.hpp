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

#pragma once

#include <map>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace plugins
{

// Config store of the plugins (openhu#152, docs/plugins.md#config-store): one JSON object per plugin in
// ~/.openhu/plugin-data/<id>/config.json, owned by OpenHU. Its top-level keys are either declared in the manifest's
// "settings" (typed, checked, with a default, shown in Settings → Apps) or free (any JSON value, the plugin's own).
// Every change is written at once (atomically) and published as "plugin.config.<id>" {values, changed}.
// A file changed by someone else (by hand, a plugin that still writes it itself) is read again before the next
// change instead of being overwritten.
class PluginConfig
{
public:
    static constexpr qint64 cMaxBytes = 1024 * 1024;
    static constexpr int cMaxTextLength = 4096;

    // the "settings" of a manifest, checked and completed (step, maxLength, applies, ...); invalid entries are left
    // out, errors says why
    static QVariantList parseSchema(const QVariantMap& manifest, QStringList* errors = nullptr);
    // a value for a declared setting in its type; text is converted ("5", "on"). Empty or the reason.
    static QString convert(const QVariantMap& entry, const QVariant& value, QVariant* result);
    static QString filePath(const QString& id);
    static QString topic(const QString& id);

    // reads the plugin's file and publishes its values
    void load(const QString& id, const QVariantList& schema);
    void unload(const QString& id);
    void clear();
    bool contains(const QString& id) const;
    QVariantList schema(const QString& id) const;
    // the schema entry of a declared key (or section), empty for a free key
    QVariantMap entry(const QString& id, const QString& key) const;

    // all values: the stored ones, declared keys with their default where nothing (valid) is stored
    QVariantMap values(const QString& id);
    // sets several keys at once, all or none. Empty or the reason; changed gets the keys whose value changed.
    QString set(const QString& id, const QVariantMap& changes, QStringList* changed = nullptr);
    // a free key is deleted, a declared one goes back to its default
    QString remove(const QString& id, const QStringList& keys, QStringList* changed = nullptr);
    // all declared keys back to their defaults, free keys stay
    QString reset(const QString& id, QStringList* changed = nullptr);

private:
    struct Store
    {
        QVariantList schema;
        QVariantMap stored;  // what is in the file
        qint64 modified = -1;
        qint64 size = -1;
    };

    QVariantMap valuesOf(const Store& store) const;
    void read(const QString& id, Store& store);
    // the file was changed by someone else: read it again and publish
    void reloadIfChanged(const QString& id, Store& store);
    QString write(const QString& id, Store& store, const QVariantMap& stored, QStringList* changed);
    void publish(const QString& id, const Store& store, const QStringList& changed) const;

    std::map<QString, Store> stores_;
};

}
}
}
}
