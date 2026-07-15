#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AgentBell 通知器（Claude Stop hook / Codex notify 两用）。

放到另一台电脑上，配进 Claude 的 Stop hook 或 Codex 的 notify 即可（见 CONNECT_ANOTHER_DEVICE.md）。
纯标准库、绕过代理直连局域网、超时快、永远静默成功（设备离线也不拖累智能体）。
"""
import json, os, socket, sys, urllib.parse, urllib.request

HOST = os.environ.get("AGENT_BELL_HOST", "agent-bell.local")
PORT = int(os.environ.get("AGENT_BELL_PORT", "80"))


def clip(s, n):                       # 按 UTF-8 字节安全截断，避免截半个汉字
    b = ("" if s is None else str(s)).encode("utf-8")[:n]
    while b:
        try: return b.decode("utf-8")
        except UnicodeDecodeError: b = b[:-1]
    return ""


def send(agent, conversation, message=""):
    body = urllib.parse.urlencode({
        "computer": clip(socket.gethostname(), 22),
        "agent": clip(agent, 14),
        "conversation": clip(conversation, 46),
        "message": clip(message, 78),
    }).encode("utf-8")
    req = urllib.request.Request("http://%s:%d/notify" % (HOST, PORT), data=body, method="POST")
    op = urllib.request.build_opener(urllib.request.ProxyHandler({}))   # 局域网直连，绕过系统代理
    try:
        op.open(req, timeout=1.5)
    except Exception as e:
        sys.stderr.write("[agent-bell] 发送失败（已忽略）：%s\n" % e)


def main():
    if len(sys.argv) > 1:                      # Codex：事件 JSON 作为最后一个参数
        try: d = json.loads(sys.argv[-1])
        except Exception: return
        if d.get("type") != "agent-turn-complete": return
        cwd = d.get("cwd")
        conv = os.path.basename(os.path.normpath(cwd)) if cwd else "Codex 会话"
        send("Codex", conv, d.get("last-assistant-message") or "")
        return
    try:                                       # Claude：Stop 事件 JSON 从 stdin 进
        d = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    except Exception:
        d = {}
    cwd = d.get("cwd") or os.getcwd()
    send("Claude", os.path.basename(os.path.normpath(cwd)) or "-", "")


if __name__ == "__main__":
    try: main()
    except Exception: pass
    sys.exit(0)                                # 永远成功退出
