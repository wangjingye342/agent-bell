#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
watcher_mac.py — macOS 版通知监听：轮询「通知中心」数据库，把 Claude / Codex
的新通知转发给 AgentBell。对外接口与 watcher.py（Windows 版）完全一致。

原理：macOS 把已送达的通知存在 SQLite 库
    $(getconf DARWIN_USER_DIR)com.apple.notificationcenter/db2/db
读这个路径需要授予本程序「完全磁盘访问权限」
（系统设置 > 隐私与安全性 > 完全磁盘访问权限），改完权限要重启本程序。

record.rec_id 单调递增：首轮只记水位不转发（开机别把历史通知全响一遍），
之后每轮查 rec_id > 水位 的新行。通知正文是 binary plist：
    {'app': bundle_id, 'req': {'titl': 标题, 'subt': 副标题, 'body': 正文}}
关键词按 bundle id 匹配（如 com.anthropic.claudefordesktop 含 "claude"）。
"""
import os
import plistlib
import sqlite3
import subprocess
import threading
import time

from watcher import send_notify          # 转发是纯 stdlib，直接复用 Windows 版的


def _db_path():
    """通知中心数据库路径；拿不到（非 macOS / 系统改了布局）返回 None。"""
    try:
        base = subprocess.check_output(
            ["getconf", "DARWIN_USER_DIR"], text=True, timeout=5).strip()
    except Exception:
        return None
    p = os.path.join(base, "com.apple.notificationcenter", "db2", "db")
    return p if os.path.exists(p) else None


def _parse_record(blob):
    """record.data（binary plist）→ (bundle_id, title, subtitle, body)。
    解析失败返回 (None, "", "", "")，调用方跳过该条。"""
    try:
        d = plistlib.loads(bytes(blob))
        req = d.get("req") or {}
        return (d.get("app"), str(req.get("titl") or ""),
                str(req.get("subt") or ""), str(req.get("body") or ""))
    except Exception:
        return None, "", "", ""


def _fetch_new(db_path, watermark):
    """读 rec_id > watermark 的新通知。返回 (新水位, [(bundle, titl, subt, body)])。

    只读打开（uri mode=ro），不碰 WAL 写锁；权限不够时 sqlite 抛
    OperationalError，由调用方按 no_access 处理。watermark 为 None 表示
    首轮定水位：只查 max(rec_id)，不取内容。
    """
    con = sqlite3.connect("file:%s?mode=ro" % db_path, uri=True, timeout=1.0)
    try:
        if watermark is None:
            row = con.execute("SELECT COALESCE(MAX(rec_id), 0) FROM record").fetchone()
            return int(row[0]), []
        rows = con.execute(
            "SELECT r.rec_id, r.data,"
            " (SELECT identifier FROM app a WHERE a.app_id = r.app_id)"
            " FROM record r WHERE r.rec_id > ? ORDER BY r.rec_id", (watermark,)
        ).fetchall()
        out, top = [], watermark
        for rec_id, blob, ident in rows:
            top = max(top, int(rec_id))
            bundle, titl, subt, body = _parse_record(blob)
            out.append((bundle or ident or "", titl, subt, body))
        return top, out
    finally:
        con.close()


class NotificationWatcher(threading.Thread):
    """后台线程：轮询通知库 → 关键词过滤 → 冷却合并 → 转发。

    与外界的接口（同 watcher.py）：
      get_host()        — 回调，取当前设备地址（None=离线）
      on_forwarded(a,c) — 转发成功回调
      on_device_lost()  — 转发失败回调（触发重新发现）
      status            — "starting"/"ok"/"no_access"/"error: ..."，UI 显示用
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
        self._watermark = None      # 已处理到的 rec_id；None=还没定首轮水位
        self._last_sent = 0.0       # 冷却计时（monotonic）

    def run(self):
        path = _db_path()
        if not path:
            self.status = "error: 找不到通知库（系统版本太新？）"
            self.log("监听：通知中心数据库不存在，无法监听")
            return
        announced = False
        while not self.stop_event.is_set():
            try:
                self._watermark, fresh = _fetch_new(path, self._watermark)
                if self.status != "ok":
                    self.status = "ok"
                    if not announced:
                        announced = True
                        self.log("监听：通知库可读，开始轮询（水位 %s）" % self._watermark)
                    else:
                        self.log("监听：权限恢复，继续轮询")
                if fresh:
                    self._process(fresh, time.monotonic())
            except (sqlite3.OperationalError, PermissionError) as e:
                # 大概率是没给「完全磁盘访问权限」；给权限后重启本程序
                if self.status != "no_access":
                    self.status = "no_access"
                    self.log("监听：读不了通知库（%s）。请在 系统设置>隐私与安全性>"
                             "完全磁盘访问权限 里允许本程序后重启。" % e)
                self.stop_event.wait(10)
                continue
            except Exception as e:
                self.status = "error: %s" % e
                self.log("监听：轮询出错（忽略继续）：%r" % e)
            self.stop_event.wait(float(self.cfg.get("poll_interval_s") or 2.0))

    def _process(self, rows, now):
        """rows = [(bundle, titl, subt, body)]，已按 rec_id 升序。"""
        keywords = [k.strip().lower()
                    for k in self.cfg.get("app_keywords") or [] if k.strip()]
        fresh = []                                   # (agent, conversation, message)
        for bundle, titl, subt, body in rows:
            b = (bundle or "").lower()
            hit = next((k for k in keywords if k in b), None)
            if not hit:
                continue
            agent = hit.capitalize()                 # claude → Claude（bundle id 没有显示名）
            conversation = titl or subt or "-"
            message = ("%s %s" % (subt, body)).strip() if titl and subt else body
            fresh.append((agent, conversation, message))
        if not fresh or not self.cfg.get("forward_enabled"):
            return

        cooldown = float(self.cfg.get("cooldown_s") or 3.0)
        if now - self._last_sent < cooldown:
            self.log("监听：%d 条新通知在冷却期内，跳过" % len(fresh))
            return

        agent, conversation, message = fresh[-1]     # 一批只转发最新一条（设备只响一次）
        if len(fresh) > 1:
            message = ("[+%d条] " % (len(fresh) - 1)) + message

        host = self.get_host()
        if not host:
            self.log("监听：有新通知但设备离线，丢弃（%s）" % agent)
            if self.on_device_lost:
                self.on_device_lost()
            return
        ok = send_notify(host, self.cfg.get("device_port"), agent, conversation, message)
        if ok:
            self._last_sent = now
            self.log("转发：%s | %s" % (agent, conversation))
            if self.on_forwarded:
                self.on_forwarded(agent, conversation)
        else:
            self.log("转发：失败（设备可能掉线），触发重新发现")
            if self.on_device_lost:
                self.on_device_lost()

    def stop(self):
        self.stop_event.set()
