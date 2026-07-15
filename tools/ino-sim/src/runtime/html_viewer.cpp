// html_viewer.cpp — render the frame gallery to index.html.
//
// The page references the PNG files (written alongside it) rather than
// embedding base64, so it stays small and the PNGs are also usable directly by
// the AI's Read tool. Images render pixelated (crisp upscaling) to show the
// real pixel grid, on a dark backdrop that mimics a powered panel.
#include "html_viewer.h"

#include <cstdio>

namespace simhtml {

bool write_index(const std::string &out_dir, const std::string &title,
                 const std::vector<Frame> &frames) {
  std::string path = out_dir + "/index.html";
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;

  fprintf(f,
    "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<title>%s</title>\n<style>\n"
    ":root{color-scheme:dark}\n"
    "body{margin:0;padding:32px;background:#0c0d10;color:#e7e9ee;"
    "font:14px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}\n"
    "h1{font-size:18px;font-weight:600;margin:0 0 4px}\n"
    ".sub{color:#8b90a0;margin:0 0 28px;font-size:13px}\n"
    ".grid{display:flex;flex-wrap:wrap;gap:24px}\n"
    ".card{background:#15171c;border:1px solid #23262e;border-radius:12px;"
    "padding:16px;box-shadow:0 1px 0 #000}\n"
    ".screen{background:#000;border-radius:6px;padding:10px;display:inline-block;"
    "box-shadow:inset 0 0 0 1px #2a2d36}\n"
    ".screen img{display:block;image-rendering:pixelated;border-radius:2px}\n"
    ".label{margin-top:10px;font-weight:600}\n"
    ".dim{color:#8b90a0;font-size:12px;margin-top:2px}\n"
    "</style>\n</head>\n<body>\n", title.c_str());

  fprintf(f, "<h1>%s</h1>\n", title.c_str());
  fprintf(f, "<p class=\"sub\">%zu frame(s) &middot; ST77xx / TFT screen simulation</p>\n",
          frames.size());
  fprintf(f, "<div class=\"grid\">\n");

  for (const auto &fr : frames) {
    fprintf(f,
      "<div class=\"card\">\n"
      "  <div class=\"screen\"><img src=\"%s\" alt=\"%s\"></div>\n"
      "  <div class=\"label\">%s</div>\n"
      "  <div class=\"dim\">%d&times;%d px</div>\n"
      "</div>\n",
      fr.png_file.c_str(), fr.name.c_str(), fr.name.c_str(),
      fr.logical_w, fr.logical_h);
  }

  fprintf(f, "</div>\n</body>\n</html>\n");
  fclose(f);
  return true;
}

} // namespace simhtml
