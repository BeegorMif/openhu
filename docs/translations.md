# Translations

The shell is available in **English** and **German** (Settings → General → Language, `General.Language` in
`openhu.ini`). Switching applies at once, without a restart: the texts of the shell and of the plugins, the date on
the home page, the decimal point of distances and the layout of the on-screen keyboard. Without a setting the
language comes from the system locale if there is a translation for it; otherwise, and on a system without a locale
(`C.UTF-8` in the container and on OpenHU OS), it is English.

Texts that are kept while they apply (an error of the hotspot, the status of the car radio) change with their
next update.

## Text IDs

The code holds IDs, not texts: `qsTrId("settings.general.language")` in QML, `qtTrId("bluetooth.no_bluetooth_adapter_found")` in
C++ (no `Q_OBJECT` needed). Every language is a translation file, English too:

| File | |
|---|---|
| `translations/openhu_en.ts` | the English texts |
| `translations/openhu_de.ts` | German |

So the English wording can be changed in `openhu_en.ts` without touching the code, and the German translation stays.

**IDs:** `<area>.<page>.<key>`, lower case, `_` inside a part, e.g. `settings.network.api_port`,
`settings.system.update_failed`, `store.checksum_does_not_match`. The area says where the text is used:

| Area | |
|---|---|
| `settings.<page>` | settings: `page` (categories, the bar at the top), `general`, `aa`, `audio`, `wireless`, `network`, `wifi`, `system`, `keys`, `info`, `error` (refused values) |
| `home`, `home.media`, `home.navigation`, `statusbar`, `dock`, `aa`, `shell` | the pages of the shell |
| `store`, `bluetooth`, `bluetooth.pairing`, `caraudio`, `wireless`, `wireless.hotspot`, `wifi`, `system`, `actions`, `video` | messages of those parts |
| `<plugin id>.` | texts of a plugin ([plugins.md](plugins.md#translations)) |

Parts that change are arguments: `qsTrId("settings.wifi.connected_to").arg(ssid)` with "Connected to “%1”", never a
sentence glued together from pieces. The same ID can be used at several places for the same text; a text that
reads differently somewhere else gets its own ID.

The build compiles the `.ts` files with `lrelease -idbased` (`qt6-l10n-tools`; Qt 6.8 still needs the flag) into the resources
(`:/i18n/openhu_<language>.qm`); `shell::Translations` installs the one of the chosen language and the plugin
translations, calls `QQmlEngine::retranslate()` and sets the default `QLocale`. In Qt 6.8 `qsTrId()` makes the
binding that calls it depend on the language, so `retranslate()` evaluates every such binding again, in the shell and
in plugin pages loaded from disk alike, whatever else the binding does (a condition, `.arg()`, an array, a function
call; `tests/test_translations.cpp`). Texts assigned once in a handler, or made in C++ and handed to QML, are not
bindings and stay as they are until their next update. Formats that differ between
languages are texts too, e.g. the date `home.date_format`: "dddd, MMMM d" / "dddd, d. MMMM". An ID without a
translation shows as the ID.

`tests/translations.sh` (CI, `docker/dev.sh test`) fails when a `.ts` file does not match the code, when an ID is
untranslated in any language (English included) or when the code has `qsTr()`/`tr()` with a text instead of an ID.

## New or changed texts

1. In the code: a new ID, `qsTrId("settings.network.new_thing")`.
2. `scripts/update-translations.sh` adds it to every `.ts` file as untranslated (and removes IDs no longer used).
3. Write the English text in `openhu_en.ts` and the others, e.g. with `linguist translations/openhu_en.ts
   translations/openhu_de.ts` (both side by side) or in a text editor.

To change only the wording, edit the text in `openhu_en.ts` (or another language); the code stays as it is. The
`.ts` files have no `<source>` texts: lupdate would move them away again, the English text lives in `openhu_en.ts`.

Without Qt on the host, run the script in the container: `docker/dev.sh shell`.

## A new language

```sh
scripts/update-translations.sh --add fr     # translations/openhu_fr.ts with every ID untranslated
```

Then translate it (next to `openhu_en.ts`) and add the language to `Translations::available()`
(`src/autoapp/Shell/Translations.cpp`). The drop-down in the settings lists every language from there, named in
itself (`QLocale::nativeLanguageName()`, e.g. "Français"). The on-screen keyboard picks its layout in
`Keyboard.qml`; check that Qt Virtual Keyboard has one for the new language.
