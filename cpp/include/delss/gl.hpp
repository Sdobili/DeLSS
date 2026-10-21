// One GL header for every platform DeLSS runs on.
//
// The shaders are written once in GLSL ES 3.00. Three API flavours host them:
//   * OpenGL ES 3.0 (Linux/Android/ANGLE, the reference): linked directly.
//   * Desktop OpenGL 3.3 core on Windows/Linux: entry points are loaded at
//     runtime through delss::gl::load() with a getProcAddress callback (e.g.
//     glfwGetProcAddress). Shaders get their #version line rewritten.
//   * Desktop OpenGL 4.1 core on macOS: OpenGL.framework exports everything,
//     so no loader is needed; shaders are rewritten the same way.
// Define DELSS_GL_DESKTOP to select a desktop flavour.
#pragma once

#include <string>
#include <string_view>

#if !defined(DELSS_GL_DESKTOP)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>

#include "delss/gl_funcs.inc"
// NOLINTBEGIN
#define DELSS_GL_DECLARE(name, type) extern type delss_##name;
DELSS_GL_FUNCS(DELSS_GL_DECLARE)
#undef DELSS_GL_DECLARE
#define DELSS_GL_LOADER 1
#include "delss/gl_macros.inc"
// NOLINTEND
#endif

namespace delss::gl {

using ProcLoader = void* (*)(const char* name);

/// Resolve GL entry points. Required once per process on desktop Windows and
/// Linux (after a context is current); a no-op returning true elsewhere.
/// Returns false and names the first missing function in `missing`.
bool load(ProcLoader loader, std::string* missing = nullptr);

/// True when shaders must be translated from GLSL ES 3.00 to desktop GLSL.
constexpr bool kDesktop =
#if defined(DELSS_GL_DESKTOP)
    true;
#else
    false;
#endif

/// Translate a GLSL ES 3.00 shader for the current GL flavour. ES 3.00 is a
/// subset of desktop GLSL 3.30 apart from the #version line (precision
/// qualifiers are accepted and ignored), so this is a one-line rewrite and the
/// shader files stay the single source of truth.
std::string translateShader(std::string_view es300, bool desktop = kDesktop);

}  // namespace delss::gl
