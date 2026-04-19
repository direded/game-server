@echo off
cd /d "%~dp0\.."
_premake5\bin\windows\premake5.exe --file=premake5.lua vs2022
pause
