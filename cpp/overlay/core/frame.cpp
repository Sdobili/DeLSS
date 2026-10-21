#include "frame.hpp"

#include <cstring>

namespace delss::overlay {

void toGlRgba(const CapturedFrame& f, std::vector<std::uint8_t>& out) {
  const auto w = static_cast<size_t>(f.width);
  const auto h = static_cast<size_t>(f.height);
  out.resize(w * h * 4);
  for (size_t y = 0; y < h; ++y) {
    const size_t srcRow = f.topDown ? h - 1 - y : y;
    const std::uint8_t* s = f.pixels.data() + srcRow * static_cast<size_t>(f.stride);
    std::uint8_t* d = out.data() + y * w * 4;
    if (f.format == PixelFormat::BGRA8) {
      for (size_t x = 0; x < w; ++x, s += 4, d += 4) {
        d[0] = s[2];
        d[1] = s[1];
        d[2] = s[0];
        d[3] = 255;
      }
    } else {
      for (size_t x = 0; x < w; ++x, s += 4, d += 4) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = 255;
      }
    }
  }
}

void flipRowsRgba(const std::uint8_t* src, int width, int height, std::vector<std::uint8_t>& out) {
  const auto row = static_cast<size_t>(width) * 4;
  out.resize(row * static_cast<size_t>(height));
  for (int y = 0; y < height; ++y) {
    std::memcpy(out.data() + static_cast<size_t>(y) * row, src + static_cast<size_t>(height - 1 - y) * row,
                row);
  }
}

}  // namespace delss::overlay
