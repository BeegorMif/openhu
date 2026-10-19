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

#include <atomic>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>
#include <f1x/openauto/autoapp/Core/LogLevel.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace core
{

namespace
{

std::atomic<int> current{boost::log::trivial::info};

}

const QStringList& logLevels()
{
    static const QStringList levels{"trace", "debug", "info", "warning", "error", "fatal"};
    return levels;
}

bool setLogLevel(const QString& level)
{
    if(!logLevels().contains(level))
    {
        return false;
    }
    boost::log::trivial::severity_level severity = boost::log::trivial::info;
    const std::string name = level.toStdString();
    boost::log::trivial::from_string(name.c_str(), name.size(), severity);
    current = severity;
    boost::log::core::get()->set_filter(boost::log::trivial::severity >= severity);
    return true;
}

QString logLevel()
{
    return QString::fromLatin1(boost::log::trivial::to_string(static_cast<boost::log::trivial::severity_level>(current.load())));
}

}
}
}
}
