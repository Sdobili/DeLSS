#include "platform/platform.hpp"

#include <cstdio>

namespace delss::overlay {
void Platform::reportError(const std::string& message) {
  std::fprintf(stderr, "delss-overlay: %s\n", message.c_str());
}
}  // namespace delss::overlay
