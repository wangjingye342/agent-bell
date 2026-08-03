#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bridge.py — AgentBell 桥接程序（Windows 托盘常驻）。

装上它以后，Claude / Codex 等桌面应用不用配任何 hook：
只要它们发出 Windows 系统通知，本程序就自动转发给局域网里的 AgentBell 设备
（蜂鸣 + 震动 + OLED 屏显）。

功能：
  · 监听 Windows 通知中心（WinRT UserNotificationListener，轮询式）
  · 按应用名关键词过滤（默认 claude / codex，可在设置里改）
  · 自动发现设备：缓存 IP → mDNS → 全网段扫描；掉线自动重搜
  · 托盘图标显示在线状态；设置面板（pywebview + ui/index.html，TE 铝面板风格）
    可调转发规则 + 设备本体设置（音量/铃声/勿扰…）
  · 可选开机自启（写 HKCU Run 注册表）

架构：主线程跑 webview 事件循环；pystray / 设备守护 / 通知监听各自线程。
UI 是本地 HTML（ui/index.html），JS 经 pywebview js_api 调下面的 Api 类。

运行：pythonw bridge.py   （install.bat 会装好自启；--show 启动即显示窗口）
"""
import os
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bridge_config import Config, LOG_PATH, APP_DIR   # noqa: E402
import device_api                                      # noqa: E402
import discovery                                       # noqa: E402

IS_MAC = sys.platform == "darwin"
if IS_MAC:                                             # 通知监听按平台选实现，接口一致
    from watcher_mac import NotificationWatcher        # noqa: E402
else:
    from watcher import NotificationWatcher            # noqa: E402

APP_NAME = "AgentBell Bridge"
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
RUN_VALUE = "AgentBellBridge"
LAUNCH_AGENT = os.path.expanduser(                     # macOS 开机自启（LaunchAgent）
    "~/Library/LaunchAgents/com.agentbell.bridge.plist")
SINGLETON_PORT = 47316          # 本机回环端口作单实例锁
WIN_W, WIN_H = 1120, 660        # 固定面板尺寸（逻辑像素，WebView 自适配 DPI）


# ============================================================================
#  日志：追加到 %APPDATA%\AgentBell\bridge.log（超 1MB 截断重来）
# ============================================================================
_log_lock = threading.Lock()


def log(msg):
    line = "%s %s" % (time.strftime("%m-%d %H:%M:%S"), msg)
    with _log_lock:
        try:
            os.makedirs(APP_DIR, exist_ok=True)
            if os.path.exists(LOG_PATH) and os.path.getsize(LOG_PATH) > 1_000_000:
                os.replace(LOG_PATH, LOG_PATH + ".old")
            with open(LOG_PATH, "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except Exception:
            pass
    try:
        print(line)
    except Exception:
        pass


# ============================================================================
#  单实例锁：绑不上回环端口说明已有实例在跑
# ============================================================================
def acquire_singleton():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind(("127.0.0.1", SINGLETON_PORT))
        s.listen(1)
        return s                      # 保持引用，进程退出自动释放
    except OSError:
        return None


# ============================================================================
#  开机自启：Windows 写 HKCU Run 注册表；macOS 写 ~/Library/LaunchAgents plist。
#  都不需要管理员权限。
# ============================================================================
def _autostart_command():
    if getattr(sys, "frozen", False):          # PyInstaller 打包版：exe 自己就是程序
        return '"%s"' % sys.executable
    exe = sys.executable
    pyw = os.path.join(os.path.dirname(exe), "pythonw.exe")   # 无控制台窗口
    if os.path.exists(pyw):
        exe = pyw
    script = os.path.abspath(__file__)
    return '"%s" "%s"' % (exe, script)


def _autostart_argv_mac():
    if getattr(sys, "frozen", False):          # .app 里的可执行文件
        return [sys.executable]
    return [sys.executable, os.path.abspath(__file__)]


def get_autostart():
    if IS_MAC:
        return os.path.exists(LAUNCH_AGENT)
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as k:
            winreg.QueryValueEx(k, RUN_VALUE)
        return True
    except OSError:
        return False


def set_autostart(enable):
    if IS_MAC:
        try:
            if enable:
                import plistlib
                os.makedirs(os.path.dirname(LAUNCH_AGENT), exist_ok=True)
                with open(LAUNCH_AGENT, "wb") as f:
                    plistlib.dump({"Label": "com.agentbell.bridge",
                                   "ProgramArguments": _autostart_argv_mac(),
                                   "RunAtLoad": True}, f)
            else:
                try:
                    os.remove(LAUNCH_AGENT)
                except FileNotFoundError:
                    pass
            return True
        except OSError as e:
            log("自启注册失败：%r" % e)
            return False
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0,
                            winreg.KEY_SET_VALUE) as k:
            if enable:
                winreg.SetValueEx(k, RUN_VALUE, 0, winreg.REG_SZ, _autostart_command())
            else:
                try:
                    winreg.DeleteValue(k, RUN_VALUE)
                except FileNotFoundError:
                    pass
        return True
    except OSError as e:
        log("自启注册失败：%r" % e)
        return False


# ============================================================================
#  设备守护线程：保持在线，掉线自动重搜（指数退避 5s→60s）
# ============================================================================
class DeviceManager(threading.Thread):
    def __init__(self, cfg, on_state_change):
        super().__init__(name="device-manager", daemon=True)
        self.cfg = cfg
        self.on_state_change = on_state_change   # (online: bool) → None，切状态时回调
        self.host = None                         # 当前可用地址；None=离线
        self.info = {}                           # 最近一次 /api/info
        self.scanning = False                    # 是否正在扫网段（UI 显示）
        self.scan_progress = (0, 0)
        self.stop_event = threading.Event()
        self._wake = threading.Event()
        self._backoff = 5.0

    def run(self):
        while not self.stop_event.is_set():
            if self.host:
                # —— 在线：周期探活 ——
                info = device_api.get_info(self.host, self.cfg.get("device_port"))
                if info:
                    self.info = info
                    self._sleep(float(self.cfg.get("health_interval_s") or 20.0))
                    continue
                if discovery.check_device(self.host, self.cfg.get("device_port")):
                    self._sleep(float(self.cfg.get("health_interval_s") or 20.0))
                    continue
                log("设备守护：%s 探活失败，标记离线" % self.host)
                self.host = None
                self.on_state_change(False)
            # —— 离线：找设备 ——
            self.scanning = True
            self.scan_progress = (0, 0)
            found = discovery.find_device(
                self.cfg, log=log,
                on_progress=lambda d, t: setattr(self, "scan_progress", (d, t)),
                stop_event=self.stop_event)
            self.scanning = False
            if found:
                self.host = found
                self.info = device_api.get_info(found, self.cfg.get("device_port")) or {}
                self._backoff = 5.0
                self.on_state_change(True)
                log("设备守护：已连接 %s" % found)
            else:
                self._sleep(self._backoff)
                self._backoff = min(self._backoff * 2, 60.0)

    def _sleep(self, seconds):
        self._wake.wait(seconds)
        self._wake.clear()

    def notify_lost(self):
        """外部（转发失败）报告设备可能掉线：立即重查。"""
        self._backoff = 5.0
        self._wake.set()

    def force_rediscover(self):
        """UI「重新搜索」：丢弃缓存地址，从头找。"""
        self.cfg.set("device_host", "")
        self.host = None
        self._backoff = 5.0
        self.on_state_change(False)
        self._wake.set()

    def set_manual_host(self, host):
        """UI 手动填 IP：写缓存并立即探活验证（阻塞 ≤2.5s）。返回是否可用。"""
        host = (host or "").strip()
        if not host:
            return False
        if not discovery.check_device(host, self.cfg.get("device_port"), timeout=2.5):
            log("手动地址：%s 探活失败（不是 AgentBell 或不在线）" % host)
            return False
        self.cfg.set("device_host", host)
        self.host = host
        self.info = device_api.get_info(host, self.cfg.get("device_port")) or {}
        self._backoff = 5.0
        self.on_state_change(True)
        log("手动地址：已连接 %s" % host)
        return True

    def stop(self):
        self.stop_event.set()
        self._wake.set()


# ============================================================================
#  托盘图标（pystray）：铃铛 + 右下角状态点（绿=在线 红=离线）
# ============================================================================
def make_icon_image(online):
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    body = (230, 176, 60, 255)                      # 金色铃铛
    d.pieslice((12, 8, 52, 48), 180, 360, fill=body)          # 铃罩上半
    d.polygon([(12, 28), (52, 28), (56, 46), (8, 46)], fill=body)  # 铃罩下摆
    d.rectangle((6, 46, 58, 50), fill=body)                   # 底沿
    d.ellipse((26, 50, 38, 62), fill=body)                    # 铃锤
    dot = (80, 200, 90, 255) if online else (220, 70, 70, 255)
    d.ellipse((42, 42, 62, 62), fill=dot, outline=(255, 255, 255, 255), width=2)
    return img


# ============================================================================
#  JS ↔ Python 桥（pywebview js_api）：UI 的全部动作都走这里。
#  每个方法都可能被 WebView2 的桥线程调用；耗时操作（探活）直接阻塞该线程即可，
#  JS 侧是 Promise，不卡界面。
# ============================================================================
class Api:
    """注意：pywebview 会把 js_api 对象的全部公有属性递归暴露给 JS，
    所以内部引用一律下划线私有，只留方法作为接口。"""

    def __init__(self, cfg, dm, watcher):
        self._cfg, self._dm, self._watcher = cfg, dm, watcher
        self._window = None           # webview.Window，main() 回填
        self._on_quit = None          # 退出回调，main() 回填

    # ---- 状态轮询（JS 每秒调一次） ----
    def get_state(self):
        dm = self._dm
        info = dm.info or {}
        d, t = dm.scan_progress
        return {
            "online": bool(dm.host), "host": dm.host or "",
            "scanning": bool(dm.scanning), "scan_done": d, "scan_total": t,
            "rssi": info.get("rssi"), "uptime_s": info.get("uptime_s"),
            "listener": self._watcher.status,
            "forward": bool(self._cfg.get("forward_enabled")),
            "autostart": get_autostart(),
            "keywords": ", ".join(self._cfg.get("app_keywords") or []),
            "poll_s": float(self._cfg.get("poll_interval_s") or 2.0),
            "cooldown_s": float(self._cfg.get("cooldown_s") or 3.0),
            "manual": self._cfg.get("device_host") or "",
            "platform": "mac" if IS_MAC else "win",
        }

    # ---- 设备设置（读 / 写，写完设备回完整设置） ----
    def get_device_settings(self):
        host = self._dm.host
        if not host:
            return None
        return device_api.get_settings(host, self._cfg.get("device_port"))

    def apply_device(self, fields):
        host = self._dm.host
        if not host:
            return None
        res = device_api.post_settings(host, self._cfg.get("device_port"),
                                       **(fields or {}))
        if res is None:
            log("设置：写入失败（设备掉线？）")
            self._dm.notify_lost()
        return res

    def get_notes(self):
        host = self._dm.host
        if not host:
            return None
        return device_api.get_notes(host, self._cfg.get("device_port"))

    # ---- 链路动作 ----
    def send_test(self):
        host = self._dm.host
        ok = bool(host and device_api.send_test(host, self._cfg.get("device_port")))
        log("测试：已发送" if ok else "测试：失败（设备离线）")
        if not ok:
            self._dm.notify_lost()
        return ok

    def rediscover(self):
        self._dm.force_rediscover()
        return True

    def connect_manual(self, host):
        return bool(self._dm.set_manual_host(host))

    # ---- 转发规则 / 本程序 ----
    def set_forward(self, on):
        self._cfg.set("forward_enabled", bool(on))
        return True

    def set_keywords(self, text):
        kws = [k.strip() for k in (text or "").split(",") if k.strip()]
        self._cfg.set("app_keywords", kws)
        return True

    def set_poll(self, v):
        self._cfg.set("poll_interval_s", max(1.0, min(30.0, float(v))))
        return True

    def set_cooldown(self, v):
        self._cfg.set("cooldown_s", max(0.0, min(120.0, float(v))))
        return True

    def set_autostart(self, on):
        return set_autostart(bool(on))

    def open_log(self):
        if IS_MAC:
            subprocess.Popen(["open", LOG_PATH])
        else:
            subprocess.Popen(["notepad", LOG_PATH])
        return True

    def open_notify_settings(self):
        if IS_MAC:      # 通知库要「完全磁盘访问权限」，直达该设置页
            subprocess.Popen(["open", "x-apple.systempreferences:"
                              "com.apple.preference.security?Privacy_AllFiles"])
        else:
            subprocess.Popen(["cmd", "/c", "start", "ms-settings:privacy-notifications"],
                             creationflags=subprocess.CREATE_NO_WINDOW)
        return True

    # ---- 窗口 ----
    def minimize(self):
        if self._window:
            self._window.minimize()
        return True

    def hide_window(self):
        if self._window:
            self._window.hide()
        return True

    def quit_app(self):
        if self._on_quit:
            self._on_quit()
        return True


def _ui_path():
    """ui/index.html 的绝对路径（源码运行和 PyInstaller onedir 都适用）。"""
    base = getattr(sys, "_MEIPASS", None) or os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, "ui", "index.html")


def _round_corners(window):
    """Win11 DWM 圆角（属性 33 = DWMWA_WINDOW_CORNER_PREFERENCE，2 = 圆角）。
    Win10 没有该属性，调用失败无副作用（窗口保持直角）。"""
    try:
        import ctypes
        hwnd = window.native.Handle.ToInt32()          # WinForms Form 句柄
        pref = ctypes.c_int(2)
        ctypes.windll.dwmapi.DwmSetWindowAttribute(
            ctypes.c_void_p(hwnd), 33, ctypes.byref(pref), 4)
    except Exception:
        pass


# ============================================================================
#  main：webview 主循环（主线程）+ pystray（自带线程）+ 两个工作线程
# ============================================================================
def main():
    lock = acquire_singleton()
    if lock is None:
        print("AgentBell Bridge 已在运行（托盘里）")
        return
    log("===== %s 启动 =====" % APP_NAME)
    cfg = Config()

    import webview

    tray = {"icon": None}              # 先占位，回调里引用

    def on_state_change(online):
        icon = tray.get("icon")
        if icon:
            try:
                icon.icon = make_icon_image(online)
                icon.title = "%s — %s" % (APP_NAME, "在线" if online else "离线")
            except Exception:
                pass

    dm = DeviceManager(cfg, on_state_change)
    watcher = NotificationWatcher(
        cfg, get_host=lambda: dm.host, log=log,
        on_device_lost=dm.notify_lost)

    api = Api(cfg, dm, watcher)
    show_now = "--show" in sys.argv or IS_MAC  # mac：菜单栏图标不保证可用，启动即显示窗口
    window = webview.create_window(
        APP_NAME, _ui_path(), js_api=api,
        width=WIN_W, height=WIN_H, resizable=False,
        frameless=True, easy_drag=False,
        hidden=not show_now, background_color="#E4E3DF")
    api._window = window

    quitting = {"flag": False}
    tray_ok = {"flag": not IS_MAC}     # win：run_detached 即认为可用；mac：挂上才算

    def do_quit():
        if quitting["flag"]:
            return
        quitting["flag"] = True
        log("退出中……")
        watcher.stop()
        dm.stop()
        icon = tray.get("icon")
        if icon:
            try:
                icon.stop()
            except Exception:
                pass
        try:
            window.destroy()
        except Exception:
            pass
    api._on_quit = do_quit

    def on_closing():                  # 系统关闭（Alt+F4 / Cmd+W）→ 收进托盘
        if quitting["flag"]:
            return True
        if IS_MAC and not tray_ok["flag"]:
            do_quit()                  # mac 且菜单栏图标没挂上：藏了就找不回，直接退出
            return True
        try:
            window.hide()
        except Exception:
            pass
        return False
    window.events.closing += on_closing

    # —— 托盘 / 菜单栏 ——
    import pystray
    from pystray import MenuItem as MI

    def show_window(*_):
        try:
            window.show()
            window.restore()
        except Exception:
            pass

    def toggle_forward(*_):
        cfg.set("forward_enabled", not cfg.get("forward_enabled"))

    def tray_test(*_):
        threading.Thread(target=api.send_test, daemon=True).start()

    menu = pystray.Menu(
        MI("打开设置", show_window, default=True),
        MI("转发到设备", toggle_forward,
           checked=lambda item: bool(cfg.get("forward_enabled"))),
        MI("发测试通知", tray_test),
        MI("重新搜索设备", lambda *_: dm.force_rediscover()),
        pystray.Menu.SEPARATOR,
        MI("退出", lambda *_: do_quit()),
    )
    icon = pystray.Icon("agentbell-bridge", make_icon_image(False),
                        "%s — 启动中" % APP_NAME, menu)
    tray["icon"] = icon

    def _setup_tray_mac():
        """macOS：菜单栏图标必须挂在 pywebview 的 NSApplication 上、且在主线程建。
        失败只降级（窗口关闭即退出），不影响转发主功能。"""
        try:
            import AppKit
            from PyObjCTools import AppHelper
            nsapp = AppKit.NSApplication.sharedApplication()

            def make():
                try:
                    try:
                        icon.run_detached(darwin_nsapplication=nsapp)
                    except TypeError:          # 老版 pystray 没有这个参数
                        icon.run_detached()
                    tray_ok["flag"] = True
                    log("托盘：菜单栏图标已挂载")
                except Exception as e:
                    log("托盘：菜单栏图标创建失败（%r），关窗即退出" % e)
            AppHelper.callAfter(make)
        except Exception as e:
            log("托盘：pyobjc 不可用（%r），关窗即退出" % e)

    dm.start()
    watcher.start()

    try:
        if IS_MAC:
            webview.start(_setup_tray_mac, window)          # cocoa 后端
        else:
            icon.run_detached()
            webview.start(_round_corners, window, gui="edgechromium")
    finally:
        if not quitting["flag"]:
            do_quit()
    log("===== 已退出 =====")


if __name__ == "__main__":
    main()
