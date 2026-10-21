// The HUD: a few characters of text rasterized on the CPU with a 3x5 pixel
// font, so it needs no font files, no extra shader and looks identical on
// every backend. The renderer uploads the RGBA image and blits it scaled.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace delss::overlay {

struct HudImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;  // bottom-up (GL order)
};

/// Upper-cases `text`; characters outside [0-9A-Z .:%/-+] render as spaces.
HudImage renderHudText(const std::string& text);

/// Exponential moving average of frame times, for a steady readout.
class FrameTimer {
 public:
  void add(double ms);
  [[nodiscard]] double average() const { return average_; }

 private:
  double average_ = 0.0;
  bool primed_ = false;
};

}  // namespace delss::overlay
