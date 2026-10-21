// Cross-implementation golden test: render every pass and preset through the
// C++ library on Mesa (llvmpipe) and compare with the goldens the WebGL tests
// produced on SwiftShader. Passing proves the shaders really are one source of
// truth: same GLSL, two runtimes, two GL implementations, same pixels.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "delss/delss.hpp"
#include "headless.hpp"

namespace fs = std::filesystem;
using delss::headless::Image;

namespace {

struct Diff {
  double mean = 0.0;
  double badRatio = 0.0;
  int max = 0;
};

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

}  // namespace

int main() {
  const fs::path root = DELSS_SOURCE_DIR;
  const fs::path out = fs::current_path() / "golden-cpp";
  fs::create_directories(out);
  int failures = 0;
  int checks = 0;

  try {
    const delss::headless::EglContext context;
    std::printf("renderer: %s\n", context.renderer().c_str());
    const auto photo = delss::headless::loadPng((root / "tests/fixtures/photo.png").string());
    const auto sceneColor = delss::headless::loadPng((root / "tests/fixtures/scene_color.png").string());
    const GLuint photoTex = delss::headless::uploadTexture(photo);
    const delss::FrameInputs photoIn{photoTex};
    const delss::FrameInputs sceneIn{
        delss::headless::uploadTexture(sceneColor),
        delss::headless::uploadTexture(
            delss::headless::loadPng((root / "tests/fixtures/scene_depth.png").string())),
        delss::headless::uploadTexture(
            delss::headless::loadPng((root / "tests/fixtures/scene_normal.png").string())),
    };
    auto canvas = delss::headless::createCanvas(photo.width, photo.height);

    const auto check = [&](const std::string& name, const std::vector<delss::Step>& chain,
                           const delss::FrameInputs& in) {
      delss::Pipeline pipeline;
      pipeline.setChain(chain);
      pipeline.render(in, photo.width, photo.height, canvas.framebuffer);
      const Image got = delss::headless::readCanvas(canvas);
      delss::headless::savePng((out / (name + ".png")).string(), got);
      const Image want = delss::headless::loadPng((root / "tests/golden/web" / (name + ".png")).string());
      const Diff d = compare(got, want);
      ++checks;
      const bool ok = passes(d);
      if (!ok) ++failures;
      std::printf("%-4s %-28s mean %.3f  >16 %.3f%%  max %d\n", ok ? "ok" : "FAIL", name.c_str(), d.mean,
                  d.badRatio * 100.0, d.max);
    };

    for (const auto& pass : delss::passes()) {
      check("pass_" + std::string(pass.name), {delss::makeStep(pass.name)}, photoIn);
    }
    check("pass_ink_gbuffer", {delss::makeStep("ink")}, sceneIn);
    for (const auto& preset : delss::presets()) {
      const auto chain = delss::presetChain(preset.id);
      check("preset_" + std::string(preset.id) + "_photo", chain, photoIn);
      check("preset_" + std::string(preset.id) + "_scene", chain, sceneIn);
    }

    // API contract: typos fail loudly instead of rendering garbage.
    const auto throws = [](const std::function<void()>& f) {
      try {
        f();
      } catch (const std::invalid_argument&) {
        return true;
      }
      return false;
    };
    ++checks;
    if (!throws([] { (void)delss::makeStep("nope"); }) ||
        !throws([] { (void)delss::makeStep("cel", {{"bandz", delss::ParamValue::number(3)}}); }) ||
        !throws([] { (void)delss::presetChain("nope"); })) {
      std::puts("FAIL invalid names must throw");
      ++failures;
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "golden_test: %s\n", e.what());
    return 1;
  }
  std::printf("%d/%d checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
