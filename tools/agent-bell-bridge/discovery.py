#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
discovery.py — 在局域网里找 AgentBell 设备，并持续盯着它别掉线。

查找顺序（快到慢）：
  1. **UDP 广播**（问 47317，固件里有应答器）——一个包，实测 0.21 秒，且 IP 变了也能找到
  2. 配置里缓存的上次 IP（老固件没有广播应答器时靠它）
  3. **原始 mDNS 组播查询**（自己发 UDP 5353，不走系统 DNS）
  4. **ARP 邻居**：系统 ARP 表里活着的十几个地址，纯出站 TCP（防火墙不拦）
  5. 子网扫描：先缓存 IP 所在的 /24，再扩到其它网卡（**最后手段**，见下）

为什么把子网扫描压到最后：实测它不只是慢，那 253 路并发 TCP/ARP 突发会把设备
自己的响应延迟推过客户端超时——漏检率空闲时 5%、风暴中 20%、风暴停止 2 秒后
仍有 33%。也就是说扫描器会把它要找的设备打趴，然后报告「没找到」。

为什么不用 socket.gethostbyname("agent-bell.local")：本机装了 Clash 一类的
TUN 代理时，.local 域名会被 fake-ip 劫持到 198.18.0.0/15 然后被 RST，
实测 0.02 秒就返回 RemoteDisconnected —— 系统 DNS 这条路是死的。
而设备的 mDNS 应答本身是好的（实测直接发 5353 组播 3/3 命中），
所以自己发组播查询：既躲开劫持，又比全网段扫描快两个数量级。

判定标准：GET /api/info 返回 JSON 且 app == "agent-bell"（强判定）；
老固件没有 /api/info 时退回探 /healthz == "ok"（弱判定，仅用于探活已知地址，
子网扫描一律强判定，避免把别的设备误认成 AgentBell）。
"""
import concurrent.futures
import ipaddress
import json
import re
import socket
import struct
import subprocess
import sys
import threading
import urllib.request

import device_api

MDNS_NAME = "agent-bell"
MDNS_ADDR = "224.0.0.251"
MDNS_PORT = 5353
DISCO_PORT = 47317          # 固件的 UDP 发现应答端口（见 agent_bell.ino discoveryTick）
DISCO_MAGIC = b"AGENTBELL?"

# 扫描参数（按实测定：设备成功响应的延迟 p90 可达 1.2 秒，1.0 秒超时会漏掉
# 10~20% 的探测——日志里「扫描 4 秒就说没找到」正是这么来的）
SCAN_TIMEOUT = 2.5
SCAN_WORKERS = 24        # 设备 backlog 只有 4，64 路并发 SYN 打过去反而更容易漏

# 不值得扫的网段：扫了纯浪费，还会把真实局域网的探测挤掉
_SKIP_NETS = [
    ipaddress.IPv4Network("198.18.0.0/15"),    # 基准测试段：Clash 等 TUN 的 fake-ip 网卡
    ipaddress.IPv4Network("100.64.0.0/10"),    # 运营商级 NAT / Tailscale
    ipaddress.IPv4Network("169.254.0.0/16"),   # link-local（没拿到 DHCP 时的自分配地址）
    ipaddress.IPv4Network("172.17.0.0/16"),    # Docker 默认桥
    ipaddress.IPv4Network("240.0.0.0/4"),      # 保留段（顺带挡住被当成地址的 255.x 掩码）
]


def _is_netmask(ip):
    """看起来像子网掩码（连续 1 后面全 0）就不是主机地址。

    从 ipconfig / ifconfig 的输出里正则捞点分四段时，掩码会一起被捞出来；
    255.255.255.0 恰好落在 Python 认作 private 的 240.0.0.0/4 里，
    不挡掉就会凭空多扫 254 个必然超时的地址。
    """
    try:
        v = int(ipaddress.IPv4Address(ip))
    except Exception:
        return True
    if v == 0:
        return True
    inv = (~v) & 0xFFFFFFFF                       # 连续 1 的反码必是 2^n-1
    return (inv & (inv + 1)) == 0


def _http_get(host, port, path, timeout):
    """GET http://host:port/path → (status, body_str)；失败抛异常。"""
    url = "http://%s:%d%s" % (host, int(port or 80), path)
    with device_api.OPENER.open(url, timeout=timeout) as r:
        return r.status, r.read(4096).decode("utf-8", "replace")


def probe_info(host, port=80, timeout=2.5):
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


def probe_alive(host, port=80, timeout=2.5):
    """弱判定：/healthz 回 ok（老固件无 /api/info 时的探活）。"""
    try:
        st, body = _http_get(host, port, "/healthz", timeout)
        return st == 200 and body.strip() == "ok"
    except Exception:
        return False


def check_device(host, port=80, timeout=3.0):
    """host 是不是（还活着的）AgentBell：强判定为主，剩余预算再试弱判定。

    注意 urllib 的 timeout 是每个 socket 操作的上限，两段串行探测最坏会花
    2×timeout；这里把总预算切成两半，让「timeout」名副其实。
    """
    if not host:
        return False
    half = max(1.0, timeout / 2.0)
    if probe_info(host, port, half) is not None:
        return True
    return probe_alive(host, port, half)


# ============================================================================
#  UDP 广播发现：一个包换掉 254 次 TCP 扫描
#  实测子网扫描不只是慢——那 253 路并发 TCP/ARP 突发会把设备自己的响应延迟推过
#  客户端超时（漏检率从空闲 5% 涨到风暴中 20%、风暴停后 2 秒仍 33%），
#  等于扫描器把要找的设备打趴了。所以能广播就绝不扫描。
# ============================================================================
def _udp_discover(timeout=0.6):
    """向本机各网段广播 AGENTBELL?，返回第一个应答设备的 IP（没有则 None）。"""
    targets = ["255.255.255.255"]
    for ip in _local_ips():
        try:
            net = ipaddress.IPv4Network(ip + "/24", strict=False)
            targets.append(str(net.broadcast_address))
        except Exception:
            continue
    socks = []
    for src in _local_ips() + ["0.0.0.0"]:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            s.bind((src, 0))
            s.settimeout(timeout)
            for dst in targets:
                try:
                    s.sendto(DISCO_MAGIC, (dst, DISCO_PORT))
                except Exception:
                    pass
            socks.append(s)
        except Exception:
            try:
                s.close()
            except Exception:
                pass
    try:
        for s in socks:
            try:
                while True:
                    data, addr = s.recvfrom(1024)
                    try:
                        info = json.loads(data.decode("utf-8", "replace"))
                    except Exception:
                        continue
                    if isinstance(info, dict) and info.get("app") == "agent-bell":
                        return info.get("ip") or addr[0]
            except Exception:
                continue
    finally:
        for s in socks:
            try:
                s.close()
            except Exception:
                pass
    return None


# ============================================================================
#  mDNS：自己发组播查询，不经系统 DNS（躲开 TUN 代理的 fake-ip 劫持）
# ============================================================================
def _mdns_query(name=MDNS_NAME, timeout=1.2):
    """发一个 A 记录查询到 224.0.0.251:5353，返回第一个应答的 IPv4 或 None。

    只用 stdlib 手搓报文：问题段是 <name>.local A IN，然后在应答里扫 A 记录。
    对每个本机接口都发一遍（多网卡时不指定出口会走错网卡）。
    """
    qname = b"".join(bytes([len(p)]) + p.encode("ascii")
                     for p in (name, "local")) + b"\x00"
    query = struct.pack("!HHHHHH", 0, 0, 1, 0, 0, 0) + qname + struct.pack("!HH", 1, 1)

    socks = []
    for src in _local_ips() + ["0.0.0.0"]:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
            if src != "0.0.0.0":
                s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                             socket.inet_aton(src))
            s.bind((src, 0))
            s.settimeout(timeout)
            s.sendto(query, (MDNS_ADDR, MDNS_PORT))
            socks.append(s)
        except Exception:
            try:
                s.close()
            except Exception:
                pass
    try:
        deadline = timeout
        for s in socks:
            s.settimeout(deadline)
            try:
                while True:
                    data, _ = s.recvfrom(2048)
                    ip = _parse_a_record(data, name)
                    if ip:
                        return ip
            except Exception:
                continue
    finally:
        for s in socks:
            try:
                s.close()
            except Exception:
                pass
    return None


def _parse_a_record(data, name):
    """从 mDNS 应答里扒出 <name>.local 的 A 记录地址（够用的最小实现）。"""
    try:
        want = (name + ".local").lower()
        qd, an = struct.unpack("!HH", data[4:8])
        pos = 12
        for _ in range(qd):                      # 跳过问题段
            pos = _skip_name(data, pos) + 4
        for _ in range(an):
            start = pos
            pos = _skip_name(data, pos)
            rtype, _cls, _ttl, rdlen = struct.unpack("!HHIH", data[pos:pos + 10])
            pos += 10
            if rtype == 1 and rdlen == 4:        # A 记录
                nm = _read_name(data, start).lower()
                if nm.rstrip(".") == want:
                    return socket.inet_ntoa(data[pos:pos + 4])
            pos += rdlen
    except Exception:
        pass
    return None


def _skip_name(data, pos):
    while pos < len(data):
        ln = data[pos]
        if ln == 0:
            return pos + 1
        if ln & 0xC0 == 0xC0:                    # 压缩指针，占 2 字节
            return pos + 2
        pos += 1 + ln
    return pos


def _read_name(data, pos, depth=0):
    parts = []
    while pos < len(data) and depth < 8:
        ln = data[pos]
        if ln == 0:
            break
        if ln & 0xC0 == 0xC0:
            ptr = struct.unpack("!H", data[pos:pos + 2])[0] & 0x3FFF
            parts.append(_read_name(data, ptr, depth + 1))
            break
        parts.append(data[pos + 1:pos + 1 + ln].decode("ascii", "replace"))
        pos += 1 + ln
    return ".".join(p for p in parts if p)


# ============================================================================
#  子网扫描
# ============================================================================
def _local_ips():
    """本机所有值得考虑的私网 IPv4（已排除 TUN/CGNAT/link-local/docker）。"""
    ips = set()
    try:
        infos = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)
        ips |= {i[4][0] for i in infos}
    except Exception:
        pass
    try:                                          # 多网卡时 gethostname 可能漏掉真实网卡
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("223.5.5.5", 80))               # 不真发包，仅取路由源 IP
        ips.add(s.getsockname()[0])
        s.close()
    except Exception:
        pass
    ips |= _ips_from_os()                          # 上面两条都可能漏，问一遍系统
    out = []
    for ip in ips:
        try:
            addr = ipaddress.IPv4Address(ip)
            if not addr.is_private or addr.is_loopback:
                continue
            if any(addr in n for n in _SKIP_NETS):
                continue
            out.append(ip)
        except Exception:
            continue
    return out


def _ips_from_os():
    """问系统要网卡地址。前两种办法在 TUN 代理环境下都会失灵：

    · getaddrinfo(gethostname()) 可能只返回虚拟网卡
    · 路由源探测（connect 到公网 IP）会返回 TUN 网卡地址（实测 198.18.0.1），
      随后被排除列表滤掉 —— 等于完全没有兜底，真实网段一旦漏掉，
      扫描目标为空会在 0.01 秒内报「未找到」，看起来像玄学故障。
    """
    out = set()
    try:
        if sys.platform == "win32":
            txt = subprocess.check_output(
                ["ipconfig"], text=True, errors="ignore", timeout=6,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        else:
            txt = subprocess.check_output(["ifconfig"], text=True,
                                          errors="ignore", timeout=6)
    except Exception:
        return out
    for m in re.finditer(r"([0-9]{1,3}(?:[.][0-9]{1,3}){3})", txt):
        ip = m.group(1)
        if not _is_netmask(ip):                   # 掩码/全零地址一起挡掉
            out.add(ip)
    return out


def arp_candidates(subnets=None):
    """从系统 ARP 表里拿本网段活着的主机 IP。

    为什么需要这条路：UDP 广播/组播发现依赖设备的**入站**回包，而 Windows 防火墙
    默认丢弃未经请求的入站 UDP —— 打包后的 exe 没被放行时，广播和 mDNS 都收不到
    应答（用 python.exe 跑能通，是因为它早前被放行过），于是只能退回全网段扫描。
    ARP 表是纯本地信息，探它列出的十几个地址是**出站** TCP，防火墙不拦，
    比逐个试 254 个地址快一个数量级。
    """
    try:
        if sys.platform == "win32":
            txt = subprocess.check_output(
                ["arp", "-a"], text=True, errors="ignore", timeout=6,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        else:
            txt = subprocess.check_output(["arp", "-an"], text=True,
                                          errors="ignore", timeout=6)
    except Exception:
        return []
    nets = subnets if subnets is not None else _local_subnets()
    out, seen = [], set()
    for m in re.finditer(r"([0-9]{1,3}(?:[.][0-9]{1,3}){3})", txt):
        ip = m.group(1)
        if ip in seen or _is_netmask(ip):
            continue
        try:
            addr = ipaddress.IPv4Address(ip)
        except Exception:
            continue
        if addr.is_multicast or addr.packed[-1] in (0, 255):
            continue
        if nets and not any(addr in n for n in nets):
            continue
        seen.add(ip)
        out.append(ip)
    return out


def _local_subnets():
    """本机所有值得扫的 /24 网段（去重，保持发现顺序）。"""
    nets, seen = [], set()
    for ip in _local_ips():
        net = ipaddress.IPv4Network(ip + "/24", strict=False)
        if net not in seen:
            seen.add(net)
            nets.append(net)
    return nets


def _scan_targets(targets, port, timeout, on_progress, stop_event, done0=0, total=None):
    """并发探一批地址，找到即停。返回 (ip, info) 或 (None, None)。"""
    if not targets:
        return None, None
    total = total or len(targets)
    found = threading.Event()
    result = [None, None]
    lock = threading.Lock()
    done = [done0]

    def probe_one(ip):
        if found.is_set() or (stop_event and stop_event.is_set()):
            return
        info = probe_info(ip, port, timeout)
        with lock:
            done[0] += 1
            if on_progress:
                on_progress(done[0], total)
        if info and not found.is_set():
            result[0], result[1] = ip, info
            found.set()

    with concurrent.futures.ThreadPoolExecutor(max_workers=SCAN_WORKERS) as ex:
        futs = [ex.submit(probe_one, ip) for ip in targets]
        for f in futs:
            if found.is_set() or (stop_event and stop_event.is_set()):
                break
            try:
                f.result()
            except Exception:
                pass
        if found.is_set() or (stop_event and stop_event.is_set()):
            for f in futs:
                f.cancel()
    return result[0], result[1], done[0]


def scan_subnets(port=80, timeout=SCAN_TIMEOUT, on_progress=None, stop_event=None,
                 prefer=None):
    """扫描本机各 /24。prefer 指定优先先扫的网段（通常是缓存 IP 所在的那个）。

    找到即停，返回 (ip, info) 或 (None, None)。
    """
    nets = _local_subnets()
    if prefer:
        try:
            pnet = ipaddress.IPv4Network(prefer + "/24", strict=False)
            nets = [pnet] + [n for n in nets if n != pnet]
        except Exception:
            pass
    if not nets:
        return None, None
    total = sum(254 for _ in nets)
    done = 0
    for net in nets:
        ip, info, done = _scan_targets([str(h) for h in net.hosts()], port, timeout,
                                       on_progress, stop_event, done, total)
        if ip:
            return ip, info
        if stop_event and stop_event.is_set():
            break
    return None, None


def find_device(cfg, log=None, on_progress=None, stop_event=None):
    """按「缓存 IP → mDNS 组播 → 缓存网段 → 全部网段」找设备。

    找到返回 host 并写回缓存，找不到返回 None。
    """
    def _log(msg):
        if log:
            log(msg)

    port = int(cfg.get("device_port") or 80)
    cached = (cfg.get("device_host") or "").strip()

    # 广播放在缓存之前：一个 UDP 包 0.2 秒就有结果，而缓存地址失效时要白等 3 秒
    # （换路由器/DHCP 重分配后必然失效）。设备没换地址时两者都是 0.2 秒级，不亏。
    ip = _udp_discover()
    if ip and probe_info(ip, port) is not None:
        if ip != cached:
            _log("发现：UDP 广播命中 %s" % ip)
        cfg.set("device_host", ip)
        return ip

    # 缓存地址给 2 秒预算就够：失败也不会放弃（后面还有 mDNS/ARP/扫描），
    # 但换过网段时能少白等 1 秒
    if cached and check_device(cached, port, timeout=2.0):
        _log("发现：缓存地址可用 %s" % cached)
        return cached

    ip = _mdns_query()
    if ip and probe_info(ip, port) is not None:
        _log("发现：mDNS 组播命中 %s" % ip)
        cfg.set("device_host", ip)
        return ip

    # ARP 候选：出站 TCP 探十几个「已知活着」的地址，比全网段扫快一个数量级，
    # 而且不依赖入站 UDP（打包后的 exe 常被防火墙挡掉广播/组播的回包）
    cands = arp_candidates()
    if cands:
        ip, info, _ = _scan_targets(cands, port, SCAN_TIMEOUT, None, stop_event)
        if ip:
            _log("发现：ARP 邻居命中 %s（试了 %d 个）" % (ip, len(cands)))
            cfg.set("device_host", ip)
            return ip

    _log("发现：开始扫描局域网（%d 个 ARP 邻居里没有，逐个试）……" % len(cands))
    ip, info = scan_subnets(port, on_progress=on_progress, stop_event=stop_event,
                            prefer=cached or None)
    if ip:
        _log("发现：扫描命中 %s（%s）" % (ip, (info or {}).get("mac", "?")))
        cfg.set("device_host", ip)
        return ip
    _log("发现：局域网内未找到设备（设备可能已关机，转一下旋钮唤醒它）")
    return None
