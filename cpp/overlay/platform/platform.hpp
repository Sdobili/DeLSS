// The seam between the shared overlay app and each OS. main.cpp owns the GLFW
// windows and the render loop; a Platform adds what GLFW cannot do portably:
// screen capture that excludes our own windows, stacking above everything,
// and global hotkeys. A Tray is the optional menu-bar / notification-area UI.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/frame.hpp"
#include "core/state.hpp"

struct GLFWwindow;
struct GLFWmonitor;

namespace delss::overlay {

struct MonitorInfo {
  int index = 0;
  std::string name;
  int x = 0;  // virtual-desktop position in pixels
  int y = 0;
  int width = 0;
  int height = 0;
  GLFWmonitor* monitor = nullptr;
};

class ScreenCapture {
 public:
  virtual ~ScreenCapture() = default;
  /// Fill `frame` with the monitor's current content, minus our own overlay
  /// windows. Returns false on failure (the caller keeps the previous frame
  /// and, if failures persist, disables the overlay for safety).
  virtual bool grab(CapturedFrame& frame) = 0;
  [[nodiscard]] virtual std::string describe() const = 0;
};

/// Overlay windows that every capture must exclude (all monitors).
using ExcludedWindows = std::function<std::vector<GLFWwindow*>()>;

class Platform {
 public:
  virtual ~Platform() = default;
  /// GLFW window hints / init hints this backend needs, applied before
  /// glfwInit (init) and before every glfwCreateWindow (window).
  virtual void initHints() {}
  virtual void windowHints() {}
  /// Called once on each overlay window while it is still hidden: make it
  /// topmost, click-through and excluded from capture.
  virtual void prepareWindow(GLFWwindow* window, const MonitorInfo& monitor) = 0;
  /// Called every frame; cheap re-assertion of topmost where the OS needs it.
  virtual void keepOnTop(GLFWwindow* /*window*/) {}
  virtual std::unique_ptr<ScreenCapture> createCapture(const MonitorInfo& monitor,
                                                       ExcludedWindows excluded) = 0;
  /// Global hotkeys pressed since the last call.
  virtual void pollHotkeys(std::vector<Command>& out) = 0;
  /// Human-readable notes printed at startup (e.g. a key combo already grabbed).
  [[nodiscard]] virtual std::vector<std::string> warnings() const { return {}; }
  [[nodiscard]] virtual std::string name() const = 0;
  /// Tell the user something went wrong (stderr everywhere; a dialog where
  /// apps are launched without a terminal, i.e. macOS).
  virtual void reportError(const std::string& message);
};

std::unique_ptr<Platform> createPlatform();

struct TrayModel {
  bool enabled = false;
  bool hud = true;
  int preset = 0;
  float intensity = 1.0F;
  std::vector<std::string> presetLabels;
  std::vector<std::string> monitorNames;
  std::vector<bool> monitors;
  std::string hotkeyHelp;
};

class Tray {
 public:
  virtual ~Tray() = default;
  virtual void update(const TrayModel& model) = 0;
  virtual void poll(std::vector<Command>& out) = 0;
};

/// Returns nullptr when this build/OS has no tray (hotkeys still work).
std::unique_ptr<Tray> createTray();

}  // namespace delss::overlay
