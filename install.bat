@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not defined LOCALAPPDATA (
    echo [ERROR] LOCALAPPDATA is not defined.
    goto fail
)

if not exist "build\ClipStash.exe" (
    echo [ClipStash] build\ClipStash.exe not found, building...
    call build.bat
    if errorlevel 1 goto fail
)

set "TARGET_DIR=%LOCALAPPDATA%\ClipStash"
set "TARGET=%TARGET_DIR%\ClipStash.exe"

rem Stop any running instance before replacing the installed exe.
if exist "build\ClipStash.exe" "build\ClipStash.exe" --quit >nul 2>&1
ping -n 2 127.0.0.1 >nul

if not exist "%TARGET_DIR%" (
    mkdir "%TARGET_DIR%"
    if errorlevel 1 (
        echo [ERROR] Cannot create "%TARGET_DIR%".
        goto fail
    )
)

copy /Y "build\ClipStash.exe" "%TARGET%" >nul
if errorlevel 1 (
    echo [ERROR] Cannot copy ClipStash.exe to "%TARGET%".
    echo         Close any running ClipStash.exe and try again.
    goto fail
)

"%TARGET%" --install
if errorlevel 1 (
    echo [ERROR] --install failed.
    goto fail
)

ping -n 2 127.0.0.1 >nul

echo.
echo [ClipStash] Installed to: "%TARGET%"
echo [ClipStash] Autostart:    HKCU\Software\Microsoft\Windows\CurrentVersion\Run\ClipStash
echo [ClipStash] Output:       %%TEMP%%\ClipStash
echo [ClipStash] Error log:    %%LOCALAPPDATA%%\ClipStash\clipstash.log
echo [ClipStash] Stop:         "%TARGET%" --quit
echo [ClipStash] Uninstall:    "%TARGET%" --uninstall

reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v ClipStash >nul 2>&1
if errorlevel 1 (
    echo [WARN] Autostart entry was not found.
) else (
    echo [OK] Autostart registered.
)

tasklist /FI "IMAGENAME eq ClipStash.exe" 2>nul | "%SystemRoot%\System32\find.exe" /I "ClipStash.exe" >nul
if errorlevel 1 (
    echo [WARN] ClipStash.exe is not running. Start it manually from the path above.
) else (
    echo [OK] ClipStash is running.
)

tasklist /FI "IMAGENAME eq PasteEx.exe" 2>nul | "%SystemRoot%\System32\find.exe" /I "PasteEx.exe" >nul
if not errorlevel 1 (
    echo [WARN] PasteEx.exe is running. Exit it before using ClipStash;
    echo        both tools fight over the clipboard.
)

echo.
if /I not "%~1"=="--no-pause" pause
exit /b 0

:fail
echo.
if /I not "%~1"=="--no-pause" pause
exit /b 1
