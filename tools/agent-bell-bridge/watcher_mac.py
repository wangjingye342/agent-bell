#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
watcher_mac.py — macOS 版通知监听：轮询「通知中心」数据库，把 Claude / Codex
的新通知转发给 AgentBell。对外接口与 watcher.py（Windows 版）完全一致。

原理：macOS 把已送达的通知存在 SQLite 库
    $(getconf DARWIN_USER_DIR)com.apple.notificationcenter/db2/db
读这个路径需要授予本程序「完全磁盘访问权限」
（系统设置 > 隐私与安全性 > 完全磁盘访问权限），改完权限要重启本程序。

去重方式（**不能靠 rec_id 单调**）：record.rec_id 是 INTEGER PRIMARY KEY 而非
AUTOINCREMENT，用户清空通知中心后 sqlite 会复用 rowid，新通知的 rec_id 可能比
旧水位还小 —— 用「rec_id > 水位」会从此永远命中 0 行、再也不转发，而状态还是 ok
（本地 sqlite 实测复现过）。所以改成和 Windows 版一样每轮重建「已见」集合，
标识用 rec_id + 投递时间（没有该列时退回内容哈希），天然自愈。
通知正文是 binary plist：
    {'app': bundle_id, 'req': {'titl': 标题, 'subt': 副标题, 'body': 正文}}
关键词按 bundle id 匹配（如 com.anthropic.claudefordesktop 含 "claude"）。
"""
import hashlib
import os
import plistlib
import sqlite3
import subprocess
import threading
import time

from watcher import send_notify          # 转发是纯 stdlib，直接复用 Windows 版的


def db_candidates():
    """通知库的候选路径，新系统在前。

    macOS 12 (Monterey) 起 usernoted 把库搬进了 Group Containers；
    `$(getconf DARWIN_USER_DIR)com.apple.notificationcenter/db2/db` 是 11 及更早的
    位置，在新系统上**根本不存在** —— 只认老路径会一直判成「没权限」，
    而真实症状是：测试通知能响（那条不经过通知库），真通知永远不转发。
    """
    out = [os.path.join(os.path.expanduser("~"), "Library", "Group Containers",
                        "group.com.apple.usernoted", "db2", "db")]
    try:
        base = subprocess.check_output(
            ["getconf", "DARWIN_USER_DIR"], text=True, timeout=5).strip()
        out.append(os.path.join(base, "com.apple.notificationcenter", "db2", "db"))
    except Exception:
        pass
    return out


def _db_path():
    """第一个存在的候选路径；都不存在返回 None。"""
    for p in db_candidates():
        try:
            if os.path.exists(p):
                return p
        except Exception:
            continue
    return None


def _connect_ro(db_path):
    """只读打开通知库，返回 (connection, cleanup)。

    直接 mode=ro 打开最省事，但这个库是 WAL 模式：只读连接需要能用 -shm 共享内存，
    权限/沙箱不允许时会抛错，或者退化成看不到 -wal 里最新的几条 —— 那正是
    「状态一直绿着、却永远收不到新通知」的静默失效。所以失败就把
    db/-wal/-shm 三个文件快照到临时目录再正常打开，保证读到最新数据。
    """
    try:
        con = sqlite3.connect("file:%s?mode=ro" % db_path, uri=True, timeout=1.0)
        con.execute("SELECT 1 FROM record LIMIT 1")        # 真读一下才知道能不能读
        return con, (lambda: None)
    except sqlite3.OperationalError:
        import shutil
        import tempfile
        tmp = tempfile.mkdtemp(prefix="agentbell-nc-")
        dst = os.path.join(tmp, "db")
        for suffix in ("", "-wal", "-shm"):
            src = db_path + suffix
            if os.path.exists(src):
                shutil.copy2(src, dst + suffix)            # 带上 WAL，才有最新几条
        con = sqlite3.connect(dst, timeout=2.0)

        def cleanup():
            try:
                con.close()
            finally:
                shutil.rmtree(tmp, ignore_errors=True)
        return con, cleanup


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


def _record_cols(con):
    try:
        return {r[1] for r in con.execute("PRAGMA table_info(record)")}
    except Exception:
        return set()


def _fetch_current(db_path, limit=200):
    """读最近 limit 条通知，返回 [(key, bundle_or_ident, blob)]（按 rec_id 升序）。

    key 是不依赖 rowid 单调性的稳定标识：优先 rec_id + 投递时间列，
    没有该列时用 rec_id + 内容哈希。只读打开（uri mode=ro），不碰 WAL 写锁；
    权限不够时 sqlite 抛 OperationalError，由调用方按 no_access 处理。
    """
    con, cleanup = _connect_ro(db_path)
    try:
        cols = _record_cols(con)
        datecol = next((c for c in ("delivered_date", "presented_date", "date")
                        if c in cols), None)
        sel = ("r.rec_id, r.data,"
               " (SELECT identifier FROM app a WHERE a.app_id = r.app_id)")
        if datecol:
            sel += ", r.%s" % datecol
        rows = con.execute(
            "SELECT %s FROM record r ORDER BY r.rec_id DESC LIMIT ?" % sel,
            (limit,)).fetchall()
    finally:
        cleanup()
    out = []
    for row in rows:
        rec_id, blob, ident = row[0], row[1], row[2]
        stamp = row[3] if len(row) > 3 else None
        if stamp is None:
            try:
                stamp = hashlib.sha1(bytes(blob or b"")).hexdigest()[:12]
            except Exception:
                stamp = "?"
        out.append(("%s|%s" % (rec_id, stamp), ident or "", blob))
    out.reverse()                                # 转成升序，保持「最后一条最新」
    return out


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
        self._seen = set()          # 已处理过的通知标识（每轮重建，天然自愈）
        self._primed = False        # 首轮只登记存量不转发（开机别把历史通知全响一遍）
        self._last_sent = 0.0       # 冷却计时（monotonic）
        self._pending = None        # 冷却中/发送失败的通知，暂存待发
        self._pending_tries = 0
        self._title_match_noted = False

    def run(self):
        announced = False
        while not self.stop_event.is_set():
            # 库路径每轮重算：没给「完全磁盘访问权限」时 os.path.exists 也会是 False，
            # 一次算不到就退出线程的话，用户授完权得重启程序才恢复
            path = _db_path()
            if not path:
                if self.status != "no_access":
                    self.status = "no_access"
                    self.log("监听：读不到通知库——可能没给「完全磁盘访问权限」，"
                             "也可能系统换了库位置。给权限后本程序会自动恢复。")
                self.stop_event.wait(10)
                continue
            try:
                rows = _fetch_current(path)
                if self.status != "ok":
                    self.status = "ok"
                    if not announced:
                        announced = True
                        self.log("监听：通知库可读，开始轮询（存量 %d 条）；库路径 %s"
                                 % (len(rows), path))
                    else:
                        self.log("监听：权限恢复，继续轮询")
                self._flush_pending(time.monotonic())   # 先补发上轮欠着的
                self._process(rows, time.monotonic())
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
        """rows = [(key, bundle_or_ident, blob)]，按 rec_id 升序。

        每轮用当前全量重建 _seen（和 Windows 版同一策略）：rowid 被复用也不会漏，
        通知中心被清空也不会永久静默。
        """
        keywords = [k.strip().lower()
                    for k in self.cfg.get("app_keywords") or [] if k.strip()]
        fresh = []                                   # (agent, conversation, message)
        current = set()
        for key, ident, blob in rows:
            current.add(key)
            if not self._primed or key in self._seen:
                continue
            bundle, titl, subt, body = _parse_record(blob)
            bundle = bundle or ident
            b = (bundle or "").lower()
            hit = next((k for k in keywords if k in b), None)
            if not hit:
                # bundle id 不中时再看标题/副标题：命令行版 Claude Code 用
                # osascript 发通知，bundle 是「脚本编辑器」或终端的，只看 bundle
                # 会漏掉。只看标题不看正文，避免正文里提一句 claude 就误触。
                t = ("%s %s" % (titl, subt)).lower()
                hit = next((k for k in keywords if k in t), None)
                if hit and not self._title_match_noted:
                    self._title_match_noted = True
                    self.log("监听：按标题匹配到「%s」（发通知的应用是 %s，"
                             "不是它自己的 bundle）" % (hit, bundle or "?"))
            if not hit:
                continue
            agent = hit.capitalize()                 # claude → Claude（bundle id 没有显示名）
            conversation = titl or subt or "-"
            message = ("%s %s" % (subt, body)).strip() if titl and subt else body
            fresh.append((agent, conversation, message))
        self._seen = current
        if not self._primed:                         # 首轮：只登记存量，不转发
            self._primed = True
            return
        if not fresh or not self.cfg.get("forward_enabled"):
            return

        agent, conversation, message = fresh[-1]     # 一批只转发最新一条（设备只响一次）
        if len(fresh) > 1:
            message = ("[+%d条] " % (len(fresh) - 1)) + message
        note = {"agent": agent, "conversation": conversation, "message": message}

        # 冷却期内暂存而不是丢弃（语义是「一批只响一次」，不是「第二条消失」）
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
            if self.on_device_lost:
                self.on_device_lost()
        return False

    def _flush_pending(self, now):
        """每轮开头补发暂存的通知。"""
        if not self._pending or not self.cfg.get("forward_enabled"):
            return
        if now - self._last_sent < float(self.cfg.get("cooldown_s") or 3.0):
            return
        self._deliver(self._pending, now)

    def stop(self):
        self.stop_event.set()
