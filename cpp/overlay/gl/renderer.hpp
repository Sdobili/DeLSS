// Per-window GL work: upload the captured frame, run the DeLSS chain into the
// window's default framebuffer, then blit the HUD on top. One Renderer per
// overlay window (each window has its own GL context).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/frame.hpp"
#include "core/hud.hpp"
#include "delss/delss.hpp"

namespace delss::overlay {

class Renderer {
 public:
  Renderer();
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  /// Rebuilds the chain only when preset or intensity actually changed.
  void configure(const std::string& presetId, float intensity);
  void upload(const CapturedFrame& frame);
  void draw(int framebufferWidth, int framebufferHeight, const HudImage* hud, int hudScale);
  /// Read the default framebuffer back as top-down RGBA (verification dumps).
  [[nodiscard]] std::vector<std::uint8_t> readback(int width, int height) const;
  [[nodiscard]] bool hasFrame() const { return sourceWidth_ > 0; }

 private:
  Pipeline pipeline_;
  std::string presetId_;
  float intensity_ = -1.0F;
  GLuint source_ = 0;
  int sourceWidth_ = 0;
  int sourceHeight_ = 0;
  std::vector<std::uint8_t> staging_;
  GLuint hudTexture_ = 0;
  GLuint hudFramebuffer_ = 0;
};

}  // namespace delss::overlay
