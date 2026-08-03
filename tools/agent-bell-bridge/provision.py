#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
provision.py — 陌生网络一键配网：把 AgentBell 接进这台电脑所在的 WiFi。

场景：带着设备到新环境（没有烧录条件），设备连不上旧 WiFi 会自动开热点
AgentBell-XXXX。本程序在电脑上跑一遍即可完成接入：

  1. 读出本机当前连接的 WiFi 名称和密码（netsh，需要本机保存过该网络）
  2. 扫描附近 WiFi，找到 AgentBell-XXXX 热点
  3. 临时切换到该热点，把凭据 POST 给设备（设备存 NVS 后重启去连新 WiFi）
  4. 把电脑切回原 WiFi
  5. 等设备上线，把 IP 写进桥接程序缓存（bridge 一启动就能用）

用法（普通用户权限即可；WiFi 密码读取需本机保存过配置文件）：
    python provision.py            # 全自动
    python provision.py --ssid X --pass Y   # 手动指定要配的 WiFi（跳过第 1 步）

限制：设备只支持 2.4GHz。若电脑连的是 5GHz 同名双频网络没关系——设备按名字连，
路由器会把它落在 2.4G 频段；若是 5G 专属名（如 xxx_5G），请用 --ssid 指定 2.4G 的名字。
"""
import argparse
import json
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from bridge_config import Config   # noqa: E402
import discovery                    # noqa: E402

AP_PREFIX = "AgentBell-"
AP_PASS = "agentbell"
AP_HOST = "192.168.4.1"
_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # 绕过系统代理

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def say(msg):
    print(msg, flush=True)


def run(cmd, timeout=20):
    """跑命令返回 stdout 文本（netsh 在中文系统输出 GBK，按 mbcs 解）。"""
    out = subprocess.run(cmd, capture_output=True, timeout=timeout,
                         creationflags=subprocess.CREATE_NO_WINDOW)
    for enc in ("utf-8", "mbcs"):
        try:
            return out.stdout.decode(enc)
        except UnicodeDecodeError:
            continue
    return out.stdout.decode("utf-8", "replace")


# ---------------------------------------------------------------------------
#  第 1 步：本机当前 WiFi 的 SSID + 密码
# ---------------------------------------------------------------------------
def profile_password(ssid):
    """从本机已保存的 WiFi 配置里读密码（不要求当前连着它）。读不到返回 None。"""
    txt = run(["netsh", "wlan", "show", "profile", "name=%s" % ssid, "key=clear"])
    for line in txt.splitlines():
        if ("Key Content" in line) or ("关键内容" in line):
            return line.split(":", 1)[1].strip()
    return None


def saved_profiles():
    """本机保存过的 WiFi 名列表（配网候选；台式机插网线时靠它选）。"""
    txt = run(["netsh", "wlan", "show", "profiles"])
    names = []
    for line in txt.splitlines():
        # 中文「所有用户配置文件 : X」/ 英文「All User Profile     : X」
        if ("配置文件" in line or "Profile" in line) and ":" in line:
            name = line.split(":", 1)[1].strip()
            if name and name not in names and "<" not in name:
                names.append(name)
    return names


def current_wifi():
    """当前连接的 (ssid, 密码)。没连 WiFi / 拿不到密码返回 (ssid|None, None)。"""
    txt = run(["netsh", "wlan", "show", "interfaces"])
    m = re.search(r"^\s*SSID\s*:\s*(.+?)\s*$", txt, re.M)   # 中英文系统该行都叫 SSID
    ssid = m.group(1) if m else None
    if not ssid:
        return None, None
    return ssid, profile_password(ssid)


# ---------------------------------------------------------------------------
#  第 2 步：找 AgentBell 热点
# ---------------------------------------------------------------------------
def scan_for_ap(tries=6):
    """netsh 扫描附近 WiFi，返回第一个 AgentBell-XXXX 的 SSID；找不到返回 None。"""
    for i in range(tries):
        txt = run(["netsh", "wlan", "show", "networks"])
        m = re.search(r"^\s*SSID\s+\d+\s*:\s*(AgentBell-[0-9A-Fa-f]{4})\s*$", txt, re.M)
        if m:
            return m.group(1)
        if i < tries - 1:
            say("  未发现设备热点，%ds 后重扫（%d/%d）……" % (5, i + 1, tries))
            time.sleep(5)
    return None


# ---------------------------------------------------------------------------
#  第 3 步：连热点 → 推凭据
# ---------------------------------------------------------------------------
PROFILE_XML = """<?xml version="1.0"?>
<WLANProfile xmlns="http://www.microsoft.com/networking/WLAN/profile/v1">
  <name>{ssid}</name>
  <SSIDConfig><SSID><name>{ssid}</name></SSID></SSIDConfig>
  <connectionType>ESS</connectionType>
  <connectionMode>manual</connectionMode>
  <MSM><security>
    <authEncryption>
      <authentication>WPA2PSK</authentication>
      <encryption>AES</encryption>
      <useOneX>false</useOneX>
    </authEncryption>
    <sharedKey>
      <keyType>passPhrase</keyType>
      <protected>false</protected>
      <keyMaterial>{password}</keyMaterial>
    </sharedKey>
  </security></MSM>
</WLANProfile>"""


def connect_wifi(ssid, password=None, wait_s=25):
    """连接指定 WiFi（必要时先导入配置文件）。成功返回 True。"""
    if password:
        import os
        import tempfile
        xml = PROFILE_XML.format(ssid=ssid, password=password)
        fd, path = tempfile.mkstemp(suffix=".xml")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(xml)
            run(["netsh", "wlan", "add", "profile", "filename=%s" % path,
                 "user=current"])
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass
    run(["netsh", "wlan", "connect", "name=%s" % ssid])
    t0 = time.time()
    while time.time() - t0 < wait_s:
        txt = run(["netsh", "wlan", "show", "interfaces"])
        if re.search(r"^\s*SSID\s*:\s*%s\s*$" % re.escape(ssid), txt, re.M) and \
           re.search(r":\s*(connected|已连接)\s*$", txt, re.M | re.I):
            return True
        time.sleep(1.5)
    return False


def push_credentials(ssid, password, timeout=6):
    """向 192.168.4.1 提交新 WiFi。设备应答后会自行重启。"""
    # 先确认对面真是 AgentBell（防连错热点）
    try:
        with _OPENER.open("http://%s/api/info" % AP_HOST, timeout=timeout) as r:
            info = json.loads(r.read().decode("utf-8", "replace"))
        if info.get("app") != "agent-bell":
            say("  ⚠ %s 应答了但不是 AgentBell，中止" % AP_HOST)
            return False
    except Exception as e:
        say("  ⚠ 访问设备配置页失败：%s" % e)
        return False
    data = urllib.parse.urlencode({"ssid": ssid, "pass": password or ""}).encode()
    try:
        with _OPENER.open(urllib.request.Request(
                "http://%s/wifi" % AP_HOST, data=data, method="POST"), timeout=timeout):
            return True
    except Exception as e:
        # 设备收到后立即重启，连接被掐断也算成功
        say("  （设备重启中断连接：%s——正常）" % e)
        return True


# ---------------------------------------------------------------------------
#  main
# ---------------------------------------------------------------------------
def main():
    p = argparse.ArgumentParser(description="AgentBell 一键配网（陌生网络）")
    p.add_argument("--ssid", help="要配给设备的 WiFi 名（默认取本机当前 WiFi）")
    p.add_argument("--pass", dest="password", help="该 WiFi 的密码（默认自动读取）")
    a = p.parse_args()

    say("① 获取要配置的 WiFi……")
    if a.ssid:
        ssid, password = a.ssid, a.password or ""
    else:
        ssid, password = current_wifi()
        if not ssid:
            # 没连 WiFi（台式机插网线等）：从保存过的配置里挑（通常第一个就是家里路由器）
            names = saved_profiles()
            if not names:
                say("  ✗ 本机没连 WiFi 也没有保存过的 WiFi。请用 --ssid/--pass 指定。")
                return 1
            say("  本机未连 WiFi，从已保存的网络中选择：")
            for i, n in enumerate(names):
                say("    [%d] %s" % (i + 1, n))
            try:
                pick = input("  选哪个配给设备？(1-%d，回车=1)：" % len(names)).strip()
            except EOFError:
                pick = ""
            idx = int(pick) - 1 if pick.isdigit() and 0 < int(pick) <= len(names) else 0
            ssid = names[idx]
            password = profile_password(ssid)
            if password is None:
                say("  ⚠ 读不到「%s」的密码，按开放网络处理；有密码请用 --pass 指定。" % ssid)
                password = ""
        elif password is None:
            say("  ⚠ 读不到「%s」的密码（企业网/开放网？）。开放网络将按无密码配置；"
                "有密码请用 --pass 指定。" % ssid)
            password = ""
    say("  ✔ 目标 WiFi：%s" % ssid)
    home_ssid = current_wifi()[0]            # 记住配网前连的网，最后切回来

    say("② 扫描 AgentBell 热点（请确认设备屏幕显示「WiFi 配网模式」）……")
    ap = scan_for_ap()
    if not ap:
        say("  ✗ 没找到 AgentBell-XXXX 热点。设备菜单里选「重新配网」，或等它连不上 WiFi 自动开热点（开机约 20 秒后）。")
        return 1
    say("  ✔ 找到 %s" % ap)

    say("③ 连接设备热点并推送凭据……")
    if not connect_wifi(ap, AP_PASS):
        say("  ✗ 连不上 %s（密码 %s）。可手动连它后访问 http://%s 网页配网。"
            % (ap, AP_PASS, AP_HOST))
        return 1
    time.sleep(2)                            # 等 DHCP 拿到 192.168.4.x
    ok = push_credentials(ssid, password)
    if not ok:
        return 1
    say("  ✔ 已推送，设备正在重启连接「%s」" % ssid)

    if home_ssid and home_ssid != ap:
        say("④ 电脑切回 %s ……" % home_ssid)
        connect_wifi(home_ssid)              # 已保存的网络，不用密码
    else:
        say("④ （本机原本没连 WiFi，跳过切回）")

    say("⑤ 等设备上线（最多 90 秒）……")
    cfg = Config()
    t0 = time.time()
    host = None
    while time.time() - t0 < 90:
        time.sleep(6)
        host = discovery.find_device(cfg)    # 缓存 → mDNS → 扫网段；命中会写回缓存
        if host:
            break
    if host:
        info = discovery.probe_info(host) or {}
        say("  ✔ 设备已上线：%s（信号 %sdBm）" % (host, info.get("rssi", "?")))
        say("完成！桥接程序（bridge.py）现在启动即可直接使用。")
        return 0
    say("  ⚠ 90 秒内没等到设备。可能密码不对（设备会退回配网热点，重跑本程序），"
        "或设备连的 2.4G 网段与电脑不同网段（看设备屏幕菜单「设备状态」里的 IP）。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
