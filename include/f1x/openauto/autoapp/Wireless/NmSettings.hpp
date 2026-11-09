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

#include <functional>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace f1x::openauto::autoapp::wireless
{

// NetworkManager profiles over D-Bus (org.freedesktop.NetworkManager.Settings), for the calls nmcli cannot be used
// for: nmcli add/modify send every property its own version knows, and an older NetworkManager rejects the whole
// profile (nmcli 1.52 in the Docker image against NetworkManager 1.46 on the host:
// "802-11-wireless.mac-address-denylist: unknown property"). Only the properties OpenHU sets go over the bus here.
// The other nmcli calls (show, up, down, delete, radio, monitor) work across versions.
// Virtual for the tests. Callbacks come on the thread of this object.
class NmSettings: public QObject
{
    Q_OBJECT

public:
    // D-Bus type a{sa{sv}}: setting -> property -> value
    using Profile = QMap<QString, QVariantMap>;
    // empty error: done
    using Done = std::function<void(const QString& error)>;

    explicit NmSettings(QObject* parent = nullptr);

    // Settings.AddConnection (saved to disk)
    virtual void add(const Profile& profile, Done done);
    // the profile with this UUID: GetSettings, the changes, Update. changes: "setting.property" -> value, an
    // invalid QVariant removes the property. Secrets stay (NetworkManager keeps them when the update has none).
    virtual void modify(const QString& uuid, const QVariantMap& changes, Done done);
    // replaces the whole profile with this UUID (Update)
    virtual void replace(const QString& uuid, const Profile& profile, Done done);

    // the D-Bus types of profiles (a{sa{sv}}, aa{sv} of ipv4.address-data)
    static void registerTypes();
    // the changes applied to a profile (what modify sends)
    static Profile applied(Profile profile, const QVariantMap& changes);

private:
    // Settings.GetConnectionByUuid
    void find(const QString& uuid, std::function<void(const QString& error, const QString& path)> found);
};

}
