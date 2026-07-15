// Wire.h — 极简 I2C 桩（ino-sim 主机端）。
//
// 本项目 sketch 只用 Wire.begin(sda, scl) 指定 OLED 的 I2C 引脚；仿真里不做真实
// I2C（U8g2 的字节回调也是桩），所以这里全是空操作。够编译、够运行即可。
#pragma once
#include <cstdint>
#include <cstddef>

class TwoWire {
public:
  void begin() {}
  void begin(int sda, int scl) { (void)sda; (void)scl; }
  void begin(uint8_t sda, uint8_t scl) { (void)sda; (void)scl; }
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t) {}
  void beginTransmission(int) {}
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t *, size_t n) { return n; }
  uint8_t endTransmission(bool = true) { return 0; }
  uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
  int available() { return 0; }
  int read() { return -1; }
};

// C++17 inline 变量：多个翻译单元包含也只有一个定义，无需单独 .cpp。
inline TwoWire Wire;
