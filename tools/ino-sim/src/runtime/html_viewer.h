// html_viewer.h — emit a self-contained HTML gallery of captured frames.
#pragma once
#include <string>
#include <vector>

namespace simhtml {

struct Frame {
  std::string name;      // label / frame name
  std::string png_file;  // PNG filename, relative to the HTML file
  int logical_w = 0;     // panel logical width (for caption)
  int logical_h = 0;     // panel logical height
};

// Write an index.html into `out_dir` showing every frame with its label and
// the panel dimensions. `title` heads the page.
bool write_index(const std::string &out_dir, const std::string &title,
                 const std::vector<Frame> &frames);

} // namespace simhtml
