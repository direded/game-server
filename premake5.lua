-- premake5.lua
-- Build system for game server
-- Dependencies are vendored in vendor/ - run scripts/setup.ps1 first

local VENDOR = "vendor"

-- Pre-build command that regenerates FlatBuffers C++ headers if any schema is
-- newer than its generated output. Applied to every project that includes the
-- generated headers, so the test project doesn't depend on the server project
-- having been built first.
local FLATC_PREBUILD = 'powershell -ExecutionPolicy Bypass -File "%{wks.location}/../scripts/flatc-compile.ps1"'

-- PostgreSQL install root (for libpq). Written by scripts/setup.ps1 into
-- _tools/pg_root.txt, or overridable via the PGROOT env var.
local function read_pg_root()
    local env = os.getenv("PGROOT")
    if env and env ~= "" then return env end
    local f = io.open("_tools/pg_root.txt", "r")
    if not f then
        error("PostgreSQL install not located. Run scripts/setup.ps1 first (or set PGROOT).")
    end
    local path = f:read("*a")
    f:close()
    -- strip trailing whitespace/newlines
    return (path:gsub("%s+$", ""))
end
local PG_ROOT = read_pg_root()
local PG_INCLUDE = PG_ROOT .. "/include"
local PG_LIB = PG_ROOT .. "/lib"

-- libsodium (Argon2id + randombytes). Official MSVC prebuilt ships three
-- x64/<Config>/v143/ flavors:
--   static/  — /MT, static lib (CRT mismatch with our /MD → LNK4098)
--   dynamic/ — /MD, DLL (matches our CRT; must ship libsodium.dll)
--   ltcg/    — /MT+LTCG, static lib (same CRT mismatch as static/)
-- Only dynamic/ matches /MD, so we link against its import lib and
-- copy libsodium.dll into the target dir via postbuildcommands.
local SODIUM_INCLUDE = VENDOR .. "/libsodium/include"
local SODIUM_LIB_DEBUG   = VENDOR .. "/libsodium/x64/Debug/v143/dynamic"
local SODIUM_LIB_RELEASE = VENDOR .. "/libsodium/x64/Release/v143/dynamic"

workspace "game-server"
    architecture "x64"
    configurations { "Debug", "Release" }
    startproject "game-server"
    location "build"

    language "C++"
    cppdialect "C++23"
    toolset "v143" -- VS2022

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
        SODIUM_INCLUDE,
        PG_INCLUDE
    }

    libdirs { PG_LIB }

    defines { "YAML_CPP_STATIC_DEFINE" }

    links {
        "yaml-cpp",
        "ixwebsocket",
        "libpq",
        "libsodium"
    }

    prebuildcommands { FLATC_PREBUILD }

    filter "system:windows"
        systemversion "latest"
        links { "ws2_32", "advapi32" }
    filter "configurations:Debug"
        libdirs { SODIUM_LIB_DEBUG }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. SODIUM_LIB_DEBUG .. '/libsodium.dll" "%{cfg.targetdir}/"'
        }
    filter "configurations:Release"
        libdirs { SODIUM_LIB_RELEASE }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. SODIUM_LIB_RELEASE .. '/libsodium.dll" "%{cfg.targetdir}/"'
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
        "src/protocol/generated/**.h",
        "tests/**.cpp"
    }

    includedirs {
        "src",
        VENDOR .. "/quill/include",
        VENDOR .. "/flatbuffers/include",
        VENDOR .. "/googletest/googletest/include",
        SODIUM_INCLUDE,
        PG_INCLUDE
    }

    libdirs { PG_LIB }

    links {
        "googletest",
        "libpq",
        "libsodium"
    }

    prebuildcommands { FLATC_PREBUILD }

    filter "system:windows"
        systemversion "latest"
        links { "ws2_32", "advapi32" }
    filter "configurations:Debug"
        libdirs { SODIUM_LIB_DEBUG }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. SODIUM_LIB_DEBUG .. '/libsodium.dll" "%{cfg.targetdir}/"'
        }
    filter "configurations:Release"
        libdirs { SODIUM_LIB_RELEASE }
        postbuildcommands {
            '{COPYFILE} "%{wks.location}/../' .. SODIUM_LIB_RELEASE .. '/libsodium.dll" "%{cfg.targetdir}/"'
        }
    filter {}
