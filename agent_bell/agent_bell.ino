// ============================================================================
//  AgentBell — ESP32-C3 SuperMini + SSD1306 128x64 OLED 智能体提醒设备
// ----------------------------------------------------------------------------
//  作用：电脑上的 Claude Code / Codex 完成一轮对话时，通过局域网 HTTP 通知本设备，
//        它会「蜂鸣 + 震动」并在 OLED 上显示：哪台电脑、哪个智能体、哪个对话。
//
//  架构：本设备是 HTTP 服务器（局域网内任意电脑都能 push）。电脑侧用 Claude 的
//        Stop hook / Codex 的 notify 程序，在对话结束时 POST /notify 过来。
//
//  录音→语音识别→回填输入框那部分留待以后（GPIO4 已空出，可留给麦克风）。
//
//  编译（Huge App 分区容纳中文字体）：
//    arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app \
//        --output-dir agent_bell/build agent_bell
//  定义 SIM_DEMO 宏则跳过联网、开机自注入示例通知（给 Wokwi 预览界面用）。
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>          // 待机屏时钟：SNTP 同步后 time()/localtime() 取本地时间
#include <sys/time.h>      // settimeofday（SIM_DEMO 预览注入假时间用）

#ifndef SIM_DEMO
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include <DNSServer.h>     // 配网热点的强制门户（captive portal）DNS
  #include <Preferences.h>   // 把网页里的开关存进 NVS，掉电不丢
#endif

#ifdef INO_SIM
  #include <sim_inject.h>    // ino-sim 屏幕模拟器：从 scenario 的 var 注入界面数据
#else
  #include <esp_sleep.h>     // 「关机」=深睡眠；转动编码器（GPIO1）唤醒
  #include <driver/gpio.h>   // gpio_hold_en：睡眠期把蜂鸣/震动脚锁在「不响」电平
#endif

// 前置声明：有函数在这两个类型定义之前被 arduino-cli 自动生成原型引用（Note* / ListAnim&），先声明避免"未命名类型"
struct Note;
struct ListAnim;
struct SetMeta;
enum SubMenu { SUB_NONE, SUB_MELODY };                          // 选项子列表（当前仅提示音用竖列表）
enum SetId   { SET_BUZVOL, SET_VIBVOL, SET_FBMODE, SET_FBVOL, SET_FBVIB, SET_ENCSENS, SET_LSTYLE, SET_RSTYLE };  // 滑块设置项（定义在顶部，供自动原型引用）

// ===== 硬件引脚（ESP32-C3 SuperMini，全部避开 strapping 脚 2/8/9）=====
#define OLED_SDA   5      // I2C 数据
#define OLED_SCL   6      // I2C 时钟
#define OLED_ADDR  0x3C   // 少数屏是 0x3D
#define BUZZER_PIN 3      // 蜂鸣器 I/O
#define VIB_PIN    10     // 震动模块 IN
// 触摸模块已移除（2026-07-30）：其"返回"功能与编码器长按完全重合，PCB 放不下遂砍掉。
// GPIO4 因此空出 —— 麦克风（以后录音用）可优先考虑它（原预留的 7/20 已被编码器占用）。
// —— 旋转编码器 HW-040（⚠ 电源脚 + 接 3V3，绝不能接 5V！输出电平跟随供电，5V 会烧 C3）——
#define ENC_CLK 1     // A 相（兼作「关机」的深睡眠唤醒脚：C3 只有 GPIO0-5 能唤醒，恰好在范围内）
#define ENC_DT  7     // B 相
#define ENC_SW  20    // 按压（低有效，用内部上拉；不在 GPIO0-5 内 → 关机后按它无法开机，得转旋钮）
#define ENC_RAW_PER_DETENT 4   // 本编码器每个物理档位产生的正交沿数（v2 PCB 板载编码器实测 4/档；旧 HW-040 模块是 2/档，换回模块记得改回 2）
// 旋转方向 encReversed、灵敏度 encDetents（每几档动一步）都是运行时设置（菜单/网页可调）

// ===== 模块触发极性 =====
// 蜂鸣器：无源 + 低电平触发（PNP 驱动），发声/静音逻辑在 ToneBuzzer 内处理，无需在此配。
#define VIB_ACTIVE_HIGH     true    // 震动模块触发极性（装上后不对就翻这里）

// ===== 联网配置 =====
// WiFi 凭据来源（优先级）：NVS 存的（配网写入）→ secrets.h（编译期烧录）→ 占位符。
// 到了陌生网络：开机连不上 → 自动开 AgentBell-XXXX 热点 + 配置页；也可菜单「重新配网」手动进。
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  const char* WIFI_SSID = "YOUR_WIFI_SSID";
  const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
#endif
const char* MDNS_NAME = "agent-bell";      // mDNS 主机名（仅供 /notify 发现，界面不再显示）
char wifiSsid[33] = "";                    // 实际使用的凭据（loadWifiCred 决定来源）
char wifiPass[65] = "";

// ============================================================================
//  声音配置（想换提示音，改这里就够了）
//  蜂鸣器（无源）音序：ToneSeg = {起始频率Hz, 结束频率Hz, 时长ms}
//    · f1==f2 → 稳定音；  f1≠f2 → 从 f1 连贯滑到 f2（滑音）；  f1==0 → 静音间隔
//  震动马达：纯时长数组（开,关,开…，从「开」起，单位 ms）
// ============================================================================
struct ToneSeg { uint16_t f1, f2, ms; };
// —— Agent 完成通知：多种可选提示音（网页可试听/选用）。f1==f2 稳定音；f1≠f2 滑音；f1==0 静音 ——
static const ToneSeg TONE_DINGDONG[] = { {1319,1319,180},{0,0,70},{1047,1047,600} };                              // 叮咚·经典下行
static const ToneSeg TONE_TRIAD[]    = { {1568,1568,150},{0,0,45},{1319,1319,150},{0,0,45},{1047,1047,520} };     // 柔和三音下行
static const ToneSeg TONE_UP[]       = { {1047,1047,140},{0,0,40},{1319,1319,140},{0,0,40},{1568,1568,460} };     // 上行叮铃
static const ToneSeg TONE_DOUBLE[]   = { {1760,1760,90},{0,0,90},{1760,1760,90} };                               // 双叮·短促
static const ToneSeg TONE_ARP[]      = { {784,784,120},{0,0,28},{1047,1047,120},{0,0,28},{1319,1319,120},{0,0,28},{1568,1568,420} }; // 琶音上行
static const ToneSeg TONE_SLIDE[]    = { {1047,1047,110},{1047,784,560},{784,784,320} };                         // 滑音·叮———咚
static const ToneSeg TONE_SINGLE[]   = { {988,988,520} };                                                        // 单长音·简洁
struct Melody { const char* name; const ToneSeg* seq; uint8_t len; };
#define MEL(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
static const Melody MELODIES[] = {
  {"叮咚·经典", TONE_DINGDONG, MEL(TONE_DINGDONG)},
  {"柔和三音",  TONE_TRIAD,    MEL(TONE_TRIAD)},
  {"上行叮铃",  TONE_UP,       MEL(TONE_UP)},
  {"双叮·短促", TONE_DOUBLE,   MEL(TONE_DOUBLE)},
  {"琶音上行",  TONE_ARP,      MEL(TONE_ARP)},
  {"滑音",      TONE_SLIDE,    MEL(TONE_SLIDE)},
  {"单长音",    TONE_SINGLE,   MEL(TONE_SINGLE)},
};
static const int MEL_N = sizeof(MELODIES) / sizeof(MELODIES[0]);
static const uint16_t ALERT_VIB[]  = { 450, 180, 450 };    // 通知震动：长震-停-长震
// —— 按键 / 反馈音（短促，跟随强度滑条）——
static const ToneSeg TAP_TONES[]   = { {3000, 3000, 45} };  // 旋钮反馈：短促「嘀」
static const ToneSeg FB_TONES[]    = { {2637, 2637, 110} }; // 调强度时的反馈音
static const uint16_t FB_VIB[]     = { 220 };              // 调强度反馈短震
static const uint16_t TAP_VIB[]    = { 95 };               // 旋钮反馈：轻震（够长以启动马达）

// 蜂鸣器是无源的：靠方波频率发声（音高可调）；震动马达用 PWM 占空比调强度。
static const uint32_t VIB_FREQ = 20000;   // 震动马达 PWM 载波，占空比=震动强度

// ===== 显示对象：SSD1306 128x64，硬件 I2C，F=全缓冲（1KB RAM）=====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/ U8X8_PIN_NONE);
// 中文用文泉驿 GB2312 全字库（存 flash，故需 huge_app）；大号拉丁字体给智能体名。
#define FONT_CN   u8g2_font_wqy12_t_gb2312
#define FONT_BIG  u8g2_font_helvB14_tr
#define FONT_CLOCK u8g2_font_logisoso20_tn   // 待机屏时钟数字（纯数字字体，含 : 和 -）
#define FONT_CLOCK_XL u8g2_font_logisoso26_tn // 「大字时间」样式用更大号数字
#include "astronaut_frames.h"   // 绕轴自转太空人逐帧位图（tools/gen_astronaut.py 生成）

#ifndef SIM_DEMO
WebServer server(80);
#endif

// ============================================================================
//  非阻塞脉冲器：按节奏数组驱动一个输出脚（蜂鸣器 / 震动），全程不 delay。
// ============================================================================
class Pulser {
 public:
  void begin(uint8_t pin, bool activeHigh, uint32_t freq) {
    pin_ = pin; activeHigh_ = activeHigh;
#ifdef INO_SIM
    (void)freq; pinMode(pin_, OUTPUT);          // 仿真主机端无 LEDC，退化为数字开关
#else
    ledcAttach(pin_, freq, 8);                  // 8-bit PWM，占空比即强度/响度
#endif
    setVolume(100);
    ovDuty_ = duty_;
    on_ = false; active_ = false; drive();      // 上电即置为「不响」
  }
  void setVolume(int v) {                        // 0-100 → 占空比；100=满占空比(直流常通,最响)
    if (v < 0) v = 0; if (v > 100) v = 100;
    duty_ = (uint32_t)v * 256 / 100;             // 100→256：8-bit 的"100%"特值=DC 常通
  }
  void trigger(const uint16_t* pat, uint8_t len, int volOverride = -1) {
    ovDuty_ = (volOverride >= 0) ? (uint32_t)volOverride * 256 / 100 : duty_;   // 可临时指定音量(0-100)
    pat_ = pat; len_ = len; idx_ = 0;
    on_ = true; active_ = true; tMark_ = millis(); drive();
  }
  void stop() { active_ = false; on_ = false; drive(); }
  bool busy() const { return active_; }
  void update() {
    if (!active_) return;
    if (millis() - tMark_ >= pat_[idx_]) {
      idx_++;
      if (idx_ >= len_) { stop(); return; }      // 节奏走完
      on_ = !on_; tMark_ = millis(); drive();     // 开/关交替
    }
  }
 private:
  void drive() {
#ifdef INO_SIM
    bool lvl = on_ ? activeHigh_ : !activeHigh_;
    digitalWrite(pin_, lvl ? HIGH : LOW);
#else
    uint32_t d = on_ ? ovDuty_ : 0;
    ledcWrite(pin_, activeHigh_ ? d : (256 - d));   // 低有效则反相（满刻度 256）
#endif
  }
  uint8_t pin_ = 0; bool activeHigh_ = true; uint32_t duty_ = 255, ovDuty_ = 255;
  const uint16_t* pat_ = nullptr; uint8_t len_ = 0, idx_ = 0;
  bool on_ = false, active_ = false; unsigned long tMark_ = 0;
};

// ============================================================================
//  无源蜂鸣器音调播放器：按 {频率,时长} 音序发方波发声，可调音高/音量，非阻塞。
//  低电平触发（PNP）：占空比 50% 最响、导通在低电平；静音时输出高电平（关断）。
// ============================================================================
class ToneBuzzer {
 public:
  void begin(uint8_t pin) {
    pin_ = pin;
#ifndef INO_SIM
    ledcAttach(pin_, 2700, 8);                 // 先挂上，频率随音序实时改
#else
    pinMode(pin_, OUTPUT);
#endif
    idleSilent();
  }
  void setVolume(int v) { vol_ = v < 0 ? 0 : (v > 100 ? 100 : v); }
  void play(const ToneSeg* seq, uint8_t n, int volOverride = -1) {
    seq_ = seq; n_ = n; i_ = 0; active_ = true; lastFreq_ = -1;
    ovVol_ = (volOverride >= 0) ? volOverride : vol_;
    tSeg_ = millis(); applyCur();
  }
  void stop() { active_ = false; idleSilent(); }
  bool busy() const { return active_; }
  void update() {
    if (!active_) return;
    unsigned long now = millis();
    if (now - tSeg_ >= seq_[i_].ms) {           // 本段结束 → 下一段
      if (++i_ >= n_) { stop(); return; }
      tSeg_ = now; applyCur(); return;
    }
    if (now - tFreq_ >= 8) applyCur();          // 滑音：约每 8ms 刷新一次频率
  }
 private:
  void applyCur() {
    tFreq_ = millis();
#ifndef INO_SIM
    const ToneSeg& s = seq_[i_];
    if (s.f1 == 0) { idleSilent(); lastFreq_ = -1; return; }        // 静音段
    uint32_t el = millis() - tSeg_; if (el > s.ms) el = s.ms;
    int span = s.ms ? (int)s.ms : 1;
    int f = (int)s.f1 + ((int)s.f2 - (int)s.f1) * (int)el / span;   // 线性滑音
    if (f != lastFreq_) { ledcChangeFrequency(pin_, f, 8); lastFreq_ = f; }
    ledcWrite(pin_, 128 + (uint32_t)(100 - ovVol_) * 128 / 100);    // 占空比=音量(50%最响)
#endif
  }
  void idleSilent() {
#ifndef INO_SIM
    ledcWrite(pin_, 256);                       // 高电平=关（无源蜂鸣器直流不发声）
#else
    digitalWrite(pin_, HIGH);
#endif
  }
  uint8_t pin_ = 0; const ToneSeg* seq_ = nullptr; uint8_t n_ = 0, i_ = 0;
  bool active_ = false; int vol_ = 100, ovVol_ = 100, lastFreq_ = -1;
  unsigned long tSeg_ = 0, tFreq_ = 0;
};

Pulser vibrator;
ToneBuzzer buzzer;

// ============================================================================
//  旋转编码器 HW-040：正交状态表解码（CLK/DT 双中断累加，不丢步）+ 按键短/长按
// ============================================================================
static const int8_t QTAB[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
volatile int32_t encRaw = 0;      // 原始正交计数
volatile uint8_t encPrev = 0;
bool encReversed = false;         // 旋钮方向反转（运行时设置，存 NVS）
int  encDetents  = 1;             // 每几个物理档位移动一步（1/2/3，运行时设置，存 NVS）

#ifndef INO_SIM
void IRAM_ATTR encISR() {
  uint8_t cur = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);
  encPrev = ((encPrev << 2) | cur) & 0x0F;
  encRaw += QTAB[encPrev];
}
#endif

static void encBegin() {
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  encPrev = (digitalRead(ENC_DT) << 1) | digitalRead(ENC_CLK);
#ifndef INO_SIM
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT),  encISR, CHANGE);
#endif
}

// 消费自上次以来的档位增量（±每档一步；方向可反）
static int encSteps() {
  static int32_t consumed = 0;
  int d = ENC_RAW_PER_DETENT * (encDetents < 1 ? 1 : encDetents);   // 每步所需正交沿数
  int32_t raw;
  noInterrupts(); raw = encRaw; interrupts();
  int s = 0;
  while (raw - consumed >=  d) { s++; consumed += d; }
  while (raw - consumed <= -d) { s--; consumed -= d; }
  return encReversed ? -s : s;
}

// 编码器按键：0=无 1=短按(松手) 2=长按(按住到时)
static int encButton() {
  static bool down = false, longFired = false;
  static unsigned long t0 = 0, tEdge = 0;
  static int lastRaw = HIGH;
  int raw = digitalRead(ENC_SW);                 // 低有效
  if (raw != lastRaw) { tEdge = millis(); lastRaw = raw; }
  bool stable = millis() - tEdge > 25;
  int ev = 0;
  if (stable && raw == LOW && !down) { down = true; longFired = false; t0 = millis(); }
  else if (stable && raw == HIGH && down) { down = false; if (!longFired) ev = 1; }
  if (down && !longFired && millis() - t0 >= 500) { longFired = true; ev = 2; }
  return ev;
}

// ============================================================================
//  通知环形缓冲（保留最近 8 条）
// ============================================================================
struct Note {
  char computer[24];
  char agent[16];
  char conversation[48];
  char message[80];
  unsigned long recv;   // millis() 收到时刻
};

static const int RING_N = 8;
Note ring[RING_N];
int  ringCount = 0;     // 已存条数（≤8）
int  ringHead  = 0;     // 下一个写入位置
int  unread    = 0;     // 未读徽标
int  viewOffset = 0;    // 0=最新，1=次新…；-1=待机（无查看）

// 取「第 k 新」的通知（k=0 最新）。越界返回 nullptr。
Note* noteByOffset(int k) {
  if (k < 0 || k >= ringCount) return nullptr;
  int idx = (ringHead - 1 - k + RING_N) % RING_N;
  return &ring[idx];
}

void addNote(const Note& n) {
  ring[ringHead] = n;
  ringHead = (ringHead + 1) % RING_N;
  if (ringCount < RING_N) ringCount++;
  viewOffset = 0;                          // 跳到最新
  if (unread < ringCount) unread++;
}

bool needRender = true;
unsigned long lastRender = 0;

// ============================================================================
//  运行时可调设置（网页控制台改，存 NVS 掉电不丢）
// ============================================================================
bool buzzerEnabled = true;    // 蜂鸣器总开关
bool vibEnabled    = true;    // 震动总开关
bool dnd           = false;   // 勿扰：收到通知只屏显，不响不震
int  buzzerVol     = 100;     // 通知蜂鸣强度 0-100
int  vibVol        = 100;     // 通知震动强度 0-100
int  alertTone     = 0;       // 选用的提示音索引（对应 MELODIES）
int  feedbackMode  = 0;       // 操作反馈：0=声+震 1=仅震 2=仅声 3=无
int  fbVol         = 60;      // 操作反馈·音量（与通知分开；够明显但比通知小）
int  fbVibVol      = 75;      // 操作反馈·震动强度（与通知分开；需高于马达启动阈值）
int  leftStyle     = 0;       // 待机屏左半模块索引（对应 PANE_NAMES）
int  rightStyle    = 1;       // 待机屏右半模块索引
// ===== 待机屏半屏模块池（左右两半都是 64×64，同一池子里任选，菜单/API/桥接共用）=====
// 模块渲染函数在下方"待机屏模块"一节定义；这里先声明，供函数指针表引用。
static void paneAstro(int x0);     static void paneClock(int x0);
static void paneAnalog(int x0);    static void paneBigTime(int x0);
static void paneCalendar(int x0);  static void paneInfo(int x0);
static void paneRadar(int x0);     static void paneMeteor(int x0);
static const char* PANE_NAMES[] = {"宇航员", "数字时钟", "模拟表盘", "竖排大钟", "日历", "信息面板", "雷达扫描", "流星夜空"};
static void (* const PANE_FNS[])(int) = {paneAstro, paneClock, paneAnalog, paneBigTime, paneCalendar, paneInfo, paneRadar, paneMeteor};
static const int PANE_N = sizeof(PANE_NAMES) / sizeof(PANE_NAMES[0]);
bool screenOff   = false;         // 屏幕是否熄灭（运行时，不持久）
bool notifyWakes = true;          // 熄屏时来通知是否自动亮屏

// ===== UI 状态机（编码器导航的丝滑菜单）=====
enum UiState { UI_IDLE, UI_NOTE, UI_MENU, UI_SLIDER, UI_POPUP };
UiState uiState = UI_IDLE;
SubMenu curSub = SUB_NONE;        // 主菜单里进入的"选项子列表"（当前仅提示音）
SetId curSet = SET_BUZVOL;        // 当前正在调的滑块设置项
int   mainSel = 0, subSel = 0;    // 主列表 / 子列表当前选中项
unsigned long lastInput = 0;      // 最近交互（15s 回待机）
unsigned long melSettleAt = 0;    // 提示音停留自动试听计时
int   melPreviewed = -1;
struct ListAnim { float top, sel; };
ListAnim mainAnim = {0, 0}, subAnim = {0, 0};   // 缓动状态（主列表 / 子列表）
float sliderAnim = 0;             // 滑块 thumb 缓动位置（0..1）

#ifndef SIM_DEMO
Preferences prefs;

// ============================================================================
//  WiFi 凭据（NVS 优先，secrets.h 兜底）+ AP 配网模式
// ============================================================================
bool apMode = false;              // 当前是否在配网热点模式
char apSsid[20] = "";             // AgentBell-XXXX（XXXX=MAC 尾两字节）
unsigned long apStartedAt = 0;    // 配网模式起始时刻（无人配网 10min 自动重启重试）
DNSServer dnsServer;              // 强制门户：所有域名都解析到设备自己

static void loadWifiCred() {
  prefs.begin("agentbell", true);
  String s = prefs.getString("wifi_ssid", "");
  String p = prefs.getString("wifi_pass", "");
  prefs.end();
  if (s.length()) {                          // NVS 里有配网写入的凭据 → 优先
    strlcpy(wifiSsid, s.c_str(), sizeof(wifiSsid));
    strlcpy(wifiPass, p.c_str(), sizeof(wifiPass));
  } else {                                   // 否则用编译期的 secrets.h
    strlcpy(wifiSsid, WIFI_SSID, sizeof(wifiSsid));
    strlcpy(wifiPass, WIFI_PASS, sizeof(wifiPass));
  }
}

static void saveWifiCred(const char* ssid, const char* pass) {
  prefs.begin("agentbell", false);
  prefs.putString("wifi_ssid", ssid);
  prefs.putString("wifi_pass", pass);
  prefs.end();
}

static void loadSettings() {
  prefs.begin("agentbell", true);          // 只读
  buzzerEnabled = prefs.getBool("buzz", true);
  vibEnabled    = prefs.getBool("vib",  true);
  dnd           = prefs.getBool("dnd",  false);
  buzzerVol     = prefs.getInt("bvol", 100); if (buzzerVol < 0 || buzzerVol > 100) buzzerVol = 100;
  vibVol        = prefs.getInt("vvol", 100); if (vibVol < 0 || vibVol > 100) vibVol = 100;
  alertTone     = prefs.getInt("tone", 0);
  if (alertTone < 0 || alertTone >= MEL_N) alertTone = 0;
  encReversed   = prefs.getBool("encrev", false);
  encDetents    = prefs.getInt("encdet", 1); if (encDetents < 1 || encDetents > 3) encDetents = 1;
  feedbackMode  = prefs.getInt("fb", 0);     if (feedbackMode < 0 || feedbackMode > 3) feedbackMode = 0;
  fbVol         = prefs.getInt("fbv2", 60);  if (fbVol < 0 || fbVol > 100) fbVol = 60;      // 新键：忽略旧的过低值，用新默认
  fbVibVol      = prefs.getInt("fbvv2", 75); if (fbVibVol < 0 || fbVibVol > 100) fbVibVol = 75;
  notifyWakes   = prefs.getBool("nwake", true);   // 熄屏时来通知是否自动亮屏
  leftStyle     = prefs.getInt("lsty", 0);   if (leftStyle < 0 || leftStyle >= PANE_N) leftStyle = 0;
  rightStyle    = prefs.getInt("rsty", 1);   if (rightStyle < 0 || rightStyle >= PANE_N) rightStyle = 1;
  prefs.end();
  buzzer.setVolume(buzzerVol);
  vibrator.setVolume(vibVol);
}
static void saveSettings() {
  prefs.begin("agentbell", false);
  prefs.putBool("buzz", buzzerEnabled);
  prefs.putBool("vib",  vibEnabled);
  prefs.putBool("dnd",  dnd);
  prefs.putInt("bvol", buzzerVol);
  prefs.putInt("vvol", vibVol);
  prefs.putInt("tone", alertTone);
  prefs.putBool("encrev", encReversed);
  prefs.putInt("encdet", encDetents);
  prefs.putInt("fb", feedbackMode);
  prefs.putInt("fbv2", fbVol);
  prefs.putInt("fbvv2", fbVibVol);
  prefs.putBool("nwake", notifyWakes);
  prefs.putInt("lsty", leftStyle);
  prefs.putInt("rsty", rightStyle);
  prefs.end();
}
#endif

// 亮屏 / 熄屏（SSD1306 省电指令）
static void setScreen(bool on) {
#ifndef INO_SIM
  u8g2.setPowerSave(on ? 0 : 1);
#endif
  screenOff = !on;
  if (on) needRender = true;
}

// ============================================================================
//  关机：外设全部安静 + 深度睡眠（µA 级）。转动旋钮开机（按键 GPIO20 不在
//  C3 的唤醒脚 0-5 范围内，唤不了）。本编码器每档翻转一次 CLK、静止电平在
//  档位间高/低交替，所以唤醒极性睡前按「当前电平的反相」布防：一转即醒。
//  唤醒 = 复位重启，从 setup() 重新来（开机 ~2s，走正常连 WiFi 流程）。
// ============================================================================
static void powerOff() {
#ifdef INO_SIM
  setScreen(false); uiState = UI_IDLE;    // 仿真主机端没有深睡，退化为熄屏
#else
  buzzer.stop();
  vibrator.stop();

  // 告别画面，顺便告诉用户怎么开机
  u8g2.setPowerSave(0);
  u8g2.clearBuffer();
  u8g2.setFont(FONT_CN);
  u8g2.drawUTF8(40, 28, "已关机");
  u8g2.drawUTF8(28, 48, "旋转旋钮开机");
  u8g2.sendBuffer();

#ifndef SIM_DEMO
  saveSettings();                          // 设置落盘（NVS）
  server.stop();
  WiFi.disconnect(true);                   // 断开并关 WiFi 射频
  WiFi.mode(WIFI_OFF);
#endif
  delay(1500);                             // 留人看清提示
  u8g2.setPowerSave(1);                    // OLED 睡眠（~10µA）

  // 蜂鸣（低有效，静默=高）/ 震动（高有效，静默=低）脚锁到「不响」电平再睡：
  // 深睡后数字引脚会悬空，PNP/驱动管一旦误导通，白耗几十 mA 甚至马达空转。
  ledcDetach(BUZZER_PIN);
  ledcDetach(VIB_PIN);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, HIGH);
  pinMode(VIB_PIN, OUTPUT);    digitalWrite(VIB_PIN, VIB_ACTIVE_HIGH ? LOW : HIGH);
  gpio_hold_en((gpio_num_t)BUZZER_PIN);
  gpio_hold_en((gpio_num_t)VIB_PIN);
  gpio_deep_sleep_hold_en();

  // 唤醒布防：CLK 当前电平的反相触发（见函数头注释）
  detachInterrupt(digitalPinToInterrupt(ENC_CLK));
  detachInterrupt(digitalPinToInterrupt(ENC_DT));
  bool clkHigh = (digitalRead(ENC_CLK) == HIGH);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << ENC_CLK,
      clkHigh ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH);
  Serial.println("[power] 深度睡眠，转动旋钮唤醒");
  Serial.flush();
  esp_deep_sleep_start();                  // 不返回
#endif
}

// ============================================================================
//  报警：入队 + 蜂鸣 + 震动
// ============================================================================
void fireAlert(const Note& n) {
#ifndef SIM_DEMO
  if (uiState == UI_SLIDER) saveSettings();       // 通知顶掉滑块/样式选择页前，先把已改的值存盘
#endif
  addNote(n);
  uiState = UI_NOTE;                              // 通知打断任何界面
  // 不传音量覆盖 → 用 setVolume 同步进来的「蜂鸣强度/震动强度」设置值
  if (!dnd && buzzerEnabled) buzzer.play(MELODIES[alertTone].seq, MELODIES[alertTone].len);
  if (!dnd && vibEnabled)    vibrator.trigger(ALERT_VIB, sizeof(ALERT_VIB) / sizeof(ALERT_VIB[0]));
  if (screenOff && notifyWakes) setScreen(true);   // 熄屏时按设置决定是否自动亮屏
  needRender = true;
  Serial.printf("[notify] %s / %s / %s\n", n.computer, n.agent, n.conversation);
}

// ============================================================================
//  OLED 渲染
// ============================================================================
// 收到多久了 → 简短中文
static void timeAgo(unsigned long recv, char* out, size_t n) {
  unsigned long s = (millis() - recv) / 1000UL;
  if (s < 5)          snprintf(out, n, "刚刚");
  else if (s < 60)    snprintf(out, n, "%lus前", s);
  else if (s < 3600)  snprintf(out, n, "%lum前", s / 60);
  else                snprintf(out, n, "%luh前", s / 3600);
}

// 响铃声波：从 (x,y) 向右画 n 圈同心弧（动画用）+ 中心点。
static void drawWaves(int x, int y, int n, uint8_t color) {
  u8g2.setDrawColor(color);
  u8g2.drawDisc(x, y, 1);
  for (int i = 1; i <= n && i <= 3; i++)
    u8g2.drawCircle(x, y, i * 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  u8g2.setDrawColor(1);
}

// ============================================================================
//  待机屏模块：屏幕分左右两个 64×64 半屏，各自从模块池任选（见 PANE_NAMES）。
//  每个模块画在 [x0, x0+64) × [0,64) 内，x0=0 是左半、x0=64 是右半。
// ============================================================================
static const int PANE = 64;    // 半屏边长 = 屏高

static const char* WEEK_CN[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

// 星空：固定星点都落在半屏 64×64 内、避开中央（8..55），部分随相位闪烁成小十字
static void drawStars(int x0, float ph) {
  static const uint8_t sx[] = {4, 16, 58, 60, 3, 50, 26, 44};
  static const uint8_t sy[] = {16, 4, 10, 50, 44, 60, 60, 4};
  int k = (int)ph;
  for (int i = 0; i < 8; i++) {
    int x = x0 + sx[i];
    u8g2.drawPixel(x, sy[i]);
    if ((k + i) % 3 == 0) {                        // 闪烁：变成小十字
      u8g2.drawPixel(x - 1, sy[i]); u8g2.drawPixel(x + 1, sy[i]);
      u8g2.drawPixel(x, sy[i] - 1); u8g2.drawPixel(x, sy[i] + 1);
    }
  }
}

// 取本地时间：SNTP 同步前（时钟还停在 1970 附近）返回 false
static bool clockNow(struct tm& t) {
  time_t now = time(nullptr);
  if (now < 1609459200) return false;    // < 2021-01-01 视为未同步
  t = *localtime(&now);
  return true;
}

static bool netOnline() {
#ifndef SIM_DEMO
  return WiFi.status() == WL_CONNECTED;
#else
  return true;
#endif
}

// —— 模块 0：宇航员 —— 闪烁星空 + 绕轴自转太空人（预渲染帧，astronaut_frames.h）
static void paneAstro(int x0) {
  drawStars(x0, millis() * 0.0016f);
  int fi = (int)((millis() / 80) % ASTRO_N);       // 逐帧自转
  u8g2.drawXBMP(x0 + (PANE - ASTRO_W) / 2, (64 - ASTRO_H) / 2, ASTRO_W, ASTRO_H, ASTRO[fi]);
}

// —— 模块 1：数字时钟 —— 大字 HH:MM + 日期 + 星期（未同步/离线显示状态文字）
static void paneClock(int x0) {
  int cx = x0 + PANE / 2;
  struct tm t;
  char buf[16];
  bool has = clockNow(t);
  if (has) snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  else     strlcpy(buf, "--:--", sizeof(buf));
  u8g2.setFont(FONT_CLOCK);
  u8g2.drawStr(cx - u8g2.getStrWidth(buf) / 2, 30, buf);

  u8g2.setFont(FONT_CN);
  if (has) {
    snprintf(buf, sizeof(buf), "%d月%d日", t.tm_mon + 1, t.tm_mday);
    u8g2.drawUTF8(cx - u8g2.getUTF8Width(buf) / 2, 48, buf);
    const char* wk = netOnline() ? WEEK_CN[t.tm_wday] : "WiFi断开";   // 断网时占用星期行提示
    u8g2.drawUTF8(cx - u8g2.getUTF8Width(wk) / 2, 62, wk);
  } else {
    const char* s = netOnline() ? "时间同步中" : "WiFi断开";
    u8g2.drawUTF8(cx - u8g2.getUTF8Width(s) / 2, 50, s);
  }
}

// —— 模块 2：模拟表盘 —— 圆形表盘 + 时/分/秒针（未同步时停在 10:08 经典表姿）
static void paneAnalog(int x0) {
  int cx = x0 + PANE / 2, cy = 32, r = 30;
  u8g2.drawCircle(cx, cy, r);
  for (int i = 0; i < 12; i++) {                    // 12 个刻度：整点长刻度、其余点
    float a = i * (PI / 6);
    float s = sinf(a), c = cosf(a);
    if (i % 3 == 0) u8g2.drawLine(cx + (int)(s * (r - 1)), cy - (int)(c * (r - 1)),
                                  cx + (int)(s * (r - 5)), cy - (int)(c * (r - 5)));
    else            u8g2.drawPixel(cx + (int)(s * (r - 3)), cy - (int)(c * (r - 3)));
  }
  struct tm t;
  int hh = 10, mm = 8, ss = 0;                      // 未同步：经典 10:08
  if (clockNow(t)) { hh = t.tm_hour; mm = t.tm_min; ss = t.tm_sec; }
  float am = (mm + ss / 60.0f) * (PI / 30);         // 分针角
  float ah = (hh % 12 + mm / 60.0f) * (PI / 6);     // 时针角
  float as = ss * (PI / 30);                        // 秒针角
  u8g2.drawLine(cx, cy, cx + (int)(sinf(ah) * 15), cy - (int)(cosf(ah) * 15));
  u8g2.drawLine(cx, cy, cx + (int)(sinf(am) * 23), cy - (int)(cosf(am) * 23));
  u8g2.drawLine(cx, cy, cx + (int)(sinf(as) * 27), cy - (int)(cosf(as) * 27));
  u8g2.drawDisc(cx, cy, 2);
}

// —— 模块 3：竖排大钟 —— 上「时」下「分」超大数字，中间冒号点每秒闪烁
static void paneBigTime(int x0) {
  int cx = x0 + PANE / 2;
  struct tm t;
  char hh[4] = "--", mm[4] = "--";
  bool has = clockNow(t);
  if (has) {
    snprintf(hh, sizeof(hh), "%02d", t.tm_hour);
    snprintf(mm, sizeof(mm), "%02d", t.tm_min);
  }
  u8g2.setFont(FONT_CLOCK_XL);
  u8g2.drawStr(cx - u8g2.getStrWidth(hh) / 2, 28, hh);
  u8g2.drawStr(cx - u8g2.getStrWidth(mm) / 2, 62, mm);
  if (!has || (millis() / 1000) % 2 == 0) {         // 冒号点（横放）：秒闪
    u8g2.drawBox(cx - 6, 30, 2, 2);
    u8g2.drawBox(cx + 4, 30, 2, 2);
  }
}

// —— 模块 4：日历 —— 反白月份栏 + 超大日期数字 + 星期
static void paneCalendar(int x0) {
  struct tm t;
  bool has = clockNow(t);
  int cx = x0 + PANE / 2;
  char buf[16];

  u8g2.drawRBox(x0 + 3, 1, PANE - 6, 15, 2);        // 反白月份栏（日历本头）
  u8g2.setDrawColor(0);
  u8g2.drawPixel(x0 + 12, 3); u8g2.drawPixel(x0 + PANE - 13, 3);   // 装订孔（栏内挖黑点）
  u8g2.setFont(FONT_CN);
  if (has) snprintf(buf, sizeof(buf), "%d年%d月", t.tm_year + 1900, t.tm_mon + 1);
  else     strlcpy(buf, "日历", sizeof(buf));
  u8g2.drawUTF8(cx - u8g2.getUTF8Width(buf) / 2, 13, buf);
  u8g2.setDrawColor(1);

  if (has) snprintf(buf, sizeof(buf), "%d", t.tm_mday);
  else     strlcpy(buf, "--", sizeof(buf));
  u8g2.setFont(FONT_CLOCK_XL);
  u8g2.drawStr(cx - u8g2.getStrWidth(buf) / 2, 46, buf);

  u8g2.setFont(FONT_CN);
  const char* wk = has ? WEEK_CN[t.tm_wday] : "--";
  u8g2.drawUTF8(cx - u8g2.getUTF8Width(wk) / 2, 62, wk);
}

// —— 模块 5：信息面板 —— 未读 / WiFi 信号 / IP 末段 / 运行时长
static void paneInfo(int x0) {
  char l[24];
  u8g2.setFont(FONT_CN);
  snprintf(l, sizeof(l), "未读 %d", unread);
  u8g2.drawUTF8(x0 + 4, 13, l);
#ifndef SIM_DEMO
  if (netOnline()) {
    snprintf(l, sizeof(l), "WiFi %d", (int)WiFi.RSSI());
    u8g2.drawUTF8(x0 + 4, 29, l);
    snprintf(l, sizeof(l), "IP .%d", (int)WiFi.localIP()[3]);
    u8g2.drawUTF8(x0 + 4, 45, l);
  } else {
    u8g2.drawUTF8(x0 + 4, 29, "WiFi 断开");
    u8g2.drawUTF8(x0 + 4, 45, "IP --");
  }
#else
  u8g2.drawUTF8(x0 + 4, 29, "WiFi -60");
  u8g2.drawUTF8(x0 + 4, 45, "IP .123");
#endif
  unsigned long up = millis() / 60000UL;            // 分钟
  if (up < 60) snprintf(l, sizeof(l), "开机 %lum", up);
  else         snprintf(l, sizeof(l), "开机 %luh%lum", up / 60, up % 60);
  u8g2.drawUTF8(x0 + 4, 61, l);
}

// —— 模块 6：雷达扫描 —— 同心圆 + 十字准线 + 旋转扫描线，扫过目标点时亮起
static void paneRadar(int x0) {
  int cx = x0 + PANE / 2, cy = 32;
  u8g2.drawCircle(cx, cy, 10);
  u8g2.drawCircle(cx, cy, 20);
  u8g2.drawCircle(cx, cy, 30);
  for (int d = 4; d <= 30; d += 5) {                // 十字准线（点线）
    u8g2.drawPixel(cx + d, cy); u8g2.drawPixel(cx - d, cy);
    u8g2.drawPixel(cx, cy + d); u8g2.drawPixel(cx, cy - d);
  }
  float th = fmodf(millis() * 0.0025f, 2 * PI);     // 扫描角（约 2.5s 一圈）
  u8g2.drawLine(cx, cy, cx + (int)(cosf(th) * 29), cy + (int)(sinf(th) * 29));
  for (int k = 1; k <= 3; k++) {                    // 拖影：三条渐稀点线
    float a = th - k * 0.12f;
    for (int rr = 6 + k * 3; rr <= 29; rr += 4 + k * 2)
      u8g2.drawPixel(cx + (int)(cosf(a) * rr), cy + (int)(sinf(a) * rr));
  }
  static const int8_t bx[] = {14, -18, 6};          // 三个目标点（相对圆心）
  static const int8_t by[] = {-9, 7, 21};
  for (int i = 0; i < 3; i++) {
    float ba = atan2f((float)by[i], (float)bx[i]);
    float diff = fmodf(th - ba + 6 * PI, 2 * PI);   // 扫描线刚扫过多久
    if (diff < 1.2f) u8g2.drawDisc(cx + bx[i], cy + by[i], diff < 0.5f ? 2 : 1);
    else             u8g2.drawPixel(cx + bx[i], cy + by[i]);
  }
}

// —— 模块 7：流星夜空 —— 闪烁星空 + 三颗错峰划过的流星（头亮点 + 斜尾迹）
static void paneMeteor(int x0) {
  drawStars(x0, millis() * 0.0016f);
  static const uint16_t period[] = {2600, 3800, 3100};   // 各自周期错开
  static const uint16_t offset[] = {0, 1300, 2200};
  static const uint8_t  startX[] = {58, 44, 60};
  static const uint8_t  startY[] = {2, 0, 18};
  for (int i = 0; i < 3; i++) {
    float ph = ((millis() + offset[i]) % period[i]) / (float)period[i];
    if (ph > 0.55f) continue;                       // 后半周期休息，别一直满屏流星
    float tt = ph / 0.55f;
    int hx = x0 + startX[i] - (int)(tt * 52);       // 向左下划
    int hy = startY[i] + (int)(tt * 44);
    if (hx < x0 + 1 || hy > 62) continue;
    u8g2.drawLine(hx, hy, hx + 7, hy - 6);          // 尾迹（指向来向）
    u8g2.drawPixel(hx + 10, hy - 8);                // 尾迹末端余烬
    u8g2.drawDisc(hx, hy, 1);                       // 流星头
  }
}

// 待机屏 = 左右两个半屏模块各自渲染（不画分隔线，模块自身留白即边界）
static void renderIdlePanes() {
  PANE_FNS[leftStyle](0);
  PANE_FNS[rightStyle](PANE);
  if (dnd) { u8g2.drawCircle(6, 7, 4); u8g2.drawLine(3, 4, 9, 10); }   // 全局左上角静音 ⊘
}

// 通知屏（重设计 2026-08-03）：
//  ┌──────────────────────────┐ 反白顶栏 18px：智能体名垂直居中（中文名自动换
//  │▓ Claude        )))  2/5 ▓│ 中文字体），右侧响铃时=声波动画，静止时=位置 k/N
//  │ [PC] 书房台式             │ 图标行×2：电脑名 / 对话名（超宽省略号截断）
//  │ [💬] agent-bell           │
//  │ ·············(点线)······ │
//  │ 已完成并验证编译…    3m前 │ 消息（截断让位时间）+ 右对齐时间
//  └──────────────────────────┘
static const int NBAR_H = 18;    // 顶栏高

// 按像素宽截断 UTF-8 串，放不下时以"..."结尾（调用前须先 setFont）
static void fitUTF8(const char* s, int maxw, char* out, size_t n) {
  if ((int)u8g2.getUTF8Width(s) <= maxw) { strlcpy(out, s, n); return; }
  int ellw = u8g2.getUTF8Width("...");
  char tmp[120];
  size_t len = 0, keep = 0;
  while (s[len] && len < sizeof(tmp) - 5 && len < n - 5) {
    unsigned char c = (unsigned char)s[len];
    size_t cl = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
    memcpy(tmp + len, s + len, cl); tmp[len + cl] = 0;
    if ((int)u8g2.getUTF8Width(tmp) + ellw > maxw) break;
    len += cl; keep = len;
  }
  memcpy(out, s, keep); strcpy(out + keep, "...");
}

// 行首小图标（12×10 框内手绘，不引图标字体）：显示器 / 对话气泡
static void iconPC(int x, int y) {
  u8g2.drawFrame(x, y, 12, 8);
  u8g2.drawHLine(x + 3, y + 9, 6);             // 底座
}
static void iconChat(int x, int y) {
  u8g2.drawRFrame(x, y, 12, 8, 2);
  u8g2.drawPixel(x + 3, y + 8);                // 气泡尾
  u8g2.drawPixel(x + 2, y + 9);
}

static void renderNote(const Note* n) {
  char line[112];
  bool ringing = buzzer.busy() || vibrator.busy();

  // —— 反白顶栏：智能体名（垂直居中，防削顶；中文名回退中文字体）——
  u8g2.drawBox(0, 0, 128, NBAR_H);
  u8g2.setDrawColor(0);
  const char* name = n->agent[0] ? n->agent : "Agent";
  bool cjk = false;
  for (const char* p = name; *p; p++) if ((unsigned char)*p >= 0x80) { cjk = true; break; }
  u8g2.setFont(cjk ? FONT_CN : FONT_BIG);      // FONT_BIG 无中文字模，中文名整段不显示 → 回退
  u8g2.setFontPosCenter();                     // 以栏中线对齐，任何字体都不越界
  fitUTF8(name, 92, line, sizeof(line));
  u8g2.drawUTF8(4, NBAR_H / 2, line);
  // 顶栏右侧：响铃时声波扩散动画；静止且有多条时显示位置 k/N
  if (ringing) {
    int waves = 1 + (int)((millis() / 180) % 3);
    drawWaves(112, NBAR_H / 2, waves, 0);
  } else if (ringCount > 1) {
    u8g2.setFont(FONT_CN);
    snprintf(line, sizeof(line), "%d/%d", (viewOffset < 0 ? 0 : viewOffset) + 1, ringCount);
    u8g2.drawUTF8(125 - u8g2.getUTF8Width(line), NBAR_H / 2, line);
  }
  u8g2.setFontPosBaseline();
  u8g2.setDrawColor(1);

  // —— 信息两行：图标 + 值（全宽可用，超宽省略）——
  u8g2.setFont(FONT_CN);
  iconPC(3, 20);
  fitUTF8(n->computer[0] ? n->computer : "-", 108, line, sizeof(line));
  u8g2.drawUTF8(19, 30, line);
  iconChat(3, 34);
  fitUTF8(n->conversation[0] ? n->conversation : "-", 108, line, sizeof(line));
  u8g2.drawUTF8(19, 44, line);

  // —— 点线分隔 + 底行：消息（截断让位时间）+ 右对齐时间 ——
  for (int x = 0; x < 128; x += 3) u8g2.drawPixel(x, 48);
  char ago[16]; timeAgo(n->recv, ago, sizeof(ago));
  int agoW = u8g2.getUTF8Width(ago);
  u8g2.drawUTF8(126 - agoW, 61, ago);
  if (n->message[0]) {
    fitUTF8(n->message, 126 - agoW - 8, line, sizeof(line));
    u8g2.drawUTF8(3, 61, line);
  }
}

// ============================================================================
//  丝滑菜单渲染（缓动光标 + 平滑滚动 + XOR 反白高亮条，借鉴 OLED_UI 观感）
// ============================================================================
// 主菜单：常用在前，按「勿扰 → 通知一组 → 反馈一组 → 待机屏 → 旋钮 → 电源 → 系统」分组排序。
// 「通知」=收到消息时的响铃/震动；「反馈」=转动/按下旋钮时的提示音/轻震，两组独立。
// 勿扰和旋钮方向是开关项：短按直接切换，名字里带当前状态。
enum MainId { MI_DND, MI_TONE, MI_NVOL, MI_NVIB, MI_FBMODE, MI_FBVOL, MI_FBVIB,
              MI_LSTYLE, MI_RSTYLE, MI_ENCDIR, MI_ENCSENS,
              MI_SCREENOFF, MI_POWEROFF, MI_REPROV, MI_SIMNOTE, MI_STATUS, MAIN_N_ };
static const int MAIN_N = MAIN_N_;
static const char* mainName(int i) {
  static char buf[24];
  switch (i) {
    case MI_DND:     snprintf(buf, sizeof(buf), "勿扰 · %s", dnd ? "开" : "关"); return buf;
    case MI_TONE:    return "提示音";
    case MI_NVOL:    return "通知音量";
    case MI_NVIB:    return "通知震动";
    case MI_FBMODE:  return "反馈方式";
    case MI_FBVOL:   return "反馈音量";
    case MI_FBVIB:   return "反馈震动";
    case MI_LSTYLE:  return "左屏样式";
    case MI_RSTYLE:  return "右屏样式";
    case MI_ENCDIR:  snprintf(buf, sizeof(buf), "旋钮方向 · %s", encReversed ? "反向" : "正向"); return buf;
    case MI_ENCSENS: return "旋钮灵敏度";
    case MI_SCREENOFF: return "熄屏";
    case MI_POWEROFF:  return "关机";
    case MI_REPROV:    return "重新配网";
    case MI_SIMNOTE:   return "模拟通知";
    default:           return "设备状态";
  }
}
// 选项子列表（提示音 / 反馈方式 / 旋钮灵敏度）——都用旋钮丝滑选择
static const char* FEEDBACK_OPTS[] = {"声音+震动", "只震动", "只声音", "无"};
static const char* ENCSENS_OPTS[]  = {"1 档动一步", "2 档动一步", "3 档动一步"};
static int subCount() { return (curSub == SUB_MELODY) ? MEL_N : 0; }
static const char* subName(int i) { return (curSub == SUB_MELODY) ? MELODIES[i].name : ""; }

static const int ROW = 16, VIS = 4;   // 行高 / 可见行数（16×4=64）
static void renderList(int n, const char* (*name)(int), int sel, ListAnim& a) {
  int curTop = (int)lroundf(a.top), want = curTop;
  if (sel < curTop) want = sel;
  else if (sel >= curTop + VIS) want = sel - VIS + 1;
  int maxTop = n > VIS ? n - VIS : 0;
  if (want < 0) want = 0; if (want > maxTop) want = maxTop;
  a.top += (want - a.top) * 0.30f;
  a.sel += (sel  - a.sel) * 0.30f;
  if (fabsf(a.top - want) < 0.02f) a.top = want;
  if (fabsf(a.sel - sel)  < 0.02f) a.sel = sel;

  u8g2.setFont(FONT_CN);
  u8g2.setDrawColor(1);
  for (int i = 0; i < n; i++) {
    int y = (int)((i - a.top) * ROW);
    if (y <= -ROW || y >= 64) continue;
    u8g2.drawUTF8(8, y + 12, name(i));
  }
  int by = (int)((a.sel - a.top) * ROW);      // 高亮条位置（缓动）
  u8g2.setDrawColor(2);                        // XOR：条下文字自动反白，过渡也平滑
  u8g2.drawRBox(2, by + 1, 122, ROW - 2, 3);
  u8g2.setDrawColor(1);
  if (n > VIS) {                               // 右侧滚动条
    int th = 64 * VIS / n, ty = 64 * (int)lroundf(a.top) / n;
    u8g2.drawBox(126, ty, 2, th);
  }
}

static void renderMenu() {
  if (curSub == SUB_NONE) renderList(MAIN_N, mainName, mainSel, mainAnim);
  else                    renderList(subCount(), subName, subSel, subAnim);
}

// —— 滑块设置项：元数据 / 取值 / 赋值 ——
struct SetMeta { const char* title; bool numeric; const char* const* opts; int nopts; };
static SetMeta setMeta(SetId id) {
  switch (id) {
    case SET_BUZVOL: return {"通知音量", true,  nullptr, 0};
    case SET_VIBVOL: return {"通知震动", true,  nullptr, 0};
    case SET_FBVOL:  return {"反馈音量", true,  nullptr, 0};
    case SET_FBVIB:  return {"反馈震动", true,  nullptr, 0};
    case SET_FBMODE: return {"反馈方式", false, FEEDBACK_OPTS, 4};
    case SET_LSTYLE: return {"左屏样式", false, PANE_NAMES, PANE_N};
    case SET_RSTYLE: return {"右屏样式", false, PANE_NAMES, PANE_N};
    default:         return {"旋钮灵敏度", false, ENCSENS_OPTS, 3};  // SET_ENCSENS
  }
}
static int setGet(SetId id) {
  switch (id) {
    case SET_BUZVOL: return buzzerVol;   case SET_VIBVOL: return vibVol;
    case SET_FBVOL:  return fbVol;       case SET_FBVIB:  return fbVibVol;
    case SET_FBMODE: return feedbackMode;
    case SET_LSTYLE: return leftStyle;
    case SET_RSTYLE: return rightStyle;
    default:         return encDetents - 1;         // SET_ENCSENS
  }
}
static void setPut(SetId id, int v) {
  switch (id) {
    case SET_BUZVOL: buzzerVol = v; buzzer.setVolume(v); break;
    case SET_VIBVOL: vibVol = v; vibrator.setVolume(v); break;
    case SET_FBVOL:  fbVol = v; break;
    case SET_FBVIB:  fbVibVol = v; break;
    case SET_FBMODE: feedbackMode = v; break;
    case SET_LSTYLE: leftStyle = v; break;           // 立即生效 → 选择器背景实时预览
    case SET_RSTYLE: rightStyle = v; break;
    default:         encDetents = v + 1; break;      // SET_ENCSENS
  }
}

// 半屏模块选择器：背景就是实时渲染的待机屏（改哪边立即生效，所见即所得），
// 正在调的半屏套 XOR 高亮框，底部药丸显示模块名 + 序号。
static void renderPanePicker() {
  renderIdlePanes();
  bool left = (curSet == SET_LSTYLE);
  int x0 = left ? 0 : PANE;
  int cur = left ? leftStyle : rightStyle;

  u8g2.setDrawColor(2);                             // XOR 双层框：任何模块背景上都可见
  u8g2.drawFrame(x0, 0, PANE, 64);
  u8g2.drawFrame(x0 + 1, 1, PANE - 2, 62);
  u8g2.setDrawColor(1);

  char pill[28];
  snprintf(pill, sizeof(pill), "%s %d/%d", PANE_NAMES[cur], cur + 1, PANE_N);
  u8g2.setFont(FONT_CN);
  int w = u8g2.getUTF8Width(pill) + 10;
  int px = x0 + (PANE - w) / 2;                     // 药丸贴在所调半屏底部
  if (px < 0) px = 0; if (px + w > 128) px = 128 - w;
  u8g2.drawRBox(px, 50, w, 14, 4);
  u8g2.setDrawColor(0);
  u8g2.drawUTF8(px + 5, 61, pill);
  u8g2.setDrawColor(1);
}

// 丝滑水平滑块：连续值(0-100)填充+thumb；离散值分段吸附点+缓动滑块+当前标签
static void renderSlider() {
  if (curSet == SET_LSTYLE || curSet == SET_RSTYLE) { renderPanePicker(); return; }
  SetMeta m = setMeta(curSet);
  int val = setGet(curSet);
  float frac; char big[20];
  if (m.numeric) { frac = val / 100.0f; snprintf(big, sizeof(big), "%d", val); }
  else           { frac = (m.nopts > 1) ? (float)val / (m.nopts - 1) : 0; strlcpy(big, m.opts[val], sizeof(big)); }
  sliderAnim += (frac - sliderAnim) * 0.30f;               // 缓动（吸附到目标）
  if (fabsf(frac - sliderAnim) < 0.004f) sliderAnim = frac;

  u8g2.setFont(FONT_CN);
  u8g2.drawUTF8(6, 14, m.title);                            // 标题
  if (m.numeric) { u8g2.setFont(FONT_BIG); u8g2.drawUTF8(6, 41, big); }   // 大数字
  else           { u8g2.drawUTF8(6, 38, big); }                          // 当前选项名

  int tx = 8, ty = 50, tw = 112, th = 10;                   // 轨道
  int usable = tw - 8;
  u8g2.drawRFrame(tx, ty, tw, th, 5);
  if (m.numeric) {
    int fill = (int)(sliderAnim * usable);
    u8g2.drawBox(tx + 3, ty + 3, fill + 1, th - 6);         // 已填充（方角，避免小宽度圆角瑕疵）
  } else {
    for (int i = 0; i < m.nopts; i++) {                     // 吸附点
      int cx = tx + 4 + (int)((float)i / (m.nopts - 1) * usable);
      u8g2.drawDisc(cx, ty + th / 2, 1);
    }
  }
  int kx = tx + 4 + (int)(sliderAnim * usable);             // 滑块 thumb（缓动）
  u8g2.drawDisc(kx, ty + th / 2, 4);
}

static void renderPopup() {
  u8g2.drawBox(0, 0, 128, 15); u8g2.setDrawColor(0);
  u8g2.setFont(FONT_CN); u8g2.drawUTF8(3, 12, "设备状态"); u8g2.setDrawColor(1);
  char l[44];
#ifndef SIM_DEMO
  snprintf(l, sizeof(l), "IP %s", WiFi.localIP().toString().c_str());  u8g2.drawUTF8(3, 32, l);
  snprintf(l, sizeof(l), "信号 %d dBm", (int)WiFi.RSSI());             u8g2.drawUTF8(3, 46, l);
  snprintf(l, sizeof(l), "运行 %lu 秒", millis() / 1000);              u8g2.drawUTF8(3, 60, l);
#else
  u8g2.setFont(FONT_CN); u8g2.drawUTF8(3, 40, "SIM 预览");
#endif
}

void render() {
  if (screenOff) return;                 // 熄屏时不绘制（面板已 setPowerSave 关闭）
  u8g2.clearBuffer();
  switch (uiState) {
    case UI_MENU:   renderMenu();   break;
    case UI_SLIDER: renderSlider(); break;
    case UI_POPUP:  renderPopup();  break;
    case UI_NOTE: { Note* n = noteByOffset(viewOffset >= 0 ? viewOffset : 0);
                    if (n) renderNote(n); else renderIdlePanes(); } break;
    default:        renderIdlePanes(); break;
  }
  u8g2.sendBuffer();
}

// ============================================================================
//  操作反馈
// ============================================================================
// 操作反馈：按 feedbackMode 给声音/震动，用独立的 fbVol/fbVibVol（与通知音量分开）
static void opFeedback() {
  if (feedbackMode == 0 || feedbackMode == 1) vibrator.trigger(TAP_VIB, 1, fbVibVol);
  if (feedbackMode == 0 || feedbackMode == 2) buzzer.play(TAP_TONES, 1, fbVol);
}

// ============================================================================
//  编码器的 UI 输入路由（丝滑菜单状态机）：短按=进入/确认，长按=返回
// ============================================================================
static void enterSub(SubMenu s, int sel) {   // 进入提示音子列表
  curSub = s; subSel = sel;
  subAnim.sel = sel; subAnim.top = sel;
  melPreviewed = -1; melSettleAt = millis();
}

static void enterSlider(SetId id) {          // 进入滑块设置页（thumb 直接对齐当前值，不飞入）
  curSet = id; uiState = UI_SLIDER;
  SetMeta m = setMeta(id); int v = setGet(id);
  sliderAnim = m.numeric ? v / 100.0f : (m.nopts > 1 ? (float)v / (m.nopts - 1) : 0);
}

static void menuActivate() {
  if (curSub == SUB_MELODY) {                 // 提示音子列表：选用
    alertTone = subSel;
    buzzer.play(MELODIES[alertTone].seq, MELODIES[alertTone].len);   // 按通知音量试听（所听即所得）
#ifndef SIM_DEMO
    saveSettings();
#endif
    curSub = SUB_NONE;
    return;
  }
  switch (mainSel) {                           // 主菜单（枚举分发，顺序见 MainId）
    case MI_DND:                                       // 勿扰：短按直接切换，行内显示状态
      dnd = !dnd;
#ifndef SIM_DEMO
      saveSettings();
#endif
      needRender = true; break;
    case MI_TONE:    enterSub(SUB_MELODY, alertTone); break;
    case MI_NVOL:    enterSlider(SET_BUZVOL);  break;
    case MI_NVIB:    enterSlider(SET_VIBVOL);  break;
    case MI_FBMODE:  enterSlider(SET_FBMODE);  break;
    case MI_FBVOL:   enterSlider(SET_FBVOL);   break;
    case MI_FBVIB:   enterSlider(SET_FBVIB);   break;
    case MI_LSTYLE:  enterSlider(SET_LSTYLE);  break;
    case MI_RSTYLE:  enterSlider(SET_RSTYLE);  break;
    case MI_ENCDIR:                                    // 旋钮方向：短按翻转（不进滑块——
      encReversed = !encReversed;                      // 靠「转」选方向会跟自己打架）
#ifndef SIM_DEMO
      saveSettings();
#endif
      needRender = true; break;
    case MI_ENCSENS: enterSlider(SET_ENCSENS); break;
    case MI_SCREENOFF: setScreen(false); uiState = UI_IDLE; break;
    case MI_POWEROFF:  powerOff(); break;              // 深睡，转旋钮开机；真机不返回
    case MI_REPROV:                                    // 重新配网：记一次性标记重启，开机直进热点
#ifndef SIM_DEMO
      // 不在运行中切 AP：WebServer 路由无法注销，正常模式的 "/" 会盖住配网页
      prefs.begin("agentbell", false);
      prefs.putBool("force_ap", true);
      prefs.end();
      u8g2.clearBuffer();
      u8g2.setFont(FONT_CN);
      u8g2.drawUTF8(16, 38, "正在进入配网…");
      u8g2.sendBuffer();
      delay(400);
      ESP.restart();                                   // 不返回
#endif
      break;
    case MI_SIMNOTE: {
      Note n{};
      strlcpy(n.computer, "本机测试",   sizeof(n.computer));
      strlcpy(n.agent, "Claude",        sizeof(n.agent));
      strlcpy(n.conversation, "对话演示", sizeof(n.conversation));
      strlcpy(n.message, "这是一条模拟通知", sizeof(n.message));
      n.recv = millis();
      fireAlert(n);
    } break;
    case MI_STATUS: uiState = UI_POPUP; break;
  }
}

static void handleInput() {
  int step = encSteps();
  int btn  = encButton();
  if (step == 0 && btn == 0) return;

  lastInput = millis();
  if (uiState != UI_SLIDER) opFeedback();   // 旋钮转/按反馈（滑块页有自己的即时试声）
  if (screenOff) { setScreen(true); return; }    // 熄屏时任意输入先唤醒

  switch (uiState) {
    case UI_IDLE:
      if (step != 0 || btn == 1) { uiState = UI_MENU; curSub = SUB_NONE; mainAnim.sel = mainSel; }
      break;
    case UI_NOTE:
      if (btn) { unread = 0; viewOffset = -1; uiState = UI_IDLE; }
      else if (step) {
        viewOffset += (step > 0 ? 1 : -1);
        if (viewOffset < 0) viewOffset = 0;
        if (viewOffset > ringCount - 1) viewOffset = ringCount - 1;
      }
      break;
    case UI_MENU: {
      if (step) {
        int  n   = (curSub == SUB_NONE) ? MAIN_N : subCount();
        int& sel = (curSub == SUB_NONE) ? mainSel : subSel;
        sel += step; if (sel < 0) sel = 0; if (sel > n - 1) sel = n - 1;
        if (curSub == SUB_MELODY) melSettleAt = millis();
      }
      if (btn == 1) menuActivate();
      if (btn == 2) { if (curSub != SUB_NONE) curSub = SUB_NONE; else uiState = UI_IDLE; }
    } break;
    case UI_SLIDER: {
      if (step) {
        SetMeta m = setMeta(curSet);
        int v = setGet(curSet);
        if (m.numeric) { v += step * 5; if (v < 0) v = 0; if (v > 100) v = 100; }
        else           { v += step; if (v < 0) v = 0; if (v > m.nopts - 1) v = m.nopts - 1; }
        setPut(curSet, v);
        switch (curSet) {                         // 即时预览：调什么就试什么
          case SET_BUZVOL: buzzer.play(FB_TONES, 1, buzzerVol); break;
          case SET_VIBVOL: vibrator.trigger(FB_VIB, 1, vibVol); break;
          case SET_FBVOL:  buzzer.play(TAP_TONES, 1, fbVol);    break;
          case SET_FBVIB:  vibrator.trigger(TAP_VIB, 1, fbVibVol); break;
          default:         opFeedback();           break;      // 离散项用统一操作反馈
        }
      }
      if (btn) {
#ifndef SIM_DEMO
        saveSettings();
#endif
        uiState = UI_MENU;
      }
    } break;
    case UI_POPUP:
      if (btn || step) uiState = UI_MENU;
      break;
  }
}

// ============================================================================
//  HTTP 处理
// ============================================================================
#ifndef SIM_DEMO
static void argTo(const char* key, const char* dflt, char* out, size_t n) {
  String v = server.hasArg(key) ? server.arg(key) : String(dflt);
  strlcpy(out, v.c_str(), n);
}

static void handleNotify() {   // 唯一保留的写入端点：给电脑侧 hook 用（所有设置已移到设备菜单）
  Note n{};
  argTo("computer",     "?",     n.computer,     sizeof(n.computer));
  argTo("agent",        "Agent", n.agent,        sizeof(n.agent));
  argTo("conversation", "-",     n.conversation, sizeof(n.conversation));
  argTo("message",      "",      n.message,      sizeof(n.message));
  n.recv = millis();
  fireAlert(n);
  server.send(200, "text/plain; charset=utf-8", "ok");
}

// ============================================================================
//  网页设置控制台：静态页存 PROGMEM（不占 RAM），JS 用 fetch 调 /api/settings
//  读写全部设置。手机/电脑连同一 WiFi 输设备 IP 即可用，与旋钮/桥接改同一份设置。
//  设计契约（与 tools/agent-bell-bridge 桌面端共享，全项目规范见 DESIGN.md）：
//  THESIS: 网页端与桌面端是同一台仪器的两块面板（TE OP-1 铝面板语言），拒绝暗色 dashboard 默认
//  OWN-WORLD: 机身 #E4E3DF + 唯一强调橙 #F04E00 + 全页唯一深色块=OLED 仿真屏 #141412；
//             推子/拨钮/‹›步进器/键帽按钮 与桌面端 te_widgets 同构
//  STORY: 用户拧的是设备里同一份设置；改动即时生效并存 flash
//  FIRST VIEWPORT: 字标+铭牌+状态LED → OLED 仿真屏（视觉锚点）→ 分区控件面板
//  FORM: Operate——表达不遮蔽任务；对比度≥4.5:1；:focus-visible 橙框；触屏目标≥44px
//  本地预览：python tools/console-preview/serve.py（mock API，免烧录迭代）
// ============================================================================
static const char CONSOLE_HTML[] PROGMEM = R"HTML(<!doctype html><meta charset=utf-8>
<meta name=viewport content='width=device-width,initial-scale=1'>
<meta name=color-scheme content=light>
<title>AgentBell 控制台</title><style>
:root{--ch:#E4E3DF;--ln:#C6C5C0;--ink:#1D1C19;--mu:#605F58;--acc:#F04E00;--acd:#C84100;
--key:#F6F5F2;--kdn:#DDDCD7;--ke:#AFAEA8;--db:#141412;--de:#2A2A26;--df:#EDEBE3;--dd:#807E75;
--mono:ui-monospace,'Cascadia Mono',Consolas,'Courier New',monospace}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,'Segoe UI','Microsoft YaHei UI',sans-serif;background:var(--ch);
color:var(--ink);max-width:480px;margin:0 auto;padding:14px 18px 44px;-webkit-tap-highlight-color:transparent}
header{display:flex;align-items:center;gap:9px;margin:8px 0 12px}
h1{font:700 14px/1 'Segoe UI',system-ui,sans-serif;letter-spacing:.34em;margin:0}
header small{font:12px var(--mono);color:var(--mu)}
#led{width:9px;height:9px;border-radius:50%;background:#E8A33D;margin-left:auto;flex:none}
#oled{position:relative;background:var(--db);border:1px solid var(--de);border-radius:8px;
padding:13px 15px;font-family:var(--mono);overflow:hidden;box-shadow:inset 0 0 22px rgba(0,0,0,.55)}
#oled .px{color:var(--df);text-shadow:0 0 6px rgba(237,235,227,.35)}
#oled::after{content:'';position:absolute;inset:0;pointer-events:none;
background:repeating-linear-gradient(0deg,transparent 0 2px,rgba(0,0,0,.22) 2px 3px)}
#oled .l1{display:flex;justify-content:space-between;font-size:12px;opacity:.85}
#oled .l2{font-size:21px;font-weight:600;margin:8px 0 2px;min-height:26px}
#oled .l2 small{font-size:12px;opacity:.7}
#oled .l3{font-size:12px;opacity:.85}
.cur{animation:bl 1.1s steps(1) infinite}@keyframes bl{50%{opacity:0}}
#sweep{position:absolute;left:0;right:0;top:-30%;height:30%;pointer-events:none;
background:linear-gradient(rgba(237,235,227,0),rgba(237,235,227,.12));animation:sw .9s ease-out 1 forwards}
@keyframes sw{to{top:110%}}
#oled.off .px{color:var(--dd);text-shadow:none}
#oled .wv{position:absolute;right:14px;top:10px;font-size:15px;letter-spacing:2px;opacity:0}
#oled.ring .wv{animation:rg .5s linear 4}@keyframes rg{0%{opacity:0}40%{opacity:1}100%{opacity:0}}
section{margin-top:20px}
h2{font:600 11px/1 var(--mono);letter-spacing:.28em;color:var(--mu);margin:0 0 2px;
display:flex;align-items:center;gap:8px}
h2::after{content:'';flex:1;height:1px;background:var(--ln)}
.it{display:flex;align-items:center;justify-content:space-between;gap:12px;
min-height:48px;border-bottom:1px solid var(--ln);padding:6px 0}
.it>span{font-size:15px}
.mu{color:var(--mu)}small.mu{font-size:12px}
.sl{display:block;padding:10px 0 4px;border-bottom:1px solid var(--ln)}
.sl .cap{display:flex;justify-content:space-between;align-items:baseline;font-size:15px}
.sl output{font:14px var(--mono);color:var(--ink)}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:34px;background:none;margin:0}
input[type=range]::-webkit-slider-runnable-track{height:2px;border-radius:1px;
background:linear-gradient(90deg,var(--acc) var(--p,0%),#B3B2AC var(--p,0%))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:13px;height:24px;margin-top:-11px;
border-radius:3px;border:1px solid #96958F;box-shadow:0 1px 2px rgba(0,0,0,.25);
background:var(--key) no-repeat center/13px 10px linear-gradient(90deg,
transparent 0 3px,#B0AFA9 3px 4px,transparent 4px 6px,#B0AFA9 6px 7px,transparent 7px 9px,#B0AFA9 9px 10px,transparent 10px)}
input[type=range]::-moz-range-track{height:2px;border-radius:1px;background:#B3B2AC}
input[type=range]::-moz-range-progress{height:2px;border-radius:1px;background:var(--acc)}
input[type=range]::-moz-range-thumb{width:13px;height:24px;border-radius:3px;border:1px solid #96958F;
box-shadow:0 1px 2px rgba(0,0,0,.25);background:var(--key) no-repeat center/13px 10px linear-gradient(90deg,
transparent 0 3px,#B0AFA9 3px 4px,transparent 4px 6px,#B0AFA9 6px 7px,transparent 7px 9px,#B0AFA9 9px 10px,transparent 10px)}
.tg{position:relative;width:46px;height:26px;flex:none}
.tg input{position:absolute;inset:-9px -6px;opacity:0;margin:0;cursor:pointer}
.tg em{position:absolute;inset:0;border-radius:13px;background-color:#B7B6B0;transition:background-color .15s}
.tg em::before{content:'';position:absolute;top:2px;left:3px;width:20px;height:20px;border-radius:50%;
background:#FFF;border:1px solid #9C9B95;transition:transform .15s}
.tg input:checked+em{background-color:var(--acc)}
.tg input:checked+em::before{transform:translateX(19px);border-color:var(--acd)}
select{-webkit-appearance:none;appearance:none;min-width:132px;max-width:56%;min-height:44px;
padding:9px 30px 9px 12px;font:13px var(--mono);color:var(--ink);border:1px solid #C9C8C2;
border-radius:5px;box-shadow:inset 0 1px 3px rgba(0,0,0,.12);background:#ECEBE7
url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='10' height='6'%3E%3Cpath d='M1 1l4 4 4-4' fill='none' stroke='%23605F58' stroke-width='1.5'/%3E%3C/svg%3E")
no-repeat right 11px center}
button.k{font-size:14px;padding:12px 16px;border-radius:6px;border:1px solid var(--ke);
background:var(--key);color:var(--ink);cursor:pointer;box-shadow:0 1px 0 rgba(0,0,0,.06)}
button.k:active{background:var(--kdn);transform:translateY(1px);box-shadow:none}
button.pri{background:var(--acd);border-color:#B23A00;color:#FFF;font-weight:600}
button.pri:active{background:#B23A00}
.btns{display:flex;gap:10px;padding:12px 0}
ul{list-style:none;margin:0;padding:0}
ul li{border-bottom:1px solid var(--ln);padding:10px 0;font-size:14px}
ul li:last-child{border-bottom:0}
ul b{font-weight:600}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--db);
border:1px solid var(--de);color:var(--df);font:13px var(--mono);padding:9px 18px;border-radius:8px;
opacity:0;transition:opacity .25s;pointer-events:none;white-space:nowrap}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
</style>
<header><h1>AGENTBELL</h1><small>console-1</small><i id=led></i></header>
<div id=oled>
 <div id=sweep></div><span class="wv px">)))</span>
 <div class="l1 px"><span id=st>连接中</span><span id=ip></span></div>
 <div class="l2 px" id=big>待机中<span class=cur>▮</span></div>
 <div class="l3 px" id=sub>&nbsp;</div>
</div>
<section><h2>NOTIFY · 通知</h2>
 <div class=it><span>提示音</span><select id=tone aria-label=提示音></select></div>
 <div class=btns><button class=k onclick="act('play')">试听</button><button class=k onclick="act('vibtest')">试震</button></div>
 <div class=sl><div class=cap><span>通知音量</span><output id=obvol></output></div><input type=range id=bvol min=0 max=100 step=5 aria-label=通知音量></div>
 <div class=sl><div class=cap><span>通知震动</span><output id=ovvol></output></div><input type=range id=vvol min=0 max=100 step=5 aria-label=通知震动></div>
 <div class=it><span>蜂鸣器</span><label class=tg><input type=checkbox id=buzz><em></em></label></div>
 <div class=it><span>震动</span><label class=tg><input type=checkbox id=vib><em></em></label></div>
 <div class=it><span>勿扰 <small class=mu>只屏显不响</small></span><label class=tg><input type=checkbox id=dnd><em></em></label></div>
 <div class=it><span>通知亮屏 <small class=mu>熄屏自动点亮</small></span><label class=tg><input type=checkbox id=nwake><em></em></label></div>
</section>
<section><h2>FEEDBACK · 操作反馈</h2>
 <div class=it><span>反馈方式</span><select id=fb aria-label=反馈方式></select></div>
 <div class=sl><div class=cap><span>反馈音量</span><output id=ofbvol></output></div><input type=range id=fbvol min=0 max=100 step=5 aria-label=反馈音量></div>
 <div class=sl><div class=cap><span>反馈震动</span><output id=ofbvib></output></div><input type=range id=fbvib min=0 max=100 step=5 aria-label=反馈震动></div>
</section>
<section><h2>SCREEN · 屏幕</h2>
 <div class=it><span>待机屏·左</span><select id=lsty aria-label=待机屏左></select></div>
 <div class=it><span>待机屏·右</span><select id=rsty aria-label=待机屏右></select></div>
</section>
<section><h2>ENCODER · 旋钮</h2>
 <div class=it><span>方向反转</span><label class=tg><input type=checkbox id=encrev><em></em></label></div>
 <div class=it><span>灵敏度</span><select id=encdet aria-label=灵敏度></select></div>
</section>
<section><h2>LOG · 最近通知</h2>
 <ul id=notes><li class=mu>加载中……</li></ul>
 <div class=btns><button class="k pri" onclick="test()">发测试通知</button></div>
</section>
<div id=toast></div>
<script>
const $=id=>document.getElementById(id);
let S=null,busy=0,fails=0;
const FB=['声音+震动','只震动','只声音','无'],DET=['1 档/步','2 档/步','3 档/步'];
function toast(m){const t=$('toast');t.textContent=m;t.style.opacity=1;
 clearTimeout(t._h);t._h=setTimeout(()=>t.style.opacity=0,1300)}
function setSl(k,v){const r=$(k);r.value=v;r.style.setProperty('--p',v+'%');$('o'+k).value=v}
function fill(sel,names,cur,base){sel.innerHTML='';(names||[]).forEach((n,i)=>{
 const o=document.createElement('option');o.value=(base||0)+i;o.textContent=n;sel.appendChild(o)});
 sel.value=cur}
function render(s){S=s;
 ['buzz','vib','dnd','nwake','encrev'].forEach(k=>$(k).checked=!!s[k]);
 ['bvol','vvol','fbvol','fbvib'].forEach(k=>setSl(k,s[k]));
 fill($('tone'),s.melodies,s.tone);fill($('fb'),FB,s.fb);fill($('encdet'),DET,s.encdet,1);
 fill($('lsty'),s.panes,s.lsty);fill($('rsty'),s.panes,s.rsty)}
function save(f){busy=1;
 fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:new URLSearchParams(f)}).then(r=>r.json()).then(s=>{render(s);toast('已保存')})
 .catch(()=>toast('保存失败，请重试')).finally(()=>busy=0)}
function act(k){save({[k]:1});ring()}
function test(){fetch('/api/test').then(()=>{toast('已发送');ring()}).catch(()=>toast('发送失败'))}
function ring(){const o=$('oled');o.classList.remove('ring');void o.offsetWidth;o.classList.add('ring')}
['tone','fb','encdet','lsty','rsty'].forEach(k=>$(k).onchange=()=>save({[k]:$(k).value}));
['buzz','vib','dnd','nwake','encrev'].forEach(k=>$(k).onchange=()=>save({[k]:$(k).checked?1:0}));
['bvol','vvol','fbvol','fbvib'].forEach(k=>{const r=$(k);
 r.oninput=()=>{r.style.setProperty('--p',r.value+'%');$('o'+k).value=r.value};
 r.onchange=()=>save({[k]:r.value})});
function esc(s){const d=document.createElement('i');d.textContent=s;return d.innerHTML}
function fmt(s){return s<3600?Math.floor(s/60)+'分':Math.floor(s/3600)+'时'+Math.floor(s%3600/60)+'分'}
function refresh(){if(busy)return;
 fetch('/api/info').then(r=>r.json()).then(i=>{fails=0;$('oled').classList.remove('off');
  $('st').textContent='在线';$('st').style.color='#3FB950';$('led').style.background='#3FB950';
  $('ip').textContent=i.ip;$('sub').textContent='信号 '+i.rssi+'dBm · 运行 '+fmt(i.uptime_s)})
 .catch(()=>{if(++fails>1){$('oled').classList.add('off');$('st').textContent='连接断开';
  $('st').style.color='#E5484D';$('led').style.background='#E5484D';$('sub').textContent='正在重试……'}});
 fetch('/api/notes').then(r=>r.json()).then(a=>{
  $('big').innerHTML=a.length?esc(a[0].agent)+' <small>'+esc(a[0].conversation)+'</small>'
                             :'待机中<span class=cur>▮</span>';
  const u=$('notes');u.innerHTML='';
  if(!a.length){u.innerHTML='<li class=mu>还没有通知。点下方按钮试一条。</li>';return}
  a.forEach(n=>{const li=document.createElement('li');
   li.innerHTML='<b></b> · <span></span> · <span></span> <span class=mu></span>';
   const e=li.querySelectorAll('b,span');e[0].textContent=n.agent;e[1].textContent=n.computer;
   e[2].textContent=n.conversation;e[3].textContent='('+n.ago+')';u.appendChild(li)})}).catch(()=>0)}
fetch('/api/settings').then(r=>r.json()).then(render);refresh();setInterval(refresh,5000);
</script>)HTML";

static void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", CONSOLE_HTML);
}

// 最近通知 → JSON 数组（控制台用；文本来自电脑侧，需转义防注入/防坏 JSON）
static void jsonEscapeTo(String& out, const char* s) {
  for (const char* p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
    else out += (char)c;
  }
}
static void handleApiNotes() {
  String j;
  j.reserve(256 + ringCount * 200);
  j += "[";
  for (int k = 0; k < ringCount; k++) {
    Note* n = noteByOffset(k);
    char ago[16]; timeAgo(n->recv, ago, sizeof(ago));
    if (k) j += ",";
    j += "{\"agent\":\"";        jsonEscapeTo(j, n->agent);
    j += "\",\"computer\":\"";   jsonEscapeTo(j, n->computer);
    j += "\",\"conversation\":\""; jsonEscapeTo(j, n->conversation);
    j += "\",\"message\":\"";    jsonEscapeTo(j, n->message);
    j += "\",\"ago\":\"";        j += ago;
    j += "\"}";
  }
  j += "]";
  server.send(200, "application/json; charset=utf-8", j);
}

// ============================================================================
//  桥接 API（给电脑侧桥接程序用，JSON 用 String 拼接，不引 ArduinoJson）
// ============================================================================
// 设备身份：局域网扫描时靠它确认「这是 AgentBell」
static void handleApiInfo() {
  String j = "{\"app\":\"agent-bell\",\"api\":1";
  j += ",\"name\":\"" + String(MDNS_NAME) + "\"";
  j += ",\"mac\":\"" + WiFi.macAddress() + "\"";
  j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"uptime_s\":" + String(millis() / 1000);
  j += "}";
  server.send(200, "application/json; charset=utf-8", j);
}

// 当前全部运行时设置 → JSON（GET 与 POST 应答共用）
static String settingsJson() {
  String j;
  j.reserve(512);            // 一次预分配，避免几十次 += 反复扩容
  j += "{";
  j += "\"buzz\":"   + String(buzzerEnabled ? 1 : 0);
  j += ",\"vib\":"   + String(vibEnabled ? 1 : 0);
  j += ",\"dnd\":"   + String(dnd ? 1 : 0);
  j += ",\"bvol\":"  + String(buzzerVol);
  j += ",\"vvol\":"  + String(vibVol);
  j += ",\"tone\":"  + String(alertTone);
  j += ",\"fb\":"    + String(feedbackMode);
  j += ",\"fbvol\":" + String(fbVol);
  j += ",\"fbvib\":" + String(fbVibVol);
  j += ",\"encrev\":" + String(encReversed ? 1 : 0);
  j += ",\"encdet\":" + String(encDetents);
  j += ",\"nwake\":" + String(notifyWakes ? 1 : 0);
  j += ",\"lsty\":"  + String(leftStyle);
  j += ",\"rsty\":"  + String(rightStyle);
  j += ",\"melodies\":[";
  for (int i = 0; i < MEL_N; i++) {           // 固定常量名，无需 JSON 转义
    if (i) j += ",";
    j += "\"" + String(MELODIES[i].name) + "\"";
  }
  j += "],\"panes\":[";
  for (int i = 0; i < PANE_N; i++) {          // 待机屏半屏模块名（左右通用）
    if (i) j += ",";
    j += "\"" + String(PANE_NAMES[i]) + "\"";
  }
  j += "]}";
  return j;
}

// 表单参数工具：有该字段才改（全部可选，只改传了的）
static bool argBool(const char* key, bool cur) {
  return server.hasArg(key) ? (server.arg(key).toInt() != 0) : cur;
}
static int argClamp(const char* key, int cur, int lo, int hi) {
  if (!server.hasArg(key)) return cur;
  int v = server.arg(key).toInt();
  if (v < lo) v = lo; if (v > hi) v = hi;
  return v;
}

// GET 读设置；POST 改设置（字段全部可选）+ 可选动作 play/vibtest
static void handleApiSettings() {
  if (server.method() == HTTP_POST) {
    buzzerEnabled = argBool("buzz",   buzzerEnabled);
    vibEnabled    = argBool("vib",    vibEnabled);
    dnd           = argBool("dnd",    dnd);
    encReversed   = argBool("encrev", encReversed);
    notifyWakes   = argBool("nwake",  notifyWakes);
    buzzerVol     = argClamp("bvol",   buzzerVol,    0, 100);
    vibVol        = argClamp("vvol",   vibVol,       0, 100);
    fbVol         = argClamp("fbvol",  fbVol,        0, 100);
    fbVibVol      = argClamp("fbvib",  fbVibVol,     0, 100);
    alertTone     = argClamp("tone",   alertTone,    0, MEL_N - 1);
    feedbackMode  = argClamp("fb",     feedbackMode, 0, 3);
    encDetents    = argClamp("encdet", encDetents,   1, 3);
    leftStyle     = argClamp("lsty",   leftStyle,    0, PANE_N - 1);
    rightStyle    = argClamp("rsty",   rightStyle,   0, PANE_N - 1);
    buzzer.setVolume(buzzerVol);              // 同步到输出通道
    vibrator.setVolume(vibVol);
    saveSettings();                           // 存 NVS 掉电不丢
    needRender = true;                        // 刷新屏幕（如静音图标）
    // 动作参数（设置改完再执行）：试听/试震是用户显式操作，不受 dnd/开关限制；
    // 音量用刚生效的设置值 → 听到的就是之后通知的实际响度
    if (server.hasArg("play") && server.arg("play").toInt() != 0)
      buzzer.play(MELODIES[alertTone].seq, MELODIES[alertTone].len);
    if (server.hasArg("vibtest") && server.arg("vibtest").toInt() != 0)
      vibrator.trigger(ALERT_VIB, sizeof(ALERT_VIB) / sizeof(ALERT_VIB[0]));
  }
  server.send(200, "application/json; charset=utf-8", settingsJson());
}

// 链路自检：注入一条固定测试通知，完整走 fireAlert（响铃+震动+屏显）
static void handleApiTest() {
  Note n{};
  strlcpy(n.computer,     "桥接程序", sizeof(n.computer));
  strlcpy(n.agent,        "测试",     sizeof(n.agent));
  strlcpy(n.conversation, "链路自检", sizeof(n.conversation));
  strlcpy(n.message,      "看到这条说明电脑到设备链路正常", sizeof(n.message));
  n.recv = millis();
  fireAlert(n);
  server.send(200, "text/plain; charset=utf-8", "ok");
}

// ============================================================================
//  AP 配网模式：连不上 WiFi（或菜单选「重新配网」）时开热点 AgentBell-XXXX。
//  手机/电脑连它 → 强制门户弹配置页（或访问 192.168.4.1）→ 填 WiFi → 设备重启去连。
//  电脑侧 tools/agent-bell-bridge/provision.py 可全自动完成这套流程。
// ============================================================================
static const char* AP_PASS = "agentbell";   // 热点密码（8 位起；页面和 OLED 都会显示）

// 配网页（内存页面，无外部资源；手机弱信号也秒开）
static void handlePortalRoot() {
  String h;
  h.reserve(1800);
  h += F("<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>AgentBell 配网</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:420px;margin:0 auto;padding:20px;background:#0b1020;color:#e7ecff}"
    "h1{font-size:22px}input,button{width:100%;box-sizing:border-box;font-size:16px;padding:10px;margin:6px 0;"
    "border-radius:8px;border:1px solid #2a3350}input{background:#161c34;color:#e7ecff}"
    "button{background:#3b82f6;color:#fff;border:none;font-weight:600}"
    ".muted{color:#9aa6cc;font-size:13px}</style>"
    "<h1>🔔 AgentBell 配网</h1>"
    "<p class=muted>把设备接入你的 WiFi（仅支持 2.4GHz）。提交后设备自动重启并连接。</p>"
    "<form method=POST action=/wifi>"
    "<input name=ssid placeholder='WiFi 名称 (SSID)' required maxlength=32>"
    "<input name=pass type=password placeholder='WiFi 密码（开放网络留空）' maxlength=64>"
    "<button>保存并连接</button></form>");
  h += "<p class=muted>设备 MAC：" + WiFi.macAddress() + "</p>";
  server.send(200, "text/html; charset=utf-8", h);
}

// 保存凭据并重启（表单和 provision.py 共用；GET/POST 都收）
static void handlePortalWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  if (!ssid.length()) { server.send(400, "text/plain; charset=utf-8", "ssid required"); return; }
  saveWifiCred(ssid.c_str(), pass.c_str());
  server.send(200, "text/html; charset=utf-8",
      F("<!doctype html><meta charset=utf-8><body style='font-family:system-ui;background:#0b1020;color:#e7ecff;"
        "text-align:center;padding-top:60px'><h2>✅ 已保存</h2><p>设备正在重启并连接新 WiFi……<br>"
        "连上后 OLED 会回到待机屏。</p>"));
  u8g2.clearBuffer();
  u8g2.setFont(FONT_CN);
  u8g2.drawUTF8(10, 30, "收到新WiFi配置");
  u8g2.drawUTF8(10, 50, "正在重启连接…");
  u8g2.sendBuffer();
  delay(600);                                // 等应答发完
  ESP.restart();
}

// 进入配网模式（不返回正常流程；10 分钟无人配则重启重试原 WiFi）
static void enterApMode() {
  apMode = true;
  apStartedAt = millis();
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "AgentBell-%02X%02X", mac[4], mac[5]);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, AP_PASS);              // 192.168.4.1
  dnsServer.start(53, "*", WiFi.softAPIP()); // 强制门户：任意域名 → 设备
  server.on("/wifi", handlePortalWifi);      // 配网接口（provision.py 用）
  server.on("/api/info", handleApiInfo);     // 身份确认（provision.py 扫描判定用）
  server.onNotFound(handlePortalRoot);       // 其余一律回配置页（含 /generate_204 等探测）
  server.begin();
  Serial.printf("[ap] 配网热点 %s 密码 %s，配置页 http://192.168.4.1\n", apSsid, AP_PASS);
}

// 配网模式的 OLED 屏（loop 里代替正常渲染）
static void renderApScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(FONT_CN);
  u8g2.drawUTF8(0, 12, "WiFi 配网模式");
  u8g2.drawHLine(0, 15, 128);
  char l[40];
  snprintf(l, sizeof(l), "热点:%s", apSsid);
  u8g2.drawUTF8(0, 27, l);
  snprintf(l, sizeof(l), "密码:%s", AP_PASS);
  u8g2.drawUTF8(0, 39, l);
  u8g2.drawUTF8(0, 51, "连热点后浏览器打开:");           // 12px/汉字，一行放不下 IP，拆两行
  int n = WiFi.softAPgetStationNum();
  if (n > 0) snprintf(l, sizeof(l), "192.168.4.1 · 已连%d台", n);
  else       strlcpy(l, "192.168.4.1", sizeof(l));
  u8g2.drawUTF8(0, 63, l);
  u8g2.sendBuffer();
}
#endif

#ifdef INO_SIM
// ino-sim：每帧从 scenario 注入的 var 同步界面状态，方便预览各种屏幕。可用变量：
//   agent / computer / conversation / message  —— 设了 agent 就显示一条通知
//   buzzer / vib / dnd  (0|1)                   —— 状态指示（关铃/关震/勿扰）
//   unread N / history N                        —— 未读数 / 待机屏历史条数
//   lsty N / rsty N                             —— 待机屏左/右半屏模块索引
// 不设 agent → 显示待机屏。
static void simRefresh() {
  buzzerEnabled = SIM_NUM("buzzer", 1) != 0;
  vibEnabled    = SIM_NUM("vib", 1) != 0;
  dnd           = SIM_NUM("dnd", 0) != 0;
  leftStyle     = (int)SIM_NUM("lsty", 0);
  rightStyle    = (int)SIM_NUM("rsty", 1);
  if (leftStyle  < 0 || leftStyle  >= PANE_N) leftStyle  = 0;
  if (rightStyle < 0 || rightStyle >= PANE_N) rightStyle = 1;
  const char* ag = SIM_STR("agent", "");
  if (ag[0]) {
    strlcpy(ring[0].agent,        ag,                           sizeof(ring[0].agent));
    strlcpy(ring[0].computer,     SIM_STR("computer", "?"),     sizeof(ring[0].computer));
    strlcpy(ring[0].conversation, SIM_STR("conversation", "-"), sizeof(ring[0].conversation));
    strlcpy(ring[0].message,      SIM_STR("message", ""),       sizeof(ring[0].message));
    ring[0].recv = millis();
    ringCount = 1; ringHead = 1; viewOffset = 0;
    unread = (int)SIM_NUM("unread", 1);
    uiState = UI_NOTE;
  } else {
    ringCount = (int)SIM_NUM("history", 0);
    viewOffset = -1;
    uiState = UI_IDLE;
  }
  needRender = true;
}
#endif

// ============================================================================
//  setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
#if !defined(SIM_DEMO) && !defined(INO_SIM)
  // 若是从「关机」深睡醒来：先解除引脚锁定，否则蜂鸣/震动脚被 hold 住无法驱动
  gpio_hold_dis((gpio_num_t)BUZZER_PIN);
  gpio_hold_dis((gpio_num_t)VIB_PIN);
  gpio_deep_sleep_hold_dis();
#endif
  buzzer.begin(BUZZER_PIN);
  vibrator.begin(VIB_PIN, VIB_ACTIVE_HIGH, VIB_FREQ);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(OLED_ADDR << 1);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setBusClock(700000);                  // 提高 I2C 速率 → 菜单动画更丝滑
  encBegin();                                // 旋转编码器（中断解码 + 按键）
  render();                                  // 先出一帧

#ifndef SIM_DEMO
  loadSettings();                            // 读回上次保存的开关状态
  loadWifiCred();                            // WiFi 凭据：NVS（配网写的）优先，其次 secrets.h

  // 菜单「重新配网」重启进来：吃掉一次性标记，直进热点（不试连原 WiFi）
  prefs.begin("agentbell", false);
  bool forceAp = prefs.getBool("force_ap", false);
  if (forceAp) prefs.putBool("force_ap", false);
  prefs.end();
  if (forceAp) {
    enterApMode();
    needRender = true;
    return;
  }

  // —— 连 WiFi（OLED 显进度，最多等 20s；连不上自动进配网热点）——
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);               // 关闭 WiFi 省电休眠 → 通知即时响应（USB 供电，不在乎功耗）
  WiFi.setAutoReconnect(true);        // 路由器重启/信号闪断后由内核自动重连
  WiFi.begin(wifiSsid, wifiPass);
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) {
    u8g2.clearBuffer();
    u8g2.setFont(FONT_CN);
    u8g2.drawUTF8(0, 24, "正在连接 WiFi");
    char dots[8] = ""; for (int d = 0; d < (i % 4); d++) strcat(dots, ".");
    u8g2.drawUTF8(0, 42, dots);
    u8g2.sendBuffer();
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {       // 20s 没连上：多半在陌生环境 → 开配网热点
    Serial.printf("WiFi「%s」连不上，进入配网模式\n", wifiSsid);
    enterApMode();
    needRender = true;
    return;                                  // 配网模式由 loop 顶部接管，不走下面的正常初始化
  }
  // 待机屏时钟：SNTP 后台同步（非阻塞，掉线重连后也会补同步）。中国时区 UTC+8。
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");

  // HTTP 服务不依赖连网时序：开机时路由器还没好（如停电恢复）也照常启动，连上 WiFi 即可用。
  // mDNS 要拿到 IP 才有意义，放在 loop 里首次连上后挂载。
  server.on("/notify",  handleNotify);      // 给电脑侧 hook 用（GET/POST 均可）
  server.on("/healthz", []() { server.send(200, "text/plain", "ok"); });
  server.on("/",        handleRoot);        // 网页设置控制台（读写 /api/settings）
  server.on("/api/info",     handleApiInfo);      // 设备身份 JSON（桥接程序扫描确认用）
  server.on("/api/settings", handleApiSettings);  // 读/改运行时设置（GET 读，POST 改）
  server.on("/api/notes",    handleApiNotes);     // 最近通知 JSON（控制台用）
  server.on("/api/test",     handleApiTest);      // 注入固定测试通知，验证链路
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  if (WiFi.status() != WL_CONNECTED) Serial.println("WiFi 尚未连上，后台继续重连…");
#else
  // —— 仿真预览（Wokwi）：注入两条示例通知，直接看界面 ——
  // ino-sim 走 scenario 注入（见 simRefresh），故这里跳过硬编码 demo。
  #ifndef INO_SIM
  struct timeval tv = { 1785406080, 0 };   // 假时间 2026-07-30 10:08（预览时钟面板用）
  settimeofday(&tv, nullptr);
  Note a{}; strlcpy(a.computer, "书房台式", sizeof(a.computer));
  strlcpy(a.agent, "Codex", sizeof(a.agent));
  strlcpy(a.conversation, "后端重构", sizeof(a.conversation));
  a.recv = millis(); addNote(a);
  Note b{}; strlcpy(b.computer, "MacBook", sizeof(b.computer));
  strlcpy(b.agent, "Claude", sizeof(b.agent));
  strlcpy(b.conversation, "仿真器", sizeof(b.conversation));
  strlcpy(b.message, "已完成并验证编译通过", sizeof(b.message));
  b.recv = millis(); fireAlert(b);
  #endif
#endif

  needRender = true;
}

void loop() {
#ifdef INO_SIM
  simRefresh();                 // ino-sim：每帧同步 scenario 注入的界面状态
#endif
#ifndef SIM_DEMO
  if (apMode) {                 // —— 配网模式：只跑门户 + 屏显，不跑正常 UI ——
    dnsServer.processNextRequest();
    server.handleClient();
    static unsigned long lastApDraw = 0;
    if (millis() - lastApDraw > 500) { lastApDraw = millis(); renderApScreen(); }
    if (millis() - apStartedAt > 10 * 60 * 1000UL) ESP.restart();   // 10min 没人配 → 重启再试原 WiFi
    delay(2);
    return;
  }
  server.handleClient();
  // 首次连上 WiFi 后挂载 mDNS（开机没连上时每 2s 补查一次；掉线重连无需重挂）
  static bool mdnsUp = false;
  static unsigned long lastNetChk = 0;
  if (!mdnsUp && millis() - lastNetChk > 2000) {
    lastNetChk = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
      if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
      mdnsUp = true;
    }
  }
#endif
  buzzer.update();
  vibrator.update();
  handleInput();

  // 提示音子菜单：停留 ~300ms 自动试听（按通知音量，所听即所得）
  if (uiState == UI_MENU && curSub == SUB_MELODY && subSel != melPreviewed && millis() - melSettleAt > 300) {
    melPreviewed = subSel;
    buzzer.play(MELODIES[subSel].seq, MELODIES[subSel].len);
  }
  // 菜单/调节/弹窗闲置 15s 回待机（滑块页超时退出也把已改的值存盘）
  if (uiState != UI_IDLE && uiState != UI_NOTE && millis() - lastInput > 15000) {
#ifndef SIM_DEMO
    if (uiState == UI_SLIDER) saveSettings();
#endif
    uiState = UI_IDLE;
  }

  // 菜单动画期高刷（~45fps）；待机/通知 40ms（宇航员 80ms/帧，采样需更密才不跳帧；熄屏时 render 直接返回）
  unsigned long interval = (uiState == UI_MENU || uiState == UI_SLIDER || uiState == UI_POPUP) ? 22 : 40;
  if (needRender || millis() - lastRender > interval) {
    render();
    needRender = false;
    lastRender = millis();
  }
}
