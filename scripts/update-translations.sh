#!/usr/bin/env bash
# Brings translations/openhu_<language>.ts up to date with the text IDs in the code (qsTrId/qtTrId): new IDs come in
# as unfinished, removed ones go. Every language is a translation, English too (openhu_en.ts holds the English
# texts). Translate with Qt Linguist (open openhu_en.ts next to the language) or in a text editor, see
# docs/translations.md.
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
    # no file/line locations: a moved text does not change the .ts files. The IDs have no source text in the code
    # (the English one lives in openhu_en.ts), lupdate warns about every one of them.
    "$lupdate" -silent -locations none -no-obsolete "${lang[@]}" \
        -extensions cpp,hpp,qml,js -I include src include -ts "$ts" 2>&1 | { grep -v "has no source" || true; } >&2
done
