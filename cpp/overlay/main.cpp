// delss-overlay: toggle a DeLSS style over whatever is on screen.
//
// capture (per-OS, excludes our own windows) -> upload -> DeLSS chain ->
// present in a topmost, borderless, click-through window per monitor.
// The overlay can never trap the user: windows ignore all mouse input, the
// exit hotkeys work at any time, and persistent capture failure hides it.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/hud.hpp"
#include "core/settings.hpp"
#include "core/state.hpp"
#include "delss/delss.hpp"
#include "gl/renderer.hpp"
#include "platform/platform.hpp"

// NOLINTBEGIN
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
// NOLINTEND

namespace ov = delss::overlay;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
  std::string configPath = ov::defaultSettingsPath();
  bool save = true;
  bool listMonitors = false;
  bool listPresets = false;
  bool forceEnable = false;
  std::string preset;
  float intensity = -1.0F;
  std::string monitors;
  int hud = -1;
  std::string dumpDir;  // verification: write capture/output PNGs at --dump-frame
  int dumpFrame = 30;
  int exitAfterFrames = 0;
  double exitAfterSeconds = 0.0;
  bool debugNoExclude = false;  // test-only negative control: capture sees the overlay
};

void usage() {
  std::puts(
      "delss-overlay: screen-wide DeLSS filter\n"
      "  --enable               start with the filter on (default: off until Ctrl+Alt+D)\n"
      "  --preset ID            cel, comic, pixel, ps1, gameboy, cga, crt, vhs, oldphoto\n"
      "  --intensity F          0..1\n"
      "  --monitors LIST        e.g. 0 / 0,1 / all\n"
      "  --hud / --no-hud       ms-per-frame readout\n"
      "  --config PATH          settings file (default: per-user config dir)\n"
      "  --no-save              do not write settings back\n"
      "  --list-monitors | --list-presets\n"
      "  --dump DIR [--dump-frame N]   save capture_<m>.png and output_<m>.png at frame N\n"
      "  --exit-after-frames N | --exit-after-seconds S\n"
      "Hotkeys: Ctrl+Alt+D toggle, Ctrl+Alt+P / O next / previous preset,\n"
      "         Ctrl+Alt+Up / Down intensity, Ctrl+Alt+H HUD, Ctrl+Alt+Esc or Ctrl+Alt+Q exit.");
}

Options parseArgs(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::invalid_argument(a + " needs a value");
      return argv[++i];
    };
    if (a == "--enable")
      o.forceEnable = true;
    else if (a == "--preset")
      o.preset = next();
    else if (a == "--intensity")
      o.intensity = std::stof(next());
    else if (a == "--monitors")
      o.monitors = next();
    else if (a == "--hud")
      o.hud = 1;
    else if (a == "--no-hud")
      o.hud = 0;
    else if (a == "--config")
      o.configPath = next();
    else if (a == "--no-save")
      o.save = false;
    else if (a == "--list-monitors")
      o.listMonitors = true;
    else if (a == "--list-presets")
      o.listPresets = true;
    else if (a == "--dump")
      o.dumpDir = next();
    else if (a == "--dump-frame")
      o.dumpFrame = std::stoi(next());
    else if (a == "--exit-after-frames")
      o.exitAfterFrames = std::stoi(next());
    else if (a == "--exit-after-seconds")
      o.exitAfterSeconds = std::stod(next());
    else if (a == "--debug-no-exclude")
      o.debugNoExclude = true;
    else if (a == "-h" || a == "--help") {
      usage();
      std::exit(0);  // NOLINT(concurrency-mt-unsafe)
    } else {
      throw std::invalid_argument("unknown option " + a);
    }
  }
  return o;
}

std::vector<ov::MonitorInfo> monitors() {
  int count = 0;
  GLFWmonitor** list = glfwGetMonitors(&count);
  std::vector<ov::MonitorInfo> out;
  for (int i = 0; i < count; ++i) {
    ov::MonitorInfo m;
    m.index = i;
    m.monitor = list[i];
    m.name = glfwGetMonitorName(list[i]) != nullptr ? glfwGetMonitorName(list[i]) : "monitor";
    glfwGetMonitorPos(list[i], &m.x, &m.y);
    const GLFWvidmode* mode = glfwGetVideoMode(list[i]);
    // Window coordinates (macOS points); capture backends work in pixels.
    m.width = mode->width;
    m.height = mode->height;
    out.push_back(m);
  }
  return out;
}

int presetIndex(const std::string& id) {
  const auto& all = delss::presets();
  for (size_t i = 0; i < all.size(); ++i) {
    if (all[i].id == id) return static_cast<int>(i);
  }
  return 0;
}

void* loadProc(const char* name) { return reinterpret_cast<void*>(glfwGetProcAddress(name)); }

/// One overlay window on one monitor.
struct Output {
  ov::MonitorInfo monitor;
  GLFWwindow* window = nullptr;
  std::unique_ptr<ov::Renderer> renderer;
  std::unique_ptr<ov::ScreenCapture> capture;
  ov::CapturedFrame frame;
  Clock::time_point lastGoodCapture = Clock::now();
  int frames = 0;
};

class App {
 public:
  App(Options options, ov::Platform& platform) : options_(std::move(options)), platform_(platform) {}

  int run();

 private:
  void createOutputs();
  void destroyOutputs();
  void renderOutput(Output& out);
  void dump(Output& out);
  void persist();
  void updateTray();
  [[nodiscard]] std::vector<GLFWwindow*> windows() const;

  Options options_;
  ov::Platform& platform_;
  std::unique_ptr<ov::Tray> tray_;
  ov::Settings settings_;
  ov::OverlayState state_;
  std::vector<ov::MonitorInfo> monitors_;
  std::vector<std::unique_ptr<Output>> outputs_;
  ov::FrameTimer workTimer_;
  ov::FrameTimer frameTimer_;
  Clock::time_point lastFrame_ = Clock::now();
  bool dirty_ = false;
  Clock::time_point dirtySince_;
};

std::vector<GLFWwindow*> App::windows() const {
  std::vector<GLFWwindow*> w;
  for (const auto& o : outputs_) w.push_back(o->window);
  return w;
}

void App::createOutputs() {
  for (const ov::MonitorInfo& m : monitors_) {
    if (!state_.monitors[static_cast<size_t>(m.index)]) continue;
    auto out = std::make_unique<Output>();
    out->monitor = m;
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
    platform_.windowHints();
    out->window = glfwCreateWindow(m.width, m.height, "delss-overlay", nullptr,
                                   outputs_.empty() ? nullptr : outputs_.front()->window);
    if (out->window == nullptr) throw std::runtime_error("cannot create the overlay window");
    glfwSetWindowPos(out->window, m.x, m.y);
    platform_.prepareWindow(out->window, m);
    glfwMakeContextCurrent(out->window);
    std::string missing;
    if (!delss::gl::load(loadProc, &missing)) throw std::runtime_error("GL entry point missing: " + missing);
    glfwSwapInterval(outputs_.empty() ? 1 : 0);  // only the first window waits for vsync
    out->renderer = std::make_unique<ov::Renderer>();
    outputs_.push_back(std::move(out));
  }
  // Captures are created after every window exists so each excludes all of them.
  for (auto& out : outputs_) {
    out->capture = platform_.createCapture(
        out->monitor, [this] { return options_.debugNoExclude ? std::vector<GLFWwindow*>{} : windows(); });
    std::printf("overlay on monitor %d \"%s\" %dx%d+%d+%d, capture: %s\n", out->monitor.index,
                out->monitor.name.c_str(), out->monitor.width, out->monitor.height, out->monitor.x,
                out->monitor.y, out->capture->describe().c_str());
  }
  // Show only once the first frame can be drawn (no flash of garbage).
  for (auto& out : outputs_) {
    renderOutput(*out);
    glfwShowWindow(out->window);
  }
}

void App::destroyOutputs() {
  for (auto& out : outputs_) {
    glfwMakeContextCurrent(out->window);
    out->renderer.reset();
    out->capture.reset();
  }
  for (auto& out : outputs_) glfwDestroyWindow(out->window);
  outputs_.clear();
}

void App::dump(Output& out) {
  namespace fs = std::filesystem;
  fs::create_directories(options_.dumpDir);
  const int idx = out.monitor.index;
  std::vector<std::uint8_t> rgba;
  ov::toGlRgba(out.frame, rgba);
  std::vector<std::uint8_t> topDown;
  ov::flipRowsRgba(rgba.data(), out.frame.width, out.frame.height, topDown);
  const std::string cap = (fs::path(options_.dumpDir) / ("capture_" + std::to_string(idx) + ".png")).string();
  stbi_write_png(cap.c_str(), out.frame.width, out.frame.height, 4, topDown.data(), out.frame.width * 4);
  int w = 0;
  int h = 0;
  glfwGetFramebufferSize(out.window, &w, &h);
  const auto pixels = out.renderer->readback(w, h);
  const std::string img = (fs::path(options_.dumpDir) / ("output_" + std::to_string(idx) + ".png")).string();
  stbi_write_png(img.c_str(), w, h, 4, pixels.data(), w * 4);
  std::printf("dumped %s and %s\n", cap.c_str(), img.c_str());
  std::fflush(stdout);
}

void App::renderOutput(Output& out) {
  glfwMakeContextCurrent(out.window);
  const auto t0 = Clock::now();
  if (out.capture->grab(out.frame)) {
    out.lastGoodCapture = t0;
    out.renderer->upload(out.frame);
  }
  const auto& preset = delss::presets()[static_cast<size_t>(state_.preset)];
  out.renderer->configure(std::string(preset.id), state_.intensity);
  int w = 0;
  int h = 0;
  glfwGetFramebufferSize(out.window, &w, &h);
  ov::HudImage hud;
  if (state_.hud) {
    char text[64];
    const double fps = frameTimer_.average() > 0.0 ? 1000.0 / frameTimer_.average() : 0.0;
    std::snprintf(text, sizeof text, "%s %.1fMS %.0fFPS", std::string(preset.id).c_str(),
                  workTimer_.average(), fps);
    hud = ov::renderHudText(text);
  }
  out.renderer->draw(w, h, state_.hud ? &hud : nullptr, std::max(2, h / 360));
  ++out.frames;
  if (!options_.dumpDir.empty() && out.frames == options_.dumpFrame) dump(out);
  platform_.keepOnTop(out.window);
  glfwSwapBuffers(out.window);
  workTimer_.add(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
}

void App::persist() {
  settings_.preset = std::string(delss::presets()[static_cast<size_t>(state_.preset)].id);
  settings_.intensity = state_.intensity;
  settings_.hud = state_.hud;
  settings_.monitors = ov::formatMonitorList(state_.monitors);
  if (options_.save && !ov::saveSettings(options_.configPath, settings_)) {
    std::fprintf(stderr, "warning: cannot save settings to %s\n", options_.configPath.c_str());
  }
}

void App::updateTray() {
  if (!tray_) return;
  ov::TrayModel m;
  m.enabled = state_.enabled;
  m.hud = state_.hud;
  m.preset = state_.preset;
  m.intensity = state_.intensity;
  for (const auto& p : delss::presets()) m.presetLabels.emplace_back(p.label);
  for (const auto& mon : monitors_) {
    m.monitorNames.push_back(std::to_string(mon.index + 1) + ": " + mon.name + " (" +
                             std::to_string(mon.width) + "x" + std::to_string(mon.height) + ")");
  }
  m.monitors = state_.monitors;
  m.hotkeyHelp = "Ctrl+Alt+D toggle / Ctrl+Alt+P preset / Ctrl+Alt+Esc exit";
  tray_->update(m);
}

int App::run() {
  settings_ = ov::loadSettings(options_.configPath);
  if (!options_.preset.empty()) settings_.preset = options_.preset;
  if (options_.intensity >= 0.0F) settings_.intensity = options_.intensity;
  if (!options_.monitors.empty()) settings_.monitors = options_.monitors;
  if (options_.hud >= 0) settings_.hud = options_.hud == 1;

  monitors_ = monitors();
  if (options_.listMonitors) {
    for (const auto& m : monitors_)
      std::printf("%d: %s %dx%d+%d+%d\n", m.index, m.name.c_str(), m.width, m.height, m.x, m.y);
    return 0;
  }
  if (monitors_.empty()) throw std::runtime_error("no monitors found");
  const int presetCount = static_cast<int>(delss::presets().size());
  state_.preset = presetIndex(settings_.preset);
  state_.intensity = settings_.intensity;
  state_.hud = settings_.hud;
  state_.monitors = ov::parseMonitorList(settings_.monitors, static_cast<int>(monitors_.size()));
  state_.enabled = options_.forceEnable || settings_.startEnabled;
  for (const auto& w : platform_.warnings()) std::fprintf(stderr, "warning: %s\n", w.c_str());

  tray_ = ov::createTray();
  updateTray();
  std::printf("delss-overlay ready (%s backend). Ctrl+Alt+D toggles, Ctrl+Alt+Esc exits.\n",
              platform_.name().c_str());
  std::fflush(stdout);

  const auto started = Clock::now();
  int totalFrames = 0;
  while (!state_.quit) {
    std::vector<ov::Command> commands;
    glfwPollEvents();
    platform_.pollHotkeys(commands);
    if (tray_) tray_->poll(commands);
    bool changed = false;
    bool monitorsChanged = false;
    for (const ov::Command& c : commands) {
      const auto before = state_.monitors;
      if (state_.apply(c, presetCount)) {
        changed = true;
        monitorsChanged = monitorsChanged || before != state_.monitors;
      }
    }
    if (changed) {
      std::printf("state: enabled=%d preset=%s intensity=%.2f hud=%d monitors=%s%s\n", state_.enabled ? 1 : 0,
                  std::string(delss::presets()[static_cast<size_t>(state_.preset)].id).c_str(),
                  static_cast<double>(state_.intensity), state_.hud ? 1 : 0,
                  ov::formatMonitorList(state_.monitors).c_str(), state_.quit ? " quit" : "");
      std::fflush(stdout);
      updateTray();
      dirty_ = true;
      dirtySince_ = Clock::now();
    }
    if (dirty_ && Clock::now() - dirtySince_ > std::chrono::milliseconds(500)) {
      persist();
      dirty_ = false;
    }
    if (state_.quit) break;
    if (monitorsChanged || (!state_.enabled && !outputs_.empty())) destroyOutputs();
    if (state_.enabled && outputs_.empty()) createOutputs();

    if (outputs_.empty()) {
      glfwWaitEventsTimeout(0.03);  // idle: just keep polling hotkeys and the tray
    } else {
      const auto now = Clock::now();
      frameTimer_.add(std::chrono::duration<double, std::milli>(now - lastFrame_).count());
      lastFrame_ = now;
      for (auto& out : outputs_) renderOutput(*out);
      // Safety: if capture keeps failing, never leave a frozen image on top.
      for (const auto& out : outputs_) {
        if (Clock::now() - out->lastGoodCapture > std::chrono::seconds(3)) {
          std::fprintf(stderr, "capture on monitor %d failed for 3 s; hiding the overlay\n",
                       out->monitor.index);
          state_.enabled = false;
          updateTray();
          break;
        }
      }
      const auto budget = std::chrono::microseconds(1000000 / std::max(1, settings_.fpsCap));
      const auto spent = Clock::now() - now;
      if (spent < budget) std::this_thread::sleep_for(budget - spent);
      ++totalFrames;
    }
    if (options_.exitAfterFrames > 0 && totalFrames >= options_.exitAfterFrames) break;
    if (options_.exitAfterSeconds > 0.0 &&
        std::chrono::duration<double>(Clock::now() - started).count() > options_.exitAfterSeconds) {
      break;
    }
  }
  destroyOutputs();
  if (dirty_) persist();
  std::puts("delss-overlay: exited cleanly");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    Options options = parseArgs(argc, argv);
    if (options.listPresets) {
      for (const auto& p : delss::presets())
        std::printf("%s\t%s\n", std::string(p.id).c_str(), std::string(p.label).c_str());
      return 0;
    }
    auto platform = ov::createPlatform();
    platform->initHints();
    glfwSetErrorCallback(
        [](int code, const char* message) { std::fprintf(stderr, "GLFW %d: %s\n", code, message); });
    if (glfwInit() != GLFW_TRUE) throw std::runtime_error("glfwInit failed (no display?)");
    int rc = 0;
    {
      App app(std::move(options), *platform);
      rc = app.run();
    }
    platform.reset();
    glfwTerminate();
    return rc;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "delss-overlay: %s\n", e.what());
    glfwTerminate();
    return 1;
  }
}
