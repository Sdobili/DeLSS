#include "delss/gl.hpp"

#if defined(DELSS_GL_LOADER)
// NOLINTBEGIN
#define DELSS_GL_DEFINE(name, type) type delss_##name = nullptr;
DELSS_GL_FUNCS(DELSS_GL_DEFINE)
#undef DELSS_GL_DEFINE
// NOLINTEND
#endif

namespace delss::gl {

#if defined(DELSS_GL_LOADER)
bool load(ProcLoader loader, std::string* missing) {
  bool ok = true;
  // NOLINTBEGIN
#define DELSS_GL_RESOLVE(name, type)                    \
  delss_##name = reinterpret_cast<type>(loader(#name)); \
  if (delss_##name == nullptr && ok) {                  \
    ok = false;                                         \
    if (missing != nullptr) *missing = #name;           \
  }
  DELSS_GL_FUNCS(DELSS_GL_RESOLVE)
#undef DELSS_GL_RESOLVE
  // NOLINTEND
  return ok;
}
#else
bool load(ProcLoader /*loader*/, std::string* /*missing*/) { return true; }
#endif

std::string translateShader(std::string_view es300, bool desktop) {
  std::string out(es300);
  if (!desktop) return out;
  constexpr std::string_view kEs = "#version 300 es";
  if (out.compare(0, kEs.size(), kEs) == 0) out.replace(0, kEs.size(), "#version 330 core");
  return out;
}

}  // namespace delss::gl
