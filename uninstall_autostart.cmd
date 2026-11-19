@echo off
setlocal
set "SHORTCUT_PATH=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\FocusriteSampleRateResetTool.lnk"

taskkill /F /IM FocusriteSampleRateResetTool.exe >nul 2>&1

if exist "%SHORTCUT_PATH%" (
    del /F /Q "%SHORTCUT_PATH%"
    echo [OK] Auto-start removed and background process stopped.
) else (
    echo [INFO] Auto-start was not installed.
)
pause