// Minimal TFT_eSPI smoke test for the ino-sim facade.
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  tft.init();
  tft.setRotation(1);            // 160x128 landscape
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(8, 8, 60, 40, TFT_RED);
  tft.drawRect(8, 8, 60, 40, TFT_WHITE);
  tft.fillCircle(120, 28, 18, TFT_BLUE);
  tft.drawLine(0, 80, 159, 80, TFT_GREEN);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("TFT_eSPI", 80, 92);
}

void loop() {}
