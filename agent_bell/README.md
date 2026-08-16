# AgentBell 设备固件

ESP32-C3 + OLED + 蜂鸣器 + 震动 + 旋转编码器做的桌面小工具：
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
| `agent_bell.ino` | 设备固件（WiFi + HTTP 服务器 + U8g2 中文 OLED + 蜂鸣/震动/编码器菜单）|
| `WIRING.md` | **接线表**（先看这个）|
| `secrets.example.h` | WiFi 凭据模板（复制为 `secrets.h` 填入，已 gitignore）|
| `diagram.json` / `wokwi.toml` | Wokwi 界面预览（可选，用 SIM_DEMO 版固件）|
| `../tools/agent-bell-bridge/` | **推荐：电脑侧桥接程序**（托盘常驻，自动转发系统通知 + 远程设置）（[说明](../tools/agent-bell-bridge/README.md)）|
| `../tools/agent-bell/` | 电脑侧 hook 方案 + Claude/Codex 适配器（备用）（[说明](../tools/agent-bell/README.md)）|

## 硬件

见 [WIRING.md](WIRING.md)：OLED(GPIO5/6)、旋转编码器(GPIO1/7/20)、蜂鸣器(GPIO3)、震动(GPIO10)。
⚠ OLED 和编码器必须 3V3 供电（C3 不耐 5V）。

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

1. 复制 `secrets.example.h` 为 `secrets.h`，填入你的 WiFi（`secrets.h` 已 gitignore，不会上传）。
2. 按上面命令编译、烧录。
3. 上电后设备连 WiFi，待机屏出现（IP 可在旋钮菜单「设备状态」里看）。
4. 自测链路：转一下旋钮进菜单，选「模拟通知」→ 应蜂鸣 + 震动 + 屏显。
   从电脑发也行：浏览器访问 `http://agent-bell.local/api/test`，或命令行
   `python ../tools/agent-bell/notify.py --demo`。
5. 接上智能体（二选一）：
   - **推荐**：装[桥接程序](../tools/agent-bell-bridge/README.md)（托盘常驻，Claude/Codex 一发系统通知就自动转发，免 hook）。
   - 备用：hook 方案见[电脑侧说明](../tools/agent-bell/README.md)（Claude 的 Stop hook、Codex 的 notify）。
     本仓库已内置项目级 `.claude/settings.json`，**在本项目里结束一轮对话即会触发**（设备在线时）。

## 待机屏（左右半屏模块）

待机时屏幕分**左右两个 64×64 半屏**，各自从同一个模块池里任选，共 8 种：

**宇航员**（星空 + 自转太空人）· **数字时钟** · **模拟表盘** · **竖排大钟** · **日历** · **信息面板**（未读/WiFi/IP/开机时长）· **雷达扫描** · **流星夜空**

改法三选一（同一份设置，存 flash 掉电不丢）：

- 设备旋钮菜单「**左屏样式 / 右屏样式**」——选择器背景就是实时渲染的待机屏，转一格换一种，所见即所得；
- 桥接程序**设置窗口**的设备设置区；
- `POST /api/settings` 的 `lsty` / `rsty` 字段（0–7，顺序同上）。

时钟类模块的时间来自 SNTP（`ntp.aliyun.com`，UTC+8），联网后自动同步；未同步时显示 `--:--` 或占位表姿。

## 设置怎么调

分两类：

- **编译期**（改完要重新编译烧录）：`agent_bell.ino` 顶部配置区——引脚（OLED/蜂鸣器/震动/编码器）、
  `MDNS_NAME`、`OLED_ADDR`、震动极性 `VIB_ACTIVE_HIGH`、编码器每档沿数 `ENC_RAW_PER_DETENT`、
  提示音音序表 `MELODIES` / 震动节奏 `ALERT_VIB`。WiFi 凭据在 `secrets.h`（见上）。
- **运行期**（改完即生效，存 flash 掉电不丢，无需重烧）：
  - **设备旋钮菜单**（转=选，短按=进/确认，长按=返回），按使用频率分组排序：
    勿扰（短按即切，行内显示开/关）→ 通知组（提示音·停留自动试听、通知音量、通知震动）→
    反馈组（反馈方式、反馈音量、反馈震动——「反馈」指转/按旋钮时的提示，与通知独立）→
    待机屏（左/右屏样式）→ 旋钮（方向·短按即翻转、灵敏度）→ 省电组（自动熄屏、自动关机）
    → 立即熄屏、立即关机（深睡 µA 级，**转动**旋钮开机；按键不在 C3 唤醒脚
    范围内）、WiFi 状态（短按立刻重连）、重新配网 → 模拟通知、设备状态；
  - **桥接程序设置窗口**（设备在线时出现设备设置区）；
  - **HTTP API**：`POST /api/settings`（字段见下）。

## 电源与省电（电池供电必读）

v2 板的电池路径是 `VBAT → SS34 → SuperMini 板载 LDO → 3.3V`，余量只有零点几伏。
WiFi 发射峰值电流（19.5dBm 约 350mA）会把 3.3V 轨拽到欠压阈值以下 → 芯片复位 →
重启又连 WiFi → 又复位，表现为**屏幕定格 + 蜂鸣器连续响**（每次开机的初始化响一下）。

固件侧的应对：**只有一档「省电但够用」，没有模式可选**（板上既没有 USB 检测脚也没有
电池 ADC，固件无法自知供电来源，所以一律按电池的约束跑）：

| 项 | 取值 | 为什么 |
|---|---|---|
| 发射功率 | 按 RSSI 自适应 11 / 13 / 15 / 17dBm | 弱信号时不吝啬——TCP 重传比多发几个 dBm 更费电，也更容易撞穿电脑侧超时 |
| WiFi 休眠 | 调制解调器休眠（随 DTIM 醒来收包）| 省下的 ~60mA 连续电流是续航的大头；代价约 +0.1s 延迟，对提醒设备无感 |
| 主频 | 80MHz | 实测 HTTP 处理只占 p50 19ms，降频不影响响应 |
| 通知音量 | 封顶 70% | **防掉电，不是省电**：蜂鸣 ~200mA + 马达 ~100mA 会和 WiFi 收包峰值叠在刚收到通知那一刻 |

> 早期有「自动/性能/省电」三档，其中「自动」会在掉过一次电后**永久**锁死省电模式
> （NVS 粘性标记只写不清，插 USB 也回不去）。既然电池小、目标就是省电，现在直接
> 合并成一档，那个 bug 也随之消失。

配套措施：开机用缓存的 BSSID + 信道直连（跳过全信道扫描，缩短高电流窗口）；
掉电复位后先显示「电池电压不足」并等 2.5s（连续掉电 6s）让电芯回压再连网；
`esp_reset_reason()` 的掉电次数在菜单「设备状态」和 `/api/info` 的 `brownouts` 里可查
（正常供电恒为 0）。

> 根治要靠硬件：5V 节点加 220~470µF 缓冲电容、把 SS34 换成理想二极管省掉 0.4V 压降，
> 或按 `hardware/POWER_PATH.md` 的原方案加升压。

## HTTP 端点

所有端点均无需登录（仅限局域网用，HTTP 明文传输，别把设备暴露到公网）：

| 端点 | 作用 |
|---|---|
| `GET /` | **网页设置控制台**：全部设置读写 + 试听/试震 + 待看通知（手机/电脑输设备 IP 即用）|
| `GET /api/notes` | **待看**通知 JSON 数组（不是历史存档：设备上按一下旋钮即整批清空）|
| `GET\|POST /notify` | 收通知（`computer/agent/conversation/message`，表单编码；给 hook / 桥接程序用）|
| `GET /healthz` | 探活 |
| `GET /api/info` | 设备身份 JSON（`app:"agent-bell"`、mac、ip、rssi、uptime_s、brownouts 掉电次数、lowpower 是否已转省电；桥接程序扫描网段时靠它确认）|
| `GET /api/settings` | 读全部运行时设置（JSON，含 `melodies`/`panes` 名称表）|
| `GET /api/state` | `{info, settings, notes}` 一次拿完——设备一次只服务一个 HTTP 请求，多台电脑连时把 3 次请求合成 1 次是最实在的省 |
| `POST /api/settings` | 改设置，字段全部可选、只改传了的：开关 `buzz/vib/dnd/encrev/nwake`（0\|1）、强度 `bvol/vvol/fbvol/fbvib`（0–100）、`tone`（提示音 0–6）、`fb`（反馈方式 0–3）、`encdet`（灵敏度 1–3）、`lsty/rsty`（半屏样式 0–7）、`aoff`（自动熄屏分钟 0/1/2/5/10/20/30）、`apwr`（自动关机分钟 0/10/20/30/60/120/240）；另有动作参数 `play=1` 试听 / `vibtest=1` 试震。应答同 GET |
| `GET\|POST /api/test` | 注入一条固定测试通知，完整走响铃+震动+屏显，验证链路 |

## 操作（编码器）

**转** = 移动/调整，**短按** = 进入/确认，**长按 ≈0.5s** = 返回/回待机。详见 [WIRING.md](WIRING.md)。
（触摸模块已移除：其"返回"功能与编码器长按完全重合，且 PCB 放不下；GPIO4 空出留给以后的麦克风。）

## 界面预览（可选，不用真机）

两条路都行：

- **ino-sim（推荐，快）**：`tools/ino-sim` 已支持 U8g2 单色屏，一条命令把各个界面
  （待机各模块组合 / 通知 / 勿扰…）逐屏截成 PNG（场景脚本见
  [tools/ino-sim/scenarios/agent_bell.scn](../tools/ino-sim/scenarios/agent_bell.scn)，可注入 `lsty`/`rsty` 预览任意半屏组合）：
  ```sh
  python tools/ino-sim/simulate.py agent_bell/agent_bell.ino \
      --scenario tools/ino-sim/scenarios/agent_bell.scn --out tools/ino-sim/out_ab
  ```
- **Wokwi（交互式）**：用 `diagram.json` + `wokwi.toml`（VSCode 里 F1 → Wokwi: Start Simulator），
  指向 **SIM_DEMO 版**固件（开机自注入示例通知、跳过联网）：
  ```sh
  arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app \
      --build-property "compiler.cpp.extra_flags=-DSIM_DEMO" \
      --output-dir agent_bell/build-sim agent_bell
  ```

## 状态 / 下一步

- ✅ 已完成：WiFi 通知（蜂鸣 + 震动 + 中文屏显）、旋钮丝滑菜单（全部设置存 flash）、
  待机屏左右半屏模块（8 种可选 + 实时预览选择器）、网页设置控制台 + `/api/*` 桥接接口、
  电脑侧桥接程序 + hook 方案两条链路（已本地端到端测过）、ino-sim U8g2 界面预览。
- ⏳ 待真机验证：接线上电、WiFi 连接、`/api/test` 三件套、真跑一轮对话自动触发。
- 🔒 以后：长按录音 → 发电脑 → 微信输入法语音识别 → 回填输入框 → 双击确认发送（GPIO4 已空出留给麦克风）。
