#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
device_api.py — AgentBell 设备 HTTP API 客户端（/api/info、/api/settings、/api/test）。

所有函数失败返回 None/False，不抛异常——设备随时可能掉线，调用方按返回值处理。
"""
import json
import urllib.parse
import urllib.request

_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # 绕过代理直连

# /api/settings 的 fb 字段取值 → 中文名（设置界面下拉框用）
FEEDBACK_MODES = ["声音+震动", "仅震动", "仅声音", "无"]


def _url(host, port, path):
    return "http://%s:%d%s" % (host, int(port or 80), path)


def get_info(host, port=80, timeout=2.0):
    """GET /api/info → dict 或 None。"""
    try:
        with _OPENER.open(_url(host, port, "/api/info"), timeout=timeout) as r:
            info = json.loads(r.read(4096).decode("utf-8", "replace"))
        return info if isinstance(info, dict) and info.get("app") == "agent-bell" else None
    except Exception:
        return None


def get_settings(host, port=80, timeout=2.5):
    """GET /api/settings → dict 或 None。"""
    try:
        with _OPENER.open(_url(host, port, "/api/settings"), timeout=timeout) as r:
            s = json.loads(r.read(8192).decode("utf-8", "replace"))
        return s if isinstance(s, dict) else None
    except Exception:
        return None


def post_settings(host, port=80, timeout=3.0, **fields):
    """POST /api/settings，只传要改的字段。返回设备回的完整设置 dict 或 None。

    额外动作字段：play=1 试听当前提示音；vibtest=1 试震动。
    """
    try:
        data = urllib.parse.urlencode(
            {k: v for k, v in fields.items() if v is not None}).encode("utf-8")
        req = urllib.request.Request(
            _url(host, port, "/api/settings"), data=data, method="POST",
            headers={"Content-Type": "application/x-www-form-urlencoded; charset=utf-8"})
        with _OPENER.open(req, timeout=timeout) as r:
            s = json.loads(r.read(8192).decode("utf-8", "replace"))
        return s if isinstance(s, dict) else None
    except Exception:
        return None


def send_test(host, port=80, timeout=3.0):
    """GET /api/test → 设备响一条测试通知。成功 True。"""
    try:
        with _OPENER.open(_url(host, port, "/api/test"), timeout=timeout) as r:
            return r.status == 200
    except Exception:
        return False


def get_notes(host, port=80, timeout=2.5):
    """GET /api/notes → 最近通知 list 或 None（设置面板「最近通知」区用）。"""
    try:
        with _OPENER.open(_url(host, port, "/api/notes"), timeout=timeout) as r:
            a = json.loads(r.read(16384).decode("utf-8", "replace"))
        return a if isinstance(a, list) else None
    except Exception:
        return None
