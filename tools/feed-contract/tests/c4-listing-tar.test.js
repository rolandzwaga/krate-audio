'use strict';
require('./helpers/write-guard.js');

// `C4 listings from tar headers` (spec SC-008; FR-030 to FR-033, FR-058; plan §4.3, §7.1, PD-12, PD-18).
// Reads the committed archives under fixtures/listings/ (never extracts them) and checks:
//   - every good archive lists to its committed .listing byte for byte, and digests to its .sha256;
//   - the special-record fixtures really carry the record they exist for (GNU L / K, pax x / g, ustar prefix);
//   - every bad archive fails with its .expected-error code (E_LAYOUT ones parse, then fail checkLinuxLayout);
//   - the FR-031 rules that need no archive, through listingFromEntries directly;
//   - each FR-033 layout item, by removing or adding it in real-shaped's lines.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');

const { ContractError } = require('../contract.js');
const {
  listingFromEntries,
  listingFromTarGz,
  digest,
  toText,
  parseListingLine,
  checkLinuxLayout,
  readTarEntries,
} = require('../listing.js');

const LISTINGS = path.join(__dirname, '..', 'fixtures', 'listings');
const GOOD_DIR = path.join(LISTINGS, 'good');
const BAD_DIR = path.join(LISTINGS, 'bad');
const BUNDLE = 'Alpha.vst3';

const GOOD = [
  'real-shaped',
  'exec-and-symlink',
  'long-name',
  'long-link',
  'pax-path',
  'pax-global',
  'implicit-dirs',
  'dot-slash',
  'ustar-prefix',
];

// name -> expected code; `item` is what an E_LAYOUT message must name.
const BAD = {
  'absolute-path': { code: 'E_LISTING_ABSOLUTE' },
  'dotdot-path': { code: 'E_LISTING_DOTDOT' },
  'drive-path': { code: 'E_LISTING_DRIVE' },
  'backslash-path': { code: 'E_LISTING_BACKSLASH' },
  'symlink-escape-root': { code: 'E_LISTING_SYMLINK_ESCAPE' },
  'symlink-escape-bundle': { code: 'E_LISTING_SYMLINK_ESCAPE' },
  'root-symlink': { code: 'E_LISTING_ROOT_SYMLINK' },
  'hard-link': { code: 'E_LISTING_TYPE' },
  'char-device': { code: 'E_LISTING_TYPE' },
  'duplicate-entry': { code: 'E_LISTING_DUPLICATE' },
  'file-dir-clash': { code: 'E_LISTING_DUPLICATE' },
  truncated: { code: 'E_LISTING_TRUNCATED' },
  'no-end-block': { code: 'E_LISTING_TRUNCATED' },
  'bad-checksum': { code: 'E_LISTING_CHECKSUM' },
  'pax-malformed': { code: 'E_LISTING_CORRUPT' },
  'base256-size': { code: 'E_LISTING_CORRUPT' },
  'symlink-dotdot-chain': { code: 'E_LISTING_SYMLINK_FORM' },
  'symlink-chain': { code: 'E_LISTING_SYMLINK_CHAIN' },
  'symlink-self': { code: 'E_LISTING_SYMLINK_CHAIN' },
  'through-symlink': { code: 'E_LISTING_THROUGH_SYMLINK' },
  'layout-extra-root-file': { code: 'E_LAYOUT', item: 'NOTES.txt' },
  'layout-missing-presets': { code: 'E_LAYOUT', item: 'presets' },
};

const readGood = (name, ext) => fs.readFileSync(path.join(GOOD_DIR, `${name}${ext}`));
const tarOf = (name) => zlib.gunzipSync(readGood(name, '.tar.gz'));
const rawOf = (name) => readTarEntries(tarOf(name), { raw: true });
const cstr = (buf) => {
  const nul = buf.indexOf(0);
  return buf.subarray(0, nul < 0 ? buf.length : nul).toString('utf8');
};

function expectCode(fn, code, label, mustName) {
  assert.throws(
    fn,
    (err) => {
      assert.ok(err instanceof ContractError, `${label}: expected a ContractError, got ${err && err.stack}`);
      assert.equal(err.code, code, `${label}: ${err.message}`);
      if (mustName !== undefined) assert.ok(err.message.includes(mustName), `${label}: message must name ${mustName}`);
      return true;
    },
    label,
  );
}

function firstDifference(actual, expected) {
  const a = actual.split('\n');
  const e = expected.split('\n');
  for (let i = 0; i < Math.max(a.length, e.length); i++) {
    if (a[i] !== e[i]) return `line ${i + 1}: got ${JSON.stringify(a[i])}, expected ${JSON.stringify(e[i])}`;
  }
  return 'no differing line (length or terminator differs)';
}

// Entry builders for the in-memory table.
const f = (p, mode = 0o644) => ({ type: 'f', mode, path: p });
const d = (p, mode = 0o755) => ({ type: 'd', mode, path: p });
const l = (p, target) => ({ type: 'l', mode: 0o777, path: p, target });

test('C4 listings from tar headers', async (t) => {
  for (const name of GOOD) {
    await t.test(`good archive ${name}: listing and digest equal the committed sidecars`, () => {
      const lines = listingFromTarGz(readGood(name, '.tar.gz'));
      const expected = readGood(name, '.listing').toString('utf8');
      const actual = toText(lines);
      assert.ok(actual === expected, `${name}.listing: ${firstDifference(actual, expected)}`);
      assert.equal(digest(lines), readGood(name, '.sha256').toString('utf8').trim(), `${name}.sha256`);
      assert.match(digest(lines), /^[0-9a-f]{64}$/);
    });
  }

  await t.test('record presence: long-name carries a GNU L record', () => {
    assert.ok(rawOf('long-name').records.some((r) => r.typeflag === 'L'), 'no typeflag L header');
  });

  await t.test('record presence: long-link carries a GNU K record', () => {
    assert.ok(rawOf('long-link').records.some((r) => r.typeflag === 'K'), 'no typeflag K header');
  });

  await t.test('record presence: pax-path carries an x record holding both path and linkpath', () => {
    const xs = rawOf('pax-path').records.filter((r) => r.typeflag === 'x');
    assert.ok(xs.length > 0, 'no typeflag x header');
    assert.ok(
      xs.some((r) => r.paxKeys.includes('path') && r.paxKeys.includes('linkpath')),
      `no x header with both keys: ${JSON.stringify(xs.map((r) => r.paxKeys))}`,
    );
  });

  await t.test('record presence: pax-global carries a g record', () => {
    assert.ok(rawOf('pax-global').records.some((r) => r.typeflag === 'g'), 'no typeflag g header');
  });

  await t.test('record presence: ustar-prefix joins a POSIX prefix and name into a listed path', () => {
    const lines = readGood('ustar-prefix', '.listing').toString('utf8').split('\n').filter((x) => x !== '');
    const listed = new Set(lines.map((x) => parseListingLine(x).path));
    const split = rawOf('ustar-prefix').records.filter(
      (r) =>
        r.typeflag === '0' &&
        r.magic.equals(Buffer.from('ustar\0', 'latin1')) &&
        r.version.equals(Buffer.from('00', 'latin1')) &&
        cstr(r.prefix) !== '',
    );
    assert.ok(split.length > 0, 'no regular-file header with magic ustar\\0, version 00 and a non-empty prefix');
    for (const r of split) {
      const joined = `${cstr(r.prefix)}/${cstr(r.name)}`;
      assert.ok(Buffer.byteLength(joined) > 100, `joined path is not over 100 bytes: ${joined}`);
      assert.ok(listed.has(joined), `joined path is not a line of ustar-prefix.listing: ${joined}`);
    }
  });

  await t.test('the bad directory holds exactly the FR-058 bad set', () => {
    const files = fs.readdirSync(BAD_DIR).sort();
    const expected = Object.keys(BAD)
      .flatMap((n) => [`${n}.expected-error`, `${n}.tar.gz`])
      .sort();
    assert.deepEqual(files, expected);
  });

  for (const [name, { code, item }] of Object.entries(BAD)) {
    await t.test(`bad archive ${name}: ${code}`, () => {
      const recorded = fs.readFileSync(path.join(BAD_DIR, `${name}.expected-error`), 'utf8').trim();
      assert.equal(recorded, code, `${name}.expected-error`);
      const buf = fs.readFileSync(path.join(BAD_DIR, `${name}.tar.gz`));
      if (code === 'E_LAYOUT') {
        const lines = listingFromTarGz(buf);
        expectCode(() => checkLinuxLayout(lines, { vst3Bundle: BUNDLE }), code, name, item);
      } else {
        expectCode(() => listingFromTarGz(buf), code, name);
      }
    });
  }

  await t.test('KAT archive', () => {
    const KAT_DIR = path.join(__dirname, '..', 'fixtures', 'kat');
    const kat = 'KatPlugin-1.2.3-Linux-x64.tar.gz';
    const lines = listingFromTarGz(fs.readFileSync(path.join(KAT_DIR, kat)));
    const expected = fs.readFileSync(path.join(KAT_DIR, `${kat}.listing`), 'utf8');
    const actual = toText(lines);
    assert.ok(actual === expected, `${kat}.listing: ${firstDifference(actual, expected)}`);
    assert.equal(digest(lines), fs.readFileSync(path.join(KAT_DIR, `${kat}.sha256`), 'utf8').trim(), `${kat}.sha256`);
    assert.doesNotThrow(() => checkLinuxLayout(lines, { vst3Bundle: 'KatPlugin.vst3' }));
  });

  await t.test('checkLinuxLayout accepts real-shaped', () => {
    const lines = listingFromTarGz(readGood('real-shaped', '.tar.gz'));
    assert.doesNotThrow(() => checkLinuxLayout(lines, { vst3Bundle: BUNDLE }));
  });

  await t.test('FR-031 rules on in-memory entries', async (tt) => {
    const rejects = [
      ['invalid UTF-8 path', [f(Buffer.from([0xc3, 0x28]))], 'E_LISTING_PATH'],
      ['invalid UTF-8 target', [l('A/x', Buffer.from([0xc3, 0x28]))], 'E_LISTING_PATH'],
      ['NUL in a path', [f('A/a\0b')], 'E_LISTING_PATH'],
      ['CR in a path', [f('A/a\rb')], 'E_LISTING_PATH'],
      ['LF in a path', [f('A/a\nb')], 'E_LISTING_PATH'],
      ['" -> " in a path', [f('A/a -> b')], 'E_LISTING_PATH'],
      ['empty path', [f('')], 'E_LISTING_PATH'],
      ['empty target', [l('A/x', '')], 'E_LISTING_PATH'],
      ['absolute path', [f('/A/x')], 'E_LISTING_ABSOLUTE'],
      ['absolute target /etc/x', [l('A/x', '/etc/x')], 'E_LISTING_ABSOLUTE'],
      ['drive target C:x', [l('A/x', 'C:x')], 'E_LISTING_DRIVE'],
      ['backslash target a\\b', [l('A/x', 'a\\b')], 'E_LISTING_BACKSLASH'],
      ['path a//b', [f('a//b')], 'E_LISTING_DOTDOT'],
      ['path a/./b', [f('a/./b')], 'E_LISTING_DOTDOT'],
      ['path A/../b', [f('A/../b')], 'E_LISTING_DOTDOT'],
      ['target a/../b', [l('A/x', 'a/../b')], 'E_LISTING_SYMLINK_FORM'],
      ['target ./a', [l('A/x', './a')], 'E_LISTING_SYMLINK_FORM'],
      ['target ..', [l('A/b/x', '..')], 'E_LISTING_SYMLINK_FORM'],
      ['target a//b', [l('A/x', 'a//b')], 'E_LISTING_SYMLINK_FORM'],
      ['target with a trailing /', [l('A/x', 'b/')], 'E_LISTING_SYMLINK_FORM'],
      ['FIFO (typeflag 6)', [{ type: 'other', typeflag: '6', mode: 0o644, path: 'A/fifo' }], 'E_LISTING_TYPE'],
      ['root-level symlink', [l('x', 'A')], 'E_LISTING_ROOT_SYMLINK'],
      ['target above the root', [l('A/x', '../../y')], 'E_LISTING_SYMLINK_ESCAPE'],
      ['target leaving its top-level entry', [l('A/b/x', '../../B/y')], 'E_LISTING_SYMLINK_ESCAPE'],
      ['target resolving to its top-level entry itself', [l('A/b/x', '../../A')], 'E_LISTING_SYMLINK_ESCAPE'],
      ['duplicate path', [f('A/x'), f('A/x')], 'E_LISTING_DUPLICATE'],
      ['file that is also an implied directory', [f('A/x'), f('A/x/y')], 'E_LISTING_DUPLICATE'],
      ['chain: a link through a link', [l('A/b/c/s', '../../d'), l('A/b/c/t', 's/x')], 'E_LISTING_SYMLINK_CHAIN'],
      ['chain: a link to a link', [l('A/s', 'd'), l('A/t', 's')], 'E_LISTING_SYMLINK_CHAIN'],
      ['self-link', [l('A/s', 's')], 'E_LISTING_SYMLINK_CHAIN'],
      ['dotdot chain (PD-12 review case)', [l('A/b/c/s', '../../d'), l('A/b/c/t', 's/../../../x')], 'E_LISTING_SYMLINK_FORM'],
      ['an entry under a link', [l('A/t', 'b'), f('A/t/evil')], 'E_LISTING_THROUGH_SYMLINK'],
      ['a link under a link', [l('A/t', 'b'), l('A/t/u', '../c')], 'E_LISTING_THROUGH_SYMLINK'],
    ];
    for (const [label, entries, code] of rejects) {
      await tt.test(`${label}: ${code}`, () => expectCode(() => listingFromEntries(entries), code, label));
    }

    await tt.test('accept: in-bundle links, the targets present as entries', () => {
      const lines = listingFromEntries([
        f('Alpha.vst3/Contents/x86_64-linux/Alpha.so'),
        l('Alpha.vst3/Contents/x86_64-linux/a', 'Alpha.so'),
        f('Alpha.vst3/Contents/Resources/moduleinfo.json'),
        l('Alpha.vst3/Contents/x86_64-linux/sub/b', '../../Resources/moduleinfo.json'),
      ]);
      assert.deepEqual(lines, [
        'd 0755 Alpha.vst3',
        'd 0755 Alpha.vst3/Contents',
        'd 0755 Alpha.vst3/Contents/Resources',
        'f 0644 Alpha.vst3/Contents/Resources/moduleinfo.json',
        'd 0755 Alpha.vst3/Contents/x86_64-linux',
        'f 0644 Alpha.vst3/Contents/x86_64-linux/Alpha.so',
        'l 0777 Alpha.vst3/Contents/x86_64-linux/a -> Alpha.so',
        'd 0755 Alpha.vst3/Contents/x86_64-linux/sub',
        'l 0777 Alpha.vst3/Contents/x86_64-linux/sub/b -> ../../Resources/moduleinfo.json',
      ]);
    });

    await tt.test('D3 mode normalisation, implied ancestors, root ./ dropped, one leading ./ stripped', () => {
      const lines = listingFromEntries([
        d('./', 0o700),
        d('./A/', 0o700),
        f('./A/x', 0o750),
        f('./A/y', 0o640),
        { type: 'l', mode: 0o755, path: './A/l', target: 'x' },
        f('B/c/d.txt', 0o600),
      ]);
      assert.deepEqual(lines, [
        'd 0755 A',
        'l 0777 A/l -> x',
        'f 0755 A/x',
        'f 0644 A/y',
        'd 0755 B',
        'd 0755 B/c',
        'f 0644 B/c/d.txt',
      ]);
    });

    await tt.test('lines sort by the UTF-8 bytes of the path', () => {
      const lines = listingFromEntries([f('A/b'), f('A-c'), f('A/é'), f('A/Z')]);
      // 'A' is a prefix of 'A-c'; '-' (0x2D) sorts before '/' (0x2F); 'Z' before 'b'; U+00E9 is 0xC3 0xA9.
      assert.deepEqual(lines, ['d 0755 A', 'f 0644 A-c', 'f 0644 A/Z', 'f 0644 A/b', 'f 0644 A/é']);
    });

    await tt.test('parseListingLine reads a path with spaces and a link target', () => {
      assert.deepEqual(parseListingLine('f 0644 presets/Warm Pads/Slow Swell.vstpreset'), {
        type: 'f',
        mode: 0o644,
        path: 'presets/Warm Pads/Slow Swell.vstpreset',
      });
      assert.deepEqual(parseListingLine('l 0777 A/b c -> d e'), { type: 'l', mode: 0o777, path: 'A/b c', target: 'd e' });
    });
  });

  await t.test('FR-033 layout items', async (tt) => {
    const base = listingFromTarGz(readGood('real-shaped', '.tar.gz'));
    const so = `${BUNDLE}/Contents/x86_64-linux/Alpha.so`;
    const moduleinfo = `${BUNDLE}/Contents/Resources/moduleinfo.json`;
    assert.ok(base.includes(`f 0644 ${so}`), 'real-shaped lists the .so as f 0644');
    assert.ok(base.includes(`f 0644 ${moduleinfo}`), 'real-shaped lists moduleinfo.json as f 0644');
    const cases = [
      ['without moduleinfo.json', base.filter((x) => x !== `f 0644 ${moduleinfo}`), moduleinfo],
      ['without the .so', base.filter((x) => x !== `f 0644 ${so}`), so],
      ['with the .so renamed Other.so', base.map((x) => (x === `f 0644 ${so}` ? x.replace('Alpha.so', 'Other.so') : x)), so],
      ['with an extra root item', [...base, 'f 0644 NOTES.txt'], 'NOTES.txt'],
    ];
    for (const [label, lines, item] of cases) {
      await tt.test(`${label}: E_LAYOUT naming ${item}`, () =>
        expectCode(() => checkLinuxLayout(lines, { vst3Bundle: BUNDLE }), 'E_LAYOUT', label, item),
      );
    }
  });
});
