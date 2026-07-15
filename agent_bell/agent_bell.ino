// ============================================================================
//  AgentBell 智能体门铃 — ESP32-C3 SuperMini + SSD1306 128x64 OLED
// ----------------------------------------------------------------------------
//  作用：电脑上的 Claude Code / Codex 完成一轮对话时，通过局域网 HTTP 通知本设备，
//        它会「蜂鸣 + 震动」并在 OLED 上显示：哪台电脑、哪个智能体、哪个对话。
//
//  架构：本设备是 HTTP 服务器（局域网内任意电脑都能 push）。电脑侧用 Claude 的
//        Stop hook / Codex 的 notify 程序，在对话结束时 POST /notify 过来。
//
//  录音→语音识别→回填输入框那部分留待以后（触摸手势、麦克风引脚已预留）。
//
//  编译（Huge App 分区容纳中文字体）：
//    arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app \
//        --output-dir agent_bell/build agent_bell
//  定义 SIM_DEMO 宏则跳过联网、开机自注入示例通知（给 Wokwi 预览界面用）。
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#ifndef SIM_DEMO
  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESPmDNS.h>
  #include <Preferences.h>   // 把网页里的开关存进 NVS，掉电不丢
#endif

#ifdef INO_SIM
  #include <sim_inject.h>    // ino-sim 屏幕模拟器：从 scenario 的 var 注入界面数据
#endif

// 前置声明：有函数在这两个类型定义之前被 arduino-cli 自动生成原型引用（Note* / ListAnim&），先声明避免"未命名类型"
struct Note;
struct ListAnim;
struct SetMeta;
enum SubMenu { SUB_NONE, SUB_MELODY };                          // 选项子列表（当前仅提示音用竖列表）
enum SetId   { SET_BUZVOL, SET_VIBVOL, SET_FBMODE, SET_FBVOL, SET_FBVIB, SET_ENCDIR, SET_ENCSENS };  // 滑块设置项（定义在顶部，供自动原型引用）

// ===== 硬件引脚（ESP32-C3 SuperMini，全部避开 strapping 脚 2/8/9）=====
#define OLED_SDA   5      // I2C 数据
#define OLED_SCL   6      // I2C 时钟
#define OLED_ADDR  0x3C   // 少数屏是 0x3D
#define TOUCH_PIN  4      // 触摸模块 SIG（数字输入）
#define BUZZER_PIN 3      // 蜂鸣器 I/O
#define VIB_PIN    10     // 震动模块 IN
// 麦克风（以后录音用）：原预留的 7/20 已被编码器占用，录音引脚以后再规划
// —— 旋转编码器 HW-040（⚠ 电源脚 + 接 3V3，绝不能接 5V！输出电平跟随供电，5V 会烧 C3）——
#define ENC_CLK 1     // A 相
#define ENC_DT  7     // B 相
#define ENC_SW  20    // 按压（低有效，用内部上拉）
#define ENC_RAW_PER_DETENT 2   // 本编码器每个物理档位产生的正交沿数（实测 div=4 时每 2 档动一下 → 2/档）
// 旋转方向 encReversed、灵敏度 encDetents（每几档动一步）都是运行时设置（菜单/网页可调）

// ===== 模块触发极性 =====
// 蜂鸣器：无源 + 低电平触发（PNP 驱动），发声/静音逻辑在 ToneBuzzer 内处理，无需在此配。
#define VIB_ACTIVE_HIGH     true    // 震动模块触发极性（装上后不对就翻这里）

// ===== 触摸键 =====
static const unsigned long TOUCH_DEBOUNCE_MS = 25;   // 触摸键现在只作"返回"：去抖后单次按下即触发

// ===== 联网配置 =====
// WiFi 凭据放在 secrets.h（已 gitignore，不上传）。有它就用你的真实凭据；没有（如别人克隆仓库）则用占位符，照样能编译。
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  const char* WIFI_SSID = "YOUR_WIFI_SSID";
  const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
#endif
const char* MDNS_NAME = "agent-bell";      // mDNS 主机名（仅供 /notify 发现，界面不再显示）

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
static const ToneSeg TAP_TONES[]   = { {3000, 3000, 45} };  // 触摸/旋钮反馈：短促「嘀」
static const ToneSeg FB_TONES[]    = { {2637, 2637, 110} }; // 调强度时的反馈音
static const uint16_t FB_VIB[]     = { 220 };              // 调强度反馈短震
static const uint16_t TAP_VIB[]    = { 95 };               // 触摸/旋钮反馈：轻震（够长以启动马达）

// 蜂鸣器是无源的：靠方波频率发声（音高可调）；震动马达用 PWM 占空比调强度。
static const uint32_t VIB_FREQ = 20000;   // 震动马达 PWM 载波，占空比=震动强度

// ===== 显示对象：SSD1306 128x64，硬件 I2C，F=全缓冲（1KB RAM）=====
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/ U8X8_PIN_NONE);
// 中文用文泉驿 GB2312 全字库（存 flash，故需 huge_app）；大号拉丁字体给智能体名。
#define FONT_CN   u8g2_font_wqy12_t_gb2312
#define FONT_BIG  u8g2_font_helvB14_tr
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
int  touchIdleLevel = -1;         // 开机采样的触摸空闲电平（自动兼容高/低有效）
bool screenOff   = false;         // 屏幕是否熄灭（运行时，不持久）
bool notifyWakes = true;          // 熄屏时来通知是否自动亮屏

// ===== UI 状态机（编码器导航的丝滑菜单）=====
enum UiState { UI_IDLE, UI_NOTE, UI_MENU, UI_SLIDER, UI_POPUP };
UiState uiState = UI_IDLE;
SubMenu curSub = SUB_NONE;        // 主菜单里进入的"选项子列表"（当前仅提示音）
SetId curSet = SET_BUZVOL;        // 当前正在调的滑块设置项
int   mainSel = 0, subSel = 0;    // 主列表 / 子列表当前选中项
bool  uiBack = false;             // 触摸键=返回
unsigned long lastInput = 0;      // 最近交互（15s 回待机）
unsigned long melSettleAt = 0;    // 提示音停留自动试听计时
int   melPreviewed = -1;
struct ListAnim { float top, sel; };
ListAnim mainAnim = {0, 0}, subAnim = {0, 0};   // 缓动状态（主列表 / 子列表）
float sliderAnim = 0;             // 滑块 thumb 缓动位置（0..1）

#ifndef SIM_DEMO
Preferences prefs;
static void loadSettings() {
  prefs.begin("agentbell", true);          // 只读
  buzzerEnabled = prefs.getBool("buzz", true);
  vibEnabled    = prefs.getBool("vib",  true);
  dnd           = prefs.getBool("dnd",  false);
  buzzerVol     = prefs.getInt("bvol", 100);
  vibVol        = prefs.getInt("vvol", 100);
  alertTone     = prefs.getInt("tone", 0);
  if (alertTone < 0 || alertTone >= MEL_N) alertTone = 0;
  encReversed   = prefs.getBool("encrev", false);
  encDetents    = prefs.getInt("encdet", 1); if (encDetents < 1 || encDetents > 3) encDetents = 1;
  feedbackMode  = prefs.getInt("fb", 0);     if (feedbackMode < 0 || feedbackMode > 3) feedbackMode = 0;
  fbVol         = prefs.getInt("fbv2", 60);    // 新键：忽略旧的过低值，用新默认
  fbVibVol      = prefs.getInt("fbvv2", 75);
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
//  报警：入队 + 蜂鸣 + 震动
// ============================================================================
void fireAlert(const Note& n) {
  addNote(n);
  uiState = UI_NOTE;                              // 通知打断任何界面
  if (!dnd && buzzerEnabled) buzzer.play(MELODIES[alertTone].seq, MELODIES[alertTone].len, 100);   // 通知：选用的提示音，满音量
  if (!dnd && vibEnabled)    vibrator.trigger(ALERT_VIB, sizeof(ALERT_VIB) / sizeof(ALERT_VIB[0]), 100);
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

#ifndef SIM_DEMO
static bool wifiOk() { return WiFi.status() == WL_CONNECTED; }
#endif

// WiFi 信号：苹果风 4 个小圆点（填充数=强度）。rightX=最右点中心，cy=中心纵坐标。
static void drawSignalDots(int rightX, int cy, int rssi, uint8_t color) {
  int lv = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
  u8g2.setDrawColor(color);
  for (int i = 0; i < 4; i++) {
    int cx = rightX - (3 - i) * 5;         // 4 点，中心间距 5px（总宽约 18）
    if (i < lv) u8g2.drawDisc(cx, cy, 1);        // 实心=有信号
    else        u8g2.drawCircle(cx, cy, 1);      // 空心=弱
  }
  u8g2.setDrawColor(1);
}

// 响铃声波：从 (x,y) 向右画 n 圈同心弧（动画用）+ 中心点。
static void drawWaves(int x, int y, int n, uint8_t color) {
  u8g2.setDrawColor(color);
  u8g2.drawDisc(x, y, 1);
  for (int i = 1; i <= n && i <= 3; i++)
    u8g2.drawCircle(x, y, i * 3, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_RIGHT);
  u8g2.setDrawColor(1);
}

// 星空：几颗固定星点（避开正中宇航员），部分随相位在「点」和「小十字」间闪烁
static void drawStars(float ph) {
  static const uint8_t sx[] = {10, 22, 108, 118, 16, 112, 30, 98};
  static const uint8_t sy[] = {14, 40, 12, 34, 56, 52, 58, 60};
  int k = (int)ph;
  for (int i = 0; i < 8; i++) {
    u8g2.drawPixel(sx[i], sy[i]);
    if ((k + i) % 3 == 0) {                        // 闪烁：变成小十字
      u8g2.drawPixel(sx[i] - 1, sy[i]); u8g2.drawPixel(sx[i] + 1, sy[i]);
      u8g2.drawPixel(sx[i], sy[i] - 1); u8g2.drawPixel(sx[i], sy[i] + 1);
    }
  }
}

// 程序化小宇航员已改为预渲染的自转帧（astronaut_frames.h + drawXBMP），见 renderIdle。

// 待机屏：星空 + 屏幕正中旋转宇航员 + 右上角苹果风信号点；静音时左上角画 ⊘
static void renderIdle() {
  float ph = millis() * 0.0016f;
  drawStars(ph);
#ifndef SIM_DEMO
  drawSignalDots(125, 6, wifiOk() ? WiFi.RSSI() : -100, 1);
#else
  drawSignalDots(125, 6, -50, 1);
#endif
  if (dnd) { u8g2.drawCircle(6, 7, 4); u8g2.drawLine(3, 4, 9, 10); }   // 左上角静音 ⊘
  int fi = (int)((millis() / 80) % ASTRO_N);       // 逐帧自转
  u8g2.drawXBMP((128 - ASTRO_W) / 2, (64 - ASTRO_H) / 2, ASTRO_W, ASTRO_H, ASTRO[fi]);
}

// 通知屏：反白标题栏（智能体名 + 响铃声波动画）+ 电脑/对话 + 底部时间/未读药丸
static void renderNote(const Note* n) {
  char line[112];
  bool ringing = buzzer.busy() || vibrator.busy();

  u8g2.drawBox(0, 0, 128, 16);                        // 反白顶栏
  u8g2.setDrawColor(0);
  u8g2.setFont(FONT_BIG);
  u8g2.drawUTF8(3, 13, n->agent[0] ? n->agent : "Agent");
  int waves = ringing ? (1 + (int)((millis() / 180) % 3)) : 3;   // 报警时 1→2→3 循环
  drawWaves(116, 8, waves, 0);
  u8g2.setDrawColor(1);

  u8g2.setFont(FONT_CN);
  snprintf(line, sizeof(line), "电脑 %s", n->computer[0] ? n->computer : "-");
  u8g2.drawUTF8(3, 31, line);                         // 过长由 U8g2 自动裁到屏边
  snprintf(line, sizeof(line), "对话 %s", n->conversation[0] ? n->conversation : "-");
  u8g2.drawUTF8(3, 45, line);

  u8g2.drawHLine(0, 50, 128);
  char ago[16]; timeAgo(n->recv, ago, sizeof(ago));
  u8g2.drawUTF8(3, 63, n->message[0] ? n->message : ago);
  if (unread > 0) {                                   // 右下未读药丸（反白）
    snprintf(line, sizeof(line), "未读%d", unread);
    int w = u8g2.getUTF8Width(line);
    u8g2.drawRBox(126 - w - 6, 52, w + 6, 12, 3);
    u8g2.setDrawColor(0);
    u8g2.drawUTF8(126 - w - 3, 62, line);
    u8g2.setDrawColor(1);
  }
}

// ============================================================================
//  丝滑菜单渲染（缓动光标 + 平滑滚动 + XOR 反白高亮条，借鉴 OLED_UI 观感）
// ============================================================================
static const char* MAIN_ITEMS[] = {"提示音", "蜂鸣强度", "震动强度", "操作反馈", "反馈音量", "反馈震动", "旋钮方向", "旋钮灵敏度", "静音", "熄屏", "模拟通知", "设备状态"};
static const int MAIN_N = sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0]);
static const char* mainName(int i) { return MAIN_ITEMS[i]; }
// 选项子列表（提示音 / 操作反馈 / 旋钮方向 / 旋钮灵敏度）——都用旋钮丝滑选择
static const char* FEEDBACK_OPTS[] = {"声音+震动", "只震动", "只声音", "无"};
static const char* ENCDIR_OPTS[]   = {"正向", "反向"};
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
    case SET_BUZVOL: return {"通知蜂鸣", true,  nullptr, 0};
    case SET_VIBVOL: return {"通知震动", true,  nullptr, 0};
    case SET_FBVOL:  return {"反馈音量", true,  nullptr, 0};
    case SET_FBVIB:  return {"反馈震动", true,  nullptr, 0};
    case SET_FBMODE: return {"操作反馈", false, FEEDBACK_OPTS, 4};
    case SET_ENCDIR: return {"旋钮方向", false, ENCDIR_OPTS,   2};
    default:         return {"旋钮灵敏度", false, ENCSENS_OPTS, 3};  // SET_ENCSENS
  }
}
static int setGet(SetId id) {
  switch (id) {
    case SET_BUZVOL: return buzzerVol;   case SET_VIBVOL: return vibVol;
    case SET_FBVOL:  return fbVol;       case SET_FBVIB:  return fbVibVol;
    case SET_FBMODE: return feedbackMode;
    case SET_ENCDIR: return encReversed ? 1 : 0;
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
    case SET_ENCDIR: encReversed = (v == 1); break;
    default:         encDetents = v + 1; break;      // SET_ENCSENS
  }
}

// 丝滑水平滑块：连续值(0-100)填充+thumb；离散值分段吸附点+缓动滑块+当前标签
static void renderSlider() {
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
                    if (n) renderNote(n); else renderIdle(); } break;
    default:        renderIdle();   break;
  }
  u8g2.sendBuffer();
}

// ============================================================================
//  操作反馈 + 触摸键（触摸键现在只作"返回"，带操作反馈）
// ============================================================================
// 操作反馈：按 feedbackMode 给声音/震动，用独立的 fbVol/fbVibVol（与通知音量分开）
static void opFeedback() {
  if (feedbackMode == 0 || feedbackMode == 1) vibrator.trigger(TAP_VIB, 1, fbVibVol);
  if (feedbackMode == 0 || feedbackMode == 2) buzzer.play(TAP_TONES, 1, fbVol);
}

// 触摸键按一下：给操作反馈 + 置返回标志（各界面语义在 handleInput 处理）
static void uiTouch() {
  opFeedback();
  uiBack = true;
}

// 触摸键：开机自动判定空闲电平（兼容高/低有效），去抖后每次「按下」触发一次 uiTouch()
static void handleTouch() {
  static int stable = -2, lastRaw = -2;
  static unsigned long tEdge = 0, lastFire = 0;
  if (touchIdleLevel < 0) touchIdleLevel = digitalRead(TOUCH_PIN);
  int raw = digitalRead(TOUCH_PIN);
  if (raw != lastRaw) { tEdge = millis(); lastRaw = raw; }
  if (raw != stable && millis() - tEdge > TOUCH_DEBOUNCE_MS) {
    stable = raw;
    if (raw != touchIdleLevel && millis() - lastFire > 250) {   // 按下沿
      uiTouch();
      lastFire = millis();
    }
  }
}

// ============================================================================
//  编码器 + 触摸 的 UI 输入路由（丝滑菜单状态机）
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
    buzzer.play(MELODIES[alertTone].seq, MELODIES[alertTone].len, 100);
#ifndef SIM_DEMO
    saveSettings();
#endif
    curSub = SUB_NONE;
    return;
  }
  switch (mainSel) {                           // 主菜单
    case 0:  enterSub(SUB_MELODY, alertTone); break;   // 提示音
    case 1:  enterSlider(SET_BUZVOL);  break;          // 蜂鸣强度
    case 2:  enterSlider(SET_VIBVOL);  break;          // 震动强度
    case 3:  enterSlider(SET_FBMODE);  break;          // 操作反馈
    case 4:  enterSlider(SET_FBVOL);   break;          // 反馈音量
    case 5:  enterSlider(SET_FBVIB);   break;          // 反馈震动
    case 6:  enterSlider(SET_ENCDIR);  break;          // 旋钮方向
    case 7:  enterSlider(SET_ENCSENS); break;          // 旋钮灵敏度
    case 8:  dnd = !dnd;                               // 静音开关
#ifndef SIM_DEMO
             saveSettings();
#endif
             needRender = true; break;
    case 9:  setScreen(false); uiState = UI_IDLE; break;   // 熄屏
    case 10: {                                         // 模拟通知
      Note n{};
      strlcpy(n.computer, "本机测试",   sizeof(n.computer));
      strlcpy(n.agent, "Claude",        sizeof(n.agent));
      strlcpy(n.conversation, "对话演示", sizeof(n.conversation));
      strlcpy(n.message, "这是一条模拟通知", sizeof(n.message));
      n.recv = millis();
      fireAlert(n);
    } break;
    case 11: uiState = UI_POPUP; break;                // 设备状态
  }
}

static void handleInput() {
  int step = encSteps();
  int btn  = encButton();
  bool back = uiBack; uiBack = false;
  if (step == 0 && btn == 0 && !back) return;

  lastInput = millis();
  if ((step != 0 || btn != 0) && uiState != UI_SLIDER) opFeedback();   // 旋钮转/按反馈（滑块页有自己的即时试声）
  if (screenOff) { setScreen(true); return; }    // 熄屏时任意输入先唤醒

  switch (uiState) {
    case UI_IDLE:
      if (step != 0 || btn == 1) { uiState = UI_MENU; curSub = SUB_NONE; mainAnim.sel = mainSel; }
      break;
    case UI_NOTE:
      if (btn || back) { unread = 0; viewOffset = -1; uiState = UI_IDLE; }
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
      if (btn == 2 || back) { if (curSub != SUB_NONE) curSub = SUB_NONE; else uiState = UI_IDLE; }
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
      if (btn || back) {
#ifndef SIM_DEMO
        saveSettings();
#endif
        uiState = UI_MENU;
      }
    } break;
    case UI_POPUP:
      if (btn || back || step) uiState = UI_MENU;
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

// 手机/电脑都能开的控制台
static void handleRoot() {   // 只读状态页（所有设置已移到设备菜单，用旋钮操作）
  String h = F("<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>AgentBell</title><style>"
    "body{font-family:system-ui,-apple-system,sans-serif;max-width:520px;margin:0 auto;padding:16px;background:#0b1020;color:#e7ecff}"
    "h1{font-size:20px}.card{background:#161c34;border:1px solid #2a3350;border-radius:12px;padding:14px;margin:12px 0}"
    ".muted{color:#9aa6cc;font-size:13px}ul{padding-left:18px}li{margin:6px 0}"
    "</style><h1>🔔 AgentBell</h1>");
  h += "<div class=card><b>设备状态</b><div class=muted>";
  h += String("IP ") + WiFi.localIP().toString()
     + " · 运行 " + String(millis() / 1000) + "s · 信号 " + String(WiFi.RSSI()) + "dBm";
  h += F("</div><div class=muted style='margin-top:8px'>所有设置请在设备上用旋钮操作。</div></div>");
  h += "<div class=card><b>最近通知（" + String(ringCount) + "）</b><ul>";
  for (int k = 0; k < ringCount; k++) {
    Note* n = noteByOffset(k);
    char ago[16]; timeAgo(n->recv, ago, sizeof(ago));
    h += "<li><b>" + String(n->agent) + "</b> · " + String(n->computer)
       + " · " + String(n->conversation) + " <span class=muted>(" + String(ago) + ")</span></li>";
  }
  if (ringCount == 0) h += F("<li class=muted>暂无</li>");
  h += F("</ul></div>");
  server.send(200, "text/html; charset=utf-8", h);
}
#endif

#ifdef INO_SIM
// ino-sim：每帧从 scenario 注入的 var 同步界面状态，方便预览各种屏幕。可用变量：
//   agent / computer / conversation / message  —— 设了 agent 就显示一条通知
//   buzzer / vib / dnd  (0|1)                   —— 状态指示（关铃/关震/勿扰）
//   unread N / history N                        —— 未读数 / 待机屏历史条数
// 不设 agent → 显示待机屏。
static void simRefresh() {
  buzzerEnabled = SIM_NUM("buzzer", 1) != 0;
  vibEnabled    = SIM_NUM("vib", 1) != 0;
  dnd           = SIM_NUM("dnd", 0) != 0;
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
  buzzer.begin(BUZZER_PIN);
  vibrator.begin(VIB_PIN, VIB_ACTIVE_HIGH, VIB_FREQ);
  pinMode(TOUCH_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(OLED_ADDR << 1);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setBusClock(700000);                  // 提高 I2C 速率 → 菜单动画更丝滑
  encBegin();                                // 旋转编码器（中断解码 + 按键）
  render();                                  // 先出一帧

#ifndef SIM_DEMO
  loadSettings();                            // 读回上次保存的开关状态

  // —— 连 WiFi（OLED 显进度）——
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);               // 关闭 WiFi 省电休眠 → 通知即时响应（USB 供电，不在乎功耗）
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    u8g2.clearBuffer();
    u8g2.setFont(FONT_CN);
    u8g2.drawUTF8(0, 24, "正在连接 WiFi");
    char dots[8] = ""; for (int d = 0; d < (i % 4); d++) strcat(dots, ".");
    u8g2.drawUTF8(0, 42, dots);
    u8g2.sendBuffer();
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
    // 开放（供 hook / 探活，无需登录）
    server.on("/notify",  handleNotify);      // 给电脑侧 hook 用（GET/POST 均可）
    server.on("/healthz", []() { server.send(200, "text/plain", "ok"); });
    server.on("/",        handleRoot);        // 只读状态页（设置全在设备菜单）
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    server.begin();
  } else {
    Serial.println("WiFi 连接失败，稍后可复位重试");
  }
#else
  // —— 仿真预览（Wokwi）：注入两条示例通知，直接看界面 ——
  // ino-sim 走 scenario 注入（见 simRefresh），故这里跳过硬编码 demo。
  #ifndef INO_SIM
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
  server.handleClient();
#endif
  buzzer.update();
  vibrator.update();
  handleTouch();
  handleInput();

  // 提示音子菜单：停留 ~300ms 自动试听
  if (uiState == UI_MENU && curSub == SUB_MELODY && subSel != melPreviewed && millis() - melSettleAt > 300) {
    melPreviewed = subSel;
    buzzer.play(MELODIES[subSel].seq, MELODIES[subSel].len, 100);
  }
  // 菜单/调节/弹窗闲置 15s 回待机
  if (uiState != UI_IDLE && uiState != UI_NOTE && millis() - lastInput > 15000) uiState = UI_IDLE;

  // 菜单动画期高刷（~45fps）；待机/通知 ~100ms（熄屏时 render 直接返回）
  unsigned long interval = (uiState == UI_MENU || uiState == UI_SLIDER || uiState == UI_POPUP) ? 22 : 100;
  if (needRender || millis() - lastRender > interval) {
    render();
    needRender = false;
    lastRender = millis();
  }
}
