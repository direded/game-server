-- premake5.lua
-- Build system for game server
-- Dependencies are vendored in vendor/ - run scripts/setup.ps1 (Windows) or scripts/setup.sh (Linux) first.

local VENDOR = "vendor"

-- Pre-build command that regenerates FlatBuffers C++ headers if any schema is
-- newer than its generated output. Applied to every project that includes the
-- generated headers, so the test project doesn't depend on the server project
-- having been built first. The script differs per host OS.
local FLATC_PREBUILD_WIN   = 'powershell -ExecutionPolicy Bypass -File "%{wks.location}/../scripts/flatc-compile.ps1"'
local FLATC_PREBUILD_LINUX = 'bash "%{wks.location}/../scripts/flatc-compile.sh"'

-- PostgreSQL install root (libpq). On Windows it's a per-user install root
-- written by scripts/setup.ps1 into _tools/pg_root.txt. On Linux we resolve
-- includes/libs through pg_config (system package: libpq-dev) so most distros
-- "just work" without a config file.
local function read_pg_root_windows()
    local env = os.getenv("PGROOT")
    if env and env ~= "" then return env end
    local f = io.open("_tools/pg_root.txt", "r")
    if not f then
        error("PostgreSQL install not located. Run scripts/setup.ps1 first (or set PGROOT).")
    end
    local path = f:read("*a")
    f:close()
    return (path:gsub("%s+$", ""))
end

local function pg_config(flag)
    local h = io.popen("pg_config " .. flag .. " 2>/dev/null")
    if not h then return nil end
    local out = h:read("*a") or ""
    h:close()
    out = out:gsub("%s+$", "")
    if out == "" then return nil end
    return out
end

local PG_INCLUDE
local PG_LIB
if os.target() == "windows" then
    local PG_ROOT = read_pg_root_windows()
    PG_INCLUDE = PG_ROOT .. "/include"
    PG_LIB     = PG_ROOT .. "/lib"
else
    PG_INCLUDE = pg_config("--includedir")
    PG_LIB     = pg_config("--libdir")
    if not PG_INCLUDE or not PG_LIB then
        error("pg_config not found. Install libpq-dev (Debian/Ubuntu: sudo apt install libpq-dev).")
    end
end

-- libsodium. Windows uses the official MSVC prebuilt (DLL + import lib).
-- Linux uses the system package (libsodium-dev) — headers and `libsodium.so`
-- are on the standard search paths, so no extra include/lib dirs needed.
local SODIUM_INCLUDE
local SODIUM_LIB_DEBUG
local SODIUM_LIB_RELEASE
if os.target() == "windows" then
    SODIUM_INCLUDE     = VENDOR .. "/libsodium/include"
    SODIUM_LIB_DEBUG   = VENDOR .. "/libsodium/x64/Debug/v143/dynamic"
    SODIUM_LIB_RELEASE = VENDOR .. "/libsodium/x64/Release/v143/dynamic"
end

workspace "game-server"
    architecture "x64"
    configurations { "Debug", "Release" }
    startproject "game-server"
    location "build"

    language "C++"
    cppdialect "C++23"

    filter "system:windows"
        toolset "v143" -- VS2022
    filter {}

    filter "configurations:Debug"
        defines { "DEBUG", "_DEBUG" }
        symbols "On"
        runtime "Debug"
        optimize "Off"

    filter "configurations:Release"
        defines { "NDEBUG" }
        symbols "Off"
        runtime "Release"
        optimize "Speed"

    filter {}

-- ──────────────────────────────────────────────────────────
-- yaml-cpp — compiled once as a static library
-- ──────────────────────────────────────────────────────────
project "yaml-cpp"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"  -- yaml-cpp's own sources don't need C++23
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}/%{prj.name}"

    files {
        VENDOR .. "/yaml-cpp/src/**.cpp",
        VENDOR .. "/yaml-cpp/src/**.h",
        VENDOR .. "/yaml-cpp/include/**.h"
    }

    includedirs {
        VENDOR .. "/yaml-cpp/include",
        VENDOR .. "/yaml-cpp/src"
    }

    defines { "YAML_CPP_STATIC_DEFINE" }

    filter "system:windows"
        systemversion "latest"
        disablewarnings { "4251", "4275" }
    filter {}

-- ──────────────────────────────────────────────────────────
-- ixwebsocket — compiled once as a static library. We build with no TLS
-- (ws:// only for now; step XXX adds wss://) and no zlib (permessage-deflate
-- disabled). The SSL adapter sources are excluded so we don't have to vendor
-- mbedtls/openssl just to throw them away.
-- ──────────────────────────────────────────────────────────
project "ixwebsocket"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}/%{prj.name}"

    files {
        VENDOR .. "/ixwebsocket/ixwebsocket/*.cpp",
        VENDOR .. "/ixwebsocket/ixwebsocket/*.h"
    }

    removefiles {
        VENDOR .. "/ixwebsocket/ixwebsocket/IXSocketOpenSSL.*",
        VENDOR .. "/ixwebsocket/ixwebsocket/IXSocketMbedTLS.*",
        VENDOR .. "/ixwebsocket/ixwebsocket/IXSocketAppleSSL.*"
    }

    includedirs {
        VENDOR .. "/ixwebsocket"
    }

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS", "WIN32_LEAN_AND_MEAN" }
    filter {}

-- ──────────────────────────────────────────────────────────
-- Google Test — compiled once as a static library
-- ──────────────────────────────────────────────────────────
project "googletest"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}/%{prj.name}"

    files {
        VENDOR .. "/googletest/googletest/src/gtest-all.cc",
        VENDOR .. "/googletest/googletest/src/gtest_main.cc"
    }

    includedirs {
        VENDOR .. "/googletest/googletest",
        VENDOR .. "/googletest/googletest/include"
    }

    filter "system:windows"
        systemversion "latest"
    filter {}

-- ──────────────────────────────────────────────────────────
-- Main server application
-- ──────────────────────────────────────────────────────────
project "game-server"
    kind "ConsoleApp"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}/%{prj.name}"
    debugdir "%{wks.location}/.."  -- VS debugger CWD = project root (where config.yaml lives)

    files {
        "src/**.h",
        "src/**.cpp"
    }

    includedirs {
        "src",
        VENDOR .. "/quill/include",
        VENDOR .. "/yaml-cpp/include",
        VENDOR .. "/flatbuffers/include",
        VENDOR .. "/ixwebsocket",
        VENDOR .. "/concurrentqueue",
        PG_INCLUDE
    }

    libdirs { PG_LIB }

    defines { "YAML_CPP_STATIC_DEFINE" }

    links {
        "yaml-cpp",
        "ixwebsocket"
    }

    filter "system:windows"
        systemversion "latest"
        includedirs { SODIUM_INCLUDE }
        links { "libpq", "libsodium", "ws2_32", "advapi32" }
        prebuildcommands { FLATC_PREBUILD_WIN }
    filter "system:linux"
        links { "pq", "sodium", "pthread", "dl" }
        prebuildcommands { FLATC_PREBUILD_LINUX }
        buildoptions { "-Wno-deprecated-declarations" }
    filter {}

    filter { "system:windows", "configurations:Debug" }
        libdirs { SODIUM_LIB_DEBUG }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. (SODIUM_LIB_DEBUG or "") .. '/libsodium.dll" "%{cfg.targetdir}/"'
        }
    filter { "system:windows", "configurations:Release" }
        libdirs { SODIUM_LIB_RELEASE }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. (SODIUM_LIB_RELEASE or "") .. '/libsodium.dll" "%{cfg.targetdir}/"'
        }
    filter {}

-- ──────────────────────────────────────────────────────────
-- Tests binary — links gtest and compiles greeter directly
-- ──────────────────────────────────────────────────────────
project "game-server-tests"
    kind "ConsoleApp"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}/%{prj.name}"
    debugdir "%{wks.location}/.."

    files {
        "src/greeter/**.h",
        "src/greeter/**.cpp",
        "src/net/framing.h",
        "src/net/framing.cpp",
        "src/session/**.h",
        "src/session/**.cpp",
        "src/auth/**.h",
        "src/auth/**.cpp",
        "src/db/**.h",
        "src/db/**.cpp",
        "src/event/**.h",
        "src/event/**.cpp",
        "src/sim/**.h",
        "src/sim/**.cpp",
        "src/chat/**.h",
        "src/chat/**.cpp",
        "src/world/**.h",
        "src/world/**.cpp",
        "src/protocol/generated/**.h",
        "tests/**.cpp"
    }

    includedirs {
        "src",
        VENDOR .. "/quill/include",
        VENDOR .. "/yaml-cpp/include",
        VENDOR .. "/flatbuffers/include",
        VENDOR .. "/googletest/googletest/include",
        VENDOR .. "/concurrentqueue",
        PG_INCLUDE
    }

    libdirs { PG_LIB }

    defines { "YAML_CPP_STATIC_DEFINE" }

    links {
        "yaml-cpp",
        "googletest"
    }

    filter "system:windows"
        systemversion "latest"
        includedirs { SODIUM_INCLUDE }
        links { "libpq", "libsodium", "ws2_32", "advapi32" }
        prebuildcommands { FLATC_PREBUILD_WIN }
    filter "system:linux"
        links { "pq", "sodium", "pthread", "dl" }
        prebuildcommands { FLATC_PREBUILD_LINUX }
        buildoptions { "-Wno-deprecated-declarations" }
    filter {}

    filter { "system:windows", "configurations:Debug" }
        libdirs { SODIUM_LIB_DEBUG }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. (SODIUM_LIB_DEBUG or "") .. '/libsodium.dll" "%{cfg.targetdir}/"'
        }
    filter { "system:windows", "configurations:Release" }
        libdirs { SODIUM_LIB_RELEASE }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. (SODIUM_LIB_RELEASE or "") .. '/libsodium.dll" "%{cfg.targetdir}/"'
        }
    filter {}
