#!/usr/bin/env bash
# Kurzform für ./dev.sh run: inkrementell bauen und OpenHU starten (Optionen siehe dev.sh)
exec "$(dirname "$0")/dev.sh" run "$@"
