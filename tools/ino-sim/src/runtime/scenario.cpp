// scenario.cpp — parser for the scenario DSL.
//
// Line-oriented: one command per line, '#' begins a comment, blank lines are
// ignored. Tokens are whitespace-separated. Kept dependency-free (no Arduino
// headers) so it links cleanly into the runtime.
#include "scenario.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace scn {

bool parse_duration(const std::string &tok, uint64_t &us) {
  if (tok.empty()) return false;
  // Split trailing unit letters from the leading number.
  size_t i = 0;
  while (i < tok.size() && (std::isdigit((unsigned char)tok[i]) ||
                            tok[i] == '.' || tok[i] == '+' || tok[i] == '-')) {
    i++;
  }
  std::string num = tok.substr(0, i);
  std::string unit = tok.substr(i);
  if (num.empty()) return false;
  double v = std::atof(num.c_str());
  if (v < 0) return false;

  if (unit == "us")          us = (uint64_t)(v);
  else if (unit == "ms")     us = (uint64_t)(v * 1000.0);
  else if (unit == "s")      us = (uint64_t)(v * 1000000.0);
  else if (unit.empty())     us = (uint64_t)(v * 1000.0); // bare number = ms
  else return false;
  return true;
}

namespace {

bool parse_level(const std::string &t, int &out) {
  if (t == "high" || t == "HIGH" || t == "1") { out = 1; return true; }
  if (t == "low" || t == "LOW" || t == "0") { out = 0; return true; }
  return false;
}

// Accept "HH:MM", "HH:MM:SS", "YYYY-MM-DD HH:MM[:SS]". Stored raw; main.cpp
// resolves it against a default date. Here we just keep the original string and
// validate it parses to numbers later. To keep the Step model simple, we stash
// the components in i0..i1 + num where practical, but for time we keep the raw
// string and let main.cpp interpret it.
} // namespace

bool parse(const std::string &text, Scenario &out, std::string &err) {
  std::istringstream in(text);
  std::string line;
  int lineno = 0;

  while (std::getline(in, line)) {
    lineno++;
    // Strip comments.
    size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);

    std::istringstream ls(line);
    std::string cmd;
    if (!(ls >> cmd)) continue; // blank
    for (auto &c : cmd) c = (char)std::tolower((unsigned char)c);

    Step s;
    auto fail = [&](const std::string &m) {
      err = "line " + std::to_string(lineno) + ": " + m;
      return false;
    };

    if (cmd == "temp") {
      if (!(ls >> s.num)) return fail("temp needs a number");
      s.op = Op::Temp;
    } else if (cmd == "hum") {
      if (!(ls >> s.num)) return fail("hum needs a number");
      s.op = Op::Hum;
    } else if (cmd == "time") {
      std::string rest, tok;
      while (ls >> tok) { if (!rest.empty()) rest += " "; rest += tok; }
      if (rest.empty()) return fail("time needs HH:MM[:SS] or a full date");
      s.op = Op::Time;
      s.str = rest;
    } else if (cmd == "pin") {
      std::string lvl;
      if (!(ls >> s.i0) || !(ls >> lvl)) return fail("pin needs <n> <high|low>");
      if (!parse_level(lvl, s.i1)) return fail("pin level must be high/low/0/1");
      s.op = Op::Pin;
    } else if (cmd == "analog") {
      if (!(ls >> s.i0) || !(ls >> s.num)) return fail("analog needs <n> <value>");
      s.op = Op::Analog;
    } else if (cmd == "touchpin") {
      if (!(ls >> s.i0)) return fail("touchpin needs a pin number");
      s.op = Op::TouchPin;
    } else if (cmd == "run") {
      std::string d;
      if (!(ls >> d) || !parse_duration(d, s.dur_us)) return fail("run needs a duration");
      s.op = Op::Run;
    } else if (cmd == "tap") {
      std::string d;
      s.dur_us = 120000; // default 120ms press
      if (ls >> d) { if (!parse_duration(d, s.dur_us)) return fail("bad tap duration"); }
      s.op = Op::Tap;
    } else if (cmd == "hold") {
      std::string d;
      s.dur_us = 1500000; // default 1.5s press
      if (ls >> d) { if (!parse_duration(d, s.dur_us)) return fail("bad hold duration"); }
      s.op = Op::Hold;
    } else if (cmd == "snap") {
      if (!(ls >> s.str)) return fail("snap needs a frame name");
      s.op = Op::Snap;
    } else if (cmd == "rotation") {
      if (!(ls >> s.i0)) return fail("rotation needs 0..3");
      s.op = Op::Rotation;
    } else if (cmd == "title") {
      std::string rest, tok;
      while (ls >> tok) { if (!rest.empty()) rest += " "; rest += tok; }
      s.op = Op::Title;
      s.str = rest;
    } else if (cmd == "var") {
      if (!(ls >> s.str)) return fail("var needs <key> <value>");
      std::string rest, tok;
      while (ls >> tok) { if (!rest.empty()) rest += " "; rest += tok; }
      s.op = Op::Var;
      s.str2 = rest; // value may be empty (clears the key)
    } else {
      return fail("unknown command '" + cmd + "'");
    }

    out.steps.push_back(std::move(s));
  }
  return true;
}

} // namespace scn
