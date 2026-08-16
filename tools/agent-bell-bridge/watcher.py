#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
watcher.py — 盯着 Windows 通知中心，把 Claude / Codex 的新通知转发给 AgentBell。

原理：WinRT UserNotificationListener（需要用户在
「设置 > 隐私和安全性 > 通知 > 让应用访问通知」里允许）。
非打包 Python 进程用不了 notification_changed 事件（会抛 0x80070490），
所以每 poll_interval_s 秒轮询一次，按通知 id（单调递增）识别新通知。

这样 Claude / Codex 客户端不用装任何 hook：只要它们发系统通知，这里就能看到。
"""
import socket
import threading
import time

import device_api

# 设备端 Note 各字段缓冲大小（字节，留余量），按 UTF-8 边界截断
_LIMITS = {"computer": 22, "agent": 14, "conversation": 46, "message": 78}


def _clip(s, maxbytes):
    s = "" if s is None else str(s)
    b = s.encode("utf-8")
    if len(b) <= maxbytes:
        return s
    b = b[:maxbytes]
    while b:
        try:
            return b.decode("utf-8")
        except UnicodeDecodeError:
            b = b[:-1]
    return ""


def send_notify(host, port, agent, conversation, message, timeout=3.5):
    """POST /notify 给设备。成功 True，失败 False（不抛异常）。

    走 device_api 的串行锁：否则转发会和设备守护线程的探活撞在一起，
    设备一次只服务一个请求，两边一起超时（实测 4 并发失败率 43%）。
    """
    if not host:
        return False
    return device_api.send_notify(
        host, port, timeout=timeout,
        computer=_clip(socket.gethostname().split(".")[0], _LIMITS["computer"]),
        agent=_clip(agent, _LIMITS["agent"]),
        conversation=_clip(conversation, _LIMITS["conversation"]),
        message=_clip(message, _LIMITS["message"]))


class NotificationWatcher(threading.Thread):
    """后台线程：轮询通知中心 → 关键词过滤 → 冷却合并 → 转发。

    与外界的接口：
      get_host()        — 回调，取当前设备地址（None=离线）
      on_forwarded(n)   — 转发成功回调（托盘提示用）
      on_device_lost()  — 转发失败回调（触发重新发现）
      status            — "ok" / "no_access" / "error: ..."，UI 显示用
    """

    def __init__(self, cfg, get_host, log, on_forwarded=None, on_device_lost=None):
        super().__init__(name="notification-watcher", daemon=True)
        self.cfg = cfg
        self.get_host = get_host
        self.log = log
        self.on_forwarded = on_forwarded
        self.on_device_lost = on_device_lost
        self.stop_event = threading.Event()
        self.status = "starting"
        self._seen = set()          # 已处理过的通知 id
        self._primed = False        # 首轮只记 id 不转发（开机别把历史通知全响一遍）
        self._last_sent = 0.0       # 冷却计时（monotonic）
        self._pending = None        # 冷却中/发送失败的通知，暂存待发（只留最新一条）
        self._pending_tries = 0     # 已经为它重试过几次

    def run(self):
        try:
            from winrt.windows.ui.notifications.management import (
                UserNotificationListener, UserNotificationListenerAccessStatus as Access)
            from winrt.windows.ui.notifications import (
                NotificationKinds, KnownNotificationBindings)
        except Exception as e:
            self.status = "error: winrt 包未装好（%s）" % e
            self.log("监听：winrt 导入失败：%r" % e)
            return

        listener = UserNotificationListener.current
        try:
            access = listener.request_access_async().get()
        except Exception as e:
            self.status = "error: 请求通知权限失败（%s）" % e
            self.log("监听：request_access 失败：%r" % e)
            return
        if access != Access.ALLOWED:
            self.status = "no_access"
            self.log("监听：无通知访问权限（状态 %s）。请在 设置>隐私和安全性>通知 里允许。" % access)
            return

        self.status = "ok"
        self.log("监听：通知访问权限 OK，开始轮询")
        while not self.stop_event.is_set():
            try:
                if listener.get_access_status() != Access.ALLOWED:
                    self.status = "no_access"
                    self.log("监听：权限被撤销，暂停（重新授权后自动恢复）")
                    self.stop_event.wait(10)
                    continue
                self.status = "ok"
                self._flush_pending(time.monotonic())      # 先把上轮欠着的补发掉
                notes = listener.get_notifications_async(NotificationKinds.TOAST).get()
                self._process(list(notes), KnownNotificationBindings, time.monotonic())
            except Exception as e:
                self.status = "error: %s" % e
                self.log("监听：轮询出错（忽略继续）：%r" % e)
            self.stop_event.wait(float(self.cfg.get("poll_interval_s") or 2.0))

    def _process(self, notes, bindings, now):
        current = set()
        fresh = []                                   # (app, texts) 本轮新出现的匹配通知
        keywords = [k.strip().lower() for k in self.cfg.get("app_keywords") or [] if k.strip()]
        for n in notes:
            try:
                nid = n.id
                current.add(nid)
                if nid in self._seen or not self._primed:
                    continue
                try:
                    app = n.app_info.display_info.display_name or ""
                except OSError:                      # 部分 Win32 应用取不了 app_info
                    app = ""
                if not any(k in app.lower() for k in keywords):
                    continue
                texts = []
                try:
                    b = n.notification.visual.get_binding(bindings.toast_generic)
                    if b:
                        texts = [t.text for t in b.get_text_elements() if t.text]
                except OSError:
                    pass
                fresh.append((app, texts))
            except Exception:
                continue                             # 单条解析失败不影响其它
        # 已消失的 id 从 seen 清掉（id 单调递增不复用，只是防集合无限膨胀）
        self._seen = current
        if not self._primed:                         # 首轮：只登记存量，不转发
            self._primed = True
            return
        if not fresh or not self.cfg.get("forward_enabled"):
            return

        app, texts = fresh[-1]                       # 一批只转发最新一条（设备只响一次）
        note = {
            "agent": app,
            "conversation": texts[0] if texts else "-",
            "message": " ".join(texts[1:]) if len(texts) > 1 else "",
        }
        if len(fresh) > 1:
            note["message"] = ("[+%d条] " % (len(fresh) - 1)) + note["message"]

        # 冷却期内不丢弃，暂存成「待发」——下一轮冷却过了会补发。
        # 冷却的语义是「一批只响一次」，不该是「第二条静默消失」。
        cooldown = float(self.cfg.get("cooldown_s") or 3.0)
        if now - self._last_sent < cooldown:
            self._pending, self._pending_tries = note, 0
            self.log("监听：%d 条新通知在冷却期内，暂存待发" % len(fresh))
            return
        self._deliver(note, now)

    # ---------- 投递：失败进暂存，下一轮继续（通知不能因为一次丢包就永久消失） ----------
    MAX_TRIES = 3

    def _deliver(self, note, now):
        host = self.get_host()
        if not host:
            self._pending, self._pending_tries = note, 0
            self.log("监听：有新通知但设备离线，暂存待发（%s）" % note["agent"])
            return False
        ok = send_notify(host, self.cfg.get("device_port"),
                         note["agent"], note["conversation"], note["message"])
        if ok:
            self._last_sent = now
            self._pending, self._pending_tries = None, 0
            self.log("转发：%s | %s" % (note["agent"], note["conversation"]))
            if self.on_forwarded:
                self.on_forwarded(note["agent"], note["conversation"])
            return True
        self._pending = note
        self._pending_tries += 1
        if self._pending_tries < self.MAX_TRIES:
            self.log("转发：失败，第 %d/%d 次重试待发（%s）"
                     % (self._pending_tries, self.MAX_TRIES, note["agent"]))
        else:
            self._pending, self._pending_tries = None, 0
            self.log("转发：%d 次都失败，放弃这条（设备可能掉线）" % self.MAX_TRIES)
            if self.on_device_lost:            # 只有重试预算用尽才怀疑连接
                self.on_device_lost()
        return False

    def _flush_pending(self, now):
        """每轮开头补发暂存的通知（冷却过了、设备回来了就发出去）。"""
        if not self._pending or not self.cfg.get("forward_enabled"):
            return
        if now - self._last_sent < float(self.cfg.get("cooldown_s") or 3.0):
            return
        self._deliver(self._pending, now)

    def stop(self):
        self.stop_event.set()
