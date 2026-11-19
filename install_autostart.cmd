@echo off
setlocal
set "APP_DIR=%~dp0"
set "EXE_PATH=%APP_DIR%FocusriteSampleRateResetTool.exe"
set "STARTUP_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "SHORTCUT_PATH=%STARTUP_DIR%\FocusriteSampleRateResetTool.lnk"

if not exist "%EXE_PATH%" (
    echo [ERROR] FocusriteSampleRateResetTool.exe not found in:
    echo %APP_DIR%
    pause
    exit /b 1
)

taskkill /F /IM FocusriteSampleRateResetTool.exe >nul 2>&1

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ws = New-Object -ComObject WScript.Shell; " ^
  "$s = $ws.CreateShortcut('%SHORTCUT_PATH%'); " ^
  "$s.TargetPath = '%EXE_PATH%'; " ^
  "$s.WorkingDirectory = '%APP_DIR%'; " ^
  "$s.Description = 'Focusrite Sample Rate Reset Tool'; " ^
  "$s.Save()"

echo [OK] Auto-start installed! The tool will launch automatically with Windows.
start "" "%EXE_PATH%"
pause