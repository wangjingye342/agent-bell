# 嘉立创 EDA 自动绘制与生产文件方案

更新日期：2026-07-18

## 结论

本机具备直接生成嘉立创 EDA 专业版工程和生产文件的可行工具链。推荐使用嘉立创/EasyEDA 的
官方 API skill 与 API Gateway，而不是仅靠鼠标自动化绘制。

已确认环境：

- 嘉立创 EDA 专业版 `V3.2.166` 已安装并正在运行；
- 本机安装包含 `pro-api 0.3.4` 接口定义；
- 已安装 Codex skill：`C:\Users\wjy\.codex\skills\easyeda-api`；
- skill 提供原理图、PCB、封装、工程管理、DRC 和制造数据导出 API；
- 当前尚未检测到 EasyEDA Bridge 服务，EDA API Gateway 扩展仍需安装/启用。

## 推荐工作流

```text
确定电源与机械要求
    ↓
API 查询嘉立创器件库、锁定 LCSC 料号和真实封装
    ↓
创建嘉立创 EDA 原理图
    ↓
ERC / 电源与驱动复核
    ↓
创建 PCB、板框、SuperMini 自定义封装和天线 keepout
    ↓
器件布局、布线、铺铜
    ↓
DRC + 3D/机械检查 + BOM/库存检查
    ↓
导出生产包
```

## 可以生成的交付物

最终 `hardware/production/` 计划包含：

- 嘉立创 EDA 专业版工程备份文件（`.epro`/`.epro2`）；
- 原理图 PDF；
- Gerber ZIP；
- 钻孔文件（包含在 Gerber 包内并单独复核）；
- BOM（CSV/XLSX，含 LCSC 料号）；
- Pick and Place / CPL 坐标文件；
- PCB 3D STEP 文件；
- 装配图和关键极性说明；
- DRC/ERC 检查记录；
- `ORDER_README.md`，列明嘉立创下单选项、DNP 器件和后焊 SuperMini 方法。

Gerber 可用于多数 PCB 厂打裸板；Gerber + BOM + CPL 用于嘉立创 PCBA。嘉立创 EDA 原生工程便于
在下单前继续人工检查和修改。

## 仍然不能跳过的检查

“能自动生成”不等于“未经检查就安全下单”。最终下单前至少要确认：

1. 电池容量、是否自带保护板、连接器型号和极性；
2. 外壳/板框尺寸、安装孔和编码器位置；
3. SuperMini 实物与 1:1 自定义封装重合；
4. OLED 实际线序、连接器和线长；
5. 蜂鸣器声孔、马达治具和编码器高度；
6. 嘉立创下单页面上的库存、贴装方向、扩展料和治具费用；
7. ERC/DRC 无未解释错误；
8. Gerber、BOM、CPL 在下单预览器中再次核对。

## 锂电版本的电源原则

用户已经确认需要 USB 优先、边充边用和自动电池切换。正式架构见
[POWER_PATH.md](POWER_PATH.md)，主充电/电源路径芯片暂定 `BQ24074RGTR / C54313`。

正式方案会加入：

- 单节锂电池连接器；
- 充电管理；
- 电池保护（若电池本身不带保护）；
- 电源开关；
- 明确的 USB/电池电源路径；
- 3.3V 稳压和负载瞬态储能；
- 电池电压检测（建议）；
- 防反接/过流保护和测试点。

不再使用旧草案的 `TP4056 + LDO`。BQ24074 负责充电和 power-path；电源开关控制后级系统电源，
关机时仍能充电。SuperMini 的烧录 USB 与主板日常充电 USB 分开，并增加防倒灌和丝印警告。

## 工具接入前置步骤

1. 在嘉立创 EDA 专业版安装并启用 `run-api-gateway.eext`；
2. 启动本机 EasyEDA Bridge 服务；
3. 验证 Bridge 健康状态和唯一 EDA 窗口连接；
4. 先执行只读 API：读取当前工程、文档和器件库；
5. 新建独立工程 `AgentBell-PCB`，不覆盖当前空白/测试工程；
6. 分阶段保存快照，原理图确认后再开始 PCB。

EDA 扩展安装完成后，可以从 API 自动建工程开始推进。
