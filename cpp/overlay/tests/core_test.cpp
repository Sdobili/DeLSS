// Headless unit tests for the shared overlay core. These run on Windows,
// macOS and Linux CI: they cover the logic every backend relies on.
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "core/frame.hpp"
#include "core/hud.hpp"
#include "core/settings.hpp"
#include "core/state.hpp"

using namespace delss::overlay;  // NOLINT(google-build-using-namespace)

namespace {
int gFailures = 0;
int gChecks = 0;
void expect(bool ok, const char* what) {
  ++gChecks;
  if (!ok) {
    ++gFailures;
    std::printf("FAIL %s\n", what);
  }
}

void testHotkeyEdges() {
  ChordEdgeDetector det;
  const auto held = [&](std::uint8_t mods, std::set<Key> keys) { return det.update(mods, keys); };
  expect(held(kCtrl | kAlt, {}).empty(), "modifiers alone fire nothing");
  auto a = held(kCtrl | kAlt, {Key::D});
  expect(a.size() == 1 && a[0] == Action::Toggle, "Ctrl+Alt+D fires Toggle");
  expect(held(kCtrl | kAlt, {Key::D}).empty(), "holding (auto-repeat) does not re-fire");
  expect(held(kCtrl, {Key::D}).empty(), "releasing Alt while D held does not fire");
  expect(held(kCtrl | kAlt, {Key::D}).empty(), "still latched until D is released");
  expect(held(kCtrl | kAlt, {}).empty(), "release D");
  a = held(kCtrl | kAlt, {Key::D});
  expect(a.size() == 1 && a[0] == Action::Toggle, "fires again after release");
  expect(held(kCtrl | kAlt | kShift, {Key::P}).empty(), "extra modifier: no match");
  a = held(kCtrl | kAlt, {Key::Escape});
  expect(a.size() == 1 && a[0] == Action::Exit, "Ctrl+Alt+Esc exits");
  expect(lookup({kCtrl | kAlt, Key::Q}) == Action::Exit, "Ctrl+Alt+Q exits too");
  expect(describe({kCtrl | kAlt, Key::D}) == "Ctrl+Alt+D", "describe chord");
}

void testState() {
  OverlayState s;
  s.normalizeMonitors(2);
  expect(s.monitors[0] && !s.monitors[1], "first monitor selected by default");
  expect(s.apply({Action::Toggle}, 9) && s.enabled, "toggle on");
  expect(s.apply({Action::PrevPreset}, 9) && s.preset == 8, "prev wraps to last");
  expect(s.apply({Action::NextPreset}, 9) && s.preset == 0, "next wraps to first");
  expect(!s.apply({Action::SetPreset, 42}, 9) && s.preset == 0, "out-of-range preset ignored");
  expect(!s.apply({Action::IntensityUp}, 9) && s.intensity == 1.0F, "intensity clamps at 1");
  s.apply({Action::IntensityDown}, 9);
  s.apply({Action::IntensityDown}, 9);
  expect(s.intensity > 0.79F && s.intensity < 0.81F, "intensity steps by 0.1");
  expect(!s.apply({Action::ToggleMonitor, 0}, 9) && s.monitors[0], "cannot deselect the last monitor");
  expect(s.apply({Action::ToggleMonitor, 1}, 9) && s.monitors[1], "select second monitor");
  expect(s.apply({Action::ToggleMonitor, 0}, 9) && !s.monitors[0], "now first can be deselected");
  expect(s.apply({Action::Exit}, 9) && s.quit && !s.enabled, "exit always disables and quits");
}

void testSettings() {
  Settings s;
  s.preset = "gameboy";
  s.intensity = 0.25F;
  s.hud = false;
  s.monitors = "0,2";
  s.fpsCap = 30;
  const Settings r = parseSettings(serializeSettings(s));
  expect(r.preset == "gameboy" && r.intensity == 0.25F && !r.hud && r.monitors == "0,2" && r.fpsCap == 30,
         "settings round-trip");
  const Settings bad = parseSettings("garbage\nintensity = lots\nfps_cap = -4\nunknown = 1\nhud=off\n");
  expect(bad.intensity == 1.0F && bad.fpsCap == 60 && !bad.hud, "bad values fall back, good ones apply");
  expect(parseMonitorList("all", 3) == std::vector<bool>({true, true, true}), "monitors all");
  expect(parseMonitorList("1, 7", 3) == std::vector<bool>({false, true, false}),
         "monitors list, out of range dropped");
  expect(parseMonitorList("9", 2) == std::vector<bool>({true, false}), "empty selection falls back to 0");
  expect(formatMonitorList({true, false, true}) == "0,2", "format list");
  expect(formatMonitorList({true, true}) == "all", "format all");
  expect(!defaultSettingsPath().empty(), "settings path");
}

void testFrameConversion() {
  // 2x2 BGRA, top-down, with 4 bytes of row padding (like DXGI/CoreVideo).
  CapturedFrame f;
  f.width = 2;
  f.height = 2;
  f.stride = 12;
  f.format = PixelFormat::BGRA8;
  f.pixels = {
      1, 2, 3, 0, 4,  5,  6,  0, 99, 99, 99, 99,  // top row: (B,G,R,X)
      7, 8, 9, 0, 10, 11, 12, 0, 99, 99, 99, 99,
  };
  std::vector<std::uint8_t> out;
  toGlRgba(f, out);
  const std::vector<std::uint8_t> want{9, 8, 7, 255, 12, 11, 10, 255, 3, 2, 1, 255, 6, 5, 4, 255};
  expect(out == want, "BGRA top-down padded -> RGBA bottom-up opaque");
  f.topDown = false;
  f.format = PixelFormat::RGBA8;
  toGlRgba(f, out);
  expect(out[0] == 1 && out[3] == 255 && out[8] == 7, "RGBA bottom-up passes through");
  std::vector<std::uint8_t> flipped;
  flipRowsRgba(want.data(), 2, 2, flipped);
  expect(flipped[0] == 3 && flipped[8] == 9, "flip rows");
}

void testHud() {
  const HudImage img = renderHudText("cel 1.5ms");
  expect(img.width == 9 * 4 - 1 + 4 && img.height == 9, "hud size");
  int lit = 0;
  for (size_t i = 0; i < img.rgba.size(); i += 4) lit += img.rgba[i + 1] == 255 ? 1 : 0;
  expect(lit > 20, "hud glyphs drawn");
  FrameTimer t;
  t.add(10.0);
  t.add(20.0);
  expect(t.average() > 10.9 && t.average() < 11.1, "frame timer EMA");
}
}  // namespace

int main() {
  testHotkeyEdges();
  testState();
  testSettings();
  testFrameConversion();
  testHud();
  std::printf("%d/%d checks passed\n", gChecks - gFailures, gChecks);
  return gFailures == 0 ? 0 : 1;
}
