#!/usr/bin/env bash
# setup.sh — Linux equivalent of setup.ps1.
#
# Fetches premake5 and the vendored deps that the build system expects under
# vendor/. libpq and libsodium are NOT vendored on Linux: they're standard
# distro packages on every Linux that matters, and managing two parallel
# vendoring stories (Windows MSVC prebuilts vs Linux source builds) is more
# fragile than just relying on apt/dnf/pacman + pkg-config.
#
# Usage:
#   chmod +x scripts/setup.sh && ./scripts/setup.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR="$ROOT/vendor"
PREMAKE_DIR="$ROOT/_premake5/bin/linux"
TOOLS_DIR="$ROOT/_tools"

echo "=== Game Server — Dependency Setup (Linux) ==="
echo "Vendor directory:  $VENDOR"
echo "Premake directory: $PREMAKE_DIR"
echo "Tools directory:   $TOOLS_DIR"

mkdir -p "$VENDOR" "$PREMAKE_DIR" "$TOOLS_DIR"

# --- helpers --------------------------------------------------------------

# Download with curl (preferred) or wget.
fetch() {
    local url="$1" out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL --retry 3 --retry-delay 5 -o "$out" "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$out" "$url"
    else
        echo "Need curl or wget to download $url" >&2
        return 1
    fi
}

require_cmd() {
    local cmd="$1" hint="$2"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Missing required command: $cmd" >&2
        echo "  $hint" >&2
        return 1
    fi
}

# --- 0. system prerequisites ---------------------------------------------
# We need a C++23-capable toolchain, make, plus headers/libs for libpq and
# libsodium. These are not vendored on Linux.
echo
echo "[0/8] Checking system toolchain and -dev packages..."
require_cmd cc       "install build-essential (Debian/Ubuntu) or base-devel (Arch)"
require_cmd make     "install build-essential / base-devel"
require_cmd unzip    "install unzip"
require_cmd pg_config "install libpq-dev (Debian/Ubuntu) or postgresql-libs (Arch)"
if ! pkg-config --exists libsodium 2>/dev/null; then
    if ! [[ -f /usr/include/sodium.h || -f /usr/local/include/sodium.h ]]; then
        echo "libsodium development headers not found." >&2
        echo "  Debian/Ubuntu: sudo apt install libsodium-dev" >&2
        echo "  Arch:          sudo pacman -S libsodium" >&2
        exit 1
    fi
fi
echo "  Toolchain + libpq + libsodium OK."

# --- 1. premake5 ---------------------------------------------------------
PREMAKE_VERSION="5.0.0-beta5"
PREMAKE_BIN="$PREMAKE_DIR/premake5"

if [[ ! -x "$PREMAKE_BIN" ]]; then
    echo
    echo "[1/8] Downloading Premake5 v$PREMAKE_VERSION..."
    tar="$PREMAKE_DIR/premake.tar.gz"
    fetch "https://github.com/premake/premake-core/releases/download/v$PREMAKE_VERSION/premake-$PREMAKE_VERSION-linux.tar.gz" "$tar"
    tar -xzf "$tar" -C "$PREMAKE_DIR"
    rm -f "$tar"
    chmod +x "$PREMAKE_BIN"
    echo "  Premake5 ready"
else
    echo "[1/8] Premake5 already present"
fi

# --- 2. Quill ------------------------------------------------------------
QUILL_VERSION="11.1.0"
QUILL_DIR="$VENDOR/quill"

if [[ ! -f "$QUILL_DIR/include/quill/Backend.h" ]]; then
    echo
    echo "[2/8] Downloading Quill v$QUILL_VERSION..."
    zip="$VENDOR/quill.zip"
    fetch "https://github.com/odygrd/quill/archive/refs/tags/v$QUILL_VERSION.zip" "$zip"
    unzip -q -o "$zip" -d "$VENDOR"
    rm -rf "$QUILL_DIR"
    mv "$VENDOR/quill-$QUILL_VERSION" "$QUILL_DIR"
    rm -f "$zip"
    echo "  Quill ready"
else
    echo "[2/8] Quill already present"
fi

# --- 3. yaml-cpp ---------------------------------------------------------
YAMLCPP_VERSION="0.8.0"
YAMLCPP_DIR="$VENDOR/yaml-cpp"

if [[ ! -f "$YAMLCPP_DIR/include/yaml-cpp/yaml.h" ]]; then
    echo
    echo "[3/8] Downloading yaml-cpp v$YAMLCPP_VERSION..."
    zip="$VENDOR/yaml-cpp.zip"
    fetch "https://github.com/jbeder/yaml-cpp/archive/refs/tags/$YAMLCPP_VERSION.zip" "$zip"
    unzip -q -o "$zip" -d "$VENDOR"
    rm -rf "$YAMLCPP_DIR"
    mv "$VENDOR/yaml-cpp-$YAMLCPP_VERSION" "$YAMLCPP_DIR"
    rm -f "$zip"
    echo "  yaml-cpp ready"
else
    echo "[3/8] yaml-cpp already present"
fi

# --- 4. Google Test ------------------------------------------------------
GTEST_VERSION="1.15.2"
GTEST_DIR="$VENDOR/googletest"

if [[ ! -f "$GTEST_DIR/googletest/include/gtest/gtest.h" ]]; then
    echo
    echo "[4/8] Downloading Google Test v$GTEST_VERSION..."
    zip="$VENDOR/googletest.zip"
    fetch "https://github.com/google/googletest/archive/refs/tags/v$GTEST_VERSION.zip" "$zip"
    unzip -q -o "$zip" -d "$VENDOR"
    rm -rf "$GTEST_DIR"
    mv "$VENDOR/googletest-$GTEST_VERSION" "$GTEST_DIR"
    rm -f "$zip"
    echo "  Google Test ready"
else
    echo "[4/8] Google Test already present"
fi

# --- 5. dbmate -----------------------------------------------------------
DBMATE_VERSION="2.22.0"
DBMATE_BIN="$TOOLS_DIR/dbmate"

if [[ ! -x "$DBMATE_BIN" ]]; then
    echo
    echo "[5/8] Downloading dbmate v$DBMATE_VERSION..."
    fetch "https://github.com/amacneil/dbmate/releases/download/v$DBMATE_VERSION/dbmate-linux-amd64" "$DBMATE_BIN"
    chmod +x "$DBMATE_BIN"
    echo "  dbmate ready"
else
    echo "[5/8] dbmate already present"
fi

# --- 6. flatc + FlatBuffers headers --------------------------------------
FLATBUFFERS_VERSION="24.3.25"
FLATC_BIN="$TOOLS_DIR/flatc"

if [[ ! -x "$FLATC_BIN" ]]; then
    echo
    echo "[6/8] Downloading flatc v$FLATBUFFERS_VERSION..."
    zip="$TOOLS_DIR/flatc.zip"
    # Linux release artifact name varies between releases; current convention:
    fetch "https://github.com/google/flatbuffers/releases/download/v$FLATBUFFERS_VERSION/Linux.flatc.binary.clang++-15.zip" "$zip"
    unzip -q -o "$zip" -d "$TOOLS_DIR"
    rm -f "$zip"
    chmod +x "$FLATC_BIN"
    echo "  flatc ready"
else
    echo "[6/8] flatc already present"
fi

FLATBUFFERS_DIR="$VENDOR/flatbuffers"
if [[ ! -f "$FLATBUFFERS_DIR/include/flatbuffers/flatbuffers.h" ]]; then
    echo
    echo "[6b/8] Downloading FlatBuffers sources v$FLATBUFFERS_VERSION..."
    zip="$VENDOR/flatbuffers.zip"
    fetch "https://github.com/google/flatbuffers/archive/refs/tags/v$FLATBUFFERS_VERSION.zip" "$zip"
    unzip -q -o "$zip" -d "$VENDOR"
    rm -rf "$FLATBUFFERS_DIR"
    extracted="$VENDOR/flatbuffers-$FLATBUFFERS_VERSION"
    mkdir -p "$FLATBUFFERS_DIR"
    mv "$extracted/include" "$FLATBUFFERS_DIR/include"
    rm -rf "$extracted"
    rm -f "$zip"
    echo "  FlatBuffers headers ready"
else
    echo "[6b/8] FlatBuffers headers already present"
fi

# --- 7. ixwebsocket -----------------------------------------------------
IXWS_VERSION="11.4.5"
IXWS_DIR="$VENDOR/ixwebsocket"

if [[ ! -f "$IXWS_DIR/ixwebsocket/IXWebSocket.h" ]]; then
    echo
    echo "[7/8] Downloading ixwebsocket v$IXWS_VERSION..."
    zip="$VENDOR/ixwebsocket.zip"
    fetch "https://github.com/machinezone/IXWebSocket/archive/refs/tags/v$IXWS_VERSION.zip" "$zip"
    unzip -q -o "$zip" -d "$VENDOR"
    rm -rf "$IXWS_DIR"
    extracted="$VENDOR/IXWebSocket-$IXWS_VERSION"
    mkdir -p "$IXWS_DIR"
    mv "$extracted/ixwebsocket" "$IXWS_DIR/ixwebsocket"
    [[ -f "$extracted/LICENSE.txt" ]] && mv "$extracted/LICENSE.txt" "$IXWS_DIR/LICENSE.txt"
    rm -rf "$extracted"
    rm -f "$zip"
    echo "  ixwebsocket ready"
else
    echo "[7/8] ixwebsocket already present"
fi

# --- 8. concurrentqueue --------------------------------------------------
CONCURRENTQUEUE_VERSION="1.0.4"
CONCURRENTQUEUE_DIR="$VENDOR/concurrentqueue"

if [[ ! -f "$CONCURRENTQUEUE_DIR/concurrentqueue.h" ]]; then
    echo
    echo "[8/8] Downloading concurrentqueue v$CONCURRENTQUEUE_VERSION..."
    zip="$VENDOR/concurrentqueue.zip"
    fetch "https://github.com/cameron314/concurrentqueue/archive/refs/tags/v$CONCURRENTQUEUE_VERSION.zip" "$zip"
    unzip -q -o "$zip" -d "$VENDOR"
    rm -rf "$CONCURRENTQUEUE_DIR"
    mv "$VENDOR/concurrentqueue-$CONCURRENTQUEUE_VERSION" "$CONCURRENTQUEUE_DIR"
    rm -f "$zip"
    echo "  concurrentqueue ready"
else
    echo "[8/8] concurrentqueue already present"
fi

echo
echo "=== All dependencies ready ==="
echo "Next: ./_premake5/premake_linux.sh && make -C build config=debug_x64 -j\$(nproc)"
