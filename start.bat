@echo off
cd /d "%~dp0bin\Release"

REM Override PATH so only this directory + Windows system dirs are searched for DLLs.
set "PATH=%~dp0bin\Release;%SystemRoot%\system32;%SystemRoot%;%SystemRoot%\System32\Wbem"

game-server.exe ..\..\config.yaml
pause
