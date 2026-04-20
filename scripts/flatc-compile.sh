#!/usr/bin/env bash
# flatc-compile.sh — regenerate C++ headers from ../protocol/*.fbs if any schema
# file is newer than its generated header. Invoked as a premake pre-build step
# on Linux. The PowerShell sibling (flatc-compile.ps1) does the same on Windows.

set -euo pipefail

SERVER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UMBRELLA_ROOT="$(cd "$SERVER_ROOT/.." && pwd)"
PROTOCOL_DIR="$UMBRELLA_ROOT/protocol"
FLATC="$SERVER_ROOT/_tools/flatc"
OUT_DIR="$SERVER_ROOT/src/protocol/generated"

if [[ ! -x "$FLATC" ]]; then
    echo "flatc not found at $FLATC. Run scripts/setup.sh first." >&2
    exit 1
fi

if [[ ! -d "$PROTOCOL_DIR" ]]; then
    echo "protocol/ not found at $PROTOCOL_DIR. Did 'git submodule update --init' run?" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"

# Collect .fbs files (newline-delimited so we can iterate).
mapfile -t fbs_files < <(find "$PROTOCOL_DIR" -maxdepth 1 -type f -name '*.fbs' | sort)
if [[ ${#fbs_files[@]} -eq 0 ]]; then
    echo "flatc-compile: no .fbs files in $PROTOCOL_DIR; nothing to do."
    exit 0
fi

# Incremental: regenerate if any generated header is missing or older than the
# newest .fbs (matches the PowerShell version's behavior).
newest_fbs_mtime=0
for f in "${fbs_files[@]}"; do
    m=$(stat -c %Y "$f")
    (( m > newest_fbs_mtime )) && newest_fbs_mtime=$m
done

needs_regen=false
for f in "${fbs_files[@]}"; do
    base="$(basename "$f" .fbs)"
    gen="$OUT_DIR/${base}_generated.h"
    if [[ ! -f "$gen" ]]; then
        needs_regen=true
        break
    fi
    gen_mtime=$(stat -c %Y "$gen")
    if (( newest_fbs_mtime > gen_mtime )); then
        needs_regen=true
        break
    fi
done

if [[ "$needs_regen" == "false" ]]; then
    echo "flatc-compile: generated headers up-to-date (${#fbs_files[@]} schema(s))."
    exit 0
fi

echo "flatc-compile: regenerating ${#fbs_files[@]} schema(s) -> $OUT_DIR"
"$FLATC" --cpp -o "$OUT_DIR" "${fbs_files[@]}"
echo "flatc-compile: done."
