#include "gl/renderer.hpp"

#include "core/frame.hpp"

namespace delss::overlay {

Renderer::Renderer() {
  glGenTextures(1, &source_);
  glBindTexture(GL_TEXTURE_2D, source_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenTextures(1, &hudTexture_);
  glGenFramebuffers(1, &hudFramebuffer_);
}

Renderer::~Renderer() {
  glDeleteTextures(1, &source_);
  glDeleteTextures(1, &hudTexture_);
  glDeleteFramebuffers(1, &hudFramebuffer_);
}

void Renderer::configure(const std::string& presetId, float intensity) {
  if (presetId == presetId_ && intensity == intensity_) return;
  auto chain = presetChain(presetId);
  for (Step& s : chain) s.intensity = intensity;
  pipeline_.setChain(std::move(chain));  // also resets temporal history
  presetId_ = presetId;
  intensity_ = intensity;
}

void Renderer::upload(const CapturedFrame& frame) {
  toGlRgba(frame, staging_);
  glBindTexture(GL_TEXTURE_2D, source_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (frame.width != sourceWidth_ || frame.height != sourceHeight_) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 staging_.data());
    sourceWidth_ = frame.width;
    sourceHeight_ = frame.height;
    pipeline_.resetHistory();
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE,
                    staging_.data());
  }
}

void Renderer::draw(int width, int height, const HudImage* hud, int hudScale) {
  if (!hasFrame()) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.0F, 0.0F, 0.0F, 0.0F);  // nothing captured yet: fully transparent
    glClear(GL_COLOR_BUFFER_BIT);
    return;
  }
  // Color only: no depth, normals or motion from a screen grab. Passes fall
  // back to luminance edges and history-only temporal blending (see shaders).
  pipeline_.render(FrameInputs{source_}, width, height, 0);
  if (hud == nullptr || hud->width == 0) return;
  glBindTexture(GL_TEXTURE_2D, hudTexture_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hud->width, hud->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               hud->rgba.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, hudFramebuffer_);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hudTexture_, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  const int margin = 8;
  const int x0 = margin;
  const int y1 = height - margin;  // top-left corner of the screen
  glBlitFramebuffer(0, 0, hud->width, hud->height, x0, y1 - hud->height * hudScale,
                    x0 + hud->width * hudScale, y1, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

std::vector<std::uint8_t> Renderer::readback(int width, int height) const {
  std::vector<std::uint8_t> raw(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
  std::vector<std::uint8_t> out;
  flipRowsRgba(raw.data(), width, height, out);
  return out;
}

}  // namespace delss::overlay
