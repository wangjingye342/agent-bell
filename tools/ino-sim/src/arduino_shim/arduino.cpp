// arduino.cpp — host implementation of the Arduino runtime + simulator I/O.
//
// Holds the simulated clock, pin state, sensor values, and wall-clock base,
// and implements the timing/GPIO functions declared in Arduino.h. Everything
// here is single-threaded and advances only when the runtime tells it to.
#include "Arduino.h"
#include "sim_io.h"

#include <ctime>
#include <map>
#include <string>

// Serial routes to stderr (see HardwareSerial in Arduino.h).
HardwareSerial Serial;

namespace sim {

uint64_t micros_now = 0;
float dht_temp = 25.0f;
float dht_hum = 50.0f;
Canvas *active_canvas = nullptr;

namespace {
std::map<int, int> g_pins;      // pin -> level (HIGH/LOW)
std::map<int, int> g_analog;    // pin -> analog value
time_t g_base_epoch = 0;        // wall-clock base (UTC epoch seconds)
uint64_t g_base_micros = 0;     // sim micros when base was set
std::map<std::string, std::string> g_vars; // scenario variables (mock data)
} // namespace

void advance_micros(uint64_t us) { micros_now += us; }

int read_pin(int pin) {
  auto it = g_pins.find(pin);
  return it == g_pins.end() ? LOW : it->second;
}
void write_pin(int pin, int val) { g_pins[pin] = val ? HIGH : LOW; }
void set_pin(int pin, int val) { g_pins[pin] = val ? HIGH : LOW; }

int read_analog(int pin) {
  auto it = g_analog.find(pin);
  return it == g_analog.end() ? 0 : it->second;
}
void set_analog(int pin, int val) { g_analog[pin] = val; }

void set_var(const char *key, const char *value) {
  if (key) g_vars[key] = value ? value : "";
}
const char *get_var(const char *key, const char *defval) {
  if (!key) return defval;
  auto it = g_vars.find(key);
  return it == g_vars.end() ? defval : it->second.c_str();
}
double get_var_num(const char *key, double defval) {
  if (!key) return defval;
  auto it = g_vars.find(key);
  return it == g_vars.end() ? defval : std::atof(it->second.c_str());
}

void set_base_time(int year, int mon, int day, int hour, int min, int sec) {
  std::tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_sec = sec;
  t.tm_isdst = 0;
  // Interpret the scenario time as UTC so the host timezone never shifts it.
  // timegm() is POSIX/BSD-only; on Windows the CRT equivalent is _mkgmtime().
#ifdef _WIN32
  g_base_epoch = _mkgmtime(&t);
#else
  g_base_epoch = timegm(&t);
#endif
  g_base_micros = micros_now;
}

bool get_local_time(struct tm *info) {
  if (!info) return false;
  uint64_t elapsed_us = micros_now - g_base_micros;
  time_t now = g_base_epoch + (time_t)(elapsed_us / 1000000ULL);
  // gmtime_r() is POSIX-only; Windows' CRT provides gmtime_s (args reversed,
  // returns 0 on success). Both give a UTC breakdown, so the sim clock is
  // timezone-independent on every host.
#ifdef _WIN32
  if (gmtime_s(info, &now) != 0) return false;
#else
  gmtime_r(&now, info);
#endif
  return true;
}

} // namespace sim

// ---------------------------------------------------------------------------
// Arduino timing
// ---------------------------------------------------------------------------
unsigned long millis() { return (unsigned long)(sim::micros_now / 1000ULL); }
unsigned long micros() { return (unsigned long)(sim::micros_now); }

// In the sim, delay() advances virtual time rather than blocking. This keeps
// any timing the sketch relies on (debounce windows, refresh intervals)
// consistent with millis() without slowing the run.
void delay(unsigned long ms) { sim::advance_micros((uint64_t)ms * 1000ULL); }
void delayMicroseconds(unsigned int us) { sim::advance_micros((uint64_t)us); }
void yield() {}

// ---------------------------------------------------------------------------
// Arduino GPIO
// ---------------------------------------------------------------------------
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t val) { sim::write_pin(pin, val); }
int digitalRead(uint8_t pin) { return sim::read_pin(pin); }
int analogRead(uint8_t pin) { return sim::read_analog(pin); }
void analogWrite(uint8_t, int) {}

// ---------------------------------------------------------------------------
// Arduino random
// ---------------------------------------------------------------------------
long random(long howbig) {
  if (howbig <= 0) return 0;
  return (long)(rand() % howbig);
}
long random(long howsmall, long howbig) {
  if (howsmall >= howbig) return howsmall;
  return howsmall + random(howbig - howsmall);
}
void randomSeed(unsigned long seed) { srand((unsigned)seed); }
