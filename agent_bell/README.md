# AgentBell 智能体门铃

ESP32-C3 + OLED + 蜂鸣器 + 震动 + 触摸做的桌面小工具：
**电脑上 Claude Code / Codex 完成一轮对话 → 它蜂鸣 + 震动 + OLED 显示「哪台电脑 / 哪个智能体 / 哪个对话」。**

> 这是独立于 `weather_station` 气象站的新子项目，两者互不影响。

```
   电脑（可多台）                          局域网                 AgentBell 设备
 ┌──────────────────┐   POST /notify   ┌──────────────────────────────────┐
 │ Claude  Stop hook │ ───────────────▶ │ ESP32-C3 (HTTP 服务器 + mDNS)     │
 │ Codex   notify    │  computer/agent/ │  → 蜂鸣 + 震动 + OLED 显示        │
 │ tools/agent-bell/ │  conversation/…  │  agent-bell.local                 │
 └──────────────────┘                  └──────────────────────────────────┘
```

## 文件

| 路径 | 作用 |
|---|---|
| `agent_bell.ino` | 设备固件（WiFi + HTTP 服务器 + U8g2 中文 OLED + 蜂鸣/震动/触摸）|
| `WIRING.md` | **接线表**（先看这个）|
| `diagram.json` / `wokwi.toml` | Wokwi 界面预览（可选，用 SIM_DEMO 版固件）|
| `../tools/agent-bell/` | 电脑侧通知器 + Claude/Codex 适配器（[说明](../tools/agent-bell/README.md)）|

## 硬件

见 [WIRING.md](WIRING.md)。本阶段只接 5 根信号线：OLED(GPIO5/6)、触摸(GPIO4)、蜂鸣器(GPIO3)、震动(GPIO10)。
⚠ OLED 和触摸必须 3V3 供电（C3 不耐 5V）。

## 构建 & 烧录

```sh
# 装库（一次）
arduino-cli lib install U8g2
# 编译（huge_app 分区容纳中文字库）
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app \
    --output-dir agent_bell/build agent_bell
# 烧录（换成你的串口，如 COM5）
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32c3 --input-dir agent_bell/build agent_bell
```
> 本机 arduino-cli 用绝对路径 `/c/Program\ Files/Arduino\ CLI/arduino-cli.exe`；PATH 坑见项目记忆。
> 已验证：正式版占 3MB 分区的 40%（1.27MB），内存宽松。

## 首次使用

1. 编辑 `agent_bell.ino` 顶部，填 `WIFI_SSID` / `WIFI_PASS`。
2. 按上面命令编译、烧录。
3. 上电后 OLED 会显示本机 **IP** 和 `agent-bell.local`。
4. 打开控制台自测：浏览器（手机/电脑，同一局域网）访问 `http://agent-bell.local/`，用 `WEB_USER`/`WEB_PASS` 登录，点「发一条测试通知」→ 应蜂鸣 + 震动 + 屏显。（命令行也行：`python ../tools/agent-bell/notify.py --demo`。）
5. 接上智能体：见 [电脑侧说明](../tools/agent-bell/README.md)（Claude 的 Stop hook、Codex 的 notify）。
   本仓库已内置项目级 `.claude/settings.json`，**在本项目里结束一轮对话即会触发**（设备在线时）。

## 设置怎么调

分两类：

- **编译期**（改完要重新编译烧录）：`agent_bell.ino` 顶部配置区——引脚、`WIFI_SSID`/`WIFI_PASS`、
  `MDNS_NAME`、`OLED_ADDR`、极性 `BUZZER_ACTIVE_HIGH`/`VIB_ACTIVE_HIGH`、节奏 `BUZZ_PATTERN`/`VIB_PATTERN`、
  网页登录 `WEB_USER`/`WEB_PASS`。
- **运行期**（网页点一下就改，存 flash 掉电不丢，无需重烧）：手机/电脑同一局域网打开
  `http://agent-bell.local/` 登录后，开关**蜂鸣器 / 震动 / 勿扰模式**，或点**测试**按钮。

## HTTP 端点

| 端点 | 登录 | 作用 |
|---|---|---|
| `GET /` | ✅ | **控制台**：蜂鸣器/震动/勿扰开关、测试按钮、状态、历史 |
| `GET /set?buzzer=on\|off&vib=on\|off&dnd=on\|off` | ✅ | 改设置（存 flash）|
| `GET /beep` · `GET /vibtest` | ✅ | 立刻试响 / 试震 |
| `GET /test` | ✅ | 发一条测试通知 |
| `POST /notify` | ❌ | 收通知（`computer/agent/conversation/message`，表单编码；给 hook 用，**开放**）|
| `GET /healthz` | ❌ | 探活（**开放**）|

> 登录 = HTTP Basic Auth，用户名/密码在固件顶部 `WEB_USER`/`WEB_PASS`（默认 `admin`/`agentbell`，**请改掉**）。
> 仅限局域网用（HTTP 明文传输），别把设备暴露到公网。

## 触摸

本阶段：**短按 = 静音当前报警 + 翻看更旧一条 / 翻完回待机**。
长按、双击已在代码里预留，留给以后的「录音」功能。

## Wokwi 预览（可选）

设备界面**不能**用现有 ino-sim 预览（那个只支持 GFX/ST7735）。要看 OLED 效果，
用 `diagram.json` + `wokwi.toml`（VSCode 里 F1 → Wokwi: Start Simulator）。
它指向 **SIM_DEMO 版**固件（开机自注入示例通知、跳过联网）：
```sh
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app \
    --build-property "compiler.cpp.extra_flags=-DSIM_DEMO" \
    --output-dir agent_bell/build-sim agent_bell
```

## 状态 / 下一步

- ✅ 已完成：WiFi 通知（蜂鸣 + 震动 + 中文屏显）、**网页控制台（登录 + 开关蜂鸣/震动/勿扰 + 测试按钮，设置存 flash）**、电脑侧通知器 + 两个适配器（已本地端到端测过）、两版固件编译通过。
- ⏳ 待真机验证：接线上电、WiFi 连接、`/test` 三件套、真跑一轮对话自动触发。
- 🔒 以后：长按录音 → 发电脑 → 微信输入法语音识别 → 回填输入框 → 双击确认发送。
