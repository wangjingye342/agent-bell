// Adafruit_I2CDevice.h — empty shim.
//
// Adafruit_GFX.h includes this at the top, but GFXcanvas16 (our rendering
// engine) never touches I2C. Providing an empty header avoids vendoring the
// real BusIO library and its hardware dependencies.
#pragma once
