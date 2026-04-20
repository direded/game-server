#!/usr/bin/env bash
# premake_linux.sh — generate gmake2 Makefiles into build/ from premake5.lua.
# Mirrors _premake5/premake_win.bat. After this runs:
#   make -C build config=debug_x64 -j$(nproc)
# produces bin/Debug/game-server and bin/Debug/game-server-tests.

set -euo pipefail
cd "$(dirname "$0")/.."

PREMAKE="$(pwd)/_premake5/bin/linux/premake5"
if [[ ! -x "$PREMAKE" ]]; then
    echo "premake5 not found at $PREMAKE. Run scripts/setup.sh first." >&2
    exit 1
fi

"$PREMAKE" --file=premake5.lua gmake2
