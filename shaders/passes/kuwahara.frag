// kuwahara: edge-preserving smoothing that turns photo texture into flat
// painted regions. For each pixel, look at four overlapping quadrants and keep
// the mean of the one with the lowest variance: flat areas get averaged, while
// an edge is never blurred because some quadrant always lies on one side of it.
uniform float u_radius;

void main() {
  int r = int(clamp(floor(u_radius + 0.5), 1.0, 6.0));
  vec2 t = texel();
  vec3 mean[4];
  vec3 sq[4];
  for (int k = 0; k < 4; ++k) {
    mean[k] = vec3(0.0);
    sq[k] = vec3(0.0);
  }
  for (int j = -6; j <= 6; ++j) {
    for (int i = -6; i <= 6; ++i) {
      if (abs(i) > r || abs(j) > r) continue;
      vec3 c = src(vUv + vec2(i, j) * t).rgb;
      vec3 c2 = c * c;
      // A tap on an axis belongs to both neighboring quadrants.
      if (i <= 0 && j <= 0) { mean[0] += c; sq[0] += c2; }
      if (i >= 0 && j <= 0) { mean[1] += c; sq[1] += c2; }
      if (i <= 0 && j >= 0) { mean[2] += c; sq[2] += c2; }
      if (i >= 0 && j >= 0) { mean[3] += c; sq[3] += c2; }
    }
  }
  float n = float((r + 1) * (r + 1));
  float best = 1e9;
  vec3 result = src(vUv).rgb;
  for (int k = 0; k < 4; ++k) {
    vec3 m = mean[k] / n;
    vec3 v = abs(sq[k] / n - m * m);
    float s = v.r + v.g + v.b;
    if (s < best) {
      best = s;
      result = m;
    }
  }
  fragColor = delssOut(result);
}
