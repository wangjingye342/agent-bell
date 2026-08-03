#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
discovery.py — 在局域网里找 AgentBell 设备，并持续盯着它别掉线。

查找顺序（快到慢）：
  1. 配置里缓存的上次 IP（毫秒级）
  2. mDNS 名 agent-bell.local（部分 Windows 网络解析不了）
  3. 全网段并发扫描：对本机每个私网 IP 所在的 /24，逐个探 /api/info

判定标准：GET /api/info 返回 JSON 且 app == "agent-bell"（固件端点）；
老固件没有 /api/info 时退回探 /healthz == "ok"（弱判定，仅在 mDNS/缓存名中招时用，
子网扫描必须强判定，避免把别的设备误认成 AgentBell）。
"""
import concurrent.futures
import ipaddress
import json
import socket
import threading
import urllib.request

# 绕过系统代理直连局域网（TUN 全局代理会拦截到设备的连接）
_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def _http_get(host, port, path, timeout):
    """GET http://host:port/path → (status, body_str)；失败抛异常。"""
    url = "http://%s:%d%s" % (host, port, path)
    with _OPENER.open(url, timeout=timeout) as r:
        return r.status, r.read(4096).decode("utf-8", "replace")


def probe_info(host, port=80, timeout=1.5):
    """强判定：/api/info 是我们的设备则返回 info dict，否则 None。"""
    try:
        st, body = _http_get(host, port, "/api/info", timeout)
        if st != 200:
            return None
        info = json.loads(body)
        if isinstance(info, dict) and info.get("app") == "agent-bell":
            return info
    except Exception:
        pass
    return None


def probe_alive(host, port=80, timeout=1.5):
    """弱判定：/healthz 回 ok（老固件无 /api/info 时的探活）。"""
    try:
        st, body = _http_get(host, port, "/healthz", timeout)
        return st == 200 and body.strip() == "ok"
    except Exception:
        return False


def check_device(host, port=80, timeout=1.5):
    """host 是不是（还活着的）AgentBell：优先强判定，退回弱判定。"""
    if not host:
        return False
    if probe_info(host, port, timeout) is not None:
        return True
    return probe_alive(host, port, timeout)


def _local_subnets():
    """本机所有私网 IPv4 所在的 /24 网段（去重）。"""
    nets, seen = [], set()
    try:
        infos = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)
        ips = {i[4][0] for i in infos}
    except Exception:
        ips = set()
    # 补一个「对外路由源地址」——多网卡/虚拟网卡时 gethostname 可能漏掉真实网卡
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("223.5.5.5", 80))          # 不真发包，仅取路由源 IP
        ips.add(s.getsockname()[0])
        s.close()
    except Exception:
        pass
    for ip in ips:
        try:
            addr = ipaddress.IPv4Address(ip)
            if not addr.is_private or addr.is_loopback:
                continue
            # 排除 198.18.0.0/15（基准测试段，Clash 等代理的 TUN fake-ip 网卡用它，扫了白扫）
            if addr in ipaddress.IPv4Network("198.18.0.0/15"):
                continue
            net = ipaddress.IPv4Network(ip + "/24", strict=False)
            if net not in seen:
                seen.add(net)
                nets.append(net)
        except Exception:
            continue
    return nets


def scan_subnets(port=80, timeout=1.0, on_progress=None, stop_event=None):
    """并发扫描本机所有 /24 网段的 /api/info。找到即停，返回 (ip, info) 或 (None, None)。

    on_progress(done, total) 供 UI 显示进度；stop_event 置位则中止。
    """
    targets = [str(h) for net in _local_subnets() for h in net.hosts()]
    if not targets:
        return None, None
    total, done = len(targets), 0
    found = threading.Event()
    result = [None, None]

    def probe_one(ip):
        nonlocal done
        if found.is_set() or (stop_event and stop_event.is_set()):
            return
        info = probe_info(ip, port, timeout)
        done += 1                              # GIL 下自增够用；进度仅供显示
        if on_progress:
            on_progress(done, total)
        if info and not found.is_set():
            result[0], result[1] = ip, info
            found.set()

    ex = concurrent.futures.ThreadPoolExecutor(max_workers=64)
    futs = [ex.submit(probe_one, ip) for ip in targets]
    for f in futs:
        if found.is_set() or (stop_event and stop_event.is_set()):
            break
        try:
            f.result()
        except Exception:
            pass
    # 找到/被叫停就不等剩余探测收尾（每个 socket 最长 timeout 秒），直接放它们自生自灭
    ex.shutdown(wait=False, cancel_futures=True)
    return result[0], result[1]


def find_device(cfg, log=None, on_progress=None, stop_event=None):
    """按「缓存 IP → mDNS → 子网扫描」找设备。找到返回 host 并写回缓存，找不到返回 None。"""
    def _log(msg):
        if log:
            log(msg)

    port = int(cfg.get("device_port") or 80)

    cached = (cfg.get("device_host") or "").strip()
    if cached and check_device(cached, port):
        _log("发现：缓存地址可用 %s" % cached)
        return cached

    mdns = "agent-bell.local"
    if check_device(mdns, port, timeout=2.5):
        _log("发现：mDNS 可用 %s" % mdns)
        try:                                   # 顺手解析成 IP 存缓存（IP 比 mDNS 名更快更稳）
            ip = socket.gethostbyname(mdns)
            if probe_info(ip, port):
                cfg.set("device_host", ip)
                return ip
        except Exception:
            pass
        cfg.set("device_host", mdns)
        return mdns

    _log("发现：开始扫描局域网……")
    ip, info = scan_subnets(port, on_progress=on_progress, stop_event=stop_event)
    if ip:
        _log("发现：扫描命中 %s（%s）" % (ip, (info or {}).get("mac", "?")))
        cfg.set("device_host", ip)
        return ip
    _log("发现：局域网内未找到设备")
    return None
