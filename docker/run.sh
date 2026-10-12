#!/usr/bin/env bash
# Short for ./dev.sh run: build incrementally and start OpenHU (options: see dev.sh)
exec "$(dirname "$0")/dev.sh" run "$@"
