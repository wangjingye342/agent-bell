# AgentBell Bridge — 通知自动转发（免 hook）

Windows 托盘常驻小程序：**Claude / Codex 等桌面应用一发系统通知，就自动转发给局域网里的
AgentBell 设备**（蜂鸣 + 震动 + OLED 屏显）。不用再给每个智能体装 Stop hook / notify 脚本。

```
 Claude 桌面版 ─┐ 系统通知                       ┌────────────────────┐
 Codex        ─┤ ────────▶ Windows 通知中心 ───▶ │ AgentBell Bridge    │ ──POST /notify──▶ 🔔 AgentBell
 其它应用      ─┘            (UserNotification    │ 托盘常驻 · 自动发现   │
                             Listener 轮询)      └────────────────────┘
```

原来的 hook 方案（`tools/agent-bell/`）依然可用，两者互不冲突；
Bridge 的优势是**装一次全局生效**，且能看到所有会发系统通知的应用。

---

## 安装（3 步）

1. 双击 [install.bat](install.bat)（装依赖 + 注册开机自启 + 立即启动）。
2. 允许通知访问：`设置 > 隐私和安全性 > 通知 > 让应用访问通知` 打开总开关
   （设置窗口里有「打开通知权限设置」直达按钮；改完权限重启一次 Bridge）。
3. 确认 Claude / Codex 桌面版自己的通知没被关（它们得先发通知，Bridge 才有的转）。

托盘出现 🔔 图标即在运行：**绿点 = 设备在线，红点 = 离线**。
左键双击图标打开设置窗口；卸载运行 [uninstall.bat](uninstall.bat)。
（macOS 上图标在顶部菜单栏、不在 Dock 里，见下面的 macOS 版一节。）

## 自动发现（断连自愈）

按快到慢四级查找：

1. **UDP 广播**问 47317（固件里有应答器）——实测 **0.21 秒**，IP 变了也能找到
2. 缓存的上次 IP（老固件没有广播应答器时靠它）
3. **自己发 mDNS 组播**（UDP 5353，不走系统 DNS）
4. 子网扫描（**最后手段**）：先扫缓存 IP 所在的 /24，再扩到其它网卡

> **为什么把子网扫描压到最后**：实测它不只是慢——254 路并发 TCP/ARP 突发会把设备
> 自己的响应延迟推过客户端超时，漏检率空闲时 5%、风暴中 20%、**风暴停止 2 秒后仍有 33%**。
> 也就是说扫描器会把它要找的设备打趴，然后报告「没找到」。日志里那个「扫描未找到」
> 紧接着「缓存地址可用」的自相矛盾顺序，正是这个自伤的指纹。

> **为什么不用 `gethostbyname("agent-bell.local")`**：装了 Clash 一类 TUN 代理时，`.local`
> 会被 fake-ip 劫持到 198.18.0.0/15 再被 RST（实测 0.02 秒返回失败），而且**任何**域名都能
> "解析成功"，连"没有 mDNS"都判断不了。设备自己的 mDNS 应答是好的，所以绕过系统解析直接发组播。

> 扫描排除：198.18/15（TUN fake-ip）、100.64/10（CGNAT/Tailscale）、169.254/16（link-local）、
> 172.17/16（Docker）、240.0.0.0/4（顺带挡住被正则捞成地址的子网掩码）。
> 网卡枚举有三重来源（`gethostname` / 路由源探测 / `ipconfig`）——前两者在 TUN 环境下都会
> 失灵（路由源探测实测返回 `198.18.0.1`），漏光了会导致「0.01 秒就报未找到」这种假故障。

**实测自愈耗时**：缓存 IP 失效（换路由器 / DHCP 重分配）→ **0.28 秒**（改之前 ~90 秒、扫三轮）。

## 多台电脑连同一台设备

设备端的 Arduino WebServer **一次只服务一个 HTTP 请求**（listen backlog 只有 4），
并发请求会互相把对方顶穿超时。所以客户端这边做了三件事：

- **按设备串行化**（`device_api.py`）：同一台设备的请求排队 + 最小间隔 250ms。
  注意单台电脑本身就有三路请求（设备守护线程 / 通知转发线程 / 面板线程），
  不排队的话自己先跟自己撞。
- **抖动错开**：探活间隔 30 秒 ±20% 随机，多台电脑不会卡在同一刻。
- **合并端点**：面板改用固件的 `/api/state`（info+settings+notes 一次拿完），
  且只在窗口可见时轮询（藏在托盘里就完全不打设备）。

实测 3 台电脑同时连一台设备：改之前失败率 30.8%，改之后 **0%**（请求量 0.72→0.26 req/s）。

## 陌生网络一键配网（provision.py）

带设备去新环境（新 WiFi，手边没有烧录条件）时用。设备连不上旧 WiFi 会在开机约 20 秒后
自动开热点 `AgentBell-XXXX`（也可在设备菜单选「重新配网」立即开）。然后在电脑上跑：

```bash
python provision.py
```

它会自动完成：读出本机 WiFi 名和密码（`netsh`，没连 WiFi 就从已保存网络里选）→
扫到设备热点 → 临时切过去把凭据 POST 给设备 → 电脑切回原 WiFi → 等设备上线并写入
Bridge 的 IP 缓存。之后 Bridge 一启动就能用。

- 手动指定要配的网：`python provision.py --ssid 名称 --pass 密码`
- 没有电脑也行：手机连 `AgentBell-XXXX`（密码 `agentbell`），弹出的页面里填 WiFi 即可
  （没弹就浏览器开 `192.168.4.1`）。
- 设备只支持 **2.4GHz**；密码配错设备会退回配网热点，重跑一次即可。
- 凭据存设备 NVS，优先于固件里编译进去的 `secrets.h`；回到老家网络无需操作（NVS 里存的
  就是老家 WiFi 时）或重配一次。

## 设置窗口

无边框「TE 铝面板」风格固定尺寸窗口（pywebview + WebView2 渲染 `ui/index.html`，
与设备自带网页控制台同一套设计语言，见仓库根 `DESIGN.md`）。标题栏可拖动窗口，
× 键收进托盘（后台继续转发），– 键最小化。

- **显示屏**（左上深色块）：设备在线/离线/扫描进度、通知监听权限状态、信号与运行时长。
- **链路按键**：发测试通知、重新搜索、通知权限直达；手动地址输入 + 连接。
- **转发规则**：总开关；应用关键词（默认 `claude, codex`，按应用显示名匹配，不分大小写）；
  轮询间隔；响铃冷却（一批通知只响一次）。
- **最近通知**：设备上最近响过的通知列表（与设备网页控制台同源）。
- **设备设置**（右栏，设备在线时可用）：蜂鸣/震动/勿扰开关、音量与震动强度推子、
  提示音步进器+试听/试震、操作反馈模式、旋钮方向与灵敏度、待机屏左右模块、通知亮屏。
  改动即时写入设备 flash，与设备上旋钮改的是同一份设置。
- **本程序**：开机自启开关、查看日志、退出。

## 已知限制

- Windows 通知监听对非打包 Python 进程**只能轮询**（默认 2 秒），不能事件推送——
  这是系统限制（`NotificationChanged` 事件需要应用包身份）。
- **应用必须真的发系统通知**才转发得了。Claude 桌面版在窗口失焦时发通知；
  如果你盯着窗口看，它一般不发（这种时候你人就在电脑前，也不需要它响）。
- 若应用通知开了「横幅但不进通知中心」以外的奇怪组合，以通知中心里能看到为准。

## 文件

| 文件 | 作用 |
|---|---|
| `bridge.py` | 主程序：托盘 + pywebview 设置面板 + 线程编排 |
| `ui/index.html` | 设置面板前端（TE 铝面板风格；浏览器直接打开走 mock 数据可预览）|
| `watcher.py` | 通知中心轮询 + 过滤 + 转发（Windows，WinRT）|
| `watcher_mac.py` | 同上的 macOS 实现（轮询通知中心 SQLite 库，接口一致）|
| `discovery.py` | 设备发现（缓存 IP → mDNS → 网段扫描）|
| `device_api.py` | 设备 API 客户端（/api/info /api/settings /api/notes /api/test）|
| `bridge_config.py` | 配置读写（`%APPDATA%\AgentBell\config.json`）|
| `provision.py` | 陌生网络一键配网（读本机 WiFi → 连设备热点推凭据 → 等上线写缓存）|
| `probe_listener.py` | 开发用：验证通知监听可用性 |

日志：`%APPDATA%\AgentBell\bridge.log`。

## 依赖

Python 3.9–3.14（本机 3.12 实测）+ `pystray` `pillow` `pywebview` + `winrt-*` 系列（见 install.bat）；
系统需有 WebView2 运行时（Win11 自带，Win10 一般随 Edge 已装）。
固件需带 `/api/*` 端点的版本（本仓库 `agent_bell/` 当前源码即是）；
老固件只有 `/notify`，转发仍可用，但设备设置区不可用、发现只能靠 mDNS/缓存。

## 打包成安装程序（给别的电脑用，目标机免装 Python）

```sh
# 1) 生成图标 + PyInstaller 打包（onedir，杀软误报少）
python pack/make_icon.py
python -m PyInstaller pack/AgentBellBridge.spec --noconfirm --distpath pack/dist --workpath pack/build
# 2) Inno Setup 出安装器（需装 Inno Setup 6 + 中文语言包）
"%LOCALAPPDATA%/Programs/Inno Setup 6/ISCC.exe" pack/AgentBellBridge.iss
# 产物：pack/Output/AgentBellBridge-Setup.exe（~15MB，单用户安装，无需管理员）
```

安装器特性：可选开机自启（与程序内开关同一注册表键）、卸载先杀进程并清自启项、
配置和日志留在 `%APPDATA%\AgentBell`（重装保留缓存的设备 IP）。

装到新电脑后要做的唯一一件事：Windows 设置 > 隐私和安全性 > 通知 >
「让应用访问通知」里允许 AgentBellBridge（打包后身份不再是 Python，需重新授权一次），
然后重启程序。设置窗口顶部有直达按钮。

打包版注意（spec 里已处理，改代码时别破坏）：winrt 模块化包、pystray._win32、
webview.platforms.winforms/edgechromium 都是运行时动态导入，必须保留在 hiddenimports
列表里；`ui/` 目录要在 datas 里带上（运行时经 `sys._MEIPASS` 定位）。

## macOS 版

同一份代码直接支持 macOS（12+）：通知监听换成轮询「通知中心」数据库
（`watcher_mac.py`），自启用 `~/Library/LaunchAgents`，界面/发现/转发与
Windows 完全一致。

**只驻留菜单栏，不进 Dock**（accessory 应用）：点顶部菜单栏的铃铛图标弹出菜单，
第一项「打开设置」就是面板；面板的 × 收回菜单栏，后台继续转发；退出走菜单里的
「退出」或面板的「退出程序」。

> 实现上必须两处配合：`.app` 的 `Info.plist` 带 `LSUIElement=true`（启动那一刻就没有
> Dock 图标），**加上**运行时把 NSApp 活动策略设回 Accessory——因为 pywebview 的 cocoa
> 后端在导入时会把策略改成 Regular，Dock 图标会冒出来。见 `bridge.py` 的 `_mac_accessory()`；
> CI 里有一步断言构建产物的 `LSUIElement` 为 true，别把它删了。
> 菜单栏图标万一创建失败，程序会自动恢复 Dock 图标并亮出窗口兜底（否则用户没有任何入口）。

**构建**（.app 只能在 Mac 上打，不能在 Windows 交叉编译）：

```sh
cd tools/agent-bell-bridge && bash pack/build_mac.sh
# 产物：pack/Output/AgentBellBridge-mac-<架构>.dmg（applesilicon / intel）
```

**安装与授权**（目标 Mac 上）：

1. 打开 DMG，把 `AgentBell Bridge.app` 拖进「应用程序」。
2. 首次启动用 **右键 → 打开**（应用未签名，直接双击会被 Gatekeeper 拦）。
3. **系统设置 > 隐私与安全性 > 完全磁盘访问权限** 里加入并勾选本 App，
   然后重启它——读通知中心数据库必须此权限（面板顶部「通知权限」按钮直达该页）。
4. 开机自启在设置面板里打开；确认 Claude / Codex 的系统通知没被关。

**源码直接跑**（免打包）：`python3 -m pip install pywebview pystray pillow`
后 `python3 bridge.py`（首次运行给终端/解释器授完全磁盘访问权限即可）。

已知差异：窗口关闭 = 收进菜单栏；无边框窗口没有最小化（mac 上 `–` 键隐藏，
按菜单栏图标唤回）。**注意：macOS 版尚未在真机验证过**——通知库解析逻辑有
跨平台单测护着、`.app` 的菜单栏驻留配置由 CI 断言，但 pywebview/权限流程需要在
Mac 上实测，问题看 `~/Library/Application Support/AgentBell/bridge.log`。
