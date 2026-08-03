#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bridge_config.py — 桥接程序配置。Windows 存 %APPDATA%\\AgentBell\\config.json，
macOS 存 ~/Library/Application Support/AgentBell/config.json。

线程安全：所有读写都过锁；save() 原子写（先写临时文件再替换），断电不损坏。
"""
import json
import os
import sys
import threading

if sys.platform == "darwin":
    APP_DIR = os.path.expanduser("~/Library/Application Support/AgentBell")
else:
    APP_DIR = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"),
                           "AgentBell")
CONFIG_PATH = os.path.join(APP_DIR, "config.json")
LOG_PATH = os.path.join(APP_DIR, "bridge.log")

DEFAULTS = {
    "forward_enabled": True,              # 总开关：是否转发通知到设备
    "app_keywords": ["claude", "codex"],  # 应用显示名包含这些词（不分大小写）才转发
    "poll_interval_s": 2.0,               # 通知中心轮询间隔（秒）
    "cooldown_s": 3.0,                    # 两次转发的最小间隔（一批通知只响一次）
    "device_host": "",                    # 上次发现的设备 IP（发现缓存，加速下次启动）
    "device_port": 80,
    "health_interval_s": 20.0,            # 设备探活间隔（秒）
}


class Config:
    """dict 风格的配置对象：cfg.get(k) / cfg.set(k, v)（set 即存盘）。"""

    def __init__(self):
        self._lock = threading.Lock()
        self._data = dict(DEFAULTS)
        self._load()

    def _load(self):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                saved = json.load(f)
            if isinstance(saved, dict):
                for k in DEFAULTS:               # 只认识已知键，忽略旧版本残留
                    if k in saved:
                        self._data[k] = saved[k]
        except Exception:
            pass                                  # 首次运行/文件损坏 → 用默认值

    def save(self):
        with self._lock:
            data = dict(self._data)
        try:
            os.makedirs(APP_DIR, exist_ok=True)
            tmp = CONFIG_PATH + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            os.replace(tmp, CONFIG_PATH)
        except Exception:
            pass                                  # 存盘失败不致命（下次再试）

    def get(self, key):
        with self._lock:
            return self._data.get(key, DEFAULTS.get(key))

    def set(self, key, value):
        with self._lock:
            self._data[key] = value
        self.save()
