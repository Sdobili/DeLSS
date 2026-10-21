// Windows notification-area icon with a popup menu (Shell_NotifyIcon).
// A message-only window receives the icon's callbacks; glfwPollEvents()
// already pumps this thread's messages, so poll() just drains the queue.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h must come first.
#include <shellapi.h>

#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace delss::overlay {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kFirstCommand = 1000;

std::wstring widen(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

class Win32Tray final : public Tray {
 public:
  Win32Tray() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &Win32Tray::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DeLSSOverlayTray";
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"DeLSS", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance,
                            nullptr);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    icon_.cbSize = sizeof icon_;
    icon_.hWnd = hwnd_;
    icon_.uID = 1;
    icon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon_.uCallbackMessage = kTrayMessage;
    icon_.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));  // delss.ico (resource 1)
    lstrcpynW(icon_.szTip, L"DeLSS overlay (Ctrl+Alt+D)", ARRAYSIZE(icon_.szTip));
    Shell_NotifyIconW(NIM_ADD, &icon_);
  }
  ~Win32Tray() override {
    Shell_NotifyIconW(NIM_DELETE, &icon_);
    DestroyWindow(hwnd_);
  }
  Win32Tray(const Win32Tray&) = delete;
  Win32Tray& operator=(const Win32Tray&) = delete;
  Win32Tray(Win32Tray&&) = delete;
  Win32Tray& operator=(Win32Tray&&) = delete;

  void update(const TrayModel& m) override { model_ = m; }

  void poll(std::vector<Command>& out) override {
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
  }

 private:
  static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Win32Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr && msg == kTrayMessage &&
        (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP)) {
      self->showMenu();
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  void add(HMENU menu, const std::string& label, bool checked, Command c) {
    const UINT id = kFirstCommand + static_cast<UINT>(commands_.size());
    commands_.push_back(c);
    AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED), id, widen(label).c_str());
  }

  void showMenu() {
    commands_.clear();
    HMENU menu = CreatePopupMenu();
    add(menu, "Enabled\tCtrl+Alt+D", model_.enabled, {Action::SetEnabled, 0, model_.enabled ? 0.0F : 1.0F});
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < model_.presetLabels.size(); ++i) {
      add(menu, model_.presetLabels[i], static_cast<int>(i) == model_.preset,
          {Action::SetPreset, static_cast<int>(i)});
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (const int pct : {25, 50, 75, 100}) {
      add(menu, "Intensity " + std::to_string(pct) + "%",
          static_cast<int>(model_.intensity * 100.0F + 0.5F) == pct,
          {Action::SetIntensity, 0, static_cast<float>(pct) / 100.0F});
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < model_.monitorNames.size(); ++i) {
      add(menu, "Monitor " + model_.monitorNames[i], i < model_.monitors.size() && model_.monitors[i],
          {Action::ToggleMonitor, static_cast<int>(i)});
    }
    add(menu, "Show HUD (ms/frame)\tCtrl+Alt+H", model_.hud, {Action::ToggleHud});
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(menu, "Quit\tCtrl+Alt+Esc", false, {Action::Exit});
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(hwnd_);  // required so the menu closes when clicking elsewhere
    const UINT id = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);
    if (id >= kFirstCommand && id - kFirstCommand < commands_.size())
      pending_.push_back(commands_[id - kFirstCommand]);
  }

  HWND hwnd_ = nullptr;
  NOTIFYICONDATAW icon_{};
  TrayModel model_;
  std::vector<Command> commands_;
  std::vector<Command> pending_;
};

}  // namespace

std::unique_ptr<Tray> createTray() { return std::make_unique<Win32Tray>(); }

}  // namespace delss::overlay
