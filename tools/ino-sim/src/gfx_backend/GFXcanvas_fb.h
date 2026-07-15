// GFXcanvas_fb.h — the rendering engine.
//
// Adafruit_GFX already ships GFXcanvas16: a full RGB565 in-memory canvas whose
// drawPixel/fillScreen/fast-line implementations are the SAME code that runs on
// hardware, and whose drawPixel applies the exact rotation transform the real
// panel applies via MADCTL. So the engine is just GFXcanvas16 plus a bridge
// that lets the runtime read pixels back in logical (post-rotation) orientation
// for PNG export. Reusing the real canvas is what makes the output
// pixel-faithful with zero drawing code of our own.
//
// Both display facades — Adafruit_ST7735/ST7789 and TFT_eSPI — render through
// an instance of this class, so shared primitives are byte-identical between
// libraries.
#pragma once
#include "Adafruit_GFX.h"
#include "sim_io.h"

class GFXcanvas_fb : public GFXcanvas16, public sim::Canvas {
public:
  // Constructed with the panel's NATIVE (rotation-0) dimensions, e.g. 128x160
  // for an ST7735s. setRotation() then swaps the logical width/height exactly
  // as on hardware. Registers itself as the active canvas for the runtime.
  GFXcanvas_fb(int16_t native_w, int16_t native_h)
      : GFXcanvas16((uint16_t)native_w, (uint16_t)native_h) {
    fillScreen(0);
    sim::active_canvas = this;
  }

  // sim::Canvas — report logical (post-rotation) geometry and pixels. width()/
  // height() come from Adafruit_GFX and already account for rotation, and
  // getPixel() takes logical coordinates and undoes the rotation internally, so
  // iterating 0..w()-1 x 0..h()-1 yields exactly what the panel displays.
  int w() const override { return const_cast<GFXcanvas_fb *>(this)->width(); }
  int h() const override { return const_cast<GFXcanvas_fb *>(this)->height(); }
  uint16_t pixel(int x, int y) const override {
    return const_cast<GFXcanvas_fb *>(this)->getPixel((int16_t)x, (int16_t)y);
  }

  // Convenience matching the hardware driver API. RGB888 -> RGB565.
  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
};
