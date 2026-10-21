// delss-cli: stylize an image offline with the same shaders as the web demo.
//
//   delss-cli --preset cel in.png out.png [--depth d.png] [--normal n.png]
//   delss-cli --list
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "delss/delss.hpp"
#include "headless.hpp"

namespace {

int usage() {
  std::fputs(
      "usage: delss-cli --preset <id> [--depth d.png] [--normal n.png] [--frames N] in.png out.png\n"
      "       delss-cli --list\n",
      stderr);
  return 2;
}

int run(const std::vector<std::string_view>& args) {
  std::string preset;
  std::string depthPath;
  std::string normalPath;
  int frames = 1;
  std::vector<std::string> positional;
  for (size_t i = 0; i < args.size(); ++i) {
    const auto next = [&]() -> std::string {
      if (i + 1 >= args.size()) throw std::invalid_argument("missing value after " + std::string(args[i]));
      return std::string(args[++i]);
    };
    if (args[i] == "--list") {
      for (const auto& p : delss::presets())
        std::printf("%-10s %s\n", std::string(p.id).c_str(), std::string(p.label).c_str());
      return 0;
    }
    if (args[i] == "--preset") {
      preset = next();
    } else if (args[i] == "--depth") {
      depthPath = next();
    } else if (args[i] == "--normal") {
      normalPath = next();
    } else if (args[i] == "--frames") {
      frames = std::stoi(next());
    } else {
      positional.emplace_back(args[i]);
    }
  }
  if (preset.empty() || positional.size() != 2) return usage();

  {
    const delss::headless::EglContext context;
    const auto color = delss::headless::loadPng(positional[0]);
    delss::FrameInputs inputs;
    inputs.color = delss::headless::uploadTexture(color);
    if (!depthPath.empty())
      inputs.depth = delss::headless::uploadTexture(delss::headless::loadPng(depthPath));
    if (!normalPath.empty())
      inputs.normal = delss::headless::uploadTexture(delss::headless::loadPng(normalPath));

    delss::Pipeline pipeline;
    pipeline.setChain(delss::presetChain(preset));
    auto canvas = delss::headless::createCanvas(color.width, color.height);
    for (int f = 0; f < frames; ++f) pipeline.render(inputs, color.width, color.height, canvas.framebuffer);
    delss::headless::savePng(positional[1], delss::headless::readCanvas(canvas));
    std::printf("%s -> %s (%s, %s)\n", positional[0].c_str(), positional[1].c_str(), preset.c_str(),
                context.renderer().c_str());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(std::vector<std::string_view>(argv + 1, argv + argc));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "delss-cli: %s\n", e.what());
  } catch (...) {
    std::fputs("delss-cli: unknown error\n", stderr);
  }
  return 1;
}
