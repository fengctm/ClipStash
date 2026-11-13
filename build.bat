@echo off
setlocal
cd /d "%~dp0"

rem Known-good path on this machine; replace with vswhere on another machine.
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist build mkdir build

cl /nologo /std:c++17 /O1 /MT /GL /Gy /EHsc /W4 /utf-8 ^
   /DNDEBUG /DUNICODE /D_UNICODE /Fo:build\ /Fe:build\ClipStash.exe ^
   src\main.cpp src\clipboard.cpp src\pngenc.cpp src\lifecycle.cpp ^
   /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
   windowscodecs.lib ole32.lib user32.lib gdi32.lib advapi32.lib

if errorlevel 1 exit /b 2
echo BUILD_OK
