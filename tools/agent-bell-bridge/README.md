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

## 自动发现（断连自愈）

按快到慢三级查找，掉线自动重搜（转发失败也会立即触发重查）：

1. 上次成功的 IP（缓存在 `%APPDATA%\AgentBell\config.json`）
2. mDNS 名 `agent-bell.local`
3. 并发扫描本机所在网段（每个私网 /24 的 `GET /api/info`，验证 `app == "agent-bell"` 才认）

> 扫描会自动排除 198.18.0.0/15（Clash 等代理的 TUN 虚拟网段）。

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
Windows 完全一致（菜单栏图标 = 托盘）。

**构建**（.app 只能在 Mac 上打，不能在 Windows 交叉编译）：

```sh
cd tools/agent-bell-bridge && bash pack/build_mac.sh
# 产物：pack/Output/AgentBellBridge-mac.dmg
```

**安装与授权**（目标 Mac 上）：

1. 打开 DMG，把 `AgentBell Bridge.app` 拖进「应用程序」。
2. 首次启动用 **右键 → 打开**（应用未签名，直接双击会被 Gatekeeper 拦）。
3. **系统设置 > 隐私与安全性 > 完全磁盘访问权限** 里加入并勾选本 App，
   然后重启它——读通知中心数据库必须此权限（面板顶部「通知权限」按钮直达该页）。
4. 开机自启在设置面板里打开；确认 Claude / Codex 的系统通知没被关。

**源码直接跑**（免打包）：`python3 -m pip install pywebview pystray pillow`
后 `python3 bridge.py`（首次运行给终端/解释器授完全磁盘访问权限即可）。

已知差异：窗口关闭 = 收进菜单栏（若菜单栏图标创建失败则直接退出，日志有记录）；
无边框窗口没有最小化。**注意：macOS 版尚未在真机验证过**——通知库解析逻辑有
跨平台单测护着，但 pywebview/菜单栏/权限流程需要在 Mac 上实测，问题看
`~/Library/Application Support/AgentBell/bridge.log`。
