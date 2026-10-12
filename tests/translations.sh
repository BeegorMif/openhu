#!/usr/bin/env bash
# The translations are complete (CI, docker/dev.sh test): translations/openhu_<language>.ts contains every string
# of the code (otherwise run scripts/update-translations.sh) and every one is translated.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

status=0
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
        grep -B2 'type="unfinished"' "$tmp/$name" | grep '<source>' | sed 's/^ */  /'
        status=1
    fi
done
[ $status -eq 0 ] && echo "translations complete: $(cd "$root/translations" && ls openhu_*.ts | tr '\n' ' ')"
exit $status
