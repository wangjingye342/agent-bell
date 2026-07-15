// DHT.h — DHT temperature/humidity sensor shim.
//
// Returns the values configured by the scenario (sim::dht_temp / dht_hum)
// instead of reading a real wire. The constructor signature matches Adafruit's
// DHT library: DHT(pin, type[, count]).
#pragma once
#include "Arduino.h"
#include "sim_io.h"

#define DHT11 11
#define DHT12 12
#define DHT21 21
#define DHT22 22
#define AM2301 21

class DHT {
public:
  DHT(uint8_t pin = 0, uint8_t type = DHT11, uint8_t count = 6)
      : _pin(pin), _type(type), _count(count) {}
  void begin(uint8_t = 55) {}

  // force/S parameters exist in the real API; ignored here.
  float readTemperature(bool isFahrenheit = false, bool = false) {
    float c = sim::dht_temp;
    return isFahrenheit ? (c * 1.8f + 32.0f) : c;
  }
  float readHumidity(bool = false) { return sim::dht_hum; }

  float computeHeatIndex(float t, float h, bool isFahrenheit = true) {
    // Simplified passthrough; sketches rarely render this in a UI sim.
    (void)h; return isFahrenheit ? t : t;
  }
  bool read(bool = false) { return true; }

private:
  uint8_t _pin, _type, _count;
};
