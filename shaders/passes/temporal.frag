// temporal: anti-flicker for quantized styles, the DeLSS counterpart of TAA.
//
// Quantizing (palette, cel bands, dither) is a discontinuous function: noise of
// one 8-bit step in the input can flip a pixel between two palette colors every
// frame. We reproject last frame's *output* with motion vectors and keep it
// when it is still a plausible answer, i.e. it lies inside the color box of the
// current frame's neighborhood, sampled one u_cell apart so that for pixel art
// the test spans neighboring fat pixels rather than the inside of one. Keeping a previous palette color is
// lossless (it is still a palette color), so unlike TAA this adds no blur;
// the neighborhood test stops ghosting on disocclusions.
uniform float u_stability;  // probability of keeping plausible history; 1 - u_stability of
                            // pixels refresh each frame so frozen patterns still converge
uniform float u_tolerance;  // how far outside the box history may be
uniform float u_cell;       // decision block in pixels; match pixelate's pixel size

void main() {
  vec3 cur = src(vUv).rgb;
  if (!uHasHistory) {
    fragColor = vec4(cur, 1.0);
    return;
  }
  float cell = max(1.0, floor(u_cell + 0.5));
  // Reproject by a whole number of blocks, read with texelFetch: history is
  // never filtered, so a kept pixel is exactly a color the chain produced
  // (bilinear taps would blend two palette colors into an off-palette one),
  // and fat pixels stay on the screen-anchored grid while the camera moves.
  vec2 blockCenter = (floor(gl_FragCoord.xy / cell) + 0.5) * cell;
  vec2 motion = uHasMotion ? texture(uMotion, blockCenter / uResolution).rg * uResolution : vec2(0.0);
  vec2 prev = gl_FragCoord.xy - floor(motion / cell + 0.5) * cell;
  if (any(lessThan(prev, vec2(0.0))) || any(greaterThanEqual(prev, uResolution))) {
    fragColor = vec4(cur, 1.0);
    return;
  }
  vec3 hist = texelFetch(uHistory, ivec2(prev), 0).rgb;
  vec3 lo = cur;
  vec3 hi = cur;
  vec2 t = texel() * cell;
  for (int j = -1; j <= 1; ++j) {
    for (int i = -1; i <= 1; ++i) {
      vec3 n = src(vUv + vec2(i, j) * t).rgb;
      lo = min(lo, n);
      hi = max(hi, n);
    }
  }
  vec3 tol = vec3(u_tolerance);
  bool plausible = all(greaterThanEqual(hist, lo - tol)) && all(lessThanEqual(hist, hi + tol));
  // Hard select instead of a blend: blending two palette colors would create an
  // off-palette color and smear pixel art.
  // One refresh decision per block, so a fat pixel never splits in two.
  uvec2 block = uvec2(floor(gl_FragCoord.xy / cell));
  float roll = hash12(block ^ uvec2(uint(uFrame) * 2654435761u));
  float keep = plausible && roll < u_stability ? 1.0 : 0.0;
  fragColor = vec4(mix(cur, hist, keep * clamp(uIntensity, 0.0, 1.0)), 1.0);
}
