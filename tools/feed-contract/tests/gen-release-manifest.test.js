'use strict';
require('./helpers/write-guard.js');

// tools/gen-release-manifest.js: release.json + SHA256SUMS from a signed, fact-recorded asset set, the C3 comments
// for the signing loop, and the signing-configuration decision (spec FR-020 to FR-029, SC-006, SC-007, SC-009,
// SC-022; plan §4.3, PD-23, PD-25, §7.1).
// The asset set is two stubs and the committed KAT tarball in a temp assets/ dir, signed with a throwaway in-memory
// key. Linux facts come from release-facts.linuxFacts with the tarball's own listing as the lister (PD-4), so every
// OS runs every case. Repo fixtures are only read; every output lands in an fs.mkdtemp dir.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const contract = require('../contract.js');
const listing = require('../listing.js');
const minisign = require('../../minisign.js');
const releaseFacts = require('../../release-facts.js');
const genRM = require('../../gen-release-manifest.js');

const FIXTURES = path.join(__dirname, '..', 'fixtures');
const KAT_TARBALL = path.join(FIXTURES, 'kat', 'KatPlugin-1.2.3-Linux-x64.tar.gz');
const KAT_LISTING = `${KAT_TARBALL}.listing`;
const KAT_PUB = path.join(FIXTURES, 'kat', 'kat.pub');
const KS_PUB = path.join(FIXTURES, 'phase2', 'kat-secret', 'kat.pub');
const KAT_PLUGIN = path.join(FIXTURES, 'phase2', 'KatPlugin');
const VERSION_JSON = path.join(KAT_PLUGIN, 'version.json');
const SETUP_ISS = path.join(KAT_PLUGIN, 'installers', 'windows', 'setup.iss');
const OBJDUMP = path.join(FIXTURES, 'phase2', 'objdump', 'glibc-mixed.txt');
const REPO = 'krate-test/krate-audio';
const PUB_DATE = '2026-10-30T12:00:00Z';
const TAG = 'katplugin/v1.2.3';
const WIN = 'KatPlugin-1.2.3-Windows-x64.exe';
const MAC = 'KatPlugin-1.2.3-macOS.pkg';
const LINUX = 'KatPlugin-1.2.3-Linux-x64.tar.gz';
const NAMES = { 'darwin-universal': MAC, 'linux-x86_64': LINUX, 'windows-x86_64': WIN };
const FORMATS = { 'darwin-universal': 'pkg', 'linux-x86_64': 'tar.gz', 'windows-x86_64': 'inno-exe' };
const KAT_PAYLOAD_SHA = 'db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6';
const PREFIX = 'gen-release-manifest: ';

const read = (p) => fs.readFileSync(p, 'utf8');
const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');
const writeJson = (p, doc) => fs.writeFileSync(p, JSON.stringify(doc, null, 2) + '\n');

function withTemp(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km2-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

function run(argv, env = {}) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
    env,
  };
  const code = genRM.main(argv, io);
  return { code, stdout, stderr };
}

function darwinFactsFor(pkg) {
  const bytes = fs.readFileSync(pkg);
  return {
    asset: path.basename(pkg),
    size: bytes.length,
    sha256: sha256(bytes),
    platform_key: 'darwin-universal',
    min_os: '11.0',
    archs: ['arm64', 'x86_64'],
    macho: [],
  };
}

function linuxFactsFor(tmp, tarball) {
  return releaseFacts.linuxFacts(
    { tarball, extracted: tmp, objdumpText: read(OBJDUMP) },
    { listDirectory: () => listing.listingFromTarGz(fs.readFileSync(tarball)) },
  );
}

/**
 * The SC-006 asset set in a fresh temp dir: stubs + the KAT tarball, a throwaway key (public half at <tmp>/pub), facts
 * files recorded from the assets, every asset signed with the comment trustedComments gives it.
 * @param {{versionJsonText?: string}} [opts]  a version.json written to the temp dir instead of the KatPlugin fixture
 */
function setup(t, name, { versionJsonText } = {}) {
  const tmp = withTemp(t, name);
  const assets = path.join(tmp, 'assets');
  const out = path.join(tmp, 'out');
  fs.mkdirSync(assets);
  fs.mkdirSync(out);
  fs.writeFileSync(path.join(assets, WIN), crypto.randomBytes(1000));
  fs.writeFileSync(path.join(assets, MAC), crypto.randomBytes(2000));
  fs.copyFileSync(KAT_TARBALL, path.join(assets, LINUX));

  const pair = minisign.generateKeyPair();
  const pub = path.join(tmp, 'pub');
  fs.writeFileSync(pub, minisign.formatPublicKey(pair));

  let versionJson = VERSION_JSON;
  if (versionJsonText !== undefined) {
    versionJson = path.join(tmp, 'version.json');
    fs.writeFileSync(versionJson, versionJsonText);
  }
  const darwinDoc = darwinFactsFor(path.join(assets, MAC));
  const darwinFacts = path.join(tmp, 'darwin-facts.json');
  writeJson(darwinFacts, darwinDoc);
  const linuxFacts = path.join(tmp, 'linux-facts.json');
  writeJson(linuxFacts, linuxFactsFor(tmp, path.join(assets, LINUX)));

  const comments = genRM.trustedComments({
    versionJsonText: read(versionJson),
    assetsDir: assets,
    darwinFacts: darwinDoc,
  });
  for (const c of comments) {
    const sig = minisign.signFile(path.join(assets, c.asset), pair, { trustedComment: c.comment });
    fs.writeFileSync(path.join(assets, `${c.asset}.minisig`), sig);
  }
  return {
    tmp,
    assets,
    out,
    pair,
    pub,
    versionJson,
    darwinFacts,
    linuxFacts,
    comments,
    releaseJson: path.join(out, 'release.json'),
    sums: path.join(out, 'SHA256SUMS'),
  };
}

function manifestArgv(s, over = {}) {
  const flags = {
    '--version-json': s.versionJson,
    '--setup-iss': SETUP_ISS,
    '--assets': s.assets,
    '--darwin-facts': s.darwinFacts,
    '--linux-facts': s.linuxFacts,
    '--repo': REPO,
    '--pub-date': PUB_DATE,
    '--public-key': s.pub,
    '--out': s.releaseJson,
    '--sums-out': s.sums,
    ...over,
  };
  return Object.entries(flags).flat();
}

function flipByte(file, at) {
  const bytes = fs.readFileSync(file);
  bytes[at] ^= 0x01;
  fs.writeFileSync(file, bytes);
}

function expectFailure(r, code) {
  assert.equal(r.code, 1, r.stderr);
  assert.ok(r.stderr.startsWith(`${PREFIX}${code}:`), r.stderr);
  assert.equal(r.stdout, '');
}

const snapshot = (file) => ({ bytes: fs.readFileSync(file), mtimeMs: fs.statSync(file).mtimeMs });

function assertUnchanged(file, before) {
  const after = snapshot(file);
  assert.ok(after.bytes.equals(before.bytes), `${path.basename(file)} keeps its bytes`);
  assert.equal(after.mtimeMs, before.mtimeMs, `${path.basename(file)} keeps its mtimeMs`);
}

function assertNoLeftovers(dir) {
  const left = fs.readdirSync(dir).filter((n) => n.endsWith('.tmp') || n.endsWith('.bak'));
  assert.deepEqual(left, [], `no .tmp / .bak in ${dir}`);
}

// Pre-created outputs: release.json and SHA256SUMS with known bytes and an mtime in the past.
function precreateOutputs(s) {
  fs.writeFileSync(s.releaseJson, '{"old": "release"}\n');
  fs.writeFileSync(s.sums, 'old sums\n');
  const past = new Date('2026-01-02T03:04:05Z');
  fs.utimesSync(s.releaseJson, past, past);
  fs.utimesSync(s.sums, past, past);
  return { release: snapshot(s.releaseJson), sums: snapshot(s.sums) };
}

test('gen-release-manifest builds a verified release.json', async (t) => {
  await t.test('manifest mode writes a release.json and SHA256SUMS that verify', (t2) => {
    const s = setup(t2, 'grm-build');
    const r = run(manifestArgv(s));
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    assert.equal(r.stdout, '');

    const text = read(s.releaseJson);
    const doc = JSON.parse(text);
    assert.equal(text, JSON.stringify(doc, null, 2) + '\n');
    assert.deepEqual(Object.keys(doc), ['tag', 'name', 'version', 'pub_date', 'assets', 'install']);
    assert.equal(doc.tag, TAG);
    assert.equal(doc.name, 'KatPlugin');
    assert.equal(doc.version, '1.2.3');
    assert.equal(doc.pub_date, PUB_DATE);
    assert.deepEqual(Object.keys(doc.assets), ['darwin-universal', 'linux-x86_64', 'windows-x86_64']);

    const pubText = read(s.pub);
    const hashes = {};
    for (const [key, asset] of Object.entries(doc.assets)) {
      const file = path.join(s.assets, NAMES[key]);
      const bytes = fs.readFileSync(file);
      hashes[NAMES[key]] = sha256(bytes);
      const expectedKeys = ['name', 'url', 'size', 'sha256', 'signature', 'format'];
      if (key !== 'windows-x86_64') expectedKeys.push('min_os');
      if (key === 'linux-x86_64') expectedKeys.push('payload');
      assert.deepEqual(Object.keys(asset), expectedKeys, key);
      assert.equal(asset.name, NAMES[key]);
      assert.equal(asset.url, `https://github.com/${REPO}/releases/download/${TAG}/${NAMES[key]}`);
      assert.equal(asset.size, fs.statSync(file).size);
      assert.equal(asset.sha256, hashes[NAMES[key]]);
      assert.equal(asset.format, FORMATS[key]);
      assert.equal(asset.signature, read(`${file}.minisig`));
      const { trustedComment } = minisign.verifyBuffer(bytes, asset.signature, pubText);
      contract.checkBinding(trustedComment, { name: 'KatPlugin', version: '1.2.3', platform: key, asset: NAMES[key] });
    }
    assert.equal(doc.assets['darwin-universal'].min_os, '11.0');
    assert.equal(doc.assets['linux-x86_64'].min_os, '2.34');
    const committedLines = read(KAT_LISTING).split('\n').filter((l) => l !== '');
    assert.deepEqual(doc.assets['linux-x86_64'].payload, { files: 10, sha256: KAT_PAYLOAD_SHA, list: committedLines });
    assert.equal(committedLines.length, 10);
    contract.checkReleaseManifest(doc, { repo: REPO });

    assert.deepEqual(doc.install, {
      vst3_bundle: 'KatPlugin.vst3',
      au_component: 'KatPlugin.component',
      windows_uninstall_key: '{0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0}_is1',
      macos_pkg_id: 'com.krateaudio.katplugin',
    });

    const sorted = [LINUX, WIN, MAC]; // bytewise: 'L' < 'W' < 'm'
    assert.equal(read(s.sums), sorted.map((n) => `${hashes[n]}  ${n}\n`).join(''));
    assert.deepEqual(fs.readdirSync(s.out).sort(), ['SHA256SUMS', 'release.json']);
  });

  await t.test('preset_subdir and identifier come from version.json', (t2) => {
    const versionJsonText =
      '{"name": "KatPlugin", "version": "1.2.3", "preset_subdir": "Kits", "identifier": "com.krateaudio.katkit"}\n';
    const s = setup(t2, 'grm-preset', { versionJsonText });
    const r = run(manifestArgv(s));
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    const { install } = JSON.parse(read(s.releaseJson));
    assert.equal(install.preset_subdir, 'Kits');
    assert.equal(install.macos_pkg_id, 'com.krateaudio.katkit');
    assert.deepEqual(Object.keys(install), [
      'vst3_bundle',
      'au_component',
      'preset_subdir',
      'windows_uninstall_key',
      'macos_pkg_id',
    ]);
  });

  for (const [asset, code] of [
    [WIN, 'E_MINISIG_SIGNATURE'],
    [MAC, 'E_FACTS_STALE'],
    [LINUX, 'E_FACTS_STALE'],
  ]) {
    await t.test(`${path.extname(asset)} flipped after signing is ${code}, nothing written`, (t2) => {
      const s = setup(t2, 'grm-tamper');
      flipByte(path.join(s.assets, asset), 100);
      const r = run(manifestArgv(s));
      expectFailure(r, code);
      assert.deepEqual(fs.readdirSync(s.out), []);
    });
  }
});

test('gen-release-manifest refuses what it cannot bind', async (t) => {
  // The .exe's bytes signed with the .pkg's comment: cryptographically valid, bound to another asset.
  function signExeWithPkgComment(s) {
    const pkgComment = s.comments.find((c) => c.asset === MAC).comment;
    const exe = path.join(s.assets, WIN);
    fs.writeFileSync(`${exe}.minisig`, minisign.signFile(exe, s.pair, { trustedComment: pkgComment }));
  }

  await t.test('1: the .exe signed with the .pkg comment is E_TC_BINDING naming the asset', (t2) => {
    const s = setup(t2, 'grm-binding');
    signExeWithPkgComment(s);
    const r = run(manifestArgv(s));
    expectFailure(r, 'E_TC_BINDING');
    assert.ok(r.stderr.includes(`asset ${WIN}`), r.stderr);
    assert.deepEqual(fs.readdirSync(s.out), []);

    // A comment that differs from the expected one in the asset field alone: the binding names field `asset`.
    const wrongAsset = contract.formatTrustedComment('KatPlugin', '1.2.3', 'windows-x86_64', MAC);
    const exe = path.join(s.assets, WIN);
    fs.writeFileSync(`${exe}.minisig`, minisign.signFile(exe, s.pair, { trustedComment: wrongAsset }));
    assert.throws(
      () =>
        genRM.buildManifest({
          versionJsonText: read(VERSION_JSON),
          setupIssText: read(SETUP_ISS),
          assetsDir: s.assets,
          darwinFacts: JSON.parse(read(s.darwinFacts)),
          linuxFacts: JSON.parse(read(s.linuxFacts)),
          repo: REPO,
          pubDate: PUB_DATE,
          publicKeyText: read(s.pub),
        }),
      (err) => err instanceof contract.ContractError && err.code === 'E_TC_BINDING' && err.field === 'asset',
    );
  });

  await t.test("2: the .exe's .minisig copied as the .pkg's is E_MINISIG_SIGNATURE", (t2) => {
    const s = setup(t2, 'grm-swap');
    fs.copyFileSync(path.join(s.assets, `${WIN}.minisig`), path.join(s.assets, `${MAC}.minisig`));
    expectFailure(run(manifestArgv(s)), 'E_MINISIG_SIGNATURE');
    assert.deepEqual(fs.readdirSync(s.out), []);
  });

  await t.test('3: a missing .minisig is E_ASSET_MISSING naming it', (t2) => {
    const s = setup(t2, 'grm-missing');
    fs.rmSync(path.join(s.assets, `${LINUX}.minisig`));
    const r = run(manifestArgv(s));
    expectFailure(r, 'E_ASSET_MISSING');
    assert.ok(r.stderr.includes(`${LINUX}.minisig`), r.stderr);
    assert.deepEqual(fs.readdirSync(s.out), []);
  });

  await t.test('4: darwin facts for another file are E_FACTS_STALE', (t2) => {
    const s = setup(t2, 'grm-stale');
    const other = path.join(s.tmp, 'Other-1.2.3-macOS.pkg');
    fs.writeFileSync(other, crypto.randomBytes(2000));
    writeJson(s.darwinFacts, darwinFactsFor(other));
    expectFailure(run(manifestArgv(s)), 'E_FACTS_STALE');
    assert.deepEqual(fs.readdirSync(s.out), []);
  });

  await t.test('5: a setup.iss without AppId is E_INPUT', (t2) => {
    const s = setup(t2, 'grm-appid');
    const iss = path.join(s.tmp, 'setup.iss');
    fs.writeFileSync(iss, '[Setup]\nAppName=KatPlugin\n');
    const r = run(manifestArgv(s, { '--setup-iss': iss }));
    expectFailure(r, 'E_INPUT');
    assert.ok(r.stderr.includes('AppId'), r.stderr);
    assert.deepEqual(fs.readdirSync(s.out), []);
  });

  await t.test('6: a --public-key from another pair is E_MINISIG_KEY_ID', (t2) => {
    const s = setup(t2, 'grm-keyid');
    const otherPub = path.join(s.tmp, 'other.pub');
    fs.writeFileSync(otherPub, minisign.formatPublicKey(minisign.generateKeyPair()));
    expectFailure(run(manifestArgv(s, { '--public-key': otherPub })), 'E_MINISIG_KEY_ID');
    assert.deepEqual(fs.readdirSync(s.out), []);
  });

  await t.test('7: a refused run leaves pre-existing outputs and no temp file', (t2) => {
    const s = setup(t2, 'grm-keep');
    const before = precreateOutputs(s);
    signExeWithPkgComment(s);
    expectFailure(run(manifestArgv(s)), 'E_TC_BINDING');
    assertUnchanged(s.releaseJson, before.release);
    assertUnchanged(s.sums, before.sums);
    assertNoLeftovers(s.out);
    assert.deepEqual(fs.readdirSync(s.out).sort(), ['SHA256SUMS', 'release.json']);
  });

  for (const field of ['files', 'sha256']) {
    await t.test(`8: linux facts with payload.${field} changed are E_LISTING_DIGEST`, (t2) => {
      const s = setup(t2, 'grm-payload');
      const facts = JSON.parse(read(s.linuxFacts));
      facts.payload[field] = field === 'files' ? facts.payload.files + 1 : '0'.repeat(64);
      writeJson(s.linuxFacts, facts);
      expectFailure(run(manifestArgv(s)), 'E_LISTING_DIGEST');
      assert.deepEqual(fs.readdirSync(s.out), []);
    });
  }

  await t.test('9: --out replaced by a directory is E_INPUT, SHA256SUMS untouched', (t2) => {
    const s = setup(t2, 'grm-outdir');
    const before = precreateOutputs(s);
    fs.rmSync(s.releaseJson);
    fs.mkdirSync(s.releaseJson);
    expectFailure(run(manifestArgv(s)), 'E_INPUT');
    assertUnchanged(s.sums, before.sums);
    assert.ok(fs.statSync(s.releaseJson).isDirectory());
    assert.deepEqual(fs.readdirSync(s.releaseJson), []);
    assertNoLeftovers(s.out);
    assert.deepEqual(fs.readdirSync(s.out).sort(), ['SHA256SUMS', 'release.json']);
  });

  await t.test('10: a failed rename onto release.json restores both outputs', (t2) => {
    const s = setup(t2, 'grm-rename');
    const before = precreateOutputs(s);
    const original = fs.renameSync;
    let refused = 0;
    fs.renameSync = function renameSync(from, to, ...rest) {
      if (path.resolve(String(to)) === path.resolve(s.releaseJson)) {
        refused += 1;
        const err = new Error('injected rename failure');
        err.code = 'EIO';
        throw err;
      }
      return original.call(this, from, to, ...rest);
    };
    let r;
    try {
      r = run(manifestArgv(s));
    } finally {
      fs.renameSync = original;
    }
    assert.equal(refused, 1, 'the rename onto release.json was attempted once');
    expectFailure(r, 'E_INPUT');
    assertUnchanged(s.releaseJson, before.release);
    assertUnchanged(s.sums, before.sums);
    assertNoLeftovers(s.out);
    assert.deepEqual(fs.readdirSync(s.out).sort(), ['SHA256SUMS', 'release.json']);
  });
});

test('gen-release-manifest prints the C3 comments', async (t) => {
  await t.test('three lines in bytewise key order, names only', (t2) => {
    const tmp = withTemp(t2, 'grm-tc');
    const assets = path.join(tmp, 'assets'); // empty: the mode reads names only, no asset or .minisig
    fs.mkdirSync(assets);
    const darwinFacts = path.join(tmp, 'darwin-facts.json');
    writeJson(darwinFacts, {
      asset: MAC,
      size: 2000,
      sha256: '0'.repeat(64),
      platform_key: 'darwin-universal',
      min_os: '11.0',
      archs: ['arm64', 'x86_64'],
      macho: [],
    });
    const r = run([
      '--trusted-comments',
      '--version-json',
      VERSION_JSON,
      '--assets',
      assets,
      '--darwin-facts',
      darwinFacts,
    ]);
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    const lines = r.stdout.split('\n');
    assert.equal(lines.pop(), '', 'final LF');
    assert.equal(lines.length, 3);
    const keys = ['darwin-universal', 'linux-x86_64', 'windows-x86_64'];
    lines.forEach((line, i) => {
      const [asset, comment, ...rest] = line.split('\t');
      assert.deepEqual(rest, []);
      assert.equal(asset, NAMES[keys[i]]);
      assert.equal(comment, `krate:1:KatPlugin:1.2.3:${keys[i]}:${NAMES[keys[i]]}`);
      assert.deepEqual(contract.parseTrustedComment(comment), {
        name: 'KatPlugin',
        version: '1.2.3',
        platform: keys[i],
        asset: NAMES[keys[i]],
      });
    });
  });
});

test('signing configuration is throwaway, on or a failure', async (t) => {
  const tmp = withTemp(t, 'grm-signing');
  const SECRET = 'km2-secret-value-5e1f0c';
  const PASSWORD = 'km2-password-value-a93d27';
  const valid = path.join(tmp, 'krate-signing.pub');
  fs.writeFileSync(valid, minisign.formatPublicKey(minisign.generateKeyPair()));
  const absent = path.join(tmp, 'absent.pub');
  const malformed = path.join(tmp, 'malformed.pub');
  const malformedText = 'untrusted comment: minisign public key: 0000000000000000\nnot base64!\n';
  fs.writeFileSync(malformed, malformedText);
  const katCopy = path.join(tmp, 'kat-copy.pub');
  fs.copyFileSync(KAT_PUB, katCopy);
  const ksCopy = path.join(tmp, 'kat-secret-copy.pub');
  fs.copyFileSync(KS_PUB, ksCopy);

  const env = ({ key = true, password = true } = {}) => ({
    ...(key === true ? { KRATE_SIGNING_KEY: SECRET } : key === '' ? { KRATE_SIGNING_KEY: '' } : {}),
    ...(password === true
      ? { KRATE_SIGNING_KEY_PASSWORD: PASSWORD }
      : password === ''
        ? { KRATE_SIGNING_KEY_PASSWORD: '' }
        : {}),
  });
  const runConfig = (dryRun, keyFile, e) => {
    const r = run(['--signing-config', '--dry-run', String(dryRun), '--public-key', keyFile], e);
    for (const stream of [r.stdout, r.stderr]) {
      assert.ok(!stream.includes(SECRET), 'the key secret never appears');
      assert.ok(!stream.includes(PASSWORD), 'the password never appears');
    }
    return r;
  };
  const expectConfigFailure = (r, { has = [], lacks = [] }) => {
    expectFailure(r, 'E_SIGNING_CONFIG');
    for (const s of has) assert.ok(r.stderr.includes(s), `names ${s}: ${r.stderr}`);
    for (const s of lacks) assert.ok(!r.stderr.includes(s), `does not name ${s}: ${r.stderr}`);
    assert.ok(r.stderr.includes('tools/feed-contract/keys/README.md'), r.stderr);
  };

  await t.test('dry runs are throwaway whatever is configured', () => {
    for (const [e, keyFile] of [
      [env(), valid],
      [env({ key: false, password: false }), absent],
      [env({ password: false }), absent],
      [env({ key: false }), valid],
    ]) {
      const r = runConfig(true, keyFile, e);
      assert.equal(r.stderr, '');
      assert.equal(r.code, 0);
      assert.equal(r.stdout, 'signing=throwaway\n');
    }
  });

  await t.test('a real run with both secrets and a valid key is on', () => {
    const r = runConfig(false, valid, env());
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    assert.equal(r.stdout, 'signing=on\n');
  });

  await t.test('key file absent names public key absent', () => {
    expectConfigFailure(runConfig(false, absent, env()), { has: ['public key absent'], lacks: ['secret', 'password'] });
  });

  await t.test('key secret empty names secret', () => {
    expectConfigFailure(runConfig(false, valid, env({ key: '' })), {
      has: ['secret'],
      lacks: ['password', 'public key'],
    });
  });

  await t.test('password empty names password', () => {
    expectConfigFailure(runConfig(false, valid, env({ password: '' })), {
      has: ['password'],
      lacks: ['secret', 'public key'],
    });
  });

  await t.test('password absent names password', () => {
    expectConfigFailure(runConfig(false, valid, env({ password: false })), {
      has: ['password'],
      lacks: ['secret', 'public key'],
    });
  });

  await t.test('everything missing names all three', () => {
    const r = runConfig(false, absent, env({ key: false, password: false }));
    expectConfigFailure(r, { has: ['secret', 'password', 'public key absent'] });
    const at = (s) => r.stderr.indexOf(s);
    assert.ok(at('secret') < at('password') && at('password') < at('public key absent'), r.stderr);
  });

  await t.test('a malformed key file carries the E_MINISIG_PUBKEY message', () => {
    let pubkeyMessage;
    assert.throws(
      () => minisign.parsePublicKey(malformedText),
      (err) => {
        pubkeyMessage = err.message;
        return err.code === 'E_MINISIG_PUBKEY';
      },
    );
    expectConfigFailure(runConfig(false, malformed, env()), { has: [pubkeyMessage], lacks: ['secret', 'password'] });
  });

  await t.test('a test public key is refused in a real run and stays throwaway in a dry run', () => {
    for (const [copy, fixture] of [
      [katCopy, 'fixtures/kat/kat.pub'],
      [ksCopy, 'fixtures/phase2/kat-secret/kat.pub'],
    ]) {
      // `lacks` names the variables, not the words: the second fixture's path itself holds 'secret'.
      expectConfigFailure(runConfig(false, copy, env()), { has: ['test key', fixture], lacks: ['KRATE_SIGNING_KEY'] });
      const dry = runConfig(true, copy, env());
      assert.equal(dry.code, 0);
      assert.equal(dry.stdout, 'signing=throwaway\n');
    }
  });

  await t.test('refuseTestKey compares the 32-byte key as well as the key id, and fails closed', () => {
    const tests = [read(KAT_PUB), read(KS_PUB)];
    // The KAT key relabelled with a fresh key id: same Ed25519 key, different id.
    const kat = minisign.parsePublicKey(read(KAT_PUB));
    const relabelled = minisign.formatPublicKey({ keyId: crypto.randomBytes(8), publicKey: kat.publicKey });
    const isConfig = (err) => err instanceof contract.ContractError && err.code === 'E_SIGNING_CONFIG';
    assert.throws(() => genRM.refuseTestKey(relabelled, tests), isConfig);
    // The KAT key id over a fresh key.
    const fresh = minisign.generateKeyPair();
    const reused = minisign.formatPublicKey({ keyId: kat.keyId, publicKey: fresh.publicKey });
    assert.throws(() => genRM.refuseTestKey(reused, tests), isConfig);
    // A production-like key passes; an unreadable (null) or unparsable test key refuses it (fail closed).
    const good = read(valid);
    genRM.refuseTestKey(good, tests);
    assert.throws(() => genRM.refuseTestKey(good, [null, tests[1]]), isConfig);
    assert.throws(() => genRM.refuseTestKey(good, [tests[0], 'not a key']), isConfig);
  });

  await t.test('a --dry-run other than true or false is a usage error', () => {
    const r = run(['--signing-config', '--dry-run', 'yes', '--public-key', valid], env());
    assert.equal(r.code, 2);
    assert.ok(r.stderr.includes('usage: node tools/gen-release-manifest.js'), r.stderr);
  });
});
