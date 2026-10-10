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

#include <mutex>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

// Central publish/subscribe hub between Android Auto services, the shell, the API server and plugins.
// Topics are dot separated ("media.metadata", "nav.turn", "vehicle.speed", ...).
// The last value of every topic is kept, so late subscribers (QML pages, API clients) get the current state.
class EventBus: public QObject
{
    Q_OBJECT

public:
    static EventBus& instance();

    // Thread safe, may be called from any thread. Subscribers are notified in the GUI thread.
    void publish(const QString& topic, const QVariantMap& data);

    Q_INVOKABLE QVariantMap last(const QString& topic) const;
    QHash<QString, QVariantMap> snapshot() const;

    // "*" matches everything, "nav.*" matches "nav.turn", exact names match themselves.
    static bool matches(const QString& pattern, const QString& topic);

signals:
    void event(const QString& topic, const QVariantMap& data);

private:
    EventBus() = default;

    mutable std::mutex mutex_;
    QHash<QString, QVariantMap> last_;
};

}
}
}
}
