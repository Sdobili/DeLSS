// vhs: analog tape. Chroma is stored at a fraction of the luma bandwidth on
// VHS, so color smears horizontally while brightness stays sharp. Optional
// tracking noise is the one place DeLSS uses time on purpose; set u_noise to 0
// for a perfectly static result.
uniform float u_chromaBlur;   // horizontal chroma smear in pixels
uniform float u_chromaShift;  // red/blue misregistration in pixels
uniform float u_noise;        // tracking noise and line jitter, 0 = off
uniform float u_sharpen;      // VHS decks over-sharpen luma (ringing)

vec3 rgb2yiq(vec3 c) {
  return mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312) * c;
}
vec3 yiq2rgb(vec3 c) {
  return mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703) * c;
}

void main() {
  vec2 t = texel();
  float row = floor(gl_FragCoord.y);
  // Per-line jitter, re-rolled every 4 frames so it reads as tape wobble, not sparkle.
  uint epoch = uint(uFrame / 4);
  float jitter = (hash12(uvec2(uint(row), epoch)) - 0.5) * u_noise * 3.0;
  vec2 uv = vUv + vec2(jitter * t.x, 0.0);

  float y = rgb2yiq(src(uv).rgb).x;
  float yl = rgb2yiq(src(uv - vec2(t.x, 0.0)).rgb).x;
  float yr = rgb2yiq(src(uv + vec2(t.x, 0.0)).rgb).x;
  y += (2.0 * y - yl - yr) * u_sharpen;

  vec2 iq = vec2(0.0);
  float wsum = 0.0;
  for (int i = -8; i <= 8; ++i) {
    float x = float(i) * max(u_chromaBlur, 0.0) / 8.0;
    float w = exp(-float(i * i) / 32.0);
    iq += rgb2yiq(src(uv + vec2((x + u_chromaShift) * t.x, 0.0)).rgb).yz * w;
    wsum += w;
  }
  iq /= wsum;
  vec3 c = yiq2rgb(vec3(y, iq));

  // Bottom-of-frame head-switching band plus sparse dropout streaks.
  float band = smoothstep(0.035, 0.0, vUv.y) * u_noise;
  float drop = step(0.9985, hash12(uvec2(uint(row), epoch * 7u + 3u))) * u_noise;
  c = mix(c, vec3(hash12(uvec2(gl_FragCoord.xy) + epoch)), max(band, drop));
  fragColor = delssOut(clamp(c, 0.0, 1.0));
}
