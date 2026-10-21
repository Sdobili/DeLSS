// ink: outlines for cel and comic looks.
//
// Edges come from geometry whenever the engine provides it: depth
// discontinuities give silhouettes, normal discontinuities give creases. Both
// are stable from frame to frame because they do not depend on lighting or
// texture. Without a G-buffer we fall back to a Sobel on the luminance of this
// pass's input; put kuwahara/cel first so it traces regions, not photo grain.
uniform float u_width;      // line width in pixels
uniform float u_threshold;  // edge detection threshold
uniform vec3 u_inkColor;
uniform float u_depthWeight;
uniform float u_normalWeight;

float lumaSobel(vec2 uv, vec2 d) {
  float s[9];
  int k = 0;
  for (int j = -1; j <= 1; ++j) {
    for (int i = -1; i <= 1; ++i) {
      s[k++] = luma(src(uv + vec2(i, j) * d).rgb);
    }
  }
  float gx = s[2] + 2.0 * s[5] + s[8] - s[0] - 2.0 * s[3] - s[6];
  float gy = s[6] + 2.0 * s[7] + s[8] - s[0] - 2.0 * s[1] - s[2];
  return length(vec2(gx, gy)) * 2.0;
}

// Relative depth jump to the 4 neighbors. Dividing by depth makes the test
// scale-free: a floor receding to the horizon changes depth smoothly (small
// relative steps, even after 8-bit quantization) while a silhouette jumps.
float depthEdge(vec2 uv, vec2 d) {
  float c = texture(uDepth, uv).r;
  float e = 0.0;
  for (int k = 0; k < 4; ++k) {
    vec2 o = k == 0 ? vec2(d.x, 0.0) : k == 1 ? vec2(-d.x, 0.0) : k == 2 ? vec2(0.0, d.y) : vec2(0.0, -d.y);
    float n = texture(uDepth, uv + o).r;
    e = max(e, abs(n - c) / max(min(n, c), 0.02));
  }
  return e * 4.0;
}

float normalEdge(vec2 uv, vec2 d) {
  vec3 n = texture(uNormal, uv).rgb * 2.0 - 1.0;
  float e = 0.0;
  e = max(e, 1.0 - dot(n, texture(uNormal, uv + vec2(d.x, 0.0)).rgb * 2.0 - 1.0));
  e = max(e, 1.0 - dot(n, texture(uNormal, uv - vec2(d.x, 0.0)).rgb * 2.0 - 1.0));
  e = max(e, 1.0 - dot(n, texture(uNormal, uv + vec2(0.0, d.y)).rgb * 2.0 - 1.0));
  e = max(e, 1.0 - dot(n, texture(uNormal, uv - vec2(0.0, d.y)).rgb * 2.0 - 1.0));
  return e * 4.0;
}

void main() {
  vec2 d = texel() * max(0.5, u_width * 0.5);
  float edge;
  if (uHasDepth || uHasNormal) {
    edge = 0.0;
    if (uHasDepth) edge = max(edge, depthEdge(vUv, d) * u_depthWeight);
    if (uHasNormal) edge = max(edge, normalEdge(vUv, d) * u_normalWeight);
  } else {
    edge = lumaSobel(vUv, d);
  }
  float t = max(u_threshold, 1e-3);
  float line = smoothstep(t, t * 1.6, edge);
  vec3 base = src(vUv).rgb;
  fragColor = delssOut(mix(base, u_inkColor, line));
}
