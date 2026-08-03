# AgentBell 单板 PCB — 前期设计资料

把 AgentBell 从"面包板+杜邦线"集成到**一块嘉立创可打样+贴片**的板子。

> **当前状态：可行性/需求确认阶段，还不是可直接生产的设计源。**
> 先阅读 [FEASIBILITY_REVIEW.md](FEASIBILITY_REVIEW.md)，并填写
> [MEASUREMENTS_NEEDED.md](MEASUREMENTS_NEEDED.md)。`SCHEMATIC.md` 和 `BOM.csv` 是前期草案，
> 在实物尺寸、电源方案和最终料号确认前不能直接用于下单。

> 这份是人可读的前期方案，不是可直接上传 JLC 的 Gerber/CPL。实际原理图、布线、ERC/DRC、
> Gerber、BOM 和 CPL 仍需在 EDA 工具中完成和复核。

新增资料：

- [SUPERMINI_CARRIER_SPEC.md](SUPERMINI_CARRIER_SPEC.md)：SuperMini 尺寸、引脚、供电和手焊封装；
- [PCBA_STRATEGY.md](PCBA_STRATEGY.md)：除 SuperMini 外全部由嘉立创贴装的生产策略。
- [EDA_AUTOMATION_PLAN.md](EDA_AUTOMATION_PLAN.md)：嘉立创 EDA API 自动绘制、检查和生产文件导出方案。
- [POWER_PATH.md](POWER_PATH.md)：USB 优先、边充边用、自动电池切换和关机充电架构。
- [COMPACT_DESIGN.md](COMPACT_DESIGN.md)：主板尺寸目标、双面叠放和小型器件选择约束。

## 方案总览

- **主控**：ESP32-C3 SuperMini 使用自定义 2×8 混合封装平放手焊；这是唯一手焊电子器件。天线端
  **悬出板边或在其下方/前方完整禁铜**。
- **屏**：0.96" SSD1306 OLED（单独一块），主板出 **4P 排线座**（3V3/GND/SDA/SCL）引到外壳正面。
- **全 SMD 由 JLC 贴**：小型无源蜂鸣器、薄型 EC05 编码器、独立小型确认键、SMD 振动马达，
  以及全部驱动/阻容/触摸/电源器件。
- **触摸**：不用触摸模块——在 PCB 顶层做一块**铜箔触摸片**接 TTP223（周围挖空地铜、盖阻焊、丝印画图标）。
- **供电基线**：主板 USB-C + BQ24074 power-path + 单节锂电池 + 电源开关。插 USB 时 USB 优先
  供系统并同时充电；拔 USB 自动切换电池；关机仍能充电。
- **手工焊/装配**：只手焊 SuperMini；OLED、电池和旋钮帽采用插接/压装，其余电子器件由 JLC 贴好。

## 供电架构

```
主板 USB-C ──► BQ24074 power-path ──► 电池
                    │
                    └─► VSYS ──► 电源开关/后级升压 ──► 5V_SYS
                                                     ├─► SuperMini 5V
                                                     └─► 外设 3.3V
```
- 主板 USB-C 负责日常供电和充电；SuperMini 自带 USB-C 只作烧录服务口。
- 插 USB 时系统负载优先，余量用于充电；拔 USB 自动由电池供电。
- 电源开关关闭后系统负载断电，但充电管理仍工作。
- SuperMini 的 3.3V 输出与载板 3.3V 不相连，避免两个稳压器并联。
- 使用 SuperMini USB 烧录前必须把主电源关闭，并通过 PCB 防倒灌器件进一步隔离。

## 引脚分配（沿用现固件，避开 strapping 2/8/9）

| GPIO | 功能 |
|---|---|
| 1  | 编码器 CLK (A) |
| 3  | 蜂鸣器（经 SS8550 低有效驱动）|
| 4  | 触摸（TTP223 输出）|
| 5  | OLED SDA |
| 6  | OLED SCL |
| 7  | 编码器 DT (B) |
| 10 | 振动马达（经 SS8050 高有效驱动）|
| 20 | 编码器按键 SW |
| 3V3/GND | 供电 |
| 空闲 | 0, 21（可留调试焊盘）|

## 平面布局（小型双面叠放）

```
主板正面：USB-C / 充电电源 / 小型编码器 / 按键 / 触摸 / 蜂鸣器 / 马达 / 接口
主板背面：ESP32-C3 SuperMini 手焊，元件面朝外

目标板框：优先尝试约 24×32mm；若热设计、USB、天线或机械件无法满足，则控制在 28×38mm 内。
```
- SuperMini 与正面 SMT 器件在平面上重叠，利用双面面积；天线区域例外，所有层完整 keepout。
- 编码器、按键、触摸片和 USB-C 放在外壳可操作边；蜂鸣器声孔和马达受力面保持无遮挡。

## 嘉立创下单要点

1. 在立创EDA 画原理图（按 `SCHEMATIC.md` 连接表）→ 导入元件（按 `BOM.csv` 的 LCSC 编号）→ PCB 布局布线 → DRC。
2. 下单：PCB + SMT 贴片；小型版包含 QFN、电源器件、薄型编码器、蜂鸣器和马达。是否适用
   Economic PCBA、双面装配或治具，以最终 BOM 在下单页面的实时提示为准。
3. **不贴、留给你手工**：仅 SuperMini；它在 BOM/CPL 中标为 DNP。OLED、电池使用工厂已贴好的
   SMD 防呆连接器和预制线束。
4. 收板后：检查电源 → 手焊 SuperMini → 插接屏幕/电池 → 烧录固件。

详见 [SCHEMATIC.md](SCHEMATIC.md)（逐块原理图+连接表）与 [BOM.csv](BOM.csv)（元件+LCSC 编号）。
