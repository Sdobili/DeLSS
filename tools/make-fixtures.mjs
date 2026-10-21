// Regenerate tests/fixtures/: a downscaled sample photo (luminance edge path)
// and the demo scene's G-buffer (depth/normal edge path). Fixtures are
// committed so the WebGL and C++ tests consume byte-identical inputs.
import { execFileSync } from 'node:child_process';
import { decodeBase64Rgba, openHarness, ROOT, writePng } from '../tests/web/browser.mjs';

const W = 320;
const H = 180;
const out = `${ROOT}/tests/fixtures`;

execFileSync('python3', [
  '-c',
  `from PIL import Image; Image.open('${ROOT}/web/samples/knight.jpg').convert('RGB')` +
    `.resize((${W}, ${H}), Image.LANCZOS).save('${out}/photo.png')`,
]);

const h = await openHarness();
const scene = await h.page.evaluate((a) => window.dumpScene(a), { time: 1.0, width: W, height: H });
for (const [name, b64] of Object.entries(scene)) {
  writePng(`${out}/scene_${name}.png`, decodeBase64Rgba(b64, W, H));
}
await h.close();
if (h.errors.length) throw h.errors[0];
console.log('fixtures written to tests/fixtures');
