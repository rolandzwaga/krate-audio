'use strict';
require('./helpers/write-guard.js');

// tools/gen-manager-latest.js (Krate Manager Phase 11: spec FR-040 to FR-042, FR-044; SC-009; plan §4.9, PD-16, PD-21;
// SI-18, SI-19, SI-21; tasks TA-11).
// Every case builds a fresh temp assets dir: random bytes under D6's names (SI-21: seven latest.json keys, no
// .deb.sig), each updater asset signed with tools/minisign.js over a throwaway in-memory key and a Tauri-shaped
// trusted comment, the .sig being the base64 of the minisig text (Tauri's form). main(argv, io) runs in process with a
// capturing io.
// Nothing here reads or writes outside os.tmpdir(); no network, no spawned process.
// Two top-level tests (the feed-tools TAP plan counts top-level tests only); every other check is a subtest.

const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const minisign = require('../../minisign.js');

const TOOL = path.resolve(__dirname, '..', '..', 'gen-manager-latest.js');
const USAGE = 'usage: node tools/gen-manager-latest.js';
const V = '0.2.0';
const TAG = `manager/v${V}`;
const NAMES = Object.freeze({
  windows: `Krate-Manager-${V}-Windows-x64-setup.exe`,
  macos: `Krate-Manager-${V}-macOS-universal.app.tar.gz`,
  linux: `Krate-Manager-${V}-Linux-x86_64.AppImage`,
});
const DEB = `Krate-Manager-${V}-Linux-amd64.deb`;
const PKG = `Krate-Manager-${V}-macOS.pkg`;
// SI-21: seven keys, bytewise order.
const SEVEN = Object.freeze([
  'darwin-aarch64',
  'darwin-universal',
  'darwin-x86_64',
  'linux-x86_64',
  'linux-x86_64-appimage',
  'windows-x86_64',
  'windows-x86_64-nsis',
]);
const KEY_OS = Object.freeze({
  'darwin-aarch64': 'macos',
  'darwin-universal': 'macos',
  'darwin-x86_64': 'macos',
  'linux-x86_64': 'linux',
  'linux-x86_64-appimage': 'linux',
  'windows-x86_64': 'windows',
  'windows-x86_64-nsis': 'windows',
});
const NOTES = '### Added\n\n- Krate Manager updates itself.\n\n### Fixed\n\n- The status line wraps.\n';

function loadTool() {
  return require(TOOL);
}

function tmpDir(tag) {
  return fs.mkdtempSync(path.join(os.tmpdir(), `km11-gml-${tag}-`));
}

function capture() {
  const io = { out: '', err: '' };
  io.stdout = { write: (s) => void (io.out += s) };
  io.stderr = { write: (s) => void (io.err += s) };
  return io;
}

function trustedComment(name, version) {
  return `timestamp:1\tfile:${name}\tversion:${version}`;
}

/** Tauri's .sig: base64 of the four-line minisig text. */
function tauriSig(data, sk, comment) {
  return Buffer.from(minisign.signBuffer(data, sk, { trustedComment: comment }), 'utf8').toString('base64');
}

/**
 * A fixture: `{ root, assets, out, outDir, pub, notes, sk, sigText }` with the updater assets of `oses` (+ .sig),
 * the .deb / .pkg when their OS is included (unless `releaseOnly` is false). The Windows .sig ends in one LF (the
 * tool trims it), the others have none.
 */
function fixture({ oses = ['windows', 'macos', 'linux'], releaseOnly = true } = {}) {
  const root = tmpDir('fx');
  const assets = path.join(root, 'assets');
  const outDir = path.join(root, 'out');
  fs.mkdirSync(assets);
  fs.mkdirSync(outDir);
  const sk = minisign.generateKeyPair();
  const pub = path.join(root, 'signing.pub');
  fs.writeFileSync(pub, minisign.formatPublicKey(sk));
  const notes = path.join(root, 'notes.md');
  fs.writeFileSync(notes, NOTES);
  const sigText = {};
  for (const osName of oses) {
    const name = NAMES[osName];
    const data = crypto.randomBytes(4096);
    fs.writeFileSync(path.join(assets, name), data);
    const sig = tauriSig(data, sk, trustedComment(name, V)) + (osName === 'windows' ? '\n' : '');
    fs.writeFileSync(path.join(assets, `${name}.sig`), sig);
    sigText[osName] = sig;
  }
  if (releaseOnly && oses.includes('linux')) fs.writeFileSync(path.join(assets, DEB), crypto.randomBytes(512));
  if (releaseOnly && oses.includes('macos')) fs.writeFileSync(path.join(assets, PKG), crypto.randomBytes(512));
  return { root, assets, outDir, out: path.join(outDir, 'latest.json'), pub, notes, sk, sigText };
}

function args(fx, extra = []) {
  return [
    '--version',
    V,
    '--tag',
    TAG,
    '--repo',
    'o/r',
    '--notes',
    fx.notes,
    '--pub-date',
    '2026-11-01T12:00:00Z',
    '--assets',
    fx.assets,
    '--pubkey',
    fx.pub,
    '--out',
    fx.out,
    ...extra,
  ];
}

function run(argv) {
  const io = capture();
  const code = loadTool().main(argv, io);
  return { code, out: io.out, err: io.err };
}

function resign(fx, osName, comment, sk = fx.sk) {
  const name = NAMES[osName];
  const data = fs.readFileSync(path.join(fx.assets, name));
  fs.writeFileSync(path.join(fx.assets, `${name}.sig`), tauriSig(data, sk, comment));
}

test('gen-manager-latest writes per-tag latest.json', async (t) => {
  await t.test('the tool form: shebang, exports, usage on exit 2', () => {
    const src = fs.readFileSync(TOOL, 'utf8');
    assert.equal(src.split(/\r?\n/u)[0], '#!/usr/bin/env node');
    assert.equal(src.includes('\r\n'), false, 'LF line ends');
    const mod = loadTool();
    assert.deepEqual(Object.keys(mod).sort(), ['ASSET_TABLE', 'build', 'main']);
    assert.equal(typeof mod.main, 'function');
    assert.equal(typeof mod.build, 'function');
    for (const argv of [[], ['--frobnicate'], ['--version']]) {
      const r = run(argv);
      assert.equal(r.code, 2, `${JSON.stringify(argv)}: exit`);
      assert.ok(r.err.includes(USAGE), `${JSON.stringify(argv)}: usage line, got ${r.err}`);
    }
  });

  await t.test('ASSET_TABLE is D6 as amended by SI-21', () => {
    const { ASSET_TABLE } = loadTool();
    const keys = ASSET_TABLE.flatMap((row) => row.keys).sort();
    assert.deepEqual(keys, [...SEVEN]);
    const names = ASSET_TABLE.map((row) => row.updater.replace('<v>', V));
    assert.deepEqual(names.sort(), Object.values(NAMES).sort());
    const releaseOnly = ASSET_TABLE.map((row) => row.releaseOnly && row.releaseOnly.replace('<v>', V)).filter(Boolean);
    assert.deepEqual(releaseOnly.sort(), [DEB, PKG].sort());
  });

  await t.test('all three OSs: seven keys, per-tag URLs, inline signatures, the notes file as is', () => {
    const fx = fixture();
    const r = run(args(fx));
    assert.equal(r.code, 0, r.err);
    assert.equal(r.err, '');
    const text = fs.readFileSync(fx.out, 'utf8');
    const doc = JSON.parse(text);
    assert.equal(text, JSON.stringify(doc, null, 2) + '\n');
    assert.deepEqual(Object.keys(doc).sort(), ['notes', 'platforms', 'pub_date', 'version']);
    assert.equal(doc.version, V);
    assert.equal(doc.pub_date, '2026-11-01T12:00:00Z');
    assert.equal(doc.notes, NOTES);
    assert.deepEqual(Object.keys(doc.platforms), [...SEVEN]);
    for (const key of SEVEN) {
      const osName = KEY_OS[key];
      const entry = doc.platforms[key];
      assert.deepEqual(Object.keys(entry), ['signature', 'url'], key);
      assert.equal(entry.url, `https://github.com/o/r/releases/download/manager/v${V}/${NAMES[osName]}`, key);
      const expected = fx.sigText[osName].endsWith('\n') ? fx.sigText[osName].slice(0, -1) : fx.sigText[osName];
      assert.equal(entry.signature, expected, key);
    }
    assert.equal(doc.platforms['windows-x86_64'].signature.endsWith('\n'), false, 'one trailing LF trimmed');
    // No release-only asset is named.
    assert.equal(text.includes(DEB), false);
    assert.equal(text.includes(PKG), false);
    assert.deepEqual(fs.readdirSync(fx.outDir), ['latest.json'], 'no temp file left beside the output');
  });

  await t.test('a second run is byte-identical, and build() returns the same text', () => {
    const fx = fixture();
    assert.equal(run(args(fx)).code, 0);
    const first = fs.readFileSync(fx.out);
    assert.equal(run(args(fx)).code, 0);
    assert.ok(fs.readFileSync(fx.out).equals(first), 'overwrite run');
    const second = path.join(fx.outDir, 'again.json');
    const argv = args(fx);
    argv[argv.indexOf('--out') + 1] = second;
    assert.equal(run(argv).code, 0);
    assert.ok(fs.readFileSync(second).equals(first), 'fresh output path');
    const built = loadTool().build({
      version: V,
      tag: TAG,
      repo: 'o/r',
      notes: NOTES,
      pubDate: '2026-11-01T12:00:00Z',
      assets: fx.assets,
      pubkeyText: fs.readFileSync(fx.pub, 'utf8'),
    });
    assert.equal(built.text, first.toString('utf8'));
    assert.deepEqual(built.json, JSON.parse(built.text));
  });

  await t.test('--check-only verifies and writes nothing', () => {
    const fx = fixture();
    const r = run(args(fx, ['--check-only']));
    assert.equal(r.code, 0, r.err);
    assert.equal(fs.existsSync(fx.out), false);
    assert.deepEqual(fs.readdirSync(fx.outDir), []);
  });

  await t.test('--check-only --platform-subset linux over Linux assets only', () => {
    for (const releaseOnly of [true, false]) {
      const fx = fixture({ oses: ['linux'], releaseOnly });
      const r = run(args(fx, ['--check-only', '--platform-subset', 'linux']));
      assert.equal(r.code, 0, `deb ${releaseOnly}: ${r.err}`);
      assert.deepEqual(fs.readdirSync(fx.outDir), []);
      // A build leg passes no notes, date or output (FR-053).
      const lean = ['--version', V, '--tag', TAG, '--assets', fx.assets, '--pubkey', fx.pub, '--check-only'];
      const r2 = run([...lean, '--platform-subset', 'linux']);
      assert.equal(r2.code, 0, `lean: ${r2.err}`);
    }
  });

  await t.test('a build leg without its pkg passes, and the macOS pkg is never named', () => {
    const fx = fixture({ releaseOnly: false });
    assert.equal(run(args(fx)).code, 0);
    const doc = JSON.parse(fs.readFileSync(fx.out, 'utf8'));
    assert.deepEqual(Object.keys(doc.platforms), [...SEVEN]);
  });

  await t.test('--repo-url-base http://127.0.0.1:7 --only windows: loopback URLs, the two Windows keys', () => {
    const fx = fixture({ oses: ['windows'] });
    const r = run(args(fx, ['--repo-url-base', 'http://127.0.0.1:7', '--only', 'windows']));
    assert.equal(r.code, 0, r.err);
    const doc = JSON.parse(fs.readFileSync(fx.out, 'utf8'));
    assert.deepEqual(Object.keys(doc.platforms), ['windows-x86_64', 'windows-x86_64-nsis']);
    for (const entry of Object.values(doc.platforms)) {
      assert.equal(entry.url, `http://127.0.0.1:7/o/r/releases/download/manager/v${V}/${NAMES.windows}`);
    }
  });
});

test('gen-manager-latest refuses what it cannot verify', async (t) => {
  // Each refusal: exit 1 (or 2 for usage), the code and the asset on stderr, and no output file (nor a temp file).
  function refuses(fx, argv, code, exit, needle) {
    const r = run(argv);
    assert.equal(r.code, exit, `exit (stderr: ${r.err})`);
    assert.ok(r.err.includes(code), `stderr names ${code}: ${r.err}`);
    if (needle !== undefined) assert.ok(r.err.includes(needle), `stderr names ${needle}: ${r.err}`);
    assert.equal(fs.existsSync(fx.out), false, 'no output file');
    assert.deepEqual(fs.readdirSync(fx.outDir), [], 'nothing written beside it');
    return r;
  }

  await t.test('a one-byte tamper', () => {
    const fx = fixture();
    const file = path.join(fx.assets, NAMES.macos);
    const data = fs.readFileSync(file);
    data[100] ^= 0x01;
    fs.writeFileSync(file, data);
    const r = refuses(fx, args(fx), 'E_LATEST_SIGNATURE', 1, NAMES.macos);
    assert.ok(r.err.includes('E_MINISIG_SIGNATURE'), r.err);
  });

  await t.test('another key', () => {
    const fx = fixture();
    resign(fx, 'linux', trustedComment(NAMES.linux, V), minisign.generateKeyPair());
    const r = refuses(fx, args(fx), 'E_LATEST_SIGNATURE', 1, NAMES.linux);
    assert.ok(r.err.includes('E_MINISIG_KEY_ID'), r.err);
  });

  await t.test('version:0.1.9', () => {
    const fx = fixture();
    resign(fx, 'windows', trustedComment(NAMES.windows, '0.1.9'));
    refuses(fx, args(fx), 'E_LATEST_SIGNATURE', 1, NAMES.windows);
  });

  await t.test('no version: field', () => {
    const fx = fixture();
    resign(fx, 'windows', `timestamp:1\tfile:${NAMES.windows}`);
    refuses(fx, args(fx), 'E_LATEST_SIGNATURE', 1, NAMES.windows);
  });

  await t.test('two version: fields', () => {
    const fx = fixture();
    resign(fx, 'windows', `timestamp:1\tversion:${V}\tversion:${V}`);
    refuses(fx, args(fx), 'E_LATEST_SIGNATURE', 1, NAMES.windows);
  });

  await t.test('a .sig that is not base64', () => {
    const fx = fixture();
    fs.writeFileSync(path.join(fx.assets, `${NAMES.linux}.sig`), 'this is not base64!\n');
    refuses(fx, args(fx), 'E_LATEST_SIGNATURE', 1, NAMES.linux);
  });

  await t.test('the signature checks run under --check-only too', () => {
    const fx = fixture();
    resign(fx, 'macos', trustedComment(NAMES.macos, '0.1.9'));
    refuses(fx, args(fx, ['--check-only']), 'E_LATEST_SIGNATURE', 1, NAMES.macos);
  });

  await t.test('a missing .sig', () => {
    const fx = fixture();
    fs.rmSync(path.join(fx.assets, `${NAMES.linux}.sig`));
    refuses(fx, args(fx), 'E_LATEST_ASSETS', 1, `${NAMES.linux}.sig`);
  });

  await t.test('a missing updater asset', () => {
    const fx = fixture();
    fs.rmSync(path.join(fx.assets, NAMES.windows));
    refuses(fx, args(fx), 'E_LATEST_ASSETS', 1, NAMES.windows);
  });

  await t.test('an extra file', () => {
    const fx = fixture();
    fs.writeFileSync(path.join(fx.assets, 'Krate-Manager-app.zip'), 'x');
    refuses(fx, args(fx), 'E_LATEST_ASSETS', 1, 'Krate-Manager-app.zip');
  });

  await t.test('a .deb.sig is extra (SI-21)', () => {
    const fx = fixture();
    fs.writeFileSync(path.join(fx.assets, `${DEB}.sig`), 'x');
    refuses(fx, args(fx), 'E_LATEST_ASSETS', 1, `${DEB}.sig`);
  });

  await t.test('another OS under --only is extra', () => {
    const fx = fixture({ oses: ['windows', 'linux'] });
    refuses(
      fx,
      args(fx, ['--repo-url-base', 'http://127.0.0.1:7', '--only', 'windows']),
      'E_LATEST_ASSETS',
      1,
      NAMES.linux,
    );
  });

  await t.test('missing and extra are each named in one refusal', () => {
    const fx = fixture();
    fs.rmSync(path.join(fx.assets, `${NAMES.macos}.sig`));
    fs.writeFileSync(path.join(fx.assets, 'stray.txt'), 'x');
    const r = refuses(fx, args(fx), 'E_LATEST_ASSETS', 1, `${NAMES.macos}.sig`);
    assert.ok(r.err.includes('stray.txt'), r.err);
  });

  await t.test('usage refusals exit 2 and write nothing', () => {
    const fx = fixture();
    const cases = [
      ['--only without --repo-url-base', args(fx, ['--only', 'windows'])],
      ['--repo-url-base without --only', args(fx, ['--repo-url-base', 'http://127.0.0.1:7'])],
      ['--repo-url-base https://x', args(fx, ['--repo-url-base', 'https://x', '--only', 'windows'])],
      ['--repo-url-base http://localhost:7', args(fx, ['--repo-url-base', 'http://localhost:7', '--only', 'windows'])],
      ['--platform-subset without --check-only', args(fx, ['--platform-subset', 'linux'])],
      ['--only with an unknown OS', args(fx, ['--repo-url-base', 'http://127.0.0.1:7', '--only', 'beos'])],
      ['a tag that is not manager/v<version>', args(fx).map((a) => (a === TAG ? 'manager/v0.1.9' : a))],
      ['a repo that is not owner/repo', args(fx).map((a) => (a === 'o/r' ? 'o/r/x' : a))],
      ['a pub-date that is not RFC 3339', args(fx).map((a) => (a === '2026-11-01T12:00:00Z' ? 'yesterday' : a))],
      ['no --out on a real run', args(fx).slice(0, -2)],
    ];
    for (const [why, argv] of cases) {
      const r = run(argv);
      assert.equal(r.code, 2, `${why}: exit (stderr: ${r.err})`);
      assert.ok(r.err.includes(USAGE), `${why}: usage line`);
      assert.equal(fs.existsSync(fx.out), false, `${why}: no output file`);
    }
    assert.deepEqual(fs.readdirSync(fx.outDir), []);
  });
});
