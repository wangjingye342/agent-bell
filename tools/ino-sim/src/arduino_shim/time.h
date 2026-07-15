// time.h — ESP32-style time shim.
//
// ESP32 sketches do `#include "time.h"` and then call configTime() and
// getLocalTime(&tm). On the host we still want the real struct tm and the C
// time functions, so we pull the system header via #include_next, then layer
// the two ESP extensions on top, backed by the scenario clock in sim_io.
//
// This file is found before the system <time.h> because the shim directory is
// first on the include path; #include_next continues the search past it.
#pragma once

#include_next <time.h>

#include "sim_io.h"

// configTime(gmtOffset_sec, daylightOffset_sec, ntpServer...) — on hardware
// this kicks off SNTP. In simulation the wall clock comes from the scenario,
// so this is a no-op (the offsets are already baked into the scenario time).
inline void configTime(long /*gmtOffset_sec*/, int /*daylightOffset_sec*/,
                       const char * /*server1*/, const char * /*server2*/ = nullptr,
                       const char * /*server3*/ = nullptr) {}

// configTzTime(tz, ntpServer...) variant — also a no-op here.
inline void configTzTime(const char * /*tz*/, const char * /*server1*/,
                         const char * /*server2*/ = nullptr,
                         const char * /*server3*/ = nullptr) {}

// getLocalTime(&tm, ms) — fills tm with scenario base time + elapsed sim time.
// Returns true (time always "available" in the sim).
inline bool getLocalTime(struct tm *info, uint32_t /*ms*/ = 5000) {
  return sim::get_local_time(info);
}
