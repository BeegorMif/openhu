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

#include <QString>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace shell
{

// Which page the shell shows when the Android Auto projection starts or stops (#87).
//
// The shell shows Android Auto as soon as the phone starts projecting, except on a plugin page: the reverse camera
// in page mode must stay while the phone connects or restarts its stream in reverse gear. Android Auto then waits
// and takes the screen when the plugin leaves its page through the API (the camera returns to its previous page when
// the reverse gear ends). The user choosing a page (dock, home page, toast), "Exit" inside Android Auto and the end of
// the projection stop the waiting. VideoFocusPolicy keeps the phone in NATIVE meanwhile (a plugin page when the
// session starts covers the projection).
class ProjectionPage
{
public:
    static bool isPluginPage(const QString& page)
    {
        return page.startsWith(QLatin1String("plugin:"));
    }

    // The projection video started or stopped while `page` is shown: the page to show now
    QString videoChanged(const QString& page, bool active)
    {
        waiting_ = active && isPluginPage(page);
        if(active)
        {
            return waiting_ ? page : QStringLiteral("aa");
        }
        return page == QLatin1String("aa") ? QStringLiteral("home") : page;
    }

    // ui.navigate through the API while `page` is shown: the page to show instead of `target`
    QString navigate(const QString& page, const QString& target)
    {
        if(waiting_ && target != page)
        {
            waiting_ = false;
            return QStringLiteral("aa");
        }
        return target;
    }

    // The user chose a page or left Android Auto ("Exit"): Android Auto does not take the screen later
    void chosenByUser()
    {
        waiting_ = false;
    }

    // Android Auto waits for the plugin page to be left
    bool waiting() const
    {
        return waiting_;
    }

private:
    bool waiting_ = false;
};

}
}
}
}
