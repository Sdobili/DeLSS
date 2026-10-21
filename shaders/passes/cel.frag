// cel: toon shading as a post process. Quantize brightness into a few bands but
// keep each pixel's chroma, so a red jacket stays red in every band. A small
// smoothstep at each band boundary removes stair-step aliasing; hysteresis
// against the previous frame (see the temporal pass) handles the rest.
uniform float u_bands;       // number of light bands
uniform float u_softness;    // width of the band transition, 0 = hard
uniform float u_saturation;  // cartoon colors are punchier than photos
uniform float u_brightness;  // cartoons are lit flatter and brighter than photos

void main() {
  vec3 c = src(vUv).rgb;
  float l = luma(c);
  float bands = max(2.0, u_bands);
  float x = l * bands;
  float f = fract(x);
  float w = max(u_softness, 1e-3) * 0.5;
  float q = (floor(x) + smoothstep(0.5 - w, 0.5 + w, f)) / bands;
  q = clamp(q + 0.5 / bands, 0.0, 1.0);
  vec3 chroma = c / max(l, 1e-3);
  vec3 styled = chroma * q * u_brightness;
  styled = mix(vec3(luma(styled)), styled, u_saturation);
  fragColor = delssOut(clamp(styled, 0.0, 1.0));
}
