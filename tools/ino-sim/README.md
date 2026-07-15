# ino-sim — Arduino ST77xx / TFT screen simulator

Render an Arduino `.ino` sketch's TFT screen output to **PNG images + an HTML
gallery on your Mac**, so you (and the AI) can see exactly what the UI code draws
without flashing hardware.

Built for the ESP32-C3 + ST7735s (128×160) workflow, but works for any sketch
that draws through **Adafruit_GFX / Adafruit_ST7735 / Adafruit_ST7789** or
**TFT_eSPI**.

## How it works (why it's pixel-faithful)

The sketch is compiled on the host against thin Arduino shims, but its drawing is
redirected into an in-memory framebuffer using the **real Adafruit_GFX rendering
code** (vendored in `third_party/`). Because GFX draws *everything* — text,
lines, circles, the manual pixel arcs — through the same `drawPixel` path the
hardware uses, and `GFXcanvas16` applies the exact rotation transform the panel
applies via MADCTL, the rendered image matches the physical screen.

Both libraries share one engine: `GFXcanvas_fb` (extends the real
`GFXcanvas16`). `Adafruit_ST7735/ST7789` is a thin subclass; `TFT_eSPI` is an
API facade forwarding to the same engine — so common primitives render
identically across both.

```
.ino ──▶ simulate.py ──▶ clang++  (sketch + shims + real Adafruit_GFX + runtime)
                              │
                          framebuffer  ──▶ out/<frame>.png  +  out/index.html
                              ▲
                       scenario.scn  (sensor / time / touch inputs)
```

## Requirements

- macOS/Linux with `clang++` (or `g++`) and **zlib** (preinstalled on macOS).
- `python3`.

No Arduino toolchain or internet needed at run time. The canonical Adafruit_GFX
source is vendored under `third_party/Adafruit_GFX/` (BSD-licensed) for offline,
reproducible builds.

## Usage

```sh
# Capture all three pages of the demo sketch (uses scenarios/default.scn):
python3 tools/ino-sim/simulate.py sketch_jun18a/sketch_jun18a.ino

# Custom scenario, output dir, and upscale factor:
python3 tools/ino-sim/simulate.py path/to/sketch.ino \
    --scenario tools/ino-sim/scenarios/default.scn \
    --out tools/ino-sim/out --scale 4
```

Options:

| flag | default | meaning |
|------|---------|---------|
| `--scenario FILE` | `scenarios/default.scn` | scenario script to run |
| `--backend auto\|adafruit\|tft_espi` | `auto` | force a backend (auto-detected from `#include`s) |
| `--scale N` | `4` | integer nearest-neighbor upscale for the PNGs |
| `--out DIR` | `<tool>/out` | output directory |
| `--keep` | off | keep the generated `build/sketch.cpp` for debugging |

Outputs land in `out/`:
- `out/<name>.png` — one image per `snap`, upscaled `--scale`×. **Read these to
  see the UI.**
- `out/index.html` — a dark-themed gallery of all frames; open in a browser.

## Scenario DSL

A scenario is a plain-text script, one command per line; `#` starts a comment.
It sets inputs, advances simulated time while calling `loop()`, and snapshots the
screen. This lets you reach **any** UI state.

```
title My UI preview        # heading shown in index.html

touchpin 1                 # which GPIO the tap/hold commands drive (demo: 1)
temp 26.5                  # DHT.readTemperature() returns this (°C)
hum 60                     # DHT.readHumidity() returns this (%)
time 14:30:00              # NTP/getLocalTime clock; or "2026-06-19 14:30:00"
pin 3 high                 # set any input pin level (high/low/1/0)
analog 2 512               # set an analogRead() value

run 500ms                  # run loop() for 500 ms of simulated time
tap                        # short press touchpin (advance a page, etc.)
hold 1500ms                # long press (e.g. demo toggles the backlight)
snap climate               # capture the screen as out/climate.png
rotation 1                 # (rarely needed) force a rotation
```

Durations accept `us`, `ms`, `s`, or a bare number (= milliseconds). Time
advances **only** inside `run`/`tap`/`hold`, so `millis()`-based debounce and
refresh logic behave exactly as on hardware.

## Supported API

**Arduino core**: `Serial` (→ stderr), `millis/micros/delay`, `pinMode/
digitalRead/digitalWrite/analogRead`, `random`, `String`, `F()`/PROGMEM, math
macros, `Print::printf`.

**Libraries shimmed**: `Adafruit_GFX`, `Adafruit_ST7735`, `Adafruit_ST7789`,
`TFT_eSPI` (common drawing + text subset), `DHT`, `WiFi`, `time.h`
(`configTime`/`getLocalTime`).

**Not yet supported**: TFT_eSPI sprites (`TFT_eSprite`), smooth/anti-aliased &
custom-loaded fonts, true SPI timing. Sketches using those will fail to compile
or fall back to the GLCD font. The classic GLCD 5×7 font is fully supported and
matches hardware.

## Layout

```
simulate.py            orchestrator (ino→cpp, prototype-gen, compile, run)
scenarios/             scenario scripts (default.scn captures the 3 demo pages)
examples/              sample sketches (e.g. tft_espi_demo)
src/arduino_shim/      Arduino.h, Print, String, pgmspace, WiFi, DHT, time
src/gfx_backend/       GFXcanvas_fb engine + Adafruit_ST7735/ST7789 + colors
src/tft_espi_backend/  TFT_eSPI.h facade
src/runtime/           framebuffer PNG export, scenario parser, main, html viewer
third_party/Adafruit_GFX/  vendored canonical GFX (BSD)
build/ out/            generated (gitignored)
```

## Adding a sketch

Just point `simulate.py` at it. The orchestrator prepends `#include <Arduino.h>`
and auto-generates function prototypes (mirroring the Arduino IDE), and picks the
backend from the sketch's includes. Write a scenario that drives it to the states
you want to preview, then read the PNGs.
