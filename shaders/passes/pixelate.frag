// pixelate: snap the frame to a coarse grid of square "fat pixels".
//
// Each cell is the box-filtered average of the pixels it covers, not a point
// sample. Point sampling picks one arbitrary texel per cell, so thin detail
// pops in and out as the camera moves; averaging makes the cell color a smooth
// function of the scene, which is what keeps pixel art from flickering.
uniform float u_pixelSize;  // cell size in output pixels

void main() {
  float size = max(1.0, floor(u_pixelSize + 0.5));
  vec2 cell = floor(gl_FragCoord.xy / size);
  vec2 origin = cell * size;
  // 4x4 stratified taps per cell, clamped so huge cells stay cheap.
  vec3 sum = vec3(0.0);
  for (int j = 0; j < 4; ++j) {
    for (int i = 0; i < 4; ++i) {
      vec2 p = origin + (vec2(i, j) + 0.5) * (size / 4.0);
      sum += src(p / uResolution).rgb;
    }
  }
  fragColor = delssOut(sum / 16.0);
}
