#!/usr/bin/env bash
# qmllint (Qt 6), fails on any warning.
#
#   tests/qmllint.sh                 the shell QML
#   tests/qmllint.sh <dir>...        other QML, e.g. the pages of plugins (openhu-plugins CI)
#
# "import OpenHU" is registered from C++ at runtime; for qmllint it is rebuilt here as a module from the
# shell's Theme.qml and BusTopic.qml (AaSurface, a C++ item, as a plain Item). Not checked: unqualified
# access, because aaBridge, shell, api, ... are context properties set from C++.
set -euo pipefail

qmllint="${QMLLINT:-/usr/lib/qt6/bin/qmllint}"
root="$(cd "$(dirname "$0")/.." && pwd)"
shell_qml="$root/src/autoapp/Shell/qml"

modules="$(mktemp -d)"
trap 'rm -rf "$modules"' EXIT
mkdir -p "$modules/OpenHU"
cp "$shell_qml/module/Theme.qml" "$shell_qml/BusTopic.qml" "$modules/OpenHU/"
printf 'import QtQuick\nItem {}\n' > "$modules/OpenHU/AaSurface.qml"
cat > "$modules/OpenHU/qmldir" <<'EOF'
module OpenHU
singleton Theme 1.0 Theme.qml
BusTopic 1.0 BusTopic.qml
AaSurface 1.0 AaSurface.qml
EOF

[ $# -gt 0 ] || set -- "$shell_qml"
status=0
for dir in "$@"; do
  mapfile -t files < <(find "$dir" -name '*.qml' | sort)
  # relative paths in the output; -I . for the imports of local folders ("settings", "..")
  if (cd "$dir" && "$qmllint" -I "$modules" -I . --max-warnings 0 --unqualified disable \
        "${files[@]/#$dir\//}"); then
    echo "qmllint: ${#files[@]} files in $dir clean"
  else
    status=1
  fi
done
exit "$status"
