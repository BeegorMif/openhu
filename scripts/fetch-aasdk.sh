#!/usr/bin/env bash
# Downloads libaasdk and libaasdk-dev from the newest aasdk release of the OpenHU fork that fits
# OPENHU_AASDK_VERSION in CMakeLists.txt (same major version, at least that minor version).
#
#   scripts/fetch-aasdk.sh <dir> [arch]    arch defaults to dpkg --print-architecture
#
# Prints the chosen version on stdout. GH_TOKEN (optional) avoids the API rate limit in CI.
set -euo pipefail

dest="${1:?usage: $0 <dir> [arch]}"
arch="${2:-$(dpkg --print-architecture)}"
repo="${AASDK_REPO:-average-web-dev/aasdk}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

want="$(sed -nE 's/^set\(OPENHU_AASDK_VERSION ([0-9]+\.[0-9]+)\)/\1/p' "$root/CMakeLists.txt")"
[ -n "$want" ] || { echo "OPENHU_AASDK_VERSION not found in CMakeLists.txt" >&2; exit 1; }

auth=()
[ -n "${GH_TOKEN:-}" ] && auth=(-H "Authorization: Bearer $GH_TOKEN")
# GitHub answers 502/503 now and then: try again instead of failing the build
retry=(--retry 5 --retry-delay 5 --retry-all-errors)

# Line 1: version, then one download URL per package
mapfile -t release < <(
  curl -fsSL "${retry[@]}" "${auth[@]}" -H "Accept: application/vnd.github+json" \
      "https://api.github.com/repos/$repo/releases?per_page=100" |
  python3 -c '
import json, re, sys
major, minor = map(int, sys.argv[1].split("."))
arch = sys.argv[2]
best = None
for r in json.load(sys.stdin):
    m = re.fullmatch(r"openhu-v(\d+)\.(\d+)\.(\d+)", r["tag_name"])
    if r["draft"] or r["prerelease"] or not m:
        continue
    v = tuple(map(int, m.groups()))
    if v[0] != major or v[1] < minor or (best and v <= best[0]):
        continue
    urls = [a["browser_download_url"] for a in r["assets"]
            if re.fullmatch(r"libaasdk(-dev)?_.*_" + re.escape(arch) + r"\.deb", a["name"])]
    if len(urls) == 2:
        best = (v, urls)
if best:
    print(".".join(map(str, best[0])))
    print("\n".join(best[1]))
' "$want" "$arch"
)

if [ "${#release[@]}" -eq 0 ]; then
  echo "No aasdk release openhu-v${want%%.*}.x (>= $want) with $arch packages in $repo" >&2
  exit 1
fi

mkdir -p "$dest"
for url in "${release[@]:1}"; do
  curl -fsSL "${retry[@]}" -o "$dest/$(basename "$url")" "$url"
done
echo "${release[0]}"
