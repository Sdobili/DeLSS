#include "delss/delss.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "delss/generated.hpp"

namespace delss {
namespace {

// Texture units are fixed so samplers are bound once per program.
enum Unit : GLint { kSrc = 0, kColor, kDepth, kNormal, kMotion, kHistory };

constexpr std::array<std::pair<const char*, GLint>, 6> kSamplers{{
    {"uSrc", kSrc},
    {"uColor", kColor},
    {"uDepth", kDepth},
    {"uNormal", kNormal},
    {"uMotion", kMotion},
    {"uHistory", kHistory},
}};

GLuint compile(GLenum type, std::string_view source, std::string_view label) {
  const GLuint shader = glCreateShader(type);
  const std::string translated = gl::translateShader(source);
  const char* text = translated.data();
  const auto length = static_cast<GLint>(translated.size());
  glShaderSource(shader, 1, &text, &length);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    std::array<char, 4096> log{};
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("DeLSS: " + std::string(label) + " failed to compile:\n" + log.data());
  }
  return shader;
}

const PassInfo& findPass(std::string_view name) {
  const auto& all = passes();
  const auto it = std::find_if(all.begin(), all.end(), [&](const PassInfo& p) { return p.name == name; });
  if (it == all.end()) throw std::invalid_argument("DeLSS: unknown pass \"" + std::string(name) + "\"");
  return *it;
}

const PaletteInfo* findPalette(std::string_view name) {
  const auto& all = palettes();
  const auto it = std::find_if(all.begin(), all.end(), [&](const PaletteInfo& p) { return p.name == name; });
  return it == all.end() ? nullptr : &*it;
}

void validate(const Step& step) {
  const PassInfo& info = findPass(step.pass);
  for (const ParamInfo& p : info.params) {
    if (step.params.find(p.name) == step.params.end()) {
      throw std::invalid_argument("DeLSS: pass \"" + step.pass + "\" is missing param \"" +
                                  std::string(p.name) + "\"; build steps with makeStep()");
    }
  }
  for (const auto& entry : step.params) {
    const std::string& name = entry.first;
    const ParamValue& value = entry.second;
    const auto it = std::find_if(info.params.begin(), info.params.end(),
                                 [&name](const ParamInfo& p) { return p.name == name; });
    if (it == info.params.end()) {
      throw std::invalid_argument("DeLSS: pass \"" + step.pass + "\" has no param \"" + name + "\"");
    }
    if (it->type == ParamType::Palette && value.palette != "none" && findPalette(value.palette) == nullptr) {
      throw std::invalid_argument("DeLSS: unknown palette \"" + value.palette + "\"");
    }
  }
}

struct Target {
  GLuint texture = 0;
  GLuint framebuffer = 0;
};

Target createTarget(int width, int height) {
  Target t;
  glGenTextures(1, &t.texture);
  glBindTexture(GL_TEXTURE_2D, t.texture);
  // glTexImage2D rather than glTexStorage2D: the latter is missing on macOS GL 4.1.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &t.framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, t.framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
  return t;
}

void destroy(Target& t) {
  glDeleteTextures(1, &t.texture);
  glDeleteFramebuffers(1, &t.framebuffer);
  t = {};
}

struct Program {
  GLuint id = 0;
  std::unordered_map<std::string, GLint> uniforms;

  [[nodiscard]] GLint at(const std::string& name) const {
    const auto it = uniforms.find(name);
    return it == uniforms.end() ? -1 : it->second;
  }
};

}  // namespace

const std::vector<PassInfo>& passes() {
  static const std::vector<PassInfo> kPasses = generated::passes();
  return kPasses;
}

const std::vector<PaletteInfo>& palettes() {
  static const std::vector<PaletteInfo> kPalettes = generated::palettes();
  return kPalettes;
}

const std::vector<Preset>& presets() {
  static const std::vector<Preset> kPresets = generated::presets();
  return kPresets;
}

Step makeStep(std::string_view pass, const std::vector<std::pair<std::string, ParamValue>>& overrides) {
  const PassInfo& info = findPass(pass);
  Step step;
  step.pass = std::string(pass);
  for (const ParamInfo& p : info.params) step.params.emplace(std::string(p.name), p.defaultValue);
  for (const auto& [name, value] : overrides) {
    if (step.params.find(name) == step.params.end()) {
      throw std::invalid_argument("DeLSS: pass \"" + step.pass + "\" has no param \"" + name + "\"");
    }
    step.params[name] = value;
  }
  validate(step);
  return step;
}

std::vector<Step> presetChain(std::string_view id) {
  const auto& all = presets();
  const auto it = std::find_if(all.begin(), all.end(), [&](const Preset& p) { return p.id == id; });
  if (it == all.end()) throw std::invalid_argument("DeLSS: unknown preset \"" + std::string(id) + "\"");
  std::vector<Step> chain;
  chain.reserve(it->passes.size());
  for (const PresetStep& s : it->passes) chain.push_back(makeStep(s.pass, s.params));
  return chain;
}

struct Pipeline::Impl {
  std::vector<Step> chain;
  GLuint vertex = 0;
  GLuint vao = 0;
  GLuint dummy = 0;
  GLuint scratch = 0;
  std::unordered_map<std::string, Program> programs;
  std::array<Target, 2> ping{};
  std::array<Target, 2> history{};  // [0] = being written, [1] = previous frame
  int width = 0;
  int height = 0;
  int frame = 0;
  bool historyValid = false;

  Impl() {
    vertex = compile(GL_VERTEX_SHADER, generated::kVertexShader, "fullscreen.vert");
    glGenVertexArrays(1, &vao);
    glGenTextures(1, &dummy);
    glBindTexture(GL_TEXTURE_2D, dummy);
    constexpr std::array<unsigned char, 4> kBlack{0, 0, 0, 0};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kBlack.data());
    glGenFramebuffers(1, &scratch);
  }

  ~Impl() {
    for (auto& [name, p] : programs) glDeleteProgram(p.id);
    for (Target& t : ping) destroy(t);
    for (Target& t : history) destroy(t);
    glDeleteShader(vertex);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &dummy);
    glDeleteFramebuffers(1, &scratch);
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  const Program& program(const std::string& name) {
    if (const auto it = programs.find(name); it != programs.end()) return it->second;
    const PassInfo& info = findPass(name);
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, info.shader, name + ".frag");
    Program p;
    p.id = glCreateProgram();
    glAttachShader(p.id, vertex);
    glAttachShader(p.id, fragment);
    glLinkProgram(p.id);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
      std::array<char, 4096> log{};
      glGetProgramInfoLog(p.id, static_cast<GLsizei>(log.size()), nullptr, log.data());
      glDeleteProgram(p.id);
      throw std::runtime_error("DeLSS: " + name + " failed to link:\n" + log.data());
    }
    GLint count = 0;
    glGetProgramiv(p.id, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
      std::array<char, 256> buf{};
      GLint size = 0;
      GLenum type = 0;
      glGetActiveUniform(p.id, static_cast<GLuint>(i), static_cast<GLsizei>(buf.size()), nullptr, &size, &type, buf.data());
      std::string uname(buf.data());
      const GLint loc = glGetUniformLocation(p.id, uname.c_str());
      if (const auto bracket = uname.find('['); bracket != std::string::npos) uname.resize(bracket);
      p.uniforms.emplace(uname, loc);
    }
    glUseProgram(p.id);
    for (const auto& [sampler, unit] : kSamplers) {
      if (const GLint loc = p.at(sampler); loc >= 0) glUniform1i(loc, unit);
    }
    return programs.emplace(name, std::move(p)).first->second;
  }

  void resize(int w, int h) {
    if (w == width && h == height) return;
    for (Target& t : ping) {
      destroy(t);
      t = createTarget(w, h);
    }
    for (Target& t : history) {
      destroy(t);
      t = createTarget(w, h);
    }
    width = w;
    height = h;
    historyValid = false;
  }

  static void setParams(const Program& p, const Step& step) {
    const PassInfo& info = findPass(step.pass);
    for (const ParamInfo& param : info.params) {
      const ParamValue& v = step.params.at(std::string(param.name));
      const std::string uname = "u_" + std::string(param.name);
      switch (param.type) {
        case ParamType::Float:
          glUniform1f(p.at(uname), v.value[0]);
          break;
        case ParamType::Int:
          glUniform1i(p.at(uname), static_cast<GLint>(v.value[0]));
          break;
        case ParamType::Color:
          glUniform3fv(p.at(uname), 1, v.value.data());
          break;
        case ParamType::Palette: {
          std::array<float, 48> flat{};
          GLint n = 0;
          if (const PaletteInfo* pal = findPalette(v.palette)) {
            n = static_cast<GLint>(pal->colors.size());
            for (GLint i = 0; i < n; ++i) {
              std::copy_n(pal->colors[static_cast<size_t>(i)].begin(), 3,
                          flat.begin() + static_cast<std::ptrdiff_t>(i) * 3);
            }
          }
          glUniform3fv(p.at(uname), 16, flat.data());
          glUniform1i(p.at(uname + "Size"), n);
          break;
        }
      }
    }
  }

  void bind(GLint unit, GLuint tex) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex != 0 ? tex : dummy);
  }

  void render(const FrameInputs& in, int w, int h, GLuint target) {
    resize(w, h);
    bind(kColor, in.color);
    bind(kDepth, in.depth);
    bind(kNormal, in.normal);
    bind(kMotion, in.motion);
    bind(kHistory, history[1].texture);
    glBindVertexArray(vao);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, w, h);

    std::vector<const Step*> active;
    for (const Step& s : chain) {
      if (s.enabled) active.push_back(&s);
    }
    GLuint source = in.color;
    size_t pingIndex = 0;
    for (size_t i = 0; i < active.size(); ++i) {
      const Step& step = *active[i];
      const Target& dest = i + 1 == active.size() ? history[0] : ping[pingIndex];
      const Program& p = program(step.pass);
      glUseProgram(p.id);
      bind(kSrc, source);
      glBindFramebuffer(GL_FRAMEBUFFER, dest.framebuffer);
      glUniform1i(p.at("uHasDepth"), in.depth != 0 ? 1 : 0);
      glUniform1i(p.at("uHasNormal"), in.normal != 0 ? 1 : 0);
      glUniform1i(p.at("uHasMotion"), in.motion != 0 ? 1 : 0);
      glUniform1i(p.at("uHasHistory"), historyValid ? 1 : 0);
      glUniform2f(p.at("uResolution"), static_cast<float>(w), static_cast<float>(h));
      glUniform1i(p.at("uFrame"), frame);
      glUniform1f(p.at("uIntensity"), step.intensity);
      setParams(p, step);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      source = dest.texture;
      pingIndex ^= 1U;
    }

    if (!active.empty()) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, history[0].framebuffer);
      std::swap(history[0], history[1]);
      historyValid = true;
    } else {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, scratch);
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, in.color, 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    ++frame;
  }
};

Pipeline::Pipeline() : impl_(std::make_unique<Impl>()) {}
Pipeline::~Pipeline() = default;
Pipeline::Pipeline(Pipeline&&) noexcept = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

void Pipeline::setChain(std::vector<Step> chain) {
  for (const Step& s : chain) validate(s);
  impl_->chain = std::move(chain);
  impl_->historyValid = false;
}

const std::vector<Step>& Pipeline::chain() const { return impl_->chain; }

void Pipeline::render(const FrameInputs& inputs, int width, int height, GLuint target) {
  impl_->render(inputs, width, height, target);
}

void Pipeline::resetHistory() { impl_->historyValid = false; }

}  // namespace delss
