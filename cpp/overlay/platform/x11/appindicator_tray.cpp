// Linux tray: an Ayatana AppIndicator (StatusNotifierItem) with a GTK menu.
// Shown by GNOME (with the AppIndicator extension), KDE, XFCE, Cinnamon, ...
// GTK runs on the overlay's thread; poll() pumps its main context without
// blocking, so no second thread touches the menu.
// Newer Ayatana releases deprecate this API in favour of a GLib-only one that
// distros do not ship yet; the old API is still what every distro packages.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <libayatana-appindicator/app-indicator.h>

#include <cstdio>
#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace delss::overlay {
namespace {

class AppIndicatorTray final : public Tray {
 public:
  AppIndicatorTray() {
    indicator_ = app_indicator_new("delss-overlay", "applications-graphics",
                                   APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(indicator_, "DeLSS overlay");
  }
  ~AppIndicatorTray() override { g_object_unref(indicator_); }
  AppIndicatorTray(const AppIndicatorTray&) = delete;
  AppIndicatorTray& operator=(const AppIndicatorTray&) = delete;
  AppIndicatorTray(AppIndicatorTray&&) = delete;
  AppIndicatorTray& operator=(AppIndicatorTray&&) = delete;

  void update(const TrayModel& m) override {
    // Rebuilding a dozen items is cheap and keeps the menu a pure function of the state.
    building_ = true;
    GtkWidget* menu = gtk_menu_new();
    check(menu, m.enabled ? "Enabled (Ctrl+Alt+D)" : "Enable (Ctrl+Alt+D)", m.enabled,
          {Action::SetEnabled, 0, m.enabled ? 0.0F : 1.0F});
    separator(menu);
    for (size_t i = 0; i < m.presetLabels.size(); ++i) {
      check(menu, m.presetLabels[i], static_cast<int>(i) == m.preset,
            {Action::SetPreset, static_cast<int>(i)});
    }
    separator(menu);
    for (const int pct : {25, 50, 75, 100}) {
      const bool on = static_cast<int>(m.intensity * 100.0F + 0.5F) == pct;
      check(menu, "Intensity " + std::to_string(pct) + "%", on,
            {Action::SetIntensity, 0, static_cast<float>(pct) / 100.0F});
    }
    separator(menu);
    for (size_t i = 0; i < m.monitorNames.size(); ++i) {
      check(menu, "Monitor " + m.monitorNames[i], i < m.monitors.size() && m.monitors[i],
            {Action::ToggleMonitor, static_cast<int>(i)});
    }
    check(menu, "Show HUD (ms/frame)", m.hud, {Action::ToggleHud});
    separator(menu);
    item(menu, "Quit (Ctrl+Alt+Esc)", {Action::Exit});
    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator_, GTK_MENU(menu));
    building_ = false;
  }

  void poll(std::vector<Command>& out) override {
    while (g_main_context_iteration(nullptr, FALSE) != 0) {
    }
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
  }

 private:
  struct Slot {
    AppIndicatorTray* self;
    Command command;
  };

  static void onActivate(GtkMenuItem* /*item*/, gpointer data) {
    auto* slot = static_cast<Slot*>(data);
    if (!slot->self->building_) slot->self->pending_.push_back(slot->command);
  }

  void connect(GtkWidget* w, Command c) {
    auto* slot = new Slot{this, c};  // NOLINT(cppcoreguidelines-owning-memory): freed by the closure notify
    g_signal_connect_data(
        w, "activate", G_CALLBACK(onActivate), slot,
        [](gpointer p, GClosure* /*closure*/) { delete static_cast<Slot*>(p); }, GConnectFlags{});
  }
  void item(GtkWidget* menu, const std::string& label, Command c) {
    GtkWidget* w = gtk_menu_item_new_with_label(label.c_str());
    connect(w, c);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
  }
  void check(GtkWidget* menu, const std::string& label, bool on, Command c) {
    GtkWidget* w = gtk_check_menu_item_new_with_label(label.c_str());
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), on ? TRUE : FALSE);
    connect(w, c);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
  }
  static void separator(GtkWidget* menu) {
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
  }

  AppIndicator* indicator_ = nullptr;
  std::vector<Command> pending_;
  bool building_ = false;
};

}  // namespace

std::unique_ptr<Tray> createTray() {
  if (gtk_init_check(nullptr, nullptr) == FALSE) {
    std::fputs("warning: GTK unavailable, running without a tray icon\n", stderr);
    return nullptr;
  }
  return std::make_unique<AppIndicatorTray>();
}

}  // namespace delss::overlay
