"""
Focusrite Sample Rate Reset Tool
Resolves the Windows USB isochronous clock drift ("robotic/distorted audio") bug
on Focusrite interfaces by toggling the ASIO hardware sample rate via a customizable hotkey.
"""

import ctypes
from ctypes import POINTER, byref, c_char, c_double, c_long, c_void_p, wintypes
import json
import os
import sys
import threading
import time
import tkinter as tk
import winreg
import comtypes
import comtypes.client
from comtypes import COMMETHOD, GUID, IUnknown
from PIL import Image, ImageDraw
import pystray

APP_NAME = "Focusrite Sample Rate Reset Tool"
ASIO_REGISTRY_PATH = r"SOFTWARE\ASIO"
ASE_OK = 0
DELAY_OPTIONS = [1.0, 1.2, 1.5, 2.0]

# Win32 Hotkey & Thread Message Constants
MOD_ALT = 0x0001
MOD_CONTROL = 0x0002
MOD_SHIFT = 0x0004
MOD_NOREPEAT = 0x4000
WM_HOTKEY = 0x0312
WM_UPDATE_HOTKEY = 0x8001
WM_PAUSE_HOTKEY = 0x8002
WM_RESUME_HOTKEY = 0x8003
HOTKEY_ID = 1

DEFAULT_HOTKEY = {"modifiers": ["Ctrl", "Alt"], "key": "R", "vk": 0x52}
MODIFIER_VKS = {16, 17, 18, 91, 92, 160, 161, 162, 163, 164, 165}
SPECIAL_KEY_NAMES = {
    0x20: "Space", 0x09: "Tab", 0x2D: "Insert", 0x2E: "Delete",
    0x24: "Home", 0x23: "End", 0x21: "PageUp", 0x22: "PageDown",
    0x26: "Up", 0x28: "Down", 0x25: "Left", 0x27: "Right",
    0xC0: "`", 0xBD: "-", 0xBB: "=", 0xDB: "[", 0xDD: "]",
    0xDC: "\\", 0xBA: ";", 0xDE: "'", 0xBC: ",", 0xBE: ".", 0xBF: "/",
}


def get_app_dir() -> str:
    if getattr(sys, "frozen", False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))


CONFIG_PATH = os.path.join(get_app_dir(), "config.json")


def vk_to_name(vk: int) -> str:
    if 0x41 <= vk <= 0x5A or 0x30 <= vk <= 0x39:
        return chr(vk)
    if 0x70 <= vk <= 0x87:
        return f"F{vk - 0x6F}"
    return SPECIAL_KEY_NAMES.get(vk, f"Key({vk})")


def format_hotkey(hk: dict) -> str:
    parts = list(hk.get("modifiers", [])) + [hk.get("key", "R")]
    return "+".join(parts)


def modifiers_to_win32(mod_list: list) -> int:
    flags = 0
    if "Ctrl" in mod_list:
        flags |= MOD_CONTROL
    if "Alt" in mod_list:
        flags |= MOD_ALT
    if "Shift" in mod_list:
        flags |= MOD_SHIFT
    return flags


def load_config() -> tuple[float, dict]:
    delay = 1.2
    hotkey = dict(DEFAULT_HOTKEY)
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
            val = float(data.get("delay_seconds", 1.2))
            if val >= 0.5:
                delay = val
            hk = data.get("hotkey")
            if isinstance(hk, dict) and hk.get("modifiers") and isinstance(hk.get("vk"), int):
                hotkey = {
                    "modifiers": [m for m in ["Ctrl", "Alt", "Shift"] if m in hk["modifiers"]],
                    "key": str(hk.get("key") or vk_to_name(hk["vk"])),
                    "vk": int(hk["vk"]),
                }
    except Exception:
        pass
    return delay, hotkey


def save_config(delay: float, hotkey: dict) -> None:
    try:
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump({"delay_seconds": delay, "hotkey": hotkey}, f, indent=2)
    except Exception:
        pass


def find_focusrite_asio_clsid() -> str:
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, ASIO_REGISTRY_PATH) as asio_key:
        subkey_count = winreg.QueryInfoKey(asio_key)[0]
        for i in range(subkey_count):
            name = winreg.EnumKey(asio_key, i)
            if "focusrite" in name.lower() and "usb" in name.lower():
                with winreg.OpenKey(asio_key, name) as driver_key:
                    clsid, _ = winreg.QueryValueEx(driver_key, "CLSID")
                    return clsid
    raise RuntimeError("Focusrite USB ASIO driver not found in Windows Registry.")


def build_asio_interface(clsid_str: str):
    class IASIO(IUnknown):
        _iid_ = GUID(clsid_str)
        _methods_ = [
            COMMETHOD([], c_long, "init", (["in"], c_void_p, "sysHandle")),
            COMMETHOD([], None, "getDriverName", (["in"], POINTER(c_char), "name")),
            COMMETHOD([], c_long, "getDriverVersion"),
            COMMETHOD([], None, "getErrorMessage", (["in"], POINTER(c_char), "string")),
            COMMETHOD([], c_long, "start"),
            COMMETHOD([], c_long, "stop"),
            COMMETHOD([], c_long, "getChannels", (["in"], POINTER(c_long)), (["in"], POINTER(c_long))),
            COMMETHOD([], c_long, "getLatencies", (["in"], POINTER(c_long)), (["in"], POINTER(c_long))),
            COMMETHOD([], c_long, "getBufferSize", (["in"], POINTER(c_long)), (["in"], POINTER(c_long)), (["in"], POINTER(c_long)), (["in"], POINTER(c_long))),
            COMMETHOD([], c_long, "canSampleRate", (["in"], c_double, "sampleRate")),
            COMMETHOD([], c_long, "getSampleRate", (["in"], POINTER(c_double), "sampleRate")),
            COMMETHOD([], c_long, "setSampleRate", (["in"], c_double, "sampleRate")),
        ]
    return IASIO


def bounce_sample_rate(delay: float) -> str:
    clsid = find_focusrite_asio_clsid()
    IASIO = build_asio_interface(clsid)

    comtypes.CoInitialize()
    asio = None
    try:
        asio = comtypes.client.CreateObject(GUID(clsid), interface=IASIO)
        hwnd = ctypes.windll.user32.GetDesktopWindow()
        if not asio.init(hwnd):
            raise RuntimeError("Failed to initialize Focusrite USB ASIO driver.")

        current_rate = c_double(0.0)
        if asio.getSampleRate(byref(current_rate)) != ASE_OK:
            raise RuntimeError("Unable to read current sample rate.")

        original_rate = current_rate.value
        temp_rate = 44100.0 if int(original_rate) == 48000 else 48000.0

        if asio.canSampleRate(c_double(temp_rate)) != ASE_OK:
            raise RuntimeError(f"Target sample rate {int(temp_rate)} Hz is unsupported.")

        if asio.setSampleRate(c_double(temp_rate)) != ASE_OK:
            raise RuntimeError(f"Failed switching to {int(temp_rate)} Hz.")

        time.sleep(delay)

        if asio.setSampleRate(c_double(original_rate)) != ASE_OK:
            raise RuntimeError(f"Failed restoring {int(original_rate)} Hz.")

        return f"{int(original_rate)} Hz -> {int(temp_rate)} Hz -> {int(original_rate)} Hz"
    finally:
        asio = None
        comtypes.CoUninitialize()


def create_tray_icon_image() -> Image.Image:
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle((4, 4, 60, 60), radius=14, fill="#D62828")
    bars = [(18, 26, 23, 38), (29, 16, 34, 48), (40, 22, 45, 42)]
    for bar in bars:
        draw.rounded_rectangle(bar, radius=2, fill="#FFFFFF")
    return img


def open_hotkey_dialog(current_hk: dict, on_save, on_cancel) -> None:
    root = tk.Tk()
    root.title("Set Global Hotkey")
    root.configure(bg="#1E1E1E")
    root.resizable(False, False)
    root.attributes("-topmost", True)

    w, h = 340, 165
    sx = (root.winfo_screenwidth() - w) // 2
    sy = (root.winfo_screenheight() - h) // 2
    root.geometry(f"{w}x{h}+{sx}+{sy}")

    captured = {"hk": dict(current_hk), "saved": False}

    tk.Label(
        root,
        text="Press a key combination (e.g. Ctrl+Alt+R):",
        fg="#CCCCCC",
        bg="#1E1E1E",
        font=("Segoe UI", 9),
    ).pack(pady=(16, 6))

    display_var = tk.StringVar(value=format_hotkey(current_hk))
    box = tk.Label(
        root,
        textvariable=display_var,
        fg="#FFFFFF",
        bg="#2D2D30",
        font=("Segoe UI", 12, "bold"),
        width=24,
        pady=6,
    )
    box.pack(pady=4)

    def handle_cancel():
        root.destroy()
        if not captured["saved"]:
            on_cancel()

    def handle_save():
        captured["saved"] = True
        hk = captured["hk"]
        root.destroy()
        on_save(hk)

    def on_key_press(event):
        user32 = ctypes.windll.user32
        ctrl = bool(user32.GetAsyncKeyState(0x11) & 0x8000)
        alt = bool(user32.GetAsyncKeyState(0x12) & 0x8000)
        shift = bool(user32.GetAsyncKeyState(0x10) & 0x8000)

        vk = event.keycode
        if vk == 27 and not (ctrl or alt or shift):
            handle_cancel()
            return "break"
        if vk in MODIFIER_VKS:
            return "break"

        mods = []
        if ctrl:
            mods.append("Ctrl")
        if alt:
            mods.append("Alt")
        if shift:
            mods.append("Shift")

        if not mods:
            display_var.set("Include Ctrl, Alt, or Shift")
            return "break"

        hk = {"modifiers": mods, "key": vk_to_name(vk), "vk": vk}
        captured["hk"] = hk
        display_var.set(format_hotkey(hk))
        return "break"

    root.protocol("WM_DELETE_WINDOW", handle_cancel)
    root.bind("<KeyPress>", on_key_press)

    btn_frame = tk.Frame(root, bg="#1E1E1E")
    btn_frame.pack(pady=(12, 10))

    tk.Button(
        btn_frame,
        text="Save",
        command=handle_save,
        bg="#D62828",
        fg="#FFFFFF",
        activebackground="#B51F1F",
        activeforeground="#FFFFFF",
        relief="flat",
        font=("Segoe UI", 9, "bold"),
        width=10,
        cursor="hand2",
    ).pack(side="left", padx=6)

    tk.Button(
        btn_frame,
        text="Cancel",
        command=handle_cancel,
        bg="#333337",
        fg="#DDDDDD",
        activebackground="#444448",
        activeforeground="#FFFFFF",
        relief="flat",
        font=("Segoe UI", 9),
        width=10,
        cursor="hand2",
    ).pack(side="left", padx=6)

    root.focus_force()
    root.mainloop()


class FocusriteResetTrayApp:
    def __init__(self):
        self.delay, self.hotkey = load_config()
        self.status_text = f"Status: Ready ({format_hotkey(self.hotkey)})"
        self._lock = threading.Lock()
        self._dialog_open = False
        self._listener_tid = None
        self._pending_hotkey = None
        self._hotkey_update_event = threading.Event()
        self._hotkey_update_ok = False

        self.icon = pystray.Icon(
            name="focusrite_sample_rate_reset_tool",
            icon=create_tray_icon_image(),
            title=f"{APP_NAME} ({format_hotkey(self.hotkey)})",
            menu=self._build_menu(),
        )

    def _build_menu(self) -> pystray.Menu:
        delay_items = [
            pystray.MenuItem(
                f"{opt:.1f}s" + (" (Default)" if opt == 1.2 else ""),
                self._make_delay_setter(opt),
                checked=lambda item, val=opt: abs(self.delay - val) < 0.01,
                radio=True,
            )
            for opt in DELAY_OPTIONS
        ]
        return pystray.Menu(
            pystray.MenuItem(
                lambda item: f"Reset Sample Rate ({format_hotkey(self.hotkey)})",
                self.trigger_reset,
                default=True,
            ),
            pystray.MenuItem("Change Hotkey...", self.prompt_hotkey),
            pystray.MenuItem("Switch Delay", pystray.Menu(*delay_items)),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem(lambda item: self.status_text, None, enabled=False),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Exit", self.exit_app),
        )

    def _make_delay_setter(self, val: float):
        def setter(icon, item):
            self.delay = val
            save_config(self.delay, self.hotkey)
            self.icon.update_menu()
        return setter

    def prompt_hotkey(self, icon=None, item=None) -> None:
        if self._dialog_open:
            return
        self._dialog_open = True
        if self._listener_tid:
            ctypes.windll.user32.PostThreadMessageW(self._listener_tid, WM_PAUSE_HOTKEY, 0, 0)

        def worker():
            try:
                open_hotkey_dialog(self.hotkey, self._apply_new_hotkey, self._resume_hotkey)
            finally:
                self._dialog_open = False

        threading.Thread(target=worker, daemon=True).start()

    def _resume_hotkey(self) -> None:
        if self._listener_tid:
            ctypes.windll.user32.PostThreadMessageW(self._listener_tid, WM_RESUME_HOTKEY, 0, 0)

    def _apply_new_hotkey(self, new_hk: dict) -> None:
        if not self._listener_tid:
            return
        self._pending_hotkey = new_hk
        self._hotkey_update_event.clear()
        ctypes.windll.user32.PostThreadMessageW(self._listener_tid, WM_UPDATE_HOTKEY, 0, 0)
        self._hotkey_update_event.wait(timeout=2.0)

        if self._hotkey_update_ok:
            hk_str = format_hotkey(self.hotkey)
            save_config(self.delay, self.hotkey)
            self.status_text = f"Status: Ready ({hk_str})"
            self.icon.title = f"{APP_NAME} ({hk_str})"
            self.icon.update_menu()
        else:
            self.icon.notify(
                f"{format_hotkey(new_hk)} is already in use by another app.",
                title=APP_NAME,
            )

    def trigger_reset(self, icon=None, item=None) -> None:
        if not self._lock.acquire(blocking=False):
            return
        threading.Thread(target=self._run_reset_worker, daemon=True).start()

    def _run_reset_worker(self) -> None:
        try:
            self.status_text = "Status: Resetting..."
            self.icon.update_menu()

            summary = bounce_sample_rate(self.delay)
            print(f"[OK] {summary}")
            self.status_text = f"Last Reset: {time.strftime('%H:%M:%S')}"
        except Exception as exc:
            err_msg = str(exc)
            print(f"[ERROR] {err_msg}")
            self.status_text = "Status: Error (Check device)"
            self.icon.notify(err_msg, title=APP_NAME)
        finally:
            self.icon.update_menu()
            self._lock.release()

    def _hotkey_listener(self) -> None:
        user32 = ctypes.windll.user32
        self._listener_tid = ctypes.windll.kernel32.GetCurrentThreadId()

        mods = modifiers_to_win32(self.hotkey["modifiers"]) | MOD_NOREPEAT
        if not user32.RegisterHotKey(None, HOTKEY_ID, mods, self.hotkey["vk"]):
            print(f"[WARN] Could not register {format_hotkey(self.hotkey)}.")

        msg = wintypes.MSG()
        try:
            while user32.GetMessageW(byref(msg), None, 0, 0) != 0:
                if msg.message == WM_HOTKEY and msg.wParam == HOTKEY_ID:
                    self.trigger_reset()
                elif msg.message == WM_PAUSE_HOTKEY:
                    user32.UnregisterHotKey(None, HOTKEY_ID)
                elif msg.message == WM_RESUME_HOTKEY:
                    old_mods = modifiers_to_win32(self.hotkey["modifiers"]) | MOD_NOREPEAT
                    user32.RegisterHotKey(None, HOTKEY_ID, old_mods, self.hotkey["vk"])
                elif msg.message == WM_UPDATE_HOTKEY and self._pending_hotkey:
                    user32.UnregisterHotKey(None, HOTKEY_ID)
                    cand = self._pending_hotkey
                    cand_mods = modifiers_to_win32(cand["modifiers"]) | MOD_NOREPEAT
                    if user32.RegisterHotKey(None, HOTKEY_ID, cand_mods, cand["vk"]):
                        self.hotkey = cand
                        self._hotkey_update_ok = True
                    else:
                        old_mods = modifiers_to_win32(self.hotkey["modifiers"]) | MOD_NOREPEAT
                        user32.RegisterHotKey(None, HOTKEY_ID, old_mods, self.hotkey["vk"])
                        self._hotkey_update_ok = False
                    self._hotkey_update_event.set()
                user32.TranslateMessage(byref(msg))
                user32.DispatchMessageW(byref(msg))
        finally:
            user32.UnregisterHotKey(None, HOTKEY_ID)

    def exit_app(self, icon=None, item=None) -> None:
        self.icon.stop()

    def run(self) -> None:
        threading.Thread(target=self._hotkey_listener, daemon=True).start()
        self.icon.run()


if __name__ == "__main__":
    FocusriteResetTrayApp().run()
