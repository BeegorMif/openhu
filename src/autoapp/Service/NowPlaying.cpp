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


#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Service/NowPlaying.hpp>

namespace f1x::openauto::autoapp::service
{

NowPlaying::NowPlaying(QObject* parent)
    : QObject(parent)
{
    auto& bus = core::EventBus::instance();
    // what was published before (only the last of each topic is known)
    for(const QString topic: {QStringLiteral("media.metadata"), QStringLiteral("media.status")})
    {
        this->update(topic, bus.last(topic), bus.source(topic));
    }
    if(!published_.isEmpty())
    {
        bus.publish("media.current", published_);
    }
    connect(&bus, &core::EventBus::event, this, [this](const QString& topic, const QVariantMap& data) {
        if(this->update(topic, data, core::EventBus::instance().source(topic)))
        {
            core::EventBus::instance().publish("media.current", published_);
        }
    });
}

bool NowPlaying::update(const QString& topic, const QVariantMap& data, const QString& publisher)
{
    const bool status = topic == QLatin1String("media.status");
    if((!status && topic != QLatin1String("media.metadata")) || data.isEmpty())
    {
        return false;
    }
    // Android Auto's events always carry "androidauto"; an API client without a source counts as the phone
    QString source = data.value("source").toString();
    if(source.isEmpty())
    {
        source = QStringLiteral("androidauto");
    }
    if(publisher.startsWith(QLatin1String("plugin:")))
    {
        plugins_.insert(source, publisher.mid(7));
    }
    if(status)
    {
        statuses_.insert(source, data);
        QVariantMap withSource = data;
        withSource.insert("source", source);
        policy_.update(withSource);
        latest_ = source;
    }
    else
    {
        metadata_.insert(source, data);
    }
    const QVariantMap now = this->current();
    if(now == published_)
    {
        return false;
    }
    published_ = now;
    return true;
}

QVariantMap NowPlaying::current() const
{
    QString source = policy_.owner();
    if(source.isEmpty())
    {
        source = latest_;
    }
    if(source.isEmpty())
    {
        return QVariantMap();
    }
    return {{"source", source}, {"plugin", plugins_.value(source)}, {"metadata", metadata_.value(source)},
            {"status", statuses_.value(source)}};
}

}
