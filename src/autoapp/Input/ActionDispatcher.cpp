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

#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <f1x/openauto/autoapp/Input/ActionDispatcher.hpp>
#include <f1x/openauto/autoapp/Core/EventBus.hpp>
#include <f1x/openauto/autoapp/Core/UsbReset.hpp>
#include <f1x/openauto/autoapp/Shell/AaBridge.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace input
{

namespace
{

using KeyCode = aap_protobuf::service::media::sink::message::KeyCode;

struct AaKey
{
    KeyCode code;
    projection::WheelDirection wheel;
};

// Actions that are Android Auto keys.
const QHash<QString, AaKey>& aaKeys()
{
    static const QHash<QString, AaKey> keys{
        {"media.play_pause", {KeyCode::KEYCODE_MEDIA_PLAY_PAUSE, projection::WheelDirection::NONE}},
        {"media.play", {KeyCode::KEYCODE_MEDIA_PLAY, projection::WheelDirection::NONE}},
        {"media.pause", {KeyCode::KEYCODE_MEDIA_PAUSE, projection::WheelDirection::NONE}},
        {"media.next", {KeyCode::KEYCODE_MEDIA_NEXT, projection::WheelDirection::NONE}},
        {"media.previous", {KeyCode::KEYCODE_MEDIA_PREVIOUS, projection::WheelDirection::NONE}},
        {"call.answer", {KeyCode::KEYCODE_CALL, projection::WheelDirection::NONE}},
        {"call.end", {KeyCode::KEYCODE_ENDCALL, projection::WheelDirection::NONE}},
        {"voice", {KeyCode::KEYCODE_SEARCH, projection::WheelDirection::NONE}},
        {"aa.home", {KeyCode::KEYCODE_HOME, projection::WheelDirection::NONE}},
        {"aa.back", {KeyCode::KEYCODE_BACK, projection::WheelDirection::NONE}},
        {"aa.navigation", {KeyCode::KEYCODE_NAVIGATION, projection::WheelDirection::NONE}},
        {"aa.media", {KeyCode::KEYCODE_MEDIA, projection::WheelDirection::NONE}},
        {"aa.phone", {KeyCode::KEYCODE_TEL, projection::WheelDirection::NONE}},
        {"dpad.up", {KeyCode::KEYCODE_DPAD_UP, projection::WheelDirection::NONE}},
        {"dpad.down", {KeyCode::KEYCODE_DPAD_DOWN, projection::WheelDirection::NONE}},
        {"dpad.left", {KeyCode::KEYCODE_DPAD_LEFT, projection::WheelDirection::NONE}},
        {"dpad.right", {KeyCode::KEYCODE_DPAD_RIGHT, projection::WheelDirection::NONE}},
        {"dpad.enter", {KeyCode::KEYCODE_DPAD_CENTER, projection::WheelDirection::NONE}},
        {"rotary.left", {KeyCode::KEYCODE_ROTARY_CONTROLLER, projection::WheelDirection::LEFT}},
        {"rotary.right", {KeyCode::KEYCODE_ROTARY_CONTROLLER, projection::WheelDirection::RIGHT}},
    };
    return keys;
}

// Default keyboard layout (same keys as the classic OpenAuto input device plus media/volume keys).
const QVariantMap& defaultKeymap()
{
    static const QVariantMap keymap{
        {"Return", "dpad.enter"}, {"Enter", "dpad.enter"},
        {"Left", "dpad.left"}, {"Right", "dpad.right"}, {"Up", "dpad.up"}, {"Down", "dpad.down"},
        {"Esc", "ui.back"}, {"H", "aa.home"}, {"P", "call.answer"}, {"O", "call.end"},
        {"X", "media.play"}, {"C", "media.pause"}, {"V", "media.previous"}, {"B", "media.play_pause"},
        {"N", "media.next"}, {"M", "voice"}, {"1", "rotary.left"}, {"2", "rotary.right"},
        {"F", "aa.navigation"}, {"J", "aa.media"}, {"G", "aa.phone"},
        {"Media Play", "media.play"}, {"Media Pause", "media.pause"}, {"Toggle Media Play/Pause", "media.play_pause"},
        {"Media Next", "media.next"}, {"Media Previous", "media.previous"},
        {"Volume Up", "volume.up"}, {"Volume Down", "volume.down"}, {"Volume Mute", "volume.mute"},
        {"+", "volume.up"}, {"-", "volume.down"},
    };
    return keymap;
}

}

ActionDispatcher::ActionDispatcher(audio::VolumeController& volume, shell::ShellController& shell, QObject* parent)
    : QObject(parent)
    , volume_(volume)
    , shell_(shell)
{
    this->loadKeymap();
}

QVariantMap ActionDispatcher::actions()
{
    return {
        {"media.play_pause", "Wiedergabe/Pause (Android Auto)"},
        {"media.play", "Wiedergabe"}, {"media.pause", "Pause"},
        {"media.next", "Nächster Titel"}, {"media.previous", "Vorheriger Titel"},
        {"call.answer", "Anruf annehmen"}, {"call.end", "Anruf beenden/ablehnen"},
        {"voice", "Sprachassistent"},
        {"aa.home", "Android Auto: Startbildschirm"}, {"aa.back", "Android Auto: Zurück"},
        {"aa.navigation", "Android Auto: Navigation"}, {"aa.media", "Android Auto: Medien"},
        {"aa.phone", "Android Auto: Telefon"},
        {"dpad.up", "Hoch"}, {"dpad.down", "Runter"}, {"dpad.left", "Links"}, {"dpad.right", "Rechts"},
        {"dpad.enter", "Auswählen"}, {"rotary.left", "Drehregler links"}, {"rotary.right", "Drehregler rechts"},
        {"volume.up", "Lauter"}, {"volume.down", "Leiser"}, {"volume.mute", "Stumm an/aus"},
        {"ui.home", "Shell: Startseite"}, {"ui.aa", "Shell: Android Auto anzeigen"},
        {"ui.settings", "Shell: Einstellungen"},
        {"ui.back", "Zurück (Android Auto oder Shell)"},
        {"aa.reconnect", "Telefon per USB-Reset neu verbinden"},
    };
}

bool ActionDispatcher::trigger(const QString& action, const QString& phase)
{
    if(phase == QLatin1String("press"))
    {
        return this->trigger(action, Phase::Press);
    }
    if(phase == QLatin1String("release"))
    {
        return this->trigger(action, Phase::Release);
    }
    return this->trigger(action, Phase::Click);
}

bool ActionDispatcher::trigger(const QString& action, Phase phase)
{
    if(action.isEmpty())
    {
        return false;
    }

    const QString phaseName = phase == Phase::Press ? "press" : phase == Phase::Release ? "release" : "click";
    // One-shot event: a (re)connecting plugin must not receive the last action again
    core::EventBus::instance().emitEvent("input.action", {{"action", action}, {"phase", phaseName}});

    // Shell actions run once per click/press.
    const bool once = phase != Phase::Release;

    if(action == QLatin1String("volume.up"))
    {
        if(once) volume_.changeVolume(5);
        return true;
    }
    if(action == QLatin1String("volume.down"))
    {
        if(once) volume_.changeVolume(-5);
        return true;
    }
    if(action == QLatin1String("volume.mute"))
    {
        if(once) volume_.toggleMute();
        return true;
    }
    if(action == QLatin1String("ui.home"))
    {
        if(once) shell_.navigate("home");
        return true;
    }
    if(action == QLatin1String("ui.aa"))
    {
        if(once) shell_.navigate("aa");
        return true;
    }
    if(action == QLatin1String("ui.settings"))
    {
        if(once) shell_.navigate("settings");
        return true;
    }
    if(action == QLatin1String("ui.back"))
    {
        // Back inside Android Auto while it is shown, otherwise back to the shell's home page.
        if(shell::AaBridge::instance().surfaceVisible())
        {
            return this->sendAaKey("aa.back", phase);
        }
        if(once) shell_.navigate("home");
        return true;
    }

    if(action == QLatin1String("aa.reconnect"))
    {
        if(once)
        {
            const auto devices = core::resetAndroidUsbDevices();
            shell_.toast(devices.isEmpty() ? "Kein Telefon am USB gefunden" : "Telefon wird neu verbunden …", 3000);
        }
        return true;
    }

    if(aaKeys().contains(action))
    {
        return this->sendAaKey(action, phase);
    }

    // Unknown here: plugins may handle it via the "input.action" event.
    return true;
}

bool ActionDispatcher::sendAaKey(const QString& action, Phase phase)
{
    const AaKey key = aaKeys().value(action);
    auto& bridge = shell::AaBridge::instance();

    if(key.code == KeyCode::KEYCODE_ROTARY_CONTROLLER)
    {
        // Wheel steps have no press/release, one step per click/release.
        if(phase == Phase::Press)
        {
            return true;
        }
        return bridge.sendButton({projection::ButtonEventType::NONE, key.wheel, key.code});
    }

    bool sent = true;
    if(phase != Phase::Release)
    {
        sent = bridge.sendButton({projection::ButtonEventType::PRESS, key.wheel, key.code});
    }
    if(phase != Phase::Press)
    {
        sent = bridge.sendButton({projection::ButtonEventType::RELEASE, key.wheel, key.code}) && sent;
    }
    return sent;
}

void ActionDispatcher::installKeyboardHandler()
{
    QCoreApplication::instance()->installEventFilter(this);
}

QVariantMap ActionDispatcher::keymap() const
{
    QVariantMap result;
    for(auto it = keymap_.cbegin(); it != keymap_.cend(); ++it)
    {
        result.insert(keyName(it.key()), it.value());
    }
    return result;
}

QVariantList ActionDispatcher::keymapList() const
{
    QVariantList list;
    const QVariantMap map = this->keymap();
    for(auto it = map.cbegin(); it != map.cend(); ++it)
    {
        list.append(QVariantMap{{"key", it.key()}, {"action", it.value()}, {"custom", overrides_.contains(it.key())}});
    }
    return list;
}

void ActionDispatcher::setKeyAction(const QString& key, const QString& action)
{
    const QString name = keyName(QKeySequence(key)[0].key());
    if(name.isEmpty())
    {
        return;
    }

    // Back to the default: no override needed
    if(defaultKeymap().value(name).toString() == action)
    {
        overrides_.remove(name);
    }
    else
    {
        overrides_.insert(name, action);
    }

    this->saveOverrides();
    this->loadKeymap();
    emit keymapChanged();
}

void ActionDispatcher::removeKey(const QString& key)
{
    this->setKeyAction(key, QString());
}

void ActionDispatcher::resetKeymap()
{
    overrides_.clear();
    this->saveOverrides();
    this->loadKeymap();
    emit keymapChanged();
}

void ActionDispatcher::captureKey(bool enabled)
{
    capturing_ = enabled;
}

QString ActionDispatcher::keyName(int key)
{
    return QKeySequence(key).toString(QKeySequence::PortableText);
}

void ActionDispatcher::loadKeymap()
{
    // keymap.json: {"N": "media.next", "F5": "camera.toggle", "H": ""} - entries replace the defaults,
    // an empty action removes a key.
    QFile file(QDir::homePath() + "/.openhu/keymap.json");
    if(file.open(QIODevice::ReadOnly))
    {
        overrides_.clear();
        const auto custom = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        for(auto it = custom.cbegin(); it != custom.cend(); ++it)
        {
            // normalize the key names ("Volume Up", "volume up" -> "Volume Up")
            const QKeySequence sequence(it.key());
            if(!sequence.isEmpty())
            {
                overrides_.insert(keyName(sequence[0].key()), it.value());
            }
        }
    }

    QVariantMap keymap;
    const QVariantMap defaults = defaultKeymap();
    for(auto it = defaults.cbegin(); it != defaults.cend(); ++it)
    {
        keymap.insert(keyName(QKeySequence(it.key())[0].key()), it.value());
    }
    for(auto it = overrides_.cbegin(); it != overrides_.cend(); ++it)
    {
        keymap.insert(it.key(), it.value());
    }

    keymap_.clear();
    for(auto it = keymap.cbegin(); it != keymap.cend(); ++it)
    {
        const QKeySequence sequence(it.key());
        const QString action = it.value().toString();
        if(!sequence.isEmpty() && !action.isEmpty())
        {
            keymap_.insert(sequence[0].key(), action);
        }
    }
}

void ActionDispatcher::saveOverrides() const
{
    QDir().mkpath(QDir::homePath() + "/.openhu");
    QFile file(QDir::homePath() + "/.openhu/keymap.json");
    if(overrides_.isEmpty())
    {
        file.remove();
    }
    else if(file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(overrides_)).toJson());
    }
}

bool ActionDispatcher::eventFilter(QObject* obj, QEvent* event)
{
    if((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && obj->isWindowType())
    {
        // Every window gets the event, handle it only once (for the focused window).
        auto* key = static_cast<QKeyEvent*>(event);

        if(capturing_)
        {
            if(event->type() == QEvent::KeyPress && !key->isAutoRepeat() && key->key() != 0)
            {
                capturing_ = false;
                emit keyCaptured(keyName(key->key()));
            }
            return true;
        }

        // Typing in a text field: all keys belong to the field, except media and volume keys
        QObject* focus = QGuiApplication::focusObject();
        const bool textInput = focus != nullptr && focus->metaObject()->indexOfProperty("cursorPosition") >= 0;
        const bool mediaKey = (key->key() >= Qt::Key_VolumeDown && key->key() <= Qt::Key_MediaLast)
                              || key->key() == Qt::Key_MediaTogglePlayPause;
        if(textInput && !mediaKey)
        {
            return QObject::eventFilter(obj, event);
        }

        const auto it = keymap_.constFind(key->key());
        if(it != keymap_.constEnd())
        {
            if(!key->isAutoRepeat())
            {
                this->trigger(it.value(), event->type() == QEvent::KeyPress ? Phase::Press : Phase::Release);
            }
            return true;
        }
    }

    return QObject::eventFilter(obj, event);
}

}
}
}
}
