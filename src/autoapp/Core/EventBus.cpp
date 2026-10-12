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

#include <QThread>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

EventBus& EventBus::instance()
{
    // Never destroyed: Qt ends the process with exit() when the display connection breaks, the QML engine is
    // still alive then and its bindings would read a destroyed context property (TypeError ... of null).
    static EventBus* bus = new EventBus;
    return *bus;
}

void EventBus::publish(const QString& topic, const QVariantMap& data)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_.insert(topic, data);
    }

    this->emitEvent(topic, data);
}

void EventBus::emitEvent(const QString& topic, const QVariantMap& data)
{
    if(QThread::currentThread() == this->thread())
    {
        emit event(topic, data);
    }
    else
    {
        QMetaObject::invokeMethod(this, [this, topic, data]() { emit event(topic, data); }, Qt::QueuedConnection);
    }
}

QVariantMap EventBus::last(const QString& topic) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_.value(topic);
}

QHash<QString, QVariantMap> EventBus::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_;
}

bool EventBus::matches(const QString& pattern, const QString& topic)
{
    if(pattern == QLatin1String("*") || pattern == topic)
    {
        return true;
    }

    return pattern.endsWith(QLatin1String(".*")) && topic.startsWith(pattern.left(pattern.size() - 1));
}

}
}
}
}
