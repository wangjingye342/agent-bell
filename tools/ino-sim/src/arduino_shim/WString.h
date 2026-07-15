// WString.h — a minimal Arduino String for the host.
//
// Backed by std::string. Covers construction from C-strings/numbers and the
// handful of methods display sketches typically use (length, c_str, indexing,
// concatenation, comparison). Not a full reimplementation of Arduino String.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>  // strtol, atof — don't rely on Arduino.h's transitive include
#include <string>

class String {
public:
  String() {}
  String(const char *s) : buf(s ? s : "") {}
  String(const std::string &s) : buf(s) {}
  String(char c) : buf(1, c) {}
  String(const String &o) : buf(o.buf) {}
  explicit String(int n, int base = 10) { fromInt((long)n, base); }
  explicit String(long n, int base = 10) { fromInt(n, base); }
  explicit String(unsigned int n, int base = 10) { fromUInt((unsigned long)n, base); }
  explicit String(unsigned long n, int base = 10) { fromUInt(n, base); }
  explicit String(double n, int digits = 2) {
    char t[40]; snprintf(t, sizeof(t), "%.*f", digits, n); buf = t;
  }
  explicit String(float n, int digits = 2) {
    char t[40]; snprintf(t, sizeof(t), "%.*f", digits, n); buf = t;
  }

  unsigned int length() const { return (unsigned int)buf.size(); }
  const char *c_str() const { return buf.c_str(); }
  char charAt(unsigned int i) const { return i < buf.size() ? buf[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }

  String &operator=(const char *s) { buf = s ? s : ""; return *this; }
  String &operator=(const String &o) { buf = o.buf; return *this; }

  String operator+(const String &o) const { return String(buf + o.buf); }
  String operator+(const char *s) const { return String(buf + (s ? s : "")); }
  String &operator+=(const String &o) { buf += o.buf; return *this; }
  String &operator+=(const char *s) { if (s) buf += s; return *this; }
  String &operator+=(char c) { buf += c; return *this; }

  bool operator==(const String &o) const { return buf == o.buf; }
  bool operator==(const char *s) const { return buf == (s ? s : ""); }
  bool operator!=(const String &o) const { return !(*this == o); }

  int toInt() const { return (int)strtol(buf.c_str(), nullptr, 10); }
  float toFloat() const { return (float)atof(buf.c_str()); }

  // Implicit conversion lets these flow into snprintf("%s", ...) style sites
  // and any code expecting a const char*.
  operator const char *() const { return buf.c_str(); }

private:
  void fromInt(long n, int base) {
    char t[34];
    const char *f = (base == 16) ? "%lx" : (base == 8) ? "%lo" : "%ld";
    snprintf(t, sizeof(t), f, n); buf = t;
  }
  void fromUInt(unsigned long n, int base) {
    char t[34];
    const char *f = (base == 16) ? "%lx" : (base == 8) ? "%lo" : "%lu";
    snprintf(t, sizeof(t), f, n); buf = t;
  }
  std::string buf;
};
