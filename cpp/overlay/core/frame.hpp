// What a capture backend hands to the renderer, and the one conversion every
// backend shares: native screen pixels (top-down, BGRA/BGRX on all three OSes)
// to the bottom-up RGBA8 rows a GL texture expects.
#pragma once

#include <cstdint>
#include <vector>

namespace delss::overlay {

enum class PixelFormat : std::uint8_t {
  BGRA8,  // DXGI B8G8R8A8, CVPixelBuffer 32BGRA, X11 ZPixmap depth 24/32 (LSB first)
  RGBA8,
};

struct CapturedFrame {
  int width = 0;
  int height = 0;
  int stride = 0;  // bytes per row, >= width * 4 (DXGI/CoreVideo pad rows)
  PixelFormat format = PixelFormat::BGRA8;
  bool topDown = true;  // row 0 = top of the screen
  std::vector<std::uint8_t> pixels;
  std::uint64_t serial = 0;  // increments when the content may have changed
};

/// Tightly packed RGBA8, bottom row first, alpha forced to 255 (the screen is
/// opaque even when an ARGB window or BGRX padding says otherwise).
void toGlRgba(const CapturedFrame& frame, std::vector<std::uint8_t>& out);

/// Inverse for debugging dumps: GL readback (bottom-up RGBA) to top-down RGBA.
void flipRowsRgba(const std::uint8_t* src, int width, int height, std::vector<std::uint8_t>& out);

}  // namespace delss::overlay
