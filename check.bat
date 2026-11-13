@echo off
setlocal
cd /d "%~dp0"

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

if not exist build\ClipStash.exe (
    echo build\ClipStash.exe not found; run build.bat first
    exit /b 2
)

dumpbin /dependents build\ClipStash.exe
echo ---
dumpbin /headers build\ClipStash.exe | findstr /i "machine subsystem"
