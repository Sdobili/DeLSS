// Builds without a tray backend: hotkeys and the settings file still work.
#include "platform/platform.hpp"

namespace delss::overlay {
std::unique_ptr<Tray> createTray() { return nullptr; }
}  // namespace delss::overlay
