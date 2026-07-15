// sim_io.h — shared simulator I/O state between the Arduino shim and the runtime.
//
// This header is deliberately tiny and free of Arduino macros (min/max/abs/...),
// so it can be included from BOTH worlds:
//   * the Arduino shim TUs (arduino.cpp, the sketch, the GFX backend), and
//   * the runtime TUs (main.cpp, scenario.cpp) which must stay STL-clean.
//
// It only pulls <cstdint> and forward-declares `struct tm`.
#pragma once
#include <cstdint>

struct tm;

namespace sim {

// ---- Simulated time -------------------------------------------------------
// Monotonic microsecond clock. millis()/micros() read it; delay() and the
// scenario `run` command advance it. Nothing advances time on its own.
extern uint64_t micros_now;
void advance_micros(uint64_t us);

// ---- Digital / analog pins ------------------------------------------------
// Input pins are driven by the scenario (set_pin); the sketch reads them with
// digitalRead. Output writes (digitalWrite) are recorded so the scenario can
// inspect them if desired.
int  read_pin(int pin);          // digitalRead
void write_pin(int pin, int val);// digitalWrite (records sketch output)
void set_pin(int pin, int val);  // external driver sets an input pin
int  read_analog(int pin);       // analogRead (settable, default 0)
void set_analog(int pin, int val);

// ---- DHT sensor -----------------------------------------------------------
extern float dht_temp; // degrees C returned by DHT.readTemperature()
extern float dht_hum;  // %RH returned by DHT.readHumidity()

// ---- Scenario variables (mock data injection) -----------------------------
// Arbitrary key/value pairs set by the scenario `var` command and read by the
// sketch (under INO_SIM) via sim_inject.h. This lets a sketch preview
// network-fetched data — outdoor weather, city name, AQI — without a network.
void set_var(const char *key, const char *value);
const char *get_var(const char *key, const char *defval); // interned, stable pointer
double get_var_num(const char *key, double defval);

// ---- Wall clock (NTP) -----------------------------------------------------
// The scenario sets a base wall-clock time; get_local_time() returns that base
// plus the simulated time elapsed since it was set. Treated as UTC internally
// to avoid host-timezone interference.
void set_base_time(int year, int mon, int day, int hour, int min, int sec);
bool get_local_time(struct tm *info);

// ---- Active drawing surface ----------------------------------------------
// The GFX backend registers its canvas here so the runtime can read pixels for
// PNG export without needing a handle to the sketch's `tft` global.
struct Canvas {
  virtual int w() const = 0;                       // logical width  (post-rotation)
  virtual int h() const = 0;                        // logical height (post-rotation)
  virtual uint16_t pixel(int x, int y) const = 0;   // logical-orientation RGB565
  virtual ~Canvas() {}
};
extern Canvas *active_canvas;

} // namespace sim
