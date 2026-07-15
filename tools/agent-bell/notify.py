#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
notify.py — 向 AgentBell 设备发一条「对话完成」通知（蜂鸣 + 震动 + 屏显）。

既可当命令行用，也可被 claude_stop_hook.py / codex_notify.py 导入调用 send()。

设计要点：只用 Python 标准库；快超时；**吞掉所有异常、永远成功退出**——
它挂在智能体的收尾钩子上，绝不能因为设备没开机/掉线而报错或拖慢智能体。

设备地址：优先 --host，其次环境变量 AGENT_BELL_HOST，默认 mDNS 名 agent-bell.local。
若 mDNS 解析不了，把设备开机时 OLED 上显示的 IP 设进环境变量即可：
    Windows:  setx AGENT_BELL_HOST 192.168.1.23
    bash:     export AGENT_BELL_HOST=192.168.1.23
"""
import argparse
import os
import socket
import sys
import urllib.parse
import urllib.request

try:                       # 中文 stderr 在 GBK 控制台上也不炸
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

DEFAULT_HOST = os.environ.get("AGENT_BELL_HOST", "agent-bell.local")
DEFAULT_PORT = int(os.environ.get("AGENT_BELL_PORT", "80"))

# 设备端各字段的缓冲大小（字节）；留一点余量，按 UTF-8 边界安全截断，避免真机截半个汉字。
LIMITS = {"computer": 22, "agent": 14, "conversation": 46, "message": 78}


def clip_bytes(s, maxbytes):
    """把字符串截到不超过 maxbytes 个 UTF-8 字节，且不切断多字节字符。"""
    s = "" if s is None else str(s)
    b = s.encode("utf-8")
    if len(b) <= maxbytes:
        return s
    b = b[:maxbytes]
    while b:                       # 回退到合法的 UTF-8 边界
        try:
            return b.decode("utf-8")
        except UnicodeDecodeError:
            b = b[:-1]
    return ""


def send(agent, conversation, message="", computer=None,
         host=None, port=None, level="", timeout=1.5):
    """发送通知。返回 True/False，但绝不抛异常。"""
    fields = {
        "computer": clip_bytes(computer or socket.gethostname(), LIMITS["computer"]),
        "agent": clip_bytes(agent, LIMITS["agent"]),
        "conversation": clip_bytes(conversation, LIMITS["conversation"]),
        "message": clip_bytes(message, LIMITS["message"]),
        "level": str(level or ""),
    }
    url = "http://%s:%d/notify" % (host or DEFAULT_HOST, port or DEFAULT_PORT)
    data = urllib.parse.urlencode(fields).encode("utf-8")   # 中文自动百分号编码
    req = urllib.request.Request(
        url, data=data, method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded; charset=utf-8"},
    )
    # 绕过系统代理直连局域网设备（有些机器开了 TUN 全局代理，会拦截/篡改到设备的连接）
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(req, timeout=timeout):
            return True
    except Exception as e:                                  # 设备离线/超时都不算错
        sys.stderr.write("[agent-bell] 通知发送失败（忽略）：%s\n" % e)
        return False


def main():
    p = argparse.ArgumentParser(description="向 AgentBell 发一条对话完成通知")
    p.add_argument("--agent", default="Agent", help="智能体名，如 Claude / Codex")
    p.add_argument("--conversation", default="-", help="对话标识，通常是项目目录名")
    p.add_argument("--message", default="", help="最后消息摘要（可选）")
    p.add_argument("--computer", default=None, help="电脑名（默认取主机名）")
    p.add_argument("--host", default=None, help="设备地址（默认 env AGENT_BELL_HOST 或 agent-bell.local）")
    p.add_argument("--port", type=int, default=None)
    p.add_argument("--level", default="", help="等级（可选，预留）")
    p.add_argument("--timeout", type=float, default=1.5)
    p.add_argument("--demo", action="store_true", help="发一条示例通知用于自测")
    a = p.parse_args()

    if a.demo:
        ok = send("Claude", "仿真器", "这是一条来自 notify.py 的测试通知",
                  host=a.host, port=a.port, timeout=a.timeout)
    else:
        ok = send(a.agent, a.conversation, a.message, a.computer,
                  a.host, a.port, a.level, a.timeout)
    sys.stderr.write("[agent-bell] %s\n" % ("已发送" if ok else "未送达（已忽略）"))
    sys.exit(0)                                             # 永远成功，别拖累调用方


if __name__ == "__main__":
    main()
