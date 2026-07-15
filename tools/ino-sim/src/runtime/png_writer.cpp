// png_writer.cpp — write a sim::Canvas to a PNG file, self-contained (no zlib).
//
// Reads the canvas in logical (post-rotation) orientation, expands RGB565 to
// 8-bit RGB, optionally nearest-neighbor upscales by an integer factor so small
// 160x128 panels are legible as thumbnails, then emits a standard truecolor PNG
// (IHDR/IDAT/IEND).
//
// The IDAT payload is a valid zlib stream built from DEFLATE *stored*
// (uncompressed) blocks plus an Adler-32 trailer, and chunk CRCs use a local
// CRC-32 (IEEE poly 0xEDB88320). This deliberately avoids linking zlib (-lz),
// which MinGW-w64 on Windows does not ship by default — so the tool builds with
// nothing but a C++ compiler on every platform. Output PNGs are larger than
// zlib-compressed ones but decode identically.
#include "png_writer.h"
#include "../arduino_shim/sim_io.h"

#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>

namespace {

void put_u32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back(x & 0xFF);
}

// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) — the polynomial PNG uses for
// chunk CRCs. Computed on the fly so no lookup table is needed. `crc` is the
// running value; seed with 0xFFFFFFFF and finalize with ^0xFFFFFFFF.
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
  }
  return crc;
}

// Adler-32 checksum over the raw (uncompressed) data — the zlib stream trailer.
uint32_t adler32(const uint8_t *data, size_t len) {
  const uint32_t MOD = 65521;
  uint32_t a = 1, b = 0;
  size_t i = 0;
  while (i < len) {
    size_t n = len - i;
    if (n > 5552) n = 5552; // largest run that can't overflow a uint32
    for (size_t k = 0; k < n; k++) { a += data[i + k]; b += a; }
    a %= MOD; b %= MOD;
    i += n;
  }
  return (b << 16) | a;
}

void write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len) {
  uint8_t lenbuf[4] = {(uint8_t)(len >> 24), (uint8_t)(len >> 16),
                       (uint8_t)(len >> 8), (uint8_t)len};
  fwrite(lenbuf, 1, 4, f);
  fwrite(type, 1, 4, f);
  if (len) fwrite(data, 1, len, f);

  // CRC over type + data.
  uint32_t crc = 0xFFFFFFFFu;
  crc = crc32_update(crc, (const uint8_t *)type, 4);
  if (len) crc = crc32_update(crc, data, len);
  crc ^= 0xFFFFFFFFu;
  uint8_t crcbuf[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16),
                       (uint8_t)(crc >> 8), (uint8_t)crc};
  fwrite(crcbuf, 1, 4, f);
}

// Wrap `raw` in a valid zlib stream using DEFLATE stored (uncompressed) blocks:
//   [78 01] [stored blocks...] [adler32 big-endian]
// Each stored block: 1 header byte (BFINAL bit + BTYPE=00), LEN and ~LEN as
// little-endian u16, then LEN literal bytes; blocks cap at 65535 bytes.
std::vector<uint8_t> zlib_store(const std::vector<uint8_t> &raw) {
  std::vector<uint8_t> out;
  out.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
  out.push_back(0x78); // CMF: deflate, 32K window
  out.push_back(0x01); // FLG: no dict, fastest
  size_t n = raw.size(), off = 0;
  do {
    size_t block = n - off;
    if (block > 65535) block = 65535;
    bool last = (off + block >= n);
    out.push_back(last ? 0x01 : 0x00); // BFINAL + BTYPE=00 (stored)
    uint16_t len = (uint16_t)block, nlen = (uint16_t)~block;
    out.push_back(len & 0xFF);  out.push_back((len >> 8) & 0xFF);
    out.push_back(nlen & 0xFF); out.push_back((nlen >> 8) & 0xFF);
    out.insert(out.end(), raw.begin() + off, raw.begin() + off + block);
    off += block;
  } while (off < n);
  uint32_t ad = adler32(raw.data(), raw.size());
  out.push_back((ad >> 24) & 0xFF); out.push_back((ad >> 16) & 0xFF);
  out.push_back((ad >> 8) & 0xFF);  out.push_back(ad & 0xFF);
  return out;
}

// RGB565 -> RGB888 with bit replication so full-scale stays full-scale
// (0x1F -> 0xFF), matching how panels expand color and how Adafruit's own
// tooling converts.
inline void rgb565_to_888(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t r5 = (c >> 11) & 0x1F;
  uint8_t g6 = (c >> 5) & 0x3F;
  uint8_t b5 = c & 0x1F;
  r = (uint8_t)((r5 << 3) | (r5 >> 2));
  g = (uint8_t)((g6 << 2) | (g6 >> 4));
  b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

// Emit an RGB truecolor PNG from an already-built raw scanline stream (each row
// prefixed with a 0 filter byte). Shared by the canvas and mono writers.
bool emit_png(const std::vector<uint8_t> &raw, int ow, int oh, const std::string &path) {
  std::vector<uint8_t> comp = zlib_store(raw);
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  put_u32(ihdr, (uint32_t)ow);
  put_u32(ihdr, (uint32_t)oh);
  ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  write_chunk(f, "IHDR", ihdr.data(), ihdr.size());
  write_chunk(f, "IDAT", comp.data(), comp.size());
  write_chunk(f, "IEND", nullptr, 0);
  fclose(f);
  return true;
}

} // namespace

namespace simpng {

bool write_canvas_png(const sim::Canvas &cv, const std::string &path, int scale) {
  if (scale < 1) scale = 1;
  const int lw = cv.w(), lh = cv.h();
  if (lw <= 0 || lh <= 0) return false;
  const int ow = lw * scale, oh = lh * scale;

  // Build raw image: each scanline prefixed with filter byte 0 (None).
  std::vector<uint8_t> raw;
  raw.reserve((size_t)oh * (1 + ow * 3));
  for (int y = 0; y < oh; y++) {
    raw.push_back(0); // filter: None
    const int sy = y / scale;
    for (int x = 0; x < ow; x++) {
      const int sx = x / scale;
      uint8_t r, g, b;
      rgb565_to_888(cv.pixel(sx, sy), r, g, b);
      raw.push_back(r);
      raw.push_back(g);
      raw.push_back(b);
    }
  }

  // Wrap the raw stream as a zlib stream for the IDAT chunk (no compression).
  std::vector<uint8_t> comp = zlib_store(raw);

  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;

  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  fwrite(sig, 1, 8, f);

  // IHDR: width, height, bit depth 8, color type 2 (truecolor RGB).
  std::vector<uint8_t> ihdr;
  put_u32(ihdr, (uint32_t)ow);
  put_u32(ihdr, (uint32_t)oh);
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // color type: truecolor
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace
  write_chunk(f, "IHDR", ihdr.data(), ihdr.size());

  write_chunk(f, "IDAT", comp.data(), comp.size());
  write_chunk(f, "IEND", nullptr, 0);

  fclose(f);
  return true;
}

bool write_mono_png(const unsigned char *buf, int w, int h,
                    unsigned fg, unsigned bg, const std::string &path, int scale) {
  if (scale < 1) scale = 1;
  if (!buf || w <= 0 || h <= 0) return false;
  const int ow = w * scale, oh = h * scale;
  const uint8_t fr = (fg >> 16) & 0xFF, fgc = (fg >> 8) & 0xFF, fb = fg & 0xFF;
  const uint8_t br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;

  // buf is the SSD1306 page format: byte[(y/8)*w + x], bit (y%8) = pixel(x,y).
  std::vector<uint8_t> raw;
  raw.reserve((size_t)oh * (1 + ow * 3));
  for (int y = 0; y < oh; y++) {
    raw.push_back(0); // filter: None
    const int sy = y / scale;
    const int row = (sy >> 3) * w;
    const uint8_t bit = (uint8_t)(1u << (sy & 7));
    for (int x = 0; x < ow; x++) {
      const int sx = x / scale;
      const bool on = (buf[row + sx] & bit) != 0;
      raw.push_back(on ? fr : br);
      raw.push_back(on ? fgc : bgc);
      raw.push_back(on ? fb : bb);
    }
  }
  return emit_png(raw, ow, oh, path);
}

} // namespace simpng
