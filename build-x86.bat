@echo off
setlocal
cd /d "%~dp0"

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1

if not exist build\x86 mkdir build\x86

cl /nologo /std:c++17 /O1 /MT /GL /Gy /EHsc /W4 /utf-8 ^
   /DNDEBUG /DUNICODE /D_UNICODE /Fo:build\x86\ /Fe:build\x86\ClipStash.exe ^
   src\main.cpp src\clipboard.cpp src\pngenc.cpp src\lifecycle.cpp ^
   /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
   windowscodecs.lib ole32.lib user32.lib gdi32.lib advapi32.lib

if errorlevel 1 exit /b 2
echo BUILD_X86_OK
