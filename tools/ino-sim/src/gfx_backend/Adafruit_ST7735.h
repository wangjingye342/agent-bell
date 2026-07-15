// Adafruit_ST7735.h — drop-in shim for the Adafruit ST7735 driver.
//
// A thin subclass of the rendering engine (GFXcanvas_fb). It reproduces the
// constructor signatures real sketches use and turns every hardware control
// call (initR, setSPISpeed, enableDisplay, ...) into a no-op. All drawing is
// inherited from GFXcanvas_fb -> GFXcanvas16 -> Adafruit_GFX, i.e. the real
// rendering code, so output matches the panel.
//
// Native panel size is 128x160 (rotation 0), matching ST7735_TFTWIDTH_128 /
// ST7735_TFTHEIGHT_160. setRotation(1) yields the 160x128 landscape the demo
// sketch uses.
#pragma once
#include "GFXcanvas_fb.h"
#include "st77xx_constants.h"

#define ST7735_TFTWIDTH_128 128
#define ST7735_TFTHEIGHT_160 160
#define ST7735_TFTHEIGHT_128 128

// Minimal SPIClass placeholder so the selectable-hardware-SPI constructor
// signature compiles when a sketch passes &SPI.
class SPIClass;

class Adafruit_ST77xx : public GFXcanvas_fb {
public:
  Adafruit_ST77xx(int16_t w, int16_t h) : GFXcanvas_fb(w, h) {}

  // Control API — inert in simulation (state recorded where useful).
  void setSPISpeed(uint32_t) {}
  void enableDisplay(bool) {}
  void enableTearing(bool) {}
  void enableSleep(bool) {}
  // invertDisplay is virtual in Adafruit_GFX; keep it a no-op here.
  void invertDisplay(bool) override {}
  void sendCommand(uint8_t, const uint8_t * = nullptr, uint8_t = 0) {}
};

class Adafruit_ST7735 : public Adafruit_ST77xx {
public:
  // Software SPI: (cs, dc, mosi, sclk, rst) — the demo sketch's form.
  Adafruit_ST7735(int8_t /*cs*/, int8_t /*dc*/, int8_t /*mosi*/, int8_t /*sclk*/,
                  int8_t /*rst*/)
      : Adafruit_ST77xx(ST7735_TFTWIDTH_128, ST7735_TFTHEIGHT_160) {}
  // Default hardware SPI: (cs, dc, rst)
  Adafruit_ST7735(int8_t /*cs*/, int8_t /*dc*/, int8_t /*rst*/)
      : Adafruit_ST77xx(ST7735_TFTWIDTH_128, ST7735_TFTHEIGHT_160) {}
  // Selectable hardware SPI: (spi, cs, dc, rst)
  Adafruit_ST7735(SPIClass * /*spi*/, int8_t /*cs*/, int8_t /*dc*/, int8_t /*rst*/)
      : Adafruit_ST77xx(ST7735_TFTWIDTH_128, ST7735_TFTHEIGHT_160) {}

  // Init entry points — no-ops; the canvas is ready to draw immediately.
  void initR(uint8_t /*options*/ = INITR_GREENTAB) {}
  void initB() {}
  void setColRowStart(int8_t, int8_t) {}
};
