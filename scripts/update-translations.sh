#!/usr/bin/env bash
# Brings translations/openhu_<language>.ts up to date with the strings in the code (qsTr/tr, English):
# new strings come in as unfinished, removed ones go. Translate them afterwards with Qt Linguist
# (linguist translations/openhu_de.ts) or in a text editor, see docs/translations.md.
#
#   scripts/update-translations.sh              all languages
#   scripts/update-translations.sh <file.ts>... these files (tests/translations.sh: copies)
#   scripts/update-translations.sh --add fr     start a new language (translations/openhu_fr.ts)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
lupdate="${LUPDATE:-/usr/lib/qt6/bin/lupdate}"

if [ "${1:-}" = "--add" ]; then
    lang=("-target-language" "$2")
    set -- "$root/translations/openhu_$2.ts"
    [ -e "$1" ] && { echo "$1 exists already" >&2; exit 1; }
else
    lang=()
fi
[ $# -gt 0 ] || set -- "$root"/translations/openhu_*.ts

cd "$root"
for ts in "$@"; do
    # no file/line locations: a moved string does not change the .ts files
    "$lupdate" -silent -locations none -no-obsolete -source-language en "${lang[@]}" \
        -extensions cpp,hpp,qml,js -I include src include -ts "$ts"
done
