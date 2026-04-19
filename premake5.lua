-- premake5.lua
-- Build system for game server
-- Dependencies are vendored in vendor/ - run scripts/setup.ps1 first

local VENDOR = "vendor"

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
        VENDOR .. "/yaml-cpp/include"
    }

    defines { "YAML_CPP_STATIC_DEFINE" }

    links {
        "yaml-cpp"
    }

    filter "system:windows"
        systemversion "latest"
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
        "tests/**.cpp"
    }

    includedirs {
        "src",
        VENDOR .. "/googletest/googletest/include"
    }

    links {
        "googletest"
    }

    filter "system:windows"
        systemversion "latest"
    filter {}
