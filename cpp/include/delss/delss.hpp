// DeLSS C++ reference library: the same GLSL passes as the WebGL2 runtime,
// driven through OpenGL ES 3.0 (desktop GL 4.3+ also runs ES 3.0 shaders via
// ARB_ES3_compatibility). Requires a current GL context on the calling thread.
#pragma once

#include <GLES3/gl3.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "delss/types.hpp"

namespace delss {

/// Engine-side inputs, mirroring DLSS: color is required, the rest optional
/// (0 = not provided). Textures are sampled at uv = gl_FragCoord / size with
/// (0,0) at the bottom-left, i.e. standard GL orientation.
struct FrameInputs {
  GLuint color = 0;
  GLuint depth = 0;   // r = linear view depth / far, in [0,1]
  GLuint normal = 0;  // rgb = view-space normal * 0.5 + 0.5
  GLuint motion = 0;  // rg = (uv_current - uv_previous), use a float format
};

/// One pass in the chain with fully resolved parameters.
struct Step {
  std::string pass;
  std::map<std::string, ParamValue, std::less<>> params;
  float intensity = 1.0F;
  bool enabled = true;
};

/// Built-in metadata (all generated from shaders/ by tools/gen.py).
const std::vector<PassInfo>& passes();
const std::vector<PaletteInfo>& palettes();
const std::vector<Preset>& presets();

/// Build a step for `pass` with every parameter at its default, then apply
/// `overrides`. Throws std::invalid_argument on unknown names.
Step makeStep(std::string_view pass, const std::vector<std::pair<std::string, ParamValue>>& overrides = {});

/// The editable chain of a built-in preset. Throws on an unknown id.
std::vector<Step> presetChain(std::string_view id);

class Pipeline {
 public:
  Pipeline();
  ~Pipeline();
  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;
  Pipeline(Pipeline&&) noexcept;
  Pipeline& operator=(Pipeline&&) noexcept;

  /// Replace the chain; validates every step. Resets temporal history.
  void setChain(std::vector<Step> chain);
  [[nodiscard]] const std::vector<Step>& chain() const;

  /// Run the chain and write the result to `target` (0 = default framebuffer).
  void render(const FrameInputs& inputs, int width, int height, GLuint target = 0);

  /// Forget the previous frame (camera cut, source change).
  void resetHistory();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace delss
