// Shared headless-Chromium setup for the WebGL tests. WebGL2 runs on
// SwiftShader (a CPU Vulkan implementation), so tests need no GPU and render
// bit-for-bit the same on every CI runner.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { PNG } from 'pngjs';
import { serve } from '../../tools/serve.mjs';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');

export async function openHarness() {
  const server = await serve(ROOT);
  const browser = await chromium.launch({
    executablePath: process.env.CHROMIUM_PATH || undefined,
    args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e));
  page.on('console', (m) => m.type() === 'error' && errors.push(new Error(m.text())));
  await page.goto(`${server.url}/web/test/harness.html`);
  await page.waitForFunction(() => window.ready === true || null, null, { timeout: 30000 });
  return {
    page,
    errors,
    async close() {
      await browser.close();
      await server.close();
    },
  };
}

export function decodeBase64Rgba(b64, width, height) {
  const data = Buffer.from(b64, 'base64');
  if (data.length !== width * height * 4) throw new Error('bad pixel buffer size');
  return { width, height, data };
}

export function writePng(path, { width, height, data }) {
  const png = new PNG({ width, height });
  data.copy(png.data);
  writeFileSync(path, PNG.sync.write(png));
}

export function readPng(path) {
  const png = PNG.sync.read(readFileSync(path));
  return { width: png.width, height: png.height, data: png.data };
}

/**
 * Compare two RGBA images. Golden tests allow tiny rounding differences
 * between GPU implementations but nothing a person could see. A pixel only
 * counts as bad if no pixel in the reference's 3x3 neighborhood matches it:
 * bilinear filtering precision differs between GL implementations, which can
 * shift a thresholded edge (an ink line) by one pixel.
 */
export function compare(a, b) {
  if (a.width !== b.width || a.height !== b.height) {
    return { ok: false, reason: `size ${a.width}x${a.height} != ${b.width}x${b.height}` };
  }
  const { width: w, height: h } = a;
  const diffAt = (i, j) => {
    let px = 0;
    for (let c = 0; c < 3; ++c) px = Math.max(px, Math.abs(a.data[i * 4 + c] - b.data[j * 4 + c]));
    return px;
  };
  let sum = 0;
  let bad = 0;
  let max = 0;
  for (let y = 0; y < h; ++y) {
    for (let x = 0; x < w; ++x) {
      const i = y * w + x;
      for (let c = 0; c < 3; ++c) sum += Math.abs(a.data[i * 4 + c] - b.data[i * 4 + c]);
      const px = diffAt(i, i);
      max = Math.max(max, px);
      if (px <= 16) continue;
      let best = px;
      for (let dy = -1; dy <= 1 && best > 16; ++dy) {
        for (let dx = -1; dx <= 1 && best > 16; ++dx) {
          const nx = x + dx;
          const ny = y + dy;
          if (nx >= 0 && ny >= 0 && nx < w && ny < h) best = Math.min(best, diffAt(i, ny * w + nx));
        }
      }
      if (best > 16) bad += 1;
    }
  }
  const n = w * h;
  const mean = sum / (n * 3);
  const badRatio = bad / n;
  const ok = mean < 1.0 && badRatio < 0.005;
  return { ok, mean, badRatio, max, reason: `mean ${mean.toFixed(3)}, >16: ${(badRatio * 100).toFixed(3)}%` };
}
