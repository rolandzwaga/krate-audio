#!/usr/bin/env node
//
// gen-manager-latest.js
// =============================================================================
// Writes Krate Manager's per-tag updater manifest, latest.json, after verifying every updater signature.
//
// WHY. The manager updates itself through tauri-plugin-updater, which reads latest.json and installs the package it
// names once the package's minisign signature verifies against the compiled pubkey. This tool is the last check
// before that file is published (Krate Manager Phase 11, spec FR-040 to FR-042, FR-044; plan §4.9, PD-16, PD-21):
//   - the assets dir must hold, for each selected OS, exactly its updater asset and that asset's .sig under D6's
//     names; the OS's release-only asset (the .deb, the .pkg) may be present and is never named; anything else (a
//     .deb.sig included, SI-21) is extra (tasks TA-11) -> E_LATEST_ASSETS naming each missing / extra file;
//   - each .sig (Tauri's form: base64 of the four-line minisig text) is trimmed of one trailing LF, base64-decoded,
//     verified over the asset's bytes against --pubkey with tools/minisign.js (key id, signature, global signature),
//     and its trusted comment must hold exactly one `version:` field equal to --version -> else E_LATEST_SIGNATURE;
//   - the output is { notes, platforms, pub_date, version } with every key in bytewise order (SI-21: seven platform
//     keys), each entry { signature: <the .sig text>, url: https://github.com/<repo>/releases/download/<tag>/<asset> },
//     JSON.stringify(doc, null, 2) + "\n", written atomically (temp file + rename) only after every check passed.
// Same inputs -> same bytes. No network module, no child process (the feed-contract hygiene scan).
//
// Usage:
//   node tools/gen-manager-latest.js --version <v> --tag manager/v<v> --repo <owner/repo> --notes <file>
//        --pub-date <RFC 3339> --assets <dir> --pubkey <file> --out <file>
//        [--check-only [--platform-subset <os>]] [--repo-url-base http://127.0.0.1:<port> --only <os>]
//   <os> is windows, macos or linux. --check-only verifies and writes nothing (--notes, --pub-date, --repo and --out
//   are then optional); --platform-subset (only with --check-only) checks one OS's assets, as a build leg holds only
//   its own (SI-19); --repo-url-base and --only go together and serve the updater-e2e job's loopback server (PD-21).
// Exit codes: 0 = written / verified, 1 = refused (one stderr line "gen-manager-latest: <CODE>: <message>"),
// 2 = usage error.
// =============================================================================
'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const { ContractError } = require('./feed-contract/contract.js');
const minisign = require('./minisign.js');

const NAME = 'gen-manager-latest';
const USAGE = [
  'usage: node tools/gen-manager-latest.js --version <v> --tag manager/v<v> --repo <owner/repo> --notes <file>',
  '         --pub-date <RFC 3339> --assets <dir> --pubkey <file> --out <file>',
  '         [--check-only [--platform-subset <windows|macos|linux>]]',
  '         [--repo-url-base http://127.0.0.1:<port> --only <windows|macos|linux>]',
].join('\n');

// D6 as amended by SI-21 (`<v>` is the version): one updater asset per OS, its latest.json keys, and the release-only
// asset latest.json never names.
const ASSET_TABLE = Object.freeze([
  Object.freeze({
    os: 'windows',
    updater: 'Krate-Manager-<v>-Windows-x64-setup.exe',
    keys: Object.freeze(['windows-x86_64', 'windows-x86_64-nsis']),
    releaseOnly: null,
  }),
  Object.freeze({
    os: 'macos',
    updater: 'Krate-Manager-<v>-macOS-universal.app.tar.gz',
    keys: Object.freeze(['darwin-aarch64', 'darwin-x86_64', 'darwin-universal']),
    releaseOnly: 'Krate-Manager-<v>-macOS.pkg',
  }),
  Object.freeze({
    os: 'linux',
    updater: 'Krate-Manager-<v>-Linux-x86_64.AppImage',
    keys: Object.freeze(['linux-x86_64', 'linux-x86_64-appimage']),
    releaseOnly: 'Krate-Manager-<v>-Linux-amd64.deb',
  }),
]);
const OSES = Object.freeze(ASSET_TABLE.map((row) => row.os));

const GITHUB_BASE = 'https://github.com';
// D18: the bounded X.Y.Z of the manager/vX.Y.Z tag (as tools/lint-manager-version.js).
const VERSION_RE = /^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$/u;
const REPO_RE = /^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$/u; // as tools/feed-publish.js
const RFC3339_RE = /^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?(Z|[+-][0-9]{2}:[0-9]{2})$/u;
const LOOPBACK_BASE_RE = /^http:\/\/127\.0\.0\.1:([1-9][0-9]{0,4})$/u;

const VALUE_FLAGS = Object.freeze([
  '--version',
  '--tag',
  '--repo',
  '--notes',
  '--pub-date',
  '--assets',
  '--pubkey',
  '--out',
  '--platform-subset',
  '--repo-url-base',
  '--only',
]);

const usageError = (why) => new ContractError('E_USAGE', why);
const assetsError = (why) => new ContractError('E_LATEST_ASSETS', why);
const signatureError = (asset, why) => new ContractError('E_LATEST_SIGNATURE', `${asset}: ${why}`);

const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
const isCanonicalBase64 = (s) => s !== '' && Buffer.from(s, 'base64').toString('base64') === s;

function rowsFor(only) {
  if (only === undefined) return ASSET_TABLE;
  return ASSET_TABLE.filter((row) => row.os === only);
}

function readText(file, what) {
  try {
    return fs.readFileSync(file, 'utf8');
  } catch (err) {
    throw new ContractError('E_INPUT', `cannot read ${what} ${file}: ${err.code || err.message}`);
  }
}

// TA-11: per selected row the updater asset and its .sig are required, the release-only asset is allowed, every other
// directory entry is extra. A required name that is not a regular file (a directory, a link) counts as missing.
function checkAssetSet(dir, version, rows) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch (err) {
    throw new ContractError('E_INPUT', `cannot read the assets dir ${dir}: ${err.code || err.message}`);
  }
  const files = new Set(entries.filter((e) => e.isFile()).map((e) => e.name));
  const required = rows.flatMap((row) => {
    const name = row.updater.replace('<v>', version);
    return [name, `${name}.sig`];
  });
  const releaseOnly = rows.filter((row) => row.releaseOnly).map((row) => row.releaseOnly.replace('<v>', version));
  const missing = required.filter((name) => !files.has(name));
  const extra = entries
    .map((e) => e.name)
    .filter((name) => !required.includes(name) && !(releaseOnly.includes(name) && files.has(name)))
    .sort(bytewise);
  if (missing.length > 0 || extra.length > 0) {
    const parts = [];
    if (missing.length > 0) parts.push(`missing ${missing.join(', ')}`);
    if (extra.length > 0) parts.push(`extra ${extra.join(', ')}`);
    throw assetsError(`${dir}: ${parts.join('; ')}`);
  }
}

// FR-042: the .sig verifies over the asset and carries exactly one `version:` field equal to `version`.
function verifySig(dir, name, version, pubkeyText) {
  const sigText = readText(path.join(dir, `${name}.sig`), 'the signature');
  const signature = sigText.endsWith('\n') ? sigText.slice(0, -1) : sigText;
  if (!isCanonicalBase64(signature)) throw signatureError(name, 'the .sig is not canonical base64');
  const minisigText = Buffer.from(signature, 'base64').toString('utf8');
  let data;
  try {
    data = fs.readFileSync(path.join(dir, name));
  } catch (err) {
    throw new ContractError('E_INPUT', `cannot read ${name}: ${err.code || err.message}`);
  }
  let verified;
  try {
    verified = minisign.verifyBuffer(data, minisigText, pubkeyText);
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    throw signatureError(name, `${err.code}: ${err.message}`);
  }
  const versions = verified.trustedComment.split('\t').filter((field) => field.startsWith('version:'));
  if (versions.length !== 1) {
    throw signatureError(name, `the trusted comment has ${versions.length} version: fields, not 1`);
  }
  if (versions[0] !== `version:${version}`) {
    throw signatureError(name, `the trusted comment says ${versions[0]}, not version:${version}`);
  }
  return signature;
}

/** Every check of FR-042 / TA-11 over the selected rows -> [{ row, name, signature }]. */
function verifyAssets({ version, assets, pubkeyText, only }) {
  minisign.parsePublicKey(pubkeyText); // a bad key is E_MINISIG_PUBKEY before any asset is named
  const rows = rowsFor(only);
  checkAssetSet(assets, version, rows);
  return rows.map((row) => {
    const name = row.updater.replace('<v>', version);
    return { row, name, signature: verifySig(assets, name, version, pubkeyText) };
  });
}

function sortedObject(entries) {
  const out = {};
  for (const [key, value] of [...entries].sort(([a], [b]) => bytewise(a, b))) out[key] = value;
  return out;
}

/**
 * FR-041: the verified latest.json -> { json, text }. `urlBase` defaults to https://github.com; `only` restricts the
 * table to one OS (PD-21). Throws a ContractError on the first refusal.
 */
function build({ version, tag, repo, notes, pubDate, assets, pubkeyText, urlBase = GITHUB_BASE, only }) {
  const verified = verifyAssets({ version, assets, pubkeyText, only });
  const platforms = [];
  for (const { row, name, signature } of verified) {
    const entry = sortedObject([
      ['signature', signature],
      ['url', `${urlBase}/${repo}/releases/download/${tag}/${name}`],
    ]);
    for (const key of row.keys) platforms.push([key, entry]);
  }
  const json = sortedObject([
    ['version', version],
    ['notes', notes],
    ['pub_date', pubDate],
    ['platforms', sortedObject(platforms)],
  ]);
  return { json, text: JSON.stringify(json, null, 2) + '\n' };
}

function parseArgs(argv) {
  const opts = new Map();
  let checkOnly = false;
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === '--check-only') {
      if (checkOnly) throw usageError('--check-only given twice');
      checkOnly = true;
    } else if (VALUE_FLAGS.includes(a)) {
      if (opts.has(a)) throw usageError(`${a} given twice`);
      if (i + 1 >= argv.length) throw usageError(`${a} needs a value`);
      opts.set(a, argv[i + 1]);
      i += 1;
    } else {
      throw usageError(`unknown argument ${a}`);
    }
  }
  const required = ['--version', '--tag', '--assets', '--pubkey'];
  if (!checkOnly) required.push('--repo', '--notes', '--pub-date', '--out');
  for (const flag of required) if (!opts.has(flag)) throw usageError(`${flag} is required`);

  const version = opts.get('--version');
  if (!VERSION_RE.test(version)) throw usageError(`--version ${version} is not X.Y.Z`);
  if (opts.get('--tag') !== `manager/v${version}`) {
    throw usageError(`--tag ${opts.get('--tag')} is not manager/v${version}`);
  }
  if (opts.has('--repo') && !REPO_RE.test(opts.get('--repo'))) {
    throw usageError(`--repo ${opts.get('--repo')} is not <owner>/<repo>`);
  }
  if (opts.has('--pub-date') && !RFC3339_RE.test(opts.get('--pub-date'))) {
    throw usageError(`--pub-date ${opts.get('--pub-date')} is not an RFC 3339 date-time`);
  }
  for (const flag of ['--platform-subset', '--only']) {
    if (opts.has(flag) && !OSES.includes(opts.get(flag))) {
      throw usageError(`${flag} ${opts.get(flag)} is not one of ${OSES.join(', ')}`);
    }
  }
  if (opts.has('--platform-subset') && !checkOnly) throw usageError('--platform-subset needs --check-only');
  if (opts.has('--platform-subset') && opts.has('--only')) {
    throw usageError('--platform-subset and --only exclude each other');
  }
  if (opts.has('--repo-url-base') !== opts.has('--only')) throw usageError('--repo-url-base and --only go together');
  if (opts.has('--repo-url-base')) {
    const m = LOOPBACK_BASE_RE.exec(opts.get('--repo-url-base'));
    if (m === null || Number(m[1]) > 65535) {
      throw usageError(`--repo-url-base ${opts.get('--repo-url-base')} is not http://127.0.0.1:<port>`);
    }
  }
  return {
    checkOnly,
    version,
    tag: opts.get('--tag'),
    repo: opts.get('--repo'),
    notesFile: opts.get('--notes'),
    pubDate: opts.get('--pub-date'),
    assets: opts.get('--assets'),
    pubkeyFile: opts.get('--pubkey'),
    out: opts.get('--out'),
    urlBase: opts.get('--repo-url-base'),
    only: opts.get('--only') ?? opts.get('--platform-subset'),
  };
}

// Temp file in the output's own directory, then rename: a reader never sees a partial latest.json.
function writeAtomic(out, text) {
  const target = path.resolve(out);
  const suffix = `${process.pid}.${crypto.randomBytes(6).toString('hex')}.tmp`;
  const tmp = path.join(path.dirname(target), `.${path.basename(target)}.${suffix}`);
  try {
    fs.writeFileSync(tmp, text, { flag: 'wx' });
    fs.renameSync(tmp, target);
  } catch (err) {
    fs.rmSync(tmp, { force: true });
    throw new ContractError('E_OUTPUT', `cannot write ${out}: ${err.code || err.message}`);
  }
}

/**
 * @param {string[]} argv
 * @param {{stdout: {write(s: string): void}, stderr: {write(s: string): void}}} io
 * @returns {0|1|2}
 */
function main(argv, io) {
  try {
    const a = parseArgs(argv);
    const pubkeyText = readText(a.pubkeyFile, 'the public key');
    if (a.checkOnly) {
      const verified = verifyAssets({ version: a.version, assets: a.assets, pubkeyText, only: a.only });
      io.stdout.write(`${NAME}: ok, verified ${verified.map((v) => v.name).join(', ')}\n`);
      return 0;
    }
    const { json, text } = build({
      version: a.version,
      tag: a.tag,
      repo: a.repo,
      notes: readText(a.notesFile, 'the notes'),
      pubDate: a.pubDate,
      assets: a.assets,
      pubkeyText,
      urlBase: a.urlBase,
      only: a.only,
    });
    writeAtomic(a.out, text);
    io.stdout.write(`${NAME}: ok, wrote ${a.out} (${Object.keys(json.platforms).length} platform keys)\n`);
    return 0;
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    if (err.code === 'E_USAGE') {
      io.stderr.write(`${NAME}: E_USAGE: ${err.message}\n${USAGE}\n`);
      return 2;
    }
    io.stderr.write(`${NAME}: ${err.code}: ${err.message}\n`);
    return 1;
  }
}

module.exports = { main, build, ASSET_TABLE };

if (require.main === module) process.exitCode = main(process.argv.slice(2), process);
