'use strict';
require('./helpers/write-guard.js');

// FR-091 (as amended by SI-20 / SI-34): .github/workflows/release-manager.yml keeps the Krate Manager Phase 11 shape
// (spec specs/krate-manager-phase11/spec.md FR-050 to FR-078, D14; plan §4.10 job table and "Where keys exist",
// §4.12's release-manager-shape paragraph, both as of revision 4; PD-13, PD-19 to PD-24, PD-27, PD-34, PD-35; SI-10,
// SI-13, SI-14, SI-20, SI-21, SI-25, SI-34, SI-37, SI-38, SI-40; tasks T015).
// The workflow is read as text with the indentation-based helpers of manager-ci-shape.test.js:43-151 (this file's own
// copy, verbatim): 2-space job keys under `jobs:`, 4-space job keys, 6-space `- ` step items, 8-space step keys. No
// YAML library, no child process. check(text) is pure and returns every finding; each mutation subtest applies one
// string edit to an in-memory copy and requires a matching finding. Files are only read.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const WORKFLOW = path.join(REPO_ROOT, '.github', 'workflows', 'release-manager.yml');
const MANAGER_CI = path.join(REPO_ROOT, '.github', 'workflows', 'manager-ci.yml');
const PLUGINS_DIR = path.join(REPO_ROOT, 'plugins');

function readText(file) {
  return fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n');
}

// --- YAML-text helpers (copied verbatim from manager-ci-shape.test.js:43-151).

function indentOf(line) {
  return line.length - line.trimStart().length;
}

function stripComment(value) {
  return value.replace(/\s+#.*$/, '').trim();
}

function unquote(value) {
  const m = /^'(.*)'$/.exec(value) || /^"(.*)"$/.exec(value);
  return m ? m[1] : value;
}

/** Names of the 2-space keys under the top-level `jobs:` mapping, in file order (empty when there is no jobs:). */
function jobNames(text) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => /^jobs:\s*$/.test(l));
  if (start < 0) return [];
  const names = [];
  for (const line of lines.slice(start + 1)) {
    if (/^\S/.test(line)) break;
    const m = /^ {2}([A-Za-z0-9_-]+):\s*$/.exec(line);
    if (m) names.push(m[1]);
  }
  return names;
}

/** The text of job `job` under `jobs:`: its key line up to the next line indented 0-2 spaces, or null. */
function jobBlock(text, job) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => /^jobs:\s*$/.test(l));
  if (start < 0) return null;
  const head = lines.findIndex((l, i) => i > start && l === `  ${job}:`);
  if (head < 0) return null;
  let end = head + 1;
  while (end < lines.length && !(lines[end].trim() !== '' && indentOf(lines[end]) <= 2)) end++;
  return lines.slice(head, end).join('\n');
}

/** The job's steps: each 6-space `- ` item, as {name, label, text, index}; label is the name or `uses: <action>`. */
function stepBlocks(jobText) {
  const lines = jobText.split('\n');
  const steps = [];
  let current = null;
  for (const line of lines) {
    if (/^ {6}- /.test(line)) {
      const named = /^ {6}- name:\s*(.*)$/.exec(line);
      const uses = /^ {6}- uses:\s*(.*)$/.exec(line);
      const name = named ? unquote(stripComment(named[1])) : '';
      current = { name, label: name || (uses ? `uses: ${stripComment(uses[1])}` : ''), lines: [line] };
      steps.push(current);
    } else if (current && line.trim() !== '' && indentOf(line) < 6) {
      current = null;
    } else if (current) {
      current.lines.push(line);
    }
  }
  return steps.map((s, index) => ({ name: s.name, label: s.label, text: s.lines.join('\n'), index }));
}

/** The value of the first `key:` line at exactly `indent` spaces in `block` (comments stripped), or null. */
function field(block, indent, key) {
  const re = new RegExp(`^ {${indent}}${key.replace(/[-]/g, '\\-')}:(.*)$`);
  for (const line of block.split('\n')) {
    const m = re.exec(line);
    if (m) return stripComment(m[1]);
  }
  return null;
}

/** The lines nested under the `key:` line at exactly `indent` spaces (its block value), or null. */
function nested(block, indent, key) {
  const lines = block.split('\n');
  const head = lines.findIndex((l) => new RegExp(`^ {${indent}}${key}:\\s*(\\|[-+]?)?\\s*$`).test(l));
  if (head < 0) return null;
  const out = [];
  for (const line of lines.slice(head + 1)) {
    if (line.trim() !== '' && indentOf(line) <= indent) break;
    out.push(line);
  }
  return out;
}

/** Trimmed, non-blank lines of a nested() result, or null. */
function trimmed(lines) {
  return lines === null ? null : lines.map((l) => l.trim()).filter((l) => l !== '');
}

/** A step's `run:` command text (block or single line), or ''. */
function runText(step) {
  const block = nested(step.text, 8, 'run');
  if (block) return block.join('\n');
  const single = /^ {8}run:\s*(.*)$/m.exec(step.text);
  return single ? single[1] : '';
}

/** A run: script's trimmed, non-blank lines. */
function scriptLines(step) {
  return runText(step).split('\n').map((l) => l.trim()).filter((l) => l !== '');
}

function count(text, needle) {
  return text.split(needle).length - 1;
}

// --- Small readers of this workflow's own forms.

/** The `uses:` of a step, on its `- uses:` item line or as an 8-space key, or null. */
function usesOf(step) {
  if (step.label.startsWith('uses: ')) return step.label.slice('uses: '.length);
  return field(step.text, 8, 'uses');
}

/** A step's `env:` lines, trimmed (empty when it has none). */
function stepEnv(step) {
  return trimmed(nested(step.text, 8, 'env')) || [];
}

/** A step's `with:` lines, trimmed (empty when it has none). */
function stepWith(step) {
  return trimmed(nested(step.text, 8, 'with')) || [];
}

/** The `matrix.include` items of a job, as objects of their 12-space `key: value` lines. */
function matrixInclude(jobText) {
  const lines = nested(jobText, 8, 'include');
  if (lines === null) return [];
  const items = [];
  for (const line of lines) {
    const first = /^ {10}- ([A-Za-z0-9_-]+):\s*(.*)$/.exec(line);
    const more = /^ {12}([A-Za-z0-9_-]+):\s*(.*)$/.exec(line);
    if (first) items.push({ [first[1]]: unquote(stripComment(first[2])) });
    else if (more && items.length > 0) items[items.length - 1][more[1]] = unquote(stripComment(more[2]));
  }
  return items;
}

/** The `matrix.os: [a, b]` list of a job, or null. */
function matrixOs(jobText) {
  const v = field(jobText, 8, 'os');
  const m = v === null ? null : /^\[(.*)\]$/.exec(v);
  return m ? m[1].split(',').map((s) => s.trim()) : null;
}

/**
 * Whether a step `if:` holds for one matrix leg. Understands only this workflow's forms: atoms `matrix.<k> == 'v'`,
 * `matrix.<k> != 'v'`, `runner.os == 'v'`, `runner.os != 'v'`, joined by `||` or by `&&` (not both); null when the
 * condition is outside that grammar (a finding, so the per-leg checks never guess).
 */
function holdsFor(cond, leg) {
  if (cond === null) return true;
  const body = cond.replace(/^\$\{\{\s*/, '').replace(/\s*\}\}$/, '').trim();
  const any = body.includes('||');
  const parts = body.split(any ? '||' : '&&').map((s) => s.trim());
  const values = parts.map((p) => {
    const m = /^(matrix\.[A-Za-z0-9_-]+|runner\.os)\s*(==|!=)\s*'([^']*)'$/.exec(p);
    if (!m) return null;
    const have = m[1] === 'runner.os' ? leg.runnerOs : leg[m[1].slice('matrix.'.length)];
    return m[2] === '==' ? have === m[3] : have !== m[3];
  });
  if (values.includes(null)) return null;
  return any ? values.some(Boolean) : values.every(Boolean);
}

const RUNNER_OS = { 'windows-latest': 'Windows', 'macos-latest': 'macOS', 'ubuntu-24.04': 'Linux' };

// --- The Phase 11 shape (plan §4.10 as of revision 4, binding).

const JOBS = [
  'preflight',
  'main-only',
  'build',
  'sign',
  'sign-dry',
  'package-macos',
  'updater-e2e',
  'first-install',
  'publish',
  'dry-publish',
  'feed',
  'feed-dry',
  'landing',
  'landing-dry',
];
const REAL = '${{ !inputs.dry_run }}';
const DRY = '${{ inputs.dry_run }}';
// Plan §4.10's table: `needs` and `if` of every job (PD-19, PD-23).
const NEEDS_IF = {
  preflight: [null, null],
  'main-only': ['preflight', null],
  build: ['[preflight, main-only]', null],
  sign: ['[preflight, build]', REAL],
  'sign-dry': ['[preflight, build]', DRY],
  'package-macos': ['[preflight, build]', null],
  'updater-e2e': ['preflight', null],
  'first-install': ['[build, package-macos]', null],
  publish: ['[preflight, build, sign, package-macos, updater-e2e, first-install]', REAL],
  'dry-publish': ['[preflight, build, sign-dry, package-macos, updater-e2e, first-install]', DRY],
  feed: ['publish', REAL],
  'feed-dry': ['dry-publish', DRY],
  // Krate Manager Phase 12 PD-25 (SI-3: needs.preflight.outputs exists only for a direct dependency).
  landing: ['[preflight, feed]', REAL],
  'landing-dry': ['[preflight, feed-dry]', DRY],
};
const SIGNING_JOBS = ['main-only', 'sign', 'publish'];
const WRITE_JOBS = ['publish', 'feed', 'feed-dry', 'landing'];
const NO_SECRET_JOBS = ['preflight', 'main-only', 'build', 'sign-dry', 'package-macos', 'updater-e2e', 'first-install', 'publish', 'dry-publish', 'landing', 'landing-dry'];

// Phase 12 PD-25: the landing render step both landing jobs run, and its env (the REPO line of release.yml / docs.yml).
const LANDING_RENDER =
  'mkdir -p _site/assets && cp docs/assets/style.css _site/assets/ && ' +
  'node tools/gen-root-index.js --out _site/index.html --manager-version "$VERSION"';
const LANDING_ENV = ['VERSION: ${{ needs.preflight.outputs.version }}', 'REPO: ${{ github.repository }}'];
// SC-032, FR-105: an automated advisory scan invoked from a workflow or a tool.
const ADVISORY_SCAN_RE = /\bcargo[ \t]+(?:audit|deny)\b/;

// PD-35: the release signing steps every signing job shares byte for byte (name and run).
const SHARED_SIGNING_STEPS = ['install rsign2', 'updater asset list', 'sign updater artifacts', 'delete signing key (after signing)'];
const RSIGN_INSTALL = ['cargo install rsign2 --version 0.6.7 --locked', 'test "$(rsign --version)" = "rsign2 0.6.7"'];
const NPM_SIGN = 'npm ci --prefix tools/feed-contract --ignore-scripts';
const SIGN_LINE =
  'rsign sign -W -s "$RUNNER_TEMP/krate.key" -t "timestamp:$(date +%s)"$\'\\t\'"file:$name"$\'\\t\'"version:$VERSION" ' +
  '-x "$RUNNER_TEMP/$name.minisig" "$ASSET_DIR/$name" < /dev/null';
const SIG_LINE = 'base64 -w0 < "$RUNNER_TEMP/$name.minisig" > "$ASSET_DIR/$name.sig"';
const THROWAWAY_LINE = 'rsign generate -W -p "$RUNNER_TEMP/krate.pub" -s "$RUNNER_TEMP/krate.key" < /dev/null';
const DELETE_LINE = 'rm -f "$RUNNER_TEMP/krate.key"';
const SECRET_ENV = [
  'KRATE_SIGNING_KEY: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}',
  'KRATE_SIGNING_KEY_PASSWORD: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD }}',
];

const BUILD_STEPS = [
  'extra config empty',
  'bundle',
  'leg key is not the release key',
  'updater-e2e absent',
  'ci-temp-roots absent',
  'build warnings',
  'signatures',
  'artifact version',
];
const BUILD_MACOS_STEPS = ['lipo', 'plist: LSMinimumSystemVersion', 'plist: no LSFileQuarantineEnabled', 'codesign verify', 'codesign ad-hoc'];
const BUILD_LINUX_STEPS = ['glibc floor'];
// FR-053: the manager-ci.yml:246-259 `build warnings` rule, copied unchanged (the subtest below compares it).
const BUILD_WARNINGS = [
  'set -euo pipefail',
  'clean="$RUNNER_TEMP/tauri-build.clean.txt"',
  "esc=$(printf '\\033')",
  'sed "s/${esc}\\[[0-9;]*m//g" "$RUNNER_TEMP/tauri-build.txt" > "$clean"',
  "finished=$(grep -cE '^ *Finished `release` profile' \"$clean\" || true)",
  "n=$(grep -c '^warning:' \"$clean\" || true)",
  'if [ "$finished" -ge 1 ] && [ "$n" = "0" ]; then',
  'echo "::notice title=build warnings ($RUNNER_OS)::0 warning lines in the release build"',
  'else',
  "grep '^warning:' \"$clean\" || true",
  'echo "::error title=build warnings ($RUNNER_OS)::warnings=$n finished-lines=$finished"; exit 1',
  'fi',
];

const E2E_ORDER = [
  'install rsign2',
  'throwaway signing key',
  'server',
  'build old',
  'marker present',
  'build new',
  'updater asset list',
  'sign updater artifacts',
  'delete signing key (after signing)',
  'latest.json',
  'rsign signature served',
  'case windows',
  'case user-placed',
  'case pkg-installed',
  'case appimage',
  'case deb',
  'annotate',
];
const PKGBUILD_BEGIN = '# FR-061 pkgbuild block begin (package-macos and the pkg-installed case hold it verbatim)';
const PKGBUILD_END = '# FR-061 pkgbuild block end';

const FIRST_INSTALL_LEGS = ['windows-latest', 'macos-latest', 'ubuntu-24.04', 'ubuntu-22.04-container'];

// FR-074: every assertion step prints a ::notice (pass) and an ::error (fail) naming the leg.
const ASSERTION_STEPS = {
  preflight: ['preflight'],
  build: [...BUILD_STEPS.filter((n) => n !== 'bundle'), ...BUILD_MACOS_STEPS, ...BUILD_LINUX_STEPS],
  sign: ['updater asset list', 'verify'],
  'sign-dry': ['updater asset list', 'verify'],
  'package-macos': ['pkgbuild', 'payload', 'pkg facts'],
  'updater-e2e': [
    'updater asset list',
    'marker present',
    'rsign signature served',
    'case windows',
    'case user-placed',
    'case pkg-installed',
    'case appimage',
    'case deb',
  ],
  'first-install': ['install', 'block live host', 'launch'],
  publish: ['compare downloaded assets'],
  'dry-publish': ['sigs not committed key', 'check latest.json'],
};

const NET_TOOL_RE = /(^|[\s;|&(`])(gh|curl|wget)(\s|$)/;

function pluginNames() {
  return fs
    .readdirSync(PLUGINS_DIR)
    .map((d) => path.join(PLUGINS_DIR, d, 'version.json'))
    .filter((f) => fs.existsSync(f))
    .map((f) => JSON.parse(fs.readFileSync(f, 'utf8')).name)
    .filter(Boolean);
}

function stepsOf(text, job) {
  const block = jobBlock(text, job);
  return block === null ? [] : stepBlocks(block);
}

function named(steps, name) {
  return steps.filter((s) => s.name === name);
}

// --- check(text): every finding on the workflow text.

function checkTop(wf, bad) {
  const on = nested(wf, 0, 'on');
  const triggers = (on || []).map((l) => /^ {2}([A-Za-z0-9_-]+):/.exec(l)).filter(Boolean).map((m) => m[1]);
  if (JSON.stringify(triggers) !== JSON.stringify(['workflow_dispatch'])) {
    bad.push(`on: triggers ${JSON.stringify(triggers)}, want only workflow_dispatch (FR-050)`);
  }
  const inputs = (on || []).map((l) => /^ {6}([A-Za-z0-9_-]+):/.exec(l)).filter(Boolean).map((m) => m[1]);
  if (JSON.stringify(inputs) !== JSON.stringify(['dry_run'])) bad.push(`on: inputs ${JSON.stringify(inputs)}, want dry_run`);
  const dry = trimmed(on || []);
  if (!dry.includes('type: boolean') || !dry.includes('default: true')) bad.push('on: dry_run is not a boolean defaulting to true');
  if (JSON.stringify(trimmed(nested(wf, 0, 'permissions'))) !== JSON.stringify(['contents: read'])) {
    bad.push('top permissions is not exactly contents: read (FR-050)');
  }
  const conc = trimmed(nested(wf, 0, 'concurrency'));
  if (JSON.stringify(conc) !== JSON.stringify(['group: krate-manager-release', 'cancel-in-progress: false'])) {
    bad.push(`concurrency ${JSON.stringify(conc)} (FR-050)`);
  }
  if (JSON.stringify(trimmed(nested(wf, 0, 'defaults'))) !== JSON.stringify(['run:', 'shell: bash'])) {
    bad.push('defaults.run.shell is not exactly bash');
  }
  const topEnv = nested(wf, 0, 'env');
  if (topEnv !== null && topEnv.join('\n').includes('secrets.')) bad.push('workflow-level env names a secret (FR-055)');
}

function checkJobs(wf, bad) {
  const names = jobNames(wf);
  if (JSON.stringify(names) !== JSON.stringify(JOBS)) bad.push(`jobs ${JSON.stringify(names)}, want ${JSON.stringify(JOBS)}`);
  for (const job of JOBS) {
    const block = jobBlock(wf, job);
    if (block === null) continue;
    const [needs, cond] = NEEDS_IF[job];
    if (field(block, 4, 'needs') !== needs) bad.push(`${job}: needs ${field(block, 4, 'needs')}, want ${needs}`);
    if (field(block, 4, 'if') !== cond) bad.push(`${job}: if ${field(block, 4, 'if')}, want ${cond}`);
    const env = field(block, 4, 'environment');
    if (SIGNING_JOBS.includes(job) ? env !== 'signing' : env !== null) {
      bad.push(`${job}: environment ${env} (environment: signing exactly on ${SIGNING_JOBS.join(', ')}, SI-20)`);
    }
    const perms = JSON.stringify(trimmed(nested(block, 4, 'permissions')));
    const want = JSON.stringify([WRITE_JOBS.includes(job) ? 'contents: write' : 'contents: read']);
    if (perms !== want) bad.push(`${job}: permissions ${perms}, want ${want} (PD-19)`);
    const jobEnv = nested(block, 4, 'env');
    if (jobEnv !== null && jobEnv.join('\n').includes('secrets.')) bad.push(`${job}: job-level env names a secret (FR-055)`);
    if (NO_SECRET_JOBS.includes(job) && block.includes('secrets.')) bad.push(`${job}: names secrets. (SI-20)`);
  }
  const main = stepsOf(wf, 'main-only');
  if (main.length !== 1) bad.push(`main-only: ${main.length} steps, want exactly one echo (PD-23)`);
  else {
    const lines = scriptLines(main[0]);
    if (lines.length !== 1 || !lines[0].startsWith('echo ') || usesOf(main[0]) !== null) {
      bad.push('main-only: its one step is not a single echo (PD-23)');
    }
  }
}

function checkSecrets(wf, bad) {
  const sign = named(stepsOf(wf, 'sign'), 'signing key');
  const envLines = sign.length === 1 ? stepEnv(sign[0]) : [];
  if (sign.length !== 1) bad.push(`sign: ${sign.length} "signing key" steps, want 1 (SI-20)`);
  if (JSON.stringify(envLines) !== JSON.stringify(SECRET_ENV)) bad.push('sign: the signing key step env is not the two secrets (SI-20)');
  const inEnv = envLines.reduce((n, l) => n + count(l, 'secrets.'), 0);
  if (count(wf, 'secrets.') !== inEnv || count(wf, 'secrets.TAURI_SIGNING_') !== 2) {
    bad.push(`secrets.: ${count(wf, 'secrets.')} references, ${inEnv} in the env of sign's signing key step (SI-20)`);
  }
}

function checkSigningJobs(wf, bad) {
  const sign = stepsOf(wf, 'sign');
  const dry = stepsOf(wf, 'sign-dry');
  for (const [job, steps, keyName] of [
    ['sign', sign, 'signing key'],
    ['sign-dry', dry, 'throwaway signing key'],
  ]) {
    const block = jobBlock(wf, job) || '';
    if (block.includes('actions/cache') || block.includes('rust-cache')) bad.push(`${job}: a cache action (PD-35)`);
    const key = named(steps, keyName);
    const install = named(steps, 'install rsign2');
    const npm = steps.filter((s) => scriptLines(s).some((l) => /\bnpm\b/.test(l)));
    if (key.length !== 1) bad.push(`${job}: ${key.length} "${keyName}" steps, want 1`);
    if (install.length !== 1) bad.push(`${job}: ${install.length} "install rsign2" steps, want 1 (PD-35)`);
    if (key.length === 1 && install.length === 1 && install[0].index > key[0].index) {
      bad.push(`${job}: install rsign2 follows the key step (PD-35)`);
    }
    if (install.length === 1 && JSON.stringify(scriptLines(install[0])) !== JSON.stringify(RSIGN_INSTALL)) {
      bad.push(`${job}: install rsign2 run is not exactly the uncached install and version check (PD-35)`);
    }
    for (const s of steps) {
      const lines = scriptLines(s);
      if (lines.some((l) => /\bnpm\b/.test(l) && l !== NPM_SIGN)) bad.push(`${job}: step ${s.label} runs npm (SI-20)`);
      if (lines.some((l) => /\bcargo\b/.test(l)) && s.name !== 'install rsign2') bad.push(`${job}: step ${s.label} runs cargo (SI-20)`);
      for (const word of [/bundle\.js/, /\btauri\b/, /\bnpx\b/]) {
        if (lines.some((l) => word.test(l))) bad.push(`${job}: step ${s.label} runs ${word.source} (SI-20)`);
      }
      if (key.length === 1 && s.index > key[0].index && (lines.some((l) => /\bnpm\b|\bcargo\b/.test(l)))) {
        bad.push(`${job}: npm or cargo after the key step (SI-20)`);
      }
    }
    if (npm.length !== 1) bad.push(`${job}: ${npm.length} npm steps, want the one ${NPM_SIGN}`);
    const signStep = named(steps, 'sign updater artifacts');
    const next = signStep.length === 1 ? steps[signStep[0].index + 1] : null;
    if (!next || next.name !== 'delete signing key (after signing)' || !scriptLines(next).includes(DELETE_LINE)) {
      bad.push(`${job}: the step after sign updater artifacts is not delete signing key (after signing) (SI-20)`);
    }
    const checkout = steps.find((s) => usesOf(s) === 'actions/checkout@v6');
    if (!checkout || !stepWith(checkout).includes('sparse-checkout: tools')) bad.push(`${job}: checkout is not sparse-checkout: tools`);
  }
  if (sign.length !== dry.length) bad.push(`sign and sign-dry: ${sign.length} and ${dry.length} steps (SI-20)`);
  else {
    for (let i = 0; i < sign.length; i++) {
      if (sign[i].name === 'signing key' && dry[i].name === 'throwaway signing key') continue;
      if (sign[i].text !== dry[i].text) bad.push(`sign and sign-dry differ at step ${i} (${sign[i].label} / ${dry[i].label}) (SI-20)`);
    }
  }
  const throwaway = named(dry, 'throwaway signing key');
  if (throwaway.length === 1) {
    if (JSON.stringify(scriptLines(throwaway[0])) !== JSON.stringify([THROWAWAY_LINE])) bad.push('sign-dry: throwaway signing key run');
  }
  const signRun = named(sign, 'sign updater artifacts');
  if (signRun.length === 1) {
    const lines = scriptLines(signRun[0]);
    if (!lines.includes(SIGN_LINE) || !lines.includes(SIG_LINE)) bad.push('sign: sign updater artifacts is not plan §4.10\'s rsign / base64 text');
    if (lines.some((l) => /\brsign sign\b/.test(l) && /\s-c\s/.test(l))) bad.push('sign: rsign sign with -c');
  }
  // PD-35: byte-equal copies in sign, sign-dry and updater-e2e.
  const e2e = stepsOf(wf, 'updater-e2e');
  for (const name of SHARED_SIGNING_STEPS) {
    const copies = [sign, dry, e2e].map((steps) => named(steps, name));
    if (copies.some((c) => c.length !== 1)) {
      bad.push(`${name}: not exactly once in each of sign, sign-dry, updater-e2e (PD-35)`);
      continue;
    }
    const runs = copies.map((c) => runText(c[0]));
    if (runs[1] !== runs[0] || runs[2] !== runs[0]) bad.push(`${name}: run differs across sign, sign-dry, updater-e2e (PD-35)`);
  }
  const t2 = [dry, e2e].map((steps) => named(steps, 'throwaway signing key'));
  if (t2.some((c) => c.length !== 1) || runText(t2[0][0]) !== runText(t2[1][0])) {
    bad.push('throwaway signing key: not byte-equal in sign-dry and updater-e2e (PD-35)');
  }
}

function checkPreflight(wf, bad) {
  const pre = named(stepsOf(wf, 'preflight'), 'preflight');
  const run = pre.length === 1 ? runText(pre[0]) : '';
  if (!run.includes('node tools/manager-release-preflight.js')) bad.push('preflight: no manager-release-preflight.js step (FR-051)');
  if (!run.includes('--repo "${{ github.repository }}"')) bad.push('preflight: no --repo "${{ github.repository }}" (SI-40)');
  for (const flag of ['--app-dir apps/krate-manager', '--dry-run ${{ inputs.dry_run }}', '--tags ', '--github-output "$GITHUB_OUTPUT"']) {
    if (!run.includes(flag)) bad.push(`preflight: no ${flag} (FR-051)`);
  }
  const tags = named(stepsOf(wf, 'preflight'), 'tags file');
  const tagsRun = tags.length === 1 ? runText(tags[0]) : '';
  if (!tagsRun.includes('git ls-remote --tags origin') || !tagsRun.includes('--limit 1000 --json tagName')) {
    bad.push('preflight: tags file does not list the remote tags and releases (FR-051)');
  }
}

function checkBuild(wf, bad) {
  const block = jobBlock(wf, 'build') || '';
  const steps = stepsOf(wf, 'build');
  const legs = matrixInclude(block);
  const want = [
    { leg: 'windows-latest', os: 'windows-latest' },
    { leg: 'macos-latest', os: 'macos-latest' },
    { leg: 'linux', os: 'ubuntu-24.04', container: 'ubuntu:22.04' },
  ];
  const got = legs.map((l) => ({ leg: l.leg, os: l.os, ...(l.container ? { container: l.container } : {}) }));
  if (JSON.stringify(got) !== JSON.stringify(want)) bad.push(`build: matrix ${JSON.stringify(got)} (FR-052)`);
  if (field(block, 4, 'container') !== '${{ matrix.container }}') bad.push('build: container is not ${{ matrix.container }}');
  if (field(block, 4, 'name') !== 'build (${{ matrix.leg }})') bad.push('build: name is not build (${{ matrix.leg }})');
  for (const name of [...BUILD_STEPS, ...BUILD_MACOS_STEPS, ...BUILD_LINUX_STEPS]) {
    if (named(steps, name).length !== 1) bad.push(`build: ${named(steps, name).length} "${name}" steps, want 1`);
  }
  for (const name of BUILD_MACOS_STEPS) {
    const s = named(steps, name)[0];
    if (s && field(s.text, 8, 'if') !== "matrix.leg == 'macos-latest'") bad.push(`build: ${name} is not macOS-only`);
  }
  // The Linux leg's fresh rustup (--default-toolchain none) needs a default for rustc outside apps/krate-manager.
  const rust = named(steps, 'Rust toolchain')[0];
  if (rust && !runText(rust).includes('rustup default "$(rustup show active-toolchain | cut -d\' \' -f1)"')) {
    bad.push('build: Rust toolchain does not make the pinned toolchain the default of a fresh rustup');
  }
  const glibc = named(steps, 'glibc floor')[0];
  if (glibc && (field(glibc.text, 8, 'if') !== "matrix.leg == 'linux'" || !runText(glibc).includes('::notice title=glibc floor::'))) {
    bad.push('build: glibc floor is not the Linux FR-065 notice');
  }
  const bundle = named(steps, 'bundle')[0];
  if (bundle) {
    const run = runText(bundle);
    for (const piece of [
      'PW=$(openssl rand -hex 16)',
      'echo "::add-mask::$PW"',
      'npx tauri signer generate --ci -p "$PW" -w "$RUNNER_TEMP/leg/k.key"',
      'TAURI_SIGNING_PRIVATE_KEY="$RUNNER_TEMP/leg/k.key" TAURI_SIGNING_PRIVATE_KEY_PASSWORD="$PW" node scripts/bundle.js --release',
      '| tee "$RUNNER_TEMP/tauri-build.txt"',
    ]) {
      if (!run.includes(piece)) bad.push(`build: bundle lacks ${piece} (PD-23)`);
    }
    for (const line of scriptLines(bundle)) {
      if (line.includes('$PW') && (line.includes('GITHUB_') || line.includes('>>') || line.includes('> '))) {
        bad.push(`build: bundle writes the password: ${line} (PD-23)`);
      }
    }
    if (field(bundle.text, 8, 'if') !== null) bad.push('build: bundle has an if (one step on both kinds of run, PD-23)');
    if (!stepEnv(bundle).includes("NO_STRIP: 'true'")) bad.push("build: bundle env lacks NO_STRIP: 'true' (PD-34)");
  }
  if (/KRATE_BUNDLE_EXTRA_CONFIG(=|:\s)/.test(block)) bad.push('build: sets KRATE_BUNDLE_EXTRA_CONFIG (SI-20)');
  const extra = named(steps, 'extra config empty')[0];
  if (extra && !runText(extra).includes('KRATE_BUNDLE_EXTRA_CONFIG')) bad.push('build: extra config empty does not read KRATE_BUNDLE_EXTRA_CONFIG');
  const warnings = named(steps, 'build warnings')[0];
  if (warnings) {
    const run = runText(warnings);
    if (!run.includes("grep -c '^warning:'")) bad.push("build: build warnings lacks grep -c '^warning:' (FR-053)");
    if (/grep\s+(-[A-Za-z]*v|--invert-match)/.test(run)) bad.push('build: build warnings excludes lines (grep -v) (FR-053)');
    if (JSON.stringify(scriptLines(warnings)) !== JSON.stringify(BUILD_WARNINGS)) bad.push('build: build warnings is not manager-ci\'s rule (FR-053)');
  }
  const sigs = named(steps, 'signatures')[0];
  if (sigs && (!runText(sigs).includes('--check-only') || !runText(sigs).includes('--platform-subset'))) {
    bad.push('build: signatures is not --check-only --platform-subset (FR-053)');
  }
  const ctr = named(steps, 'ci-temp-roots absent')[0];
  if (ctr && !runText(ctr).includes('src-tauri/binaries/krate-manager-helper-')) bad.push('build: ci-temp-roots absent lacks the SI-10 Linux form');
  const legKey = named(steps, 'leg key is not the release key')[0];
  if (legKey && !runText(legKey).includes('does not match the public key from')) bad.push('build: leg key step does not read the CLI warning (PD-23)');
  const uploads = steps.filter((s) => usesOf(s) === 'actions/upload-artifact@v7' && stepWith(s).some((l) => l.includes('release-assets')));
  if (uploads.length === 0) bad.push('build: no release-assets upload');
  for (const u of uploads) {
    if (!stepWith(u).includes('!apps/krate-manager/target/release-assets/*.sig')) bad.push('build: a release-assets upload does not exclude *.sig (PD-23)');
  }
}

function checkConfinement(wf, bad) {
  const all = JOBS.flatMap((j) => stepsOf(wf, j));
  for (const token of ['ci-temp-roots', '--ci-roots']) {
    const inSteps = all.filter((s) => s.name === 'ci-temp-roots absent').reduce((n, s) => n + count(s.text, token), 0);
    if (count(wf, token) !== inSteps) bad.push(`${token} outside the ci-temp-roots absent steps (FR-056)`);
  }
  if (/--features[ =]['"]?[^\n]*ci-temp-roots/.test(wf)) bad.push('a --features names ci-temp-roots (FR-056)');
  const e2e = jobBlock(wf, 'updater-e2e') || '';
  for (const token of ['--features updater-e2e', '--repo-url-base', '--only', 'dangerousInsecureTransportProtocol']) {
    if (count(wf, token) !== count(e2e, token)) bad.push(`${token} outside updater-e2e (FR-091)`);
  }
  const fi = jobBlock(wf, 'first-install') || '';
  const both = e2e + '\n' + fi;
  for (const [label, re] of [
    ['sudo', /\bsudo\b/g],
    ['installer -pkg', /installer -pkg/g],
    ['apt-get install ./', /apt-get install[^\n]*\s\.\//g],
  ]) {
    if ((wf.match(re) || []).length !== (both.match(re) || []).length) bad.push(`${label} outside updater-e2e / first-install (FR-091)`);
  }
  for (const token of ['tauri-apps/tauri-action', '/releases/latest/', 'api.github.com', 'dev-feed', 'APPIMAGE_EXTRACT_AND_RUN', '--clobber']) {
    if (wf.includes(token)) bad.push(`release-manager.yml names ${token} (FR-091)`);
  }
  for (const job of ['updater-e2e', 'first-install']) {
    for (const s of stepsOf(wf, job)) {
      const run = runText(s);
      if (run.split('\n').some((l) => NET_TOOL_RE.test(l))) bad.push(`${job}: step ${s.label} invokes gh, curl or wget (FR-077)`);
      if (run.includes('github.com')) bad.push(`${job}: step ${s.label} names github.com (FR-077)`);
    }
  }
  const lines = wf.split('\n');
  const create = lines.filter((l) => l.includes('gh release create'));
  const edit = lines.filter((l) => l.includes('gh release edit'));
  if (create.length !== 1 || !create[0].includes('--latest=false') || !create[0].includes('--draft')) bad.push('gh release create: not one --draft --latest=false (FR-057)');
  if (edit.length !== 1 || !edit[0].includes('--latest=false') || !edit[0].includes('--draft=false')) bad.push('gh release edit: not one --draft=false --latest=false (FR-057)');
  for (const [job, dry] of [
    ['feed', 'false'],
    ['feed-dry', 'true'],
  ]) {
    const block = jobBlock(wf, job) || '';
    if (field(block, 4, 'uses') !== './.github/workflows/feed.yml') bad.push(`${job}: does not use ./.github/workflows/feed.yml (FR-058)`);
    if (JSON.stringify(trimmed(nested(block, 4, 'with'))) !== JSON.stringify([`dry_run: ${dry}`])) bad.push(`${job}: with is not dry_run: ${dry} (PD-19)`);
  }
  const neg = named(stepsOf(wf, 'dry-publish'), 'sigs not committed key');
  if (neg.length !== 1 || !runText(neg[0]).includes('minisign: E_MINISIG_KEY_ID') || !runText(neg[0]).includes('tools/feed-contract/keys/krate-signing.pub')) {
    bad.push('dry-publish: no sigs not committed key step expecting minisign: E_MINISIG_KEY_ID (SC-019)');
  }
  for (const name of pluginNames()) if (wf.includes(name)) bad.push(`release-manager.yml names the plugin ${name}`);
}

function pkgbuildBlock(step) {
  const lines = scriptLines(step);
  const a = lines.indexOf(PKGBUILD_BEGIN);
  const b = lines.indexOf(PKGBUILD_END);
  return a < 0 || b < a ? null : lines.slice(a, b + 1);
}

function checkPackageMacos(wf, bad) {
  const steps = stepsOf(wf, 'package-macos');
  const facts = named(steps, 'pkg facts')[0];
  const run = facts ? runText(facts) : '';
  if (!run.includes("xmllint --xpath 'count(//relocate/*)'") || !/"\$relocated" (!=|=) "0"/.test(run)) {
    bad.push("package-macos: pkg facts does not require xmllint --xpath 'count(//relocate/*)' to print 0 (SI-37)");
  }
  if (!steps.some((s) => usesOf(s) === 'actions/upload-artifact@v7' && stepWith(s).includes('name: packageinfo'))) {
    bad.push('package-macos: no packageinfo upload (SI-37)');
  }
  const own = named(steps, 'pkgbuild')[0];
  const e2e = named(stepsOf(wf, 'updater-e2e'), 'case pkg-installed')[0];
  const a = own ? pkgbuildBlock(own) : null;
  const b = e2e ? pkgbuildBlock(e2e) : null;
  if (a === null || b === null) bad.push('pkgbuild block missing from package-macos or the pkg-installed case (FR-061)');
  else {
    if (JSON.stringify(a) !== JSON.stringify(b)) bad.push('the pkg-installed case\'s pkgbuild block differs from package-macos\'s (FR-061)');
    const text = a.join('\n');
    for (const piece of ['--component-plist', 'plutil -replace 0.BundleIsRelocatable -bool NO', '--identifier com.krateaudio.krate-manager', '--install-location /']) {
      if (!text.includes(piece)) bad.push(`pkgbuild block lacks ${piece} (FR-061)`);
    }
    for (const piece of ['--scripts', '--ownership']) if (text.includes(piece)) bad.push(`pkgbuild block has ${piece} (FR-061)`);
  }
}

function checkE2e(wf, bad) {
  const block = jobBlock(wf, 'updater-e2e') || '';
  const steps = stepsOf(wf, 'updater-e2e');
  if (JSON.stringify(matrixOs(block)) !== JSON.stringify(['windows-latest', 'macos-latest', 'ubuntu-24.04'])) {
    bad.push(`updater-e2e: matrix os ${JSON.stringify(matrixOs(block))} (FR-071)`);
  }
  if (field(block, 4, 'name') !== 'updater-e2e (${{ matrix.os }})') bad.push('updater-e2e: name (FR-071)');
  if (field(block, 4, 'container') !== null) bad.push('updater-e2e: has a container (PD-34)');
  let last = -1;
  for (const name of E2E_ORDER) {
    const s = named(steps, name);
    if (s.length !== 1) {
      bad.push(`updater-e2e: ${s.length} "${name}" steps, want 1`);
      continue;
    }
    if (s[0].index <= last) bad.push(`updater-e2e: ${name} is out of order (plan §4.10)`);
    last = s[0].index;
  }
  const keys = [];
  for (const name of ['build old', 'build new']) {
    const s = named(steps, name)[0];
    if (!s) continue;
    const run = runText(s);
    const m = /npx tauri signer generate --ci -p "\$PW" -w "(\$RUNNER_TEMP\/[^"]+)"/.exec(run);
    if (!m || !run.includes(`TAURI_SIGNING_PRIVATE_KEY="${m[1]}"`)) bad.push(`updater-e2e: ${name} lacks its per-build CLI key (PD-35)`);
    else keys.push(m[1]);
    if (!run.includes('--features updater-e2e')) bad.push(`updater-e2e: ${name} lacks --features updater-e2e`);
    if (!stepEnv(s).includes("NO_STRIP: 'true'")) bad.push(`updater-e2e: ${name} env lacks NO_STRIP: 'true' (PD-34)`);
  }
  if (keys.length === 2 && (keys[0] === keys[1] || keys.includes('$RUNNER_TEMP/krate.key'))) {
    bad.push('updater-e2e: the per-build CLI keys are not two different paths beside the rsign key (PD-35)');
  }
  const old = named(steps, 'build old')[0];
  if (old && !runText(old).includes('pub=$(base64 -w0 < "$RUNNER_TEMP/krate.pub")')) bad.push('updater-e2e: build old does not trust the rsign key (PD-35)');
  const latest = named(steps, 'latest.json')[0];
  if (latest) {
    const run = runText(latest);
    for (const piece of ['--pubkey "$RUNNER_TEMP/krate.pub"', '--repo ${{ github.repository }}', '--repo-url-base', '--only']) {
      if (!run.includes(piece)) bad.push(`updater-e2e: latest.json lacks ${piece}`);
    }
  }
  const served = named(steps, 'rsign signature served')[0];
  if (served && !/base64 -w0 < "\$RUNNER_TEMP\/\$name\.minisig"/.test(runText(served))) bad.push('updater-e2e: rsign signature served does not compare with the .minisig (PD-35)');
  const win = named(steps, 'case windows')[0];
  if (win) {
    const run = runText(win);
    for (const piece of ["-ArgumentList '/S' -Wait -PassThru", '$env:LOCALAPPDATA\\Krate Manager\\krate-manager.exe', 'ProductVersion']) {
      if (!run.includes(piece)) bad.push(`updater-e2e: case windows lacks ${piece} (FR-072, SI-14)`);
    }
  }
  const pkg = named(steps, 'case pkg-installed')[0];
  if (pkg) {
    const run = runText(pkg);
    // SIGKILL: SecurityAgent defers SIGTERM while it holds XPC transactions, so the prompt stayed up (run 41625257363).
    for (const piece of ['pgrep -x SecurityAgent', 'sudo pkill -KILL -x SecurityAgent', 'error-E_SELF_UPDATE_INSTALL', 'sudo installer -pkg']) {
      if (!run.includes(piece)) bad.push(`updater-e2e: case pkg-installed lacks ${piece} (PD-13)`);
    }
  }
  const deb = named(steps, 'case deb')[0];
  if (deb && !runText(deb).includes('error-E_SELF_UPDATE_UNSUPPORTED')) bad.push('updater-e2e: case deb does not expect E_SELF_UPDATE_UNSUPPORTED (PD-24)');
  if (!block.includes('libfuse2t64')) bad.push('updater-e2e: no libfuse2t64 (FR-072)');
}

function checkFirstInstall(wf, bad) {
  const block = jobBlock(wf, 'first-install') || '';
  const steps = stepsOf(wf, 'first-install');
  const legs = matrixInclude(block);
  if (JSON.stringify(legs.map((l) => l.leg)) !== JSON.stringify(FIRST_INSTALL_LEGS)) bad.push(`first-install: legs ${JSON.stringify(legs.map((l) => l.leg))} (FR-075)`);
  const ctr = legs.find((l) => l.leg === 'ubuntu-22.04-container');
  if (!ctr || ctr.os !== 'ubuntu-24.04' || ctr.container !== 'ubuntu:22.04') bad.push('first-install: the container leg is not ubuntu-24.04 + ubuntu:22.04');
  if (field(block, 4, 'container') !== '${{ matrix.container }}') bad.push('first-install: container is not ${{ matrix.container }}');
  for (const l of legs) {
    const leg = { ...l, runnerOs: RUNNER_OS[l.os] };
    const mine = [];
    for (const s of steps) {
      const h = holdsFor(field(s.text, 8, 'if'), leg);
      if (h === null) bad.push(`first-install: step ${s.label} has an if outside the per-leg grammar`);
      else if (h) mine.push(s);
    }
    const launch = mine.filter((s) => s.name === 'launch');
    const blockHost = mine.filter((s) => s.name === 'block live host');
    if (launch.length !== 1 || blockHost.length !== 1) {
      bad.push(`first-install (${l.leg}): ${blockHost.length} block live host and ${launch.length} launch steps, want 1 each (FR-078)`);
    } else if (mine.indexOf(blockHost[0]) !== mine.indexOf(launch[0]) - 1) {
      bad.push(`first-install (${l.leg}): block live host is not directly before launch (FR-078)`);
    }
    for (const s of launch) {
      const run = runText(s);
      if (/latest\.json|versions\.json/.test(run)) bad.push(`first-install (${l.leg}): launch reads feed content (FR-077)`);
      if (!/pgrep|\/proc\/|Get-Process/.test(run)) bad.push(`first-install (${l.leg}): launch checks no process (FR-077)`);
    }
  }
  const hostSteps = steps.filter((s) => s.name === 'block live host');
  for (const job of JOBS.filter((j) => j !== 'first-install')) {
    if (named(stepsOf(wf, job), 'block live host').length > 0) bad.push(`${job}: a block live host step (FR-078)`);
  }
  for (const token of ['/etc/hosts', 'drivers\\etc\\hosts']) {
    const inHost = hostSteps.reduce((n, s) => n + count(s.text, token), 0);
    if (count(wf, token) !== inHost) bad.push(`the hosts file (${token}) is edited outside block live host (FR-078)`);
  }
}

function checkAssertions(wf, bad) {
  for (const [job, names] of Object.entries(ASSERTION_STEPS)) {
    const steps = stepsOf(wf, job);
    for (const name of names) {
      const mine = named(steps, name);
      if (mine.length === 0) bad.push(`${job}: no assertion step "${name}"`);
      for (const s of mine) {
        const run = runText(s);
        if (!run.includes('::notice') || !run.includes('::error')) bad.push(`${job}: ${name} prints no ::notice or no ::error (FR-074)`);
      }
    }
  }
}

/** Every finding on the workflow text; [] when the Phase 11 shape holds. */
function check(text) {
  const bad = [];
  const wf = text.replace(/\r\n/g, '\n');
  checkTop(wf, bad);
  checkJobs(wf, bad);
  checkSecrets(wf, bad);
  checkSigningJobs(wf, bad);
  checkPreflight(wf, bad);
  checkBuild(wf, bad);
  checkConfinement(wf, bad);
  checkPackageMacos(wf, bad);
  checkE2e(wf, bad);
  checkFirstInstall(wf, bad);
  checkAssertions(wf, bad);
  return bad;
}

// --- Mutations: string edits on in-memory copies.

/** `text` with the one occurrence of `from` replaced; throws when `from` does not occur exactly once. */
function replaceOnce(text, from, to) {
  const n = count(text, from);
  if (n !== 1) throw new Error(`mutation anchor occurs ${n} time(s): ${JSON.stringify(from)}`);
  return text.replace(from, () => to);
}

/** The [start, end) line range of the step named `name` in job `job`. */
function stepRange(text, job, name) {
  const lines = text.split('\n');
  const block = jobBlock(text, job);
  if (block === null) throw new Error(`mutation: no job ${job}`);
  const jobStart = lines.indexOf(`  ${job}:`);
  const jobEnd = jobStart + block.split('\n').length;
  const head = lines.findIndex((l, i) => {
    if (i < jobStart || i >= jobEnd) return false;
    const m = /^ {6}- name:\s*(.*)$/.exec(l);
    return m !== null && unquote(stripComment(m[1])) === name;
  });
  if (head < 0) throw new Error(`mutation: no step ${name} in ${job}`);
  let end = head + 1;
  while (end < jobEnd && !/^ {6}- /.test(lines[end]) && !(lines[end].trim() !== '' && indentOf(lines[end]) < 6)) end++;
  return { lines, head, end };
}

/** `text` without the step `name` of job `job`. */
function removeStep(text, job, name) {
  const { lines, head, end } = stepRange(text, job, name);
  return [...lines.slice(0, head), ...lines.slice(end)].join('\n');
}

/** `text` with the step `name` of job `job` moved to directly after step `after` of the same job. */
function moveStepAfter(text, job, name, after) {
  const { lines, head, end } = stepRange(text, job, name);
  const moved = lines.slice(head, end);
  const rest = [...lines.slice(0, head), ...lines.slice(end)].join('\n');
  const target = stepRange(rest, job, after);
  return [...target.lines.slice(0, target.end), ...moved, ...target.lines.slice(target.end)].join('\n');
}

/** `text` with `edit` applied to the text of step `name` of job `job` only. */
function editStep(text, job, name, edit) {
  const { lines, head, end } = stepRange(text, job, name);
  const step = lines.slice(head, end).join('\n');
  const changed = edit(step);
  if (changed === step) throw new Error(`mutation: edit changed nothing in ${job} / ${name}`);
  return [...lines.slice(0, head), changed, ...lines.slice(end)].join('\n');
}

/** `text` with `stepText` inserted directly after step `after` of job `job`. */
function insertAfter(text, job, after, stepText) {
  const { lines, end } = stepRange(text, job, after);
  return [...lines.slice(0, end), ...stepText.split('\n'), ...lines.slice(end)].join('\n');
}

const CACHE_STEP = [
  '      - name: Cache rsign2',
  '        uses: actions/cache@v5',
  '        with:',
  '          path: ~/.cargo/bin/rsign',
  '          key: rsign2-0.6.7-${{ runner.os }}-${{ runner.arch }}',
  '',
].join('\n');

const MUTATIONS = [
  ['a push trigger', (w) => replaceOnce(w, 'on:\n  workflow_dispatch:\n', 'on:\n  push:\n  workflow_dispatch:\n'), /on: triggers/],
  ['dry_run default false', (w) => replaceOnce(w, '        default: true\n', '        default: false\n'), /dry_run is not a boolean defaulting to true/],
  ['top permissions write', (w) => replaceOnce(w, 'permissions:\n  contents: read\n', 'permissions:\n  contents: write\n'), /top permissions/],
  ['cancel-in-progress true', (w) => replaceOnce(w, 'cancel-in-progress: false', 'cancel-in-progress: true'), /concurrency/],
  [
    'environment: signing on build',
    (w) => replaceOnce(w, '    name: build (${{ matrix.leg }})\n', '    name: build (${{ matrix.leg }})\n    environment: signing\n'),
    /build: environment signing/,
  ],
  [
    'a second step in main-only',
    (w) => insertAfter(w, 'main-only', 'main-only', '      - uses: actions/checkout@v6'),
    /main-only: 2 steps/,
  ],
  [
    'the if dropped from sign',
    (w) => replaceOnce(w, "  sign:\n    needs: [preflight, build]\n    if: ${{ !inputs.dry_run }}\n", '  sign:\n    needs: [preflight, build]\n'),
    /sign: if null/,
  ],
  [
    'a secret added to a build step',
    (w) => editStep(w, 'build', 'bundle', (s) => s.replace("          NO_STRIP: 'true'\n", "          NO_STRIP: 'true'\n          TOKEN: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY }}\n")),
    /build: names secrets\./,
  ],
  [
    'a secret added to sign-dry',
    (w) => editStep(w, 'sign-dry', 'throwaway signing key', (s) => s.replace('        run:', '        env:\n          K: ${{ secrets.TAURI_SIGNING_PRIVATE_KEY_PASSWORD }}\n        run:')),
    /sign-dry: names secrets\./,
  ],
  [
    'an npm ci after the key step',
    (w) => editStep(w, 'sign', 'delete signing key (after signing)', (s) => `${s}\n      - name: late npm\n        run: npm ci`),
    /sign: step late npm runs npm/,
  ],
  ['the delete step removed from sign', (w) => removeStep(w, 'sign', 'delete signing key (after signing)'), /sign: the step after sign updater artifacts is not delete/],
  ['an rsign2 cache step added to sign', (w) => insertAfter(w, 'sign', 'npm ci (feed-contract)', CACHE_STEP), /sign: a cache action/],
  ['install rsign2 moved after the key step', (w) => moveStepAfter(w, 'sign', 'install rsign2', 'signing key'), /sign: install rsign2 follows the key step/],
  [
    "one character of updater-e2e's sign updater artifacts changed",
    (w) => editStep(w, 'updater-e2e', 'sign updater artifacts', (s) => s.replace('base64 -w0 <', 'base64 -w 0 <')),
    /sign updater artifacts: run differs/,
  ],
  [
    "updater-e2e's throwaway signing key changed",
    (w) => editStep(w, 'updater-e2e', 'throwaway signing key', (s) => s.replace('rsign generate -W', 'rsign generate -W -f')),
    /throwaway signing key: not byte-equal/,
  ],
  [
    'the preflight --repo dropped',
    (w) => editStep(w, 'preflight', 'preflight', (s) => s.replace(' --repo "${{ github.repository }}"', '')),
    /preflight: no --repo/,
  ],
  [
    'a grep -v in build warnings',
    (w) => editStep(w, 'build', 'build warnings', (s) => s.replace(`n=$(grep -c '^warning:' "$clean" || true)`, `n=$(grep -v 'Warn' "$clean" | grep -c '^warning:' || true)`)),
    /build warnings excludes lines/,
  ],
  [
    "NO_STRIP dropped from updater-e2e's build new",
    (w) => editStep(w, 'updater-e2e', 'build new', (s) => s.replace("          NO_STRIP: 'true'\n", '')),
    /build new env lacks NO_STRIP/,
  ],
  [
    'the xmllint relocate count replaced by a grep',
    (w) => editStep(w, 'package-macos', 'pkg facts', (s) => s.replace(`relocated=$(xmllint --xpath 'count(//relocate/*)' "$x/PackageInfo")`, `relocated=$(grep -c '<bundle ' "$x/PackageInfo" || true)`)),
    /count\(\/\/relocate\/\*\)/,
  ],
  // removeStep takes the first step of that name: the macOS leg's.
  ['block live host removed from the macOS leg', (w) => removeStep(w, 'first-install', 'block live host'), /first-install \(macos-latest\): 0 block live host/],
  ['latest.json moved before sign updater artifacts', (w) => moveStepAfter(w, 'updater-e2e', 'latest.json', 'updater asset list'), /latest\.json is out of order|sign updater artifacts is out of order/],
  [
    '--features ci-temp-roots on the build bundle line',
    (w) => editStep(w, 'build', 'bundle', (s) => s.replace('node scripts/bundle.js --release ', 'node scripts/bundle.js --release --features ci-temp-roots ')),
    /a --features names ci-temp-roots/,
  ],
  [
    'APPIMAGE_EXTRACT_AND_RUN in updater-e2e',
    (w) => editStep(w, 'updater-e2e', 'case appimage', (s) => s.replace('set -euo pipefail', 'set -euo pipefail\n          export APPIMAGE_EXTRACT_AND_RUN=1')),
    /APPIMAGE_EXTRACT_AND_RUN/,
  ],
  [
    'a curl in first-install',
    (w) => editStep(w, 'first-install', 'launch', (s) => s.replace('set -euo pipefail', 'set -euo pipefail\n          curl -fsS https://example.invalid/x || true')),
    /first-install: step launch invokes gh, curl or wget/,
  ],
  ['--clobber on the upload', (w) => replaceOnce(w, 'gh release upload "$TAG" --repo "$GITHUB_REPOSITORY" assets/*', 'gh release upload "$TAG" --repo "$GITHUB_REPOSITORY" --clobber assets/*'), /--clobber/],
  ['--latest=false dropped from the edit', (w) => replaceOnce(w, '--draft=false --latest=false', '--draft=false'), /gh release edit/],
  ['tauri-action used', (w) => replaceOnce(w, '      - name: release create\n', '      - uses: tauri-apps/tauri-action@v1\n      - name: release create\n'), /tauri-apps\/tauri-action/],
  [
    'a container on updater-e2e',
    (w) => replaceOnce(w, '    name: updater-e2e (${{ matrix.os }})\n', '    name: updater-e2e (${{ matrix.os }})\n    container: ubuntu:22.04\n'),
    /updater-e2e: has a container/,
  ],
  [
    "--scripts in the pkg-installed case's pkgbuild",
    (w) => editStep(w, 'updater-e2e', 'case pkg-installed', (s) => s.replace('--install-location / "$pkg"', '--install-location / --scripts "$RUNNER_TEMP/s" "$pkg"')),
    /pkgbuild block differs/,
  ],
  ['sudo in package-macos', (w) => editStep(w, 'package-macos', 'stage', (s) => s.replace('ditto -x -k', 'sudo ditto -x -k')), /sudo outside updater-e2e/],
  ['feed-dry with dry_run false', (w) => replaceOnce(w, '    with:\n      dry_run: true\n', '    with:\n      dry_run: false\n'), /feed-dry: with is not dry_run: true/],
  ['sigs not committed key removed', (w) => removeStep(w, 'dry-publish', 'sigs not committed key'), /sigs not committed key/],
  ['api.github.com named', (w) => replaceOnce(w, '# Spec:', '# api.github.com\n# Spec:'), /api\.github\.com/],
  ['dev-feed named', (w) => editStep(w, 'updater-e2e', 'build new', (s) => s.replace('--features updater-e2e', '--features updater-e2e,dev-feed')), /dev-feed/],
  ['a plugin named', (w) => replaceOnce(w, '# Spec:', `# ${pluginNames()[0]}\n# Spec:`), /names the plugin/],
];

test('release-manager.yml keeps the Phase 11 shape', async (t) => {
  assert.ok(fs.existsSync(WORKFLOW), `${path.relative(REPO_ROOT, WORKFLOW)} is missing`);
  const text = readText(WORKFLOW);

  await t.test('the real file gives no finding', () => {
    assert.deepEqual(check(text), []);
  });

  await t.test('a CRLF copy gives no finding', () => {
    assert.deepEqual(check(text.replace(/\n/g, '\r\n')), []);
  });

  await t.test("build warnings is manager-ci.yml's rule, unchanged (FR-053)", () => {
    const steps = stepBlocks(jobBlock(readText(MANAGER_CI), 'check'));
    const own = steps.find((s) => s.name === 'build warnings');
    assert.ok(own, 'manager-ci.yml has no build warnings step');
    assert.deepEqual(scriptLines(own), BUILD_WARNINGS);
  });

  await t.test('the plugin roster is read', () => {
    assert.ok(pluginNames().length >= 9, `only ${pluginNames().length} plugin names read`);
  });

  // Phase 12 PD-25, FR-085, SC-029: checkout (full history, for the plugin tags), Node 24, render, then deploy or upload.
  const landingJob = (job, last) => {
    const block = jobBlock(text, job);
    assert.ok(block, `no job ${job}`);
    assert.equal(field(block, 4, 'runs-on'), 'ubuntu-24.04', `${job}: runs-on`);
    const steps = stepBlocks(block);
    assert.deepEqual(steps.map((s) => usesOf(s) || s.name), ['actions/checkout@v6', 'actions/setup-node@v4', 'render', last], `${job}: steps`);
    assert.deepEqual(stepWith(steps[0]), ['fetch-depth: 0'], `${job}: checkout`);
    assert.deepEqual(stepWith(steps[1]), ["node-version: '24'"], `${job}: setup-node`);
    assert.deepEqual(scriptLines(steps[2]), [LANDING_RENDER], `${job}: render run`);
    assert.deepEqual(stepEnv(steps[2]), LANDING_ENV, `${job}: render env`);
    return { block, steps };
  };

  await t.test('landing: needs [preflight, feed], real runs only, contents: write, render then gh-pages with keep_files (PD-25, FR-085)', () => {
    const { block, steps } = landingJob('landing', 'peaceiris/actions-gh-pages@v4');
    assert.equal(field(block, 4, 'needs'), '[preflight, feed]');
    assert.equal(field(block, 4, 'if'), REAL);
    assert.deepEqual(trimmed(nested(block, 4, 'permissions')), ['contents: write']);
    assert.deepEqual(stepWith(steps[3]), ['github_token: ${{ github.token }}', 'publish_dir: ./_site', 'keep_files: true']);
    assert.equal(count(text, 'peaceiris/actions-gh-pages@'), 1, 'a second gh-pages deploy in release-manager.yml');
  });

  await t.test('landing-dry: needs [preflight, feed-dry], dry runs only, contents: read, the same render, uploaded, never deployed (PD-25, FR-085)', () => {
    const { block, steps } = landingJob('landing-dry', 'actions/upload-artifact@v7');
    assert.equal(field(block, 4, 'needs'), '[preflight, feed-dry]');
    assert.equal(field(block, 4, 'if'), DRY);
    assert.deepEqual(trimmed(nested(block, 4, 'permissions')), ['contents: read']);
    assert.deepEqual(stepWith(steps[3]), ['name: manager-landing-dry-run', 'path: _site/index.html', 'if-no-files-found: error']);
    assert.equal(block.includes('peaceiris/'), false, 'landing-dry deploys');
    assert.equal(steps[2].text, stepBlocks(jobBlock(text, 'landing'))[2].text, 'the two render steps differ');
  });

  await t.test('no_advisory_scan_tool: no workflow and no tools/*.js runs cargo audit or cargo deny (SC-032, FR-105)', () => {
    const workflows = path.join(REPO_ROOT, '.github', 'workflows');
    const tools = path.join(REPO_ROOT, 'tools');
    const files = [
      ...fs.readdirSync(workflows).filter((n) => /\.ya?ml$/.test(n)).map((n) => path.join(workflows, n)),
      ...fs.readdirSync(tools).filter((n) => n.endsWith('.js')).map((n) => path.join(tools, n)),
    ];
    assert.ok(files.length >= 20, `only ${files.length} files scanned`);
    const hits = files
      .filter((f) => ADVISORY_SCAN_RE.test(readText(f)))
      .map((f) => path.relative(REPO_ROOT, f).split(path.sep).join('/'));
    assert.deepEqual(hits, []);
    assert.ok(ADVISORY_SCAN_RE.test('run: cargo audit --deny warnings'), 'the pattern misses cargo audit');
    assert.ok(ADVISORY_SCAN_RE.test('cargo  deny check advisories'), 'the pattern misses cargo deny');
  });

  for (const [name, mutate, expected] of MUTATIONS) {
    await t.test(`mutation: ${name}`, () => {
      const mutated = mutate(text);
      assert.notEqual(mutated, text, 'the mutation changed nothing');
      const errs = check(mutated);
      assert.ok(errs.some((e) => expected.test(e)), `no finding matches ${expected}: ${JSON.stringify(errs)}`);
    });
  }
});
