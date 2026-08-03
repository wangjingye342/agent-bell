#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""serve.py — AgentBell 网页控制台本地预览（纯 stdlib，零依赖）。

从 agent_bell/agent_bell.ino 里抽出 PROGMEM 的 CONSOLE_HTML，配一套内存 mock API，
让控制台页面不用烧录就能在电脑浏览器里完整交互（改设置、试听、发测试通知都有响应）。

用法（仓库根目录下）：
    python tools/console-preview/serve.py              # http://127.0.0.1:8756
    python tools/console-preview/serve.py --dump o.html  # 只导出 HTML 后退出
"""
import argparse
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
INO = os.path.join(HERE, "..", "..", "agent_bell", "agent_bell.ino")
PORT = 8756

# 与固件 settingsJson() 字段一一对应的 mock 状态
STATE = {
    "buzz": 1, "vib": 1, "dnd": 0, "nwake": 1, "encrev": 0,
    "bvol": 80, "vvol": 70, "fbvol": 40, "fbvib": 35,
    "tone": 0, "fb": 0, "encdet": 1, "lsty": 0, "rsty": 1,
}
MELODIES = ["叮咚·经典", "柔和三音", "上行叮铃", "双叮·短促", "琶音上行", "滑音", "单长音"]
PANES = ["宇航员", "数字时钟", "模拟表盘", "竖排大钟", "日历", "信息面板", "雷达扫描", "流星夜空"]
NOTES = [
    {"agent": "Claude", "computer": "书房台式", "conversation": "设置UI协同", "message": "两端设置界面已统一", "ago": "2分前"},
    {"agent": "Codex", "computer": "MacBook", "conversation": "后端重构", "message": "重构完成并通过测试", "ago": "31分前"},
    {"agent": "Claude", "computer": "书房台式", "conversation": "固件编译", "message": "编译通过", "ago": "1时前"},
]


def extract_html():
    with open(INO, encoding="utf-8") as f:
        src = f.read()
    m = re.search(r'R"HTML\((.*?)\)HTML"', src, re.S)
    if not m:
        sys.exit("找不到 R\"HTML(...)HTML\" 段（agent_bell.ino 里 CONSOLE_HTML 变了？）")
    return m.group(1)


def settings_json():
    d = dict(STATE)
    d["melodies"] = MELODIES
    d["panes"] = PANES
    return json.dumps(d, ensure_ascii=False)


class H(BaseHTTPRequestHandler):
    def _send(self, body, ctype="application/json; charset=utf-8"):
        raw = body.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if self.path == "/":
            self._send(extract_html(), "text/html; charset=utf-8")   # 每次现抽，改 .ino 刷新即见
        elif self.path == "/api/settings":
            self._send(settings_json())
        elif self.path == "/api/info":
            self._send(json.dumps({"app": "agent-bell", "api": 1, "name": "agent-bell",
                                   "mac": "AA:BB:CC:DD:EE:FF", "ip": "192.168.31.60",
                                   "rssi": -52, "uptime_s": 8130}))
        elif self.path == "/api/notes":
            self._send(json.dumps(NOTES, ensure_ascii=False))
        elif self.path == "/api/test":
            self._send("ok", "text/plain; charset=utf-8")
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path != "/api/settings":
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length") or 0)
        form = parse_qs(self.rfile.read(n).decode("utf-8"))
        for k, v in form.items():
            if k in STATE:
                STATE[k] = int(v[0])
        self._send(settings_json())

    def log_message(self, fmt, *args):                     # 安静点
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", metavar="FILE", help="只导出 HTML 到文件后退出")
    ap.add_argument("--port", type=int, default=PORT)
    args = ap.parse_args()
    if args.dump:
        html = extract_html()
        with open(args.dump, "w", encoding="utf-8") as f:
            f.write(html)
        print("%s  (%d bytes)" % (args.dump, len(html.encode("utf-8"))))
        return
    print("预览: http://127.0.0.1:%d  (Ctrl-C 停)" % args.port)
    ThreadingHTTPServer(("127.0.0.1", args.port), H).serve_forever()


if __name__ == "__main__":
    main()
