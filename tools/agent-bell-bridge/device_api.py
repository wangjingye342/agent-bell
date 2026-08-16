#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
device_api.py — AgentBell 设备 HTTP 客户端（/api/info、/api/settings、/api/notes、/api/test）。

所有函数失败返回 None/False，不抛异常——设备随时可能掉线，调用方按返回值处理。

这一层还负责三件全局的事（实测得出，别绕过它自己 urlopen）：

1. **按设备串行化**。设备端的 Arduino WebServer 一次只服务一个 HTTP 请求
   （backlog 4，第 5 个连接直接被拒），并发请求会互相把对方顶穿超时：
   实测 1 并发失败 21%、4 并发失败 43%，同批请求最慢-最快差 p50 1.7 秒。
   而单台电脑本身就有三路请求（设备守护线程 / 通知转发线程 / 设置面板线程），
   所以这里用「每个 host 一把锁 + 最小间隔」把它们排成队。多台电脑之间管不了，
   但把自伤消掉之后，剩下的余量才够分给别的电脑。

2. **墙钟预算**。urllib 的 timeout 是「每个 socket 操作」而不是「整次请求」，
   connect 和 read 各拿一份，名义 2 秒实际能堵 4 秒以上。这里统一按 budget
   算截止时刻，每一步传剩余时间。

3. **共用一个 opener**（实测多线程共用安全：800 并发 0 异常），省掉每模块一份。
"""
import json
import threading
import time
import urllib.parse
import urllib.request

# 绕过系统代理直连局域网（TUN 全局代理会拦截到设备的连接）
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))

# /api/settings 的 fb 字段取值 → 中文名（设置界面下拉框用）
FEEDBACK_MODES = ["声音+震动", "仅震动", "仅声音", "无"]

# 同一台设备两次请求之间的最小间隔：设备串行处理，挨太紧只会自己排队
MIN_GAP_S = 0.25

_locks_guard = threading.Lock()
_locks = {}          # host → (Lock, [上次请求完成时刻])


def _host_slot(host):
    with _locks_guard:
        slot = _locks.get(host)
        if slot is None:
            slot = (threading.Lock(), [0.0])
            _locks[host] = slot
        return slot


class _Serialized:
    """with _Serialized(host): ... —— 同一设备的请求排队，并保证最小间隔。"""

    def __init__(self, host):
        self._lock, self._last = _host_slot(host)

    def __enter__(self):
        self._lock.acquire()
        gap = MIN_GAP_S - (time.monotonic() - self._last[0])
        if gap > 0:
            time.sleep(gap)
        return self

    def __exit__(self, *exc):
        self._last[0] = time.monotonic()
        self._lock.release()
        return False


def _url(host, port, path):
    return "http://%s:%d%s" % (host, int(port or 80), path)


def _request(host, port, path, budget, data=None, limit=8192):
    """带墙钟预算的一次请求：返回 body 文本，失败返回 None。

    budget 是整次请求的总时长上限（秒）。connect/read 各步传剩余时间，
    所以最坏耗时 ≈ budget，而不是 urllib 语义下的 2×budget。
    """
    deadline = time.monotonic() + budget
    with _Serialized(host):
        left = deadline - time.monotonic()
        if left <= 0.05:
            return None
        try:
            if data is None:
                req = urllib.request.Request(_url(host, port, path))
            else:
                req = urllib.request.Request(
                    _url(host, port, path), data=data, method="POST",
                    headers={"Content-Type":
                             "application/x-www-form-urlencoded; charset=utf-8"})
            with OPENER.open(req, timeout=max(0.3, left)) as r:
                if r.status != 200:
                    return None
                return r.read(limit).decode("utf-8", "replace")
        except Exception:
            return None


def _json(host, port, path, budget, data=None, limit=8192):
    body = _request(host, port, path, budget, data=data, limit=limit)
    if not body:
        return None
    try:
        return json.loads(body)
    except Exception:
        return None


# ============================================================================
#  只读接口
# ============================================================================
def get_info(host, port=80, timeout=3.0):
    """GET /api/info → dict 或 None。"""
    info = _json(host, port, "/api/info", timeout, limit=4096)
    return info if isinstance(info, dict) and info.get("app") == "agent-bell" else None


def get_settings(host, port=80, timeout=3.0):
    """GET /api/settings → dict 或 None。"""
    s = _json(host, port, "/api/settings", timeout)
    return s if isinstance(s, dict) else None


def get_notes(host, port=80, timeout=3.0):
    """GET /api/notes → 最近通知 list 或 None（设置面板「最近通知」区用）。"""
    a = _json(host, port, "/api/notes", timeout, limit=16384)
    return a if isinstance(a, list) else None


def get_state(host, port=80, timeout=3.5):
    """GET /api/state → {info, settings, notes} 合并结果，一次连接拿完。

    设备的吞吐上限很低（实测干净路径 2~3 req/s），能合并就别拆成三次。
    老固件没有这个端点时返回 None，调用方退回分别取。
    """
    st = _json(host, port, "/api/state", timeout, limit=32768)
    if not isinstance(st, dict) or not isinstance(st.get("info"), dict):
        return None
    return st


# ============================================================================
#  写接口 / 动作
# ============================================================================
def post_settings(host, port=80, timeout=4.0, **fields):
    """POST /api/settings，只传要改的字段。返回设备回的完整设置 dict 或 None。

    额外动作字段：play=1 试听当前提示音；vibtest=1 试震动。
    """
    data = urllib.parse.urlencode(
        {k: v for k, v in fields.items() if v is not None}).encode("utf-8")
    s = _json(host, port, "/api/settings", timeout, data=data)
    return s if isinstance(s, dict) else None


def send_test(host, port=80, timeout=4.0):
    """GET /api/test → 设备响一条测试通知。成功 True。"""
    return _request(host, port, "/api/test", timeout, limit=64) is not None


def send_notify(host, port=80, timeout=3.5, **fields):
    """POST /notify → 让设备响一条通知。成功 True。

    字段：computer / agent / conversation / message（都可选，固件有默认值）。
    走和其它请求同一把串行锁——否则转发会和探活撞车，两边一起超时。
    """
    data = urllib.parse.urlencode(
        {k: v for k, v in fields.items() if v not in (None, "")}).encode("utf-8")
    return _request(host, port, "/notify", timeout, data=data, limit=256) is not None
