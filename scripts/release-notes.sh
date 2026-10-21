#!/usr/bin/env bash
# Release notes of an OpenHU release in Markdown on stdout: the version, the aasdk it was built against
# and the pull requests merged since the previous release.
#
#   scripts/release-notes.sh VERSION PACKAGE_DIR [PREVIOUS_TAG]
#
# PACKAGE_DIR holds the .deb files of the release (openhu and libaasdk); PREVIOUS_TAG defaults to the
# newest release before vVERSION. Needs gh (GH_TOKEN in CI).
set -euo pipefail

VERSION="${1:?usage: $0 VERSION PACKAGE_DIR [PREVIOUS_TAG]}"
DIR="${2:?usage: $0 VERSION PACKAGE_DIR [PREVIOUS_TAG]}"
REPO=BeegorMif/openhu

PREVIOUS="${3:-$(gh release list -R "$REPO" --exclude-drafts --limit 50 --json tagName -q '.[].tagName' |
                 grep -vx "v$VERSION" | head -1 || true)}"
PREVIOUS_DATE=""
[ -n "$PREVIOUS" ] && PREVIOUS_DATE="$(gh release view "$PREVIOUS" -R "$REPO" --json publishedAt -q .publishedAt)"

# libaasdk version the packages were built against (same for every architecture)
# (libaasdk_2.0.0_deb13u1_amd64.deb or libaasdk_2.0.0-1_amd64.deb)
aasdk="$(ls "$DIR"/libaasdk_*.deb 2>/dev/null | head -1 | xargs -r basename |
           sed -nE 's/^libaasdk_([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' || true)"

echo "OpenHU $VERSION for Debian 13 \"trixie\", amd64 and arm64."
echo
echo "- **OpenHU** $VERSION"
if [ -n "$aasdk" ]; then
  echo "- built against **libaasdk** [$aasdk](https://github.com/BeegorMif/aasdk/releases/tag/openhu-v$aasdk)" \
       "(the package depends on libaasdk ${aasdk%.*}.x)"
fi
echo

if [ -n "$PREVIOUS_DATE" ]; then
  echo "## Changes since $PREVIOUS"
  echo
  prs="$(gh pr list -R "$REPO" --state merged --limit 200 --search "merged:>$PREVIOUS_DATE" \
             --json number,title,url -q '.[] | "- \(.title) ([#\(.number)](\(.url)))"' | grep -viE '^- aasdk [0-9.]+: depend' || true)"
  echo "${prs:-No pull requests merged.}"
  echo
else
  echo "## First release"
  echo
  echo "Merged pull requests so far: https://github.com/$REPO/pulls?q=is%3Apr+is%3Amerged"
  echo
fi

echo "## Installation"
echo
echo "OpenHU OS contains OpenHU already. On another Debian 13: install the openhu and libaasdk packages of your"
echo "architecture together, e.g. \`sudo apt install ./openhu_*_amd64.deb ./libaasdk_*_amd64.deb\`. The package"
echo "starts nothing by itself: see [Start](https://github.com/$REPO/blob/v$VERSION/docs/installation.md#start)."
echo
echo "Check the downloads against \`SHA256SUMS\`: \`sha256sum -c SHA256SUMS --ignore-missing\`."
