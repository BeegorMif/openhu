#!/usr/bin/env bash
# The translations are complete (CI, docker/dev.sh test): translations/openhu_<language>.ts contains every text ID
# of the code (otherwise run scripts/update-translations.sh) and every one is translated, English included; the code
# uses IDs only (qsTrId/qtTrId, no qsTr/tr with English texts).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

status=0
# texts in the code instead of IDs
if grep -rnE '(^|[^A-Za-z_:.>])(qsTr|tr|QT_TR_NOOP|QT_TRANSLATE_NOOP)\(' "$root/src" "$root/include" \
       --include='*.cpp' --include='*.hpp' --include='*.qml' --include='*.js'; then
    echo "texts in the code: use qsTrId()/qtTrId() with an ID (docs/translations.md)"
    status=1
fi
for ts in "$root"/translations/openhu_*.ts; do
    name="$(basename "$ts")"
    cp "$ts" "$tmp/$name"
    bash "$root/scripts/update-translations.sh" "$tmp/$name"
    if ! diff -u "$ts" "$tmp/$name" > "$tmp/$name.diff"; then
        echo "translations/$name does not match the code, run scripts/update-translations.sh:"
        head -40 "$tmp/$name.diff"
        status=1
    fi
    # source lines of the untranslated messages
    if grep -q 'type="unfinished"' "$tmp/$name"; then
        echo "translations/$name: untranslated:"
        grep -B2 'type="unfinished"' "$tmp/$name" | grep -o 'id="[^"]*"' | sed 's/^/  /'
        status=1
    fi
done
[ $status -eq 0 ] && echo "translations complete: $(cd "$root/translations" && ls openhu_*.ts | tr '\n' ' ')"
exit $status
