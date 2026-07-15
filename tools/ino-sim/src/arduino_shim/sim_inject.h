// sim_inject.h — sketch-facing accessor for simulator mock data.
//
// Include this in a sketch to read values the scenario injected with `var`.
// Guard usage with INO_SIM so the same sketch compiles for real hardware:
//
//   #ifdef INO_SIM
//   #include <sim_inject.h>
//   #endif
//   ...
//   #ifdef INO_SIM
//     strcpy(weather.city, SIM_STR("city", "Beijing"));
//     weather.temp = SIM_NUM("out_temp", 20);
//   #else
//     fetchWeatherFromNetwork();
//   #endif
//
// Only available under INO_SIM (the simulator defines it on the command line).
#pragma once
#ifdef INO_SIM
#include "sim_io.h"

// String value for `key`, or `def` if the scenario didn't set it.
#define SIM_STR(key, def) (sim::get_var((key), (def)))
// Numeric value for `key`, or `def` if unset / unparseable.
#define SIM_NUM(key, def) (sim::get_var_num((key), (def)))
// True if the scenario set `key` at all.
#define SIM_HAS(key) (sim::get_var((key), nullptr) != nullptr)
#endif
