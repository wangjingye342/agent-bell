// main.cpp — the simulator runtime entry point.
//
// Flow:
//   1. read the scenario file named on argv,
//   2. call the sketch's setup(),
//   3. execute scenario steps — setting inputs, stepping virtual time while
//      calling loop(), and snapshotting the screen to PNG frames,
//   4. write an index.html gallery of the frames.
//
// The sketch provides setup()/loop(); the GFX backend registers its canvas as
// sim::active_canvas during construction of the sketch's display object, which
// happens at static-init time (the `tft` global) or inside setup().
#include "../arduino_shim/sim_io.h"
#include "scenario.h"
#include "png_writer.h"
#include "html_viewer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Provided by the sketch (compiled as sketch.cpp).
extern void setup();
extern void loop();

#ifdef SIM_U8G2
// Provided by u8g2_sim_hal.cpp — returns the U8g2 monochrome full buffer + size.
extern "C" const unsigned char *sim_u8g2_buffer(int *w, int *h);
#endif

namespace {

// Time-stepping granularity. Small enough that debounce/long-press windows in
// typical sketches (tens to hundreds of ms) are sampled faithfully.
constexpr uint64_t STEP_US = 5000; // 5 ms

// Run loop() repeatedly while advancing virtual time by `dur_us` total.
void run_for(uint64_t dur_us) {
  uint64_t elapsed = 0;
  // Always run at least one loop() iteration even for a zero/short duration.
  do {
    loop();
    sim::advance_micros(STEP_US);
    elapsed += STEP_US;
  } while (elapsed < dur_us);
}

// Default date applied when the scenario `time` gives only a clock.
int g_year = 2026, g_mon = 1, g_day = 1;

// Parse "HH:MM[:SS]" or "YYYY-MM-DD HH:MM[:SS]" and push to the sim clock.
bool apply_time(const std::string &raw) {
  int Y = g_year, Mo = g_mon, D = g_day, h = 0, m = 0, s = 0;
  // Has a date part?
  if (raw.find('-') != std::string::npos) {
    if (sscanf(raw.c_str(), "%d-%d-%d %d:%d:%d", &Y, &Mo, &D, &h, &m, &s) < 5)
      return false;
    g_year = Y; g_mon = Mo; g_day = D; // remember as new default date
  } else {
    if (sscanf(raw.c_str(), "%d:%d:%d", &h, &m, &s) < 2) return false;
  }
  sim::set_base_time(Y, Mo, D, h, m, s);
  return true;
}

std::string basename_noext(const std::string &p) {
  size_t slash = p.find_last_of("/\\");
  std::string b = (slash == std::string::npos) ? p : p.substr(slash + 1);
  size_t dot = b.find_last_of('.');
  return (dot == std::string::npos) ? b : b.substr(0, dot);
}

} // namespace

int main(int argc, char **argv) {
  std::string scenario_path, out_dir = "out";
  int scale = 4;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
    else if (a == "--scale" && i + 1 < argc) scale = atoi(argv[++i]);
    else if (scenario_path.empty()) scenario_path = a;
  }
  if (scenario_path.empty()) {
    fprintf(stderr, "usage: %s <scenario> [--out DIR] [--scale N]\n", argv[0]);
    return 2;
  }

  std::ifstream in(scenario_path);
  if (!in) { fprintf(stderr, "cannot open scenario: %s\n", scenario_path.c_str()); return 2; }
  std::stringstream ss; ss << in.rdbuf();

  scn::Scenario sc;
  std::string err;
  if (!scn::parse(ss.str(), sc, err)) {
    fprintf(stderr, "scenario error: %s\n", err.c_str());
    return 2;
  }

  // Touch pin defaults to GPIO 1 (matches the demo sketch); overridable via the
  // `touchpin` command.
  int touch_pin = 1;

  std::vector<simhtml::Frame> frames;

  // Apply an input-setting step. Returns true if it handled the op (i.e. it was
  // an input, not a time-advancing action or snapshot).
  auto apply_input = [&](const scn::Step &st) -> bool {
    using scn::Op;
    switch (st.op) {
      case Op::Temp: sim::dht_temp = (float)st.num; return true;
      case Op::Hum: sim::dht_hum = (float)st.num; return true;
      case Op::Time:
        if (!apply_time(st.str)) {
          fprintf(stderr, "bad time: %s\n", st.str.c_str());
          exit(2);
        }
        return true;
      case Op::Pin: sim::set_pin(st.i0, st.i1); return true;
      case Op::Analog: sim::set_analog(st.i0, (int)st.num); return true;
      case Op::TouchPin: touch_pin = st.i0; return true;
      case Op::Title: sc.title = st.str; return true;
      case Op::Var: sim::set_var(st.str.c_str(), st.str2.c_str()); return true;
      case Op::Rotation: return true; // sketches normally call setRotation themselves
      default: return false;          // Run/Tap/Hold/Snap are actions
    }
  };

  // Pre-pass: apply every leading input step (those before the first action) so
  // the sketch sees the initial sensor/clock/mock state inside setup().
  size_t i = 0;
  for (; i < sc.steps.size(); i++) {
    if (!apply_input(sc.steps[i])) break;
  }

  // Run the sketch's setup(). The display object (often a global) is
  // constructed before or during this, registering sim::active_canvas.
  setup();

  // Execute the remaining steps in order.
  for (; i < sc.steps.size(); i++) {
    const auto &st = sc.steps[i];
    using scn::Op;
    if (apply_input(st)) continue;
    switch (st.op) {
      case Op::Run: run_for(st.dur_us); break;
      case Op::Tap:
        // Short press: assert touch, run for the press duration, release, then
        // run a little longer so the sketch sees the release edge + debounce.
        sim::set_pin(touch_pin, 1);
        run_for(st.dur_us);
        sim::set_pin(touch_pin, 0);
        run_for(120000); // 120 ms settle after release
        break;
      case Op::Hold:
        sim::set_pin(touch_pin, 1);
        run_for(st.dur_us);
        sim::set_pin(touch_pin, 0);
        run_for(120000);
        break;
      case Op::Snap: {
        std::string png = st.str + ".png";
        std::string full = out_dir + "/" + png;
        simhtml::Frame fr;
        fr.name = st.str;
        fr.png_file = png;
#ifdef SIM_U8G2
        {
          int uw = 0, uh = 0;
          const unsigned char *ub = sim_u8g2_buffer(&uw, &uh);
          if (ub && uw > 0 && uh > 0) {
            // OLED 观感：点亮像素=青白，背景=深蓝黑（与网页控制台配色呼应）。
            if (!simpng::write_mono_png(ub, uw, uh, 0xD6F0FF, 0x0B1020, full, scale)) {
              fprintf(stderr, "failed to write %s\n", full.c_str());
              return 3;
            }
            fr.logical_w = uw;
            fr.logical_h = uh;
            frames.push_back(fr);
            fprintf(stderr, "snap %-12s -> %s (%dx%d mono, %dx)\n", st.str.c_str(),
                    full.c_str(), uw, uh, scale);
            break;
          }
        }
#endif
        if (!sim::active_canvas) {
          fprintf(stderr, "snap '%s': no canvas registered (did the sketch "
                          "create its display object?)\n", st.str.c_str());
          return 3;
        }
        if (!simpng::write_canvas_png(*sim::active_canvas, full, scale)) {
          fprintf(stderr, "failed to write %s\n", full.c_str());
          return 3;
        }
        fr.logical_w = sim::active_canvas->w();
        fr.logical_h = sim::active_canvas->h();
        frames.push_back(fr);
        fprintf(stderr, "snap %-12s -> %s (%dx%d, %dx)\n", st.str.c_str(),
                full.c_str(), fr.logical_w, fr.logical_h, scale);
        break;
      }
      default: break; // inputs handled above
    }
  }

  if (!simhtml::write_index(out_dir, sc.title, frames)) {
    fprintf(stderr, "failed to write index.html\n");
    return 3;
  }

  // stdout = machine-readable summary the orchestrator can echo.
  printf("frames=%zu\n", frames.size());
  for (const auto &fr : frames)
    printf("frame %s %s %dx%d\n", fr.name.c_str(), fr.png_file.c_str(),
           fr.logical_w, fr.logical_h);
  printf("html=%s/index.html\n", out_dir.c_str());
  return 0;
}
