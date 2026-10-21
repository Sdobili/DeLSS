// Compositor screen shaders (tools/compositor.py -> compositor/hyprland/) are
// the Wayland path. Each one is a whole preset flattened into one fragment
// shader; render it over the photo fixture exactly as a compositor would (one
// full-screen draw sampling `tex`) and compare with the WebGL preset golden.
#include <array>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "delss/delss.hpp"
#include "delss/generated.hpp"
#include "headless.hpp"

namespace fs = std::filesystem;

namespace {
GLuint compile(GLenum type, const std::string& text) {
  const std::string src = delss::gl::translateShader(text);
  const GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = GL_FALSE;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    std::array<char, 4096> log{};
    glGetShaderInfoLog(s, log.size(), nullptr, log.data());
    throw std::runtime_error(log.data());
  }
  return s;
}
}  // namespace

int main() {
  const fs::path root = DELSS_SOURCE_DIR;
  int failures = 0;
  int checks = 0;
  try {
    const delss::headless::EglContext context;
    const auto photo = delss::headless::loadPng((root / "tests/fixtures/photo.png").string());
    const GLuint tex = delss::headless::uploadTexture(photo);
    auto canvas = delss::headless::createCanvas(photo.width, photo.height);
    const GLuint vs = compile(GL_VERTEX_SHADER, std::string(delss::generated::kVertexShader));
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    fs::create_directories("compositor-out");  // relative to the ctest build dir
    for (const auto& preset : delss::presets()) {
      const std::string id(preset.id);
      std::stringstream buf;
      buf << std::ifstream(root / "compositor/hyprland" / ("delss-" + id + ".frag")).rdbuf();
      std::string text = buf.str();
      const std::string from = "in vec2 v_texcoord;";
      text.replace(text.find(from), from.size(), "in vec2 vUv;\n#define v_texcoord vUv");
      ++checks;
      try {
        const GLuint fs = compile(GL_FRAGMENT_SHADER, text);
        const GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "tex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindFramebuffer(GL_FRAMEBUFFER, canvas.framebuffer);
        glViewport(0, 0, photo.width, photo.height);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        const auto got = delss::headless::readCanvas(canvas);
        delss::headless::savePng("compositor-out/" + id + ".png", got);
        const auto want =
            delss::headless::loadPng((root / "tests/golden/web" / ("preset_" + id + "_photo.png")).string());
        const auto d = delss::headless::compare(got, want);
        // Exact match is expected unless a pass samples its input between
        // texels: multi-pass bilinearly filters the RGBA8 intermediate there,
        // the flattened shader evaluates the previous pass exactly at that
        // point (ink width 1.5, VHS chroma shift). Those must stay close.
        const bool exact = delss::headless::passes(d);
        const bool close = d.mean < 8.0 && d.badRatio < 0.03;
        if (!exact && !close) ++failures;
        std::printf("%-6s hyprland/%-18s mean %.3f  >16 %.3f%%  max %d\n",
                    exact   ? "ok"
                    : close ? "approx"
                            : "FAIL",
                    ("delss-" + id + ".frag").c_str(), d.mean, d.badRatio * 100.0, d.max);
        glDeleteProgram(prog);
        glDeleteShader(fs);
      } catch (const std::exception& e) {
        ++failures;
        std::printf("FAIL %s: %s\n", id.c_str(), e.what());
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "compositor_test: %s\n", e.what());
    return 1;
  }
  std::printf("%d/%d compositor shaders pass (exact or approx)\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
