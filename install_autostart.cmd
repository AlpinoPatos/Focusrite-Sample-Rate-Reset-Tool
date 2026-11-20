@echo off
chcp 65001 >nul
setlocal

set "APP_NAME=FocusriteSampleRateResetTool"
set "EXE_NAME=FocusriteSampleRateResetTool.exe"
set "SCRIPT_DIR=%~dp0"
set "EXE_PATH=%SCRIPT_DIR%%EXE_NAME%"
set "STARTUP_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "LNK_PATH=%STARTUP_DIR%\%APP_NAME%.lnk"

if not exist "%EXE_PATH%" (
    echo [ERROR] %EXE_NAME% not found in:
    echo %SCRIPT_DIR%
    pause
    exit /b 1
)

if not exist "%STARTUP_DIR%" (
    mkdir "%STARTUP_DIR%" >nul 2>&1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ws = New-Object -ComObject WScript.Shell; " ^
    "$sc = $ws.CreateShortcut('%LNK_PATH%'); " ^
    "$sc.TargetPath = '%EXE_PATH%'; " ^
    "$sc.WorkingDirectory = '%SCRIPT_DIR:~0,-1%'; " ^
    "$sc.Description = 'Focusrite ASIO Sample Rate Reset Tool'; " ^
    "$sc.Save()"

if exist "%LNK_PATH%" (
    echo [OK] Autostart enabled:
    echo %LNK_PATH%
) else (
    echo [ERROR] Failed to create autostart shortcut.
)

pause
exit /b 0