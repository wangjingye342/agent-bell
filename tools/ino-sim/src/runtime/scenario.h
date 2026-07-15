// scenario.h — the scenario DSL: parsing + step model.
//
// A scenario is a small text script that drives the simulated sketch: it sets
// sensor/clock/pin inputs, advances time while calling loop(), and snapshots
// the screen to named PNG frames. Parsing lives here; execution lives in
// main.cpp (which owns setup()/loop() and the time-stepping).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace scn {

enum class Op {
  Temp,     // temp <float>            set DHT temperature (C)
  Hum,      // hum <float>             set DHT humidity (%)
  Time,     // time HH:MM[:SS] | YYYY-MM-DD HH:MM[:SS]   set wall clock
  Pin,      // pin <n> <high|low|0|1>  set an input pin level
  Analog,   // analog <n> <value>      set an analog pin value
  TouchPin, // touchpin <n>            which pin tap/hold drive (default 1)
  Run,      // run <dur>               run loop() for a duration (e.g. 300ms, 2s)
  Tap,      // tap [dur]               press touch pin, run, release (short press)
  Hold,     // hold [dur]              press touch pin for a long duration
  Snap,     // snap <name>             capture current screen as frame <name>
  Rotation, // rotation <0..3>         force a rotation (rarely needed)
  Title,    // title <text...>         set the HTML viewer title
  Var,      // var <key> <value...>    inject mock data (read via sim_inject.h)
};

struct Step {
  Op op;
  // Generic operands; only the relevant ones are populated per op.
  double num = 0;      // temp/hum/analog value
  int i0 = 0, i1 = 0;  // pin number / level / rotation
  uint64_t dur_us = 0; // duration for run/tap/hold
  std::string str;     // snap name / title text / raw time string / var key
  std::string str2;    // var value
};

struct Scenario {
  std::vector<Step> steps;
  std::string title = "ino-sim";
};

// Parse scenario text. On a syntax error, returns false and fills `err`.
bool parse(const std::string &text, Scenario &out, std::string &err);

// Parse a duration token like "300ms", "2s", "1500us", or a bare number
// (interpreted as milliseconds). Returns microseconds. Returns false if the
// token is malformed.
bool parse_duration(const std::string &tok, uint64_t &us);

} // namespace scn
