@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not defined LOCALAPPDATA (
    echo [ERROR] LOCALAPPDATA is not defined.
    goto fail
)

set "TARGET_DIR=%LOCALAPPDATA%\ClipStash"
set "TARGET=%TARGET_DIR%\ClipStash.exe"
set "LOCAL_EXE=%~dp0ClipStash.exe"

echo [ClipStash] Stopping...
if exist "%TARGET%" "%TARGET%" --uninstall
if exist "%LOCAL_EXE%" "%LOCAL_EXE%" --quit >nul 2>&1
if exist "%~dp0build\ClipStash.exe" "%~dp0build\ClipStash.exe" --quit >nul 2>&1

rem Ensure the autostart value is gone even if --uninstall returned an error.
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v ClipStash /f >nul 2>&1
ping -n 2 127.0.0.1 >nul

echo [ClipStash] Checking...
tasklist /FI "IMAGENAME eq ClipStash.exe" 2>nul | "%SystemRoot%\System32\find.exe" /I "ClipStash.exe" >nul
if errorlevel 1 (
    echo [OK] ClipStash is not running.

    if exist "%TARGET%" (
        del /f /q "%TARGET%" >nul 2>&1
    )
    if exist "%TARGET%" (
        echo [WARN] Could not delete "%TARGET%".
    ) else (
        echo [OK] Removed the installed exe.
    )

    if exist "%TARGET_DIR%" rmdir "%TARGET_DIR%" >nul 2>&1
) else (
    echo [WARN] ClipStash is still running; the exe was not deleted.
)

reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v ClipStash >nul 2>&1
if errorlevel 1 (
    echo [OK] Autostart removed.
) else (
    echo [WARN] Autostart entry is still present.
)

echo.
echo Logs:      %%LOCALAPPDATA%%\ClipStash\clipstash.log
echo PNG files: %%TEMP%%\ClipStash
echo These user files were left in place; delete them manually for a full purge.
echo.
if /I not "%~1"=="--no-pause" pause
exit /b 0

:fail
echo.
if /I not "%~1"=="--no-pause" pause
exit /b 1
