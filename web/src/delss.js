// DeLSS WebGL2 runtime: runs a chain of stylization passes as the final stage
// of a frame. All shader code comes from ./generated.js (see tools/gen.py).
import { PALETTES, PASSES, PASS_SHADERS, PRESETS, VERTEX_SHADER } from './generated.js';

export { PALETTES, PASSES, PRESETS };

/** Texture units, fixed so every program binds its samplers once. */
const UNIT = { src: 0, color: 1, depth: 2, normal: 3, motion: 4, history: 5 };
const SAMPLERS = {
  uSrc: UNIT.src,
  uColor: UNIT.color,
  uDepth: UNIT.depth,
  uNormal: UNIT.normal,
  uMotion: UNIT.motion,
  uHistory: UNIT.history,
};

/**
 * @typedef {{pass: string, params?: Record<string, unknown>, intensity?: number,
 *            enabled?: boolean}} StepSpec
 * @typedef {{pass: string, params: Record<string, number | number[] | string>,
 *            intensity: number, enabled: boolean}} Step
 * @typedef {{color: WebGLTexture, depth?: WebGLTexture | null,
 *            normal?: WebGLTexture | null, motion?: WebGLTexture | null}} FrameInputs
 */

/** Hex "#rrggbb" to linear [0,1] triple. */
export function hexToRgb(hex) {
  const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
  if (!m) throw new Error(`DeLSS: bad color ${hex}`);
  return [1, 2, 3].map((i) => parseInt(m[i], 16) / 255);
}

/**
 * Resolve a step against the manifest: fill defaults, validate names, convert
 * colors. Throws on unknown passes or parameters so typos fail loudly.
 * @param {StepSpec} spec
 * @returns {Step}
 */
export function resolveStep(spec) {
  const desc = PASSES[spec.pass];
  if (!desc) throw new Error(`DeLSS: unknown pass "${spec.pass}"`);
  const given = spec.params ?? {};
  for (const key of Object.keys(given)) {
    if (!(key in desc.params)) throw new Error(`DeLSS: pass "${spec.pass}" has no param "${key}"`);
  }
  /** @type {Step['params']} */
  const params = {};
  for (const [name, p] of Object.entries(desc.params)) {
    const value = given[name] ?? p.default;
    params[name] = p.type === 'color' && typeof value === 'string' ? hexToRgb(value) : value;
  }
  return { pass: spec.pass, params, intensity: spec.intensity ?? 1, enabled: spec.enabled ?? true };
}

/** A fresh, editable copy of a built-in preset's chain. */
export function presetChain(id) {
  const preset = PRESETS.find((p) => p.id === id);
  if (!preset) throw new Error(`DeLSS: unknown preset "${id}"`);
  return preset.passes.map(resolveStep);
}

function compile(gl, type, source, label) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(shader);
    gl.deleteShader(shader);
    throw new Error(`DeLSS: ${label} failed to compile:\n${log}`);
  }
  return shader;
}

function createTarget(gl, width, height) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, width, height);
  for (const [k, v] of [
    [gl.TEXTURE_MIN_FILTER, gl.LINEAR],
    [gl.TEXTURE_MAG_FILTER, gl.LINEAR],
    [gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE],
    [gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE],
  ]) {
    gl.texParameteri(gl.TEXTURE_2D, k, v);
  }
  const framebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
  return { texture, framebuffer };
}

export class DeLSS {
  /** @param {WebGL2RenderingContext} gl */
  constructor(gl) {
    this.gl = gl;
    /** @type {Step[]} */
    this.chain = [];
    this.frame = 0;
    this.width = 0;
    this.height = 0;
    /** @type {Map<string, {program: WebGLProgram, uniforms: Map<string, WebGLUniformLocation>}>} */
    this.programs = new Map();
    this.vertex = compile(gl, gl.VERTEX_SHADER, VERTEX_SHADER, 'fullscreen.vert');
    this.vao = gl.createVertexArray();
    this.dummy = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.dummy);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(4));
    /** Intermediate ping-pong targets plus a history pair that survives frames. */
    this.ping = [];
    this.history = [];
    this.historyValid = false;
    /** @type {WebGLFramebuffer | null} */
    this.scratch = null;
  }

  /** Replace the pass chain. Accepts specs or resolved steps. @param {StepSpec[]} steps */
  setChain(steps) {
    this.chain = steps.map((s) => resolveStep(s));
    this.historyValid = false;
  }

  /** Forget the previous frame, e.g. after a camera cut or a source change. */
  resetHistory() {
    this.historyValid = false;
  }

  program(name) {
    let entry = this.programs.get(name);
    if (entry) return entry;
    const gl = this.gl;
    const fragment = compile(gl, gl.FRAGMENT_SHADER, PASS_SHADERS[name], `${name}.frag`);
    const program = gl.createProgram();
    gl.attachShader(program, this.vertex);
    gl.attachShader(program, fragment);
    gl.linkProgram(program);
    gl.deleteShader(fragment);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      throw new Error(`DeLSS: ${name} failed to link:\n${gl.getProgramInfoLog(program)}`);
    }
    const uniforms = new Map();
    const count = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < count; ++i) {
      const info = gl.getActiveUniform(program, i);
      const key = info.name.replace(/\[0\]$/, '');
      uniforms.set(key, gl.getUniformLocation(program, info.name));
    }
    gl.useProgram(program);
    for (const [sampler, unit] of Object.entries(SAMPLERS)) {
      if (uniforms.has(sampler)) gl.uniform1i(uniforms.get(sampler), unit);
    }
    entry = { program, uniforms };
    this.programs.set(name, entry);
    return entry;
  }

  resize(width, height) {
    if (width === this.width && height === this.height) return;
    const gl = this.gl;
    for (const t of [...this.ping, ...this.history]) {
      gl.deleteTexture(t.texture);
      gl.deleteFramebuffer(t.framebuffer);
    }
    this.ping = [createTarget(gl, width, height), createTarget(gl, width, height)];
    this.history = [createTarget(gl, width, height), createTarget(gl, width, height)];
    this.width = width;
    this.height = height;
    this.historyValid = false;
  }

  setParams(uniforms, step) {
    const gl = this.gl;
    const desc = PASSES[step.pass].params;
    for (const [name, value] of Object.entries(step.params)) {
      const type = desc[name].type;
      if (type === 'palette') {
        const colors = PALETTES[value]?.colors.map(hexToRgb) ?? [];
        const flat = new Float32Array(48);
        colors.forEach((c, i) => flat.set(c, i * 3));
        gl.uniform3fv(uniforms.get(`u_${name}`) ?? null, flat);
        gl.uniform1i(uniforms.get(`u_${name}Size`) ?? null, colors.length);
      } else if (type === 'color') {
        gl.uniform3fv(uniforms.get(`u_${name}`) ?? null, /** @type {number[]} */ (value));
      } else if (type === 'int') {
        gl.uniform1i(uniforms.get(`u_${name}`) ?? null, Number(value));
      } else {
        gl.uniform1f(uniforms.get(`u_${name}`) ?? null, Number(value));
      }
    }
  }

  /**
   * Run the chain on one frame.
   * @param {FrameInputs} inputs engine textures; depth/normal/motion optional
   * @param {{width: number, height: number, target?: WebGLFramebuffer | null}} out
   *   output size and destination framebuffer (null = canvas)
   */
  render(inputs, { width, height, target = null }) {
    const gl = this.gl;
    this.resize(width, height);
    const has = {
      depth: Boolean(inputs.depth),
      normal: Boolean(inputs.normal),
      motion: Boolean(inputs.motion),
    };
    const bind = (unit, tex) => {
      gl.activeTexture(gl.TEXTURE0 + unit);
      gl.bindTexture(gl.TEXTURE_2D, tex ?? this.dummy);
    };
    bind(UNIT.color, inputs.color);
    bind(UNIT.depth, inputs.depth);
    bind(UNIT.normal, inputs.normal);
    bind(UNIT.motion, inputs.motion);
    bind(UNIT.history, this.history[1].texture);

    gl.bindVertexArray(this.vao);
    gl.disable(gl.BLEND);
    gl.disable(gl.DEPTH_TEST);
    gl.viewport(0, 0, width, height);

    const steps = this.chain.filter((s) => s.enabled);
    let source = inputs.color;
    let pingIndex = 0;
    steps.forEach((step, i) => {
      const last = i === steps.length - 1;
      const dest = last ? this.history[0] : this.ping[pingIndex];
      const { program, uniforms } = this.program(step.pass);
      gl.useProgram(program);
      bind(UNIT.src, source);
      gl.bindFramebuffer(gl.FRAMEBUFFER, dest.framebuffer);
      gl.uniform1i(uniforms.get('uHasDepth') ?? null, has.depth ? 1 : 0);
      gl.uniform1i(uniforms.get('uHasNormal') ?? null, has.normal ? 1 : 0);
      gl.uniform1i(uniforms.get('uHasMotion') ?? null, has.motion ? 1 : 0);
      gl.uniform1i(uniforms.get('uHasHistory') ?? null, this.historyValid ? 1 : 0);
      gl.uniform2f(uniforms.get('uResolution') ?? null, width, height);
      gl.uniform1i(uniforms.get('uFrame') ?? null, this.frame);
      gl.uniform1f(uniforms.get('uIntensity') ?? null, step.intensity);
      this.setParams(uniforms, step);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      source = dest.texture;
      pingIndex ^= 1;
    });

    if (steps.length) {
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.history[0].framebuffer);
      this.history.reverse();
      this.historyValid = true;
    } else {
      // Empty chain = exact bypass: copy the input color straight through.
      this.scratch ??= gl.createFramebuffer();
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.scratch);
      gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, inputs.color, 0);
    }
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, target);
    gl.blitFramebuffer(0, 0, width, height, 0, 0, width, height, gl.COLOR_BUFFER_BIT, gl.NEAREST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.bindVertexArray(null);
    this.frame += 1;
  }

  dispose() {
    const gl = this.gl;
    for (const { program } of this.programs.values()) gl.deleteProgram(program);
    for (const t of [...this.ping, ...this.history]) {
      gl.deleteTexture(t.texture);
      gl.deleteFramebuffer(t.framebuffer);
    }
    gl.deleteShader(this.vertex);
    gl.deleteTexture(this.dummy);
    gl.deleteVertexArray(this.vao);
    if (this.scratch) gl.deleteFramebuffer(this.scratch);
    this.programs.clear();
  }
}
