// Raster export of the editable SVG design artifacts (not image editing).
const path = require('node:path');
const fs = require('node:fs/promises');
const sharp = require('C:/Users/ASUS/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp');

(async () => {
  const files = (await fs.readdir(__dirname)).filter(name => name.endsWith('.svg'));
  for (const name of files) {
    const src = path.join(__dirname, name);
    const dst = path.join(__dirname, name.replace(/\.svg$/, '.png'));
    const info = await sharp(src).png().toFile(dst);
    console.log(`${path.basename(dst)}: ${info.width}x${info.height}`);
  }
})().catch(err => { console.error(err); process.exitCode = 1; });
