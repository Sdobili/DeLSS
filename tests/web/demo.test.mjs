// Smoke test for the live demo page: every preset and source boots without
// errors and the HUD reports a frame time.
import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';
import { chromium } from 'playwright';
import { PRESETS } from '../../web/src/generated.js';
import { serve } from '../../tools/serve.mjs';
import { ROOT } from './browser.mjs';

let server;
let browser;
before(async () => {
  server = await serve(`${ROOT}/web`);
  browser = await chromium.launch({
    executablePath: process.env.CHROMIUM_PATH || undefined,
    args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
});
after(async () => {
  await browser?.close();
  await server?.close();
});

for (const source of ['scene', 'knight']) {
  test(`demo runs every preset on ${source}`, async () => {
    const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
    const errors = [];
    page.on('pageerror', (e) => errors.push(String(e)));
    page.on('console', (m) => m.type() === 'error' && errors.push(m.text()));
    // Small render size: CI runners rasterize WebGL on the CPU.
    await page.goto(`${server.url}/#source=${source}&width=320`);
    await page.waitForFunction(() => window.__delss?.ready);
    for (const preset of PRESETS) {
      await page.click(`#presets button[data-id="${preset.id}"]`);
      await page.waitForFunction(() => /ms/.test(document.getElementById('hud').textContent ?? ''));
    }
    assert.match(await page.textContent('#hud'), /\d+(\.\d+)? ms/);
    assert.ok((await page.locator('#chain .step').count()) > 0, 'pass chain UI is rendered');
    assert.deepEqual(errors, []);
    await page.close();
  });
}
