# AgentBell 单板 原理图草案（连接表）

> **未定版、不可直接投板。** 本文件仍保留旧 TP4056 锂电方案作为历史草案。当前正式基线是主板
> USB-C + BQ24074 power-path + 后级系统电源、SuperMini 平贴手焊、其余器件由嘉立创贴装；应按
> [SUPERMINI_CARRIER_SPEC.md](SUPERMINI_CARRIER_SPEC.md) 和 [PCBA_STRATEGY.md](PCBA_STRATEGY.md)
> 以及 [POWER_PATH.md](POWER_PATH.md) 重画正式原理图，不要逐项照抄本文件。

电源网络：`VBUS`(USB-C 5V，仅充电) · `VBAT`(锂电正) · `3V3`(主电源轨) · `GND`。
按下面逐块在立创EDA连线即可。元件编号见 [BOM.csv](BOM.csv)。

---

## 1. 充电（TP4056，SOP-8）

| TP4056 脚 | 接到 |
|---|---|
| VCC(4) | VBUS；对地 10µF |
| BAT(5) | VBAT（→ 锂电 JST +）；对地 10µF |
| PROG(2) | R_prog 1.2kΩ → GND（设定 ~1A 充电）|
| CE(8) | VCC |
| CHRG(7) | → LED红 → R 1kΩ → VBUS（充电中亮，开漏拉低）|
| STDBY(6) | → LED绿 → R 1kΩ → VBUS（充满亮）|
| GND(3)+散热盘 | GND |

USB-C(16P) 座：VBUS/GND 取电；CC1/CC2 各 5.1kΩ 下拉到 GND（识别为受电端）；D+/D− 可不接。

## 2. 电源开关 + 3.3V LDO

| 网络 | 连接 |
|---|---|
| VBAT → 开关 | SPDT 滑动开关（SK-12D07）：公共=VBAT，一档→LDO_IN（ON），另一档悬空（OFF）|
| LDO(AP2112K-3.3, SOT-23-5) | IN=LDO_IN；EN=LDO_IN；GND=GND；OUT=3V3 |
| 电容 | IN 对地 1µF；OUT 对地 1µF + 22µF；3V3 轨再加 **100µF** 体电容（WiFi 峰值）|

> 3V3 同时供：SuperMini 的 3V3 脚、蜂鸣/马达/触摸/编码器、OLED。编程时开关拨 OFF。

## 3. 主控 ESP32-C3 SuperMini（2×8 母座，THT 手工）

| SuperMini 脚 | 接到 |
|---|---|
| 3V3 | 3V3 |
| GND | GND |
| GPIO1 | ENC_A（编码器 CLK）|
| GPIO7 | ENC_B（编码器 DT）|
| GPIO20 | ENC_SW（编码器按键）|
| GPIO3 | BUZ_DRV（→蜂鸣驱动）|
| GPIO10 | VIB_DRV（→马达驱动）|
| GPIO4 | TOUCH_OUT（←TTP223）|
| GPIO5 | I2C_SDA（→OLED 座）|
| GPIO6 | I2C_SCL（→OLED 座）|
| 其余脚 | 不接（可把 GPIO0/21 引到测试焊盘）|

天线端短边悬出板边，正下方 keepout（无铜、无地）。

## 4. 蜂鸣器（历史草案：MLT-8530 + SS8550）

> 小型正式版将改为 MLT-5020 / C94598 及重新核算的驱动电路，本节只保留旧方案参考。

```
3V3 ──emitter[SS8550]collector── Buzzer(+) ── Buzzer(−) ── GND
GPIO3 ──R 1kΩ── base ;  base ──R 10kΩ── 3V3(上拉，开机默认关)
```
GPIO3 输出低电平方波 → PNP 导通 → 蜂鸣器发声（固件 ToneBuzzer 就是低有效）。

## 5. 振动马达（SMD 币式 + SS8050 NPN，高电平触发＝配现固件）

```
3V3 ── Motor(+) ── Motor(−)=节点M ──collector[SS8050]emitter── GND
GPIO10 ──R 1kΩ── base
续流二极管 1N4148W：anode=节点M，cathode=3V3   ← 感性负载必须加，防反峰烧管/MCU
```
GPIO10 高 → NPN 导通 → 马达转（固件 VIB_ACTIVE_HIGH=true）。

## 6. 触摸（TTP223-BA6，SOT-23-6）

| TTP223 脚 | 接到 |
|---|---|
| VDD(5) | 3V3；VDD-VSS 就近 0.1µF |
| VSS(2) | GND |
| I(3) | **PCB 顶层铜箔触摸片**；可留 Cs 焊盘（I→GND，默认不贴=最灵敏）|
| Q(1) | GPIO4 |
| AHLB(4) | 悬空（默认高有效）|
| TOG(6) | 悬空（默认点动：按住输出有效）|

触摸片：顶层 8–15mm 铜箔，盖阻焊，正下方及四周挖空地铜；表面可覆薄亚克力。

## 7. 旋转编码器（历史草案：EC11）

> 小型正式版改用 EC05E1220401 / C116648，并增加独立小型确认键；本节不可直接照抄。

| EC11 脚 | 接到 |
|---|---|
| A | ENC_A(GPIO1)；上拉 10kΩ→3V3；去抖 10nF→GND |
| C（公共）| GND |
| B | ENC_B(GPIO7)；上拉 10kΩ→3V3；去抖 10nF→GND |
| SW1 | ENC_SW(GPIO20)；上拉 10kΩ→3V3；0.1µF→GND |
| SW2 | GND |

（固件已开 INPUT_PULLUP + 中断解码，外部上拉/电容是额外抗抖，更稳。）

## 8. OLED 排线座（4P，JST-PH2.0 或 1×4 排针）

| 座脚 | 接到 |
|---|---|
| 1 | 3V3 |
| 2 | GND |
| 3 | I2C_SDA(GPIO5) |
| 4 | I2C_SCL(GPIO6) |

I2C 上拉：SDA/SCL 各 4.7kΩ→3V3（OLED 模块多自带，可做成焊盘选贴）。

---

## 去耦与杂项
- 每个有源器件 VCC 就近 0.1µF；3V3 轨 100µF 体电容。
- 全部 GND 一体地平面（触摸片下方除外，那里 keepout）。
- 可选：3V3 上一颗电源指示 LED + 1kΩ。
