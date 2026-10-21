#include "headless.hpp"

#include <EGL/eglext.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
// NOLINTBEGIN
#include "stb_image.h"
#include "stb_image_write.h"
// NOLINTEND

namespace delss::headless {

namespace {
#if defined(DELSS_GL_DESKTOP)
constexpr EGLint kRenderable = EGL_OPENGL_BIT;
constexpr EGLenum kApi = EGL_OPENGL_API;
const std::array<EGLint, 7> kContextAttribs{EGL_CONTEXT_MAJOR_VERSION,
                                            3,
                                            EGL_CONTEXT_MINOR_VERSION,
                                            3,
                                            EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                            EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                            EGL_NONE};
#else
constexpr EGLint kRenderable = EGL_OPENGL_ES3_BIT;
constexpr EGLenum kApi = EGL_OPENGL_ES_API;
const std::array<EGLint, 3> kContextAttribs{EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
#endif
}  // namespace

EglContext::EglContext() {
  // Prefer the surfaceless platform (no X/Wayland needed); fall back to the
  // default display for drivers without it.
  auto getPlatformDisplay =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (getPlatformDisplay != nullptr) {
    display_ = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  }
  if (display_ == EGL_NO_DISPLAY) display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
    throw std::runtime_error("EGL: no display");
  }
  const std::array<EGLint, 5> configAttribs{EGL_RENDERABLE_TYPE, kRenderable, EGL_SURFACE_TYPE,
                                            EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (eglChooseConfig(display_, configAttribs.data(), &config, 1, &count) != EGL_TRUE || count == 0) {
    // Surfaceless displays may expose configs without pbuffer support.
    const std::array<EGLint, 3> esOnly{EGL_RENDERABLE_TYPE, kRenderable, EGL_NONE};
    if (eglChooseConfig(display_, esOnly.data(), &config, 1, &count) != EGL_TRUE || count == 0) {
      throw std::runtime_error("EGL: no ES3 config");
    }
  }
  eglBindAPI(kApi);
  context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, kContextAttribs.data());
  if (context_ == EGL_NO_CONTEXT) throw std::runtime_error("EGL: cannot create a GL context");
  if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_) != EGL_TRUE) {
    throw std::runtime_error("EGL: eglMakeCurrent failed (needs EGL_KHR_surfaceless_context)");
  }
  std::string missing;
  if (!gl::load([](const char* name) { return reinterpret_cast<void*>(eglGetProcAddress(name)); },
                &missing)) {
    throw std::runtime_error("GL: missing entry point " + missing);
  }
}

EglContext::~EglContext() {
  eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display_, context_);
  eglTerminate(display_);
}

std::string EglContext::renderer() const {
  return std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))) + " / " +
         reinterpret_cast<const char*>(glGetString(GL_VERSION));
}

Image loadPng(const std::string& path) {
  Image img;
  int channels = 0;
  stbi_uc* data = stbi_load(path.c_str(), &img.width, &img.height, &channels, 4);
  if (data == nullptr) throw std::runtime_error("cannot read image " + path);
  img.rgba.assign(data, data + static_cast<size_t>(img.width) * img.height * 4);
  stbi_image_free(data);
  return img;
}

void savePng(const std::string& path, const Image& image) {
  if (stbi_write_png(path.c_str(), image.width, image.height, 4, image.rgba.data(), image.width * 4) == 0) {
    throw std::runtime_error("cannot write " + path);
  }
}

namespace {
std::vector<std::uint8_t> flipRows(const std::vector<std::uint8_t>& src, int width, int height) {
  std::vector<std::uint8_t> out(src.size());
  const size_t row = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height; ++y) {
    std::memcpy(out.data() + static_cast<size_t>(y) * row,
                src.data() + static_cast<size_t>(height - 1 - y) * row, row);
  }
  return out;
}
}  // namespace

GLuint uploadTexture(const Image& image) {
  const auto flipped = flipRows(image.rgba, image.width, image.height);
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               flipped.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return tex;
}

Canvas createCanvas(int width, int height) {
  Canvas c{0, 0, width, height};
  glGenTextures(1, &c.texture);
  glBindTexture(GL_TEXTURE_2D, c.texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenFramebuffers(1, &c.framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, c.framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.texture, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return c;
}

void destroyCanvas(Canvas& canvas) {
  glDeleteTextures(1, &canvas.texture);
  glDeleteFramebuffers(1, &canvas.framebuffer);
  canvas = {};
}

Image readCanvas(const Canvas& canvas) {
  std::vector<std::uint8_t> px(static_cast<size_t>(canvas.width) * canvas.height * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, canvas.framebuffer);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, canvas.width, canvas.height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return {canvas.width, canvas.height, flipRows(px, canvas.width, canvas.height)};
}

// Same tolerance as tests/web/browser.mjs: a pixel is bad only if nothing in
// the reference's 3x3 neighborhood matches it within 16 (edge shifts by one
// pixel from bilinear-precision differences are not visible regressions).
Diff compare(const Image& a, const Image& b) {
  Diff d;
  if (a.width != b.width || a.height != b.height) return {255.0, 1.0, 255};
  const int w = a.width;
  const int h = a.height;
  const auto diffAt = [&](size_t i, size_t j) {
    int px = 0;
    for (size_t c = 0; c < 3; ++c)
      px = std::max(px, std::abs(int{a.rgba[i * 4 + c]} - int{b.rgba[j * 4 + c]}));
    return px;
  };
  double sum = 0.0;
  size_t bad = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w)) + static_cast<size_t>(x);
      for (size_t c = 0; c < 3; ++c) sum += std::abs(int{a.rgba[i * 4 + c]} - int{b.rgba[i * 4 + c]});
      const int px = diffAt(i, i);
      d.max = std::max(d.max, px);
      if (px <= 16) continue;
      int best = px;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx >= 0 && ny >= 0 && nx < w && ny < h) {
            best = std::min(best, diffAt(i, (static_cast<size_t>(ny) * static_cast<size_t>(w)) +
                                                static_cast<size_t>(nx)));
          }
        }
      }
      if (best > 16) ++bad;
    }
  }
  const auto n = static_cast<double>(w) * h;
  d.mean = sum / (n * 3.0);
  d.badRatio = static_cast<double>(bad) / n;
  return d;
}

bool passes(const Diff& d) { return d.mean < 1.0 && d.badRatio < 0.005; }

}  // namespace delss::headless
