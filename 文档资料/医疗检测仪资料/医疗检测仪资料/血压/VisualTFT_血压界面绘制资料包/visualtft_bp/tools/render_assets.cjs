// Rasterize source SVG layout assets; original SPO2 and prior BP files are untouched.
const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('C:/Users/ASUS/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp');
const root = path.resolve(__dirname, '..');
(async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(root, 'manifest.json'), 'utf8'));
  for (const item of manifest.renders) {
    const out = await sharp(path.join(root, item.svg)).flatten({ background: '#0F1B2D' }).png().toFile(path.join(root, item.png));
    if (out.width !== item.width || out.height !== item.height) throw new Error(`Bad dimensions: ${item.png}`);
  }
  console.log(JSON.stringify({ rendered: manifest.renders.length, status: 'ok' }));
})().catch(error => { console.error(error); process.exitCode = 1; });
