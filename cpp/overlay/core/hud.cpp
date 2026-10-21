#include "hud.hpp"

#include <array>
#include <cctype>

namespace delss::overlay {

namespace {
// 3x5 glyphs, one row per entry, bit 2 = left column.
struct Glyph {
  char c;
  std::array<std::uint8_t, 5> rows;
};
constexpr std::array<Glyph, 46> kFont{{
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {7, 1, 7, 4, 7}}, {'3', {7, 1, 3, 1, 7}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 7, 1, 7}}, {'6', {7, 4, 7, 5, 7}}, {'7', {7, 1, 1, 2, 2}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 7}}, {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}},
    {'C', {3, 4, 4, 4, 3}}, {'D', {6, 5, 5, 5, 6}}, {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}},
    {'G', {3, 4, 5, 5, 3}}, {'H', {5, 5, 7, 5, 5}}, {'I', {7, 2, 2, 2, 7}}, {'J', {1, 1, 1, 5, 2}},
    {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}}, {'M', {5, 7, 7, 5, 5}}, {'N', {6, 5, 5, 5, 5}},
    {'O', {2, 5, 5, 5, 2}}, {'P', {6, 5, 6, 4, 4}}, {'Q', {2, 5, 5, 6, 3}}, {'R', {6, 5, 6, 5, 5}},
    {'S', {3, 4, 2, 1, 6}}, {'T', {7, 2, 2, 2, 2}}, {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}},
    {'W', {5, 5, 7, 7, 5}}, {'X', {5, 5, 2, 5, 5}}, {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
    {'.', {0, 0, 0, 0, 2}}, {':', {0, 2, 0, 2, 0}}, {'%', {5, 1, 2, 4, 5}}, {'/', {1, 1, 2, 4, 4}},
    {'-', {0, 0, 7, 0, 0}}, {'+', {0, 2, 7, 2, 0}}, {' ', {0, 0, 0, 0, 0}}, {'(', {1, 2, 2, 2, 1}},
    {')', {4, 2, 2, 2, 4}}, {'=', {0, 7, 0, 7, 0}},
}};

const Glyph& glyph(char c) {
  for (const Glyph& g : kFont) {
    if (g.c == c) return g;
  }
  return kFont[42];  // space
}
}  // namespace

HudImage renderHudText(const std::string& text) {
  constexpr int kPad = 2;
  HudImage img;
  img.width = static_cast<int>(text.size()) * 4 - 1 + kPad * 2;
  img.height = 5 + kPad * 2;
  img.rgba.assign(static_cast<size_t>(img.width * img.height) * 4, 0);
  // Opaque dark backdrop so the readout stays legible over any content.
  for (size_t i = 0; i < img.rgba.size(); i += 4) {
    img.rgba[i] = 16;
    img.rgba[i + 1] = 16;
    img.rgba[i + 2] = 16;
    img.rgba[i + 3] = 255;
  }
  for (size_t n = 0; n < text.size(); ++n) {
    const Glyph& g = glyph(static_cast<char>(std::toupper(static_cast<unsigned char>(text[n]))));
    for (int row = 0; row < 5; ++row) {
      for (int col = 0; col < 3; ++col) {
        if ((g.rows[static_cast<size_t>(row)] & (4 >> col)) == 0) continue;
        const int x = kPad + static_cast<int>(n) * 4 + col;
        const int y = img.height - 1 - (kPad + row);  // bottom-up
        const auto i = static_cast<size_t>((y * img.width + x) * 4);
        img.rgba[i] = 120;
        img.rgba[i + 1] = 255;
        img.rgba[i + 2] = 120;
      }
    }
  }
  return img;
}

void FrameTimer::add(double ms) {
  if (!primed_) {
    average_ = ms;
    primed_ = true;
    return;
  }
  average_ += (ms - average_) * 0.1;
}

}  // namespace delss::overlay
