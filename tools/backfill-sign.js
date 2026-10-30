#!/usr/bin/env node
// Backfills the manager files (SHA256SUMS, one .minisig per asset, release.json) onto each plugin's current GitHub
// release, which the release workflow published before it learned to sign (spec specs/krate-manager-phase2/spec.md
// FR-051 to FR-057a, D8, D9, D11, D12; plan §4.4, PD-4, PD-9, PD-10, PD-20, PD-24, PD-26, PD-28).
//
// WHY: the manager installs only what a signed release.json describes. The releases already published carry none, so
// without this tool every installed plugin would read as "unknown" until its next release. It changes published
// releases, so every effect on the world (gh, git, pkgutil, lipo, tar, objdump, rsign, the log) goes through one
// injected `deps` object and the tests drive it against a fake; productionDeps is the only code that spawns anything,
// always with a fixed argument array and never a shell. It adds files to a release and never replaces one. It names no
// plugin: the plugin set is the version.json files under --plugins-dir.
//
// Usage:
//   node tools/backfill-sign.js plan --plugins-dir <dir> --repo <owner/name> --out <plan.json>
//   node tools/backfill-sign.js download --plan <plan.json> --format <inno-exe|pkg|tar.gz> --dir <dir>
//        --repo <owner/name>
//   node tools/backfill-sign.js facts --plan <plan.json> --os <darwin|linux> --dir <download dir> --work <dir>
//        --out <facts dir>
//   plan      per plugin the current release (gen-feed's selectReleases: newest stable, drafts ignored), classified
//             planned | skipped-signed | skipped-no-stable | reported, with its assets and its asset names at plan time
//   download  one asset format of every planned release into <dir>/<tag with "/" as "-">/, checked against GitHub's
//             size and digest (an asset GitHub lists without one is refused); failures go to <dir>/download-report.json
//   facts     the release-facts of every downloaded release (darwin: pkgutil + lipo; linux: the tarball checked before
//             tar and objdump run); failures go to <out>/facts-report.<os>.json
//   apply     three passes (PD-9). 1: per planned release not in a facts report, its three assets downloaded and
//             checked again, version.json and setup.iss read at the tag (git show), each asset signed through deps.sign
//             and the signature verified at once against --public-key (a failure aborts: E_SIGN_KEY_MISMATCH),
//             release.json built, SHA256SUMS, the .minisigs and release.json written to <out-dir>/<tag-dashed>/; then
//             the --key file is deleted (PD-20). 2 (not with --dry-run): per signed release `gh release upload` of
//             SHA256SUMS and the three .minisigs, then of release.json alone (D8); never --clobber. 3: the asset audit
//             of every viewed release (FR-057a). Summary: one line per plugin, then `uploads: <n>` (the upload calls
//             that succeeded).
// Exit codes: plan, download, facts: 0 the loop finished (per-release failures are in the reports), 1 abort (nothing
// written), 2 usage. apply: 0 nothing reported, 3 at least one release reported (the others signed), 1 abort
// (E_SIGNING_CONFIG, E_SIGN_KEY_MISMATCH, E_BACKFILL_ASSETS_CHANGED, a missing facts report), 2 usage.
// Errors go to stderr as `backfill-sign: <CODE>: <message>`; the codes are in tools/feed-contract/CONTRACT.md §7.
// Exports main(argv, io, deps), plan, download, facts, apply, productionDeps, cliDeps.
'use strict';
const childProcess = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const contract = require('./feed-contract/contract.js');
const genFeed = require('./gen-feed.js');
const genReleaseManifest = require('./gen-release-manifest.js');
const minisign = require('./minisign.js');
const releaseFacts = require('./release-facts.js');

const { ContractError, CODES } = contract;

const USAGE = [
  'usage: node tools/backfill-sign.js plan --plugins-dir <dir> --repo <owner/name> --out <plan.json>',
  '       node tools/backfill-sign.js download --plan <plan.json> --format <inno-exe|pkg|tar.gz> --dir <dir>',
  '            --repo <owner/name>',
  '       node tools/backfill-sign.js facts --plan <plan.json> --os <darwin|linux> --dir <download dir>',
  '            --work <dir> --out <facts dir>',
  '       node tools/backfill-sign.js apply --plan <plan.json> --plugins-dir <dir> --facts <facts dir> --key <file>',
  '            --public-key <file> --repo <owner/name> --work <dir> --out-dir <dir> [--dry-run]',
  '',
].join('\n');

const LIST_LIMIT = 1000;
const LIST_FIELDS = 'tagName,isPrerelease,isDraft,publishedAt';
const VIEW_FIELDS = 'tagName,publishedAt,isImmutable,assets';
const EXEC_TOOLS = new Set(['pkgutil', 'lipo', 'tar', 'objdump']);
const MAX_BUFFER = 64 * 1024 * 1024;
const HASH_CHUNK = 1 << 20;
// format -> the platform key whose asset name it is (every darwin key names the same .pkg, contract.js ASSET_SUFFIX)
const FORMAT_KEY = Object.freeze({ 'inno-exe': 'windows-x86_64', pkg: 'darwin-universal', 'tar.gz': 'linux-x86_64' });
const FORMATS = Object.keys(FORMAT_KEY);
const REPO_RE = /^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$/u;
const PUB_DATE_RE = /^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$/u;
const DIGEST_RE = /^sha256:([0-9a-f]{64})$/u;

const json = (value) => JSON.stringify(value, null, 2) + '\n';
const show = (value) => JSON.stringify(value);
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
const isObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const fail = (code, message, where) => new ContractError(code, message, where);
const tagDir = (tag) => tag.replace('/', '-');

// --- file IO

/** As tools/gen-feed.js writeAtomic: a temp file in the same directory, then a rename; E_INPUT on any failure. */
function writeAtomic(outPath, text) {
  const tmp = path.join(
    path.dirname(outPath),
    `.${path.basename(outPath)}.${process.pid}.${crypto.randomBytes(4).toString('hex')}.tmp`,
  );
  try {
    fs.writeFileSync(tmp, text, { flag: 'wx' });
    fs.renameSync(tmp, outPath);
  } catch (err) {
    try {
      fs.rmSync(tmp, { force: true });
    } catch {
      // The temp file was never created (or cannot be touched); the E_INPUT below is the report.
    }
    throw fail('E_INPUT', `${outPath}: cannot write (${err.code || err.message})`, { file: outPath });
  }
}

function makeDir(dir, label) {
  try {
    fs.mkdirSync(dir, { recursive: true });
  } catch (err) {
    throw fail('E_INPUT', `${label} ${dir}: cannot create (${err.code || err.message})`, { file: dir });
  }
}

function readJsonFile(file, label) {
  let text;
  try {
    text = fs.readFileSync(file, 'utf8');
  } catch (err) {
    throw fail('E_INPUT', `${label} ${file}: cannot read (${err.code || err.message})`, { file });
  }
  try {
    return JSON.parse(text);
  } catch (err) {
    throw fail('E_INPUT', `${label} ${file}: not JSON (${err.message})`, { file });
  }
}

/** Size and lower-case SHA-256 of a file, read in 1 MiB chunks. */
function hashFile(file) {
  const hash = crypto.createHash('sha256');
  const chunk = Buffer.allocUnsafe(HASH_CHUNK);
  let size = 0;
  let fd;
  try {
    fd = fs.openSync(file, 'r');
    for (let n = fs.readSync(fd, chunk, 0, HASH_CHUNK, null); n > 0; n = fs.readSync(fd, chunk, 0, HASH_CHUNK, null)) {
      hash.update(chunk.subarray(0, n));
      size += n;
    }
  } catch (err) {
    throw fail('E_INPUT', `${path.basename(file)}: cannot read (${err.code || err.message})`, { file });
  } finally {
    if (fd !== undefined) fs.closeSync(fd);
  }
  return { size, sha256: hash.digest('hex') };
}

// --- gh output

function ghJson(deps, argv, label) {
  const r = deps.gh(argv);
  if (r.status !== 0) throw fail('E_INPUT', `${label}: exit ${r.status}`);
  try {
    return JSON.parse(r.stdout);
  } catch (err) {
    throw fail('E_INPUT', `${label}: output is not JSON (${err.message})`);
  }
}

/** gh release list rows: the four fields gen-feed's selectReleases reads, each of its type. */
function checkListRows(rows) {
  if (!Array.isArray(rows)) throw fail('E_INPUT', 'gh release list: output is not a JSON array');
  rows.forEach((row, i) => {
    const ok =
      isObject(row) &&
      typeof row.tagName === 'string' &&
      row.tagName !== '' &&
      typeof row.isPrerelease === 'boolean' &&
      typeof row.isDraft === 'boolean';
    if (!ok) throw fail('E_INPUT', `gh release list: row ${i} is not {tagName, isPrerelease, isDraft, publishedAt}`);
  });
  return rows;
}

// --- plan (FR-052, PD-10)

/** One plugin's plan entry from its `gh release view` document. */
function classify(name, dir, stable, view) {
  const tag = stable.tagName;
  const version = stable.version;
  if (!isObject(view) || view.tagName !== tag || !Array.isArray(view.assets)) {
    throw fail('E_INPUT', `gh release view ${tag}: not the release's {tagName, assets} document`);
  }
  const listed = new Map();
  for (const a of view.assets) {
    if (!isObject(a) || typeof a.name !== 'string') throw fail('E_INPUT', `gh release view ${tag}: an asset has no name`);
    listed.set(a.name, a);
  }
  const assetsBefore = [...listed.keys()].sort(bytewise);
  const base = { name, dir, tag, assets_before: assetsBefore };
  const reported = (code, message) => ({ name, dir, status: 'reported', code, message, tag, assets_before: assetsBefore });

  if (listed.has('release.json')) return { name, dir, status: 'skipped-signed', tag, assets_before: assetsBefore };
  const expected = FORMATS.map((format) => ({
    name: contract.assetNameFor(name, version, FORMAT_KEY[format]),
    format,
  }));
  const missing = expected.find((e) => !listed.has(e.name));
  if (missing) return reported('E_BACKFILL_ASSET_MISSING', `${tag}: asset ${missing.name} is missing`);
  if (view.isImmutable === true) return reported('E_BACKFILL_IMMUTABLE', `${tag}: the release is immutable`);
  const stray = ['SHA256SUMS', ...expected.map((e) => `${e.name}.minisig`)].find((n) => listed.has(n));
  if (stray) {
    return reported('E_BACKFILL_PARTIAL', `${tag}: ${stray} is present without release.json (an earlier upload stopped)`);
  }
  if (typeof view.publishedAt !== 'string' || !PUB_DATE_RE.test(view.publishedAt)) {
    return reported('E_INPUT', `${tag}: publishedAt ${show(view.publishedAt)} is not YYYY-MM-DDTHH:MM:SSZ`);
  }
  const assets = [];
  for (const e of expected) {
    const a = listed.get(e.name);
    if (!Number.isSafeInteger(a.size) || a.size < 1) {
      return reported('E_INPUT', `${tag}: asset ${e.name} size ${show(a.size)} is not a positive integer`);
    }
    if (a.digest !== null && a.digest !== undefined && typeof a.digest !== 'string') {
      return reported('E_INPUT', `${tag}: asset ${e.name} digest ${show(a.digest)} is not a string or null`);
    }
    assets.push({ name: e.name, format: e.format, size: a.size, digest: a.digest === undefined ? null : a.digest });
  }
  return { ...base, status: 'planned', version, publishedAt: view.publishedAt, assets };
}

/**
 * FR-052: writes plan.json; 0 when the loop finished, 1 on an abort (a gh release list failure, 1000 rows, an
 * E_TAG_FORMAT tag), with no plan file written.
 */
function plan({ pluginsDir, repo, out }, deps) {
  const plugins = genFeed.readPluginsDir(pluginsDir, { withSetupIss: false }).map((p) => {
    const name = isObject(p.versionJson) ? p.versionJson.name : undefined;
    if (!contract.isPluginName(name)) {
      throw fail('E_INPUT', `--plugins-dir ${p.dir}/version.json: name ${show(name)} is not a plugin name`);
    }
    return { name, dir: p.dir };
  });
  const listArgv = ['release', 'list', '--repo', repo, '--limit', String(LIST_LIMIT), '--json', LIST_FIELDS];
  const rows = checkListRows(ghJson(deps, listArgv, 'gh release list'));
  if (rows.length >= LIST_LIMIT) {
    throw fail('E_BACKFILL_LIST_TRUNCATED', `gh release list returned ${rows.length} rows, the --limit; releases may be missing`);
  }
  // Every selection first, so an E_TAG_FORMAT anywhere aborts before any release is viewed (PD-10).
  const selected = plugins.map((p) => ({ ...p, picked: genFeed.selectReleases(p.name, rows) }));
  const releases = [];
  for (const { name, dir, picked } of selected) {
    if (picked === null) {
      releases.push({ name, dir, status: 'skipped-no-stable' });
      continue;
    }
    const tag = picked.stable.tagName;
    // A failed or unreadable view is this release's report (no assets_before: nothing was viewed), not an abort.
    try {
      const view = ghJson(deps, ['release', 'view', tag, '--repo', repo, '--json', VIEW_FIELDS], `gh release view ${tag}`);
      releases.push(classify(name, dir, picked.stable, view));
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      releases.push({ name, dir, status: 'reported', code: err.code, message: err.message, tag });
    }
  }
  releases.sort((a, b) => bytewise(a.name, b.name));
  writeAtomic(out, json({ repo, releases }));
  for (const e of releases) {
    const detail = e.status === 'reported' ? ` ${e.code}: ${e.message}` : '';
    deps.log(`${e.name}: ${e.status}${e.tag ? ` ${e.tag}` : ''}${detail}`);
  }
  return 0;
}

// --- reading plan.json back (download, facts)

/** A planned entry with its asset names rebuilt from name and version (never taken from the file as a path). */
function plannedRelease(e, i) {
  const where = `plan.json releases[${i}]`;
  if (!contract.isPluginName(e.name)) throw fail('E_INPUT', `${where}: name ${show(e.name)} is not a plugin name`);
  const prefix = `${e.name.toLowerCase()}/v`;
  if (typeof e.tag !== 'string' || !e.tag.startsWith(prefix) || !contract.isStableCore(e.tag.slice(prefix.length))) {
    throw fail('E_INPUT', `${where}: tag ${show(e.tag)} is not ${prefix}<X.Y.Z>`);
  }
  if (e.version !== e.tag.slice(prefix.length)) throw fail('E_INPUT', `${where}: version ${show(e.version)} is not the tag's`);
  if (!Array.isArray(e.assets)) throw fail('E_INPUT', `${where}: assets is not an array`);
  const assets = {};
  for (const format of FORMATS) {
    const name = contract.assetNameFor(e.name, e.version, FORMAT_KEY[format]);
    const a = e.assets.find((x) => isObject(x) && x.format === format);
    if (!a || a.name !== name || !Number.isSafeInteger(a.size)) {
      throw fail('E_INPUT', `${where}: no ${format} asset ${name} with a size`);
    }
    assets[format] = { name, size: a.size, digest: a.digest === undefined ? null : a.digest };
  }
  return { name: e.name, version: e.version, tag: e.tag, assets };
}

function readPlanDoc(file) {
  const doc = readJsonFile(file, '--plan');
  if (!isObject(doc) || !Array.isArray(doc.releases)) throw fail('E_INPUT', `--plan ${file}: no releases array`);
  return doc;
}

function readPlan(file) {
  return readPlanDoc(file)
    .releases.map((e, i) => (isObject(e) && e.status === 'planned' ? plannedRelease(e, i) : null))
    .filter((e) => e !== null);
}

const errorEntry = (tag, err) => ({ tag, code: err.code, message: err.message });

// --- download (FR-053, PD-28)

/** Checks one downloaded asset against the plan; returns true once its digest matched. Throws E_BACKFILL_*. */
function checkDownloaded(file, asset, tag) {
  const measured = hashFile(file);
  if (measured.size !== asset.size) {
    throw fail('E_BACKFILL_SIZE', `${tag}: ${asset.name} is ${measured.size} bytes, the release lists ${asset.size}`);
  }
  // No trusted reference hash: never unpack or sign it (Clarifications 2026-10-30, CC-verify).
  if (asset.digest === null) {
    throw fail('E_BACKFILL_DIGEST', `${tag}: ${asset.name} has no digest on GitHub, so it cannot be checked`);
  }
  const m = typeof asset.digest === 'string' ? DIGEST_RE.exec(asset.digest.toLowerCase()) : null;
  if (m === null) {
    throw fail('E_BACKFILL_DIGEST', `${tag}: ${asset.name} digest ${show(asset.digest)} is not sha256:<64 hex>`);
  }
  if (m[1] !== measured.sha256) {
    throw fail('E_BACKFILL_DIGEST', `${tag}: ${asset.name} SHA-256 is ${measured.sha256}, GitHub's digest is ${m[1]}`);
  }
  return true;
}

/** FR-053: one format of every planned release; <dir>/download-report.json always written. */
function download({ plan: planFile, format, dir, repo }, deps) {
  const planned = readPlan(planFile);
  makeDir(dir, '--dir');
  const report = [];
  let checked = 0;
  for (const rel of planned) {
    const asset = rel.assets[format];
    const dest = path.join(dir, tagDir(rel.tag));
    try {
      makeDir(dest, '--dir');
      const r = deps.gh(['release', 'download', rel.tag, '--repo', repo, '--pattern', asset.name, '--dir', dest]);
      if (r.status !== 0) {
        throw fail('E_INPUT', `gh release download ${rel.tag} --pattern ${asset.name}: exit ${r.status}`);
      }
      if (checkDownloaded(path.join(dest, asset.name), asset, rel.tag)) checked += 1;
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      report.push(errorEntry(rel.tag, err));
      deps.log(`download: ${rel.tag}: ${err.code}: ${err.message}`);
    }
  }
  writeAtomic(path.join(dir, 'download-report.json'), json(report));
  deps.log(`digest checked: ${checked} of ${planned.length}`);
  return 0;
}

// --- facts (FR-053a, PD-4, PD-26)

function runExec(deps, command, argv) {
  const r = deps.exec(command, argv);
  if (r.status !== 0) throw fail('E_INPUT', `${command} ${argv[0]}: exit ${r.status}`);
  return r.stdout;
}

function darwinFactsOf(rel, dir, work, deps) {
  const asset = rel.assets.pkg;
  const pkg = path.join(dir, tagDir(rel.tag), asset.name);
  const expanded = path.join(work, `${tagDir(rel.tag)}-x`);
  runExec(deps, 'pkgutil', ['--expand-full', pkg, expanded]);
  const tree = releaseFacts.darwinFactsFromTree(expanded);
  for (const m of tree.macho) {
    const file = path.join(expanded, ...m.path.split('/'));
    const words = String(runExec(deps, 'lipo', ['-archs', file])).trim().split(/\s+/u).filter(Boolean).sort(bytewise);
    if (words.join(' ') !== m.archs.join(' ')) {
      throw fail(
        'E_FACTS_LIPO_MISMATCH',
        `${rel.tag}: lipo -archs ${m.path} prints [${words.join(' ')}], the Mach-O reader found [${m.archs.join(' ')}]`,
        { file: m.path },
      );
    }
  }
  const { size, sha256 } = hashFile(pkg);
  return {
    asset: asset.name,
    size,
    sha256,
    platform_key: tree.platform_key,
    min_os: tree.min_os,
    archs: tree.archs,
    macho: tree.macho,
  };
}

function linuxFactsOf(rel, dir, work, deps) {
  const tarball = path.join(dir, tagDir(rel.tag), rel.assets['tar.gz'].name);
  let buf;
  try {
    buf = fs.readFileSync(tarball);
  } catch (err) {
    throw fail('E_INPUT', `${rel.tag}: ${path.basename(tarball)}: cannot read (${err.code || err.message})`);
  }
  // PD-26: the listing, the single-bundle rule and the layout before anything extracts the archive.
  const { vst3Bundle, soPath } = releaseFacts.linuxPrecheck(buf);
  if (soPath === null) {
    throw fail('E_LAYOUT', `layout: ${vst3Bundle} holds no Contents/x86_64-linux/<Name>.so regular file`);
  }
  const extracted = path.join(work, `${tagDir(rel.tag)}-x`);
  makeDir(extracted, '--work');
  runExec(deps, 'tar', ['-xzf', tarball, '-C', extracted]);
  const objdumpText = runExec(deps, 'objdump', ['-T', path.join(extracted, ...soPath.split('/'))]);
  return releaseFacts.linuxFacts({ tarball, extracted, objdumpText }, { listDirectory: deps.listDirectory });
}

/** FR-053a: the facts of every downloaded release; <out>/facts-report.<os>.json always written. */
function facts({ plan: planFile, os: osName, dir, work, out }, deps) {
  const planned = readPlan(planFile);
  const downloadReport = readJsonFile(path.join(dir, 'download-report.json'), '--dir');
  if (!Array.isArray(downloadReport)) throw fail('E_INPUT', `--dir ${dir}: download-report.json is not a JSON array`);
  makeDir(out, '--out');
  makeDir(work, '--work');
  const report = [...downloadReport];
  const failedDownload = new Set(downloadReport.map((e) => (isObject(e) ? e.tag : undefined)));
  for (const rel of planned) {
    if (failedDownload.has(rel.tag)) continue;
    try {
      const doc = osName === 'darwin' ? darwinFactsOf(rel, dir, work, deps) : linuxFactsOf(rel, dir, work, deps);
      writeAtomic(path.join(out, `${rel.name}-${rel.version}.${osName}.json`), json(doc));
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      report.push(errorEntry(rel.tag, err));
      deps.log(`facts ${osName}: ${rel.tag}: ${err.code}: ${err.message}`);
    }
  }
  writeAtomic(path.join(out, `facts-report.${osName}.json`), json(report));
  return 0;
}

// --- apply (FR-054 to FR-057a, PD-9, PD-20, PD-23, PD-24)

const STATUSES = Object.freeze(['planned', 'skipped-signed', 'skipped-no-stable', 'reported']);
const isStringArray = (v) => Array.isArray(v) && v.every((x) => typeof x === 'string');

/**
 * One plan.json entry as apply uses it: name and dir checked against the --plugins-dir roster (dir becomes part of a
 * `git show` path), the tag of its own plugin, assets_before a string array; a planned entry rebuilt by plannedRelease.
 */
function applyEntry(e, i, roster) {
  const where = `plan.json releases[${i}]`;
  if (!isObject(e) || !contract.isPluginName(e.name)) throw fail('E_INPUT', `${where}: no plugin name`);
  if (roster.get(e.name) !== e.dir) {
    throw fail('E_INPUT', `${where}: ${e.name} with dir ${show(e.dir)} is not a plugin of --plugins-dir`);
  }
  if (!STATUSES.includes(e.status)) throw fail('E_INPUT', `${where}: status ${show(e.status)} is not a plan status`);
  if (e.tag !== undefined && (typeof e.tag !== 'string' || !e.tag.startsWith(`${e.name.toLowerCase()}/v`))) {
    throw fail('E_INPUT', `${where}: tag ${show(e.tag)} is not a tag of ${e.name}`);
  }
  if (e.assets_before !== undefined && !isStringArray(e.assets_before)) {
    throw fail('E_INPUT', `${where}: assets_before is not an array of names`);
  }
  const entry = {
    name: e.name,
    dir: e.dir,
    status: e.status,
    tag: e.tag === undefined ? null : e.tag,
    assetsBefore: e.assets_before === undefined ? null : e.assets_before,
  };
  if (e.status === 'reported') {
    if (typeof e.code !== 'string' || typeof e.message !== 'string') {
      throw fail('E_INPUT', `${where}: a reported entry without code and message`);
    }
    return { ...entry, code: e.code, message: e.message };
  }
  if (e.status !== 'planned') return entry;
  if (entry.assetsBefore === null) throw fail('E_INPUT', `${where}: a planned entry without assets_before`);
  if (typeof e.publishedAt !== 'string' || !PUB_DATE_RE.test(e.publishedAt)) {
    throw fail('E_INPUT', `${where}: publishedAt ${show(e.publishedAt)} is not YYYY-MM-DDTHH:MM:SSZ`);
  }
  return { ...entry, ...plannedRelease(e, i), publishedAt: e.publishedAt };
}

/** A facts job's report: a JSON array of {tag, code, message}; a missing file means that job never ran (E_INPUT). */
function readFactsReport(factsDir, osName) {
  const file = path.join(factsDir, `facts-report.${osName}.json`);
  const doc = readJsonFile(file, '--facts');
  const ok =
    Array.isArray(doc) &&
    doc.every((e) => isObject(e) && ['tag', 'code', 'message'].every((k) => typeof e[k] === 'string'));
  if (!ok) throw fail('E_INPUT', `--facts ${file}: not a JSON array of {tag, code, message}`);
  return doc;
}

/** D12: a file at the release's own tag. */
function gitShow(deps, rel, relPath) {
  const spec = `${rel.tag}:plugins/${rel.dir}/${relPath}`;
  const r = deps.git(['show', spec]);
  if (r.status !== 0) throw fail('E_INPUT', `git show ${spec}: exit ${r.status}`);
  return r.stdout;
}

/** The tag's version.json must name this plugin and carry the tag's version (E_VERSION_MISMATCH). */
function checkTagVersion(rel, versionJsonText) {
  let doc;
  try {
    doc = JSON.parse(versionJsonText);
  } catch (err) {
    throw fail('E_INPUT', `${rel.tag}: plugins/${rel.dir}/version.json at the tag is not JSON (${err.message})`);
  }
  if (!isObject(doc) || doc.name !== rel.name) {
    throw fail('E_INPUT', `${rel.tag}: plugins/${rel.dir}/version.json at the tag does not name ${rel.name}`);
  }
  if (doc.version !== rel.version) {
    throw fail(
      'E_VERSION_MISMATCH',
      `${rel.tag}: plugins/${rel.dir}/version.json at the tag says version ${show(doc.version)}, the tag ${rel.version}`,
    );
  }
}

/**
 * Pass 1 for one release: its three assets (FR-053 checks), both files at the tag, both facts files, one signature per
 * asset verified at once against the public key (an abort when it fails, PD-9), the manifest, the outputs in
 * <outDir>/<tag-dashed>/. Returns the two upload batches as absolute paths (D8: release.json alone and last).
 */
function prepareRelease(rel, opts, deps, digest) {
  const { factsDir, key, publicKeyText, repo, work, outDir } = opts;
  const assetsDir = path.join(work, tagDir(rel.tag), 'assets');
  makeDir(assetsDir, '--work');
  for (const format of FORMATS) {
    const asset = rel.assets[format];
    digest[format].of += 1;
    const r = deps.gh(['release', 'download', rel.tag, '--repo', repo, '--pattern', asset.name, '--dir', assetsDir]);
    if (r.status !== 0) throw fail('E_INPUT', `gh release download ${rel.tag} --pattern ${asset.name}: exit ${r.status}`);
    if (checkDownloaded(path.join(assetsDir, asset.name), asset, rel.tag)) digest[format].checked += 1;
  }
  const versionJsonText = gitShow(deps, rel, 'version.json');
  const setupIssText = gitShow(deps, rel, 'installers/windows/setup.iss');
  checkTagVersion(rel, versionJsonText);
  const factsOf = (osName) => {
    const file = path.join(factsDir, `${rel.name}-${rel.version}.${osName}.json`);
    if (!fs.existsSync(file)) {
      throw fail('E_FACTS_MISSING', `${rel.tag}: ${path.basename(file)} is not in --facts and no facts report names it`);
    }
    return readJsonFile(file, '--facts');
  };
  const darwinFacts = factsOf('darwin');
  const linuxFacts = factsOf('linux');

  const signatures = [];
  for (const { asset, comment } of genReleaseManifest.trustedComments({ versionJsonText, assetsDir, darwinFacts })) {
    const file = path.join(assetsDir, asset);
    const sigText = contract.toLfMinisig(deps.sign(file, comment, key));
    writeAtomic(`${file}.minisig`, sigText);
    try {
      minisign.verifyFile(file, sigText, publicKeyText);
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      throw fail(
        'E_SIGN_KEY_MISMATCH',
        `${rel.tag}: the signature made for ${asset} does not verify against --public-key (${err.code}); ` +
          'the signing key and the committed public key differ, so nothing is uploaded',
      );
    }
    signatures.push({ name: `${asset}.minisig`, text: sigText });
  }
  const { releaseJsonText, sumsText } = genReleaseManifest.buildManifest({
    versionJsonText,
    setupIssText,
    assetsDir,
    darwinFacts,
    linuxFacts,
    repo,
    pubDate: rel.publishedAt,
    publicKeyText,
  });

  const dest = path.join(outDir, tagDir(rel.tag));
  makeDir(dest, '--out-dir');
  const out = (name, text) => {
    const file = path.resolve(dest, name);
    writeAtomic(file, text);
    return file;
  };
  const first = [out('SHA256SUMS', sumsText), ...signatures.map((s) => out(s.name, s.text))];
  return { first, second: [out('release.json', releaseJsonText)] };
}

/** PD-20: the key file is removed; true when it no longer exists. */
function removeKey(key) {
  try {
    fs.rmSync(key, { force: true });
  } catch {
    // Reported by the existence check below.
  }
  return !fs.existsSync(key);
}

/** FR-057a: every viewed release's asset names now vs assets_before (+ this run's uploads); the problems found. */
function audit(entries, uploaded, repo, deps) {
  const problems = [];
  for (const e of entries) {
    if (e.tag === null || e.assetsBefore === null) continue;
    let view;
    try {
      view = ghJson(deps, ['release', 'view', e.tag, '--repo', repo, '--json', 'assets'], `gh release view ${e.tag}`);
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      problems.push({ code: err.code, message: err.message });
      continue;
    }
    const named = (a) => isObject(a) && typeof a.name === 'string';
    if (!isObject(view) || !Array.isArray(view.assets) || !view.assets.every(named)) {
      problems.push({ code: 'E_INPUT', message: `gh release view ${e.tag}: not an {assets: [{name}]} document` });
      continue;
    }
    const now = view.assets.map((a) => a.name).sort(bytewise);
    const expected = [...new Set([...e.assetsBefore, ...(uploaded.get(e.tag) || [])])].sort(bytewise);
    if (show(now) === show(expected)) continue;
    const added = now.filter((n) => !expected.includes(n));
    const gone = expected.filter((n) => !now.includes(n));
    problems.push({
      code: 'E_BACKFILL_ASSETS_CHANGED',
      message: `${e.tag}: assets not expected ${show(added)}, expected but absent ${show(gone)}`,
    });
  }
  return problems;
}

const SUMMARY = Object.freeze({
  signed: 'signed',
  'skipped-signed': 'skipped (already signed)',
  'skipped-no-stable': 'skipped (no stable release)',
});

/**
 * FR-054 to FR-057a: pass 1 signs and builds every planned release, pass 2 uploads (live only), pass 3 audits every
 * viewed release. 0 nothing reported, 3 at least one release reported, 1 abort (thrown).
 */
function apply({ plan: planFile, pluginsDir, facts: factsDir, key, publicKey, repo, work, outDir, dryRun }, deps) {
  const outcome = new Map(); // name -> {status, code?, message?}
  const report = (name, err) => outcome.set(name, { status: 'reported', code: err.code, message: err.message });
  const signed = [];
  let entries;
  let keyGone = false;
  try {
    // 1. Every input before any effect: plan, roster, both facts reports, the public key (PD-23).
    const doc = readPlanDoc(planFile);
    const roster = new Map(
      genFeed.readPluginsDir(pluginsDir, { withSetupIss: false }).map((p) => [
        isObject(p.versionJson) ? p.versionJson.name : undefined,
        p.dir,
      ]),
    );
    entries = doc.releases.map((e, i) => applyEntry(e, i, roster));
    const factsReported = new Map();
    for (const osName of ['darwin', 'linux']) {
      for (const e of readFactsReport(factsDir, osName)) if (!factsReported.has(e.tag)) factsReported.set(e.tag, e);
    }
    let publicKeyText;
    try {
      publicKeyText = fs.readFileSync(publicKey, 'utf8');
    } catch (err) {
      throw fail('E_INPUT', `--public-key ${publicKey}: cannot read (${err.code || err.message})`, { file: publicKey });
    }
    genReleaseManifest.refuseTestKey(publicKeyText);
    makeDir(work, '--work');

    // 2. Pass 1 (PD-9): nothing is uploaded before every release has been signed and verified.
    const digest = Object.fromEntries(FORMATS.map((format) => [format, { checked: 0, of: 0 }]));
    const opts = { factsDir, key, publicKeyText, repo, work, outDir };
    for (const e of entries) {
      if (e.status === 'reported') report(e.name, e);
      if (e.status !== 'planned') {
        if (!outcome.has(e.name)) outcome.set(e.name, { status: e.status });
        continue;
      }
      if (factsReported.has(e.tag)) {
        report(e.name, factsReported.get(e.tag));
        continue;
      }
      try {
        signed.push({ rel: e, ...prepareRelease(e, opts, deps, digest) });
        outcome.set(e.name, { status: 'signed' });
      } catch (err) {
        if (!(err instanceof ContractError) || err.code === 'E_SIGN_KEY_MISMATCH') throw err;
        report(e.name, err);
      }
    }
    for (const format of FORMATS) deps.log(`digest checked: ${digest[format].checked} of ${digest[format].of}`);
  } finally {
    keyGone = removeKey(key); // PD-20: also on an abort inside pass 1
  }
  if (!keyGone) throw fail('E_INPUT', `--key ${key}: cannot remove the key file, so nothing is uploaded`);

  // 3. Pass 2 (live only, FR-056, PD-24): two calls per signed release, release.json last; a failed call reports it.
  let uploads = 0;
  const uploaded = new Map();
  for (const { rel, first, second } of dryRun ? [] : signed) {
    uploaded.set(rel.tag, []);
    for (const files of [first, second]) {
      const names = files.map((f) => path.basename(f));
      const argv = ['release', 'upload', rel.tag, '--repo', repo, ...files];
      if (argv.includes('--clobber')) throw new TypeError('backfill-sign: an upload never replaces an asset');
      const clash = names.find((n) => rel.assetsBefore.includes(n));
      if (clash !== undefined) {
        report(rel.name, fail('E_BACKFILL_PARTIAL', `${rel.tag}: ${clash} was already on the release at plan time`));
        break;
      }
      const r = deps.gh(argv);
      if (r.status !== 0) {
        report(rel.name, fail('E_INPUT', `gh release upload ${rel.tag} ${names.join(' ')}: exit ${r.status}`));
        break;
      }
      uploads += 1;
      uploaded.get(rel.tag).push(...names);
    }
  }

  // 4. Pass 3, the audit (FR-057a), then 5. the summary (FR-057).
  const problems = audit(entries, uploaded, repo, deps);
  let reported = 0;
  for (const e of entries) {
    const o = outcome.get(e.name);
    if (o.status === 'reported') reported += 1;
    deps.log(`${e.name}: ${o.status === 'reported' ? `reported ${o.code}: ${o.message}` : SUMMARY[o.status]}`);
  }
  deps.log(`uploads: ${uploads}`);
  if (problems.length > 0) {
    const changed = problems.find((p) => p.code === 'E_BACKFILL_ASSETS_CHANGED');
    throw fail(changed ? changed.code : problems[0].code, problems.map((p) => p.message).join('; '));
  }
  return reported > 0 ? 3 : 0;
}

// --- the real world (FR-051)

/**
 * Spawns gh, the four exec tools and rsign with cwd = workDir, and git with cwd = repoDir (a directory inside the
 * checkout: D12's `git show <tag>:…` needs the repository and its tags); fixed argument arrays and no shell.
 */
function productionDeps(workDir, repoDir) {
  const spawn = (cmd, argv, cwd = workDir) => {
    const r = childProcess.spawnSync(cmd, argv, {
      cwd,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'],
      shell: false,
      maxBuffer: MAX_BUFFER,
    });
    if (r.error) throw fail('E_INPUT', `${cmd}: cannot run (${r.error.code || r.error.message})`);
    return { status: r.status, stdout: r.stdout, stderr: r.stderr };
  };
  return {
    gh: (argv) => spawn('gh', argv),
    git: (argv) => {
      const r = spawn('git', argv, repoDir);
      return { status: r.status, stdout: r.stdout };
    },
    exec: (command, argv) => {
      if (!EXEC_TOOLS.has(command)) throw new TypeError(`exec: ${show(command)} is not pkgutil, lipo, tar or objdump`);
      return spawn(command, argv);
    },
    sign: (file, trustedComment, keyFile) => {
      const sig = `${file}.minisig`;
      const r = spawn('rsign', ['sign', '-W', '-s', keyFile, '-t', trustedComment, '-x', sig, file]);
      if (r.status !== 0) throw fail('E_INPUT', `rsign sign ${path.basename(file)}: exit ${r.status}`);
      try {
        return fs.readFileSync(sig, 'utf8');
      } catch (err) {
        throw fail('E_INPUT', `rsign sign ${path.basename(file)}: no signature written (${err.code || err.message})`);
      }
    },
    log: (line) => process.stdout.write(`${line}\n`),
  };
}

// --- CLI

const SUBCOMMANDS = Object.freeze({
  plan: ['--plugins-dir', '--repo', '--out'],
  download: ['--plan', '--format', '--dir', '--repo'],
  facts: ['--plan', '--os', '--dir', '--work', '--out'],
  apply: ['--plan', '--plugins-dir', '--facts', '--key', '--public-key', '--repo', '--work', '--out-dir'],
});
// Flags that take no value (optional).
const SWITCHES = Object.freeze({ apply: ['--dry-run'] });

const usageError = (message) => fail('E_USAGE', message);

/** @returns {{sub: string, opts: Map<string, string | true>}} every valued flag required, none repeated */
function parseArgs(argv) {
  const [sub, ...rest] = argv;
  if (sub === undefined) throw usageError('a subcommand is required');
  if (!Object.prototype.hasOwnProperty.call(SUBCOMMANDS, sub)) throw usageError(`unknown subcommand ${show(sub)}`);
  const flags = SUBCOMMANDS[sub];
  const switches = SWITCHES[sub] || [];
  const opts = new Map();
  for (let i = 0; i < rest.length; i += 1) {
    const a = rest[i];
    if (!flags.includes(a) && !switches.includes(a)) throw usageError(`${show(a)} is not a flag of ${sub}`);
    if (opts.has(a)) throw usageError(`${a} given twice`);
    if (switches.includes(a)) {
      opts.set(a, true);
      continue;
    }
    const value = rest[i + 1];
    if (value === undefined || value === '' || value.startsWith('--')) throw usageError(`${a} needs a value`);
    opts.set(a, value);
    i += 1;
  }
  for (const flag of flags) {
    if (!opts.has(flag)) throw usageError(`${sub} needs ${flag}`);
  }
  if (opts.has('--repo') && !REPO_RE.test(opts.get('--repo'))) {
    throw usageError(`--repo ${show(opts.get('--repo'))} is not <owner>/<name>`);
  }
  if (opts.has('--format') && !FORMATS.includes(opts.get('--format'))) {
    throw usageError(`--format ${show(opts.get('--format'))} is not one of ${FORMATS.join(', ')}`);
  }
  if (opts.has('--os') && !['darwin', 'linux'].includes(opts.get('--os'))) {
    throw usageError(`--os ${show(opts.get('--os'))} is not darwin or linux`);
  }
  return { sub, opts };
}

/** A flag's value when argv holds one, else os.tmpdir(); parsing proper happens in main. */
function flagDirOf(argv, flag) {
  const i = argv.indexOf(flag);
  return i >= 0 && typeof argv[i + 1] === 'string' && argv[i + 1] !== '' ? path.resolve(argv[i + 1]) : os.tmpdir();
}

/** The CLI's effects: tools run in --work, git in --plugins-dir (inside the checkout; CI's --work is outside it). */
function cliDeps(argv) {
  return productionDeps(flagDirOf(argv, '--work'), flagDirOf(argv, '--plugins-dir'));
}

/**
 * @param {string[]} argv
 * @param {{stdout: {write(s: string): void}, stderr: {write(s: string): void}}} io
 * @param {object} [deps] the effects (constructing productionDeps spawns nothing)
 * @returns {0|1|2}
 */
function main(argv, io, deps = cliDeps(argv)) {
  try {
    const { sub, opts } = parseArgs(argv);
    if (sub === 'plan') {
      return plan({ pluginsDir: opts.get('--plugins-dir'), repo: opts.get('--repo'), out: opts.get('--out') }, deps);
    }
    if (sub === 'download') {
      const args = { plan: opts.get('--plan'), format: opts.get('--format'), dir: opts.get('--dir') };
      return download({ ...args, repo: opts.get('--repo') }, deps);
    }
    if (sub === 'apply') {
      const args = { plan: opts.get('--plan'), pluginsDir: opts.get('--plugins-dir'), facts: opts.get('--facts') };
      const keys = { key: opts.get('--key'), publicKey: opts.get('--public-key'), repo: opts.get('--repo') };
      const dirs = { work: opts.get('--work'), outDir: opts.get('--out-dir'), dryRun: opts.has('--dry-run') };
      return apply({ ...args, ...keys, ...dirs }, deps);
    }
    const args = { plan: opts.get('--plan'), os: opts.get('--os'), dir: opts.get('--dir') };
    return facts({ ...args, work: opts.get('--work'), out: opts.get('--out') }, deps);
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    io.stderr.write(`backfill-sign: ${err.code}: ${err.message}\n`);
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
  plan,
  download,
  facts,
  apply,
  productionDeps,
  cliDeps,
};
