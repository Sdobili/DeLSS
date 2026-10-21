#include "state.hpp"

#include <algorithm>
#include <cmath>

namespace delss::overlay {

const std::vector<Binding>& defaultBindings() {
  static const std::vector<Binding> kBindings{
      {{kCtrl | kAlt, Key::D}, Action::Toggle},           {{kCtrl | kAlt, Key::P}, Action::NextPreset},
      {{kCtrl | kAlt, Key::O}, Action::PrevPreset},       {{kCtrl | kAlt, Key::Up}, Action::IntensityUp},
      {{kCtrl | kAlt, Key::Down}, Action::IntensityDown}, {{kCtrl | kAlt, Key::H}, Action::ToggleHud},
      {{kCtrl | kAlt, Key::Escape}, Action::Exit},        {{kCtrl | kAlt, Key::Q}, Action::Exit},
  };
  return kBindings;
}

std::string describe(const Chord& chord) {
  std::string s;
  if ((chord.mods & kCtrl) != 0) s += "Ctrl+";
  if ((chord.mods & kAlt) != 0) s += "Alt+";
  if ((chord.mods & kShift) != 0) s += "Shift+";
  if ((chord.mods & kSuper) != 0) s += "Super+";
  static const char* const kNames[] = {"D", "P", "O", "H", "Q", "Up", "Down", "Esc"};
  return s + kNames[static_cast<int>(chord.key)];
}

std::optional<Action> lookup(const Chord& chord, const std::vector<Binding>& bindings) {
  for (const Binding& b : bindings) {
    if (b.chord == chord) return b.action;
  }
  return std::nullopt;
}

ChordEdgeDetector::ChordEdgeDetector(std::vector<Binding> bindings) : bindings_(std::move(bindings)) {}

std::vector<Action> ChordEdgeDetector::update(std::uint8_t heldMods, const std::set<Key>& heldKeys) {
  std::vector<Action> fired;
  for (const Binding& b : bindings_) {
    // Exact modifier match, so Ctrl+Alt+Shift+D does not also fire Ctrl+Alt+D.
    const bool down = heldMods == b.chord.mods && heldKeys.count(b.chord.key) != 0;
    if (down) {
      if (latched_.insert(b.chord).second) fired.push_back(b.action);
    } else if (heldKeys.count(b.chord.key) == 0) {
      latched_.erase(b.chord);  // re-arm only once the key itself is released
    }
  }
  return fired;
}

bool OverlayState::apply(const Command& c, int presetCount) {
  const auto wrap = [presetCount](int i) {
    return presetCount <= 0 ? 0 : ((i % presetCount) + presetCount) % presetCount;
  };
  const auto clamp01 = [](float v) { return std::round(std::clamp(v, 0.0F, 1.0F) * 100.0F) / 100.0F; };
  const OverlayState before = *this;
  switch (c.action) {
    case Action::Toggle:
      enabled = !enabled;
      break;
    case Action::SetEnabled:
      enabled = c.value != 0.0F;
      break;
    case Action::NextPreset:
      preset = wrap(preset + 1);
      break;
    case Action::PrevPreset:
      preset = wrap(preset - 1);
      break;
    case Action::SetPreset:
      if (c.index >= 0 && c.index < presetCount) preset = c.index;
      break;
    case Action::IntensityUp:
      intensity = clamp01(intensity + 0.1F);
      break;
    case Action::IntensityDown:
      intensity = clamp01(intensity - 0.1F);
      break;
    case Action::SetIntensity:
      intensity = clamp01(c.value);
      break;
    case Action::ToggleHud:
      hud = !hud;
      break;
    case Action::ToggleMonitor: {
      if (c.index < 0 || c.index >= static_cast<int>(monitors.size())) break;
      const auto i = static_cast<size_t>(c.index);
      const auto selected = std::count(monitors.begin(), monitors.end(), true);
      if (monitors[i] && selected == 1) break;  // never deselect the last one
      monitors[i] = !monitors[i];
      break;
    }
    case Action::Exit:
      enabled = false;
      quit = true;
      break;
  }
  return enabled != before.enabled || quit != before.quit || hud != before.hud || preset != before.preset ||
         intensity != before.intensity || monitors != before.monitors;
}

void OverlayState::normalizeMonitors(int monitorCount) {
  monitors.resize(static_cast<size_t>(std::max(monitorCount, 0)), false);
  if (!monitors.empty() && std::find(monitors.begin(), monitors.end(), true) == monitors.end())
    monitors[0] = true;
}

}  // namespace delss::overlay
