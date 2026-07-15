#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
claude_stop_hook.py — Claude Code 的 Stop hook 适配器。

Claude Code 在「主 agent 完成一轮答复」时，会用它并通过 stdin 传入 JSON：
    {"session_id":..., "transcript_path":..., "cwd":..., "hook_event_name":"Stop", ...}
本脚本据此给 AgentBell 发一条通知：对话=项目目录名，摘要=最后一条助手消息。

接线（加进 ~/.claude/settings.json，全局所有项目都通知；或本仓库 .claude/settings.json 只本项目）：
    {"hooks":{"Stop":[{"hooks":[
      {"type":"command","command":"python \"<绝对路径>\\claude_stop_hook.py\""}
    ]}]}}

无论如何都 exit 0，绝不阻塞 Claude。
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notify  # noqa: E402


def last_assistant_text(path):
    """尽力从 transcript(.jsonl) 尾部取最后一条助手文本；任何失败都返回空串。"""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except Exception:
        return ""
    for line in reversed(lines[-100:]):
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except Exception:
            continue
        if obj.get("type") != "assistant":
            continue
        content = obj.get("message", {}).get("content")
        if isinstance(content, str):
            text = content
        elif isinstance(content, list):
            text = " ".join(
                c.get("text", "") for c in content
                if isinstance(c, dict) and c.get("type") == "text"
            ).strip()
        else:
            text = ""
        if text:
            return " ".join(text.split())      # 折叠换行/多空格
    return ""


def main():
    try:                                       # 显式按 UTF-8 读 stdin（别被中文 Windows 的 GBK 默认坑到）
        raw = sys.stdin.buffer.read()
        data = json.loads(raw.decode("utf-8"))
    except Exception:
        data = {}

    cwd = data.get("cwd") or os.getcwd()
    conversation = os.path.basename(os.path.normpath(cwd)) or "-"

    summary = ""
    tp = data.get("transcript_path")
    if tp:
        summary = last_assistant_text(tp)

    notify.send(agent="Claude", conversation=conversation, message=summary)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        sys.stderr.write("[agent-bell] claude hook 出错（已忽略）：%s\n" % e)
    sys.exit(0)
