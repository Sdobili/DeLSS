// Linux / X11 backend.
//
// Capture: XComposite. Every top-level window is redirected (automatic mode,
// so the X server or the running compositor still paints the screen), and a
// frame is rebuilt by stacking the windows' backing pixmaps bottom-to-top,
// skipping our own overlay windows. Capturing the root window instead would
// read back our own output and feed it into the next frame.
// Overlay: override-redirect (the window manager never decorates, moves or
// lowers it), an empty XShape input region (all clicks fall through), raised
// periodically to stay on top of newly mapped windows.
// Hotkeys: XGrabKey on the root window over a private Display connection.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "platform/platform.hpp"

namespace delss::overlay {
namespace {

// Windows can vanish between XQueryTree and XGetImage; ignore those errors
// for the duration of a capture instead of letting Xlib abort the process.
int gXError = 0;
int ignoreXError(Display* /*display*/, XErrorEvent* event) {
  gXError = event->error_code;
  return 0;
}

class TrappedErrors {
 public:
  explicit TrappedErrors(Display* d) : display_(d) {
    XSync(display_, False);
    gXError = 0;
    previous_ = XSetErrorHandler(ignoreXError);
  }
  ~TrappedErrors() {
    XSync(display_, False);
    XSetErrorHandler(previous_);
  }
  TrappedErrors(const TrappedErrors&) = delete;
  TrappedErrors& operator=(const TrappedErrors&) = delete;
  TrappedErrors(TrappedErrors&&) = delete;
  TrappedErrors& operator=(TrappedErrors&&) = delete;

 private:
  Display* display_;
  int (*previous_)(Display*, XErrorEvent*) = nullptr;
};

struct Rect {
  int x, y, w, h;
};

Rect intersect(const Rect& a, const Rect& b) {
  const int x0 = std::max(a.x, b.x);
  const int y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x + a.w, b.x + b.w);
  const int y1 = std::min(a.y + a.h, b.y + b.h);
  return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

class X11Capture final : public ScreenCapture {
 public:
  X11Capture(const MonitorInfo& monitor, ExcludedWindows excluded)
      : rect_{monitor.x, monitor.y, monitor.width, monitor.height}, excluded_(std::move(excluded)) {
    // A private connection: capture never interleaves with GLFW's requests.
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) throw std::runtime_error("X11 capture: cannot open display");
    int event = 0;
    int error = 0;
    int major = 0;
    int minor = 2;
    if (XCompositeQueryExtension(display_, &event, &error) == 0 ||
        XCompositeQueryVersion(display_, &major, &minor) == 0 || (major == 0 && minor < 2)) {
      XCloseDisplay(display_);
      throw std::runtime_error("X11 capture needs the Composite extension >= 0.2");
    }
    root_ = DefaultRootWindow(display_);
    // Automatic redirection coexists with a compositing manager's manual one.
    XCompositeRedirectSubwindows(display_, root_, CompositeRedirectAutomatic);
    XSync(display_, False);
  }

  ~X11Capture() override {
    XCompositeUnredirectSubwindows(display_, root_, CompositeRedirectAutomatic);
    XCloseDisplay(display_);
  }
  X11Capture(const X11Capture&) = delete;
  X11Capture& operator=(const X11Capture&) = delete;
  X11Capture(X11Capture&&) = delete;
  X11Capture& operator=(X11Capture&&) = delete;

  bool grab(CapturedFrame& f) override {
    f.width = rect_.w;
    f.height = rect_.h;
    f.stride = rect_.w * 4;
    f.format = PixelFormat::BGRA8;
    f.topDown = true;
    f.pixels.assign(static_cast<size_t>(f.stride) * static_cast<size_t>(f.height), 0);
    ++f.serial;

    std::vector<Window> skip;
    for (GLFWwindow* w : excluded_()) skip.push_back(glfwGetX11Window(w));

    const TrappedErrors trap(display_);
    Window rootReturn = 0;
    Window parent = 0;
    Window* children = nullptr;
    unsigned int count = 0;
    if (XQueryTree(display_, root_, &rootReturn, &parent, &children, &count) == 0) return false;
    // XQueryTree lists children bottom-most first: paint in that order.
    for (unsigned int i = 0; i < count; ++i) {
      const Window child = children[i];
      if (std::find(skip.begin(), skip.end(), child) != skip.end()) continue;
      XWindowAttributes a{};
      if (XGetWindowAttributes(display_, child, &a) == 0) continue;
      if (a.map_state != IsViewable || a.c_class == InputOnly) continue;
      const Rect win{a.x, a.y, a.width + 2 * a.border_width, a.height + 2 * a.border_width};
      const Rect r = intersect(win, rect_);
      if (r.w == 0 || r.h == 0) continue;
      const Pixmap pixmap = XCompositeNameWindowPixmap(display_, child);
      XImage* img = XGetImage(display_, pixmap, r.x - win.x, r.y - win.y, static_cast<unsigned>(r.w),
                              static_cast<unsigned>(r.h), AllPlanes, ZPixmap);
      if (img != nullptr) {
        blit(*img, a.depth == 32, r, f);
        XDestroyImage(img);
      }
      XFreePixmap(display_, pixmap);
    }
    if (children != nullptr) XFree(children);
    return true;
  }

  [[nodiscard]] std::string describe() const override { return "X11 XComposite (excludes overlay windows)"; }

 private:
  // Copy (or, for ARGB windows, alpha-blend premultiplied) one window's
  // pixels into the frame. Assumes the ubiquitous 32 bpp little-endian
  // TrueColor layout (B, G, R, X/A bytes).
  void blit(const XImage& img, bool argb, const Rect& r, CapturedFrame& f) const {
    if (img.bits_per_pixel != 32) return;
    for (int y = 0; y < r.h; ++y) {
      const auto* s =
          reinterpret_cast<const std::uint8_t*>(img.data + static_cast<ptrdiff_t>(y) * img.bytes_per_line);
      std::uint8_t* d = f.pixels.data() +
                        (static_cast<size_t>(r.y - rect_.y + y) * static_cast<size_t>(f.stride)) +
                        static_cast<size_t>(r.x - rect_.x) * 4;
      if (!argb) {
        for (int x = 0; x < r.w; ++x) {
          d[x * 4] = s[x * 4];
          d[x * 4 + 1] = s[x * 4 + 1];
          d[x * 4 + 2] = s[x * 4 + 2];
          d[x * 4 + 3] = 255;
        }
        continue;
      }
      for (int x = 0; x < r.w; ++x) {
        const int alpha = s[x * 4 + 3];
        for (int c = 0; c < 3; ++c) {
          const int v = s[x * 4 + c] + (d[x * 4 + c] * (255 - alpha) + 127) / 255;
          d[x * 4 + c] = static_cast<std::uint8_t>(std::min(v, 255));
        }
        d[x * 4 + 3] = 255;
      }
    }
  }

  Display* display_ = nullptr;
  Window root_ = 0;
  Rect rect_;
  ExcludedWindows excluded_;
};

struct KeyBinding {
  KeyCode code;
  unsigned int mods;
  Action action;
};

class X11Platform final : public Platform {
 public:
  X11Platform() {
    hotkeyDisplay_ = XOpenDisplay(nullptr);
    if (hotkeyDisplay_ == nullptr) return;
    const Window root = DefaultRootWindow(hotkeyDisplay_);
    const auto keysym = [](Key k) -> KeySym {
      switch (k) {
        case Key::D:
          return XK_d;
        case Key::P:
          return XK_p;
        case Key::O:
          return XK_o;
        case Key::H:
          return XK_h;
        case Key::Q:
          return XK_q;
        case Key::Up:
          return XK_Up;
        case Key::Down:
          return XK_Down;
        case Key::Escape:
          return XK_Escape;
      }
      return NoSymbol;
    };
    const TrappedErrors trap(hotkeyDisplay_);
    for (const Binding& b : defaultBindings()) {
      unsigned int mods = 0;
      if ((b.chord.mods & kCtrl) != 0) mods |= ControlMask;
      if ((b.chord.mods & kAlt) != 0) mods |= Mod1Mask;
      if ((b.chord.mods & kShift) != 0) mods |= ShiftMask;
      if ((b.chord.mods & kSuper) != 0) mods |= Mod4Mask;
      const KeyCode code = XKeysymToKeycode(hotkeyDisplay_, keysym(b.chord.key));
      if (code == 0) continue;
      // Also grab with CapsLock / NumLock held, or the hotkey silently fails.
      for (const unsigned int extra : {0U, static_cast<unsigned>(LockMask), static_cast<unsigned>(Mod2Mask),
                                       static_cast<unsigned>(LockMask | Mod2Mask)}) {
        gXError = 0;
        XGrabKey(hotkeyDisplay_, code, mods | extra, root, False, GrabModeAsync, GrabModeAsync);
        XSync(hotkeyDisplay_, False);
        if (gXError != 0 && extra == 0)
          warnings_.push_back(describe(b.chord) + " is already grabbed by another app");
      }
      bindings_.push_back({code, mods, b.action});
    }
  }

  ~X11Platform() override {
    if (hotkeyDisplay_ != nullptr) XCloseDisplay(hotkeyDisplay_);
  }
  X11Platform(const X11Platform&) = delete;
  X11Platform& operator=(const X11Platform&) = delete;
  X11Platform(X11Platform&&) = delete;
  X11Platform& operator=(X11Platform&&) = delete;

  void initHints() override { glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11); }

  void windowHints() override {
    // GLES 3.0 through EGL: the reference GL flavour of libdelss on Linux.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  }

  void prepareWindow(GLFWwindow* window, const MonitorInfo& m) override {
    Display* d = glfwGetX11Display();
    const Window w = glfwGetX11Window(window);
    XSetWindowAttributes attrs{};
    attrs.override_redirect = True;
    XChangeWindowAttributes(d, w, CWOverrideRedirect, &attrs);
    XMoveResizeWindow(d, w, m.x, m.y, static_cast<unsigned>(m.width), static_cast<unsigned>(m.height));
    // Empty input shape: the pointer never hits this window (GLFW's
    // MOUSE_PASSTHROUGH does the same; set it explicitly to be certain).
    const Region empty = XCreateRegion();
    XShapeCombineRegion(d, w, ShapeInput, 0, 0, empty, ShapeSet);
    XDestroyRegion(empty);
    XFlush(d);
  }

  void keepOnTop(GLFWwindow* window) override {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastRaise_ < std::chrono::milliseconds(250)) return;
    lastRaise_ = now;
    XRaiseWindow(glfwGetX11Display(), glfwGetX11Window(window));
  }

  std::unique_ptr<ScreenCapture> createCapture(const MonitorInfo& m, ExcludedWindows excluded) override {
    return std::make_unique<X11Capture>(m, std::move(excluded));
  }

  void pollHotkeys(std::vector<Command>& out) override {
    if (hotkeyDisplay_ == nullptr) return;
    constexpr unsigned int kRelevant = ControlMask | Mod1Mask | ShiftMask | Mod4Mask;
    while (XPending(hotkeyDisplay_) > 0) {
      XEvent e{};
      XNextEvent(hotkeyDisplay_, &e);
      if (e.type != KeyPress) continue;
      for (const KeyBinding& b : bindings_) {
        if (e.xkey.keycode == b.code && (e.xkey.state & kRelevant) == b.mods) out.push_back({b.action});
      }
    }
  }

  [[nodiscard]] std::vector<std::string> warnings() const override {
    auto w = warnings_;
    if (hotkeyDisplay_ == nullptr) w.emplace_back("cannot open X display for hotkeys");
    return w;
  }
  [[nodiscard]] std::string name() const override { return "X11"; }

 private:
  Display* hotkeyDisplay_ = nullptr;
  std::vector<KeyBinding> bindings_;
  std::vector<std::string> warnings_;
  std::chrono::steady_clock::time_point lastRaise_{};
};

}  // namespace

std::unique_ptr<Platform> createPlatform() { return std::make_unique<X11Platform>(); }

}  // namespace delss::overlay
