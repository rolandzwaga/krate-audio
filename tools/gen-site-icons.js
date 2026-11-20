#!/usr/bin/env node
//
// gen-site-icons.js
// =============================================================================
// Rasterises the site logo (docs/assets/favicon.svg) into the bitmap icons search engines and devices ask for, and
// renders the site's own 1200 x 630 social card. Output (committed; tools/gen-site.js copies it into the site):
//   docs/favicon.ico                  16, 32 and 48 px (PNG-in-ICO); Google wants a favicon in multiples of 48 px
//   docs/assets/apple-touch-icon.png  180 px
//   docs/assets/logo-512.png          512 px, the Organization logo in the structured data (tools/site/common.js)
//   docs/assets/og-image.png          1200 x 630, the root page's and the fallback social card
//
// It drives a local Chromium (Edge or Chrome) in headless mode: no npm dependency, and the browser's SVG renderer is
// the one visitors see. Rerun after changing favicon.svg or the card text below.
//
// Usage: node tools/gen-site-icons.js [--browser <path to msedge/chrome>]
// Exit codes: 0 = written, 1 = failed.
// =============================================================================
'use strict';

const childProcess = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.resolve(__dirname, '..');
const SVG = path.join(ROOT, 'docs', 'assets', 'favicon.svg');

const BROWSERS = [
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
  '/usr/bin/google-chrome',
  '/usr/bin/chromium',
  '/usr/bin/chromium-browser',
];

function fail(msg) {
  process.stderr.write(`gen-site-icons: ${msg}\n`);
  process.exit(1);
}

function pngSize(buf) {
  if (buf.length < 24 || buf.readUInt32BE(0) !== 0x89504e47) return null;
  return { width: buf.readUInt32BE(16), height: buf.readUInt32BE(20) };
}

/** Renders @p html at width x height through the headless browser; returns the PNG bytes. */
function screenshot(browser, html, width, height, transparent) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'site-icons-'));
  try {
    const page = path.join(dir, 'page.html');
    const out = path.join(dir, 'shot.png');
    fs.writeFileSync(page, html);
    const args = [
      '--headless=new',
      '--disable-gpu',
      '--hide-scrollbars',
      '--force-device-scale-factor=1',
      `--user-data-dir=${path.join(dir, 'profile')}`,
      `--window-size=${width},${height}`,
      `--screenshot=${out}`,
    ];
    if (transparent) args.push('--default-background-color=00000000');
    args.push(`file:///${page.split(path.sep).join('/')}`);
    const r = childProcess.spawnSync(browser, args, { timeout: 60000, encoding: 'utf8' });
    if (!fs.existsSync(out)) fail(`${path.basename(browser)} wrote no screenshot (exit ${r.status}): ${r.stderr}`);
    const png = fs.readFileSync(out);
    const size = pngSize(png);
    if (size === null || size.width !== width || size.height !== height) {
      fail(`screenshot is ${JSON.stringify(size)}, want ${width} x ${height}`);
    }
    return png;
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

function iconPage(svg, size) {
  return `<!DOCTYPE html><html><head><style>html,body{margin:0;background:transparent}
svg{display:block;width:${size}px;height:${size}px}</style></head><body>${svg}</body></html>`;
}

function ogPage(svg) {
  return `<!DOCTYPE html><html><head><style>
html,body{margin:0;width:1200px;height:630px;overflow:hidden}
body{background:radial-gradient(circle at 30% 20%,#1d1d35 0%,#0a0a0f 70%);color:#e8e8f0;
  font-family:'Inter','Segoe UI',Roboto,Helvetica,Arial,sans-serif;display:flex;align-items:center;gap:64px;padding:0 96px;
  box-sizing:border-box}
svg{width:260px;height:260px;flex:none}
h1{margin:0 0 18px;font-size:92px;font-weight:700;letter-spacing:-2px;
  background:linear-gradient(135deg,#e8e8f0 0%,#818cf8 100%);-webkit-background-clip:text;color:transparent}
p{margin:0;font-size:36px;line-height:1.35;color:#a5a5c0}
</style></head><body>${svg}<div><h1>Krate Audio</h1><p>Free open-source VST3 plugins<br>for Windows, macOS and Linux</p></div></body></html>`;
}

/** An ICO file holding the given PNGs (Windows Vista+ and every browser read PNG-compressed ICO entries). */
function ico(pngs) {
  const header = Buffer.alloc(6 + 16 * pngs.length);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(pngs.length, 4);
  let offset = header.length;
  pngs.forEach((png, i) => {
    const { width, height } = pngSize(png);
    const e = 6 + 16 * i;
    header.writeUInt8(width >= 256 ? 0 : width, e);
    header.writeUInt8(height >= 256 ? 0 : height, e + 1);
    header.writeUInt8(0, e + 2);
    header.writeUInt8(0, e + 3);
    header.writeUInt16LE(1, e + 4);
    header.writeUInt16LE(32, e + 6);
    header.writeUInt32LE(png.length, e + 8);
    header.writeUInt32LE(offset, e + 12);
    offset += png.length;
  });
  return Buffer.concat([header, ...pngs]);
}

function main(argv) {
  const at = argv.indexOf('--browser');
  const browser = at >= 0 ? argv[at + 1] : BROWSERS.find((b) => fs.existsSync(b));
  if (!browser || !fs.existsSync(browser)) fail('no Edge or Chrome found; pass --browser <path>');
  const svg = fs.readFileSync(SVG, 'utf8');
  const icon = (size) => screenshot(browser, iconPage(svg, size), size, size, true);

  const write = (rel, bytes) => {
    fs.writeFileSync(path.join(ROOT, ...rel.split('/')), bytes);
    process.stdout.write(`${rel}: ${bytes.length} bytes\n`);
  };
  write('docs/favicon.ico', ico([icon(16), icon(32), icon(48)]));
  write('docs/assets/apple-touch-icon.png', icon(180));
  write('docs/assets/logo-512.png', icon(512));
  write('docs/assets/og-image.png', screenshot(browser, ogPage(svg), 1200, 630, false));
}

main(process.argv.slice(2));
