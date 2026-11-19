# Focusrite Sample Rate Reset Tool

Lightweight Windows tray utility to **recover from** Focusrite USB audio **distorted / robotic audio** via a customizable hotkey (Ctrl+Alt+R by default).

## The Problem

On Windows 10/11, Focusrite USB audio interfaces can occasionally start outputting heavily distorted, robotic, or crackling audio. The standard manual workaround is opening **Focusrite Control**, changing the sample rate (e.g., `48 kHz` -> `44.1 kHz`), waiting ~1 second, and switching it back to force the audio stream to re-initialize.

Standard Windows Core Audio APIs (`IPolicyConfig`) reject external sample rate changes while the Focusrite driver holds the clock lock.

## How It Works

Instead of kernel-mode hacks or restarting Windows audio services (which breaks active game/VoIP audio routing), this tool communicates directly with the **Focusrite USB ASIO COM driver** (`IASIO`) via `ctypes` and `comtypes`:

1. Dynamically resolves the `Focusrite USB ASIO` driver `CLSID` from `HKLM\SOFTWARE\ASIO`.
2. Reads the active hardware sample rate (`getSampleRate`).
3. Switches to a temporary sample rate (`44100 Hz` or `48000 Hz`) and waits for the configured lock delay (`1.2s` default).
4. Restores the original sample rate, forcing the USB audio endpoint and Windows WASAPI mixer to re-initialize cleanly.

## Features

- **Customizable Global Hotkey (`Ctrl + Alt + R` default):** Uses the native Win32 `RegisterHotKey` API (0% idle CPU usage, no low-level keyboard hooks, safe alongside kernel-level anti-cheats). Assign any key combination directly from the system tray menu.
- **Configurable Lock Delay:** Right-click the system tray icon to choose between `1.0s`, `1.2s` (default), `1.5s`, and `2.0s` switching intervals.
- **One-Click Auto-Start:** Includes `install_autostart.cmd` and `uninstall_autostart.cmd` scripts to manage Windows startup cleanly without requiring administrator privileges.
- **Portable & Zero-Bloat:** Standalone `.exe` that stores settings in a local `config.json` in the application directory without touching the Windows Registry.

## Usage

1. Download `FocusriteSampleRateResetTool-v1.0.0.zip` from the [Releases](../../releases) page and extract it to a permanent folder.
2. Run `FocusriteSampleRateResetTool.exe` — it minimizes to the Windows System Tray.
3. Whenever audio distortion occurs, press **`Ctrl + Alt + R`** (or right-click the tray icon to trigger a reset or customize the hotkey).
4. *(Optional)* Run `install_autostart.cmd` to launch automatically with Windows, or `uninstall_autostart.cmd` to remove it.

## Building from Source

Requires Windows and Python 3.10+:

```bash
pip install -r requirements.txt
pyinstaller --onefile --noconsole --name "FocusriteSampleRateResetTool" --icon "icon.ico" --hidden-import "pystray._win32" app.py
```

## Disclaimer

Tested on a Focusrite Scarlett 2i2 4th Gen on Windows 11. This is an independent community utility and is not affiliated with or endorsed by Focusrite Audio Engineering Ltd.
