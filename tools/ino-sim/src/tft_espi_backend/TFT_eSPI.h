// TFT_eSPI.h — compatibility facade for the TFT_eSPI API.
//
// TFT_eSPI is not an Adafruit_GFX subclass, but its drawing primitives have the
// same semantics. This facade owns a GFXcanvas_fb (the shared rendering engine)
// and forwards every call to it, so a TFT_eSPI sketch and an Adafruit_GFX
// sketch that draw the same shapes produce byte-identical pixels.
//
// Scope (v1): the common immediate-mode drawing + text API. NOT yet supported:
// sprites (TFT_eSprite), smooth/anti-aliased fonts, custom loaded fonts, and
// DMA. Those are documented as a later add-on. The default GLCD font (font 1)
// is the same 5x7 glyph set Adafruit_GFX uses, so text matches.
//
// Panel size defaults to 128x160 (ST7735s). Override by #defining TFT_WIDTH /
// TFT_HEIGHT before including, or by passing dimensions to the constructor.
#pragma once
#include "Arduino.h"
#include "../gfx_backend/GFXcanvas_fb.h"

#ifndef TFT_WIDTH
#define TFT_WIDTH 128
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 160
#endif

// ---- Colors (RGB565) ------------------------------------------------------
#define TFT_BLACK 0x0000
#define TFT_NAVY 0x000F
#define TFT_DARKGREEN 0x03E0
#define TFT_DARKCYAN 0x03EF
#define TFT_MAROON 0x7800
#define TFT_PURPLE 0x780F
#define TFT_OLIVE 0x7BE0
#define TFT_LIGHTGREY 0xC618
#define TFT_DARKGREY 0x7BEF
#define TFT_BLUE 0x001F
#define TFT_GREEN 0x07E0
#define TFT_CYAN 0x07FF
#define TFT_RED 0xF800
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW 0xFFE0
#define TFT_WHITE 0xFFFF
#define TFT_ORANGE 0xFDA0
#define TFT_GREENYELLOW 0xB7E0
#define TFT_PINK 0xFE19
#define TFT_BROWN 0x9A60
#define TFT_GOLD 0xFEA0
#define TFT_SILVER 0xC618
#define TFT_SKYBLUE 0x867D
#define TFT_VIOLET 0x915C

// ---- Text datums ----------------------------------------------------------
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define CL_DATUM 3
#define MC_DATUM 4
#define CC_DATUM 4
#define MR_DATUM 5
#define CR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
#define L_BASELINE 9
#define C_BASELINE 10
#define R_BASELINE 11

class TFT_eSPI : public Print {
public:
  TFT_eSPI(int16_t w = TFT_WIDTH, int16_t h = TFT_HEIGHT) : canvas(w, h) {}

  // ---- Lifecycle (inert) --------------------------------------------------
  void init(uint8_t = 0) {}
  void begin(uint8_t = 0) {}
  void setSwapBytes(bool) {}
  void setRotation(uint8_t r) { canvas.setRotation(r); }
  uint8_t getRotation() { return canvas.getRotation(); }
  void invertDisplay(bool) {}

  int16_t width() { return canvas.width(); }
  int16_t height() { return canvas.height(); }

  // ---- Color helper -------------------------------------------------------
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return GFXcanvas_fb::color565(r, g, b);
  }

  // ---- Pixels / fills -----------------------------------------------------
  void drawPixel(int32_t x, int32_t y, uint32_t color) {
    canvas.drawPixel((int16_t)x, (int16_t)y, (uint16_t)color);
  }
  void fillScreen(uint32_t color) { canvas.fillScreen((uint16_t)color); }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    canvas.fillRect(x, y, w, h, (uint16_t)color);
  }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    canvas.drawRect(x, y, w, h, (uint16_t)color);
  }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
    canvas.drawFastHLine(x, y, w, (uint16_t)color);
  }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
    canvas.drawFastVLine(x, y, h, (uint16_t)color);
  }

  // ---- Lines / shapes -----------------------------------------------------
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    canvas.drawLine(x0, y0, x1, y1, (uint16_t)color);
  }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    canvas.drawCircle(x, y, r, (uint16_t)color);
  }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) {
    canvas.fillCircle(x, y, r, (uint16_t)color);
  }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius,
                     uint32_t color) {
    canvas.drawRoundRect(x, y, w, h, radius, (uint16_t)color);
  }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius,
                     uint32_t color) {
    canvas.fillRoundRect(x, y, w, h, radius, (uint16_t)color);
  }
  void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                    int32_t y2, uint32_t color) {
    canvas.drawTriangle(x0, y0, x1, y1, x2, y2, (uint16_t)color);
  }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                    int32_t y2, uint32_t color) {
    canvas.fillTriangle(x0, y0, x1, y1, x2, y2, (uint16_t)color);
  }
  void drawEllipse(int16_t x, int16_t y, int16_t rx, int16_t ry, uint16_t color) {
    canvas.drawEllipse(x, y, rx, ry, color);
  }
  void fillEllipse(int16_t x, int16_t y, int16_t rx, int16_t ry, uint16_t color) {
    canvas.fillEllipse(x, y, rx, ry, color);
  }

  // ---- Text state ---------------------------------------------------------
  void setCursor(int16_t x, int16_t y) { canvas.setCursor(x, y); }
  void setCursor(int16_t x, int16_t y, uint8_t font) { (void)font; canvas.setCursor(x, y); }
  void setTextColor(uint16_t c) { textcolor = textbg = c; canvas.setTextColor(c); }
  void setTextColor(uint16_t c, uint16_t bg) {
    textcolor = c; textbg = bg; canvas.setTextColor(c, bg);
  }
  void setTextSize(uint8_t s) { textsize = s ? s : 1; canvas.setTextSize(textsize); }
  void setTextWrap(bool w) { canvas.setTextWrap(w); }
  void setTextDatum(uint8_t d) { datum = d; }
  void setTextFont(uint8_t) {}   // only GLCD font 1 supported in v1
  void setTextPadding(uint16_t) {}
  int16_t fontHeight(void) { return 8 * textsize; }
  int16_t textWidth(const char *s) { return (int16_t)(s ? strlen(s) : 0) * 6 * textsize; }

  // Print interface — gives print/println/printf, routed through the canvas
  // so the classic-font rendering is shared with Adafruit_GFX.
  size_t write(uint8_t c) override { return canvas.write(c); }
  using Print::write;

  // ---- drawString family --------------------------------------------------
  // Renders with the GLCD font using the current text datum for alignment.
  int16_t drawString(const char *s, int32_t x, int32_t y) {
    return drawDatumString(s, x, y, datum);
  }
  int16_t drawString(const String &s, int32_t x, int32_t y) {
    return drawDatumString(s.c_str(), x, y, datum);
  }
  int16_t drawCentreString(const char *s, int32_t x, int32_t y, uint8_t /*font*/ = 1) {
    return drawDatumString(s, x, y, TC_DATUM);
  }
  int16_t drawRightString(const char *s, int32_t x, int32_t y, uint8_t /*font*/ = 1) {
    return drawDatumString(s, x, y, TR_DATUM);
  }

private:
  int16_t drawDatumString(const char *s, int32_t x, int32_t y, uint8_t dat) {
    if (!s) return 0;
    int16_t len = (int16_t)strlen(s);
    int16_t cw = 6 * textsize, ch = 8 * textsize;
    int16_t pw = len * cw;
    int32_t dx = x, dy = y;
    switch (dat) {
      case TC_DATUM: dx = x - pw / 2; break;
      case TR_DATUM: dx = x - pw; break;
      case ML_DATUM: dy = y - ch / 2; break;
      case MC_DATUM: dx = x - pw / 2; dy = y - ch / 2; break;
      case MR_DATUM: dx = x - pw; dy = y - ch / 2; break;
      case BL_DATUM: dy = y - ch; break;
      case BC_DATUM: dx = x - pw / 2; dy = y - ch; break;
      case BR_DATUM: dx = x - pw; dy = y - ch; break;
      default: break;  // TL_DATUM and baselines: top-left origin
    }
    canvas.setCursor((int16_t)dx, (int16_t)dy);
    canvas.setTextSize(textsize);
    canvas.setTextColor(textcolor, textbg);
    canvas.setTextWrap(false);
    canvas.print(s);
    return pw;
  }

  GFXcanvas_fb canvas;
  uint16_t textcolor = TFT_WHITE, textbg = TFT_BLACK;
  uint8_t textsize = 1;
  uint8_t datum = TL_DATUM;
};
