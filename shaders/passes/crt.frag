// crt: a consumer CRT television. Barrel curvature, horizontal scanlines tied
// to a virtual line count, an RGB aperture-grille mask and a vignette.
// Everything is a pure function of screen position: no time term, no flicker.
uniform float u_curvature;  // 0 = flat glass
uniform float u_scanlines;  // scanline darkness 0..1
uniform float u_lineHeight; // scanline pitch in output pixels (3 at 720p ~ 240 lines)
uniform float u_mask;       // aperture grille strength 0..1
uniform float u_vignette;
uniform float u_glow;       // bright pixels bleed into the dark gaps

vec2 barrel(vec2 uv) {
  vec2 c = uv * 2.0 - 1.0;
  c *= 1.0 + u_curvature * dot(c, c) * 0.25;
  return c * 0.5 + 0.5;
}

void main() {
  vec2 uv = barrel(vUv);
  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
    fragColor = delssOut(vec3(0.0));
    return;
  }
  // Sample the center of the virtual scanline so each line is one color band.
  float lines = uResolution.y / max(2.0, u_lineHeight);
  float line = uv.y * lines;
  vec2 suv = vec2(uv.x, (floor(line) + 0.5) / lines);
  vec3 c = src(suv).rgb;
  vec3 blur = (src(suv + vec2(texel().x * 2.0, 0.0)).rgb + src(suv - vec2(texel().x * 2.0, 0.0)).rgb) * 0.5;
  c = max(c, blur * u_glow);
  // Gaussian beam profile across the line: bright lines are wider.
  float d = fract(line) - 0.5;
  float beam = exp(-d * d / (0.06 + 0.1 * luma(c)));
  c *= mix(1.0, beam * 1.25, u_scanlines);
  // Aperture grille: R, G, B stripes, one output pixel each.
  int stripe = int(mod(gl_FragCoord.x, 3.0));
  vec3 m = vec3(stripe == 0, stripe == 1, stripe == 2);
  c *= mix(vec3(1.0), m * 2.2 + 0.25, u_mask);
  vec2 v = vUv * (1.0 - vUv);
  c *= mix(1.0, pow(16.0 * v.x * v.y, 0.25), u_vignette);
  fragColor = delssOut(clamp(c, 0.0, 1.0));
}
