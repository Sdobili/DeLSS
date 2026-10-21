// palette: quantize to a fixed palette (or to N levels per channel) with
// ordered dithering.
//
// Ordered dithering is the temporally stable choice: the threshold depends only
// on the integer screen cell, so a static image produces a static pattern.
// Error diffusion (Floyd-Steinberg) would ripple across the whole frame
// whenever a single pixel changes.
uniform vec3 u_palette[16];
uniform int u_paletteSize;  // 0 = no palette, use u_levels instead
uniform float u_levels;     // per-channel levels when u_paletteSize == 0 (32 = 15-bit)
uniform float u_dither;     // 0..1 dither strength
uniform float u_ditherScale;  // dither cell size, match pixelate for crisp pixel art
uniform int u_ditherMatrix;   // 0 = Bayer 8x8, 1 = PS1 4x4 hardware table
uniform int u_mapping;        // 0 = nearest color in OKLab, 1 = luminance ramp
uniform float u_exposure;     // pre-quantization exposure, retro palettes like bright input

vec3 nearestPalette(vec3 c) {
  vec3 lab = toOklab(c);
  float best = 1e9;
  vec3 result = u_palette[0];
  for (int i = 0; i < 16; ++i) {
    if (i >= u_paletteSize) break;
    vec3 d = toOklab(u_palette[i]) - lab;
    float dist = dot(d, d);
    if (dist < best) {
      best = dist;
      result = u_palette[i];
    }
  }
  return result;
}

// Luminance ramp: sort the palette by brightness and index it by the pixel's
// brightness. This is how monochrome hardware (Game Boy, CGA) is usually
// emulated: a 4-shade green screen should show light and shadow, not try to
// find the "closest green" to a blue sky.
vec3 rampPalette(float l) {
  vec3 sorted[16];
  int n = 0;
  for (int i = 0; i < 16; ++i) {
    if (i >= u_paletteSize) break;
    vec3 c = u_palette[i];
    int j = n;
    while (j > 0 && luma(sorted[j - 1]) > luma(c)) {
      sorted[j] = sorted[j - 1];
      --j;
    }
    sorted[j] = c;
    ++n;
  }
  int idx = clamp(int(floor(l * float(n))), 0, n - 1);
  return sorted[idx];
}

void main() {
  vec3 c = clamp(src(vUv).rgb * u_exposure, 0.0, 1.0);
  ivec2 cell = ivec2(floor(gl_FragCoord.xy / max(1.0, floor(u_ditherScale + 0.5))));
  vec3 q;
  if (u_paletteSize > 0) {
    // Spread ~ one palette step; a palette of n colors spans ~1/(n^(1/3)) per axis.
    float spread = u_dither / pow(float(u_paletteSize), 1.0 / 3.0);
    float t = bayer8(cell) - 0.5;
    if (u_mapping == 1) {
      float n = float(u_paletteSize);
      q = rampPalette(clamp(luma(c) + t * u_dither / n, 0.0, 1.0));
    } else {
      q = nearestPalette(clamp(c + vec3(t * spread), 0.0, 1.0));
    }
  } else {
    float levels = max(2.0, u_levels);
    float step = 1.0 / (levels - 1.0);
    float t = u_ditherMatrix == 1 ? ps1Dither(cell) / 8.0 : bayer8(cell) - 0.5;
    q = floor((c + t * step * u_dither) / step + 0.5) * step;
  }
  fragColor = delssOut(clamp(q, 0.0, 1.0));
}
