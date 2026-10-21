// halftone: comic-book print dots. A rotated screen-space grid of round dots
// whose radius follows darkness. The grid is anchored to the screen, like a
// printing screen over a page, so dots never swim.
uniform float u_dotSize;   // grid period in pixels
uniform float u_angle;     // screen angle in degrees (comics often use 15 or 45)
uniform vec3 u_paperColor;
uniform vec3 u_dotColor;
uniform float u_shadowOnly;  // 1 = dots only in shadows, colors kept elsewhere

void main() {
  vec3 c = src(vUv).rgb;
  float a = radians(u_angle);
  mat2 rot = mat2(cos(a), -sin(a), sin(a), cos(a));
  float period = max(2.0, u_dotSize);
  vec2 p = rot * gl_FragCoord.xy / period;
  vec2 cellCenter = floor(p) + 0.5;
  float dist = length(p - cellCenter);
  float darkness = 1.0 - luma(c);
  // Area coverage = darkness -> radius = sqrt(darkness / pi) in cell units.
  float radius = sqrt(darkness / 3.14159265) * 1.1;
  float aa = 0.75 / period;  // ~one pixel of antialiasing
  float dot_ = 1.0 - smoothstep(radius - aa, radius + aa, dist);
  vec3 styled;
  if (u_shadowOnly > 0.5) {
    float shadow = smoothstep(0.45, 0.25, luma(c));
    styled = mix(c, mix(c, u_dotColor, 0.85), dot_ * shadow);
  } else {
    styled = mix(u_paperColor, u_dotColor, dot_);
  }
  fragColor = delssOut(styled);
}
