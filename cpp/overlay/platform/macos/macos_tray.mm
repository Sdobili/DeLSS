// macOS menu-bar item (NSStatusItem) with the preset list, intensity,
// monitors, HUD and Quit. Menu actions queue Commands; poll() drains them.
#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace delss::overlay {
struct TrayQueue {
  std::vector<Command> commands;  // index = NSMenuItem.tag
  std::vector<Command> pending;
};
}  // namespace delss::overlay

@interface DelssTrayTarget : NSObject
@property(nonatomic) delss::overlay::TrayQueue* queue;
- (void)select:(NSMenuItem*)item;
@end

@implementation DelssTrayTarget
- (void)select:(NSMenuItem*)item {
  const auto i = static_cast<size_t>(item.tag);
  if (i < self.queue->commands.size()) self.queue->pending.push_back(self.queue->commands[i]);
}
@end

namespace delss::overlay {
namespace {

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

class MacTray final : public Tray {
 public:
  MacTray() {
    item_ = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    // Monochrome template image: macOS tints it for light/dark menu bars.
    NSImage* image = [NSImage imageNamed:@"MenuBarTemplate"];
    if (image != nil) {
      image.template = YES;
      image.size = NSMakeSize(18, 18);
      item_.button.image = image;
    } else {
      item_.button.title = @"DeLSS";
    }
    item_.button.toolTip = @"DeLSS Overlay";
    target_ = [[DelssTrayTarget alloc] init];
    target_.queue = &queue_;
  }
  ~MacTray() override { [[NSStatusBar systemStatusBar] removeStatusItem:item_]; }
  MacTray(const MacTray&) = delete;
  MacTray& operator=(const MacTray&) = delete;
  MacTray(MacTray&&) = delete;
  MacTray& operator=(MacTray&&) = delete;

  void update(const TrayModel& m) override {
    queue_.commands.clear();
    NSMenu* menu = [[NSMenu alloc] init];
    menu.autoenablesItems = NO;
    add(menu, "Enabled  (⌃⌥D)", m.enabled, {Action::SetEnabled, 0, m.enabled ? 0.0F : 1.0F});
    [menu addItem:[NSMenuItem separatorItem]];
    for (size_t i = 0; i < m.presetLabels.size(); ++i) {
      add(menu, m.presetLabels[i], static_cast<int>(i) == m.preset, {Action::SetPreset, static_cast<int>(i)});
    }
    [menu addItem:[NSMenuItem separatorItem]];
    for (const int pct : {25, 50, 75, 100}) {
      add(menu, "Intensity " + std::to_string(pct) + "%",
          static_cast<int>(m.intensity * 100.0F + 0.5F) == pct,
          {Action::SetIntensity, 0, static_cast<float>(pct) / 100.0F});
    }
    [menu addItem:[NSMenuItem separatorItem]];
    for (size_t i = 0; i < m.monitorNames.size(); ++i) {
      add(menu, "Monitor " + m.monitorNames[i], i < m.monitors.size() && m.monitors[i],
          {Action::ToggleMonitor, static_cast<int>(i)});
    }
    add(menu, "Show HUD (ms/frame)", m.hud, {Action::ToggleHud});
    [menu addItem:[NSMenuItem separatorItem]];
    add(menu, "Quit  (⌃⌥Esc)", false, {Action::Exit});
    item_.menu = menu;
    item_.button.appearsDisabled = !m.enabled;  // dimmed icon while the filter is off
  }

  void poll(std::vector<Command>& out) override {
    out.insert(out.end(), queue_.pending.begin(), queue_.pending.end());
    queue_.pending.clear();
  }

 private:
  void add(NSMenu* menu, const std::string& label, bool on, Command c) {
    NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:ns(label) action:@selector(select:) keyEquivalent:@""];
    mi.target = target_;
    mi.tag = static_cast<NSInteger>(queue_.commands.size());
    mi.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    queue_.commands.push_back(c);
    [menu addItem:mi];
  }

  NSStatusItem* item_ = nil;
  DelssTrayTarget* target_ = nil;
  TrayQueue queue_;
};

}  // namespace

std::unique_ptr<Tray> createTray() { return std::make_unique<MacTray>(); }

}  // namespace delss::overlay
