@echo off
call "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
