// u8g2_sim_hal.cpp — U8g2 在 ino-sim 主机端的硬件抽象桩。
//
// 真机上 U8g2 通过 I2C/GPIO 回调把全缓冲推给 SSD1306；仿真里我们不做真实 I2C，
// 而是让 U8g2 照常把画面画进它自己的**全缓冲**（_F_ 全缓冲模式，1KB），画完后
// 直接读那块缓冲导出 PNG。所以这两个回调只需返回成功即可。
//
// U8g2lib.h 里 HW-I2C 构造函数会取这两个符号的地址（它们本在未 vendor 的
// U8x8lib.cpp 里），故在此提供。包含 U8x8lib.h 以精确匹配其签名/链接。
#include "U8x8lib.h"
#include "u8g2.h"

// u8x8_t 是 u8g2_t 的首个成员，故 (u8g2_t*)u8x8 即外层 u8g2 指针。首次回调时捕获，
// 供 main.cpp 快照时读取全缓冲。
static u8g2_t *g_u8g2 = 0;
static inline void capture(u8x8_t *u8x8) { if (!g_u8g2) g_u8g2 = (u8g2_t *)u8x8; }

uint8_t u8x8_byte_arduino_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)msg; (void)arg_int; (void)arg_ptr;
  capture(u8x8);
  return 1;
}
uint8_t u8x8_gpio_and_delay_arduino(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
  (void)msg; (void)arg_int; (void)arg_ptr;
  capture(u8x8);
  return 1;
}

// U8G2 的 HW-I2C 构造函数会调它记录 reset/clock/data 引脚（本在未 vendor 的
// U8x8lib.cpp 里）。仿真只需把 reset 记进结构体即可。
void u8x8_SetPin_HW_I2C(u8x8_t *u8x8, uint8_t reset, uint8_t clock, uint8_t data) {
  u8x8_SetPin(u8x8, U8X8_PIN_RESET, reset);
  (void)clock; (void)data;
}

// 给 runtime(main.cpp) 用：返回单色全缓冲指针 + 尺寸。缓冲按 SSD1306 页格式排布：
// 字节 buf[(y/8)*W + x] 的 bit(y%8) 即像素 (x,y)。C 链接，方便 C++ 直接调用。
extern "C" const unsigned char *sim_u8g2_buffer(int *w, int *h) {
  if (!g_u8g2) { if (w) *w = 0; if (h) *h = 0; return 0; }
  if (w) *w = (int)u8g2_GetDisplayWidth(g_u8g2);
  if (h) *h = (int)u8g2_GetDisplayHeight(g_u8g2);
  return u8g2_GetBufferPtr(g_u8g2);
}
