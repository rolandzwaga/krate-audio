#!/usr/bin/env node
// Builds, checks and decides the plugin feed for the feed workflow (.github/workflows/feed.yml): it collects the
// GitHub releases and the release.json each carries, runs gen-feed on them, compares the result with the live feed, and
// turns every step's exit code into the workflow's decisions (spec specs/krate-manager-phase3/spec.md FR-020 to
// FR-029e, FR-040, FR-041; plan §4.1, §4.2, PD-3 to PD-13, PD-17, PD-18).
//
// WHY: every decision of the feed workflow lives here, in code the tests drive offline, instead of in YAML and shell
// that only a hosted runner can exercise. Every gh call and the gen-feed run go through one injected `deps` object;
// `deps` is required (main throws a TypeError without it), and productionDeps, which only the entry point builds, is the
// only code that spawns anything: gh, with a fixed argument array and no shell. Files are written only at paths given
// as flags. Every line the tool prints goes through one writer that quotes a line holding a control character or
// starting "::", so no data from a release or the live feed can form a workflow command. It names no plugin: every
// name comes from a version.json under --plugins-dir.
//
// Usage:
//   node tools/feed-publish.js collect --plugins-dir <dir> --repo <owner/name> --out-dir <dir> [--annotate]
//   node tools/feed-publish.js generate --in-dir <dir> --plugins-dir <dir> --repo <owner/name> --site-dir <dir>
//        [--annotate]
//   node tools/feed-publish.js outcome --tool <collect|generate|compare> --exit <n> --github-output <file>
//   node tools/feed-publish.js compare --live <file> --feed <file> --releases <file> --dry-run <true|false>
//        [--report <file>] [--summary <file>] [--annotate]
//   node tools/feed-publish.js verdict --dry-run <true|false> --ref <ref> --build <result> --deploy <result>
//        --feed-degraded <true|false|""> --manager-degraded <true|false|""> --drift-warned <true|false|"">
//        [--evidence <dir>] [--annotate]
// Exit codes:
//   collect   0 complete; 1 abort (nothing of releases.json, manifests/, manager/, collect-report.json left in
//             --out-dir; a pre-filled --out-dir is refused untouched); 2 usage; 3 complete, manager degraded
//   generate  0 gen-feed 0, deterministic; 1 gen-feed 1, E_FEED_NONDETERMINISTIC, a staging or report failure, an
//             existing <site-dir>/manager; 2 usage (its own, or gen-feed's 2); 3 gen-feed 3, deterministic
//   outcome   0 continue (flag appended); 1 fail (E_STEP_FAILED, nothing appended); 2 usage
//   compare   0 no drift; 1 a block drift, any drift with --dry-run true, E_INPUT, E_FEED_SCHEMA; 2 usage;
//             3 --dry-run false and every drift warn
//   verdict   0 green; 1 red; 2 usage
// Errors go to stderr as `feed-publish: <CODE>: <message>`, degrades as `feed-publish: WARN <CODE>: <message>`; the
// codes are in tools/feed-contract/CONTRACT.md §7.
// Exports main(argv, io, deps), collect, generate, outcome, compare, compareFeeds, verdict, withGhRetry, productionDeps and the
// constants of plan §4.1.1.
'use strict';
const childProcess = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const contract = require('./feed-contract/contract.js');
const genFeed = require('./gen-feed.js');

const { ContractError, CODES } = contract;

const USAGE_LINES = Object.freeze([
  'usage: node tools/feed-publish.js collect --plugins-dir <dir> --repo <owner/name> --out-dir <dir> [--annotate]',
  '       node tools/feed-publish.js generate --in-dir <dir> --plugins-dir <dir> --repo <owner/name> --site-dir <dir>',
  '            [--annotate]',
  '       node tools/feed-publish.js outcome --tool <collect|generate|compare> --exit <n> --github-output <file>',
  '       node tools/feed-publish.js compare --live <file> --feed <file> --releases <file> --dry-run <true|false>',
  '            [--report <file>] [--summary <file>] [--annotate]',
  '       node tools/feed-publish.js verdict --dry-run <true|false> --ref <ref> --build <result> --deploy <result>',
  '            --feed-degraded <true|false|""> --manager-degraded <true|false|""> --drift-warned <true|false|"">',
  '            [--evidence <dir>] [--annotate]',
]);

const GH_ATTEMPTS = 3;
const GH_BACKOFF_MS = Object.freeze([5000, 15000]);
const MANAGER_TAG_RE = /^manager\/v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$/u;
const MANAGER_LATEST_URL = 'https://krateaudio.com/manager/latest.json';
const MAX_BUFFER = 64 * 1024 * 1024;
const REPO_RE = /^[A-Za-z0-9-]+\/[A-Za-z0-9._-]+$/u;
const EXIT_RE = /^(0|[1-9][0-9]*)$/u;
const RESULTS = Object.freeze(['success', 'failure', 'cancelled', 'skipped']);
const TRISTATE = Object.freeze(['true', 'false', '']);
const BOOLEANS = Object.freeze(['true', 'false']);
const OUTCOME_FLAG = Object.freeze({ collect: 'manager_degraded', generate: 'feed_degraded', compare: 'drift_warned' });
const EVIDENCE_FILES = Object.freeze([
  'releases.json',
  'collect-report.json',
  'gen-feed.log',
  'live-versions.json',
  'compare.txt',
]);
const ANNOTATION_LEVELS = new Set(['notice', 'warning', 'error']);
const ANNOTATION_TITLES = new Set([
  'feed-collect',
  'feed-generate',
  'feed-degrade',
  'feed-validate',
  'feed-compare',
  'feed-drift',
  'feed-verdict',
]);
// A code unit U+0000-U+001F or U+007F (PD-18).
const CONTROL_RE = /[\u0000-\u001f\u007f]/u;

const show = (value) => JSON.stringify(value);
const fail = (code, message, where) => new ContractError(code, message, where);
const usageError = (message) => fail('E_USAGE', message);
const isObject = (v) => v !== null && typeof v === 'object' && !Array.isArray(v);
const bytewise = (a, b) => Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));

// --- file IO

/** As tools/backfill-sign.js writeAtomic: a temp file in the same directory, then a rename; E_INPUT on any failure. */
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

/** A JSON input named by its flag (never by its path or the parser's text, so the message is stable). */
function readJsonInput(file, flag) {
  let text;
  try {
    text = fs.readFileSync(file, 'utf8');
  } catch (err) {
    throw fail('E_INPUT', `${flag}: cannot read (${err.code || err.message})`, { file });
  }
  try {
    return JSON.parse(text);
  } catch {
    throw fail('E_INPUT', `${flag}: not JSON`, { file });
  }
}

/** PD-8: `gh release list` rows, the fields selectReleases reads, each of its type (tools/backfill-sign.js:160-172). */
function checkListRows(rows, label) {
  if (!Array.isArray(rows)) throw fail('E_INPUT', `${label}: not a JSON array`);
  rows.forEach((row, i) => {
    const ok =
      isObject(row) &&
      typeof row.tagName === 'string' &&
      row.tagName !== '' &&
      typeof row.isPrerelease === 'boolean' &&
      typeof row.isDraft === 'boolean';
    if (!ok) throw fail('E_INPUT', `${label}: row ${i} is not {tagName, isPrerelease, isDraft, publishedAt}`);
  });
  return rows;
}

// --- output (PD-18 (c), §4.1.6)

/** A line as the tool may print it: quoted (JSON form) when it holds a control character or starts "::". */
function safeLine(line) {
  return CONTROL_RE.test(line) || line.startsWith('::') ? JSON.stringify(line) : line;
}

/** The writer of every line that is not an annotation. */
function emit(sink, line) {
  sink.write(`${safeLine(line)}\n`);
}

/** The only writer of a line starting "::": a GitHub workflow command, its message single-lined (FR-029e). */
function annotate(io, level, title, message) {
  if (!ANNOTATION_LEVELS.has(level) || !ANNOTATION_TITLES.has(title)) {
    throw new Error(`feed-publish bug: annotation ${show(level)} / ${show(title)} is not a fixed level and title`);
  }
  io.stdout.write(`::${level} title=${title}::${message.replace(/[\r\n]+/gu, ' ')}\n`);
}

// --- gh retry (FR-023a, PD-4, PD-5)

function flagValue(argv, flag) {
  const i = argv.indexOf(flag);
  return i >= 0 ? argv[i + 1] : undefined;
}

/** `list` for release list, `<subcommand> <tag>` for view / download (PD-5). */
function ghLabel(argv) {
  return argv[1] === 'list' ? 'list' : `${argv[1]} ${argv[2]}`;
}

/** @returns {(argv: string[]) => {status: number, stdout: string, stderr: string, attempts: number}} */
function withGhRetry(gh, { sleep, log }) {
  return (argv) => {
    for (let attempt = 1; ; attempt += 1) {
      const r = gh(argv);
      if (r.status === 0 || attempt === GH_ATTEMPTS) return { ...r, attempts: attempt };
      const wait = GH_BACKOFF_MS[attempt - 1];
      log(
        `feed-publish: WARN gh attempt ${attempt}/${GH_ATTEMPTS} failed: ${ghLabel(argv)} (exit ${r.status}); ` +
          `retrying in ${wait / 1000} s`,
      );
      const out = flagValue(argv, '--output'); // FR-023a: remove a partial download before the retry
      if (out !== undefined) fs.rmSync(out, { force: true });
      sleep(wait);
    }
  };
}

/** The only code that spawns: gh, fixed argv, shell: false, wrapped in withGhRetry (sleep = Atomics.wait). */
function productionDeps() {
  const rawGh = (argv) => {
    const r = childProcess.spawnSync('gh', argv, {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'pipe'],
      shell: false,
      maxBuffer: MAX_BUFFER,
    });
    if (r.error) return { status: -1, stdout: '', stderr: String(r.error.code || r.error.message) };
    return { status: r.status === null ? -1 : r.status, stdout: r.stdout, stderr: r.stderr };
  };
  const sleep = (ms) => {
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
  };
  return {
    gh: withGhRetry(rawGh, { sleep, log: (line) => emit(process.stderr, line) }),
    genFeedMain: genFeed.main,
  };
}

// --- collect (FR-021 to FR-026, FR-023a; plan §4.1.2, §4.1.3, PD-6 to PD-8; Clarifications 2026-10-30 (plan) P-2)

const LIST_LIMIT = 1000;
const LIST_FIELDS = 'tagName,isPrerelease,isDraft,publishedAt';
const VIEW_FIELDS = 'tagName,assets';
const DIGEST_RE = /^sha256:([0-9a-f]{64})$/u;
// The four names collect owns in --out-dir (PD-6): refused when present, removed on an abort.
const OWN_OUTPUTS = Object.freeze(['releases.json', 'manifests', 'manager', 'collect-report.json']);

const json = (value) => JSON.stringify(value, null, 2) + '\n';

/** PD-7: the plugin names, checked before any gh call, bytewise. */
function pluginNames(pluginsDir) {
  const names = genFeed.readPluginsDir(pluginsDir, { withSetupIss: false }).map((p) => p.versionJson.name);
  const byLower = new Map();
  for (const name of names) {
    if (!contract.isPluginName(name)) {
      throw fail('E_PLUGIN_NAME', `version.json name ${show(name)} is not a plugin name`, { field: 'name' });
    }
    const lower = name.toLowerCase();
    if (lower === 'manager') {
      throw fail('E_PLUGIN_NAME', `plugin ${name} lower-cases to manager, the manager's own tag prefix`, {
        plugin: name,
        field: 'name',
      });
    }
    if (byLower.has(lower)) {
      throw fail('E_DUPLICATE_PLUGIN', `plugins ${byLower.get(lower)} and ${name} are equal after lower-casing`, {
        plugin: name,
        field: 'name',
      });
    }
    byLower.set(lower, name);
  }
  return names.sort(bytewise);
}

/**
 * One gh call through deps.gh (withGhRetry in production), its attempts added to `counter.calls` (FR-026). A status
 * other than 0 after the last attempt is E_INPUT naming the subcommand, the tag and the attempts (FR-023, FR-023a).
 */
function ghRun(deps, counter, argv) {
  const r = deps.gh(argv);
  const attempts = Number.isSafeInteger(r.attempts) && r.attempts > 0 ? r.attempts : 1;
  counter.calls += attempts;
  if (r.status !== 0) {
    const what = argv[1] === 'list' ? 'gh release list' : `gh release ${argv[1]} ${argv[2]}`;
    throw fail('E_INPUT', `${what}: exit ${r.status} after ${attempts} attempt${attempts === 1 ? '' : 's'}`);
  }
  return r;
}

/** As tools/backfill-sign.js ghJson: a successful call whose stdout is JSON. */
function ghJson(deps, counter, argv, label) {
  const r = ghRun(deps, counter, argv);
  try {
    return JSON.parse(r.stdout);
  } catch {
    throw fail('E_INPUT', `${label}: output is not JSON`);
  }
}

/** PD-8: `{tagName: <tag>, assets: [{name, size, digest?}]}`, digest absent, null or sha256:<64 hex> in any case. */
function checkView(view, tag) {
  const label = `gh release view ${tag}`;
  if (!isObject(view) || !Array.isArray(view.assets)) throw fail('E_INPUT', `${label}: not {tagName, assets}`);
  if (view.tagName !== tag) throw fail('E_INPUT', `${label}: returned tagName ${show(view.tagName)}`);
  view.assets.forEach((a, i) => {
    const ok =
      isObject(a) &&
      typeof a.name === 'string' &&
      Number.isSafeInteger(a.size) &&
      a.size >= 0 &&
      (a.digest === undefined ||
        a.digest === null ||
        (typeof a.digest === 'string' && DIGEST_RE.test(a.digest.toLowerCase())));
    if (!ok) throw fail('E_INPUT', `${label}: asset ${i} is not {name, size, digest}`);
  });
  return view;
}

/** The view of `tag` (PD-8 checked). */
function viewRelease(deps, counter, repo, tag) {
  const view = ghJson(deps, counter, ['release', 'view', tag, '--repo', repo, '--json', VIEW_FIELDS], `gh release view ${tag}`);
  return checkView(view, tag);
}

/**
 * FR-022: download one asset straight to `file` (its directory exists), then check the file against the asset record.
 * An asset without a digest is refused before the download (P-2, the Phase 2 CC-verify rule).
 */
function downloadChecked(deps, counter, repo, tag, asset, file) {
  if (asset.digest === null || asset.digest === undefined) {
    throw fail('E_FEED_DOWNLOAD', `${tag}: ${asset.name} has no digest on GitHub, so it cannot be checked`);
  }
  ghRun(deps, counter, ['release', 'download', tag, '--repo', repo, '--pattern', asset.name, '--output', file]);
  let bytes;
  try {
    bytes = fs.readFileSync(file);
  } catch (err) {
    throw fail('E_INPUT', `${tag}: ${asset.name} was not written (${err.code || err.message})`, { file });
  }
  if (bytes.length !== asset.size) {
    throw fail('E_FEED_DOWNLOAD', `${tag}: ${asset.name} is ${bytes.length} bytes, the release lists ${asset.size}`);
  }
  const want = DIGEST_RE.exec(asset.digest.toLowerCase())[1];
  const got = crypto.createHash('sha256').update(bytes).digest('hex');
  if (got !== want) {
    throw fail('E_FEED_DOWNLOAD', `${tag}: ${asset.name} SHA-256 is ${got}, GitHub's digest is ${want}`);
  }
  return bytes;
}

/** FR-025: the manager's latest.json, structurally; throws E_MANAGER_LATEST (values quoted, PD-18 (b)). */
function checkLatestJson(bytes, tag, version) {
  const bad = (why) => fail('E_MANAGER_LATEST', `${tag} latest.json: ${why}`, { field: 'latest.json' });
  let doc;
  try {
    doc = JSON.parse(bytes.toString('utf8'));
  } catch {
    throw bad('not JSON');
  }
  if (!isObject(doc)) throw bad('not a JSON object');
  const v = typeof doc.version === 'string' && doc.version.startsWith('v') ? doc.version.slice(1) : doc.version;
  if (typeof doc.version !== 'string' || v !== version) {
    throw bad(`version ${show(doc.version)} is not the tag's ${version}`);
  }
  if (!isObject(doc.platforms) || Object.keys(doc.platforms).length === 0) {
    throw bad('platforms is not a non-empty object');
  }
  for (const [key, entry] of Object.entries(doc.platforms)) {
    const where = `platforms.${show(key)}`;
    if (!isObject(entry)) throw bad(`${where} is not an object`);
    if (typeof entry.signature !== 'string') throw bad(`${where}.signature is not a string`);
    try {
      contract.checkUrl(entry.url, { field: `${where}.url` });
    } catch (err) {
      if (!(err instanceof ContractError)) throw err;
      throw bad(`${where}.url ${show(entry.url)} fails contract.checkUrl (${err.code})`);
    }
  }
}

/** FR-024: the highest non-draft, non-prerelease manager/v<core> row by SemVer, or null. */
function managerRelease(rows) {
  let best = null;
  for (const row of rows) {
    if (row.isDraft || row.isPrerelease) continue;
    const m = MANAGER_TAG_RE.exec(row.tagName);
    if (m === null) continue;
    const version = `${m[1]}.${m[2]}.${m[3]}`;
    if (best === null || contract.compareSemVer(version, best.version) > 0) best = { tag: row.tagName, version };
  }
  return best;
}

/** FR-024, FR-025: `{status, tag?, message?}`; the downloaded file stays only when copied. */
function collectManager(deps, counter, repo, rows, outDir) {
  const release = managerRelease(rows);
  if (release === null) return { status: 'none' };
  const asset = viewRelease(deps, counter, repo, release.tag).assets.find((a) => a.name === 'latest.json');
  if (asset === undefined) return { status: 'none' };
  const dir = path.join(outDir, 'manager');
  fs.mkdirSync(dir);
  const bytes = downloadChecked(deps, counter, repo, release.tag, asset, path.join(dir, 'latest.json'));
  try {
    checkLatestJson(bytes, release.tag, release.version);
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    fs.rmSync(dir, { recursive: true, force: true });
    return { status: 'degraded', tag: release.tag, message: err.message };
  }
  return { status: 'copied', tag: release.tag };
}

/** @returns {0 | 3} (an abort throws a ContractError after removing what this run created; main maps it to 1) */
function collect({ pluginsDir, repo, outDir, annotate: annotating }, io, deps) {
  // 1. names first: no gh call is spent on a roster gen-feed would refuse (PD-7).
  const names = pluginNames(pluginsDir);
  // 2. a pre-filled out-dir is refused untouched (PD-6).
  try {
    fs.mkdirSync(outDir, { recursive: true });
  } catch (err) {
    throw fail('E_INPUT', `${outDir}: cannot create (${err.code || err.message})`, { file: outDir });
  }
  for (const name of OWN_OUTPUTS) {
    if (fs.existsSync(path.join(outDir, name))) {
      throw fail('E_INPUT', `${outDir} already holds ${name}`, { file: path.join(outDir, name) });
    }
  }
  const counter = { calls: 0 };
  const plugins = [];
  let manager;
  let complete = false;
  try {
    fs.mkdirSync(path.join(outDir, 'manifests'));
    // 3. the list, shape-checked before selectReleases sees it (PD-8).
    const listArgv = ['release', 'list', '--repo', repo, '--limit', String(LIST_LIMIT), '--json', LIST_FIELDS];
    const rows = checkListRows(ghJson(deps, counter, listArgv, 'gh release list'), 'gh release list');
    if (rows.length >= LIST_LIMIT) {
      throw fail('E_FEED_LIST_TRUNCATED', `gh release list returned ${rows.length} rows; releases may be missing`);
    }
    // 4. every selection before any view, so an E_TAG_FORMAT aborts first (FR-021).
    const picks = names.map((name) => [name, genFeed.selectReleases(name, rows)]);
    // 5. per plugin: view each selected release, download its release.json when it lists one (FR-022).
    for (const [name, sel] of picks) {
      if (sel === null) {
        plugins.push({ name, status: 'no-stable' });
        continue;
      }
      const manifests = [];
      for (const release of [sel.stable, sel.sibling]) {
        if (release === null) continue;
        const tag = release.tagName;
        const asset = viewRelease(deps, counter, repo, tag).assets.find((a) => a.name === 'release.json');
        if (asset === undefined) {
          manifests.push({ tag, status: 'no-manifest' });
          continue;
        }
        const file = path.join(outDir, 'manifests', `${tag.replace('/', '-')}.json`);
        downloadChecked(deps, counter, repo, tag, asset, file);
        manifests.push({ tag, status: 'downloaded' });
      }
      const sibling = sel.sibling === null ? null : sel.sibling.tagName;
      plugins.push({ name, stable: sel.stable.tagName, sibling, manifests });
    }
    // 6. the manager release (FR-024, FR-025).
    manager = collectManager(deps, counter, repo, rows, outDir);
    // 7. the two files gen-feed and generate read, last and atomically (FR-021, FR-026).
    writeAtomic(path.join(outDir, 'releases.json'), json(rows));
    writeAtomic(path.join(outDir, 'collect-report.json'), json({ plugins, manager, gh_calls: counter.calls }));
    complete = true;
  } finally {
    if (!complete) for (const name of OWN_OUTPUTS) fs.rmSync(path.join(outDir, name), { recursive: true, force: true });
  }

  // 8. stdout, through the PD-18 writer; a degraded manager is a WARN on stderr and exit 3.
  for (const p of plugins) {
    if (p.status === 'no-stable') {
      emit(io.stdout, `collect: ${p.name} no-stable`);
      continue;
    }
    const parts = p.manifests.map((m, i) => `${i === 0 ? 'stable' : 'sibling'} ${m.tag} (${m.status})`);
    emit(io.stdout, `collect: ${p.name} ${parts.join(', ')}`);
  }
  if (manager.status === 'degraded') {
    emit(io.stdout, `manager: degraded ${manager.tag}: ${manager.message}`);
    emit(io.stderr, `feed-publish: WARN E_MANAGER_LATEST: ${manager.message}`);
  } else {
    emit(io.stdout, `manager: ${manager.status}${manager.status === 'copied' ? ` ${manager.tag}` : ''}`);
  }
  if (annotating) {
    const all = plugins.flatMap((p) => p.manifests || []);
    const count = (status) => all.filter((m) => m.status === status).length;
    const noStable = plugins.filter((p) => p.status === 'no-stable').length;
    annotate(
      io,
      'notice',
      'feed-collect',
      `${count('downloaded')} downloaded, ${count('no-manifest')} no-manifest, ${noStable} no-stable, ` +
        `manager ${manager.status}`,
    );
  }
  return manager.status === 'degraded' ? 3 : 0;
}

// --- generate (FR-029a, FR-009; plan §4.1.4, PD-9)

const MANAGER_STATUSES = Object.freeze(['none', 'copied', 'degraded']);

/** Step 1: `<inDir>/collect-report.json`, an object whose manager.status is one of the three. */
function readCollectReport(inDir) {
  const file = path.join(inDir, 'collect-report.json');
  const report = readJsonInput(file, 'collect-report.json');
  const status = isObject(report) && isObject(report.manager) ? report.manager.status : undefined;
  if (!MANAGER_STATUSES.includes(status)) {
    throw fail('E_INPUT', `collect-report.json: manager.status ${show(status)} is not one of none, copied, degraded`, {
      file,
    });
  }
  return report;
}

/** One gen-feed run in-process through deps.genFeedMain, its stdout and stderr captured. */
function runGenFeed(deps, argv) {
  let stdout = '';
  let stderr = '';
  const captureIo = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = deps.genFeedMain(argv, captureIo);
  return { code, stdout, stderr };
}

/** A file's bytes for the determinism check; a missing output is E_INPUT naming it. */
function readOutput(file) {
  try {
    return fs.readFileSync(file);
  } catch (err) {
    throw fail('E_INPUT', `${file}: cannot read gen-feed's output (${err.code || err.message})`, { file });
  }
}

/** Text split on LF only (a CR stays in its line and makes emit quote it, PD-18 (c)); no trailing empty line. */
function linesOfText(text) {
  if (text === '') return [];
  const lines = text.split('\n');
  if (lines[lines.length - 1] === '') lines.pop();
  return lines;
}

/** @returns {0 | 1 | 2 | 3} gen-feed's exit code (1 and 2 returned before the second run). */
function generate({ inDir, pluginsDir, repo, siteDir, annotate: annotating }, io, deps) {
  // 1. the report decides --manager-latest-url and the staging.
  const copied = readCollectReport(inDir).manager.status === 'copied';
  // 2. gen-feed does no mkdir; nothing under <siteDir>/manager is made by anyone but this run.
  try {
    fs.mkdirSync(siteDir, { recursive: true });
  } catch (err) {
    throw fail('E_INPUT', `${siteDir}: cannot create (${err.code || err.message})`, { file: siteDir });
  }
  const siteManager = path.join(siteDir, 'manager');
  if (fs.existsSync(siteManager)) throw fail('E_INPUT', `${siteManager} already exists`, { file: siteManager });
  // 3. argv
  const feedFile = path.join(siteDir, 'versions.json');
  const againFile = path.join(inDir, 'versions.again.json');
  const argvFor = (out) => [
    '--plugins-dir',
    pluginsDir,
    '--releases',
    path.join(inDir, 'releases.json'),
    '--manifests',
    path.join(inDir, 'manifests'),
    '--repo',
    repo,
    ...(copied ? ['--manager-latest-url', MANAGER_LATEST_URL] : []),
    '--out',
    out,
  ];
  // 4. run 1: its stderr is the log verdict quotes, echoed here through the PD-18 writer.
  const run1 = runGenFeed(deps, argvFor(feedFile));
  writeAtomic(path.join(inDir, 'gen-feed.log'), run1.stderr);
  const stderrLines = linesOfText(run1.stderr);
  for (const line of stderrLines) emit(io.stderr, line);
  if (run1.code !== 0 && run1.code !== 3) return run1.code;
  // 5. run 2: same inputs, same exit code, same stderr, same bytes (FR-009, PD-9).
  const run2 = runGenFeed(deps, argvFor(againFile));
  if (run2.code !== run1.code) {
    throw fail('E_FEED_NONDETERMINISTIC', `the second gen-feed run exited ${run2.code}, the first ${run1.code}`);
  }
  if (run2.stderr !== run1.stderr) {
    throw fail('E_FEED_NONDETERMINISTIC', 'the second gen-feed run printed a different stderr than the first');
  }
  if (!readOutput(againFile).equals(readOutput(feedFile))) {
    throw fail('E_FEED_NONDETERMINISTIC', `${againFile} differs from ${feedFile}`, { file: againFile });
  }
  // 6. stage manager/latest.json; on any fs error remove the directory this run created.
  if (copied) {
    const source = path.join(inDir, 'manager', 'latest.json');
    const target = path.join(siteManager, 'latest.json');
    let createdManager = false;
    try {
      fs.mkdirSync(siteManager);
      createdManager = true;
      fs.copyFileSync(source, target, fs.constants.COPYFILE_EXCL);
    } catch (err) {
      if (createdManager) fs.rmSync(siteManager, { recursive: true, force: true });
      throw fail('E_INPUT', `${target}: cannot stage ${source} (${err.code || err.message})`, { file: target });
    }
  }
  // 7. annotations
  if (annotating) {
    annotate(io, 'notice', 'feed-generate', `exit ${run1.code}, deterministic`);
    for (const line of stderrLines) {
      if (line.startsWith('gen-feed: WARN ')) annotate(io, 'warning', 'feed-degrade', safeLine(line));
    }
  }
  // 8.
  return run1.code;
}

// --- outcome (FR-029b)

/** @returns {0 | 1} */
function outcome({ tool, exit, githubOutput }, io) {
  if (exit !== 0 && exit !== 3) {
    emit(io.stderr, `feed-publish: E_STEP_FAILED: ${tool} exited ${exit}`);
    return 1;
  }
  const line = `${OUTCOME_FLAG[tool]}=${exit === 3 ? 'true' : 'false'}\n`;
  try {
    fs.appendFileSync(githubOutput, line);
  } catch (err) {
    throw fail('E_INPUT', `${githubOutput}: cannot append (${err.code || err.message})`, { file: githubOutput });
  }
  return 0;
}

// --- verdict (FR-029c, PD-12)

/** The lines of an evidence file split on LF only (a CR stays in its line, PD-18), or null when unreadable. */
function evidenceLines(evidence, name) {
  if (evidence === undefined) return null;
  try {
    const text = fs.readFileSync(path.join(evidence, name), 'utf8');
    const lines = text.split('\n');
    if (lines[lines.length - 1] === '') lines.pop();
    return lines;
  } catch {
    return null;
  }
}

function managerMessage(evidence) {
  if (evidence === undefined) return null;
  try {
    const doc = JSON.parse(fs.readFileSync(path.join(evidence, 'collect-report.json'), 'utf8'));
    const message = doc && doc.manager && doc.manager.message;
    return typeof message === 'string' ? message : null;
  } catch {
    return null;
  }
}

function evidencePresent(evidence, name) {
  return evidence !== undefined && fs.existsSync(path.join(evidence, name));
}

/** @returns {0 | 1} reasons in plan PD-12 order; each reason is its lines, already through safeLine. */
function verdict(args, io) {
  const { dryRun, ref, build, deploy, feedDegraded, managerDegraded, driftWarned, evidence } = args;
  const reasons = [];
  const refReason = !dryRun && ref !== 'refs/heads/main';
  if (build !== 'success') reasons.push([`build ${build}`]);
  if (refReason) reasons.push([`real run from ${CONTROL_RE.test(ref) ? show(ref) : ref}`]);
  if (build === 'success' && !refReason) {
    const deployOk = deploy === 'success' || (dryRun && deploy === 'skipped');
    if (!deployOk) reasons.push([`deploy ${deploy}`]);
  }
  if (feedDegraded === 'true') {
    const lines = evidenceLines(evidence, 'gen-feed.log');
    const warn = lines === null ? ['(unreadable)'] : lines.filter((l) => l.startsWith('gen-feed: WARN '));
    reasons.push(['feed degraded:', ...warn.map(safeLine)]);
  }
  if (managerDegraded === 'true') {
    const message = managerMessage(evidence);
    reasons.push([`manager latest.json degraded: ${message === null ? '(unreadable)' : safeLine(message)}`]);
  }
  if (driftWarned === 'true') {
    const lines = evidenceLines(evidence, 'compare.txt');
    const warn = lines === null ? null : lines.filter((l) => /^drift .* warn$/su.test(l)).map(safeLine);
    reasons.push([`v1 drift deployed: ${warn === null ? '(unreadable)' : warn.join(' / ')}`]);
  }
  if (build === 'success') {
    for (const name of EVIDENCE_FILES) if (!evidencePresent(evidence, name)) reasons.push([`evidence missing ${name}`]);
  }

  if (reasons.length === 0) {
    emit(io.stdout, 'verdict: green');
    if (args.annotate) annotate(io, 'notice', 'feed-verdict', 'green');
    return 0;
  }
  for (const [first, ...rest] of reasons) {
    emit(io.stdout, `verdict: red ${first}`);
    for (const line of rest) emit(io.stdout, line);
  }
  if (args.annotate) for (const lines of reasons) annotate(io, 'error', 'feed-verdict', lines.join('\n'));
  return 1;
}

// --- compare (FR-027, FR-040, FR-041; plan §4.2, PD-10, PD-11, PD-13, PD-18)

// The drift kinds in their fixed print order, with their severity (FR-040, Q1).
const DRIFT_SEVERITY = Object.freeze({
  'release_notes not empty': 'block',
  'version lowered': 'block',
  removed: 'block',
  'version changed without a release': 'warn',
  'download_url changed without a newer release': 'warn',
  'added without a release': 'warn',
});
const PUBLISHED_AT_RE = /^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:.]+Z$/u;
const V1_KEYS = Object.freeze(['version', 'download_url', 'release_notes']);

// PD-18 (b): a data-derived value is printed raw only when it passes its own validator.
const isVersion = (v) => contract.parseSemVer(v) !== null;
const pName = (name) => (contract.isPluginName(name) ? name : show(name));
const pVersion = (v) => (isVersion(v) ? v : show(v));
const pTag = (name, v) => {
  const tag = `${name.toLowerCase()}/v${v}`;
  return contract.isPluginName(name) && isVersion(v) ? tag : show(tag);
};
const pPublishedAt = (at) => (typeof at === 'string' && PUBLISHED_AT_RE.test(at) ? at : show(at));
function pUrl(url) {
  try {
    contract.checkUrl(url);
    return url;
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    return show(url);
  }
}

/** PD-11: true only for two SemVer values with a > b; never hands compareSemVer anything else. */
const gt = (a, b) => isVersion(a) && isVersion(b) && contract.compareSemVer(a, b) > 0;

/** The first matching case of plan §4.2 for one plugin; `l` / `n` its live / new v1 entry, or undefined. */
function comparePlugin(name, l, n, rows) {
  const lower = name.toLowerCase();
  const rowFor = (v, prerelease) =>
    rows.find((r) => !r.isDraft && r.isPrerelease === prerelease && r.tagName === `${lower}/v${v}`);
  const stableRow = (v) => rowFor(v, false);
  const preRow = (v) => rowFor(v, true);
  const shown = pName(name);
  const both = l !== undefined && n !== undefined;
  // Case 2's release condition: a newer version that a stable release carries.
  const released = both && gt(n.version, l.version) ? stableRow(n.version) : undefined;

  if (both) {
    const urlEqual = l.download_url === n.download_url;
    const notesEqual = l.release_notes === n.release_notes;
    if (l.version === n.version && urlEqual && notesEqual) {
      return { name, kind: 'same', line: `same ${shown} ${pVersion(n.version)}` };
    }
    if (released !== undefined && notesEqual) {
      const url = urlEqual ? '' : ` download_url ${pUrl(l.download_url)} -> ${pUrl(n.download_url)}`;
      const at = `(${pTag(name, n.version)} ${pPublishedAt(released.publishedAt)})`;
      const line = `newer ${shown} ${pVersion(l.version)} -> ${pVersion(n.version)} ${at}${url}`;
      return { name, kind: 'newer', line };
    }
    if (preRow(l.version) && !stableRow(l.version) && urlEqual && notesEqual) {
      const sel = genFeed.selectReleases(name, rows);
      if (sel !== null && sel.stable.version === n.version) {
        const line =
          `q9-corrected ${shown} ${pVersion(l.version)} -> ${pVersion(n.version)} ` +
          `(prerelease ${pTag(name, l.version)})`;
        return { name, kind: 'q9-corrected', line };
      }
    }
  } else if (l !== undefined) {
    if (preRow(l.version) && genFeed.selectReleases(name, rows) === null) {
      const line = `q9-corrected ${shown} ${pVersion(l.version)} -> absent (prerelease ${pTag(name, l.version)})`;
      return { name, kind: 'q9-corrected', line };
    }
  } else {
    const row = stableRow(n.version);
    if (row) {
      const line = `added ${shown} ${pVersion(n.version)} (${pTag(name, n.version)} ${pPublishedAt(row.publishedAt)})`;
      return { name, kind: 'added', line };
    }
  }

  // Case 6. Every condition below is the kind table's; `released === undefined` is "not case 2's release condition".
  const applies = {
    'release_notes not empty': both && (l.release_notes !== '' || n.release_notes !== ''),
    'version lowered': both && gt(l.version, n.version),
    removed: n === undefined,
    'version changed without a release':
      both && l.version !== n.version && !gt(l.version, n.version) && released === undefined,
    'download_url changed without a newer release': both && l.download_url !== n.download_url && released === undefined,
    'added without a release': l === undefined,
  };
  const kinds = Object.keys(DRIFT_SEVERITY).filter((kind) => applies[kind]);
  const severity = kinds.some((kind) => DRIFT_SEVERITY[kind] === 'block') ? 'block' : 'warn';
  return { name, kind: 'drift', drift: { kinds, severity }, line: `drift ${shown} ${kinds.join('; ')} ${severity}` };
}

/**
 * The pure rule (plan §4.2). Only plugins.<name>.{version, download_url, release_notes} of either feed are read, so a
 * raw feed and its v1 projection give the same result. `lines` are the per-plugin lines, bytewise by name.
 * @returns {{lines: string[], summaryLine: string, plugins: Array<{name: string, kind: string,
 *            drift?: {kinds: string[], severity: 'block'|'warn'}, line: string}>,
 *            counts: {same: number, newer: number, added: number, q9: number, drift: number, block: number,
 *                     warn: number}}}
 */
function compareFeeds({ liveDoc, feedDoc, rows }) {
  const live = liveDoc.plugins;
  const next = feedDoc.plugins;
  const entry = (doc, name) => (Object.hasOwn(doc, name) ? doc[name] : undefined);
  const names = [...new Set([...Object.keys(live), ...Object.keys(next)])].sort(bytewise);
  const counts = { same: 0, newer: 0, added: 0, q9: 0, drift: 0, block: 0, warn: 0 };
  const plugins = names.map((name) => {
    const p = comparePlugin(name, entry(live, name), entry(next, name), rows);
    counts[p.kind === 'q9-corrected' ? 'q9' : p.kind] += 1;
    if (p.drift !== undefined) counts[p.drift.severity] += 1;
    return p;
  });
  const summaryLine =
    `compare: ${counts.same} same, ${counts.newer} newer, ${counts.added} added, ${counts.q9} q9-corrected, ` +
    `${counts.drift} drift (${counts.block} block, ${counts.warn} warn)`;
  return { lines: plugins.map((p) => p.line), summaryLine, plugins, counts };
}

/** gen-feed's v1 projection of an input feed; its E_INPUT names the flag. */
function projectInput(doc, flag) {
  try {
    return JSON.parse(genFeed.projectV1(doc));
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    throw fail(err.code, `${flag}: ${err.message}`, { file: flag });
  }
}

/** PD-18 (a): no live key, and no live string v1 value, holds a control character. */
function refuseControlCharacters(livePlugins) {
  for (const [key, value] of Object.entries(livePlugins)) {
    const strings = V1_KEYS.map((k) => value[k]).filter((v) => typeof v === 'string');
    if (CONTROL_RE.test(key) || strings.some((v) => CONTROL_RE.test(v))) {
      throw fail('E_INPUT', `live plugins.${show(key)} holds a control character`, { field: 'plugins' });
    }
  }
}

/** The checks before the rule, in plan §4.2 order, then the rule. Throws ContractError (E_INPUT, E_FEED_SCHEMA). */
function compareInputs(live, feed, releases) {
  const liveRaw = readJsonInput(live, '--live');
  const feedRaw = readJsonInput(feed, '--feed');
  const rows = readJsonInput(releases, '--releases');
  for (const schema of ['v2', 'v1']) {
    const { valid, errors } = contract.validateFeed(feedRaw, { schema });
    if (!valid) {
      const { instancePath, message } = errors[0];
      throw fail('E_FEED_SCHEMA', `${schema} ${instancePath}: ${message}`, { instancePath });
    }
  }
  checkListRows(rows, '--releases');
  const liveDoc = projectInput(liveRaw, '--live');
  const feedDoc = projectInput(feedRaw, '--feed');
  refuseControlCharacters(liveDoc.plugins);
  return compareFeeds({ liveDoc, feedDoc, rows });
}

/** PD-18 (d): a fence longer than any backtick run in the lines, so no value can close it. */
function appendSummary(file, lines) {
  const longest = (lines.join('\n').match(/`+/gu) || []).reduce((m, run) => Math.max(m, run.length), 0);
  const fence = '`'.repeat(Math.max(3, longest + 1));
  try {
    fs.appendFileSync(file, `${fence}text\n${lines.map((l) => `${l}\n`).join('')}${fence}\n`);
  } catch (err) {
    throw fail('E_INPUT', `${file}: cannot append (${err.code || err.message})`, { file });
  }
}

/**
 * FR-027. The report and the summary are written whatever the exit code; on E_INPUT / E_FEED_SCHEMA there are no
 * result lines, so the report is empty (PD-13). @returns {0 | 1 | 3}
 */
function compare({ live, feed, releases, dryRun, report, summary, annotate: annotating }, io) {
  let result = null;
  let failure = null;
  try {
    result = compareInputs(live, feed, releases);
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    failure = err;
  }
  const lines = result === null ? [] : [...result.lines, result.summaryLine];
  for (const line of lines) emit(io.stdout, line);
  const printed = lines.map(safeLine); // what stdout carries, so the report equals it (PD-13)
  if (report !== undefined) writeAtomic(report, printed.map((l) => `${l}\n`).join(''));
  if (summary !== undefined) appendSummary(summary, printed);
  if (failure !== null) {
    if (annotating && failure.code === 'E_FEED_SCHEMA') {
      annotate(io, 'error', 'feed-validate', safeLine(failure.message));
    }
    throw failure;
  }

  const fails = (p) => dryRun || p.drift.severity === 'block';
  const drifts = result.plugins.filter((p) => p.kind === 'drift');
  for (const p of drifts) emit(io.stderr, `feed-publish: ${fails(p) ? '' : 'WARN '}E_V1_DRIFT: ${p.line}`);
  if (annotating) {
    annotate(io, 'notice', 'feed-validate', 'v2 ok, v1 ok');
    annotate(io, 'notice', 'feed-compare', result.summaryLine);
    for (const p of drifts) annotate(io, fails(p) ? 'error' : 'warning', 'feed-drift', safeLine(p.line));
  }
  const { drift, block } = result.counts;
  if (drift === 0) return 0;
  return block > 0 || dryRun ? 1 : 3;
}

// --- CLI (§4.1.7)

const SUBCOMMANDS = Object.freeze({
  collect: { required: ['--plugins-dir', '--repo', '--out-dir'], optional: [], switches: ['--annotate'] },
  generate: { required: ['--in-dir', '--plugins-dir', '--repo', '--site-dir'], optional: [], switches: ['--annotate'] },
  outcome: { required: ['--tool', '--exit', '--github-output'], optional: [], switches: [] },
  compare: {
    required: ['--live', '--feed', '--releases', '--dry-run'],
    optional: ['--report', '--summary'],
    switches: ['--annotate'],
  },
  verdict: {
    required: ['--dry-run', '--ref', '--build', '--deploy', '--feed-degraded', '--manager-degraded', '--drift-warned'],
    optional: ['--evidence'],
    switches: ['--annotate'],
  },
});
// The verdict flags whose value may be '' (a job output that was never written, FR-029c).
const EMPTY_OK = Object.freeze(['--feed-degraded', '--manager-degraded', '--drift-warned']);
// Flag -> allowed values (checked for every subcommand that has the flag).
const ENUMS = Object.freeze({
  '--dry-run': BOOLEANS,
  '--build': RESULTS,
  '--deploy': RESULTS,
  '--feed-degraded': TRISTATE,
  '--manager-degraded': TRISTATE,
  '--drift-warned': TRISTATE,
  '--tool': Object.keys(OUTCOME_FLAG),
});

/** @returns {{sub: string, opts: Map<string, string | true>}} */
function parseArgs(argv) {
  const [sub, ...rest] = argv;
  if (sub === undefined) throw usageError('a subcommand is required');
  if (!Object.hasOwn(SUBCOMMANDS, sub)) throw usageError(`unknown subcommand ${show(sub)}`);
  const { required, optional, switches } = SUBCOMMANDS[sub];
  const opts = new Map();
  for (let i = 0; i < rest.length; i += 1) {
    const a = rest[i];
    if (!required.includes(a) && !optional.includes(a) && !switches.includes(a)) {
      throw usageError(`${show(a)} is not a flag of ${sub}`);
    }
    if (opts.has(a)) throw usageError(`${a} given twice`);
    if (switches.includes(a)) {
      opts.set(a, true);
      continue;
    }
    const value = rest[i + 1];
    const emptyOk = sub === 'verdict' && EMPTY_OK.includes(a);
    if (value === undefined || (value === '' && !emptyOk) || value.startsWith('--')) {
      throw usageError(`${a} needs a value`);
    }
    opts.set(a, value);
    i += 1;
  }
  for (const flag of required) if (!opts.has(flag)) throw usageError(`${sub} needs ${flag}`);
  if (opts.has('--repo') && !REPO_RE.test(opts.get('--repo'))) {
    throw usageError(`--repo ${show(opts.get('--repo'))} is not <owner>/<name>`);
  }
  for (const [flag, allowed] of Object.entries(ENUMS)) {
    if (opts.has(flag) && !allowed.includes(opts.get(flag))) {
      throw usageError(`${flag} ${show(opts.get(flag))} is not one of ${allowed.map(show).join(', ')}`);
    }
  }
  if (opts.has('--exit')) {
    const exit = opts.get('--exit');
    if (!EXIT_RE.test(exit) || !Number.isSafeInteger(Number(exit))) {
      throw usageError(`--exit ${show(exit)} is not a non-negative integer`);
    }
  }
  return { sub, opts };
}

/**
 * @param {string[]} argv
 * @param {{stdout: {write(s: string): unknown}, stderr: {write(s: string): unknown}}} io
 * @param {{gh: (argv: string[]) => object, genFeedMain: (argv: string[], io: object) => number}} deps  required
 * @returns {0 | 1 | 2 | 3}
 */
function main(argv, io, deps) {
  if (deps === null || typeof deps !== 'object') {
    throw new TypeError(
      'feed-publish: main(argv, io, deps): deps is required (an object with gh and genFeedMain; ' +
        'only the entry point builds productionDeps())',
    );
  }
  try {
    const { sub, opts } = parseArgs(argv);
    if (sub === 'outcome') {
      const args = { tool: opts.get('--tool'), exit: Number(opts.get('--exit')) };
      return outcome({ ...args, githubOutput: opts.get('--github-output') }, io);
    }
    if (sub === 'verdict') {
      const runArgs = { dryRun: opts.get('--dry-run') === 'true', ref: opts.get('--ref') };
      const results = { build: opts.get('--build'), deploy: opts.get('--deploy') };
      const flags = {
        feedDegraded: opts.get('--feed-degraded'),
        managerDegraded: opts.get('--manager-degraded'),
        driftWarned: opts.get('--drift-warned'),
      };
      const out = { evidence: opts.get('--evidence'), annotate: opts.has('--annotate') };
      return verdict({ ...runArgs, ...results, ...flags, ...out }, io);
    }
    if (sub === 'compare') {
      const files = { live: opts.get('--live'), feed: opts.get('--feed'), releases: opts.get('--releases') };
      const out = { report: opts.get('--report'), summary: opts.get('--summary'), annotate: opts.has('--annotate') };
      return compare({ ...files, dryRun: opts.get('--dry-run') === 'true', ...out }, io);
    }
    if (sub === 'generate') {
      const dirs = { inDir: opts.get('--in-dir'), pluginsDir: opts.get('--plugins-dir'), siteDir: opts.get('--site-dir') };
      return generate({ ...dirs, repo: opts.get('--repo'), annotate: opts.has('--annotate') }, io, deps);
    }
    const dirs = { pluginsDir: opts.get('--plugins-dir'), outDir: opts.get('--out-dir') };
    return collect({ ...dirs, repo: opts.get('--repo'), annotate: opts.has('--annotate') }, io, deps);
  } catch (err) {
    if (!(err instanceof ContractError)) throw err;
    emit(io.stderr, `feed-publish: ${err.code}: ${err.message}`);
    if (CODES[err.code] === 'usage') {
      for (const line of USAGE_LINES) emit(io.stderr, line);
      return 2;
    }
    return 1;
  }
}

if (require.main === module) process.exitCode = main(process.argv.slice(2), process, productionDeps());

module.exports = {
  main,
  collect,
  generate,
  outcome,
  compare,
  compareFeeds,
  verdict,
  withGhRetry,
  productionDeps,
  GH_ATTEMPTS,
  GH_BACKOFF_MS,
  MANAGER_TAG_RE,
  MANAGER_LATEST_URL,
};
