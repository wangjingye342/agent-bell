// Adafruit_ST7789.h — drop-in shim for the Adafruit ST7789 driver.
//
// Same pattern as the ST7735 shim: a thin subclass of GFXcanvas_fb with inert
// control calls. The native size is provided by the constructor (ST7789 panels
// come in 240x240, 240x320, 135x240, etc.), so the sketch's chosen dimensions
// flow straight through to the canvas.
#pragma once
#include "GFXcanvas_fb.h"
#include "st77xx_constants.h"
#include "Adafruit_ST7735.h"  // for Adafruit_ST77xx + SPIClass placeholder

class Adafruit_ST7789 : public Adafruit_ST77xx {
public:
  Adafruit_ST7789(int8_t /*cs*/, int8_t /*dc*/, int8_t /*mosi*/, int8_t /*sclk*/,
                  int8_t /*rst*/)
      : Adafruit_ST77xx(240, 320) {}
  Adafruit_ST7789(int8_t /*cs*/, int8_t /*dc*/, int8_t /*rst*/)
      : Adafruit_ST77xx(240, 320) {}
  Adafruit_ST7789(SPIClass * /*spi*/, int8_t /*cs*/, int8_t /*dc*/, int8_t /*rst*/)
      : Adafruit_ST77xx(240, 320) {}

  // init(w, h[, mode]) sets the active resolution for the chosen panel.
  void init(uint16_t width, uint16_t height, uint8_t /*spiMode*/ = 0) {
    resize(width, height);
  }

private:
  // Re-create the backing buffer at a new native size. Only valid before
  // drawing; ST7789 sketches call init() in setup() exactly once.
  void resize(uint16_t w, uint16_t h) {
    this->WIDTH = (int16_t)w;
    this->HEIGHT = (int16_t)h;
    this->_width = (int16_t)w;
    this->_height = (int16_t)h;
    if (buffer) free(buffer);
    buffer = (uint16_t *)malloc((size_t)w * h * 2);
    if (buffer) memset(buffer, 0, (size_t)w * h * 2);
  }
};
