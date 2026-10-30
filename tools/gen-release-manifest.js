#!/usr/bin/env node
// Builds a release's release.json (C2) and SHA256SUMS from its three signed, fact-recorded assets; prints the C3
// trusted comments the signing loop uses; decides the signing configuration of a release run.
//
// WHY: the manager trusts a release only through release.json: per asset its size, SHA-256, minisign signature, format
// and minimum OS, plus the install facts. Writing it from the finished assets, after verifying every signature against
// the committed public key and its binding to its own asset, means a manifest that exists is one that verifies
// (spec specs/krate-manager-phase2/spec.md FR-020 to FR-029, D6, D7; plan §4.3, PD-23, PD-25). The release job and the
// backfill (buildManifest, in-process) share this one code path. It is offline: no network, no child process, and it
// names no plugin (every name comes from --version-json).
//
// Usage:
//   node tools/gen-release-manifest.js --version-json <file> --setup-iss <file> --assets <dir> --darwin-facts <json>
//        --linux-facts <json> --repo <owner/name> --pub-date <YYYY-MM-DDTHH:MM:SSZ> --public-key <file>
//        --out <release.json> --sums-out <SHA256SUMS>
//   node tools/gen-release-manifest.js --trusted-comments --version-json <file> --assets <dir> --darwin-facts <json>
//   node tools/gen-release-manifest.js --signing-config --dry-run <true|false> --public-key <file>
//   (manifest)          checks in the FR-026 order, then writes SHA256SUMS and release.json as one unit (PD-25)
//   --trusted-comments  prints `<asset>\t<C3 comment>` per platform key, keys bytewise; reads names only
//   --signing-config    prints `signing=throwaway` (dry run) or `signing=on`; the key secret and its password are
//                       read as present / absent from KRATE_SIGNING_KEY / KRATE_SIGNING_KEY_PASSWORD, never printed
// Exit codes: 0 written / printed, 1 abort (nothing written or changed; E_SIGNING_CONFIG included), 2 usage.
// Errors go to stderr as `gen-release-manifest: <CODE>: <message>`; the codes are in
// tools/feed-contract/CONTRACT.md §7.
// Exports main(argv, io), buildManifest, trustedComments, signingConfig, refuseTestKey.
'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const contract = require('./feed-contract/contract.js');
const listing = require('./feed-contract/listing.js');
const minisign = require('./minisign.js');
const genFeed = require('./gen-feed.js');

const { ContractError, CODES } = contract;

const USAGE = [
  'usage: node tools/gen-release-manifest.js --version-json <file> --setup-iss <file> --assets <dir>',
  '         --darwin-facts <json> --linux-facts <json> --repo <owner/name> --pub-date <YYYY-MM-DDTHH:MM:SSZ>',
  '         --public-key <file> --out <release.json> --sums-out <SHA256SUMS>',
  '       node tools/gen-release-manifest.js --trusted-comments --version-json <file> --assets <dir>',
  '         --darwin-facts <json>',
  '       node tools/gen-release-manifest.js --signing-config --dry-run <true|false> --public-key <file>',
  '',
].join('\n');

// The C2 version pattern, from the schema itself so the two cannot drift (release.schema.json $defs.semver).
const SEMVER_RE = new RegExp(require('./feed-contract/release.schema.json').$defs.semver.pattern, 'u');
const FORMAT = Object.freeze({ windows: 'inno-exe', darwin: 'pkg', linux: 'tar.gz' });
const KEYS_README = 'tools/feed-contract/keys/README.md';
const SIGNING_KEY_VAR = 'KRATE_SIGNING_KEY';
const SIGNING_PASSWORD_VAR = 'KRATE_SIGNING_KEY_PASSWORD';
// Plan PD-23: the repo's test public keys, which a real run must never accept as the release key.
const TEST_KEYS = Object.freeze([
  {
    label: 'tools/feed-contract/fixtures/kat/kat.pub',
    file: path.join(__dirname, 'feed-contract', 'fixtures', 'kat', 'kat.pub'),
  },
  {
    label: 'tools/feed-contract/fixtures/phase2/kat-secret/kat.pub',
    file: path.join(__dirname, 'feed-contract', 'fixtures', 'phase2', 'kat-secret', 'kat.pub'),
  },
]);
const HASH_CHUNK = 1 << 20;

const show = (value) => JSON.stringify(value);
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
const isObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const fail = (code, message, where) => new ContractError(code, message, where);
const familyOf = (key) => key.slice(0, key.indexOf('-'));

// --- inputs

/** version.json text -> {name, version, preset_subdir?, identifier?}; E_INPUT on bad JSON, name or version. */
function parseVersionJson(text) {
  let doc;
  try {
    doc = JSON.parse(text);
  } catch (err) {
    throw fail('E_INPUT', `--version-json: not JSON (${err.message})`);
  }
  if (!isObject(doc)) throw fail('E_INPUT', '--version-json: not a JSON object');
  if (!contract.isPluginName(doc.name)) {
    throw fail('E_INPUT', `--version-json: name ${show(doc.name)} is not a plugin name`);
  }
  if (typeof doc.version !== 'string' || !SEMVER_RE.test(doc.version)) {
    throw fail('E_INPUT', `--version-json: version ${show(doc.version)} is not a C2 version`);
  }
  return doc;
}

/** The darwin facts' platform key (a darwin-* member of PLATFORM_KEYS), else E_INPUT. */
function darwinKeyOf(darwinFacts) {
  const key = isObject(darwinFacts) ? darwinFacts.platform_key : undefined;
  if (typeof key !== 'string' || !key.startsWith('darwin-') || !contract.PLATFORM_KEYS.includes(key)) {
    throw fail('E_INPUT', `--darwin-facts: platform_key ${show(key)} is not a darwin-* platform key`);
  }
  return key;
}

/** The three platform keys of a release in bytewise order, each with its asset name. */
function assetKeys(name, version, darwinFacts) {
  return ['windows-x86_64', darwinKeyOf(darwinFacts), 'linux-x86_64']
    .sort(bytewise)
    .map((key) => ({ key, asset: contract.assetNameFor(name, version, key) }));
}

function isFile(p) {
  const st = fs.statSync(p, { throwIfNoEntry: false });
  return st !== undefined && st.isFile();
}

function readText(file, label) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch (err) {
    throw fail('E_INPUT', `${label} ${file}: cannot read (${err.code || err.message})`, { file });
  }
}

function readJson(file, label) {
  const text = readText(file, label);
  try {
    return JSON.parse(text);
  } catch (err) {
    throw fail('E_INPUT', `${label} ${file}: not JSON (${err.message})`, { file });
  }
}

/** SHA-256 of a file, read in 1 MiB chunks (FR-022: streamed). */
function sha256File(file) {
  const hash = crypto.createHash('sha256');
  const chunk = Buffer.allocUnsafe(HASH_CHUNK);
  let fd;
  try {
    fd = fs.openSync(file, 'r');
    for (let n = fs.readSync(fd, chunk, 0, HASH_CHUNK, null); n > 0; n = fs.readSync(fd, chunk, 0, HASH_CHUNK, null)) {
      hash.update(chunk.subarray(0, n));
    }
  } catch (err) {
    throw fail('E_INPUT', `--assets: cannot read ${file} (${err.code || err.message})`, { file });
  } finally {
    if (fd !== undefined) fs.closeSync(fd);
  }
  return hash.digest('hex');
}

/** FR-023: a facts file's asset, size and sha256 must be this asset's. */
function checkFactsBelong(facts, label, asset, measured) {
  if (!isObject(facts)) throw fail('E_INPUT', `${label}: not a JSON object`);
  for (const field of ['asset', 'size', 'sha256']) {
    const expected = field === 'asset' ? asset : measured[field];
    if (facts[field] !== expected) {
      const why = `${label} ${field} is ${show(facts[field])}, the asset in --assets has ${show(expected)}`;
      throw fail('E_FACTS_STALE', why, { field });
    }
  }
}

// --- the manifest (FR-020 to FR-026)

/**
 * Pure core used by main and by backfill-sign apply: every check in the FR-026 order, the first failure thrown as a
 * ContractError; writes nothing.
 * @returns {{doc: object, releaseJsonText: string, sumsText: string}}
 */
function buildManifest({
  versionJsonText,
  setupIssText,
  assetsDir,
  darwinFacts,
  linuxFacts,
  repo,
  pubDate,
  publicKeyText,
}) {
  // 1. version.json and the platform keys.
  const versionJson = parseVersionJson(versionJsonText);
  const { name, version } = versionJson;
  const keys = assetKeys(name, version, darwinFacts);
  const fileOf = (asset) => path.join(assetsDir, asset);

  // 2. FR-021: each asset and its .minisig.
  for (const { asset } of keys) {
    for (const file of [asset, `${asset}.minisig`]) {
      if (!isFile(fileOf(file))) throw fail('E_ASSET_MISSING', `--assets ${assetsDir}: ${file} is missing`, { file });
    }
  }
  const measured = new Map(
    keys.map(({ key, asset }) => [key, { size: fs.statSync(fileOf(asset)).size, sha256: sha256File(fileOf(asset)) }]),
  );

  // 3. FR-023: the facts belong to these assets; the Linux payload is the tarball's own listing.
  const darwin = keys.find(({ key }) => familyOf(key) === 'darwin');
  const linux = keys.find(({ key }) => key === 'linux-x86_64');
  checkFactsBelong(darwinFacts, '--darwin-facts', darwin.asset, measured.get(darwin.key));
  checkFactsBelong(linuxFacts, '--linux-facts', linux.asset, measured.get(linux.key));
  let tarball;
  try {
    tarball = fs.readFileSync(fileOf(linux.asset));
  } catch (err) {
    throw fail('E_INPUT', `--assets: cannot read ${linux.asset} (${err.code || err.message})`, { file: linux.asset });
  }
  const lines = listing.listingFromTarGz(tarball);
  const payload = { files: lines.length, sha256: listing.digest(lines) };
  const factsPayload = isObject(linuxFacts.payload) ? linuxFacts.payload : {};
  for (const field of ['files', 'sha256']) {
    if (factsPayload[field] !== payload[field]) {
      const why =
        `--linux-facts payload.${field} is ${show(factsPayload[field])}, ` +
        `the tarball's listing gives ${show(payload[field])}`;
      throw fail('E_LISTING_DIGEST', why, { field: `payload.${field}` });
    }
  }

  // 4. FR-024: every signature against --public-key, then its binding to its own asset (keys bytewise).
  const signatures = new Map();
  for (const { key, asset } of keys) {
    const sigText = readText(fileOf(`${asset}.minisig`), '--assets');
    let trustedComment;
    try {
      ({ trustedComment } = minisign.verifyFile(fileOf(asset), sigText, publicKeyText));
      contract.checkBinding(trustedComment, { name, version, platform: key, asset });
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      throw fail(err.code, `asset ${asset} (${key}): ${err.message}`, { file: asset, field: err.field });
    }
    signatures.set(key, contract.toLfMinisig(sigText));
  }

  // 5. FR-025: the install block.
  const appId = genFeed.extractAppId(setupIssText);
  if (appId === undefined) throw fail('E_INPUT', '--setup-iss: no AppId={{<GUID>} line');
  const install = { vst3_bundle: `${name}.vst3`, au_component: `${name}.component` };
  if (versionJson.preset_subdir !== undefined) install.preset_subdir = versionJson.preset_subdir;
  install.windows_uninstall_key = `${appId}_is1`;
  install.macos_pkg_id = versionJson.identifier ?? `com.krateaudio.${name.toLowerCase()}`;

  // 6. the document, then C2 in full.
  const tag = `${name.toLowerCase()}/v${version}`;
  const minOs = { [darwin.key]: darwinFacts.min_os, [linux.key]: linuxFacts.min_os };
  const assets = {};
  for (const { key, asset } of keys) {
    const entry = {
      name: asset,
      url: contract.perTagUrl(repo, tag, asset),
      size: measured.get(key).size,
      sha256: measured.get(key).sha256,
      signature: signatures.get(key),
      format: FORMAT[familyOf(key)],
    };
    if (key in minOs) entry.min_os = minOs[key];
    if (key === linux.key) entry.payload = { ...payload, list: lines };
    assets[key] = entry;
  }
  const doc = { tag, name, version, pub_date: pubDate, assets, install };
  contract.checkReleaseManifest(doc, { repo });

  // 7. D7: GNU sha256sum lines, sorted bytewise by name.
  const sumsText = keys
    .map(({ key, asset }) => ({ asset, sha256: measured.get(key).sha256 }))
    .sort((a, b) => bytewise(a.asset, b.asset))
    .map(({ asset, sha256 }) => `${sha256}  ${asset}\n`)
    .join('');
  return { doc, releaseJsonText: JSON.stringify(doc, null, 2) + '\n', sumsText };
}

/** FR-027: per platform key (bytewise) its asset name and C3 comment; reads no file. */
function trustedComments({ versionJsonText, assetsDir, darwinFacts }) {
  void assetsDir; // accepted for symmetry with the manifest mode; the comments need only the names (plan §4.3)
  const { name, version } = parseVersionJson(versionJsonText);
  return assetKeys(name, version, darwinFacts).map(({ key, asset }) => ({
    asset,
    platform: key,
    comment: contract.formatTrustedComment(name, version, key, asset),
  }));
}

// --- writing (PD-25)

const sidecar = (target, ext) => {
  const rand = crypto.randomBytes(4).toString('hex');
  return path.join(path.dirname(target), `.${path.basename(target)}.${process.pid}.${rand}.${ext}`);
};

function removeQuietly(p) {
  try {
    fs.rmSync(p, { force: true });
  } catch {
    // Never created, or cannot be touched: the error already thrown (or the success) is the report.
  }
}

/**
 * Writes SHA256SUMS and release.json as one unit: both temp files first, then both targets checked (absent or a
 * regular file), the old SHA256SUMS moved aside, the new one renamed in, release.json renamed in last. A failure at any
 * point leaves both pre-existing outputs' bytes and mtimes as they were; temps and the backup are always removed.
 */
function writePair(sumsOut, sumsText, out, releaseJsonText) {
  const tS = sidecar(sumsOut, 'tmp');
  const tR = sidecar(out, 'tmp');
  let backup = null;
  try {
    for (const [tmp, text, flag] of [
      [tS, sumsText, '--sums-out'],
      [tR, releaseJsonText, '--out'],
    ]) {
      try {
        fs.writeFileSync(tmp, text, { flag: 'wx' });
      } catch (err) {
        throw fail('E_INPUT', `${flag} ${flag === '--out' ? out : sumsOut}: cannot write (${err.code || err.message})`);
      }
    }
    for (const [target, flag] of [
      [sumsOut, '--sums-out'],
      [out, '--out'],
    ]) {
      const st = fs.lstatSync(target, { throwIfNoEntry: false });
      if (st !== undefined && !st.isFile()) {
        throw fail('E_INPUT', `${flag} ${target}: exists and is not a regular file`);
      }
    }
    try {
      if (fs.lstatSync(sumsOut, { throwIfNoEntry: false }) !== undefined) {
        const b = sidecar(sumsOut, 'bak');
        fs.renameSync(sumsOut, b);
        backup = b;
      }
      fs.renameSync(tS, sumsOut);
    } catch (err) {
      if (backup !== null) {
        fs.renameSync(backup, sumsOut);
        backup = null;
      }
      throw fail('E_INPUT', `--sums-out ${sumsOut}: cannot write (${err.code || err.message})`);
    }
    try {
      fs.renameSync(tR, out);
    } catch (err) {
      removeQuietly(sumsOut);
      if (backup !== null) {
        fs.renameSync(backup, sumsOut);
        backup = null;
      }
      throw fail('E_INPUT', `--out ${out}: cannot write (${err.code || err.message})`);
    }
  } finally {
    removeQuietly(tS);
    removeQuietly(tR);
    if (backup !== null) removeQuietly(backup);
  }
}

// --- signing configuration (FR-029, D6, PD-23)

const rawKey = (publicKey) => publicKey.export({ format: 'der', type: 'spki' }).subarray(-32);

/** The repo's test public keys as text, in TEST_KEYS order; an unreadable one is null (refuseTestKey fails closed). */
function readTestPublicKeys() {
  return TEST_KEYS.map(({ file }) => {
    try {
      return fs.readFileSync(file, 'utf8');
    } catch {
      return null;
    }
  });
}

/**
 * PD-23: E_SIGNING_CONFIG when the public key's key id or 32-byte Ed25519 key equals a test key's, or when a test key
 * is null or unparsable (fail closed). The test keys default to the repo's two fixtures, read relative to this file.
 */
function refuseTestKey(publicKeyText, testPublicKeyTexts = readTestPublicKeys()) {
  const candidate = minisign.parsePublicKey(publicKeyText);
  const candidateKey = rawKey(candidate.publicKey);
  testPublicKeyTexts.forEach((text, i) => {
    const label = i < TEST_KEYS.length ? TEST_KEYS[i].label : `test key ${i + 1}`;
    let test;
    try {
      test = minisign.parsePublicKey(text);
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      throw fail(
        'E_SIGNING_CONFIG',
        `the test public key ${label} is unreadable or malformed, so the public key cannot be cleared ` +
          `(fail closed); see ${KEYS_README}`,
      );
    }
    if (candidate.keyId.equals(test.keyId) || candidateKey.equals(rawKey(test.publicKey))) {
      throw fail('E_SIGNING_CONFIG', `public key is a test key (${label}); see ${KEYS_README}`);
    }
  });
}

/**
 * FR-029: 'throwaway' for a dry run; 'on' for a real run with both secrets present and a parsable public key that is
 * not a test key; otherwise E_SIGNING_CONFIG naming every missing or invalid part. Never sees a secret's value.
 */
function signingConfig({ dryRun, secretPresent, passwordPresent, publicKeyText, testPublicKeyTexts }) {
  if (dryRun) return 'throwaway';
  const problems = [];
  if (!secretPresent) problems.push(`secret ${SIGNING_KEY_VAR} is absent or empty`);
  if (!passwordPresent) problems.push(`password ${SIGNING_PASSWORD_VAR} is absent or empty`);
  if (publicKeyText === null || publicKeyText === undefined) {
    problems.push('public key absent or unreadable');
  } else {
    try {
      minisign.parsePublicKey(publicKeyText);
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      problems.push(err.message);
    }
  }
  if (problems.length > 0) throw fail('E_SIGNING_CONFIG', `${problems.join('; ')}; see ${KEYS_README}`);
  refuseTestKey(publicKeyText, testPublicKeyTexts);
  return 'on';
}

// --- CLI

const MODES = Object.freeze({
  manifest: [
    '--version-json',
    '--setup-iss',
    '--assets',
    '--darwin-facts',
    '--linux-facts',
    '--repo',
    '--pub-date',
    '--public-key',
    '--out',
    '--sums-out',
  ],
  '--trusted-comments': ['--version-json', '--assets', '--darwin-facts'],
  '--signing-config': ['--dry-run', '--public-key'],
});

const usageError = (message) => fail('E_USAGE', message);

/** @returns {{mode: string, opts: Map<string, string>}} one mode, every flag of it required, none repeated */
function parseArgs(argv) {
  let mode = 'manifest';
  const opts = new Map();
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === '--trusted-comments' || a === '--signing-config') {
      if (mode !== 'manifest') throw usageError(`${a} given with ${mode}`);
      mode = a;
      continue;
    }
    if (opts.has(a)) throw usageError(`${a} given twice`);
    if (!MODES.manifest.includes(a) && !MODES['--signing-config'].includes(a)) {
      throw usageError(`unknown argument ${show(a)}`);
    }
    const value = argv[i + 1];
    if (value === undefined || value === '' || value.startsWith('--')) throw usageError(`${a} needs a value`);
    opts.set(a, value);
    i += 1;
  }
  const flags = MODES[mode];
  for (const flag of opts.keys()) {
    if (!flags.includes(flag)) throw usageError(`${flag} is not a flag of the ${mode} mode`);
  }
  for (const flag of flags) {
    if (!opts.has(flag)) throw usageError(`the ${mode} mode needs ${flag}`);
  }
  return { mode, opts };
}

function signingConfigCommand(opts, io) {
  const dryRunText = opts.get('--dry-run');
  if (dryRunText !== 'true' && dryRunText !== 'false') {
    throw usageError(`--dry-run must be true or false, not ${show(dryRunText)}`);
  }
  const dryRun = dryRunText === 'true';
  const env = io.env || {};
  const present = (variable) => typeof env[variable] === 'string' && env[variable] !== '';
  let publicKeyText = null;
  let testPublicKeyTexts;
  if (!dryRun) {
    // A dry run reads no key file at all: its throwaway key is the whole configuration (D6, PD-23).
    const file = opts.get('--public-key');
    if (fs.existsSync(file)) {
      try {
        publicKeyText = fs.readFileSync(file, 'utf8');
      } catch {
        publicKeyText = null; // reported as 'public key absent or unreadable'
      }
    }
    testPublicKeyTexts = readTestPublicKeys();
  }
  const mode = signingConfig({
    dryRun,
    secretPresent: present(SIGNING_KEY_VAR),
    passwordPresent: present(SIGNING_PASSWORD_VAR),
    publicKeyText,
    testPublicKeyTexts,
  });
  io.stdout.write(`signing=${mode}\n`);
  return 0;
}

/**
 * @param {string[]} argv
 * @param {{stdout: {write(s: string): void}, stderr: {write(s: string): void}, env?: object}} io
 * @returns {0|1|2}
 */
function main(argv, io) {
  try {
    const { mode, opts } = parseArgs(argv);
    if (mode === '--signing-config') return signingConfigCommand(opts, io);
    const versionJsonText = readText(opts.get('--version-json'), '--version-json');
    const darwinFacts = readJson(opts.get('--darwin-facts'), '--darwin-facts');
    if (mode === '--trusted-comments') {
      const lines = trustedComments({ versionJsonText, assetsDir: opts.get('--assets'), darwinFacts });
      io.stdout.write(lines.map(({ asset, comment }) => `${asset}\t${comment}\n`).join(''));
      return 0;
    }
    const { releaseJsonText, sumsText } = buildManifest({
      versionJsonText,
      setupIssText: readText(opts.get('--setup-iss'), '--setup-iss'),
      assetsDir: opts.get('--assets'),
      darwinFacts,
      linuxFacts: readJson(opts.get('--linux-facts'), '--linux-facts'),
      repo: opts.get('--repo'),
      pubDate: opts.get('--pub-date'),
      publicKeyText: readText(opts.get('--public-key'), '--public-key'),
    });
    writePair(opts.get('--sums-out'), sumsText, opts.get('--out'), releaseJsonText);
    return 0;
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    io.stderr.write(`gen-release-manifest: ${err.code}: ${err.message}\n`);
    if (CODES[err.code] === 'usage') {
      io.stderr.write(USAGE);
      return 2;
    }
    return 1;
  }
}

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);

module.exports = {
  main,
  buildManifest,
  trustedComments,
  signingConfig,
  refuseTestKey,
};
