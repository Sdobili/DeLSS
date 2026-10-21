// Golden-image tests for every pass and preset, rendered headless on
// SwiftShader. UPDATE_GOLDEN=1 rewrites the references; review the PNG diff
// in the pull request before merging.
import assert from 'node:assert/strict';
import { existsSync, mkdirSync } from 'node:fs';
import { after, before, describe, test } from 'node:test';
import { PNG } from 'pngjs';
import { PASSES, PRESETS } from '../../web/src/generated.js';
import { compare, decodeBase64Rgba, openHarness, readPng, ROOT, writePng } from './browser.mjs';

const UPDATE = process.env.UPDATE_GOLDEN === '1';
const GOLDEN = `${ROOT}/tests/golden/web`;
const ACTUAL = `${ROOT}/test-results`;
const PHOTO = { color: '/tests/fixtures/photo.png' };
const SCENE = {
  color: '/tests/fixtures/scene_color.png',
  depth: '/tests/fixtures/scene_depth.png',
  normal: '/tests/fixtures/scene_normal.png',
};

let h;
before(async () => {
  h = await openHarness();
  mkdirSync(ACTUAL, { recursive: true });
});
after(async () => {
  await h?.close();
});

async function run(chain, frames) {
  const r = await h.page.evaluate((args) => window.runChain(args), { chain, frames });
  assert.deepEqual(h.errors, [], 'page errors');
  return r.outputs.map((b64) => decodeBase64Rgba(b64, r.width, r.height));
}

function checkGolden(name, image) {
  const file = `${GOLDEN}/${name}.png`;
  writePng(`${ACTUAL}/${name}.png`, image);
  if (UPDATE || !existsSync(file)) {
    writePng(file, image);
    return;
  }
  const result = compare(image, readPng(file));
  assert.ok(result.ok, `${name} differs from golden: ${result.reason}`);
}

describe('passes (defaults)', () => {
  for (const pass of Object.keys(PASSES)) {
    test(`${pass} on photo`, async () => {
      const [img] = await run([{ pass }], [PHOTO]);
      checkGolden(`pass_${pass}`, img);
    });
  }
  test('ink on G-buffer uses depth and normals', async () => {
    const [geo] = await run([{ pass: 'ink' }], [SCENE]);
    const [lum] = await run([{ pass: 'ink' }], [{ color: SCENE.color }]);
    checkGolden('pass_ink_gbuffer', geo);
    checkGolden('pass_ink_luma_scene', lum);
    assert.ok(compare(geo, lum).mean > 0.5, 'G-buffer edges must differ from luminance edges');
  });
});

describe('presets', () => {
  for (const preset of PRESETS) {
    test(`${preset.id}`, async () => {
      const [photo] = await run(preset.passes, [PHOTO]);
      const [scene] = await run(preset.passes, [SCENE]);
      checkGolden(`preset_${preset.id}_photo`, photo);
      checkGolden(`preset_${preset.id}_scene`, scene);
    });
  }
});

describe('contract', () => {
  test('empty chain is an exact bypass', async () => {
    const [img] = await run([], [PHOTO]);
    const ref = readPng(`${ROOT}/tests/fixtures/photo.png`);
    assert.equal(compare(img, ref).max, 0);
  });

  test('intensity 0 bypasses every pass', async () => {
    const ref = readPng(`${ROOT}/tests/fixtures/photo.png`);
    for (const pass of Object.keys(PASSES)) {
      const [img] = await run([{ pass, intensity: 0 }], [PHOTO]);
      assert.ok(compare(img, ref).max <= 1, `${pass} with intensity 0 changed the image`);
    }
  });

  test('unknown pass and parameter names throw', async () => {
    const err = await h.page.evaluate(async () => {
      const { resolveStep } = await import('/web/src/delss.js');
      const messages = [];
      for (const spec of [{ pass: 'nope' }, { pass: 'cel', params: { bandz: 3 } }]) {
        try {
          resolveStep(spec);
        } catch (e) {
          messages.push(String(e));
        }
      }
      return messages;
    });
    assert.equal(err.length, 2);
  });
});

/** Photo plus +-amp uniform noise per pixel, as a data URL (simulates sensor/video noise). */
function noisyFrame(seed, amp) {
  const ref = readPng(`${ROOT}/tests/fixtures/photo.png`);
  const png = new PNG({ width: ref.width, height: ref.height });
  let s = seed * 2654435761;
  for (let i = 0; i < ref.data.length; ++i) {
    s = (s * 1664525 + 1013904223) >>> 0;
    const noise = ((s >>> 8) / 16777216 - 0.5) * 2 * amp;
    png.data[i] = i % 4 === 3 ? 255 : Math.max(0, Math.min(255, Math.round(ref.data[i] + noise)));
  }
  return `data:image/png;base64,${PNG.sync.write(png).toString('base64')}`;
}

function flicker(frames) {
  let changed = 0;
  let total = 0;
  for (let f = 1; f < frames.length; ++f) {
    const a = frames[f - 1].data;
    const b = frames[f].data;
    for (let i = 0; i < a.length; i += 4) {
      total += 1;
      if (a[i] !== b[i] || a[i + 1] !== b[i + 1] || a[i + 2] !== b[i + 2]) changed += 1;
    }
  }
  return changed / total;
}

describe('temporal stability', () => {
  test('temporal pass suppresses quantization flicker on noisy input', async () => {
    const frames = Array.from({ length: 8 }, (_, i) => ({ color: noisyFrame(i + 1, 6) }));
    for (const id of ['pixel', 'gameboy', 'cel']) {
      const chain = PRESETS.find((p) => p.id === id).passes;
      const without = chain.filter((s) => s.pass !== 'temporal');
      const off = flicker(await run(without, frames));
      const on = flicker(await run(chain, frames));
      console.log(`  ${id}: flicker ${(off * 100).toFixed(2)}% -> ${(on * 100).toFixed(2)}%`);
      assert.ok(on < off / 3, `${id}: temporal should cut flicker 3x (${off} -> ${on})`);
    }
  });

  test('a static input produces a static output (no crawling dither)', async () => {
    for (const preset of PRESETS) {
      const outs = await run(preset.passes, [PHOTO, PHOTO, PHOTO]);
      const expectsMotion = preset.passes.some((s) => s.pass === 'vhs');
      if (!expectsMotion) assert.equal(flicker(outs), 0, `${preset.id} flickers on a still frame`);
    }
  });
});
