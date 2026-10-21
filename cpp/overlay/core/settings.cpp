#include "settings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace delss::overlay {

namespace {
std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

std::string env(const char* name) {
  const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): read once at startup
  return v != nullptr ? v : "";
}

bool parseBool(const std::string& v, bool fallback) {
  if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
  if (v == "0" || v == "false" || v == "off" || v == "no") return false;
  return fallback;
}
}  // namespace

std::string defaultSettingsPath() {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  fs::path base = env("APPDATA");
  if (base.empty()) base = fs::temp_directory_path();
  return (base / "DeLSS" / "overlay.ini").string();
#elif defined(__APPLE__)
  return (fs::path(env("HOME")) / "Library/Application Support/DeLSS/overlay.ini").string();
#else
  fs::path base = env("XDG_CONFIG_HOME");
  if (base.empty()) base = fs::path(env("HOME")) / ".config";
  return (base / "delss" / "overlay.ini").string();
#endif
}

Settings parseSettings(const std::string& text) {
  Settings s;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "preset" && !value.empty()) {
      s.preset = value;
    } else if (key == "intensity") {
      char* end = nullptr;
      const float f = std::strtof(value.c_str(), &end);
      if (end != value.c_str()) s.intensity = std::clamp(f, 0.0F, 1.0F);
    } else if (key == "hud") {
      s.hud = parseBool(value, s.hud);
    } else if (key == "start_enabled") {
      s.startEnabled = parseBool(value, s.startEnabled);
    } else if (key == "monitors" && !value.empty()) {
      s.monitors = value;
    } else if (key == "fps_cap") {
      const long n = std::strtol(value.c_str(), nullptr, 10);
      if (n >= 1 && n <= 1000) s.fpsCap = static_cast<int>(n);
    }
  }
  return s;
}

std::string serializeSettings(const Settings& s) {
  std::ostringstream out;
  char intensity[32];
  std::snprintf(intensity, sizeof intensity, "%.2f", static_cast<double>(s.intensity));
  out << "# delss-overlay settings (written by the app; safe to edit)\n"
      << "preset = " << s.preset << "\n"
      << "intensity = " << intensity << "\n"
      << "hud = " << (s.hud ? "true" : "false") << "\n"
      << "start_enabled = " << (s.startEnabled ? "true" : "false") << "\n"
      << "monitors = " << s.monitors << "\n"
      << "fps_cap = " << s.fpsCap << "\n";
  return out.str();
}

Settings loadSettings(const std::string& path) {
  std::ifstream in(path);
  if (!in) return {};
  std::stringstream buf;
  buf << in.rdbuf();
  return parseSettings(buf.str());
}

bool saveSettings(const std::string& path, const Settings& settings) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    out << serializeSettings(settings);
    if (!out) return false;
  }
  fs::rename(tmp, path, ec);
  return !ec;
}

std::vector<bool> parseMonitorList(const std::string& list, int monitorCount) {
  std::vector<bool> out(static_cast<size_t>(std::max(monitorCount, 0)), false);
  if (trim(list) == "all") {
    std::fill(out.begin(), out.end(), true);
    return out;
  }
  std::istringstream in(list);
  std::string item;
  while (std::getline(in, item, ',')) {
    item = trim(item);
    if (item.empty()) continue;
    char* end = nullptr;
    const long i = std::strtol(item.c_str(), &end, 10);
    if (*end == '\0' && i >= 0 && i < monitorCount) out[static_cast<size_t>(i)] = true;
  }
  if (!out.empty() && std::find(out.begin(), out.end(), true) == out.end()) out[0] = true;
  return out;
}

std::string formatMonitorList(const std::vector<bool>& monitors) {
  if (!monitors.empty() && std::all_of(monitors.begin(), monitors.end(), [](bool b) { return b; }) &&
      monitors.size() > 1) {
    return "all";
  }
  std::string out;
  for (size_t i = 0; i < monitors.size(); ++i) {
    if (!monitors[i]) continue;
    if (!out.empty()) out += ",";
    out += std::to_string(i);
  }
  return out.empty() ? "0" : out;
}

}  // namespace delss::overlay
