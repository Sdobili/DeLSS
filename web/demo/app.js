// DeLSS live demo: sample photos, an animated G-buffer scene, webcam or an
// uploaded file, run through any preset with a before/after split.
import { DeLSS, PALETTES, PASSES, PRESETS, presetChain } from '../src/delss.js';
import { GBufferScene } from './scene.js';

const MAX_WIDTH = 1280;
const $ = (id) => document.getElementById(id);

const canvas = $('canvas');
const gl = canvas.getContext('webgl2', { antialias: false, alpha: false });
if (!gl) {
  document.body.innerHTML = '<p style="padding:40px">此浏览器不支持 WebGL2。WebGL2 is required.</p>';
  throw new Error('WebGL2 unavailable');
}
const delss = new DeLSS(gl);
const scene = new GBufferScene(gl);
const timerExt = gl.getExtension('EXT_disjoint_timer_query_webgl2');

const SOURCES = [
  { id: 'scene', label: '3D 场景 (G-buffer)', kind: 'scene' },
  { id: 'knight', label: '骑士', kind: 'image', url: 'samples/knight.jpg' },
  { id: 'street', label: '雨夜街道', kind: 'image', url: 'samples/street.jpg' },
  { id: 'cat', label: '橘猫', kind: 'image', url: 'samples/cat.jpg' },
  { id: 'mountains', label: '梯田', kind: 'image', url: 'samples/mountains.jpg' },
  { id: 'webcam', label: '摄像头', kind: 'webcam' },
];

const state = {
  source: /** @type {any} */ (null),
  presetId: 'cel',
  split: 0.5,
  width: 1280,
  height: 720,
  colorTex: /** @type {WebGLTexture | null} */ (null),
  media: /** @type {HTMLVideoElement | null} */ (null),
  time: 0,
  lastNow: performance.now(),
  gpuMs: /** @type {number | null} */ (null),
  cpuMs: 0,
  fps: 0,
  queries: /** @type {WebGLQuery[]} */ ([]),
};

// ---------------------------------------------------------------- textures

function createColorTexture() {
  const tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  return tex;
}

/** Upload an image/video/canvas, downscaled so the canvas stays <= MAX_WIDTH. */
const staging = document.createElement('canvas');
function upload(el, w, h) {
  const scale = Math.min(1, MAX_WIDTH / w);
  state.width = Math.round(w * scale);
  state.height = Math.round(h * scale);
  let pixels = el;
  if (scale < 1) {
    staging.width = state.width;
    staging.height = state.height;
    staging.getContext('2d').drawImage(el, 0, 0, state.width, state.height);
    pixels = staging;
  }
  state.colorTex ??= createColorTexture();
  gl.bindTexture(gl.TEXTURE_2D, state.colorTex);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
}

function stopMedia() {
  if (state.media) {
    state.media.pause();
    const stream = /** @type {MediaStream | null} */ (state.media.srcObject);
    stream?.getTracks().forEach((t) => t.stop());
    if (state.media.src.startsWith('blob:')) URL.revokeObjectURL(state.media.src);
    state.media = null;
  }
}

async function playVideo(setup) {
  const video = document.createElement('video');
  video.muted = true;
  video.loop = true;
  video.playsInline = true;
  setup(video);
  await video.play();
  state.media = video;
}

async function selectSource(source) {
  stopMedia();
  state.source = source;
  delss.resetHistory();
  document.querySelectorAll('#sources button').forEach((b) => {
    b.classList.toggle('active', b.dataset.id === source.id);
  });
  $('gbuffer').style.display = source.kind === 'scene' ? '' : 'none';
  try {
    if (source.kind === 'image') {
      const img = new Image();
      img.src = source.url;
      await img.decode();
      upload(img, img.naturalWidth, img.naturalHeight);
      if (source.url.startsWith('blob:')) URL.revokeObjectURL(source.url);
    } else if (source.kind === 'webcam') {
      const stream = await navigator.mediaDevices.getUserMedia({ video: { width: 1280, height: 720 } });
      await playVideo((v) => (v.srcObject = stream));
    } else if (source.kind === 'video') {
      await playVideo((v) => (v.src = source.url));
    } else {
      state.width = 1280;
      state.height = 720;
    }
  } catch (err) {
    console.error(err);
    $('hud').textContent = `无法打开输入：${err.message}`;
  }
}

// ---------------------------------------------------------------- UI

function buildSources() {
  for (const s of SOURCES) {
    const b = document.createElement('button');
    b.textContent = s.label;
    b.dataset.id = s.id;
    b.onclick = () => selectSource(s);
    $('sources').append(b);
  }
  const openFile = (file) => {
    if (!file) return;
    const url = URL.createObjectURL(file);
    const kind = file.type.startsWith('video/') ? 'video' : 'image';
    selectSource({ id: 'file', label: file.name, kind, url });
  };
  $('file').onchange = (e) => openFile(e.target.files[0]);
  const vp = $('viewport');
  vp.ondragover = (e) => {
    e.preventDefault();
    vp.classList.add('dragging');
  };
  vp.ondragleave = () => vp.classList.remove('dragging');
  vp.ondrop = (e) => {
    e.preventDefault();
    vp.classList.remove('dragging');
    openFile(e.dataTransfer.files[0]);
  };
}

function buildPresets() {
  for (const p of PRESETS) {
    const b = document.createElement('button');
    b.textContent = p.label;
    b.dataset.id = p.id;
    b.onclick = () => selectPreset(p.id);
    $('presets').append(b);
  }
}

function selectPreset(id) {
  state.presetId = id;
  delss.setChain(presetChain(id));
  history.replaceState(null, '', `#preset=${id}`);
  document.querySelectorAll('#presets button').forEach((b) => {
    b.classList.toggle('active', b.dataset.id === id);
  });
  buildChainUi();
}

const toHex = (rgb) =>
  `#${rgb
    .map((v) =>
      Math.round(v * 255)
        .toString(16)
        .padStart(2, '0'),
    )
    .join('')}`;
const fromHex = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255);

function paramRow(label, input, readout) {
  const row = document.createElement('label');
  row.className = 'param';
  const name = document.createElement('span');
  name.textContent = label;
  const value = document.createElement('span');
  value.textContent = readout ?? '';
  row.append(name, input, value);
  return { row, value };
}

function slider(min, max, step, value, onInput) {
  const input = document.createElement('input');
  Object.assign(input, { type: 'range', min, max, step, value });
  input.oninput = () => onInput(Number(input.value));
  return input;
}

/** Editable controls bound directly to the live chain's step objects. */
function buildChainUi() {
  const root = $('chain');
  root.replaceChildren();
  delss.chain.forEach((step) => {
    const desc = PASSES[step.pass];
    const box = document.createElement('div');
    box.className = 'step';
    const head = document.createElement('header');
    const toggle = document.createElement('input');
    toggle.type = 'checkbox';
    toggle.checked = step.enabled;
    toggle.onchange = () => {
      step.enabled = toggle.checked;
      box.classList.toggle('disabled', !step.enabled);
      delss.resetHistory();
    };
    head.append(toggle, document.createTextNode(desc.label));
    box.append(head);

    const intensityInput = slider(0, 1, 0.01, step.intensity, (v) => {
      step.intensity = v;
      intensity.value.textContent = v.toFixed(2);
    });
    const intensity = paramRow('Intensity 强度', intensityInput, step.intensity.toFixed(2));
    box.append(intensity.row);

    for (const [name, p] of Object.entries(desc.params)) {
      let input;
      let readout = '';
      if (p.type === 'palette') {
        input = document.createElement('select');
        for (const id of [...Object.keys(PALETTES), 'none']) {
          input.add(new Option(PALETTES[id]?.label ?? 'none (levels)', id, false, step.params[name] === id));
        }
        input.onchange = () => (step.params[name] = input.value);
      } else if (p.type === 'color') {
        input = document.createElement('input');
        input.type = 'color';
        input.value = toHex(step.params[name]);
        input.oninput = () => (step.params[name] = fromHex(input.value));
      } else {
        readout = String(step.params[name]);
        input = slider(p.min, p.max, p.step, step.params[name], (v) => {
          step.params[name] = v;
          row.value.textContent = String(v);
        });
      }
      const row = paramRow(p.label, input, readout);
      box.append(row.row);
    }
    root.append(box);
  });
}

function bindSplit() {
  const vp = $('viewport');
  const set = (clientX) => {
    const r = vp.getBoundingClientRect();
    state.split = Math.min(1, Math.max(0, (clientX - r.left) / r.width));
    $('split').value = String(state.split);
  };
  let dragging = false;
  vp.onpointerdown = (e) => {
    dragging = true;
    vp.setPointerCapture(e.pointerId);
    set(e.clientX);
  };
  vp.onpointermove = (e) => dragging && set(e.clientX);
  vp.onpointerup = () => (dragging = false);
  $('split').oninput = (e) => (state.split = Number(e.target.value));
}

// ---------------------------------------------------------------- frame loop

const originalFbo = gl.createFramebuffer();

/** Draw the unprocessed input over the left part of the canvas. */
function drawOriginal(tex) {
  const splitPx = Math.round(state.split * state.width);
  if (splitPx <= 0) return;
  gl.bindFramebuffer(gl.READ_FRAMEBUFFER, originalFbo);
  gl.framebufferTexture2D(gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
  gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
  gl.enable(gl.SCISSOR_TEST);
  gl.scissor(0, 0, splitPx, state.height);
  const { width: w, height: h } = state;
  gl.blitFramebuffer(0, 0, w, h, 0, 0, w, h, gl.COLOR_BUFFER_BIT, gl.NEAREST);
  gl.disable(gl.SCISSOR_TEST);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
}

function pollTimers() {
  while (state.queries.length) {
    const q = state.queries[0];
    if (!gl.getQueryParameter(q, gl.QUERY_RESULT_AVAILABLE)) break;
    if (!gl.getParameter(timerExt.GPU_DISJOINT_EXT)) {
      const ms = gl.getQueryParameter(q, gl.QUERY_RESULT) / 1e6;
      state.gpuMs = state.gpuMs === null ? ms : state.gpuMs * 0.9 + ms * 0.1;
    }
    gl.deleteQuery(q);
    state.queries.shift();
  }
}

function frame(now) {
  const dt = Math.min(0.1, (now - state.lastNow) / 1000);
  state.lastNow = now;
  state.fps = state.fps * 0.95 + (dt > 0 ? 1 / dt : 0) * 0.05;
  const src = state.source;
  let inputs = null;

  if (src?.kind === 'scene') {
    const prev = state.time;
    if ($('animate').checked) state.time += dt;
    const g = scene.render(state.time, $('animate').checked ? prev : state.time, state.width, state.height);
    inputs = {
      color: g.color,
      depth: $('useDepth').checked ? g.depth : null,
      normal: $('useNormal').checked ? g.normal : null,
      motion: $('useMotion').checked ? g.motion : null,
    };
  } else if (state.media && state.media.readyState >= 2) {
    upload(state.media, state.media.videoWidth, state.media.videoHeight);
    inputs = { color: state.colorTex };
  } else if (src?.kind === 'image' && state.colorTex) {
    inputs = { color: state.colorTex };
  }

  if (inputs) {
    if (canvas.width !== state.width || canvas.height !== state.height) {
      canvas.width = state.width;
      canvas.height = state.height;
    }
    let query = null;
    if (timerExt && state.queries.length < 4) {
      query = gl.createQuery();
      gl.beginQuery(timerExt.TIME_ELAPSED_EXT, query);
    }
    const t0 = performance.now();
    delss.render(inputs, { width: state.width, height: state.height });
    state.cpuMs = state.cpuMs * 0.9 + (performance.now() - t0) * 0.1;
    if (query) {
      gl.endQuery(timerExt.TIME_ELAPSED_EXT);
      state.queries.push(query);
    }
    drawOriginal(inputs.color);
    if (timerExt) pollTimers();
    const passes = delss.chain.filter((s) => s.enabled).length;
    const timing =
      state.gpuMs !== null ? `GPU ${state.gpuMs.toFixed(2)} ms` : `CPU 提交 ${state.cpuMs.toFixed(2)} ms`;
    $('hud').textContent =
      `${timing} · ${passes} passes · ${state.width}×${state.height} · ${state.fps.toFixed(0)} fps`;
  }
  $('splitLine').style.left = `${state.split * 100}%`;
  requestAnimationFrame(frame);
}

// ---------------------------------------------------------------- boot

buildSources();
buildPresets();
bindSplit();
const hash = new URLSearchParams(location.hash.slice(1));
const initialPreset = PRESETS.some((p) => p.id === hash.get('preset')) ? hash.get('preset') : 'cel';
const initialSource = SOURCES.find((s) => s.id === hash.get('source')) ?? SOURCES[1];
if (hash.has('split')) state.split = Number(hash.get('split'));
$('split').value = String(state.split);
selectPreset(initialPreset);
await selectSource(initialSource);
requestAnimationFrame(frame);
window.__delss = { delss, state, ready: true };
