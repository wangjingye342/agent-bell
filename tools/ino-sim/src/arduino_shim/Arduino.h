// Arduino.h — host shim for the Arduino core API.
//
// Just enough of the Arduino runtime to compile and run a typical display
// sketch on the desktop: Print/Stream, a minimal String, the math helper
// macros, GPIO + timing functions, and a Serial object routed to stderr.
//
// Drawing is NOT here — that lives in the GFX backend. This file is about the
// non-graphics surface a sketch touches (Serial.begin, millis, pinMode, ...).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <string>

#include "pgmspace.h"
#include "sim_io.h"

// ---------------------------------------------------------------------------
// Tell Adafruit_GFX we're an ESP32 so it takes the ESP code paths (pgmspace
// include, etc.). We provide a matching <pgmspace.h> on the include path.
// ---------------------------------------------------------------------------
#ifndef ESP32
#define ESP32 1
#endif
#define ARDUINO 10819

// ---------------------------------------------------------------------------
// Math / utility macros. Defined as functions where possible to avoid the
// classic double-evaluation bugs, but kept macro-compatible with sketches.
// ---------------------------------------------------------------------------
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef HALF_PI
#define HALF_PI 1.5707963267948966192313216916398
#endif
#ifndef TWO_PI
#define TWO_PI 6.283185307179586476925286766559
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef abs
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
#ifndef radians
#define radians(deg) ((deg) * DEG_TO_RAD)
#endif
#ifndef degrees
#define degrees(rad) ((rad) * RAD_TO_DEG)
#endif
#ifndef sq
#define sq(x) ((x) * (x))
#endif
#ifndef _BV
#define _BV(bit) (1UL << (bit))
#endif

inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  if (in_max == in_min) return out_min;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// MinGW (Windows) 没有 BSD 的 strlcpy；有的 sketch（如 agent_bell）用到它，补一个。
// mac / 新版 glibc 自带，故仅在 Windows 上定义，避免与系统声明冲突。
#ifdef _WIN32
inline size_t strlcpy(char *dst, const char *src, size_t size) {
  size_t sl = strlen(src);
  if (size) { size_t n = (sl >= size) ? size - 1 : sl; memcpy(dst, src, n); dst[n] = '\0'; }
  return sl;
}
#endif

// ---------------------------------------------------------------------------
// Pin / digital constants
// ---------------------------------------------------------------------------
#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x0
#define OUTPUT 0x1
#define INPUT_PULLUP 0x2
#define INPUT_PULLDOWN 0x3
#define LSBFIRST 0
#define MSBFIRST 1
#define CHANGE 1
#define FALLING 2
#define RISING 3

typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int word;

// ---------------------------------------------------------------------------
// Timing / GPIO — implemented in arduino.cpp against sim_io state.
// ---------------------------------------------------------------------------
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int val);

// 中断开关：主机端单线程仿真，无真中断，做成空操作即可
inline void noInterrupts() {}
inline void interrupts() {}

long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);

// ESP32 helpers occasionally used by sketches
inline uint32_t esp_random() { return (uint32_t)rand(); }

// ---------------------------------------------------------------------------
// __FlashStringHelper — F() returns a plain const char* on the host, so make
// the type an alias the Print overloads can still distinguish.
// ---------------------------------------------------------------------------
class __FlashStringHelper;

// ---------------------------------------------------------------------------
// Print / Stream — the base used by Adafruit_GFX (public Print) and Serial.
// write(uint8_t) is the single virtual everything funnels through; subclasses
// (GFX) override it to render glyphs.
// ---------------------------------------------------------------------------
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class String;  // fwd

class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    while (size--) { if (write(*buffer++)) n++; else break; }
    return n;
  }
  size_t write(const char *str) {
    if (!str) return 0;
    return write((const uint8_t *)str, strlen(str));
  }
  size_t write(const char *buffer, size_t size) {
    return write((const uint8_t *)buffer, size);
  }

  size_t print(const char *s) { return write(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(const std::string &s) { return write(s.c_str()); }
  size_t print(int n, int base = DEC) { return printNumber((long)n, base); }
  size_t print(unsigned int n, int base = DEC) { return printNumber((unsigned long)n, base); }
  size_t print(long n, int base = DEC) { return printNumber(n, base); }
  size_t print(unsigned long n, int base = DEC) { return printNumber(n, base); }
  size_t print(double n, int digits = 2) { return printFloat(n, digits); }
  size_t print(const __FlashStringHelper *s) { return write((const char *)s); }

  size_t println() { return write("\r\n"); }
  size_t println(const char *s) { size_t n = print(s); return n + println(); }
  size_t println(char c) { size_t n = print(c); return n + println(); }
  size_t println(const std::string &s) { size_t n = print(s); return n + println(); }
  size_t println(int n, int base = DEC) { size_t k = print(n, base); return k + println(); }
  size_t println(unsigned int n, int base = DEC) { size_t k = print(n, base); return k + println(); }
  size_t println(long n, int base = DEC) { size_t k = print(n, base); return k + println(); }
  size_t println(unsigned long n, int base = DEC) { size_t k = print(n, base); return k + println(); }
  size_t println(double n, int digits = 2) { size_t k = print(n, digits); return k + println(); }
  size_t println(const __FlashStringHelper *s) { size_t n = print(s); return n + println(); }

  // ESP32 / Teensy style printf — used by the demo sketch (tft.printf).
  size_t printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0) return 0;
    if ((size_t)len < sizeof(buf)) return write((const uint8_t *)buf, (size_t)len);
    // Long output: allocate exactly.
    std::string big((size_t)len, '\0');
    va_start(ap, fmt);
    vsnprintf(&big[0], (size_t)len + 1, fmt, ap);
    va_end(ap);
    return write((const uint8_t *)big.data(), (size_t)len);
  }

private:
  size_t printNumber(long n, int base) {
    char buf[34];
    const char *fmt = (base == HEX) ? "%lx" : (base == OCT) ? "%lo" : "%ld";
    if (base == BIN) { return printBinary((unsigned long)n); }
    int len = snprintf(buf, sizeof(buf), fmt, n);
    return write((const uint8_t *)buf, len < 0 ? 0 : (size_t)len);
  }
  size_t printNumber(unsigned long n, int base) {
    char buf[34];
    const char *fmt = (base == HEX) ? "%lx" : (base == OCT) ? "%lo" : "%lu";
    if (base == BIN) { return printBinary(n); }
    int len = snprintf(buf, sizeof(buf), fmt, n);
    return write((const uint8_t *)buf, len < 0 ? 0 : (size_t)len);
  }
  size_t printBinary(unsigned long n) {
    char buf[33]; int i = 32; buf[32] = '\0';
    if (n == 0) { return write((uint8_t)'0'); }
    while (n && i) { buf[--i] = '0' + (n & 1); n >>= 1; }
    return write(buf + i, (size_t)(32 - i));
  }
  size_t printFloat(double n, int digits) {
    char buf[40];
    int len = snprintf(buf, sizeof(buf), "%.*f", digits, n);
    return write((const uint8_t *)buf, len < 0 ? 0 : (size_t)len);
  }
};

// Stream adds the input side of the API. Sketches rarely read in a sim, so the
// readers are stubs returning "no data".
class Stream : public Print {
public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  void flush() {}
};

// ---------------------------------------------------------------------------
// HardwareSerial — prints route to stderr so sketch debug output is visible to
// the AI/user without polluting stdout (which the runtime may use).
// ---------------------------------------------------------------------------
class HardwareSerial : public Stream {
public:
  void begin(unsigned long) {}
  void begin(unsigned long, int) {}
  void end() {}
  operator bool() const { return true; }
  size_t write(uint8_t c) override { fputc(c, stderr); return 1; }
  using Print::write;
};
extern HardwareSerial Serial;

#include "WString.h"
