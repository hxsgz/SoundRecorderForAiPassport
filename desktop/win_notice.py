"""Windows notification area balloon using Shell_NotifyIconW."""

import ctypes as c
from ctypes import wintypes as w
import threading
import time


class Notice(c.Structure):
    _fields_ = [
        ("cbSize", w.DWORD),
        ("hWnd", w.HWND),
        ("uID", w.UINT),
        ("uFlags", w.UINT),
        ("uCallbackMessage", w.UINT),
        ("hIcon", w.HICON),
        ("szTip", w.WCHAR * 128),
        ("dwState", w.DWORD),
        ("dwStateMask", w.DWORD),
        ("szInfo", w.WCHAR * 256),
        ("uTimeout", w.UINT),
        ("szInfoTitle", w.WCHAR * 64),
        ("dwInfoFlags", w.DWORD),
        ("guidItem", c.c_byte * 16),
        ("hBalloonIcon", w.HICON),
    ]


def show(title, message):
    def send():
        user = c.windll.user32
        user.CreateWindowExW.restype = w.HWND
        user.DestroyWindow.argtypes = [w.HWND]
        user.LoadIconW.restype = w.HICON
        shell = c.windll.shell32.Shell_NotifyIconW
        shell.argtypes = [w.DWORD, c.POINTER(Notice)]
        wnd = user.CreateWindowExW(
            0, "STATIC", "Passport", 0, 0, 0, 0, 0, None, None, None, None
        )
        if not wnd:
            return
        data = Notice()
        data.cbSize = c.sizeof(data)
        data.hWnd = wnd
        data.uID = 1
        data.hIcon = user.LoadIconW(None, 32516)
        data.uFlags = 2 | 4
        data.szTip = "Passport Recorder"
        try:
            if not shell(0, c.byref(data)):
                return
            data.uFlags = 16
            data.szInfoTitle = title[:60]
            data.szInfo = message[:240]
            data.dwInfoFlags = 1
            shell(1, c.byref(data))
            end = time.monotonic() + 7
            msg = w.MSG()
            while time.monotonic() < end:
                while user.PeekMessageW(c.byref(msg), None, 0, 0, 1):
                    user.TranslateMessage(c.byref(msg))
                    user.DispatchMessageW(c.byref(msg))
                time.sleep(0.1)
        finally:
            shell(2, c.byref(data))
            user.DestroyWindow(wnd)

    threading.Thread(target=send, name="passport-notification").start()
