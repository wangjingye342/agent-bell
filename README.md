# AgentBell

独立的桌面小工具项目：电脑上 **Claude Code / Codex** 完成一轮对话时，局域网里的 ESP32-C3 设备
**蜂鸣 + 震动**，并在 OLED 上显示「哪台电脑 / 哪个智能体 / 哪个对话」完成了。
还带一个**带登录的网页控制台**，手机/电脑可随时开关蜂鸣/震动/勿扰。

> 本项目独立于气象站「仿真器」(aurora-weather-station)，自成一个文件夹与 git 仓库，互不影响。

```
   电脑（可多台）                          局域网                 AgentBell 设备
 ┌──────────────────┐   POST /notify   ┌──────────────────────────────────┐
 │ Claude  Stop hook │ ───────────────▶ │ ESP32-C3（HTTP 服务器 + mDNS）    │
 │ Codex   notify    │                  │  → 蜂鸣 + 震动 + OLED 显示        │
 │ tools/agent-bell/ │                  │  + 网页控制台 agent-bell.local    │
 └──────────────────┘                  └──────────────────────────────────┘
```

## 目录

| 路径 | 内容 |
|---|---|
| `agent_bell/` | **设备固件**（ESP32-C3 + SSD1306 OLED）+ Wokwi 预览。见 [agent_bell/README.md](agent_bell/README.md) |
| `agent_bell/WIRING.md` | **接线表**（先看这个） |
| `tools/agent-bell-bridge/` | **推荐：电脑侧桥接程序**（托盘常驻，免 hook 自动转发系统通知 + 自动发现设备 + 远程设置）。见 [tools/agent-bell-bridge/README.md](tools/agent-bell-bridge/README.md) |
| `tools/agent-bell/` | 电脑侧 hook 方案（Claude/Codex 适配器，备用）。见 [tools/agent-bell/README.md](tools/agent-bell/README.md) |
| `tools/ino-sim/` | 从气象站项目拷来的**屏幕模拟器**（备用，见下） |

## 快速开始

1. 接线：照 [agent_bell/WIRING.md](agent_bell/WIRING.md)（⚠ OLED / 触摸必须 3V3）。
2. 填 WiFi + 改网页登录密码：编辑 [agent_bell/agent_bell.ino](agent_bell/agent_bell.ino) 顶部配置区。
3. 编译烧录（在本项目根目录下）：
   ```sh
   arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app --output-dir agent_bell/build agent_bell
   arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32c3 --input-dir agent_bell/build agent_bell
   ```
4. 打开 `http://agent-bell.local/` 登录 → 点「发一条测试通知」自测。
5. 接电脑（二选一）：
   - **推荐**：装桥接程序（免 hook，Claude/Codex 发系统通知即自动转发），见 [tools/agent-bell-bridge/README.md](tools/agent-bell-bridge/README.md)。
   - 备用：hook 方案（Claude 的 Stop hook / Codex 的 notify），见 [tools/agent-bell/README.md](tools/agent-bell/README.md)。

## 关于 ino-sim 模拟器

`tools/ino-sim/` 是从气象站项目搬来的屏幕模拟器（把 Arduino 屏幕输出渲染成 PNG，让 AI 能"看到"UI 效果）。

**注意**：它目前只支持 Adafruit_GFX / ST7735（RGB565 彩屏），**还不能渲染本项目的 SSD1306 单色 OLED（U8g2 驱动）**。
所以本项目的界面预览暂时走 **Wokwi**（SSD1306 是 Wokwi 原生器件，见 [agent_bell/README.md](agent_bell/README.md)）或真机。
若以后想让 ino-sim 也能预览这块 OLED，需要给它加一个 SSD1306/U8g2 单色后端——可作为后续任务。

## 状态

- ✅ 已完成：WiFi 通知（蜂鸣 + 震动 + 中文屏显）、网页控制台（登录 + 开关蜂鸣/震动/勿扰 + 测试，设置存 flash）、
  电脑侧通知器 + 两个适配器（已本地端到端测过）、两版固件编译通过。
- ⏳ 待真机验证：接线上电、WiFi 连接、控制台、真跑一轮对话自动触发。
- 🔒 以后：长按录音 → 语音识别 → 回填输入框 → 双击确认发送（触摸手势、麦克风引脚 7/20/21 已预留）。
