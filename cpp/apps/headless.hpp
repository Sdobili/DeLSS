// Headless helpers shared by the CLI and the tests: an offscreen OpenGL ES 3.0
// context (or desktop GL 3.3 core when built with DELSS_GL_DESKTOP) through EGL (Mesa llvmpipe works without
// a GPU or display) and PNG load/save that converts between top-down images and bottom-up GL textures.
#pragma once

#include <EGL/egl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "delss/gl.hpp"

namespace delss::headless {

class EglContext {
 public:
  EglContext();
  ~EglContext();
  EglContext(const EglContext&) = delete;
  EglContext& operator=(const EglContext&) = delete;
  EglContext(EglContext&&) = delete;
  EglContext& operator=(EglContext&&) = delete;

  [[nodiscard]] std::string renderer() const;

 private:
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
};

struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;  // top-down, 4 bytes per pixel
};

Image loadPng(const std::string& path);
void savePng(const std::string& path, const Image& image);

/// Upload a top-down image as a bottom-up RGBA8 texture (GL convention).
GLuint uploadTexture(const Image& image);

/// Render-target helper: RGBA8 texture plus framebuffer.
struct Canvas {
  GLuint texture = 0;
  GLuint framebuffer = 0;
  int width = 0;
  int height = 0;
};
Canvas createCanvas(int width, int height);
void destroyCanvas(Canvas& canvas);
Image readCanvas(const Canvas& canvas);

struct Diff {
  double mean = 0.0;
  double badRatio = 0.0;
  int max = 0;
};

/// Golden comparison shared by every test (same tolerance as the WebGL tests):
/// a pixel is bad only if nothing in the reference's 3x3 neighbourhood is
/// within 16, so one-pixel edge shifts from GPU precision do not fail.
Diff compare(const Image& a, const Image& b);
bool passes(const Diff& d);

}  // namespace delss::headless
