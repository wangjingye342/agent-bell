// png_writer.h — PNG export for a sim::Canvas.
#pragma once
#include <string>

namespace sim { struct Canvas; }

namespace simpng {
// Write `cv` to `path` as a truecolor PNG, nearest-neighbor upscaled by
// `scale` (>=1). Returns false on any I/O or compression error.
bool write_canvas_png(const sim::Canvas &cv, const std::string &path, int scale);

// Write a 1-bit SSD1306-style page buffer as a truecolor PNG. Pixel (x,y) is on
// iff buf[(y/8)*w + x] has bit (y%8) set. `fg`/`bg` are 0xRRGGBB colors for the
// on/off pixels. Nearest-neighbor upscaled by `scale` (>=1).
bool write_mono_png(const unsigned char *buf, int w, int h,
                    unsigned fg, unsigned bg, const std::string &path, int scale);
}
