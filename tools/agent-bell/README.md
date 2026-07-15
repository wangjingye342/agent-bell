# AgentBell 电脑侧通知器

当 **Claude Code / Codex** 完成一轮对话时，给局域网里的 AgentBell 设备发一条 HTTP 通知，
让它蜂鸣 + 震动 + 在 OLED 上显示「哪台电脑 / 哪个智能体 / 哪个对话」完成了。

- **纯 Python 标准库**，无需 `pip install` 任何东西。
- 三个脚本：
  - `notify.py` — 核心发送器（也可命令行直接用）。
  - `claude_stop_hook.py` — 挂在 Claude Code 的 `Stop` hook 上。
  - `codex_notify.py` — 挂在 Codex 的 `notify` 上。
- **永不阻塞智能体**：设备没开机 / 掉线时静默忽略，脚本总是成功退出。

---

## 1. 设备地址

脚本按这个顺序找设备：`--host` 参数 → 环境变量 `AGENT_BELL_HOST` → 默认 `agent-bell.local`（mDNS）。

大多数情况下 mDNS 直接可用，无需配置。若解析不了（部分 Windows 网络环境），
把设备**开机时 OLED 上显示的 IP** 设进环境变量：

```powershell
# Windows（永久，重开终端生效）
setx AGENT_BELL_HOST 192.168.1.23
```
```bash
# bash / macOS / Linux（当前会话）
export AGENT_BELL_HOST=192.168.1.23
```

---

## 2. 先自测（确认设备能响）

设备烧好、连上 WiFi 后，任选其一：

```bash
python tools/agent-bell/notify.py --demo
# 或浏览器打开控制台 http://agent-bell.local/ ，登录后点「发一条测试通知」
```

设备应当立刻蜂鸣 + 震动，并显示一条测试通知。控制台 `http://agent-bell.local/`（需登录，
用户名/密码在固件 `WEB_USER`/`WEB_PASS`）还能开关蜂鸣器/震动/勿扰、看 IP 与最近通知列表。

---

## 3. 接 Claude Code

把下面的 `Stop` hook 加进配置。二选一：

- **全局**（推荐，所有项目结束对话都通知）：`~/.claude/settings.json`
  （Windows 是 `C:\Users\你的用户名\.claude\settings.json`）
- **仅本项目**：仓库里的 `.claude/settings.json`（本仓库已内置一份作示例）

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          { "type": "command",
            "command": "python \"D:\\开发板玩法\\agent-bell\\tools\\agent-bell\\claude_stop_hook.py\"" }
        ]
      }
    ]
  }
}
```

> 路径按你的实际位置改；JSON 里反斜杠要写成 `\\`。
> 若 `python` 不在 PATH 上，把命令里的 `python` 换成 Python 绝对路径（如
> `C:\Users\你\AppData\Local\Programs\Python\Python312\python.exe`）。

---

## 4. 接 Codex

编辑 `~/.codex/config.toml`（Windows：`C:\Users\你的用户名\.codex\config.toml`），加一行：

```toml
notify = ["python", "D:\\开发板玩法\\agent-bell\\tools\\agent-bell\\codex_notify.py"]
```

Codex 每轮结束会把事件 JSON 作为最后一个参数传给它；脚本只处理 `agent-turn-complete`。

---

## 5. 手动 / 脚本里发通知

```bash
python tools/agent-bell/notify.py \
    --agent Claude --conversation agent-bell --message "编译通过" \
    --computer 书房台式 --host 192.168.1.23
```

字段（都可选，`--agent`/`--conversation` 最有用）：`--agent`（智能体名）、
`--conversation`（对话/项目名）、`--message`（摘要）、`--computer`（默认取本机主机名）、
`--host`/`--port`、`--level`（预留）、`--timeout`（默认 1.5s）。

中文会自动 URL 编码传输，设备端解回 UTF-8 显示；过长字段按 UTF-8 边界安全截断。
