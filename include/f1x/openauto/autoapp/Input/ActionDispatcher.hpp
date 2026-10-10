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

#include <QHash>
#include <QObject>
#include <QStringList>
#include <f1x/openauto/autoapp/Audio/VolumeController.hpp>
#include <f1x/openauto/autoapp/Shell/ShellController.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace input
{

// Named actions ("media.next", "volume.up", "call.answer", ...) from any input source: keyboard,
// steering wheel/CAN plugins and the API. Each action is executed as an Android Auto key or a shell
// function and is also published as "input.action" so plugins can implement their own actions
// (e.g. "camera.toggle").
class ActionDispatcher: public QObject
{
    Q_OBJECT

public:
    enum class Phase
    {
        Click,      // press + release
        Press,
        Release
    };

    ActionDispatcher(audio::VolumeController& volume, shell::ShellController& shell, QObject* parent = nullptr);

    // False if the action is unknown and no plugin may handle it either (empty name).
    Q_INVOKABLE bool trigger(const QString& action, const QString& phase = QStringLiteral("click"));
    bool trigger(const QString& action, Phase phase);

    // Built-in actions with a short description.
    static QVariantMap actions();

    // Shell mode: keyboard -> actions according to the key map (~/.openauto/keymap.json or defaults).
    void installKeyboardHandler();
    QVariantMap keymap() const;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    bool sendAaKey(const QString& action, Phase phase);
    void loadKeymap();

    audio::VolumeController& volume_;
    shell::ShellController& shell_;
    QHash<int, QString> keymap_;
};

}
}
}
}
