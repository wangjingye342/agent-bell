# 让另一台电脑也连上 AgentBell（多设备接入）

同一个 WiFi 下，**任意台电脑**上的 Claude Code / Codex 都能给这个门铃发通知。
本文件自包含：照着做即可，不需要拷贝本项目仓库。

## 设备信息

| 项 | 值 |
|---|---|
| 名称（mDNS） | `agent-bell.local` |
| 当前 IP | `192.168.31.187`（DHCP，可能变；建议在路由后台给它绑定固定 IP，见文末） |
| 通知接口 | `POST http://agent-bell.local/notify`（字段 `computer/agent/conversation/message`）|
| 控制台 | `http://agent-bell.local/`（浏览器打开，无需登录）|

前提：**新电脑和门铃在同一个 WiFi**（不能是访客网络/开了 AP 隔离），且装了 **Python 3**。

---

## 🤖 如果你是 AI 智能体，照这几步做

1. 确认能连上设备：命令行 `ping agent-bell.local`，或浏览器开 `http://agent-bell.local/`。连不上看文末排错。
2. 新建脚本 `agentbell_notify.py`（内容见下方「脚本」一节），放在一个固定路径，例如：
   - Windows：`C:\Users\<你>\agentbell_notify.py`
   - macOS/Linux：`~/agentbell_notify.py`
3. 若 `agent-bell.local` 解析不了，就设环境变量 `AGENT_BELL_HOST=192.168.31.187`（见下）。
4. 接 Claude Code：把 Stop hook 合并进用户级 `~/.claude/settings.json`（见「接 Claude」）。
5. 接 Codex：把 notify 合并进 `~/.codex/config.toml`（见「接 Codex」）。
6. 自测：`echo {"cwd":"/tmp/demo"} | python3 <脚本路径>` → 门铃应"叮咚"+震动+屏显。
7. 重启 Claude Code / Codex 让配置生效。

---

## 脚本 `agentbell_notify.py`

一个文件同时兼容 Claude（Stop hook，读 stdin）和 Codex（notify，读命令行参数）。
纯标准库、绕过代理直连局域网、超时快、**永远静默成功**（设备离线也不拖累智能体）。

```python
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""AgentBell 通知器（Claude Stop hook / Codex notify 两用）。"""
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
```

---

## 设备地址（可选）

默认用 mDNS 名 `agent-bell.local`，多数情况直接可用。若解析不了，设环境变量指向 IP：

```powershell
# Windows（永久，重开终端生效）
setx AGENT_BELL_HOST 192.168.31.187
```
```bash
# macOS / Linux（写进 ~/.zshrc 或 ~/.bashrc 持久化）
export AGENT_BELL_HOST=192.168.31.187
```

---

## 接 Claude Code

编辑用户级 `~/.claude/settings.json`（Windows：`C:\Users\<你>\.claude\settings.json`），
**合并**下面的 `Stop` hook（保留文件里已有的其它内容，别整体覆盖）：

```json
{
  "hooks": {
    "Stop": [
      { "hooks": [
        { "type": "command", "command": "python3 \"<脚本完整路径>\"" }
      ] }
    ]
  }
}
```

- Windows 把 `python3` 换成 `python`，路径用双反斜杠，如
  `"command": "python \"C:\\Users\\你\\agentbell_notify.py\""`。
- 只想本项目通知，就把它放进该项目的 `.claude/settings.json` 而不是用户级。

---

## 接 Codex

编辑 `~/.codex/config.toml`（Windows：`C:\Users\<你>\.codex\config.toml`），加一行：

```toml
notify = ["python3", "<脚本完整路径>"]
```

Windows 用 `["python", "C:\\Users\\你\\agentbell_notify.py"]`。Codex 每轮结束会把事件 JSON 作为最后一个参数传入，脚本只处理 `agent-turn-complete`。

---

## 自测

```bash
# 模拟一次 Claude 结束（应触发门铃"叮咚"+震动+屏显）
echo '{"cwd":"/tmp/demo-project"}' | python3 <脚本完整路径>
# 或直接浏览器打开，让设备自己发一条测试：
#   http://agent-bell.local/test
```

设备响了就成功。之后随便跑一轮真实对话，结束时就会自动通知。

---

## 连不上？排错

1. **不在同一 WiFi / 访客网络**：确认两台设备连的是同一个路由的同一个（非访客）SSID。
2. **AP 隔离**：有些路由（尤其访客网络）开了"AP 隔离/设备隔离"，会挡住设备间互访 —— 在路由后台关掉，或把两者放同一主网络。
3. **`agent-bell.local` 打不开**：改用 IP（`http://192.168.31.187/`）；并设 `AGENT_BELL_HOST` 为该 IP。
4. **IP 变了**：DHCP 可能重新分配。看门铃**控制台首页**或**待机屏顶栏**上的当前地址；一劳永逸的办法是下一节固定 IP。
5. **公司电脑有代理**：脚本已用 `ProxyHandler({})` 绕过代理直连，通常无需额外设置。

---

## 建议：给门铃固定 IP（防掉线）

在路由后台（如小米路由 `192.168.31.1` → 常用设置 → DHCP/IP 绑定 / 静态分配）里，
把门铃的 MAC 绑定成一个固定 IP（如 `192.168.31.187`）。这样重启/过期后地址不变，
`AGENT_BELL_HOST` 或书签就一直有效。设备 MAC 可在控制台首页或路由的已连接设备列表里看到。
