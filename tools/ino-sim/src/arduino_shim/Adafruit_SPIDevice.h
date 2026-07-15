// Adafruit_SPIDevice.h — empty shim.
//
// Adafruit_GFX.h includes this at the top, but GFXcanvas16 (our rendering
// engine) never touches SPI. Providing an empty header avoids vendoring the
// real BusIO library and its hardware dependencies.
#pragma once
