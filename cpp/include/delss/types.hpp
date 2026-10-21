// Plain data types shared by the generated manifest and the public API.
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace delss {

using Rgb = std::array<float, 3>;

enum class ParamType { Float, Int, Color, Palette };

/// One artist parameter value. Numbers live in value[0], colors in all three
/// components, palettes are referenced by name ("none" = per-channel levels).
struct ParamValue {
  Rgb value{};
  std::string palette;

  static ParamValue number(float v) { return {{v, 0.0F, 0.0F}, {}}; }
  static ParamValue color(Rgb c) { return {c, {}}; }
  static ParamValue named(std::string name) { return {{}, std::move(name)}; }
};

struct ParamInfo {
  std::string_view name;
  ParamType type;
  ParamValue defaultValue;
  float min;
  float max;
};

struct PassInfo {
  std::string_view name;
  std::string_view label;
  std::string_view shader;  // complete GLSL ES 3.00 fragment shader
  std::vector<ParamInfo> params;
};

struct PaletteInfo {
  std::string_view name;
  std::string_view label;
  std::vector<Rgb> colors;
};

struct PresetStep {
  std::string pass;
  std::vector<std::pair<std::string, ParamValue>> params;
};

struct Preset {
  std::string_view id;
  std::string_view label;
  std::vector<PresetStep> passes;
};

}  // namespace delss
