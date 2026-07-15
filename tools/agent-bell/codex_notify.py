#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
codex_notify.py — OpenAI Codex CLI 的 notify 程序适配器。

Codex 在每轮结束时会调用它，并把事件 JSON 作为**最后一个命令行参数**传入：
    codex 执行： python codex_notify.py '{"type":"agent-turn-complete", ...}'
负载常见字段：type / turn-id / input-messages / last-assistant-message /（新版）cwd。

接线（加进 ~/.codex/config.toml）：
    notify = ["python", "<绝对路径>\\codex_notify.py"]

只处理 agent-turn-complete；其它事件忽略。无论如何 exit 0。
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notify  # noqa: E402


def main():
    if len(sys.argv) < 2:                    # 没拿到负载（手动运行等）
        return
    try:
        data = json.loads(sys.argv[-1])
    except Exception:
        return

    if data.get("type") != "agent-turn-complete":
        return                               # 只关心「一轮完成」

    cwd = data.get("cwd")                     # 新版 Codex 才有
    conversation = os.path.basename(os.path.normpath(cwd)) if cwd else "Codex 会话"
    message = data.get("last-assistant-message") or ""

    notify.send(agent="Codex", conversation=conversation, message=message)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        sys.stderr.write("[agent-bell] codex notify 出错（已忽略）：%s\n" % e)
    sys.exit(0)
