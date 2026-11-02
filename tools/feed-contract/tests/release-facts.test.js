'use strict';
require('./helpers/write-guard.js');

// tools/release-facts.js (spec FR-010 to FR-016, SC-004, SC-005, SC-008, SC-024; plan §4.2, PD-4, PD-26, §7.1).
// Mach-O facts run on synthetic buffers from helpers/macho.js, so every OS proves them. The glibc floor reads the
// committed objdump -T samples. The Linux half: linux-precheck runs on every OS; listing an extracted tree needs POSIX
// modes (Phase 1 D4), so those subtests are skipped as subtests on win32 and are PENDING-CI on feed-tools.yml
// node-test (ubuntu-latest | macos-latest). Subtest names are the same on every OS.
// T011 adds the win32 half of SC-008 (the manifest payload vs the committed .listing) to the last test.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

const macho = require('./helpers/macho.js');
const externalTools = require('./helpers/external-tools.js');
const { ContractError } = require('../contract.js');
const listing = require('../listing.js');
const minisign = require('../../minisign.js');
const genReleaseManifest = require('../../gen-release-manifest.js');
const facts = require('../../release-facts.js');

const FIXTURES = path.join(__dirname, '..', 'fixtures');
const OBJDUMP_DIR = path.join(FIXTURES, 'phase2', 'objdump');
const KAT_TARBALL = path.join(FIXTURES, 'kat', 'KatPlugin-1.2.3-Linux-x64.tar.gz');
const KAT_PLUGIN = path.join(FIXTURES, 'phase2', 'KatPlugin');
const BAD_DIR = path.join(FIXTURES, 'listings', 'bad');
const IS_WIN = process.platform === 'win32';
const POSIX_ONLY = IS_WIN ? 'Phase 1 D4: listingFromDirectory needs POSIX modes' : false;
const USAGE_PREFIX = 'usage: node tools/release-facts.js';

const { thin, fat, buildVersion, versionMin } = macho;
const arm64 = (minos, extra = {}) =>
  thin({ cputype: macho.CPU_TYPE_ARM64, commands: [buildVersion(minos)], ...extra });
const x86 = (version) =>
  thin({ cputype: macho.CPU_TYPE_X86_64, cpusubtype: macho.CPU_SUBTYPE_X86_64_ALL, commands: [versionMin(version)] });
// fat x86_64 (LC_VERSION_MIN_MACOSX 10.13) + arm64 (LC_BUILD_VERSION 11.0), the SC-004 / SC-024 universal file
const universal = (opts) => fat([x86(0x000a0d00), arm64(0x000b0000)], opts);

function tempDir(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km2-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

function put(root, rel, data) {
  const file = path.join(root, ...rel.split('/'));
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, data);
  return file;
}

function throwsCode(fn, code) {
  assert.throws(fn, (err) => {
    assert.ok(err instanceof ContractError, `expected a ContractError, got ${err && err.stack}`);
    assert.equal(err.code, code, err.message);
    return true;
  });
}

function run(argv) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = facts.main(argv, io);
  return { code, stdout, stderr };
}

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');

test('release-facts reads Mach-O arch sets and minimum macOS', async (t) => {
  await t.test('thin arm64 with LC_BUILD_VERSION 15.0', () => {
    assert.deepEqual(facts.machoFacts(arm64(0x000f0000)), {
      archs: ['arm64'],
      minOs: '15.0',
      slices: [{ arch: 'arm64', minOs: '15.0' }],
    });
  });

  await t.test('arm64 subtype V8 is arm64', () => {
    const buf = arm64(0x000f0000, { cpusubtype: macho.CPU_SUBTYPE_ARM64_V8 });
    assert.deepEqual(facts.machoFacts(buf).archs, ['arm64']);
  });

  for (const wide of [false, true]) {
    await t.test(`fat x86_64 + arm64 (${wide ? 'fat_arch_64' : 'fat_arch'})`, () => {
      assert.deepEqual(facts.machoFacts(universal({ wide })), {
        archs: ['arm64', 'x86_64'],
        minOs: '11.0',
        slices: [
          { arch: 'x86_64', minOs: '10.13' },
          { arch: 'arm64', minOs: '11.0' },
        ],
      });
    });
  }

  await t.test('minos 0x000F0601 reads 15.6.1', () => {
    assert.equal(facts.machoFacts(arm64(0x000f0601)).minOs, '15.6.1');
  });

  await t.test('LC_BUILD_VERSION for macOS wins over an earlier LC_VERSION_MIN_MACOSX', () => {
    const buf = thin({ cputype: macho.CPU_TYPE_ARM64, commands: [versionMin(0x000a0f00), buildVersion(0x000c0000)] });
    assert.equal(facts.machoFacts(buf).minOs, '12.0');
  });

  await t.test('arm64 subtype 2 (arm64e) is E_FACTS_ARCH', () => {
    throwsCode(() => facts.machoFacts(arm64(0x000f0000, { cpusubtype: macho.CPU_SUBTYPE_ARM64E })), 'E_FACTS_ARCH');
    const abiFlag = (macho.CPU_SUBTYPE_ARM64E | 0x80000000) >>> 0;
    throwsCode(() => facts.machoFacts(arm64(0x000f0000, { cpusubtype: abiFlag })), 'E_FACTS_ARCH');
  });

  await t.test('a thin big-endian 64-bit file (0xcffaedfe) is E_FACTS_ARCH', () => {
    throwsCode(() => facts.machoFacts(arm64(0x000f0000, { magic: macho.MH_CIGAM_64 })), 'E_FACTS_ARCH');
  });

  await t.test('a thin 32-bit file (0xfeedface, 0xcefaedfe) is E_FACTS_ARCH', () => {
    for (const magic of [macho.MH_MAGIC, macho.MH_CIGAM]) {
      const buf = thin({ cputype: macho.CPU_TYPE_I386, cpusubtype: macho.CPU_SUBTYPE_I386_ALL, magic });
      throwsCode(() => facts.machoFacts(buf), 'E_FACTS_ARCH');
    }
  });

  await t.test('a fat file with an i386 slice is E_FACTS_ARCH', () => {
    const i386 = thin({
      cputype: macho.CPU_TYPE_I386,
      cpusubtype: macho.CPU_SUBTYPE_I386_ALL,
      magic: macho.MH_MAGIC,
      commands: [versionMin(0x000a0d00)],
    });
    throwsCode(() => facts.machoFacts(fat([i386, arm64(0x000b0000)])), 'E_FACTS_ARCH');
  });

  await t.test('no version command is E_FACTS_NO_MINOS', () => {
    throwsCode(() => facts.machoFacts(thin({ cputype: macho.CPU_TYPE_ARM64 })), 'E_FACTS_NO_MINOS');
    const ios = thin({ cputype: macho.CPU_TYPE_ARM64, commands: [buildVersion(0x000f0000, macho.PLATFORM_IOS)] });
    throwsCode(() => facts.machoFacts(ios), 'E_FACTS_NO_MINOS');
  });

  await t.test('a command past the end is E_FACTS_MACHO_CORRUPT', () => {
    const inflated = arm64(0x000f0000);
    inflated.writeUInt32LE(4096, 32 + 4); // the first command's cmdsize
    throwsCode(() => facts.machoFacts(inflated), 'E_FACTS_MACHO_CORRUPT');
    const truncated = arm64(0x000f0000).subarray(0, 32 + 16);
    throwsCode(() => facts.machoFacts(truncated), 'E_FACTS_MACHO_CORRUPT');
  });

  await t.test('cmdsize below 8 and nfat_arch 0 are E_FACTS_MACHO_CORRUPT', () => {
    const small = arm64(0x000f0000);
    small.writeUInt32LE(4, 32 + 4);
    throwsCode(() => facts.machoFacts(small), 'E_FACTS_MACHO_CORRUPT');
    const empty = Buffer.alloc(8);
    empty.writeUInt32BE(macho.FAT_MAGIC, 0);
    throwsCode(() => facts.machoFacts(empty), 'E_FACTS_MACHO_CORRUPT');
  });

  await t.test('a tree with the fat file and a thin arm64 file is E_FACTS_ARCH_MISMATCH', (t2) => {
    const root = tempDir(t2, 'macho-mismatch');
    put(root, 'a/K', universal());
    put(root, 'b/K', arm64(0x000b0000));
    throwsCode(() => facts.darwinFactsFromTree(root), 'E_FACTS_ARCH_MISMATCH');
  });

  await t.test('a tree of two fat files is darwin-universal, min_os 11.0', (t2) => {
    const root = tempDir(t2, 'macho-two-fat');
    put(root, 'b/Contents/MacOS/K', universal({ wide: true }));
    put(root, 'a/Contents/MacOS/K', universal());
    put(root, 'a/Contents/Info.plist', '<plist/>\n');
    assert.deepEqual(facts.darwinFactsFromTree(root), {
      platform_key: 'darwin-universal',
      min_os: '11.0',
      archs: ['arm64', 'x86_64'],
      macho: [
        { path: 'a/Contents/MacOS/K', archs: ['arm64', 'x86_64'], min_os: '11.0' },
        { path: 'b/Contents/MacOS/K', archs: ['arm64', 'x86_64'], min_os: '11.0' },
      ],
    });
  });

  await t.test('a thin 32-bit file beside a valid arm64 file is E_FACTS_ARCH, not a skip', (t2) => {
    const root = tempDir(t2, 'macho-32bit');
    put(root, 'a/K', arm64(0x000b0000));
    put(root, 'b/K', thin({ cputype: macho.CPU_TYPE_I386, cpusubtype: 3, magic: macho.MH_MAGIC }));
    throwsCode(() => facts.darwinFactsFromTree(root), 'E_FACTS_ARCH');
  });

  await t.test('a tree of plain files is E_FACTS_NO_MACHO', (t2) => {
    const root = tempDir(t2, 'macho-none');
    put(root, 'Contents/Info.plist', '<plist/>\n');
    put(root, 'Contents/Resources/moduleinfo.json', '{}\n');
    put(root, 'empty', '');
    throwsCode(() => facts.darwinFactsFromTree(root), 'E_FACTS_NO_MACHO');
  });

  await t.test('platformKeyForArchs maps the three sets', () => {
    assert.equal(facts.platformKeyForArchs(['arm64', 'x86_64']), 'darwin-universal');
    assert.equal(facts.platformKeyForArchs(['arm64']), 'darwin-aarch64');
    assert.equal(facts.platformKeyForArchs(['x86_64']), 'darwin-x86_64');
    throwsCode(() => facts.platformKeyForArchs([]), 'E_FACTS_ARCH');
  });

  const symlinkSkip = IS_WIN ? 'win32: fs.symlinkSync needs a privilege this leg does not have' : false;
  await t.test('a symlink to a Mach-O inside the tree is not counted', { skip: symlinkSkip }, (t2) => {
    const root = tempDir(t2, 'macho-link');
    put(root, 'a/K', universal());
    fs.mkdirSync(path.join(root, 'c'));
    fs.symlinkSync(path.join('..', 'a', 'K'), path.join(root, 'c', 'K'));
    fs.symlinkSync(path.join(root, 'a'), path.join(root, 'd'));
    assert.deepEqual(
      facts.darwinFactsFromTree(root).macho.map((m) => m.path),
      ['a/K'],
    );
  });
});

test('release-facts reads the glibc floor', async (t) => {
  const sample = (name) => fs.readFileSync(path.join(OBJDUMP_DIR, name), 'utf8');

  await t.test('GLIBC_2.2.5, 2.17, 2.34 and GLIBC_PRIVATE give 2.34', () => {
    assert.equal(facts.glibcFloor(sample('glibc-mixed.txt')), '2.34');
  });

  await t.test('GLIBC_2.9 and GLIBC_2.10 give 2.10 (numeric, not lexical)', () => {
    assert.equal(facts.glibcFloor(sample('glibc-numeric.txt')), '2.10');
  });

  await t.test('no GLIBC_ token is E_FACTS_NO_GLIBC', () => {
    throwsCode(() => facts.glibcFloor(sample('no-glibc.txt')), 'E_FACTS_NO_GLIBC');
  });
});

test('release-facts darwin CLI writes facts and lipo lines', async (t) => {
  const VST3 = 'Payload/Library/Audio/Plug-Ins/VST3/K.vst3/Contents/MacOS/K';
  const COMPONENT = 'Payload/Library/Audio/Plug-Ins/Components/K.component/Contents/MacOS/K';

  await t.test('two fat files: exact facts JSON and two lipo lines', (t2) => {
    const tmp = tempDir(t2, 'facts-darwin');
    const expanded = path.join(tmp, 'expanded');
    put(expanded, VST3, universal());
    put(expanded, COMPONENT, universal({ wide: true }));
    put(expanded, 'Payload/Library/Audio/Plug-Ins/VST3/K.vst3/Contents/Info.plist', '<plist/>\n');
    const pkgBytes = crypto.randomBytes(2000);
    const pkg = put(tmp, 'K-1.0.0-macOS.pkg', pkgBytes);
    const out = path.join(tmp, 'darwin.json');
    const r = run(['darwin', '--pkg', pkg, '--expanded', expanded, '--out', out]);
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    const archs = ['arm64', 'x86_64'];
    const expected = {
      asset: 'K-1.0.0-macOS.pkg',
      size: 2000,
      sha256: sha256(pkgBytes),
      platform_key: 'darwin-universal',
      min_os: '11.0',
      archs,
      macho: [
        { path: COMPONENT, archs, min_os: '11.0' },
        { path: VST3, archs, min_os: '11.0' },
      ],
    };
    assert.equal(fs.readFileSync(out, 'utf8'), JSON.stringify(expected, null, 2) + '\n');
    assert.equal(r.stdout, `arm64 x86_64\t${COMPONENT}\narm64 x86_64\t${VST3}\n`);
  });

  await t.test('an unknown mode exits 2 with the usage line', () => {
    const r = run(['macos', '--pkg', 'x', '--expanded', 'y', '--out', 'z']);
    assert.equal(r.code, 2);
    assert.ok(
      r.stderr.split('\n').some((line) => line.startsWith(USAGE_PREFIX)),
      `stderr: ${JSON.stringify(r.stderr)}`,
    );
    assert.equal(r.stdout, '');
  });

  await t.test('a tree without a Mach-O exits 1 and writes no --out', (t2) => {
    const tmp = tempDir(t2, 'facts-darwin-none');
    const expanded = path.join(tmp, 'expanded');
    put(expanded, 'Payload/Library/Audio/Plug-Ins/VST3/K.vst3/Contents/Info.plist', '<plist/>\n');
    const pkg = put(tmp, 'K-1.0.0-macOS.pkg', crypto.randomBytes(2000));
    const out = path.join(tmp, 'darwin.json');
    const r = run(['darwin', '--pkg', pkg, '--expanded', expanded, '--out', out]);
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('release-facts: E_FACTS_NO_MACHO:'), r.stderr);
    assert.equal(r.stdout, '');
    assert.equal(fs.existsSync(out), false);
    assert.deepEqual(fs.readdirSync(tmp).sort(), ['K-1.0.0-macOS.pkg', 'expanded']);
  });
});

test("Linux payload digest equals the extracted tree's listing", async (t) => {
  const KAT_PAYLOAD = { files: 10, sha256: 'db287e4955f6d98ab50273f65947f12512b1d5d5cefc6c4e59b3207f37b47ad6' };

  const copyIn = (tmp, src) => {
    const dest = path.join(tmp, path.basename(src));
    fs.copyFileSync(src, dest);
    return dest;
  };

  await t.test('linux-precheck accepts the KAT tarball silently', (t2) => {
    const tmp = tempDir(t2, 'precheck-kat');
    const r = run(['linux-precheck', '--tarball', copyIn(tmp, KAT_TARBALL)]);
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    assert.equal(r.stdout, '');
  });

  for (const name of ['dotdot-path', 'symlink-escape-root', 'layout-missing-presets']) {
    await t.test(`linux-precheck rejects ${name}`, (t2) => {
      const tmp = tempDir(t2, 'precheck-bad');
      const expectedCode = fs.readFileSync(path.join(BAD_DIR, `${name}.expected-error`), 'utf8').trim();
      const r = run(['linux-precheck', '--tarball', copyIn(tmp, path.join(BAD_DIR, `${name}.tar.gz`))]);
      assert.equal(r.code, 1);
      assert.ok(r.stderr.startsWith(`release-facts: ${expectedCode}:`), r.stderr);
      assert.equal(r.stdout, '');
    });
  }

  // The extracted-tree half: tar -xzf through helpers/external-tools.js, then the real listingFromDirectory.
  function extractKat(tmp) {
    const tarball = copyIn(tmp, KAT_TARBALL);
    const extracted = path.join(tmp, 'extracted');
    fs.mkdirSync(extracted);
    externalTools.extractTarGz(tarball, extracted);
    const objdump = copyIn(tmp, path.join(OBJDUMP_DIR, 'glibc-mixed.txt'));
    return { tarball, extracted, objdump, out: path.join(tmp, 'linux.json') };
  }

  await t.test('linux CLI on the extracted KAT writes the payload', { skip: POSIX_ONLY }, (t2) => {
    const tmp = tempDir(t2, 'facts-linux');
    const { tarball, extracted, objdump, out } = extractKat(tmp);
    const r = run(['linux', '--tarball', tarball, '--extracted', extracted, '--objdump', objdump, '--out', out]);
    assert.equal(r.stderr, '');
    assert.equal(r.code, 0);
    assert.equal(r.stdout, '');
    const bytes = fs.readFileSync(tarball);
    const expected = {
      asset: 'KatPlugin-1.2.3-Linux-x64.tar.gz',
      size: bytes.length,
      sha256: sha256(bytes),
      min_os: '2.34',
      payload: KAT_PAYLOAD,
    };
    assert.equal(fs.readFileSync(out, 'utf8'), JSON.stringify(expected, null, 2) + '\n');
  });

  await t.test('an extracted tree with one extra file is E_FACTS_LISTING_MISMATCH', { skip: POSIX_ONLY }, (t2) => {
    const tmp = tempDir(t2, 'facts-linux-extra');
    const { tarball, extracted, objdump, out } = extractKat(tmp);
    fs.writeFileSync(path.join(extracted, 'presets', 'Extra.preset'), 'extra\n', { mode: 0o644 });
    const r = run(['linux', '--tarball', tarball, '--extracted', extracted, '--objdump', objdump, '--out', out]);
    assert.equal(r.code, 1);
    assert.ok(r.stderr.startsWith('release-facts: E_FACTS_LISTING_MISMATCH:'), r.stderr);
    assert.equal(fs.existsSync(out), false);
  });

  // SC-008 on every OS (T011): the manifest's payload against the COMMITTED .listing, not FR-023's own computation.
  await t.test('the manifest payload equals the committed KAT listing', (t2) => {
    const tmp = tempDir(t2, 'facts-manifest');
    const assets = path.join(tmp, 'assets');
    fs.mkdirSync(assets);
    const WIN = 'KatPlugin-1.2.3-Windows-x64.exe';
    const MAC = 'KatPlugin-1.2.3-macOS.pkg';
    const LINUX = 'KatPlugin-1.2.3-Linux-x64.tar.gz';
    fs.writeFileSync(path.join(assets, WIN), crypto.randomBytes(1000));
    fs.writeFileSync(path.join(assets, MAC), crypto.randomBytes(2000));
    fs.copyFileSync(KAT_TARBALL, path.join(assets, LINUX));

    const versionJsonText = fs.readFileSync(path.join(KAT_PLUGIN, 'version.json'), 'utf8');
    const macBytes = fs.readFileSync(path.join(assets, MAC));
    const darwinFacts = {
      asset: MAC,
      size: macBytes.length,
      sha256: sha256(macBytes),
      platform_key: 'darwin-universal',
      min_os: '11.0',
      archs: ['arm64', 'x86_64'],
      macho: [],
    };
    const linuxFacts = facts.linuxFacts(
      {
        tarball: path.join(assets, LINUX),
        extracted: tmp,
        objdumpText: fs.readFileSync(path.join(OBJDUMP_DIR, 'glibc-mixed.txt'), 'utf8'),
      },
      { listDirectory: () => listing.listingFromTarGz(fs.readFileSync(path.join(assets, LINUX))) },
    );

    const pair = minisign.generateKeyPair();
    for (const c of genReleaseManifest.trustedComments({ versionJsonText, assetsDir: assets, darwinFacts })) {
      const sig = minisign.signFile(path.join(assets, c.asset), pair, { trustedComment: c.comment });
      fs.writeFileSync(path.join(assets, `${c.asset}.minisig`), sig);
    }

    const { doc } = genReleaseManifest.buildManifest({
      versionJsonText,
      setupIssText: fs.readFileSync(path.join(KAT_PLUGIN, 'installers', 'windows', 'setup.iss'), 'utf8'),
      assetsDir: assets,
      darwinFacts,
      linuxFacts,
      repo: 'krate-test/krate-audio',
      pubDate: '2026-10-30T12:00:00Z',
      publicKeyText: minisign.formatPublicKey(pair),
    });

    const committed = fs
      .readFileSync(`${KAT_TARBALL}.listing`, 'utf8')
      .split('\n')
      .filter((l) => l !== '');
    const payload = doc.assets['linux-x86_64'].payload;
    assert.equal(payload.files, 10);
    assert.equal(payload.sha256, listing.digest(committed));
    assert.deepEqual(payload.list, committed);
  });
});
