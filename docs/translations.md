# Translations

The shell is available in **German** and **English** (Settings → General → Language, `General.Language` in
`openhu.ini`). Switching applies at once, without a restart: the texts of the shell and of the plugins, the
date on the home page, the decimal point of distances and the layout of the on-screen keyboard. Without a
setting the language comes from the system locale if there is a translation for it; otherwise, and on a system
without a locale (`C.UTF-8` in the container and on OpenHU OS), it is English.

Texts that are kept while they apply (an error of the hotspot, the status of the car radio) change with their
next update.

## How it works

- The strings in the code are **English**: `qsTr("...")` in QML, `tr("...")` in C++ classes with `Q_OBJECT`.
  Parts that change are arguments: `qsTr("Version %1").arg(version)`, never a sentence glued together from pieces.
- `translations/openhu_<language>.ts` holds the translations (Qt Linguist format). The build compiles them with
  `lrelease` (`qt6-l10n-tools`) into the resources (`:/i18n/openhu_<language>.qm`).
- `shell::Translations` installs the one of the chosen language and the plugin translations
  (`<plugin>/i18n/<language>.qm`, [plugins.md](plugins.md#translations)), calls `QQmlEngine::retranslate()` and
  sets the default `QLocale`. Formats that differ between languages are part of the translation, e.g. the date
  `"dddd, MMMM d"` → `"dddd, d. MMMM"`.
- `tests/translations.sh` (CI, `docker/dev.sh test`) fails when a `.ts` file does not match the code or has an
  untranslated string.

## New or changed strings

```sh
scripts/update-translations.sh        # collect the strings of src/ and include/ (lupdate)
linguist translations/openhu_de.ts    # translate the new ones (or edit the .ts in a text editor)
```

A changed English source is a new string: its old translation goes away and the new one is untranslated until
it is filled in. Without Qt on the host, run the script in the container: `docker/dev.sh shell`.

## A new language

```sh
scripts/update-translations.sh --add fr     # translations/openhu_fr.ts with every string untranslated
```

Then translate it and add the language to `Translations::available()` (`src/autoapp/Shell/Translations.cpp`). The
drop-down in the settings lists every language from there, named in itself (`QLocale::nativeLanguageName()`, e.g.
"Français"). The on-screen keyboard picks its layout in `Keyboard.qml`; check that Qt Virtual Keyboard has one for
the new language and map it there.
