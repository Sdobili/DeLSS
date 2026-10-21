// grade: "old photograph" color grading. Sepia toning, faded blacks, contrast,
// vignette and film grain. Grain is anchored to the screen and only re-rolled
// when u_grainSpeed > 0, so the default look is static.
uniform float u_sepia;
uniform float u_saturation;
uniform float u_contrast;
uniform float u_fade;       // lifted blacks
uniform float u_vignette;
uniform float u_grain;
uniform float u_grainSpeed; // 0 = frozen grain, n = re-roll every 1/n frames

void main() {
  vec3 c = src(vUv).rgb;
  float l = luma(c);
  c = mix(vec3(l), c, u_saturation);
  vec3 sep = vec3(dot(c, vec3(0.393, 0.769, 0.189)), dot(c, vec3(0.349, 0.686, 0.168)),
                  dot(c, vec3(0.272, 0.534, 0.131)));
  c = mix(c, sep, u_sepia);
  c = (c - 0.5) * u_contrast + 0.5;
  c = mix(vec3(u_fade), vec3(1.0 - u_fade * 0.3), c);
  vec2 v = vUv * (1.0 - vUv);
  c *= mix(1.0, pow(16.0 * v.x * v.y, 0.35), u_vignette);
  uint epoch = u_grainSpeed > 0.0 ? uint(float(uFrame) * u_grainSpeed) : 0u;
  float g = hash12(uvec2(gl_FragCoord.xy) + uvec2(epoch * 131u, epoch * 71u)) - 0.5;
  c += g * u_grain;
  fragColor = delssOut(clamp(c, 0.0, 1.0));
}
