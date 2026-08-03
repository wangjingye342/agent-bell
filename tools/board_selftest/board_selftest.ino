// ============================================================================
//  board_selftest.ino — AgentBell v2 载板首次上电自检（不联网、执行器手动触发）
// ----------------------------------------------------------------------------
//  给新焊的板子做最小损失验证：上电先把蜂鸣/震动锁在「不响」电平，然后按顺序
//  自检 I2C → OLED → 编码器；蜂鸣和震动只在你按键时给一个短脉冲（焊错也只响一下）。
//
//  操作：
//    转动旋钮      → 屏幕/串口上计数变化（验证 CLK/DT 相序与方向）
//    短按旋钮      → 蜂鸣 120ms 中等音量（验证 PNP 低有效驱动）
//    长按旋钮 0.5s → 震动 150ms 60% 占空比（验证 NMOS 高有效驱动）
//
//  一切反馈同时走 OLED 和串口 115200：屏幕不亮也能从串口判断卡在哪一步。
//
//  编译烧录（ASCII 字体，无需 huge_app）：
//    arduino-cli compile --fqbn esp32:esp32:esp32c3 --output-dir tools/board_selftest/build tools/board_selftest
//    arduino-cli upload -p COM6 --fqbn esp32:esp32:esp32c3 --input-dir tools/board_selftest/build
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// —— 引脚（与正式固件 agent_bell.ino 一致，对应 v2 golden-netlist）——
#define OLED_SDA   5
#define OLED_SCL   6
#define BUZZER_PIN 3     // PNP 低有效：HIGH=静音
#define VIB_PIN    10    // NMOS 高有效：LOW=停
#define ENC_CLK    1
#define ENC_DT     7
#define ENC_SW     20
#define TOUCH_PIN  4     // 预留 TOUCH_SIG，只读不驱动

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// —— 编码器：正交表解码（与正式固件同款）——
static const int8_t QTAB[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
volatile int32_t encRaw = 0;
volatile uint8_t encPrev = 0;
void IRAM_ATTR encISR() {
  uint8_t cur = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);
  encPrev = ((encPrev << 2) | cur) & 0x0F;
  encRaw += QTAB[encPrev];
}

uint8_t i2cAddrs[8]; int i2cN = 0;   // I2C 扫描结果
bool oledOk = false;
int beeps = 0, vibs = 0;             // 已触发次数（上屏）

static void beep120() {              // 短蜂鸣：2700Hz，中低音量（低有效：256=静音,128=最响）
  ledcChangeFrequency(BUZZER_PIN, 2700, 8);
  ledcWrite(BUZZER_PIN, 200);
  delay(120);
  ledcWrite(BUZZER_PIN, 256);
  beeps++;
  Serial.printf("[test] beep #%d done\n", beeps);
}

static void vib150() {               // 短震动：20kHz 载波 60% 占空比
  ledcWrite(VIB_PIN, 153);
  delay(150);
  ledcWrite(VIB_PIN, 0);
  vibs++;
  Serial.printf("[test] vib #%d done\n", vibs);
}

void setup() {
  // ---- 第 0 步：安全默认 —— 执行器先锁死在「不响」电平，再做其它任何事 ----
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, HIGH);
  pinMode(VIB_PIN, OUTPUT);    digitalWrite(VIB_PIN, LOW);

  Serial.begin(115200);
  delay(600);                        // 等 USB-CDC 枚举，头几行日志别丢
  Serial.println("\n===== AgentBell v2 board self-test =====");

  // ---- 第 1 步：I2C 总线扫描（OLED 应答 0x3C 或 0x3D）----
  Wire.begin(OLED_SDA, OLED_SCL);
  for (uint8_t a = 8; a < 120 && i2cN < 8; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) i2cAddrs[i2cN++] = a;
  }
  Serial.printf("[i2c] %d device(s):", i2cN);
  for (int i = 0; i < i2cN; i++) Serial.printf(" 0x%02X", i2cAddrs[i]);
  Serial.println(i2cN ? "" : " none! check SDA=5 SCL=6 wiring/pullups");

  // ---- 第 2 步：OLED 初始化 + 全亮一闪（看有没有坏点/花屏）----
  bool has3C = false, has3D = false;
  for (int i = 0; i < i2cN; i++) { if (i2cAddrs[i] == 0x3C) has3C = true; if (i2cAddrs[i] == 0x3D) has3D = true; }
  if (has3C || has3D) {
    u8g2.setI2CAddress((has3C ? 0x3C : 0x3D) << 1);
    u8g2.begin();
    u8g2.setBusClock(700000);        // 与正式固件一致；100kHz 全帧要 ~110ms，会把按键采样拖出可感延迟
    oledOk = true;
    u8g2.clearBuffer(); u8g2.drawBox(0, 0, 128, 64); u8g2.sendBuffer();  // 全亮
    delay(400);
    Serial.println("[oled] init OK (full-white flashed)");
  } else {
    Serial.println("[oled] NOT FOUND - continuing with serial only");
  }

  // ---- 第 3 步：编码器（板载上拉已有，内部再并一份无妨）----
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);         // 预留脚只观察
  encPrev = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encISR, CHANGE);
  Serial.printf("[enc] idle levels CLK=%d DT=%d SW=%d (all should be 1)\n",
                digitalRead(ENC_CLK), digitalRead(ENC_DT), digitalRead(ENC_SW));

  // ---- 执行器挂上 LEDC（初值仍为静默）----
  ledcAttach(BUZZER_PIN, 2700, 8);  ledcWrite(BUZZER_PIN, 256);
  ledcAttach(VIB_PIN, 20000, 8);    ledcWrite(VIB_PIN, 0);

  Serial.println("[ready] rotate=count  short-press=beep  hold-0.5s=vib");
}

void loop() {
  // —— 按键：短按=蜂鸣，长按=震动（与正式固件同样的消抖思路）——
  static bool down = false, longFired = false;
  static unsigned long t0 = 0, tEdge = 0;
  static int lastRaw = HIGH;
  int raw = digitalRead(ENC_SW);
  if (raw != lastRaw) { tEdge = millis(); lastRaw = raw; }
  if (millis() - tEdge > 25) {
    if (raw == LOW && !down) { down = true; longFired = false; t0 = millis(); }
    else if (raw == HIGH && down) { down = false; if (!longFired) beep120(); }
    if (down && !longFired && millis() - t0 >= 500) { longFired = true; vib150(); }
  }

  // —— 状态上屏 + 串口心跳（USB-CDC 复位后重枚举，启动日志常抓不到 → 周期重播关键状态）——
  static int32_t lastShown = INT32_MIN;
  static unsigned long lastBeat = 0;
  int32_t rawCnt; noInterrupts(); rawCnt = encRaw; interrupts();
  if (rawCnt != lastShown && lastShown != INT32_MIN)
    Serial.printf("[enc] raw=%ld (detent=%ld)\n", (long)rawCnt, (long)(rawCnt / 4));
  lastShown = rawCnt;
  if (millis() - lastBeat > 2000) {
    lastBeat = millis();
    Serial.printf("[beat] up=%lus i2c=%d@0x%02X oled=%d enc=%ld sw=%d tp=%d beeps=%d vibs=%d\n",
                  millis() / 1000, i2cN, i2cN ? i2cAddrs[0] : 0, oledOk ? 1 : 0,
                  (long)(rawCnt / 4), digitalRead(ENC_SW), digitalRead(TOUCH_PIN), beeps, vibs);
  }

  // —— 屏幕节流重画（80ms 一帧足够；输入采样不被渲染拖慢）——
  static unsigned long lastDraw = 0;
  if (oledOk && millis() - lastDraw > 80) {
    lastDraw = millis();
    char l[32];
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, "AgentBell v2 selftest");
    snprintf(l, sizeof(l), "I2C:%d dev @0x%02X", i2cN, i2cN ? i2cAddrs[0] : 0);
    u8g2.drawStr(0, 24, l);
    snprintf(l, sizeof(l), "ENC:%-6ld SW:%s", (long)(rawCnt / 4), digitalRead(ENC_SW) ? "up" : "DOWN");
    u8g2.drawStr(0, 38, l);
    snprintf(l, sizeof(l), "beep:%d vib:%d tp:%d", beeps, vibs, digitalRead(TOUCH_PIN));
    u8g2.drawStr(0, 52, l);
    u8g2.drawStr(0, 63, "press=beep hold=vib");
    u8g2.sendBuffer();
  }
  delay(1);
}
