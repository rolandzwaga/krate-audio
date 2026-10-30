#!/usr/bin/env node
// Builds the feed-contract JSON fixtures from the tables in this file, deterministically (plan PD-7, §4.7).
//
// WHY: the fixtures carry long signature texts and hex digests that later phases (2, 3, 5, 7, 8) read in place;
// hand-written copies drift. One builder plus `--check` turns drift into a red run instead of a silent mismatch.
//
// Usage:
//   node tools/feed-contract/fixtures/make-fixtures.js --out <dir>   write every fixture set under <dir>
//   node tools/feed-contract/fixtures/make-fixtures.js --check       build into a temp dir, compare byte for byte with
//                                                                    the committed tree; exit 1 naming the first
//                                                                    differing, missing or stray file, else exit 0
// Exports build(outDir), check([committedDir]) -> {ok, diff}, fixtureSignature(comment), SETS.
//
// Each set declares the files it owns (and any directory it owns entirely, where a file it did not build is a stray).
// gen-feed goldens (expected.*) and the snapshot directory are never owned: they are not built here.
// Requires only node: modules, ../contract.js and ../listing.js (the release set's real C4 payload listings) and, for
// the phase3 set, ../../gen-feed.js (through phase3/make-phase3.js).
// Contains no shipped plugin name (fixture names: Alpha, Bravo, ...).
'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const contract = require('../contract.js');
const listing = require('../listing.js');
const phase3 = require('./phase3/make-phase3.js');

const FIXTURES_DIR = __dirname;

// --- helpers

const json = (value) => JSON.stringify(value, null, 2) + '\n';

/**
 * A minisign-shaped text that is not a signature (plan §4.7): zero-filled 'ED' blob (zero key id, zero signature),
 * zero global signature, LF line ends. It parses under parseMinisigText; it verifies under no key.
 */
function fixtureSignature(comment) {
  const blob = Buffer.concat([Buffer.from('ED', 'latin1'), Buffer.alloc(8), Buffer.alloc(64)]);
  return (
    'untrusted comment: krate fixture signature (not cryptographic)\n' +
    blob.toString('base64') +
    '\ntrusted comment: ' +
    comment +
    '\n' +
    Buffer.alloc(64).toString('base64') +
    '\n'
  );
}

// --- set: trusted-comments.json (FR-057, SC-007)

function trustedComments() {
  const { formatTrustedComment, assetNameFor, PLATFORM_KEYS } = contract;
  const tc = (name, version, key) => formatTrustedComment(name, version, key, assetNameFor(name, version, key));
  const linuxAsset = 'Alpha-1.2.0-Linux-x64.tar.gz';
  const raw = (name, version, key, asset) => ['krate', '1', name, version, key, asset].join(':');

  const valid = [
    ...PLATFORM_KEYS.map((key) => tc('Alpha', '1.2.0', key)), // stable, one per defined key
    tc('Alpha', '1.2.0-beta.1', 'linux-x86_64'), // prerelease version
  ];

  const invalid = [
    { comment: 'krate:1:Alpha:1.2.0:linux-x86_64', code: 'E_TC_FIELDS', why: 'five fields' },
    { comment: raw('Alpha', '1.2.0', 'linux-x86_64', linuxAsset) + ':extra', code: 'E_TC_FIELDS', why: 'seven fields' },
    { comment: 'krate:2:Alpha:1.2.0:linux-x86_64:' + linuxAsset, code: 'E_TC_FIELDS', why: 'prefix krate:2' },
    {
      comment: raw('Alpha', '1.2.0', 'windows-aarch64', 'Alpha-1.2.0-Windows-x64.exe'),
      code: 'E_TC_PLATFORM',
      why: 'windows-aarch64 is not one of the five defined keys',
    },
    { comment: raw('Alpha', '1.2', 'linux-x86_64', linuxAsset), code: 'E_TC_VERSION', why: 'version with two parts' },
    { comment: raw('Alpha', '01.2.3', 'linux-x86_64', linuxAsset), code: 'E_TC_VERSION', why: 'leading zero' },
    {
      comment: raw('Alpha', '1.2.3+b1', 'linux-x86_64', linuxAsset),
      code: 'E_TC_VERSION',
      why: 'build metadata is not allowed',
    },
    {
      comment: raw('Alpha', '1.2.4294967296', 'linux-x86_64', linuxAsset),
      code: 'E_TC_VERSION',
      why: 'a 10-digit part exceeds the FR-004 bound (uint32_t)',
    },
    {
      comment: raw('Alpha', '1.2.0', 'linux-x86_64', 'dir/' + linuxAsset),
      code: 'E_TC_ASSET',
      why: "asset name with '/'",
    },
    {
      comment: raw('Alpha', '1.2.0', 'linux-x86_64', 'dir\\' + linuxAsset),
      code: 'E_TC_ASSET',
      why: "asset name with '\\'",
    },
    { comment: raw('Alpha', '1.2.0', 'linux-x86_64', '..'), code: 'E_TC_ASSET', why: "asset name '..'" },
    {
      comment: raw('Alpha', '1.2.0', 'linux-x86_64', 'C:' + linuxAsset),
      code: 'E_TC_FIELDS',
      why: "asset name with ':': the colon splits it, so the comment has seven fields",
    },
    { comment: raw('', '1.2.0', 'linux-x86_64', linuxAsset), code: 'E_TC_NAME', why: 'empty name' },
    { comment: raw('Alpha', '', 'linux-x86_64', linuxAsset), code: 'E_TC_VERSION', why: 'empty version' },
    { comment: raw('Alpha', '1.2.0', '', linuxAsset), code: 'E_TC_PLATFORM', why: 'empty platform key' },
    { comment: raw('Alpha', '1.2.0', 'linux-x86_64', ''), code: 'E_TC_ASSET', why: 'empty asset name' },
    { comment: raw('Nul', '1.2.0', 'linux-x86_64', linuxAsset), code: 'E_TC_NAME', why: 'reserved device name (PD-14)' },
    {
      comment: raw('Alpha', '1.2.0', 'linux-x86_64', 'Alpha-1.2.0-Linux\r-x64.tar.gz'),
      code: 'E_TC_FIELDS',
      why: 'embedded CR',
    },
    {
      comment: raw('Alpha', '1.2.0', 'linux-x86_64', linuxAsset) + '\r',
      code: 'E_TC_FIELDS',
      why: 'trailing CR (a CRLF line end left in the comment)',
    },
    {
      comment: raw('Alpha', '1.2.0', 'linux-x86_64', 'Alpha-1.2.0\n-Linux-x64.tar.gz'),
      code: 'E_TC_FIELDS',
      why: 'embedded LF',
    },
  ];

  const a = { name: 'Alpha', version: '1.2.0', platform: 'linux-x86_64', asset: linuxAsset };
  const binding = {
    comment: formatTrustedComment(a.name, a.version, a.platform, a.asset),
    matches: a,
    mismatches: [
      {
        metadata: { name: 'Alpha', version: '1.2.0', platform: 'windows-x86_64', asset: 'Alpha-1.2.0-Windows-x64.exe' },
        field: 'platform',
        why: "asset A's comment presented with asset B's metadata: platform is the first differing field",
      },
      { metadata: { ...a, name: 'Bravo' }, field: 'name', why: 'name only differs' },
      { metadata: { ...a, version: '1.2.1' }, field: 'version', why: 'version only differs' },
      { metadata: { ...a, asset: 'Alpha-1.2.0-Linux-arm64.tar.gz' }, field: 'asset', why: 'asset name only differs' },
    ],
  };

  return { 'trusted-comments.json': json({ valid, invalid, binding }) };
}

// --- set: feeds/ (C1, FR-001 to FR-014, FR-055, SC-005, SC-026; plan §3.1, §4.7 PD-11)

const REPO = 'rolandzwaga/krate-audio';
const PUB_DATE = '2026-10-29T00:00:00Z';
const MANAGER_LATEST_URL = 'https://rolandzwaga.github.io/krate-audio/manager/latest.json';
const FORMAT_BY_PREFIX = Object.freeze({ windows: 'inno-exe', darwin: 'pkg', linux: 'tar.gz' });
const MIN_OS_BY_PREFIX = Object.freeze({ darwin: '11.0', linux: '2.31' }); // macOS product version / glibc version

const sha256Hex = (text) => crypto.createHash('sha256').update(text, 'utf8').digest('hex');
// The stated synthetic contents behind every size and sha256 (fixtures/README.md, feeds/).
const assetContent = (asset) => `krate fixture asset ${asset}\n`;
const payloadContent = (asset) => `krate fixture payload ${asset}\n`;

/** One platform object (FR-006, FR-007, FR-009), keys in C2 asset order minus `name` (FR-043). */
function platformFor(name, version, tag, key, { windowsMinOs = false } = {}) {
  const prefix = key.slice(0, key.indexOf('-'));
  const asset = contract.assetNameFor(name, version, key);
  const p = {
    url: contract.perTagUrl(REPO, tag, asset),
    size: Buffer.byteLength(assetContent(asset), 'utf8'),
    sha256: sha256Hex(assetContent(asset)),
    signature: fixtureSignature(contract.formatTrustedComment(name, version, key, asset)),
    format: FORMAT_BY_PREFIX[prefix],
  };
  if (MIN_OS_BY_PREFIX[prefix]) p.min_os = MIN_OS_BY_PREFIX[prefix];
  if (prefix === 'windows' && windowsMinOs) p.min_os = '10.0';
  if (p.format === 'tar.gz') p.payload = { files: 3, sha256: sha256Hex(payloadContent(asset)) };
  return p;
}

/** `platforms` with its keys sorted bytewise (FR-043). */
function platformsFor(name, version, tag, keys, opts) {
  const out = {};
  for (const key of [...keys].sort()) out[key] = platformFor(name, version, tag, key, opts);
  return out;
}

const guidKey = (n) => `{0A1F0000-0000-4000-8000-${String(n).padStart(12, '0')}}_is1`;

/** FR-008: the names a plugin with these platform keys must carry. */
function installFor(name, keys, { presetSubdir, guid } = {}) {
  const darwin = keys.some((k) => k.startsWith('darwin-'));
  const install = { vst3_bundle: `${name}.vst3` };
  if (darwin) install.au_component = `${name}.component`;
  if (presetSubdir !== undefined) install.preset_subdir = presetSubdir;
  if (keys.includes('windows-x86_64')) install.windows_uninstall_key = guidKey(guid);
  if (darwin) install.macos_pkg_id = `com.krateaudio.${name.toLowerCase()}`;
  return install;
}

const releaseNotesUrl = (tag) => `https://github.com/${REPO}/releases/tag/${tag}`;

/** A stable entry (FR-003), keys in FR-043 order. No `keys` = a v1-only entry (no platforms, no install). */
function entryFor(name, version, { keys, presetSubdir, guid, notes, windowsMinOs, prerelease } = {}) {
  const tag = `${name.toLowerCase()}/v${version}`;
  const entry = {
    version,
    download_url: `https://krateaudio.com/${name.toLowerCase()}/`,
    release_notes: '',
    tag,
    pub_date: PUB_DATE,
    channel: 'stable',
    release_notes_url: releaseNotesUrl(tag),
  };
  if (notes !== undefined) entry.notes_markdown = notes;
  if (keys) {
    entry.platforms = platformsFor(name, version, tag, keys, { windowsMinOs });
    entry.install = installFor(name, keys, { presetSubdir, guid });
  }
  if (prerelease) entry.prerelease = prerelease;
  return entry;
}

/** A prerelease sibling (FR-010): no v1 keys; `install` is the plugin's own (one plugin, FR-014). */
function siblingFor(name, version, { keys, install } = {}) {
  const tag = `${name.toLowerCase()}/v${version}`;
  const sibling = { tag, pub_date: PUB_DATE, channel: 'beta', release_notes_url: releaseNotesUrl(tag) };
  if (keys) sibling.platforms = platformsFor(name, version, tag, keys);
  if (install) sibling.install = install;
  return sibling;
}

const ALPHA_KEYS = Object.freeze(['windows-x86_64', 'darwin-aarch64', 'linux-x86_64']);
const BRAVO_KEYS = Object.freeze(['windows-x86_64', 'darwin-x86_64', 'linux-x86_64']);
const CHARLIE_KEYS = Object.freeze(['windows-x86_64', 'darwin-universal', 'linux-x86_64']);

/** The full-v2 base: all five defined keys across Alpha, Bravo, Charlie; Bravo carries the "Kits" preset folder. */
function baseFeed() {
  return {
    schema_version: 2,
    plugins: {
      Alpha: entryFor('Alpha', '1.2.0', { keys: ALPHA_KEYS, guid: 1, notes: '### Fixed\n\n- A fixture note.' }),
      Bravo: entryFor('Bravo', '2.0.1', {
        keys: BRAVO_KEYS,
        presetSubdir: 'Kits',
        guid: 2,
        prerelease: siblingFor('Bravo', '2.1.0-beta.1'),
      }),
      Charlie: entryFor('Charlie', '0.9.0', { keys: CHARLIE_KEYS, guid: 3, windowsMinOs: true }),
    },
    manager: { latest_json_url: MANAGER_LATEST_URL },
  };
}

/** The base with one change: fn(doc, plugins) edits a fresh copy. */
function changed(fn) {
  const doc = baseFeed();
  fn(doc, doc.plugins);
  return doc;
}

/** Renames an object key in place, keeping key order. */
function renameKey(obj, from, to) {
  const entries = Object.entries(obj).map(([k, v]) => [k === from ? to : k, v]);
  for (const k of Object.keys(obj)) delete obj[k];
  for (const [k, v] of entries) obj[k] = v;
}

/** Edits line `index` (0-based) of a signature text; throws if the edit changed nothing. */
function editSigLine(sig, index, edit) {
  const lines = sig.split('\n');
  const before = lines[index];
  lines[index] = edit(before);
  if (lines[index] === before) throw new Error('make-fixtures: signature edit changed nothing');
  return lines.join('\n');
}

const blobBase64 = (alg, bytes) => Buffer.concat([Buffer.from(alg, 'latin1'), Buffer.alloc(bytes - 2)]).toString('base64');

function notes14000() {
  let s = '### Changed\n\n';
  for (let i = 1; Buffer.byteLength(s, 'utf8') <= 14100; i += 1) {
    s += `- Fixture change ${String(i).padStart(4, '0')}: one line of a long release-notes section that no reader may truncate.\n`;
  }
  return s.trimEnd();
}

const plats = (entry) => entry.platforms;
const sig = (fn) => (d, p) => {
  const lin = plats(p.Alpha)['linux-x86_64'];
  lin.signature = fn(lin.signature);
};

// One row per plan §4.7 "C1 invalid rows" id: [rule, expect, change] (change = (doc, plugins) => void, or a raw text).
const C1_INVALID = [
  ['FR-001/malformed-json', 'E_INPUT', '{"schema_version": 2, "plugins": {"Alpha": {"version": "1.2.0",\n'],
  ['FR-001/schema_version-const', '/schema_version const', (d) => (d.schema_version = 1)],
  ['FR-001/plugins-required', ' required', (d) => delete d.plugins],
  ['FR-002/plugin-key-pattern', '/plugins pattern', (d) => renameKey(d.plugins, 'Alpha', 'alpha')],
  ['FR-002/plugin-key-reserved', '/plugins not', (d) => renameKey(d.plugins, 'Alpha', 'Nul')],
  ['FR-003/version-type', '/plugins/Alpha/version type', (d, p) => (p.Alpha.version = 120)],
  ['FR-003/download_url-required', '/plugins/Alpha required', (d, p) => delete p.Alpha.download_url],
  ['FR-003/release_notes-const', '/plugins/Alpha/release_notes const', (d, p) => (p.Alpha.release_notes = 'x')],
  ['FR-003/tag-required', '/plugins/Alpha required', (d, p) => delete p.Alpha.tag],
  ['FR-003/pub_date-required', '/plugins/Alpha required', (d, p) => delete p.Alpha.pub_date],
  ['FR-003/channel-required', '/plugins/Alpha required', (d, p) => delete p.Alpha.channel],
  ['FR-003/release_notes_url-required', '/plugins/Alpha required', (d, p) => delete p.Alpha.release_notes_url],
  ['FR-003/channel-stable-const', '/plugins/Alpha/channel const', (d, p) => (p.Alpha.channel = 'beta')],
  ['FR-003/notes_markdown-type', '/plugins/Alpha/notes_markdown type', (d, p) => (p.Alpha.notes_markdown = ['x'])],
  ['FR-003/platforms-requires-install', '/plugins/Alpha dependentRequired', (d, p) => delete p.Alpha.install],
  ['FR-003/install-requires-platforms', '/plugins/Alpha dependentRequired', (d, p) => delete p.Alpha.platforms],
  ['FR-004/version-suffix', '/plugins/Alpha/version pattern', (d, p) => (p.Alpha.version = '1.1.0-beta.1')],
  ['FR-004/version-ten-digits', '/plugins/Alpha/version pattern', (d, p) => (p.Alpha.version = '4294967296.0.0')],
  ['FR-004/version-leading-zero', '/plugins/Alpha/version pattern', (d, p) => (p.Alpha.version = '01.2.3')],
  ['FR-005/tag-pattern', '/plugins/Alpha/tag pattern', (d, p) => (p.Alpha.tag = 'Alpha/v1.0.0')],
  ['FR-005/tag-stable-suffix', '/plugins/Alpha/tag pattern', (d, p) => (p.Alpha.tag = 'alpha/v1.0.0-rc1')],
  ['FR-005/pub_date-pattern', '/plugins/Alpha/pub_date pattern', (d, p) => (p.Alpha.pub_date = '2026-10-29T00:00:00+01:00')],
  [
    'FR-005/url-https',
    '/plugins/Alpha/platforms/windows-x86_64/url pattern',
    (d, p) => (plats(p.Alpha)['windows-x86_64'].url = plats(p.Alpha)['windows-x86_64'].url.replace('https://', 'http://')),
  ],
  [
    'FR-005/url-whitespace',
    '/plugins/Alpha/download_url pattern',
    (d, p) => (p.Alpha.download_url = 'https://krateaudio.com/alpha plugin/'),
  ],
  [
    'FR-005/url-latest',
    '/plugins/Alpha/download_url not',
    (d, p) => (p.Alpha.download_url = `https://github.com/${REPO}/releases/latest/download/Alpha-1.2.0-Windows-x64.exe`),
  ],
  ['FR-006/platform-key-pattern', '/plugins/Alpha/platforms pattern', (d, p) => renameKey(plats(p.Alpha), 'darwin-aarch64', 'macos-arm64')],
  ['FR-006/platform-url-required', '/plugins/Alpha/platforms/windows-x86_64 required', (d, p) => delete plats(p.Alpha)['windows-x86_64'].url],
  ['FR-006/platform-size-zero', '/plugins/Alpha/platforms/windows-x86_64/size minimum', (d, p) => (plats(p.Alpha)['windows-x86_64'].size = 0)],
  [
    'FR-006/platform-size-max',
    '/plugins/Alpha/platforms/windows-x86_64/size maximum',
    (d, p) => (plats(p.Alpha)['windows-x86_64'].size = 2147483648),
  ],
  ['FR-006/platform-size-type', '/plugins/Alpha/platforms/windows-x86_64/size type', (d, p) => (plats(p.Alpha)['windows-x86_64'].size = 1.5)],
  [
    'FR-006/platform-sha256-length',
    '/plugins/Alpha/platforms/windows-x86_64/sha256 pattern',
    (d, p) => (plats(p.Alpha)['windows-x86_64'].sha256 = plats(p.Alpha)['windows-x86_64'].sha256.slice(1)),
  ],
  [
    'FR-006/platform-sha256-hex',
    '/plugins/Alpha/platforms/windows-x86_64/sha256 pattern',
    (d, p) => (plats(p.Alpha)['windows-x86_64'].sha256 = plats(p.Alpha)['windows-x86_64'].sha256.toUpperCase()),
  ],
  ['FR-006/format-windows', '/plugins/Alpha/platforms/windows-x86_64/format const', (d, p) => (plats(p.Alpha)['windows-x86_64'].format = 'pkg')],
  [
    'FR-006/format-darwin',
    '/plugins/Charlie/platforms/darwin-universal/format const',
    (d, p) => (plats(p.Charlie)['darwin-universal'].format = 'inno-exe'),
  ],
  ['FR-006/format-linux', '/plugins/Alpha/platforms/linux-x86_64/format const', (d, p) => (plats(p.Alpha)['linux-x86_64'].format = 'pkg')],
  ['FR-006/min_os-darwin-required', '/plugins/Alpha/platforms/darwin-aarch64 required', (d, p) => delete plats(p.Alpha)['darwin-aarch64'].min_os],
  ['FR-006/min_os-linux-required', '/plugins/Alpha/platforms/linux-x86_64 required', (d, p) => delete plats(p.Alpha)['linux-x86_64'].min_os],
  ['FR-006/min_os-pattern', '/plugins/Alpha/platforms/darwin-aarch64/min_os pattern', (d, p) => (plats(p.Alpha)['darwin-aarch64'].min_os = '14')],
  ['FR-007/signature-legacy-Ed', 'E_SIGNATURE_TEXT', sig((s) => editSigLine(s, 1, () => blobBase64('Ed', 74)))],
  ['FR-007/signature-cr', 'E_SIGNATURE_TEXT', sig((s) => s.replace(/\n/gu, '\r\n'))],
  ['FR-007/signature-blob-length', 'E_SIGNATURE_TEXT', sig((s) => editSigLine(s, 1, () => blobBase64('ED', 73)))],
  // 74 bytes end in a 2-byte group "AAA=" whose last char carries 2 unused bits; "AAB=" decodes the same bytes.
  ['FR-007/signature-noncanonical-base64', 'E_SIGNATURE_TEXT', sig((s) => editSigLine(s, 1, (l) => l.replace(/A=$/u, 'B=')))],
  ['FR-007/signature-trusted-comment', 'E_TC_FIELDS', sig((s) => editSigLine(s, 2, () => 'trusted comment: krate:1:Alpha:1.2.0'))],
  ['FR-007/signature-no-final-lf', '/plugins/Alpha/platforms/linux-x86_64/signature pattern', sig((s) => s.slice(0, -1))],
  ['FR-008/install-dotdot', 'E_INSTALL_VALUE', (d, p) => (p.Alpha.install.vst3_bundle = 'Alpha..vst3')],
  ['FR-008/install-slash', 'E_INSTALL_VALUE', (d, p) => (p.Alpha.install.vst3_bundle = 'sub/Alpha.vst3')],
  ['FR-008/install-backslash', 'E_INSTALL_VALUE', (d, p) => (p.Alpha.install.vst3_bundle = 'sub\\Alpha.vst3')],
  ['FR-008/vst3_bundle-required', '/plugins/Alpha/install required', (d, p) => delete p.Alpha.install.vst3_bundle],
  [
    'FR-008/uninstall-key-pattern',
    '/plugins/Alpha/install/windows_uninstall_key pattern',
    (d, p) => (p.Alpha.install.windows_uninstall_key = '{0a1f0000-0000-4000-8000-00000000000a}_is1'),
  ],
  ['FR-008/pkg-id-pattern', '/plugins/Alpha/install/macos_pkg_id pattern', (d, p) => (p.Alpha.install.macos_pkg_id = 'com.other.alpha')],
  ['FR-008/darwin-requires-au_component', '/plugins/Alpha/install required', (d, p) => delete p.Alpha.install.au_component],
  ['FR-008/darwin-requires-macos_pkg_id', '/plugins/Alpha/install required', (d, p) => delete p.Alpha.install.macos_pkg_id],
  ['FR-008/windows-requires-uninstall-key', '/plugins/Alpha/install required', (d, p) => delete p.Alpha.install.windows_uninstall_key],
  ['FR-008/bundle-reserved-stem', '/plugins/Alpha/install/vst3_bundle not', (d, p) => (p.Alpha.install.vst3_bundle = 'Nul.vst3')],
  ['FR-008/component-reserved-stem', '/plugins/Alpha/install/au_component not', (d, p) => (p.Alpha.install.au_component = 'Con.component')],
  ['FR-008/preset_subdir-reserved', '/plugins/Bravo/install/preset_subdir not', (d, p) => (p.Bravo.install.preset_subdir = 'Aux')],
  ['FR-008/preset_subdir-trailing-space', '/plugins/Bravo/install/preset_subdir pattern', (d, p) => (p.Bravo.install.preset_subdir = 'Kits ')],
  ['FR-009/payload-tar-gz-required', '/plugins/Alpha/platforms/linux-x86_64 required', (d, p) => delete plats(p.Alpha)['linux-x86_64'].payload],
  [
    'FR-009/payload-inno-exe-forbidden',
    '/plugins/Alpha/platforms/windows-x86_64/payload false schema',
    (d, p) => (plats(p.Alpha)['windows-x86_64'].payload = { ...plats(p.Alpha)['linux-x86_64'].payload }),
  ],
  [
    'FR-009/payload-pkg-forbidden',
    '/plugins/Alpha/platforms/darwin-aarch64/payload false schema',
    (d, p) => (plats(p.Alpha)['darwin-aarch64'].payload = { ...plats(p.Alpha)['linux-x86_64'].payload }),
  ],
  ['FR-009/payload-files-min', '/plugins/Alpha/platforms/linux-x86_64/payload/files minimum', (d, p) => (plats(p.Alpha)['linux-x86_64'].payload.files = 0)],
  [
    'FR-009/payload-sha256-pattern',
    '/plugins/Alpha/platforms/linux-x86_64/payload/sha256 pattern',
    (d, p) => (plats(p.Alpha)['linux-x86_64'].payload.sha256 = 'not-a-digest'),
  ],
  ['FR-010/sibling-version-forbidden', '/plugins/Bravo/prerelease/version false schema', (d, p) => (p.Bravo.prerelease.version = '2.1.0')],
  [
    'FR-010/sibling-download_url-forbidden',
    '/plugins/Bravo/prerelease/download_url false schema',
    (d, p) => (p.Bravo.prerelease.download_url = 'https://krateaudio.com/bravo/'),
  ],
  ['FR-010/sibling-release_notes-forbidden', '/plugins/Bravo/prerelease/release_notes false schema', (d, p) => (p.Bravo.prerelease.release_notes = '')],
  ['FR-010/sibling-channel-const', '/plugins/Bravo/prerelease/channel const', (d, p) => (p.Bravo.prerelease.channel = 'stable')],
  ['FR-010/sibling-tag-required', '/plugins/Bravo/prerelease required', (d, p) => delete p.Bravo.prerelease.tag],
  ['FR-010/sibling-tag-pattern', '/plugins/Bravo/prerelease/tag pattern', (d, p) => (p.Bravo.prerelease.tag = 'bravo/v2.1')],
  [
    'FR-010/sibling-platforms-requires-install',
    '/plugins/Bravo/prerelease dependentRequired',
    (d, p) => (p.Bravo.prerelease.platforms = platformsFor('Bravo', '2.1.0-beta.1', p.Bravo.prerelease.tag, BRAVO_KEYS)),
  ],
  ['FR-011/manager-latest_json_url-required', '/manager required', (d) => delete d.manager.latest_json_url],
  [
    'FR-011/manager-latest_json_url-suffix',
    '/manager/latest_json_url pattern',
    (d) => (d.manager.latest_json_url = 'https://rolandzwaga.github.io/krate-audio/manager/feed.json'),
  ],
  [
    'FR-011/manager-latest_json_url-https',
    '/manager/latest_json_url pattern',
    (d) => (d.manager.latest_json_url = MANAGER_LATEST_URL.replace('https://', 'http://')),
  ],
  ['FR-012/v1-version-required', '/plugins/Alpha required', (d, p) => delete p.Alpha.version, 'v1'],
  ['FR-012/v1-version-type', '/plugins/Alpha/version type', (d, p) => (p.Alpha.version = 120), 'v1'],
  ['FR-014/dup-vst3_bundle', 'E_INSTALL_DUPLICATE', (d, p) => (p.Bravo.install.vst3_bundle = 'Alpha.vst3')],
  ['FR-014/dup-au_component', 'E_INSTALL_DUPLICATE', (d, p) => (p.Bravo.install.au_component = 'Alpha.component')],
  ['FR-014/dup-uninstall-key', 'E_INSTALL_DUPLICATE', (d, p) => (p.Bravo.install.windows_uninstall_key = guidKey(1))],
  ['FR-014/dup-pkg-id', 'E_INSTALL_DUPLICATE', (d, p) => (p.Bravo.install.macos_pkg_id = 'com.krateaudio.alpha')],
  [
    'FR-014/dup-vst3_bundle-casefold',
    'E_INSTALL_DUPLICATE',
    // PD-13: ALPHA (Linux only, so its only install name is vst3_bundle) next to Alpha; ALPHA.vst3 folds to Alpha.vst3.
    (d, p) => (p.ALPHA = entryFor('ALPHA', '1.2.0', { keys: ['linux-x86_64'] })),
  ],
];

const TENTH_NAMES = Object.freeze(['Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel', 'India', 'Juliet']);

/** An entry whose only platform is windows-aarch64: a key this contract does not define (D1). */
function unknownKeyEntry() {
  const name = 'Delta';
  const version = '1.0.0';
  const tag = `delta/v${version}`;
  const asset = `${name}-${version}-Windows-arm64.exe`;
  const entry = entryFor(name, version);
  entry.platforms = {
    'windows-aarch64': {
      url: contract.perTagUrl(REPO, tag, asset),
      size: Buffer.byteLength(assetContent(asset), 'utf8'),
      sha256: sha256Hex(assetContent(asset)),
      signature: fixtureSignature(['krate', '1', name, version, 'windows-aarch64', asset].join(':')),
      format: 'inno-exe',
    },
  };
  entry.install = { vst3_bundle: `${name}.vst3` };
  return entry;
}

// [rule, file name, document, expect, plugin?]
const C1_VALID = [
  [
    'FR-055/v1-only',
    'v1-only',
    // Today's live shape (release.yml legacy generator): schema_version 1, three v1 keys per plugin. A v1 row.
    {
      schema_version: 1,
      plugins: Object.fromEntries(
        ['Alpha', 'Bravo', 'Charlie'].map((n, i) => [
          n,
          { version: `1.${i}.0`, download_url: `https://krateaudio.com/${n.toLowerCase()}/`, release_notes: '' },
        ]),
      ),
    },
    'accept',
    undefined,
    'v1',
  ],
  ['FR-055/full-v2', 'full-v2', baseFeed(), 'accept'],
  ['FR-055/mixed', 'mixed', changed((d, p) => (p.Delta = entryFor('Delta', '3.0.0'))), 'accept'],
  [
    'FR-055/prerelease-suffixed',
    'prerelease-suffixed',
    changed((d, p) => (p.Alpha.prerelease = siblingFor('Alpha', '1.3.0-beta.1', { keys: ALPHA_KEYS, install: { ...p.Alpha.install } }))),
    'accept',
  ],
  [
    'FR-055/prerelease-plain',
    'prerelease-plain',
    changed((d, p) => (p.Alpha.prerelease = siblingFor('Alpha', '1.3.0', { keys: ALPHA_KEYS, install: { ...p.Alpha.install } }))),
    'accept',
  ],
  ['FR-055/notes-14000', 'notes-14000', changed((d, p) => (p.Alpha.notes_markdown = notes14000())), 'accept'],
  [
    'FR-055/tenth-plugin',
    'tenth-plugin',
    changed((d, p) => {
      for (const [i, n] of TENTH_NAMES.entries()) p[n] = entryFor(n, `0.${i + 1}.0`, { keys: ['linux-x86_64'] });
    }),
    'accept',
  ],
  ['FR-055/unknown-platform-key', 'unknown-platform-key', changed((d, p) => (p.Delta = unknownKeyEntry())), 'accept-without-platforms', 'Delta'],
  [
    'FR-055/unknown-extra-keys',
    'unknown-extra-keys',
    changed((d, p) => {
      d.generated_by = 'a future gen-feed';
      d.manager.channel_hint = 'stable';
      p.Alpha.homepage = 'https://krateaudio.com/alpha/';
      plats(p.Alpha)['linux-x86_64'].arch_note = 'built on a future runner';
      plats(p.Alpha)['linux-x86_64'].payload.compression = 'gzip';
      p.Alpha.install.future_field = 'ignored by readers today';
      p.Bravo.prerelease.future_flag = true;
    }),
    'accept',
  ],
  [
    'FR-014/sibling-same-install',
    'sibling-same-install',
    changed(
      (d, p) =>
        (p.Bravo.prerelease = siblingFor('Bravo', '2.1.0-beta.1', { keys: BRAVO_KEYS, install: { ...p.Bravo.install } })),
    ),
    'accept',
  ],
];

function feeds() {
  const files = {};
  const cases = [];
  const add = (rel, content) => {
    if (Object.hasOwn(files, rel)) throw new Error(`make-fixtures: two feeds rows write ${rel}`);
    files[rel] = content;
  };
  for (const [rule, name, doc, expect, plugin, schema = 'v2'] of C1_VALID) {
    const file = `valid/${name}.json`;
    add(`feeds/${file}`, json(doc));
    cases.push({ rule, file, schema, expect, ...(plugin ? { plugin } : {}) });
  }
  for (const [rule, error, change, schema = 'v2'] of C1_INVALID) {
    const file = `invalid/${rule.slice(rule.indexOf('/') + 1)}.json`;
    add(`feeds/${file}`, typeof change === 'string' ? change : json(changed(change)));
    cases.push({ rule, file, schema, expect: { error } });
  }
  // The Phase 3 mixed feed (FR-045); its file is built by the phase3 set.
  cases.push({ rule: 'Phase3/one-v2-eight-v1', file: 'valid/one-v2-eight-v1.json', schema: 'v2', expect: 'accept' });
  add('feeds/cases.json', json(cases));
  return files;
}

// --- set: release/ (C2 release.json, FR-015 to FR-018, FR-056, SC-006; plan §4.7 PD-10, PD-11)

/** The stated synthetic Linux tree behind every C2 payload (C4 root layout, FR-033); the .so is 0755. */
function linuxTree(name) {
  const bundle = `${name}.vst3`;
  return [
    { type: 'f', mode: 0o644, path: `${bundle}/Contents/Resources/moduleinfo.json` },
    { type: 'f', mode: 0o755, path: `${bundle}/Contents/x86_64-linux/${name}.so` },
    { type: 'f', mode: 0o644, path: 'presets/Default.preset' },
    { type: 'f', mode: 0o644, path: 'README.txt' },
    { type: 'f', mode: 0o644, path: 'CHANGELOG.txt' },
  ];
}

/** payload {files, sha256, list} for these listing lines (FR-016, FR-017). */
const payloadOf = (list) => ({ files: list.length, sha256: listing.digest(list), list });

/** One C2 asset: `name`, then the C1 platform fields (same synthetic values), with a real C4 payload on tar.gz. */
function releaseAsset(name, version, tag, key, { windowsMinOs = false } = {}) {
  const { payload, ...platform } = platformFor(name, version, tag, key, { windowsMinOs });
  const asset = { name: contract.assetNameFor(name, version, key), ...platform };
  if (payload) asset.payload = payloadOf(listing.listingFromEntries(linuxTree(name)));
  return asset;
}

/** A valid manifest (FR-015, FR-016): windows-x86_64, one darwin-* key and linux-x86_64, assets sorted bytewise. */
function releaseFor(name, version, darwinKey, { guid, presetSubdir, windowsMinOs = true } = {}) {
  const tag = `${name.toLowerCase()}/v${version}`;
  const keys = ['windows-x86_64', darwinKey, 'linux-x86_64'];
  const assets = {};
  for (const key of [...keys].sort()) assets[key] = releaseAsset(name, version, tag, key, { windowsMinOs });
  return { tag, name, version, pub_date: PUB_DATE, assets, install: installFor(name, keys, { presetSubdir, guid }) };
}

const baseRelease = () => releaseFor('Alpha', '1.2.0', 'darwin-aarch64', { guid: 1 });

/** The base manifest with one change: fn(doc, assets) edits a fresh copy. */
function releaseChanged(fn) {
  const doc = baseRelease();
  fn(doc, doc.assets);
  return doc;
}

const WIN = 'windows-x86_64';
const MAC = 'darwin-aarch64';
const LIN = 'linux-x86_64';

/** Replaces the linux payload list and recomputes files and sha256, so only the list's content can fail. */
const relist = (fn) => (d, a) => (a[LIN].payload = payloadOf(fn([...a[LIN].payload.list])));

// [rule, code checkReleaseManifest throws, schema finding ("<instancePath> <keyword>", or null: schema-valid), change]
const C2_INVALID = [
  ['FR-015/tag-required', 'E_MANIFEST_SCHEMA', ' required', (d) => delete d.tag],
  ['FR-015/name-required', 'E_MANIFEST_SCHEMA', ' required', (d) => delete d.name],
  ['FR-015/version-required', 'E_MANIFEST_SCHEMA', ' required', (d) => delete d.version],
  ['FR-015/pub_date-required', 'E_MANIFEST_SCHEMA', ' required', (d) => delete d.pub_date],
  ['FR-015/assets-required', 'E_MANIFEST_SCHEMA', ' required', (d) => delete d.assets],
  ['FR-015/install-required', 'E_MANIFEST_SCHEMA', ' required', (d) => delete d.install],
  ['FR-015/channel-stable', 'E_MANIFEST_SCHEMA', ' additionalProperties', (d) => (d.channel = 'stable')],
  ['FR-015/channel-beta', 'E_MANIFEST_SCHEMA', ' additionalProperties', (d) => (d.channel = 'beta')],
  ['FR-015/unknown-key', 'E_MANIFEST_SCHEMA', ' additionalProperties', (d) => (d.homepage = 'https://krateaudio.com/alpha/')],
  ['FR-015/name-reserved', 'E_MANIFEST_SCHEMA', '/name not', (d) => (d.name = 'Nul')],
  ['FR-016/missing-asset', 'E_MANIFEST_SCHEMA', '/assets required', (d, a) => delete a[LIN]],
  // A fourth key outside the five defined ones (windows-aarch64): the asset count and the key enum both reject it.
  ['FR-016/fourth-asset', 'E_MANIFEST_SCHEMA', '/assets maxProperties', (d, a) => (a['windows-aarch64'] = { ...a[WIN] })],
  [
    'FR-016/two-darwin-keys',
    'E_MANIFEST_SCHEMA',
    '/assets maxProperties',
    (d, a) => (a['darwin-x86_64'] = releaseAsset(d.name, d.version, d.tag, 'darwin-x86_64')),
  ],
  ['FR-016/format-windows-pkg', 'E_MANIFEST_SCHEMA', `/assets/${WIN}/format const`, (d, a) => (a[WIN].format = 'pkg')],
  ['FR-016/format-darwin-inno-exe', 'E_MANIFEST_SCHEMA', `/assets/${MAC}/format const`, (d, a) => (a[MAC].format = 'inno-exe')],
  ['FR-016/format-linux-pkg', 'E_MANIFEST_SCHEMA', `/assets/${LIN}/format const`, (d, a) => (a[LIN].format = 'pkg')],
  ['FR-016/min_os-linux-required', 'E_MANIFEST_SCHEMA', `/assets/${LIN} required`, (d, a) => delete a[LIN].min_os],
  ['FR-016/min_os-darwin-required', 'E_MANIFEST_SCHEMA', `/assets/${MAC} required`, (d, a) => delete a[MAC].min_os],
  ['FR-016/payload-tar-gz-required', 'E_MANIFEST_SCHEMA', `/assets/${LIN} required`, (d, a) => delete a[LIN].payload],
  [
    'FR-016/payload-pkg-forbidden',
    'E_MANIFEST_SCHEMA',
    `/assets/${MAC}/payload false schema`,
    (d, a) => (a[MAC].payload = structuredClone(a[LIN].payload)),
  ],
  ['FR-016/list-empty', 'E_MANIFEST_SCHEMA', `/assets/${LIN}/payload/list minItems`, (d, a) => (a[LIN].payload.list = [])],
  ['FR-016/install-missing-au_component', 'E_MANIFEST_SCHEMA', '/install required', (d) => delete d.install.au_component],
  ['FR-016/install-missing-macos_pkg_id', 'E_MANIFEST_SCHEMA', '/install required', (d) => delete d.install.macos_pkg_id],
  [
    'FR-016/install-missing-uninstall-key',
    'E_MANIFEST_SCHEMA',
    '/install required',
    (d) => delete d.install.windows_uninstall_key,
  ],
  ['FR-016/install-missing-vst3_bundle', 'E_MANIFEST_SCHEMA', '/install required', (d) => delete d.install.vst3_bundle],
  ['FR-016/install-dotdot', 'E_INSTALL_VALUE', '/install/vst3_bundle not', (d) => (d.install.vst3_bundle = 'Alpha..vst3')],
  ['FR-017/tag-mismatch', 'E_MANIFEST_SCHEMA', null, (d) => (d.tag = 'alpha/v1.2.1')],
  ['FR-017/asset-name', 'E_MANIFEST_SCHEMA', null, (d, a) => (a[WIN].name = 'Alpha-1.2.0-Windows-x86.exe')],
  ['FR-017/vst3_bundle-name', 'E_MANIFEST_SCHEMA', null, (d) => (d.install.vst3_bundle = 'AlphaFx.vst3')],
  ['FR-017/au_component-name', 'E_MANIFEST_SCHEMA', null, (d) => (d.install.au_component = 'AlphaFx.component')],
  [
    'FR-017/url-other-tag',
    'E_URL_NOT_PER_TAG',
    null,
    (d, a) => (a[WIN].url = contract.perTagUrl(REPO, 'alpha/v1.1.0', a[WIN].name)),
  ],
  [
    'FR-017/url-latest',
    'E_URL_LATEST',
    `/assets/${WIN}/url not`,
    (d, a) => (a[WIN].url = `https://github.com/${REPO}/releases/latest/download/${a[WIN].name}`),
  ],
  ['FR-017/url-http', 'E_URL_NOT_HTTPS', `/assets/${WIN}/url pattern`, (d, a) => (a[WIN].url = a[WIN].url.replace('https://', 'http://'))],
  // A 73-byte blob: still base64 text, so the schema's line pattern holds; only the minisign parse rejects it.
  [
    'FR-017/signature-text',
    'E_SIGNATURE_TEXT',
    null,
    (d, a) => (a[WIN].signature = editSigLine(a[WIN].signature, 1, () => blobBase64('ED', 73))),
  ],
  // The Windows asset carries the Linux asset's signature text: its comment binds another platform and asset.
  ['FR-017/binding', 'E_TC_BINDING', null, (d, a) => (a[WIN].signature = a[LIN].signature)],
  ['FR-017/list-not-canonical', 'E_LISTING_NOT_CANONICAL', null, relist((list) => list.reverse())],
  ['FR-017/list-bad-path', 'E_LISTING_DOTDOT', null, relist((list) => [...list, 'f 0644 presets/../escape.txt'])],
  ['FR-017/files-count', 'E_LISTING_COUNT', null, (d, a) => (a[LIN].payload.files += 1)],
  ['FR-017/digest', 'E_LISTING_DIGEST', null, (d, a) => (a[LIN].payload.sha256 = sha256Hex('krate fixture wrong digest\n'))],
  ['FR-017/layout', 'E_LAYOUT', null, relist((list) => list.filter((l) => l !== 'f 0644 CHANGELOG.txt'))],
];

// [rule, document] (FR-056: one per macOS key, Windows without min_os, a suffixed and a plain three-part version)
const C2_VALID = [
  ['FR-056/darwin-aarch64', baseRelease()],
  ['FR-056/darwin-x86_64', releaseFor('Bravo', '2.0.1', 'darwin-x86_64', { guid: 2, presetSubdir: 'Kits' })],
  ['FR-056/darwin-universal', releaseFor('Charlie', '0.9.0', 'darwin-universal', { guid: 3 })],
  ['FR-056/windows-without-min_os', releaseFor('Alpha', '1.2.0', 'darwin-aarch64', { guid: 1, windowsMinOs: false })],
  ['FR-056/prerelease-suffixed', releaseFor('Alpha', '1.3.0-beta.1', 'darwin-aarch64', { guid: 1 })],
  // FR-018: a manifest has no channel, so this one serves a stable release and a plain-version prerelease alike.
  ['FR-056/plain-version', releaseFor('Alpha', '1.3.0', 'darwin-aarch64', { guid: 1 })],
];

function release() {
  const files = {};
  const cases = [];
  const add = (rule, dir, content, expect) => {
    const file = `${dir}/${rule.slice(rule.indexOf('/') + 1)}.json`;
    if (Object.hasOwn(files, `release/${file}`)) throw new Error(`make-fixtures: two release rows write ${file}`);
    files[`release/${file}`] = content;
    cases.push({ rule, file, expect });
  };
  for (const [rule, doc] of C2_VALID) add(rule, 'valid', json(doc), 'accept');
  for (const [rule, error, ajv, change] of C2_INVALID) {
    add(rule, 'invalid', json(releaseChanged(change)), { error, ajv });
  }
  files['release/cases.json'] = json(cases);
  return files;
}

// --- git-versionsort (SC-004, FR-040): tag sets whose git -v:refname order gen-feed's legacy mode must reproduce.
// No set states an expected answer: the real git, run by tests/git-versionsort.test.js, is the oracle.

// [id, prefix, what the set exercises, versions]; the plugin name is the prefix with a capital first letter.
const VERSIONSORT_SETS = [
  ['multi-digit-minor', 'minor', 'multi-digit minor part', ['1.2.0', '1.9.0', '1.10.0', '1.10.1']],
  ['multi-digit-patch', 'patch', 'multi-digit patch part', ['1.0.9', '1.0.10', '1.0.99', '1.0.100']],
  ['multi-digit-major', 'major', 'multi-digit major part', ['2.0.0', '9.0.0', '9.10.0', '10.0.0']],
  ['release-history', 'history', 'a real-shaped history', ['0.9.4', '0.9.9', '0.9.10', '0.9.11', '0.10.0', '0.12.3']],
  ['common-leading-digit', 'common', 'digit runs sharing a first digit', ['1.2.0', '1.19.0', '1.20.0', '1.199.0']],
  ['rc-over-release', 'rc', '-rc1 / -rc2 beside their release', ['1.0.0', '1.0.0-rc1', '1.0.0-rc2']],
  ['rc-multi-digit', 'rcten', '-rc1 / -rc9 / -rc10', ['1.0.0-rc1', '1.0.0-rc9', '1.0.0-rc10']],
  ['beta-multi-digit', 'beta', '-beta.1 / -beta.2 / -beta.10 over an older release', ['1.1.0', '1.2.0-beta.1', '1.2.0-beta.2', '1.2.0-beta.10']],
  ['beta-same-core', 'betasame', 'a suffixed tag beside its own release', ['1.1.0', '1.1.0-beta.1']],
  ['beta-lower-core', 'betalower', 'a suffixed tag with a lower core', ['1.0.0-beta.1', '1.1.0']],
  ['beta-leading-zero', 'betazero', '-beta.09 beside -beta.9 and -beta.10', ['2.0.0-beta.9', '2.0.0-beta.09', '2.0.0-beta.10']],
  ['leading-zero', 'lead', 'a leading zero (01)', ['1.0.0', '1.01.0', '1.1.0', '1.10.0']],
  ['two-leading-zeros', 'leadtwo', 'leading zeros (01, 001, 00)', ['1.001', '1.01', '1.0', '1.00', '1.1']],
  ['zero-runs', 'zeros', 'runs of zeros only', ['0.0.0', '0.0.00', '0.00.0', '0.0.01']],
  ['fraction-runs', 'fraction', 'zero-led runs compared lexically', ['1.05', '1.050', '1.5', '1.0050']],
  ['suffix-bytes', 'suffix', 'non-digit suffixes compared bytewise', ['1.0.0', '1.0.0a', '1.0.0-a', '1.0.0.1', '1.0.0_1']],
  ['prerelease-kinds', 'kinds', 'alpha / beta / rc suffixes beside the release', ['3.0.0', '3.0.0-alpha', '3.0.0-beta.1', '3.0.0-rc1', '3.0.0-rc.1']],
  ['dash-number', 'dashnum', 'numeric suffixes', ['2.0.0-1', '2.0.0-2', '2.0.0-10']],
  ['many-parts', 'deep', 'five-part versions', ['1.2.3.4', '1.2.3.4.5', '1.2.3.4.10']],
  ['big-numbers', 'big', 'a part above 2^32', ['1.0.999', '1.0.1000', '1.0.4294967296']],
  ['prefix-short', 'alpha', 'a prefix of another prefix (alphabet/v*)', ['1.0.0', '1.2.0']],
  ['prefix-long', 'alphabet', 'the longer prefix, with higher versions', ['0.1.0', '3.0.0']],
  ['single-tag', 'single', 'one tag', ['0.1.0']],
  ['no-tag', 'nomatch', 'no tag of its own, only near misses', []],
];

// Tags that belong to no set: plain v* tags, other prefixes, and near misses of the set prefixes.
const VERSIONSORT_UNRELATED = [
  'v0.1.1',
  'v0.8.0',
  'manager/v0.1.0',
  'alpha-v9.9.9',
  'alpha/x2.0.0',
  'alphav/v5.0.0',
  'minor/1.99.0',
  'nomatch-v1.0.0',
  'nomatch/x1.0.0',
  'nomatchx/v1.0.0',
  'release-2026.10',
];

function gitVersionsort() {
  const sets = VERSIONSORT_SETS.map(([id, prefix, covers, versions]) => ({
    id,
    prefix,
    name: prefix[0].toUpperCase() + prefix.slice(1),
    covers,
    tags: versions.map((v) => `${prefix}/v${v}`),
  }));
  return { 'git-versionsort.json': json({ unrelated: VERSIONSORT_UNRELATED, sets }) };
}

// --- set: gen-feed case inputs (FR-059, SC-002, SC-003, SC-010, SC-022 to SC-027; plan §4.7; tasks T015, T016).
// Goldens (expected.*) are not built here: gen-feed.js writes them once and they are reviewed by hand
// (fixtures/README.md).

const GF_URL = (name) => `https://krateaudio.com/${name.toLowerCase()}/`;
const appIdOf = (guid) => guidKey(guid).slice(0, -'_is1'.length); // {0A1F0000-...-00000000000n}

/** A plugin's version.json in the repo's shape (plugins/<p>/version.json); identifier / preset_subdir when given. */
function gfVersionJson(name, version, { identifier, presetSubdir } = {}) {
  const v = { version, name, description: `Krate fixture plugin ${name}`, publisher: 'Krate Audio', url: GF_URL(name) };
  if (identifier !== undefined) v.identifier = identifier;
  if (presetSubdir !== undefined) v.preset_subdir = presetSubdir;
  return v;
}

/** The lines of an Inno Setup script gen-feed reads (FR-043 check 2): AppId with Inno's doubled opening brace. */
const gfSetupIss = (name, guid) =>
  `; Krate fixture installer script (a gen-feed case input, not an installer)\n[Setup]\nAppId={${appIdOf(guid)}\nAppName=${name}\n`;

/** A CHANGELOG.md in the repo's shape: `## [<version>] - <date>` headers, newest first. sections: [[version, body]]. */
function gfChangelog(name, sections) {
  let s = `# Changelog\n\nAll notable changes to ${name} are documented in this file.\n`;
  sections.forEach(([version, body], i) => {
    s += `\n## [${version}] - 2026-10-${String(20 - i).padStart(2, '0')}\n\n${body}\n`;
  });
  return s;
}

/**
 * One plugin of a case: dir = name lower-cased; every plugin carries a setup.iss with its own fixture AppId (guid).
 * Reject / degrade overrides: `dir` (another directory name), `url` (any value, even a non-string, as version.json
 * `url`), `setupIss` (the script's text, or null for no setup.iss at all).
 * @returns {{dir, versionJson, changelog?, setupIss: string | null}}
 */
function gfPlugin(name, version, guid, { identifier, presetSubdir, changelog, dir, url, setupIss } = {}) {
  const p = { dir: dir ?? name.toLowerCase(), versionJson: gfVersionJson(name, version, { identifier, presetSubdir }) };
  if (url !== undefined) p.versionJson.url = url;
  if (changelog !== undefined) p.changelog = changelog;
  p.setupIss = setupIss !== undefined ? setupIss : gfSetupIss(name, guid);
  return p;
}

/**
 * Release rows, keys in the order the snapshot's `gh release list --json` output has them; publishedAt one day apart.
 * A draft gets a placeholder date: gen-feed drops drafts before it reads publishedAt.
 */
function gfReleases(rows) {
  return rows.map(([tagName, { pre = false, draft = false } = {}], i) => ({
    isDraft: draft,
    isPrerelease: pre,
    publishedAt: draft ? '0001-01-01T00:00:00Z' : `2026-10-${String(i + 1).padStart(2, '0')}T12:00:00Z`,
    tagName,
  }));
}

/** A manifest (C2) for the plugin's release, its manifests/ file name `<name lowercased>-<version>.json`. */
function gfManifest(name, version, guid, { darwinKey = 'darwin-aarch64', presetSubdir, pkgId } = {}) {
  const doc = releaseFor(name, version, darwinKey, { guid, presetSubdir });
  if (pkgId !== undefined) doc.install.macos_pkg_id = pkgId;
  return [`${name.toLowerCase()}-${version}.json`, doc];
}

const SECTION = Object.freeze({
  short: (n) => `### Fixed\n\n- Fixture fix number ${n}.`,
  added: '### Added\n\n- A fixture feature.\n\n### Changed\n\n- A fixture change.',
});

/** gfManifest, then change(doc, assets) on the fresh copy (a degrade case's one bad field). */
function gfManifestWith(name, version, guid, opts, change) {
  const [file, doc] = gfManifest(name, version, guid, opts);
  change(doc, doc.assets);
  return [file, doc];
}

// pre-plain and pre-plain-promoted share these manifest bytes (SC-022: promotion leaves release.json untouched).
const PRE_PLAIN_MANIFEST = () => gfManifest('Alpha', '1.2.0', 1);

// --- gen-feed reject cases (SC-010, FR-046 abort class; tasks T016). exit 1, `error` = the first stderr line's code.

const rejectCase = (error, inputs) => ({ caseJson: { group: 'reject', modes: ['v2'], exit: 1, error }, manifests: [], ...inputs });
const latestUrl = (asset) => `https://github.com/${REPO}/releases/latest/download/${asset}`;
// `git ls-remote --tags origin` output, not bare tag names: a `<sha>\trefs/tags/<tag>` line and its peeled `^{}` line.
const LS_REMOTE_TAGS = `${'a'.repeat(40)}\trefs/tags/alpha/v1.0.0\n${'b'.repeat(40)}\trefs/tags/alpha/v1.0.0^{}\n`;

const GF_REJECT_CASES = {
  'reject-v1-type': rejectCase('E_V1_TYPE', { plugins: [gfPlugin('Alpha', '1.0.0', 1, { url: 42 })], releases: [['alpha/v1.0.0']] }),
  'reject-url-latest': rejectCase('E_URL_LATEST', {
    plugins: [gfPlugin('Alpha', '1.0.0', 1, { url: latestUrl('Alpha-1.0.0-Windows-x64.exe') })],
    releases: [['alpha/v1.0.0']],
  }),
  'reject-url-http': rejectCase('E_URL_NOT_HTTPS', {
    plugins: [gfPlugin('Alpha', '1.0.0', 1, { url: 'http://krateaudio.com/alpha/' })],
    releases: [['alpha/v1.0.0']],
  }),
  // SC-022d: a suffixed version on a release whose prerelease flag is not set.
  'reject-tag-suffix': rejectCase('E_TAG_FORMAT', {
    plugins: [gfPlugin('Alpha', '1.2.0', 1)],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0-rc1']],
  }),
  'reject-tag-ten-digits': rejectCase('E_TAG_FORMAT', {
    plugins: [gfPlugin('Alpha', '1.0.0', 1)],
    releases: [['alpha/v1.0.0'], ['alpha/v4294967296.0.0']],
  }),
  'reject-plugin-name': rejectCase('E_PLUGIN_NAME', { plugins: [gfPlugin('alpha', '1.0.0', 1)], releases: [['alpha/v1.0.0']] }),
  // PD-14; the directory is not named "nul": Windows cannot create a folder with a reserved device name.
  'reject-reserved-name': rejectCase('E_PLUGIN_NAME', {
    plugins: [gfPlugin('Nul', '1.0.0', 1, { dir: 'reserved' })],
    releases: [['nul/v1.0.0']],
  }),
  'reject-duplicate-plugin': rejectCase('E_DUPLICATE_PLUGIN', {
    plugins: [gfPlugin('Alpha', '1.0.0', 1, { dir: 'alpha1' }), gfPlugin('Alpha', '1.1.0', 2, { dir: 'alpha2' })],
    releases: [['alpha/v1.0.0'], ['alpha/v1.1.0']],
  }),
  // PD-13: Alpha and ALPHA name one folder on NTFS and default APFS, and select the same alpha/v* tags.
  'duplicate-plugin-case': rejectCase('E_DUPLICATE_PLUGIN', {
    plugins: [gfPlugin('Alpha', '1.0.0', 1, { dir: 'alpha1' }), gfPlugin('ALPHA', '1.0.0', 2, { dir: 'alpha2' })],
    releases: [['alpha/v1.0.0']],
  }),
  'reject-input-ls-remote': {
    ...rejectCase('E_INPUT', { plugins: [gfPlugin('Alpha', '1.0.0', 1)], releases: [['alpha/v1.0.0']], tagsText: LS_REMOTE_TAGS }),
    caseJson: { group: 'reject', modes: ['legacy'], exit: 1, error: 'E_INPUT' },
  },
  // A malformed row (isPrerelease is a string): valid JSON, so the first stderr line holds no parser message or path.
  'reject-input-releases': rejectCase('E_INPUT', {
    plugins: [gfPlugin('Alpha', '1.0.0', 1)],
    releasesJson: [{ isDraft: false, isPrerelease: 'false', publishedAt: '2026-10-01T12:00:00Z', tagName: 'alpha/v1.0.0' }],
  }),
  // FR-049: an abort-class error beside a manifest that would degrade Bravo; abort wins, nothing is written.
  'reject-abort-plus-degrade': rejectCase('E_V1_TYPE', {
    plugins: [gfPlugin('Alpha', '1.0.0', 1, { url: 42 }), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.0.0'], ['bravo/v1.0.0']],
    manifests: [gfManifestWith('Bravo', '1.0.0', 2, {}, (d, a) => (a[WIN].signature = a[LIN].signature))],
  }),
};

// --- gen-feed degrade cases (SC-023 to SC-025, FR-046 degrade class, FR-049; Clarifications 2026-10-30 Q-A DEGRADE and
// Q-C; tasks T016). exit 3; `warn` rows {code, plugin | null, file?} in stderr order.

const gfWarn = (code, plugin = 'Alpha', file) => (file === undefined ? { code, plugin } : { code, plugin, file });
const ALPHA_ISS = 'alpha/installers/windows/setup.iss';
const tcText = (name, version, key, asset) => ['krate', '1', name, version, key, asset].join(':');
const winSignature = (comment) => (d, a) => (a[WIN].signature = fixtureSignature(comment));
const WIN_ASSET = 'Alpha-1.2.0-Windows-x64.exe';

/** Alpha (version.json `version`) with release 1.2.0 and its manifest changed by `change`, beside a healthy Bravo. */
function alphaDegrade(warn, change = () => {}, { plugin = {}, manifest = {}, version = '1.2.0' } = {}) {
  return {
    caseJson: { group: 'degrade', exit: 3, warn },
    plugins: [gfPlugin('Alpha', version, 1, plugin), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0']],
    manifests: [gfManifestWith('Alpha', '1.2.0', 1, manifest, change), gfManifest('Bravo', '1.0.0', 2)],
  };
}

/** alphaDegrade whose Linux payload list gains `lines` (re-digested), so only the C4 rule fails. */
const alphaListing = (code, ...lines) => alphaDegrade([gfWarn(code)], relist((list) => [...list, ...lines]));

const GF_DEGRADE_CASES = {
  // FR-043 check 1 (version.json version equal to the manifest's): a dropped "Kits", another pkg id.
  'degrade-preset-subdir': alphaDegrade([gfWarn('E_MANIFEST_MISMATCH')], undefined, { plugin: { presetSubdir: 'Kits' } }),
  'degrade-pkg-id': alphaDegrade([gfWarn('E_MANIFEST_MISMATCH')], undefined, { manifest: { pkgId: 'com.krateaudio.alphaother' } }),
  // FR-043 check 2: the key against the setup.iss AppId; a script with no AppId= line; no script.
  'degrade-uninstall-key': alphaDegrade(
    [gfWarn('E_MANIFEST_MISMATCH', 'Alpha', ALPHA_ISS)],
    (d) => (d.install.windows_uninstall_key = guidKey(9)),
  ),
  'degrade-no-appid': alphaDegrade([gfWarn('E_MANIFEST_MISMATCH', 'Alpha', ALPHA_ISS)], undefined, {
    plugin: { setupIss: '; Krate fixture installer script (a gen-feed case input, not an installer)\n[Setup]\nAppName=Alpha\n' },
  }),
  'degrade-no-setup-iss': alphaDegrade([gfWarn('E_MANIFEST_MISMATCH', 'Alpha', ALPHA_ISS)], undefined, { plugin: { setupIss: null } }),
  'degrade-install-slash': alphaDegrade([gfWarn('E_INSTALL_VALUE')], (d) => (d.install.vst3_bundle = 'sub/Alpha.vst3')),
  'degrade-install-backslash': alphaDegrade([gfWarn('E_INSTALL_VALUE')], (d) => (d.install.vst3_bundle = 'sub\\Alpha.vst3')),
  'degrade-install-dotdot': alphaDegrade([gfWarn('E_INSTALL_VALUE')], (d) => (d.install.vst3_bundle = 'Alpha..vst3')),
  'degrade-signature-text': alphaDegrade(
    [gfWarn('E_SIGNATURE_TEXT')],
    (d, a) => (a[WIN].signature = editSigLine(a[WIN].signature, 1, () => blobBase64('ED', 73))),
  ),
  'degrade-tc-fields': alphaDegrade([gfWarn('E_TC_FIELDS')], winSignature('krate:1:Alpha:1.2.0')),
  'degrade-tc-name': alphaDegrade([gfWarn('E_TC_NAME')], winSignature(tcText('alpha', '1.2.0', WIN, WIN_ASSET))),
  'degrade-tc-version': alphaDegrade([gfWarn('E_TC_VERSION')], winSignature(tcText('Alpha', '1.2', WIN, WIN_ASSET))),
  'degrade-tc-platform': alphaDegrade([gfWarn('E_TC_PLATFORM')], winSignature(tcText('Alpha', '1.2.0', 'windows-aarch64', WIN_ASSET))),
  'degrade-tc-asset': alphaDegrade([gfWarn('E_TC_ASSET')], winSignature(tcText('Alpha', '1.2.0', WIN, '..'))),
  // The Windows asset carries the Linux asset's signature: its comment binds another platform and asset.
  'degrade-tc-binding': alphaDegrade([gfWarn('E_TC_BINDING')], (d, a) => (a[WIN].signature = a[LIN].signature)),
  'degrade-listing-dotdot': alphaDegrade([gfWarn('E_LISTING_DOTDOT')], relist((list) => [...list, 'f 0644 presets/../escape.txt'])),
  'degrade-listing-not-canonical': alphaDegrade([gfWarn('E_LISTING_NOT_CANONICAL')], relist((list) => list.reverse())),
  'degrade-listing-count': alphaDegrade([gfWarn('E_LISTING_COUNT')], (d, a) => (a[LIN].payload.files += 1)),
  'degrade-listing-digest': alphaDegrade(
    [gfWarn('E_LISTING_DIGEST')],
    (d, a) => (a[LIN].payload.sha256 = sha256Hex('krate fixture wrong digest\n')),
  ),
  // The other C4 path / symlink codes checkCanonicalListing can raise (FR-046, FR-059): the list plus these lines
  // (re-digested). E_LISTING_TYPE / _CHECKSUM / _TRUNCATED / _CORRUPT cannot come from a listing line (CONTRACT.md).
  'degrade-listing-path': alphaListing('E_LISTING_PATH', 'f 0644 presets/escape -> txt'),
  'degrade-listing-absolute': alphaListing('E_LISTING_ABSOLUTE', 'f 0644 /escape.txt'),
  'degrade-listing-drive': alphaListing('E_LISTING_DRIVE', 'f 0644 C:/escape.txt'),
  'degrade-listing-backslash': alphaListing('E_LISTING_BACKSLASH', 'f 0644 presets\\escape.txt'),
  'degrade-listing-duplicate': alphaListing('E_LISTING_DUPLICATE', 'f 0644 README.txt'),
  'degrade-listing-root-symlink': alphaListing('E_LISTING_ROOT_SYMLINK', 'l 0777 escape -> README.txt'),
  'degrade-listing-symlink-form': alphaListing('E_LISTING_SYMLINK_FORM', 'l 0777 presets/link -> ./Default.preset'),
  'degrade-listing-symlink-escape': alphaListing('E_LISTING_SYMLINK_ESCAPE', 'l 0777 presets/link -> ../README.txt'),
  'degrade-listing-symlink-chain': alphaListing(
    'E_LISTING_SYMLINK_CHAIN',
    'l 0777 presets/link-a -> link-b',
    'l 0777 presets/link-b -> Default.preset',
  ),
  'degrade-listing-through-symlink': alphaListing(
    'E_LISTING_THROUGH_SYMLINK',
    'l 0777 presets/link -> Default.preset',
    'f 0644 presets/link/escape.txt',
  ),
  'degrade-layout': alphaDegrade([gfWarn('E_LAYOUT')], relist((list) => list.filter((l) => l !== 'f 0644 CHANGELOG.txt'))),
  // URLs in a release.json degrade (the version.json url aborts, reject-url-*).
  'degrade-url-http': alphaDegrade([gfWarn('E_URL_NOT_HTTPS')], (d, a) => (a[WIN].url = a[WIN].url.replace('https://', 'http://'))),
  'degrade-url-latest': alphaDegrade([gfWarn('E_URL_LATEST')], (d, a) => (a[WIN].url = latestUrl(a[WIN].name))),
  'degrade-url-not-per-tag': alphaDegrade(
    [gfWarn('E_URL_NOT_PER_TAG')],
    (d, a) => (a[WIN].url = contract.perTagUrl(REPO, 'alpha/v1.1.0', a[WIN].name)),
  ),
  // The sibling's manifest fails: the entry and the sibling both lose platforms and install.
  'degrade-sibling-manifest': {
    caseJson: { group: 'degrade', exit: 3, warn: [gfWarn('E_TC_BINDING')] },
    plugins: [gfPlugin('Alpha', '1.1.0', 1), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0-beta.1', { pre: true }], ['bravo/v1.0.0']],
    manifests: [
      gfManifest('Alpha', '1.1.0', 1),
      gfManifestWith('Alpha', '1.2.0-beta.1', 1, {}, (d, a) => (a[WIN].signature = a[LIN].signature)),
      gfManifest('Bravo', '1.0.0', 2),
    ],
  },
  // plan §4.7 (PD-10): C2 requires every install name; version.json ahead, so check 1 cannot catch it first.
  'degrade-c2-missing-pkg-id': alphaDegrade([gfWarn('E_MANIFEST_SCHEMA')], (d) => delete d.install.macos_pkg_id, { version: '1.3.0' }),
  'degrade-c2-wrong-format': alphaDegrade([gfWarn('E_MANIFEST_SCHEMA')], (d, a) => (a[WIN].format = 'pkg')),
  // FR-014 through gen-feed (Clarifications 2026-10-30 Q-C): a copied AppId, a shared identifier; both plugins
  // degrade, the third (Charlie) stays healthy.
  'degrade-dup-uninstall-key': {
    caseJson: { group: 'degrade', exit: 3, warn: [gfWarn('E_INSTALL_DUPLICATE', 'Alpha'), gfWarn('E_INSTALL_DUPLICATE', 'Bravo')] },
    plugins: [gfPlugin('Alpha', '1.2.0', 1), gfPlugin('Bravo', '1.0.0', 1), gfPlugin('Charlie', '1.0.0', 3)],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0'], ['charlie/v1.0.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1), gfManifest('Bravo', '1.0.0', 1), gfManifest('Charlie', '1.0.0', 3)],
  },
  'degrade-dup-pkg-id': {
    caseJson: { group: 'degrade', exit: 3, warn: [gfWarn('E_INSTALL_DUPLICATE', 'Alpha'), gfWarn('E_INSTALL_DUPLICATE', 'Bravo')] },
    plugins: [
      gfPlugin('Alpha', '1.2.0', 1, { identifier: 'com.krateaudio.shared' }),
      gfPlugin('Bravo', '1.0.0', 2, { identifier: 'com.krateaudio.shared' }),
      gfPlugin('Charlie', '1.0.0', 3),
    ],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0'], ['charlie/v1.0.0']],
    manifests: [
      gfManifest('Alpha', '1.2.0', 1, { pkgId: 'com.krateaudio.shared' }),
      gfManifest('Bravo', '1.0.0', 2, { pkgId: 'com.krateaudio.shared' }),
      gfManifest('Charlie', '1.0.0', 3),
    ],
  },
  // Bravo's manifest carries Alpha's bundle names: FR-017 binds them to the plugin name, so only Bravo degrades.
  'degrade-copied-bundle-name': {
    caseJson: { group: 'degrade', exit: 3, warn: [gfWarn('E_MANIFEST_SCHEMA', 'Bravo')] },
    plugins: [gfPlugin('Alpha', '1.2.0', 1), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0']],
    manifests: [
      gfManifest('Alpha', '1.2.0', 1),
      gfManifestWith('Bravo', '1.0.0', 2, {}, (d) => {
        d.install.vst3_bundle = 'Alpha.vst3';
        d.install.au_component = 'Alpha.component';
      }),
    ],
  },
  // PD-8 (Q-A DEGRADE): a manifest file that is not JSON is ignored with a WARN naming the file.
  'degrade-manifest-unparseable': {
    caseJson: { group: 'degrade', exit: 3, warn: [gfWarn('E_MANIFEST_SCHEMA', null, 'manifests/broken.json')] },
    plugins: [gfPlugin('Alpha', '1.2.0', 1)],
    releases: [['alpha/v1.2.0']],
    manifests: [['broken.json', '{"tag": '], gfManifest('Alpha', '1.2.0', 1)],
  },
  // PD-8: two files claim alpha/v1.2.0; both are dropped and Alpha degrades with one WARN naming both.
  'degrade-manifest-duplicate-tag': {
    caseJson: { group: 'degrade', exit: 3, warn: [gfWarn('E_MANIFEST_DUPLICATE')] },
    plugins: [gfPlugin('Alpha', '1.2.0', 1), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1), ['alpha-1.2.0-copy.json', gfManifest('Alpha', '1.2.0', 1)[1]], gfManifest('Bravo', '1.0.0', 2)],
  },
};

/**
 * The gen-feed case inputs: name -> {caseJson, plugins, releases | releasesJson, tagsText?, manifests: [[file, doc |
 * raw text]]}. Groups clean and prerelease (T015) here; reject and degrade (T016) from the two tables above.
 */
const GF_CASES = Object.freeze({
  'clean-basic': {
    caseJson: { group: 'clean' },
    plugins: [gfPlugin('Alpha', '2.0.0', 1), gfPlugin('Bravo', '1.10.0', 2), gfPlugin('Charlie', '1.0.0', 3)],
    releases: [
      ['alpha/v1.9.0'],
      ['alpha/v1.10.0'],
      ['alpha/v2.0.0'],
      ['bravo/v1.9.0'],
      ['bravo/v1.10.0'],
      ['v0.8.0'],
      ['manager/v0.1.0'],
    ],
    manifests: [gfManifest('Alpha', '2.0.0', 1)],
  },
  'clean-identifier': {
    caseJson: { group: 'clean' },
    plugins: [gfPlugin('Alpha', '1.2.0', 1, { identifier: 'com.krateaudio.alphasynth' }), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1, { pkgId: 'com.krateaudio.alphasynth' })],
  },
  'clean-changelog': {
    caseJson: { group: 'clean' },
    plugins: [
      gfPlugin('Alpha', '1.2.0', 1, { changelog: gfChangelog('Alpha', [['1.2.0', SECTION.added], ['1.1.0', SECTION.short(1)]]) }),
      // Bravo's CHANGELOG has no section for its released version 1.0.0: no notes_markdown.
      gfPlugin('Bravo', '1.0.0', 2, { changelog: gfChangelog('Bravo', [['0.9.0', SECTION.short(2)]]) }),
    ],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0'], ['bravo/v1.0.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1)],
  },
  'clean-long-section': {
    caseJson: { group: 'clean' },
    plugins: [
      gfPlugin('Alpha', '1.2.0', 1, {
        changelog: gfChangelog('Alpha', [['1.3.0', SECTION.short(3)], ['1.2.0', notes14000()], ['1.1.0', SECTION.short(1)]]),
      }),
    ],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1)],
  },
  'clean-preset-subdir': {
    caseJson: { group: 'clean' },
    plugins: [gfPlugin('Alpha', '1.2.0', 1, { presetSubdir: 'Kits' }), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1, { presetSubdir: 'Kits' })],
  },
  // version.json is ahead (1.3.0) of the newest release (1.2.0), and its identifier and preset_subdir differ from the
  // manifest's: check 1 is skipped (SC-024b); check 2 still holds.
  'clean-version-ahead': {
    caseJson: { group: 'clean' },
    plugins: [gfPlugin('Alpha', '1.3.0', 1, { identifier: 'com.krateaudio.alphanext', presetSubdir: 'Presets' })],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1, { presetSubdir: 'Kits' })],
  },
  'tenth-plugin': {
    caseJson: { group: 'clean' },
    plugins: ['Alpha', 'Bravo', 'Charlie', ...TENTH_NAMES].map((n, i) => gfPlugin(n, `1.${i}.0`, i + 1)),
    releases: ['Alpha', 'Bravo', 'Charlie', ...TENTH_NAMES].map((n, i) => [`${n.toLowerCase()}/v1.${i}.0`]),
    manifests: [gfManifest('Alpha', '1.0.0', 1), gfManifest('Juliet', '1.9.0', 10, { darwinKey: 'darwin-universal' })],
  },
  'manager-block': {
    caseJson: { group: 'clean', managerLatestUrl: MANAGER_LATEST_URL },
    plugins: [gfPlugin('Alpha', '1.2.0', 1), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.2.0'], ['bravo/v1.0.0']],
    manifests: [gfManifest('Alpha', '1.2.0', 1)],
  },
  // pre-plain with the GitHub prerelease flag unticked: same manifest bytes, now the stable entry (SC-022c).
  'pre-plain-promoted': {
    caseJson: { group: 'clean' },
    plugins: [gfPlugin('Alpha', '1.2.0', 1)],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0']],
    manifests: [PRE_PLAIN_MANIFEST()],
  },
  // SC-003 rows; legacyExpect is the table's legacy column.
  'pre-newer': {
    caseJson: { group: 'prerelease', legacyExpect: { plugin: 'Alpha', version: '1.2.0-beta.1' } },
    plugins: [
      gfPlugin('Alpha', '1.1.0', 1, {
        changelog: gfChangelog('Alpha', [['1.2.0-beta.1', SECTION.added], ['1.1.0', SECTION.short(1)]]),
      }),
    ],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0-beta.1', { pre: true }]],
    // The sibling's manifest says preset_subdir "Kits", version.json has none: check 1 is skipped, the versions differ.
    manifests: [gfManifest('Alpha', '1.2.0-beta.1', 1, { presetSubdir: 'Kits' })],
  },
  'pre-same-core': {
    caseJson: { group: 'prerelease', legacyExpect: { plugin: 'Alpha', version: '1.1.0-beta.1' } },
    plugins: [gfPlugin('Alpha', '1.1.0', 1)],
    releases: [['alpha/v1.1.0-beta.1', { pre: true }], ['alpha/v1.1.0']],
    manifests: [],
  },
  'pre-lower-core': {
    caseJson: { group: 'prerelease', legacyExpect: { plugin: 'Alpha', version: '1.1.0' } },
    plugins: [gfPlugin('Alpha', '1.1.0', 1)],
    releases: [['alpha/v1.0.0-beta.1', { pre: true }], ['alpha/v1.1.0']],
    manifests: [],
  },
  'pre-only': {
    caseJson: { group: 'prerelease', legacyExpect: { plugin: 'Alpha', version: '1.0.0-beta.2' } },
    plugins: [gfPlugin('Alpha', '1.0.0', 1), gfPlugin('Bravo', '1.0.0', 2)],
    releases: [['alpha/v1.0.0-beta.1', { pre: true }], ['alpha/v1.0.0-beta.2', { pre: true }], ['bravo/v1.0.0']],
    manifests: [],
  },
  'draft-above': {
    caseJson: { group: 'prerelease', legacyExpect: { plugin: 'Alpha', version: '1.2.0' } },
    plugins: [gfPlugin('Alpha', '1.1.0', 1)],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0', { draft: true }]],
    manifests: [],
  },
  'pre-plain': {
    caseJson: { group: 'prerelease', legacyExpect: { plugin: 'Alpha', version: '1.2.0' } },
    plugins: [gfPlugin('Alpha', '1.2.0', 1)],
    releases: [['alpha/v1.1.0'], ['alpha/v1.2.0', { pre: true }]],
    manifests: [PRE_PLAIN_MANIFEST()],
  },
  ...GF_REJECT_CASES,
  ...GF_DEGRADE_CASES,
});

function genFeedCases() {
  const files = {};
  for (const [name, c] of Object.entries(GF_CASES)) {
    const base = `gen-feed/${name}`;
    const releases = c.releasesJson ?? gfReleases(c.releases);
    // plan §4.7 case.json shape and key order.
    const { group, modes = ['v2', 'legacy'], managerLatestUrl, exit = 0, error, warn, legacyExpect } = c.caseJson;
    const caseJson = { group, modes, repo: REPO };
    if (managerLatestUrl !== undefined) caseJson.managerLatestUrl = managerLatestUrl;
    caseJson.exit = exit;
    if (error !== undefined) caseJson.error = error;
    if (warn !== undefined) caseJson.warn = warn;
    if (legacyExpect !== undefined) caseJson.legacyExpect = legacyExpect;
    files[`${base}/case.json`] = json(caseJson);
    for (const p of c.plugins) {
      files[`${base}/plugins/${p.dir}/version.json`] = json(p.versionJson);
      if (p.changelog !== undefined) files[`${base}/plugins/${p.dir}/CHANGELOG.md`] = p.changelog;
      if (p.setupIss !== null) files[`${base}/plugins/${p.dir}/installers/windows/setup.iss`] = p.setupIss;
    }
    files[`${base}/releases.json`] = json(releases);
    // FR-059: a case's tags.txt is its releases.json tag names, drafts included (reject-input-ls-remote: raw
    // `git ls-remote` output instead).
    files[`${base}/tags.txt`] = c.tagsText ?? releases.map((r) => r.tagName).join('\n') + '\n';
    for (const [file, doc] of c.manifests) {
      files[`${base}/manifests/${file}`] = typeof doc === 'string' ? doc : json(doc);
    }
  }
  return files;
}

// --- registry: each set owns the paths its files() returns ({relPath: content}) and every file under its dirs
// (except paths matching its `keep` pattern, which are committed but not built: the gen-feed goldens)

const SETS = Object.freeze([
  { name: 'trusted-comments', dirs: [], files: trustedComments },
  { name: 'feeds', dirs: ['feeds'], files: feeds },
  { name: 'release', dirs: ['release'], files: release },
  { name: 'git-versionsort', dirs: [], files: gitVersionsort },
  {
    name: 'gen-feed',
    dirs: Object.keys(GF_CASES).map((name) => `gen-feed/${name}`),
    keep: /^gen-feed\/[^/]+\/expected\.[^/]+$/u,
    files: genFeedCases,
  },
  { name: 'phase3', dirs: ['phase3'], keep: /^phase3\/make-phase3\.js$/u, files: () => phase3.files({ fixtureSignature }) },
]);

function builtFiles() {
  const all = new Map();
  for (const set of SETS) {
    for (const [rel, content] of Object.entries(set.files())) {
      if (all.has(rel)) throw new Error(`make-fixtures: ${rel} is owned by two sets`);
      all.set(rel, Buffer.from(content, 'utf8'));
    }
  }
  return all;
}

/** Writes every fixture set under outDir. */
function build(outDir) {
  for (const [rel, content] of builtFiles()) {
    const target = path.join(outDir, ...rel.split('/'));
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, content);
  }
}

function listRecursive(dir, base = dir) {
  if (!fs.existsSync(dir)) return [];
  const out = [];
  for (const d of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, d.name);
    if (d.isDirectory()) out.push(...listRecursive(full, base));
    else out.push(path.relative(base, full).split(path.sep).join('/'));
  }
  return out.sort();
}

/** Builds into a temp dir and compares with committedDir. diff names the first differing, missing or stray file. */
function check(committedDir = FIXTURES_DIR) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km1-fixtures-'));
  try {
    build(tmp);
    const built = listRecursive(tmp);
    for (const rel of built) {
      const committed = path.join(committedDir, ...rel.split('/'));
      if (!fs.existsSync(committed)) return { ok: false, diff: `missing: ${rel}` };
      if (!fs.readFileSync(committed).equals(fs.readFileSync(path.join(tmp, ...rel.split('/'))))) {
        return { ok: false, diff: `differs: ${rel}` };
      }
    }
    const builtSet = new Set(built);
    for (const set of SETS) {
      for (const dir of set.dirs) {
        for (const rel of listRecursive(path.join(committedDir, ...dir.split('/')))) {
          const full = `${dir}/${rel}`;
          if (!builtSet.has(full) && !set.keep?.test(full)) return { ok: false, diff: `stray: ${full}` };
        }
      }
    }
    return { ok: true, diff: null };
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

function main(argv) {
  const usage = 'usage: node tools/feed-contract/fixtures/make-fixtures.js --out <dir> | --check';
  if (argv.length === 1 && argv[0] === '--check') {
    const { ok, diff } = check();
    console.log(ok ? 'make-fixtures: committed fixtures match the builder' : `make-fixtures: DRIFT ${diff}`);
    return ok ? 0 : 1;
  }
  if (argv.length === 2 && argv[0] === '--out' && argv[1]) {
    build(path.resolve(argv[1]));
    console.log(`make-fixtures: wrote ${builtFiles().size} file(s) under ${argv[1]}`);
    return 0;
  }
  console.error(usage);
  return 2;
}

if (require.main === module) {
  process.exitCode = main(process.argv.slice(2));
}

module.exports = { build, check, fixtureSignature, SETS };
