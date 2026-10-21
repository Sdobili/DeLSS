// Persisted overlay settings: a tiny key=value file in the per-user config dir
//   Windows  %APPDATA%\DeLSS\overlay.ini
//   macOS    ~/Library/Application Support/DeLSS/overlay.ini
//   Linux    $XDG_CONFIG_HOME/delss/overlay.ini (default ~/.config/delss/)
#pragma once

#include <string>

#include "state.hpp"

namespace delss::overlay {

struct Settings {
  std::string preset = "cel";  // preset id, so reordering presets keeps the choice
  float intensity = 1.0F;
  bool hud = true;
  bool startEnabled = false;   // start with the filter on (default: wait for the hotkey)
  std::string monitors = "0";  // comma-separated monitor indices, or "all"
  int fpsCap = 60;
};

std::string defaultSettingsPath();
/// Parse key=value lines; unknown keys and bad values are ignored, so an old or
/// hand-edited file never prevents the overlay from starting.
Settings parseSettings(const std::string& text);
std::string serializeSettings(const Settings& settings);
Settings loadSettings(const std::string& path);
/// Writes atomically (temp file + rename). Returns false on I/O failure.
bool saveSettings(const std::string& path, const Settings& settings);

/// "0,2" / "all" <-> per-monitor flags.
std::vector<bool> parseMonitorList(const std::string& list, int monitorCount);
std::string formatMonitorList(const std::vector<bool>& monitors);

}  // namespace delss::overlay
