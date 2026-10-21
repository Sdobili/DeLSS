// A small raymarched "game engine" that renders a full G-buffer: color, linear
// depth, view-space normals and exact camera motion vectors. It stands in for
// the engine that would feed DeLSS in a real game, so the demo can show the
// depth/normal edge path and motion-vector reprojection with self-made media.
import { VERTEX_SHADER } from '../src/generated.js';

const SCENE_FRAG = `#version 300 es
precision highp float;
in vec2 vUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outDepth;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outMotion;
uniform vec2 uResolution;
uniform float uTime;
uniform float uPrevTime;

const float FAR = 40.0;

struct Camera { vec3 pos; mat3 basis; };

Camera camera(float t) {
  float a = 0.6 + t * 0.25;
  vec3 pos = vec3(7.5 * sin(a), 2.6 + 0.4 * sin(t * 0.5), 7.5 * cos(a));
  vec3 target = vec3(0.0, 0.9, 0.0);
  vec3 f = normalize(target - pos);
  vec3 r = normalize(cross(f, vec3(0.0, 1.0, 0.0)));
  vec3 u = cross(r, f);
  return Camera(pos, mat3(r, u, f));
}

float sdBox(vec3 p, vec3 b) {
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - 0.05;
}
float sdTorus(vec3 p, vec2 t) { return length(vec2(length(p.xz) - t.x, p.y)) - t.y; }

// Returns (distance, material id).
vec2 map(vec3 p) {
  vec2 res = vec2(p.y, 0.0);
  float s = length(p - vec3(0.0, 1.1, 0.0)) - 1.1;
  if (s < res.x) res = vec2(s, 1.0);
  float b = sdBox(p - vec3(2.6, 0.7, -1.0), vec3(0.7));
  if (b < res.x) res = vec2(b, 2.0);
  float t = sdTorus((p - vec3(-2.4, 0.45, 1.2)).xzy, vec2(0.8, 0.3));
  if (t < res.x) res = vec2(t, 3.0);
  for (int i = 0; i < 6; ++i) {
    float a = float(i) * 1.0472;
    vec3 c = vec3(5.0 * cos(a), 1.5, 5.0 * sin(a));
    float pillar = sdBox(p - c, vec3(0.25, 1.5, 0.25));
    if (pillar < res.x) res = vec2(pillar, 4.0);
  }
  return res;
}

vec3 calcNormal(vec3 p) {
  const vec2 e = vec2(1e-3, 0.0);
  return normalize(vec3(map(p + e.xyy).x - map(p - e.xyy).x, map(p + e.yxy).x - map(p - e.yxy).x,
                        map(p + e.yyx).x - map(p - e.yyx).x));
}

float softShadow(vec3 ro, vec3 rd) {
  float res = 1.0;
  float t = 0.03;
  for (int i = 0; i < 48; ++i) {
    float h = map(ro + rd * t).x;
    res = min(res, 10.0 * h / t);
    t += clamp(h, 0.02, 0.4);
    if (res < 0.002 || t > 12.0) break;
  }
  return clamp(res, 0.0, 1.0);
}

float ao(vec3 p, vec3 n) {
  float occ = 0.0;
  for (int i = 1; i <= 5; ++i) {
    float h = 0.06 * float(i);
    occ += (h - map(p + n * h).x) / float(i);
  }
  return clamp(1.0 - 2.5 * occ, 0.0, 1.0);
}

vec3 albedo(vec3 p, float m) {
  if (m < 0.5) {
    float c = mod(floor(p.x) + floor(p.z), 2.0);
    return mix(vec3(0.55, 0.52, 0.48), vec3(0.32, 0.30, 0.28), c);
  }
  if (m < 1.5) return vec3(0.85, 0.15, 0.12);
  if (m < 2.5) return vec3(0.15, 0.45, 0.85);
  if (m < 3.5) return vec3(0.95, 0.72, 0.2);
  return vec3(0.78, 0.76, 0.7);
}

vec3 sky(vec3 rd) {
  return mix(vec3(0.85, 0.82, 0.78), vec3(0.32, 0.52, 0.85), clamp(rd.y * 1.6, 0.0, 1.0));
}

vec2 project(Camera cam, vec3 p) {
  vec3 v = transpose(cam.basis) * (p - cam.pos);
  vec2 ndc = v.xy / v.z * 1.6;
  ndc.x *= uResolution.y / uResolution.x;
  return ndc * 0.5 + 0.5;
}

void main() {
  Camera cam = camera(uTime);
  vec2 ndc = vUv * 2.0 - 1.0;
  ndc.x *= uResolution.x / uResolution.y;
  vec3 rd = cam.basis * normalize(vec3(ndc / 1.6, 1.0));
  float t = 0.0;
  float m = -1.0;
  for (int i = 0; i < 128; ++i) {
    vec2 h = map(cam.pos + rd * t);
    if (h.x < 1e-3 * t) { m = h.y; break; }
    t += h.x;
    if (t > FAR) break;
  }
  vec3 sun = normalize(vec3(0.6, 0.8, 0.35));
  if (m < 0.0) {
    outColor = vec4(sky(rd), 1.0);
    outDepth = vec4(1.0);
    outNormal = vec4(0.5, 0.5, 0.0, 1.0);
    // Sky at infinity: motion from camera rotation only.
    vec3 farP = cam.pos + rd * 1000.0;
    outMotion = vec4(vUv - project(camera(uPrevTime), farP), 0.0, 1.0);
    return;
  }
  vec3 p = cam.pos + rd * t;
  vec3 n = calcNormal(p);
  vec3 a = albedo(p, m);
  float diff = max(dot(n, sun), 0.0) * softShadow(p + n * 0.01, sun);
  float occ = ao(p, n);
  vec3 h = normalize(sun - rd);
  float spec = pow(max(dot(n, h), 0.0), m == 1.0 ? 64.0 : 16.0) * diff;
  vec3 col = a * (diff * vec3(1.25, 1.15, 1.0) + occ * 0.35 * sky(n)) + spec * 0.6;
  col = mix(col, sky(rd), smoothstep(12.0, FAR, t));
  col = col / (1.0 + col);  // Reinhard tone map
  col = pow(col, vec3(1.0 / 2.2));
  outColor = vec4(clamp(col, 0.0, 1.0), 1.0);
  outDepth = vec4(vec3(t / FAR), 1.0);
  outNormal = vec4(transpose(cam.basis) * n * 0.5 + 0.5, 1.0);
  outMotion = vec4(vUv - project(camera(uPrevTime), p), 0.0, 1.0);
}`;

function target(gl, format, w, h) {
  const tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texStorage2D(gl.TEXTURE_2D, 1, format, w, h);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  return tex;
}

export class GBufferScene {
  /** @param {WebGL2RenderingContext} gl */
  constructor(gl) {
    this.gl = gl;
    // Motion vectors need sub-pixel precision; RGBA8 is not enough.
    this.floatMotion = Boolean(gl.getExtension('EXT_color_buffer_float'));
    const compile = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    };
    this.program = gl.createProgram();
    gl.attachShader(this.program, compile(gl.VERTEX_SHADER, VERTEX_SHADER));
    gl.attachShader(this.program, compile(gl.FRAGMENT_SHADER, SCENE_FRAG));
    gl.linkProgram(this.program);
    if (!gl.getProgramParameter(this.program, gl.LINK_STATUS)) {
      throw new Error(gl.getProgramInfoLog(this.program));
    }
    this.vao = gl.createVertexArray();
    this.width = 0;
    this.height = 0;
  }

  resize(w, h) {
    if (w === this.width && h === this.height) return;
    const gl = this.gl;
    if (this.fbo) {
      gl.deleteFramebuffer(this.fbo);
      Object.values(this.textures).forEach((t) => gl.deleteTexture(t));
    }
    this.textures = {
      color: target(gl, gl.RGBA8, w, h),
      depth: target(gl, gl.RGBA8, w, h),
      normal: target(gl, gl.RGBA8, w, h),
      motion: target(gl, this.floatMotion ? gl.RGBA16F : gl.RGBA8, w, h),
    };
    // Color is sampled with bilinear filtering by pixelate's box filter.
    gl.bindTexture(gl.TEXTURE_2D, this.textures.color);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    this.fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
    Object.values(this.textures).forEach((tex, i) =>
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0 + i, gl.TEXTURE_2D, tex, 0),
    );
    gl.drawBuffers([0, 1, 2, 3].map((i) => gl.COLOR_ATTACHMENT0 + i));
    this.width = w;
    this.height = h;
  }

  /** Render the scene at time t (seconds); prevT drives the motion vectors. */
  render(t, prevT, w, h) {
    const gl = this.gl;
    this.resize(w, h);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
    gl.viewport(0, 0, w, h);
    gl.useProgram(this.program);
    gl.uniform2f(gl.getUniformLocation(this.program, 'uResolution'), w, h);
    gl.uniform1f(gl.getUniformLocation(this.program, 'uTime'), t);
    gl.uniform1f(gl.getUniformLocation(this.program, 'uPrevTime'), prevT);
    gl.bindVertexArray(this.vao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindVertexArray(null);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    return this.textures;
  }
}
