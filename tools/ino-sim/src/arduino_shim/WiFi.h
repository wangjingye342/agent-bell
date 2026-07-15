// WiFi.h — no-op WiFi shim.
//
// Display sketches include WiFi mainly to bring the network up before NTP.
// In simulation there's no network; these calls succeed instantly and time is
// provided by the time.h shim from the scenario.
#pragma once
#include "Arduino.h"

#define WL_CONNECTED 3
#define WL_IDLE_STATUS 0
#define WL_DISCONNECTED 6

class WiFiClass {
public:
  void begin() {}
  void begin(const char *) {}
  void begin(const char *, const char *) {}
  void mode(int) {}
  int status() { return WL_CONNECTED; }
  void disconnect(bool = false) {}
  bool isConnected() { return true; }
  String localIP() { return String("192.168.0.123"); }
  String macAddress() { return String("DE:AD:BE:EF:00:01"); }
  int RSSI() { return -55; }
  void setSleep(bool) {}
};
extern WiFiClass WiFi;

#define WIFI_STA 1
#define WIFI_AP 2
