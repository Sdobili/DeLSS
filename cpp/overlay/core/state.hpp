// Overlay control state and the hotkey state machine. Pure C++, no OS or GL:
// every backend (Win32 RegisterHotKey/polling, Carbon hotkeys, X11 XGrabKey,
// tray menus) turns user input into Commands, and only this file decides what
// they do. Unit-tested headlessly in tests/core_test.cpp.
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace delss::overlay {

enum class Action : std::uint8_t {
  Toggle,         // overlay on/off
  NextPreset,     // cycle presets forward
  PrevPreset,     // cycle presets backward
  IntensityUp,    // +10 %
  IntensityDown,  // -10 %
  ToggleHud,      // ms-per-frame HUD
  Exit,           // safe exit: always tears every overlay window down
  // Tray/menu-only commands carry an argument.
  SetPreset,
  SetIntensity,
  ToggleMonitor,
  SetEnabled,
};

struct Command {
  Action action;
  int index = 0;       // SetPreset, ToggleMonitor
  float value = 0.0F;  // SetIntensity (0..1), SetEnabled (0/1)
};

/// Keys a binding can use. Kept tiny on purpose: every backend maps these.
enum class Key : std::uint8_t { D, P, O, H, Q, Up, Down, Escape };

enum Mod : std::uint8_t { kCtrl = 1, kAlt = 2, kShift = 4, kSuper = 8 };

struct Chord {
  std::uint8_t mods = 0;
  Key key = Key::D;
  friend bool operator<(const Chord& a, const Chord& b) {
    return a.mods != b.mods ? a.mods < b.mods : a.key < b.key;
  }
  friend bool operator==(const Chord& a, const Chord& b) { return a.mods == b.mods && a.key == b.key; }
};

struct Binding {
  Chord chord;
  Action action;
};

/// Default bindings. Ctrl+Alt+Esc and Ctrl+Alt+Q both exit, so the user can
/// always get out even if one combination is taken by the desktop.
const std::vector<Binding>& defaultBindings();
std::string describe(const Chord& chord);
std::optional<Action> lookup(const Chord& chord, const std::vector<Binding>& bindings = defaultBindings());

/// Edge detector for backends that can only poll key state (GetAsyncKeyState):
/// fires once when a bound chord becomes fully held, never on auto-repeat, and
/// re-arms only after it is released.
class ChordEdgeDetector {
 public:
  explicit ChordEdgeDetector(std::vector<Binding> bindings = defaultBindings());
  /// `held` = mods currently down + every bound key currently down.
  std::vector<Action> update(std::uint8_t heldMods, const std::set<Key>& heldKeys);

 private:
  std::vector<Binding> bindings_;
  std::set<Chord> latched_;
};

struct OverlayState {
  bool enabled = false;
  bool quit = false;
  bool hud = true;
  int preset = 0;
  float intensity = 1.0F;
  std::vector<bool> monitors;  // selected monitors (index = GLFW monitor order)

  /// Apply a command. Returns true when something observable changed.
  bool apply(const Command& command, int presetCount);
  /// At least one monitor stays selected; out-of-range picks fall back to 0.
  void normalizeMonitors(int monitorCount);
};

}  // namespace delss::overlay
