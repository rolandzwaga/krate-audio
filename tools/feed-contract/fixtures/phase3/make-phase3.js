// Builds the Phase 3 fixtures in memory (spec specs/krate-manager-phase3/spec.md FR-044, FR-045, SC-007, SC-008;
// plan §4.7, PD-14, PD-15). Registered as make-fixtures.js set `phase3`; never run on its own. Names no plugin.
//
// WHY: the mixed feed must be what gen-feed really writes for the 2026-10-29 snapshot plus one C2 manifest, and the
// compare cases must hold expected lines written down before tools/feed-publish.js exists. One builder, checked by
// `make-fixtures.js --check`, keeps both from drifting.
//
// files({fixtureSignature}) returns {relPath (under fixtures/): content} for:
//   - phase3/<lower>-v<version>.json: the C2 manifest of the one snapshot plugin whose version.json carries
//     preset_subdir (exactly one does; every name, the tag and the file name come from that file);
//   - feeds/valid/one-v2-eight-v1.json: gen-feed's buildFeed on the snapshot and that manifest;
//   - phase3/compare/cases.json and phase3/compare/<case>/{live,feed,releases}.json: the SC-007 triples, with invented
//     plugins Alpha, Bravo, Charlie. Every expected line is a literal string in CASES below (plan §4.2, §4.1.6, PD-13,
//     PD-18), never computed by feed-publish.js.
'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const contract = require('../../contract.js');
const listing = require('../../listing.js');
const genFeed = require('../../../gen-feed.js');

const REPO = 'rolandzwaga/krate-audio';
const SNAPSHOT_DIR = path.join(__dirname, '..', 'gen-feed', 'snapshot-2026-10-29');

const json = (value) => JSON.stringify(value, null, 2) + '\n';
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
const sha256Hex = (text) => crypto.createHash('sha256').update(text, 'utf8').digest('hex');

// --- the C2 manifest (FR-044), built as make-fixtures.js releaseAsset / platformFor build theirs

// The stated synthetic contents behind every size and sha256 (make-fixtures.js assetContent, fixtures/README.md).
const assetContent = (asset) => `krate fixture asset ${asset}\n`;
const FORMAT_BY_PREFIX = Object.freeze({ windows: 'inno-exe', darwin: 'pkg', linux: 'tar.gz' });
const MIN_OS_BY_PREFIX = Object.freeze({ darwin: '11.0', linux: '2.31', windows: '10.0' });
const MANIFEST_KEYS = Object.freeze(['darwin-universal', 'linux-x86_64', 'windows-x86_64']);

/** The five-entry C4 Linux tree of make-fixtures.js linuxTree; the .so is 0755. */
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

function manifestAsset(name, version, tag, key, fixtureSignature) {
  const prefix = key.slice(0, key.indexOf('-'));
  const asset = contract.assetNameFor(name, version, key);
  const out = {
    name: asset,
    url: contract.perTagUrl(REPO, tag, asset),
    size: Buffer.byteLength(assetContent(asset), 'utf8'),
    sha256: sha256Hex(assetContent(asset)),
    signature: fixtureSignature(contract.formatTrustedComment(name, version, key, asset)),
    format: FORMAT_BY_PREFIX[prefix],
    min_os: MIN_OS_BY_PREFIX[prefix],
  };
  if (out.format === 'tar.gz') {
    const list = listing.listingFromEntries(linuxTree(name));
    out.payload = { files: list.length, sha256: listing.digest(list), list };
  }
  return out;
}

/** The snapshot inputs and the one plugin with preset_subdir. */
function snapshotInputs() {
  const plugins = genFeed.readPluginsDir(path.join(SNAPSHOT_DIR, 'plugins'), { withSetupIss: true });
  const releases = JSON.parse(fs.readFileSync(path.join(SNAPSHOT_DIR, 'releases.json'), 'utf8'));
  const withPresets = plugins.filter((p) => Object.hasOwn(p.versionJson, 'preset_subdir'));
  if (withPresets.length !== 1) {
    throw new Error(`make-phase3: expected exactly one snapshot version.json with preset_subdir, found ${withPresets.length}`);
  }
  return { plugins, releases, plugin: withPresets[0] };
}

function buildManifest({ plugin, releases }, fixtureSignature) {
  const { name, version, preset_subdir: presetSubdir } = plugin.versionJson;
  const lower = name.toLowerCase();
  const tag = `${lower}/v${version}`;
  const row = releases.find((r) => r.tagName === tag);
  if (!row || row.isDraft || row.isPrerelease) throw new Error(`make-phase3: no stable snapshot release ${tag}`);
  const selection = genFeed.selectReleases(name, releases);
  if (selection === null || selection.stable.tagName !== tag) {
    throw new Error(`make-phase3: ${tag} is not the stable release gen-feed selects`);
  }
  const appId = genFeed.extractAppId(plugin.setupIss);
  if (appId === undefined) throw new Error(`make-phase3: ${plugin.dir}/installers/windows/setup.iss has no AppId`);
  const assets = {};
  for (const key of [...MANIFEST_KEYS].sort(bytewise)) assets[key] = manifestAsset(name, version, tag, key, fixtureSignature);
  const doc = {
    tag,
    name,
    version,
    pub_date: row.publishedAt,
    assets,
    install: {
      vst3_bundle: `${name}.vst3`,
      au_component: `${name}.component`,
      preset_subdir: presetSubdir,
      windows_uninstall_key: `${appId}_is1`,
      macos_pkg_id: `com.krateaudio.${lower}`,
    },
  };
  contract.checkReleaseManifest(doc, { repo: REPO });
  return { rel: `${lower}-v${version}.json`, text: json(doc) };
}

// --- the SC-007 compare triples (FR-027, FR-040, FR-041; plan §4.2, PD-10, PD-11, PD-13, PD-18)

// publishedAt of every invented release, so a feed entry's pub_date is the row's.
const AT = Object.freeze({
  'alpha/v1.0.0': '2026-09-01T10:00:00Z',
  'alpha/v1.1.0': '2026-10-01T10:00:00Z',
  'alpha/v1.2.0-beta.1': '2026-10-15T10:00:00Z',
  'bravo/v2.0.0': '2026-08-20T09:30:00Z',
  'bravo/v2.0.1': '2026-10-28T18:45:12Z',
  'charlie/v0.1.0-beta.1': '2026-07-04T12:00:00Z',
  'charlie/v0.9.0': '2026-10-20T08:15:00Z',
});

/** A `gh release list` row in the snapshot's key order. */
const row = (tagName) => ({
  isDraft: false,
  isPrerelease: tagName.includes('-'),
  publishedAt: AT[tagName],
  tagName,
});
const rows = (...tags) => tags.map(row);
const R_BASE = Object.freeze(['alpha/v1.0.0', 'alpha/v1.1.0', 'bravo/v2.0.0']);

const site = (name) => `https://krateaudio.com/${name.toLowerCase()}/`;
const notesUrl = (tag) => `https://github.com/${REPO}/releases/tag/${tag}`;

/** A live v1 entry (today's inline generator's three keys). */
const v1 = (version, { url, notes = '' } = {}) => ({ version, download_url: url, release_notes: notes });

/** A v2 entry with the v1-only shape of the snapshot's expected.v2.json, plus an optional prerelease sibling. */
function v2(name, version, { url = site(name), sibling, channel = 'stable' } = {}) {
  const tag = `${name.toLowerCase()}/v${version}`;
  const entry = {
    version,
    download_url: url,
    release_notes: '',
    tag,
    pub_date: AT[tag],
    channel,
    release_notes_url: notesUrl(tag),
  };
  if (sibling !== undefined) {
    const siblingTag = `${name.toLowerCase()}/v${sibling}`;
    entry.prerelease = { tag: siblingTag, pub_date: AT[siblingTag], channel: 'beta', release_notes_url: notesUrl(siblingTag) };
  }
  return entry;
}

const liveV1 = (plugins) => ({ schema_version: 1, plugins });
const feedV2 = (plugins) => ({ schema_version: 2, plugins });

const A = 'https://krateaudio.com/alpha/';
const A2 = 'https://krateaudio.com/alpha-plugin/';
const B = 'https://krateaudio.com/bravo/';
const B2 = 'https://krateaudio.com/bravo-plugin/';
const C = 'https://krateaudio.com/charlie/';
const LF_KEY = 'X\n::notice title=feed-verdict::green';
const CR_KEY = 'X\r::notice title=feed-verdict::green';
const BACKTICK_KEY = 'X```y';

const VALID_OK = '::notice title=feed-validate::v2 ok, v1 ok';

// Inputs shared by a -real / -dry pair.
const IN = Object.freeze({
  releaseNotes: {
    live: liveV1({ Alpha: v1('1.1.0', { url: A, notes: 'Fixed a crash.' }) }),
    feed: feedV2({ Alpha: v2('Alpha', '1.1.0') }),
    releases: rows(...R_BASE),
  },
  versionLowered: {
    live: liveV1({ Alpha: v1('1.2.0', { url: A }) }),
    feed: feedV2({ Alpha: v2('Alpha', '1.1.0') }),
    releases: rows(...R_BASE),
  },
  removed: {
    live: liveV1({ Alpha: v1('1.1.0', { url: A }), Bravo: v1('2.0.0', { url: B }) }),
    feed: feedV2({ Alpha: v2('Alpha', '1.1.0') }),
    releases: rows(...R_BASE),
  },
  urlWithoutRelease: {
    live: liveV1({ Alpha: v1('1.1.0', { url: A }) }),
    feed: feedV2({ Alpha: v2('Alpha', '1.1.0', { url: A2 }) }),
    releases: rows(...R_BASE),
  },
  versionWithoutRelease: {
    live: liveV1({ Alpha: v1('1.0.0', { url: A }) }),
    feed: feedV2({ Alpha: v2('Alpha', '1.1.0') }),
    releases: rows('alpha/v1.0.0'),
  },
  addedWithoutRelease: {
    live: liveV1({ Alpha: v1('1.1.0', { url: A }) }),
    feed: feedV2({ Alpha: v2('Alpha', '1.1.0'), Charlie: v2('Charlie', '0.9.0') }),
    releases: rows('alpha/v1.0.0', 'alpha/v1.1.0'),
  },
  q9Feed: feedV2({ Alpha: v2('Alpha', '1.1.0', { sibling: '1.2.0-beta.1' }), Bravo: v2('Bravo', '2.0.0') }),
  aloneSame: feedV2({ Alpha: v2('Alpha', '1.1.0') }),
});

// [case, dryRun, exit, stdout (result lines), stderr, annotations (the --annotate lines after the result lines),
//  {live (object, or raw text), feed, releases}]
const CASES = [
  [
    'same',
    true,
    0,
    ['same Alpha 1.1.0', 'same Bravo 2.0.0', 'compare: 2 same, 0 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)'],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 2 same, 0 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }), Bravo: v1('2.0.0', { url: B }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0'), Bravo: v2('Bravo', '2.0.0') }),
      releases: rows(...R_BASE),
    },
  ],
  [
    'newer',
    true,
    0,
    [
      'newer Alpha 1.0.0 -> 1.1.0 (alpha/v1.1.0 2026-10-01T10:00:00Z)',
      'same Bravo 2.0.0',
      'compare: 1 same, 1 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)',
    ],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 1 same, 1 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      live: liveV1({ Alpha: v1('1.0.0', { url: A }), Bravo: v1('2.0.0', { url: B }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0'), Bravo: v2('Bravo', '2.0.0') }),
      releases: rows(...R_BASE),
    },
  ],
  [
    'newer-url',
    true,
    0,
    [
      'newer Alpha 1.0.0 -> 1.1.0 (alpha/v1.1.0 2026-10-01T10:00:00Z) download_url https://krateaudio.com/alpha/ -> https://krateaudio.com/alpha-plugin/',
      'compare: 0 same, 1 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)',
    ],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 0 same, 1 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      live: liveV1({ Alpha: v1('1.0.0', { url: A }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0', { url: A2 }) }),
      releases: rows(...R_BASE),
    },
  ],
  [
    'added',
    true,
    0,
    [
      'same Alpha 1.1.0',
      'added Charlie 0.9.0 (charlie/v0.9.0 2026-10-20T08:15:00Z)',
      'compare: 1 same, 0 newer, 1 added, 0 q9-corrected, 0 drift (0 block, 0 warn)',
    ],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 1 same, 0 newer, 1 added, 0 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0'), Charlie: v2('Charlie', '0.9.0') }),
      releases: rows('alpha/v1.0.0', 'alpha/v1.1.0', 'charlie/v0.9.0'),
    },
  ],
  [
    'q9-corrected-v1-live',
    true,
    0,
    [
      'q9-corrected Alpha 1.2.0-beta.1 -> 1.1.0 (prerelease alpha/v1.2.0-beta.1)',
      'same Bravo 2.0.0',
      'compare: 1 same, 0 newer, 0 added, 1 q9-corrected, 0 drift (0 block, 0 warn)',
    ],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 1 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      live: liveV1({ Alpha: v1('1.2.0-beta.1', { url: A }), Bravo: v1('2.0.0', { url: B }) }),
      feed: IN.q9Feed,
      releases: rows(...R_BASE, 'alpha/v1.2.0-beta.1'),
    },
  ],
  [
    'q9-corrected-v2-live',
    false,
    0,
    [
      'q9-corrected Alpha 1.2.0-beta.1 -> 1.1.0 (prerelease alpha/v1.2.0-beta.1)',
      'same Bravo 2.0.0',
      'compare: 1 same, 0 newer, 0 added, 1 q9-corrected, 0 drift (0 block, 0 warn)',
    ],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 1 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      // A schema_version 2 live file (projected like a v1 one, FR-040); its Alpha entry carries the prerelease version.
      live: feedV2({ Alpha: v2('Alpha', '1.2.0-beta.1'), Bravo: v2('Bravo', '2.0.0') }),
      feed: IN.q9Feed,
      releases: rows(...R_BASE, 'alpha/v1.2.0-beta.1'),
    },
  ],
  [
    'q9-corrected-absent',
    true,
    0,
    [
      'same Alpha 1.1.0',
      'q9-corrected Charlie 0.1.0-beta.1 -> absent (prerelease charlie/v0.1.0-beta.1)',
      'compare: 1 same, 0 newer, 0 added, 1 q9-corrected, 0 drift (0 block, 0 warn)',
    ],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 1 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }), Charlie: v1('0.1.0-beta.1', { url: C }) }),
      feed: IN.aloneSame,
      releases: rows('alpha/v1.0.0', 'alpha/v1.1.0', 'charlie/v0.1.0-beta.1'),
    },
  ],
  [
    // The Pages cache still serves the feed of one stable release ago: an explained difference.
    'edge-cache',
    false,
    0,
    [
      'same Alpha 1.1.0',
      'newer Bravo 2.0.0 -> 2.0.1 (bravo/v2.0.1 2026-10-28T18:45:12Z)',
      'compare: 1 same, 1 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)',
    ],
    [],
    [VALID_OK, '::notice title=feed-compare::compare: 1 same, 1 newer, 0 added, 0 q9-corrected, 0 drift (0 block, 0 warn)'],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }), Bravo: v1('2.0.0', { url: B }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0'), Bravo: v2('Bravo', '2.0.1') }),
      releases: rows(...R_BASE, 'bravo/v2.0.1'),
    },
  ],
  [
    'release-notes-not-empty-real',
    false,
    1,
    ['drift Alpha release_notes not empty block', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Alpha release_notes not empty block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Alpha release_notes not empty block',
    ],
    IN.releaseNotes,
  ],
  [
    'release-notes-not-empty-dry',
    true,
    1,
    ['drift Alpha release_notes not empty block', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Alpha release_notes not empty block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Alpha release_notes not empty block',
    ],
    IN.releaseNotes,
  ],
  [
    'version-lowered-real',
    false,
    1,
    ['drift Alpha version lowered block', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Alpha version lowered block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Alpha version lowered block',
    ],
    IN.versionLowered,
  ],
  [
    'version-lowered-dry',
    true,
    1,
    ['drift Alpha version lowered block', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Alpha version lowered block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Alpha version lowered block',
    ],
    IN.versionLowered,
  ],
  [
    'removed-real',
    false,
    1,
    ['same Alpha 1.1.0', 'drift Bravo removed block', 'compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Bravo removed block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Bravo removed block',
    ],
    IN.removed,
  ],
  [
    'removed-dry',
    true,
    1,
    ['same Alpha 1.1.0', 'drift Bravo removed block', 'compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Bravo removed block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Bravo removed block',
    ],
    IN.removed,
  ],
  [
    'url-without-release-real',
    false,
    3,
    [
      'drift Alpha download_url changed without a newer release warn',
      'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
    ],
    ['feed-publish: WARN E_V1_DRIFT: drift Alpha download_url changed without a newer release warn'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
      '::warning title=feed-drift::drift Alpha download_url changed without a newer release warn',
    ],
    IN.urlWithoutRelease,
  ],
  [
    'url-without-release-dry',
    true,
    1,
    [
      'drift Alpha download_url changed without a newer release warn',
      'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
    ],
    ['feed-publish: E_V1_DRIFT: drift Alpha download_url changed without a newer release warn'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
      '::error title=feed-drift::drift Alpha download_url changed without a newer release warn',
    ],
    IN.urlWithoutRelease,
  ],
  [
    'version-without-release-real',
    false,
    3,
    ['drift Alpha version changed without a release warn', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)'],
    ['feed-publish: WARN E_V1_DRIFT: drift Alpha version changed without a release warn'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
      '::warning title=feed-drift::drift Alpha version changed without a release warn',
    ],
    IN.versionWithoutRelease,
  ],
  [
    'version-without-release-dry',
    true,
    1,
    ['drift Alpha version changed without a release warn', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Alpha version changed without a release warn'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
      '::error title=feed-drift::drift Alpha version changed without a release warn',
    ],
    IN.versionWithoutRelease,
  ],
  [
    'added-without-release-real',
    false,
    3,
    [
      'same Alpha 1.1.0',
      'drift Charlie added without a release warn',
      'compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
    ],
    ['feed-publish: WARN E_V1_DRIFT: drift Charlie added without a release warn'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
      '::warning title=feed-drift::drift Charlie added without a release warn',
    ],
    IN.addedWithoutRelease,
  ],
  [
    'added-without-release-dry',
    true,
    1,
    [
      'same Alpha 1.1.0',
      'drift Charlie added without a release warn',
      'compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
    ],
    ['feed-publish: E_V1_DRIFT: drift Charlie added without a release warn'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
      '::error title=feed-drift::drift Charlie added without a release warn',
    ],
    IN.addedWithoutRelease,
  ],
  [
    // The live version looks like a prerelease but no prerelease row exists for it: not q9-corrected, a lowering.
    'version-lowered-no-prerelease',
    false,
    1,
    ['drift Alpha version lowered block', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift Alpha version lowered block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Alpha version lowered block',
    ],
    {
      live: liveV1({ Alpha: v1('1.2.0-beta.1', { url: A }) }),
      feed: IN.aloneSame,
      releases: rows(...R_BASE),
    },
  ],
  [
    'mixed-kinds-one-plugin',
    false,
    1,
    [
      'drift Alpha version lowered; download_url changed without a newer release block',
      'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
    ],
    ['feed-publish: E_V1_DRIFT: drift Alpha version lowered; download_url changed without a newer release block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift Alpha version lowered; download_url changed without a newer release block',
    ],
    {
      live: liveV1({ Alpha: v1('1.2.0', { url: A }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0', { url: A2 }) }),
      releases: rows(...R_BASE),
    },
  ],
  [
    'mixed-two-plugins',
    false,
    1,
    [
      'drift Alpha version lowered block',
      'drift Bravo download_url changed without a newer release warn',
      'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 2 drift (1 block, 1 warn)',
    ],
    [
      'feed-publish: E_V1_DRIFT: drift Alpha version lowered block',
      'feed-publish: WARN E_V1_DRIFT: drift Bravo download_url changed without a newer release warn',
    ],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 2 drift (1 block, 1 warn)',
      '::error title=feed-drift::drift Alpha version lowered block',
      '::warning title=feed-drift::drift Bravo download_url changed without a newer release warn',
    ],
    {
      live: liveV1({ Alpha: v1('1.2.0', { url: A }), Bravo: v1('2.0.0', { url: B }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0'), Bravo: v2('Bravo', '2.0.0', { url: B2 }) }),
      releases: rows(...R_BASE),
    },
  ],
  [
    // PD-11: a live version parseSemVer rejects is never compared; it lands in `version changed without a release`.
    'non-semver-live',
    false,
    3,
    ['drift Alpha version changed without a release warn', 'compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)'],
    ['feed-publish: WARN E_V1_DRIFT: drift Alpha version changed without a release warn'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 0 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (0 block, 1 warn)',
      '::warning title=feed-drift::drift Alpha version changed without a release warn',
    ],
    {
      live: liveV1({ Alpha: v1('1.1', { url: A }) }),
      feed: IN.aloneSame,
      releases: rows(...R_BASE),
    },
  ],
  [
    // The messages name the flag, never the path or the JSON parser's text, so the lines are literal.
    'live-not-json',
    true,
    1,
    [],
    ['feed-publish: E_INPUT: --live: not JSON'],
    [],
    { live: '{"schema_version": 1, "plugins": {\n', feed: IN.aloneSame, releases: rows(...R_BASE) },
  ],
  [
    'live-without-plugins',
    true,
    1,
    [],
    ['feed-publish: E_INPUT: --live: the feed has no "plugins" object'],
    [],
    { live: { schema_version: 1 }, feed: IN.aloneSame, releases: rows(...R_BASE) },
  ],
  [
    'feed-fails-v2',
    true,
    1,
    [],
    ['feed-publish: E_FEED_SCHEMA: v2 /plugins/Alpha/channel: /plugins/Alpha/channel must be equal to constant'],
    ['::error title=feed-validate::v2 /plugins/Alpha/channel: /plugins/Alpha/channel must be equal to constant'],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }) }),
      feed: feedV2({ Alpha: v2('Alpha', '1.1.0', { channel: 'beta' }) }),
      releases: rows(...R_BASE),
    },
  ],
  [
    // PD-18 (a): a live key that would plant a workflow command is refused before any result line exists.
    'live-control-lf',
    false,
    1,
    [],
    ['feed-publish: E_INPUT: live plugins."X\\n::notice title=feed-verdict::green" holds a control character'],
    [],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }), [LF_KEY]: v1('1.0.0', { url: A }) }),
      feed: IN.aloneSame,
      releases: rows(...R_BASE),
    },
  ],
  [
    'live-control-cr',
    false,
    1,
    [],
    ['feed-publish: E_INPUT: live plugins."X\\r::notice title=feed-verdict::green" holds a control character'],
    [],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }), [CR_KEY]: v1('1.0.0', { url: A }) }),
      feed: IN.aloneSame,
      releases: rows(...R_BASE),
    },
  ],
  [
    'live-control-bel',
    false,
    1,
    [],
    ['feed-publish: E_INPUT: live plugins."Alpha" holds a control character'],
    [],
    {
      live: liveV1({ Alpha: v1('1.1.0\u0007', { url: A }) }),
      feed: IN.aloneSame,
      releases: rows(...R_BASE),
    },
  ],
  [
    // PD-18 (b): a key that fails isPluginName is printed in its JSON.stringify form.
    'live-backtick',
    false,
    1,
    ['same Alpha 1.1.0', 'drift "X```y" removed block', 'compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)'],
    ['feed-publish: E_V1_DRIFT: drift "X```y" removed block'],
    [
      VALID_OK,
      '::notice title=feed-compare::compare: 1 same, 0 newer, 0 added, 0 q9-corrected, 1 drift (1 block, 0 warn)',
      '::error title=feed-drift::drift "X```y" removed block',
    ],
    {
      live: liveV1({ Alpha: v1('1.1.0', { url: A }), [BACKTICK_KEY]: v1('1.0.0', { url: A }) }),
      feed: IN.aloneSame,
      releases: rows(...R_BASE),
    },
  ],
];

function compareFiles() {
  const files = {};
  const table = [];
  for (const [name, dryRun, exit, stdout, stderr, annotations, { live, feed, releases }] of CASES) {
    const base = `phase3/compare/${name}`;
    if (Object.hasOwn(files, `${base}/live.json`)) throw new Error(`make-phase3: case ${name} appears twice`);
    files[`${base}/live.json`] = typeof live === 'string' ? live : json(live);
    files[`${base}/feed.json`] = json(feed);
    files[`${base}/releases.json`] = json(releases);
    table.push({ case: name, dryRun, exit, stdout, stderr, annotations });
  }
  files['phase3/compare/cases.json'] = json(table);
  return files;
}

// --- files

/** @param {{fixtureSignature: (comment: string) => string}} helpers  make-fixtures.js's own builder
 *  @returns {Record<string, string>}  relPath (under fixtures/) -> content */
function files({ fixtureSignature }) {
  const inputs = snapshotInputs();
  const manifest = buildManifest(inputs, fixtureSignature);
  const { text, warnings } = genFeed.buildFeed({
    plugins: inputs.plugins,
    releases: inputs.releases,
    manifests: [{ file: `manifests/${manifest.rel}`, text: manifest.text }],
    repo: REPO,
  });
  if (warnings.length !== 0) throw new Error(`make-phase3: gen-feed warned: ${JSON.stringify(warnings)}`);
  return {
    [`phase3/${manifest.rel}`]: manifest.text,
    'feeds/valid/one-v2-eight-v1.json': text,
    ...compareFiles(),
  };
}

module.exports = { files };
