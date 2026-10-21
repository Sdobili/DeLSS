// DeLSS shared prelude. Inlined into every pass by tools/gen.py.
//
// Input contract (mirrors DLSS): every pass sees the original color frame and
// optional G-buffer channels. A pass that needs a channel checks its uHas*
// flag and falls back gracefully when the host did not provide it.
precision highp float;
precision highp int;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uSrc;      // output of the previous pass (the frame on pass 0)
uniform sampler2D uColor;    // original engine color, linear-ish sRGB, RGBA8
uniform sampler2D uDepth;    // r = linear depth in [0,1], 0 = near
uniform sampler2D uNormal;   // rgb = view-space normal * 0.5 + 0.5
uniform sampler2D uMotion;   // rg = screen-space motion in UV units (cur - prev)
uniform sampler2D uHistory;  // previous final output of this chain
uniform bool uHasDepth;
uniform bool uHasNormal;
uniform bool uHasMotion;
uniform bool uHasHistory;
uniform vec2 uResolution;    // output size in pixels
uniform int uFrame;          // frame counter, only used by passes that want noise
uniform float uIntensity;    // 0 = bypass, 1 = full effect (artist control)

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

float luma(vec3 c) { return dot(c, LUMA); }

vec2 texel() { return 1.0 / uResolution; }

vec4 src(vec2 uv) { return texture(uSrc, uv); }

// Classic 8x8 Bayer threshold in [0,1). Anchored to integer screen cells, so it
// never crawls when the camera is still and never shimmers between frames.
float bayer8(ivec2 p) {
  p &= 7;
  int x = p.x;
  int y = p.y ^ p.x;
  int v = ((y & 1) << 5) | ((x & 1) << 4) | ((y & 2) << 2) | ((x & 2) << 1) | ((y & 4) >> 1) |
          ((x & 4) >> 2);
  return (float(v) + 0.5) / 64.0;
}

// The PlayStation GPU's 4x4 dither table, in 8-bit color steps.
float ps1Dither(ivec2 p) {
  const float m[16] = float[16](-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0,
                                3.0, -1.0, 2.0, -2.0);
  p &= 3;
  return m[p.y * 4 + p.x];
}

// Integer hash (no sin()): bit-exact across GPUs, which keeps goldens stable.
float hash12(uvec2 p) {
  uvec3 v = uvec3(p, 0x9E37u) * uvec3(1597334673u, 3812015801u, 2798796415u);
  uint n = (v.x ^ v.y ^ v.z) * 1597334673u;
  return float(n >> 8) / 16777216.0;
}

vec3 srgbToLinear(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }
vec3 linearToSrgb(vec3 c) { return pow(max(c, 0.0), vec3(1.0 / 2.2)); }

// OKLab: perceptual color space; nearest-palette search in it looks much better
// than plain RGB distance (no muddy greens for skin tones).
vec3 toOklab(vec3 srgb) {
  vec3 c = srgbToLinear(srgb);
  vec3 lms = mat3(0.4122214708, 0.2119034982, 0.0883024619, 0.5363325363, 0.6806995451,
                  0.2817188376, 0.0514459929, 0.1073969566, 0.6299787005) *
             c;
  lms = pow(max(lms, 0.0), vec3(1.0 / 3.0));
  return mat3(0.2104542553, 1.9779984951, 0.0259040371, 0.7936177850, -2.4285922050,
              0.7827717662, -0.0040720468, 0.4505937099, -0.8086757660) *
         lms;
}

// Final blend shared by every pass: the intensity slider is a straight mix
// against the pass input, so 0 is an exact bypass.
vec4 delssOut(vec3 styled) {
  vec3 base = src(vUv).rgb;
  return vec4(mix(base, styled, clamp(uIntensity, 0.0, 1.0)), 1.0);
}
