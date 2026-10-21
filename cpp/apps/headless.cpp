#include "headless.hpp"

#include <EGL/eglext.h>

#include <array>
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
  const std::array<EGLint, 5> configAttribs{EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE,
                                            EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (eglChooseConfig(display_, configAttribs.data(), &config, 1, &count) != EGL_TRUE || count == 0) {
    // Surfaceless displays may expose configs without pbuffer support.
    const std::array<EGLint, 3> esOnly{EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    if (eglChooseConfig(display_, esOnly.data(), &config, 1, &count) != EGL_TRUE || count == 0) {
      throw std::runtime_error("EGL: no ES3 config");
    }
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  const std::array<EGLint, 3> contextAttribs{EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs.data());
  if (context_ == EGL_NO_CONTEXT) throw std::runtime_error("EGL: cannot create ES 3.0 context");
  if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_) != EGL_TRUE) {
    throw std::runtime_error("EGL: eglMakeCurrent failed (needs EGL_KHR_surfaceless_context)");
  }
}

EglContext::~EglContext() {
  eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display_, context_);
  eglTerminate(display_);
}

std::string EglContext::renderer() const { return reinterpret_cast<const char*>(glGetString(GL_RENDERER)); }

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
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
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

}  // namespace delss::headless
