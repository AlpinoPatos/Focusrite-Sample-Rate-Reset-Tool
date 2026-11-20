@echo off
chcp 65001 >nul
setlocal

set "APP_NAME=FocusriteSampleRateResetTool"
set "STARTUP_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "LNK_PATH=%STARTUP_DIR%\%APP_NAME%.lnk"

if exist "%LNK_PATH%" (
    del /f /q "%LNK_PATH%" >nul 2>&1
    if not exist "%LNK_PATH%" (
        echo [OK] Autostart disabled and shortcut removed:
        echo %LNK_PATH%
    ) else (
        echo [ERROR] Failed to remove autostart shortcut.
    )
) else (
    echo [INFO] Autostart shortcut was not found.
)

pause
exit /b 0