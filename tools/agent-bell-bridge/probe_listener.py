#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_listener.py — 实测 WinRT UserNotificationListener 在非打包 Python 下的可用性。

诊断脚本：权限状态 + 当前通知中心里每条 toast 的 id/应用名/文本。
依赖与 watcher.py 相同（install.bat 已装的 winrt-* 模块化包，不是废弃的 winsdk）。
"""
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from winrt.windows.ui.notifications.management import (
    UserNotificationListener, UserNotificationListenerAccessStatus as Access,
)
from winrt.windows.ui.notifications import NotificationKinds, KnownNotificationBindings


def main():
    listener = UserNotificationListener.current
    try:
        status = listener.request_access_async().get()
    except Exception as e:
        print("request_access_async 抛异常:", repr(e))
        return
    print("访问状态:", status, "(0=Unspecified 1=Allowed 2=Denied)")
    if status != Access.ALLOWED:
        print("未授权 — 需要在 设置>隐私和安全性>通知 里允许应用访问通知")
        return

    notes = listener.get_notifications_async(NotificationKinds.TOAST).get()
    print("当前通知中心里的 toast 数:", notes.size)
    for n in notes:
        try:
            app = n.app_info.display_info.display_name
        except Exception as e:
            app = f"<app_info 失败 {e!r}>"
        try:
            binding = n.notification.visual.get_binding(
                KnownNotificationBindings.toast_generic)
            texts = [t.text for t in binding.get_text_elements()] if binding else []
        except Exception as e:
            texts = [f"<取文本失败 {e!r}>"]
        print(f"  id={n.id}  app={app!r}  texts={texts}")


if __name__ == "__main__":
    main()
