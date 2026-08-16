#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
diag.py — 通知监听自检（`AgentBellBridge --diag` 调用，不开界面/托盘，打完就退）。

为什么做进 App、而不是给用户一个 python 脚本：
macOS 的「完全磁盘访问权限」是**按可执行文件**授予的。用 Terminal 里的 python3
去读通知库，测的是 Terminal 有没有权限，跟 App 有没有权限是两件事——结论会误导。
所以自检必须由 App 自己的二进制跑。

用法（macOS）：
    "/Applications/AgentBell Bridge.app/Contents/MacOS/AgentBellBridge" --diag
"""
import os
import subprocess
import sys
import time


def _mac(cfg, watch_s=45):
    import sqlite3

    import watcher_mac as wm

    print("=== AgentBell 通知监听自检（macOS）===")
    print("可执行文件：%s" % sys.executable)
    try:
        print("系统版本：%s" % subprocess.check_output(
            ["sw_vers", "-productVersion"], text=True).strip())
    except Exception:
        pass

    print("")
    print("-- 通知库候选路径（新系统在前）--")
    found = None
    for cand in wm.db_candidates():
        try:
            ok = os.path.exists(cand)
        except Exception:
            ok = False
        extra = ""
        if ok:
            wal = cand + "-wal"
            wsz = os.path.getsize(wal) if os.path.exists(wal) else 0
            extra = "（%d 字节，WAL %d 字节）" % (os.path.getsize(cand), wsz)
        print("  [%s] %s%s" % ("存在" if ok else "不存在", cand, extra))
        if ok and not found:
            found = cand

    if not found:
        print("")
        print("结论：候选路径一个都不存在。要么系统又换了库位置，要么本 App 没有")
        print("「完全磁盘访问权限」——没权限时连 os.path.exists 都会是 False。")
        print("去 系统设置 > 隐私与安全性 > 完全磁盘访问权限，加入并勾选")
        print("AgentBell Bridge，然后重启本 App。")
        return

    print("")
    print("-- 读取（含 WAL）--")
    try:
        rows = wm._fetch_current(found, limit=400)
    except (sqlite3.OperationalError, PermissionError) as e:
        print("  读不了：%s" % e)
        print("")
        print("结论：路径找到了但读不了——就是「完全磁盘访问权限」没给本 App。")
        return
    print("  成功，读到 %d 条" % len(rows))

    keywords = [k.strip().lower() for k in (cfg.get("app_keywords") or []) if k.strip()]
    print("  当前关键词：%s" % (", ".join(keywords) or "(空)"))

    print("")
    print("-- 最近 12 条通知 --")
    print("  %-8s %-36s %-24s %s" % ("匹配", "发通知的应用(bundle)", "标题", "正文"))
    hits = 0
    for key, ident, blob in rows[-12:][::-1]:
        bundle, titl, subt, body = wm._parse_record(blob)
        bundle = bundle or ident or "?"
        b = bundle.lower()
        t = ("%s %s" % (titl, subt)).lower()
        by_b = next((k for k in keywords if k in b), None)
        by_t = next((k for k in keywords if k in t), None)
        if by_b:
            mark = "✓bundle"
        elif by_t:
            mark = "✓标题"
        else:
            mark = "  跳过"
        if by_b or by_t:
            hits += 1
        body1 = " ".join((body or "").split())
        print("  %-8s %-36s %-24s %s" % (mark, bundle[:36],
                                         (titl or subt or "-")[:24], body1[:40]))
    print("")
    print("  这 12 条里能匹配上的：%d 条" % hits)
    if not hits:
        print("  ⚠ 一条都匹配不上。看「发通知的应用」那一列：如果 Claude 的通知")
        print("    署名是别的 bundle（比如终端、脚本编辑器），把那个名字里的词")
        print("    加进设置面板的「应用关键词」即可。")

    print("")
    print("-- 实时观察 %d 秒：现在去让 Claude 完成一轮，看这里有没有新行 --" % watch_s)
    seen = set(k for k, _, _ in rows)
    t0 = time.time()
    n_new = 0
    while time.time() - t0 < watch_s:
        time.sleep(2)
        try:
            cur = wm._fetch_current(found, limit=400)
        except Exception as e:
            print("  读取出错：%r" % e)
            continue
        for key, ident, blob in cur:
            if key in seen:
                continue
            seen.add(key)
            n_new += 1
            bundle, titl, subt, body = wm._parse_record(blob)
            bundle = bundle or ident or "?"
            b = bundle.lower()
            t = ("%s %s" % (titl, subt)).lower()
            hit = next((k for k in keywords if k in b or k in t), None)
            verdict = ("会转发（命中 %s）" % hit) if hit else "不转发（关键词不匹配）"
            print("  +新通知 %-34s %-22s → %s"
                  % (bundle[:34], (titl or subt or "-")[:22], verdict))
    print("")
    print("  %d 秒内新增 %d 条。" % (watch_s, n_new))
    if n_new == 0:
        print("  ⚠ 一条新通知都没进库。问题在更上游：Claude 那边没真的发出系统通知")
        print("    （检查 系统设置>通知 里 Claude 的开关；它通常只在窗口失焦时才发），")
        print("    或者本 App 读的库不是系统正在写的那个。")


def _win(cfg):
    print("=== AgentBell 通知监听自检（Windows）===")
    print("可执行文件：%s" % sys.executable)
    print("当前关键词：%s" % (", ".join(cfg.get("app_keywords") or []) or "(空)"))
    try:
        from winrt.windows.ui.notifications.management import UserNotificationListener
        from winrt.windows.ui.notifications import NotificationKinds
        lis = UserNotificationListener.current
        print("访问状态：%s（1=已允许 2=被拒绝）" % lis.get_access_status())
        # WinRT 的 IAsyncOperation 用 .get() 同步等，不能塞给 asyncio（watcher.py 同款写法）
        notes = lis.get_notifications_async(NotificationKinds.TOAST).get()
        print("通知中心里 %d 条：" % len(notes))
        for n in list(notes)[:12]:
            try:
                app = n.app_info.display_info.display_name
            except Exception:
                app = "?"
            texts = []
            try:
                for t in n.notification.visual.bindings[0].get_text_elements():
                    texts.append(t.text)
            except Exception:
                pass
            print("  app=%-22s %s" % (app[:22], " | ".join(texts)[:60]))
    except Exception as e:
        print("读通知中心失败：%r" % e)
        print("请在 设置 > 隐私和安全性 > 通知 里允许本程序访问通知，然后重启。")


def run(cfg, is_mac):
    """入口：bridge.py 见到 --diag 就调这个。cfg 是 Config 实例。"""
    if is_mac:
        _mac(cfg)
    else:
        _win(cfg)
