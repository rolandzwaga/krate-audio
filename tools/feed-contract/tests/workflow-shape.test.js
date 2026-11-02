'use strict';
require('./helpers/write-guard.js');

// SC-014: the release workflows keep the Phase 2 shape (spec specs/krate-manager-phase2/spec.md SC-014, FR-040 to
// FR-050, FR-047a; plan §4.8, §7.1, PD-7, PD-8, PD-11, PD-12, PD-13, PD-20 to PD-22, PD-26).
// The workflows are read as text with an indentation-based block extractor (PD-8): 2-space job keys under `jobs:`,
// 6-space `- ` step items, 8-space step keys. No YAML library. Files are only read.
// FR-081's protected ranges are not checked here (the write guard admits no `git show`); T020's scope check does that.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const WORKFLOWS = path.join(REPO_ROOT, '.github', 'workflows');

function readWorkflow(name) {
  return fs.readFileSync(path.join(WORKFLOWS, name), 'utf8').replace(/\r\n/g, '\n');
}

function indentOf(line) {
  return line.length - line.trimStart().length;
}

// A trailing ` # comment` after a plain scalar (the `- name: X   # PD-21: ...` lines).
function stripComment(value) {
  return value.replace(/\s+#.*$/, '').trim();
}

/** Names of the 2-space keys under the top-level `jobs:` mapping, in file order. */
function jobNames(text) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => /^jobs:\s*$/.test(l));
  assert.ok(start >= 0, 'no top-level jobs: key');
  const names = [];
  for (const line of lines.slice(start + 1)) {
    if (/^\S/.test(line)) break;
    const m = /^ {2}([A-Za-z0-9_-]+):\s*$/.exec(line);
    if (m) names.push(m[1]);
  }
  return names;
}

/** The text of job `job` under `jobs:`: its key line up to the next line indented 0-2 spaces. */
function jobBlock(text, job) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => /^jobs:\s*$/.test(l));
  assert.ok(start >= 0, 'no top-level jobs: key');
  const head = lines.findIndex((l, i) => i > start && l === `  ${job}:`);
  assert.ok(head > start, `no job ${job}`);
  let end = head + 1;
  while (end < lines.length && !(lines[end].trim() !== '' && indentOf(lines[end]) <= 2)) end++;
  return lines.slice(head, end).join('\n');
}

/** The job's steps: each 6-space `- ` item up to the next one (or the job's end), as {name, text, index}. */
function stepBlocks(jobText) {
  const lines = jobText.split('\n');
  const steps = [];
  let current = null;
  for (const line of lines) {
    if (/^ {6}- /.test(line)) {
      const m = /^ {6}- name:\s*(.*)$/.exec(line);
      current = { name: m ? stripComment(m[1]) : '', lines: [line] };
      steps.push(current);
    } else if (current && line.trim() !== '' && indentOf(line) < 6) {
      current = null;
    } else if (current) {
      current.lines.push(line);
    }
  }
  return steps.map((s, index) => ({ name: s.name, text: s.lines.join('\n'), index }));
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

/** A step's `run:` command text (block or single line), or ''. */
function runText(step) {
  const block = nested(step.text, 8, 'run');
  if (block) return block.join('\n');
  const single = /^ {8}run:\s*(.*)$/m.exec(step.text);
  return single ? single[1] : '';
}

function stepByName(steps, name, where) {
  const hits = steps.filter((s) => s.name === name);
  assert.equal(hits.length, 1, `${where}: step "${name}" appears ${hits.length} time(s)`);
  return hits[0];
}

function count(text, re) {
  return (text.match(re) || []).length;
}

const ON_DRY = /^(\$\{\{\s*)?inputs\.dry_run(\s*\}\})?$/;
const NOT_DRY = /^(\$\{\{\s*)?!\s*inputs\.dry_run(\s*\}\})?$/;
const ASSET_PREFIX = 'installers/${{ needs.version.outputs.plugin_name }}-${{ needs.version.outputs.version }}';
const ASSETS = [`${ASSET_PREFIX}-Windows-x64.exe`, `${ASSET_PREFIX}-macOS.pkg`, `${ASSET_PREFIX}-Linux-x64.tar.gz`];
const KEY_TOKEN = /secrets\.TAURI_SIGNING_PRIVATE_KEY(?![A-Za-z0-9_])/g;
const PASSWORD_TOKEN = /secrets\.TAURI_SIGNING_PRIVATE_KEY_PASSWORD(?![A-Za-z0-9_])/g;

// Plan §4.8: the Phase 2 steps of release.yml per job, in order, with the existing step each run sits between.
const PHASE2_STEPS = {
  'macos-installer': {
    after: 'Build Installer',
    before: 'Upload Installer',
    names: ['Setup Node (facts)', 'Install feed-contract deps', 'Record macOS facts', 'Upload macOS facts'],
  },
  'linux-archive': {
    after: 'Create Archive',
    before: 'Upload Archive',
    names: ['Setup Node (facts)', 'Install feed-contract deps', 'Record Linux facts', 'Upload Linux facts'],
  },
  release: {
    after: 'Generate Release Notes',
    before: 'Create Release',
    names: [
      'Tag must not exist',
      'Download macOS facts',
      'Download Linux facts',
      'Setup Node (signing)',
      'Install feed-contract deps',
      'Install rsign2',
      'Check rsign2 version',
      'Signing configuration and key',
      'Sign assets',
      'Delete signing key (after signing)',
      'Write release.json and SHA256SUMS',
      'Verify signatures and sums',
    ],
    tail: [
      'Attest build provenance',
      'Stage dry-run artifact',
      'Upload dry-run artifact',
      'Verify dry-run artifact',
      'Delete signing key',
    ],
  },
};

// Plan §4.9: the backfill's jobs, in file order.
const BACKFILL_JOBS = ['plan', 'facts-macos', 'facts-linux', 'apply-dry', 'apply-live'];
const APPLY_JOBS = ['apply-dry', 'apply-live'];
// The key step of both apply jobs checks the real configuration (a backfill dry run still signs with the real key).
const SIGNING_CONFIG_LIVE =
  'gen-release-manifest.js --signing-config --dry-run false --public-key tools/feed-contract/keys/krate-signing.pub';
// apply-dry's one extra token: the ` --dry-run` switch of `backfill-sign.js apply`, never followed by a value (the key
// step's `--dry-run false` is gen-release-manifest's flag, present in both jobs).
const APPLY_DRY_SWITCH = / --dry-run(?![\w-])(?![ \t]+(?:true|false)\b)/g;

/** The trimmed, non-blank lines nested under a top-level (column 0) `key:`, or null. */
function topLevel(text, key) {
  const block = nested(text, 0, key);
  return block === null ? null : block.map((l) => l.trim()).filter((l) => l !== '');
}

/** A job's `permissions:` entries (trimmed, non-blank), or null. */
function jobPermissions(block) {
  const lines = nested(block, 4, 'permissions');
  return lines === null ? null : lines.map((l) => l.trim()).filter((l) => l !== '');
}

/** A job's `steps:` text with trailing blank lines removed. */
function stepsText(block) {
  const lines = nested(block, 4, 'steps');
  assert.ok(lines, 'the job has no steps: block');
  return lines.join('\n').trimEnd();
}

/** The `on.workflow_dispatch.inputs` block: {names, blocks: name -> text}. */
function dispatchInputs(text) {
  const lines = text.split('\n');
  const head = lines.findIndex((l) => l === '    inputs:');
  assert.ok(head >= 0, 'no on.workflow_dispatch.inputs block');
  const names = [];
  const blocks = new Map();
  let current = null;
  for (const line of lines.slice(head + 1)) {
    if (line.trim() !== '' && indentOf(line) < 6) break;
    const m = /^ {6}([A-Za-z0-9_-]+):\s*$/.exec(line);
    if (m) {
      current = [line];
      names.push(m[1]);
      blocks.set(m[1], current);
    } else if (current) {
      current.push(line);
    }
  }
  return { names, blocks: new Map([...blocks].map(([k, v]) => [k, v.join('\n')])) };
}

/** release.yml's own `options:` list (the shipped plugin directory names), read from the plugin input. */
function rosterOptions(text) {
  const plugin = dispatchInputs(text).blocks.get('plugin');
  assert.ok(plugin, 'no plugin input');
  const opts = nested(plugin, 8, 'options');
  assert.ok(opts, 'the plugin input has no options: list');
  const names = opts.map((l) => /^ {10}- (\S+)\s*$/.exec(l)).filter(Boolean).map((m) => m[1]);
  assert.ok(names.length >= 9, `options: lists ${names.length} plugin(s), expected at least 9`);
  return names;
}

test('release.yml and backfill-sign.yml keep the Phase 2 shape', async (t) => {
  const rel = readWorkflow('release.yml');
  const jobs = jobNames(rel);
  const jobSteps = new Map(jobs.map((j) => [j, stepBlocks(jobBlock(rel, j))]));
  const release = jobSteps.get('release');
  assert.ok(release && release.length > 0, 'release.yml has no release job steps');
  const idx = (name) => stepByName(release, name, 'release').index;

  await t.test('release.yml: dry_run boolean input, default false, and no other new input', () => {
    const { names, blocks } = dispatchInputs(rel);
    assert.deepEqual(names, ['plugin', 'prerelease', 'dry_run']);
    const dry = blocks.get('dry_run');
    assert.equal(field(dry, 8, 'type'), 'boolean');
    assert.equal(field(dry, 8, 'default'), 'false');
  });

  await t.test('release.yml: every Phase 2 step exists in its job, in order, at its place', () => {
    for (const [job, spec] of Object.entries(PHASE2_STEPS)) {
      const steps = jobSteps.get(job);
      assert.ok(steps, `no job ${job}`);
      const all = steps.map((s) => s.name);
      for (const n of [...spec.names, ...(spec.tail || [])]) stepByName(steps, n, job);
      const after = stepByName(steps, spec.after, job).index;
      const before = stepByName(steps, spec.before, job).index;
      assert.deepEqual(all.slice(after + 1, before), spec.names, `${job}: steps between ${spec.after} and ${spec.before}`);
      if (spec.tail) assert.deepEqual(all.slice(before + 1), spec.tail, `${job}: steps after ${spec.before}`);
    }
  });

  await t.test('release.yml: Create Release is skipped on dry runs and never replaces assets', () => {
    const step = stepByName(release, 'Create Release', 'release');
    assert.match(field(step.text, 8, 'if') ?? '', NOT_DRY);
    assert.equal(field(step.text, 10, 'make_latest'), 'false');
    assert.equal(field(step.text, 10, 'target_commitish'), '${{ github.sha }}');
    assert.equal(field(step.text, 10, 'fail_on_unmatched_files'), 'true');
    assert.equal(field(step.text, 10, 'overwrite_files'), 'false');
    const files = (nested(step.text, 10, 'files') ?? []).map((l) => l.trim()).filter((l) => l !== '');
    assert.deepEqual(files, [
      ...ASSETS,
      'installers/SHA256SUMS',
      ...ASSETS.map((a) => `${a}.minisig`),
      'installers/release.json',
    ]);
  });

  await t.test('release.yml: deploy-pages is skipped on dry runs', () => {
    assert.match(field(jobBlock(rel, 'deploy-pages'), 4, 'if') ?? '', NOT_DRY);
  });

  await t.test('release.yml: one build-provenance attestation, real runs only, of the three installers', () => {
    assert.equal(count(rel, /actions\/attest-build-provenance@v4/g), 1);
    assert.equal(rel.includes('actions/attest@'), false);
    const step = stepByName(release, 'Attest build provenance', 'release');
    assert.ok(step.text.includes('uses: actions/attest-build-provenance@v4'), 'the attestation step uses the action');
    assert.match(field(step.text, 8, 'if') ?? '', NOT_DRY);
    const subjects = (nested(step.text, 10, 'subject-path') ?? []).map((l) => l.trim()).filter((l) => l !== '');
    assert.deepEqual(subjects, ASSETS);
    assert.ok(idx('Attest build provenance') > idx('Create Release'), 'attestation after Create Release (PD-12)');
  });

  await t.test('release.yml: the signing secrets reach one step, before Create Release and every rsign sign', () => {
    assert.equal(count(rel, KEY_TOKEN), 1);
    assert.equal(count(rel, PASSWORD_TOKEN), 1);
    const key = stepByName(release, 'Signing configuration and key', 'release');
    assert.equal(count(key.text, KEY_TOKEN), 1);
    assert.equal(count(key.text, PASSWORD_TOKEN), 1);
    const run = runText(key);
    assert.match(run, /gen-release-manifest\.js --signing-config\b/);
    assert.ok(run.includes('--public-key tools/feed-contract/keys/krate-signing.pub'), 'the committed public key');
    assert.ok(run.includes('minisign.js decrypt-key'), 'the key is decrypted by minisign.js');
    assert.ok(key.index < idx('Create Release'));
    const signers = release.filter((s) => s.text.includes('rsign sign'));
    assert.ok(signers.length > 0, 'no rsign sign step');
    for (const s of signers) assert.ok(key.index < s.index, `${s.name} runs rsign sign before the key step`);
    for (const [job, steps] of jobSteps) {
      if (job === 'release') continue;
      assert.deepEqual(steps.filter((s) => s.text.includes('rsign sign')).map((s) => s.name), [], `${job} signs`);
    }
  });

  await t.test('release.yml: the key password is never inside a run: text', () => {
    const hits = [];
    for (const [job, steps] of jobSteps) {
      for (const s of steps) if (runText(s).includes('KRATE_SIGNING_KEY_PASSWORD')) hits.push(`${job}: ${s.name}`);
    }
    assert.deepEqual(hits, []);
  });

  await t.test('release.yml: no unsigned fallback and no ignored failure (D6, FR-047a)', () => {
    assert.equal(rel.includes('signing=off'), false);
    assert.deepEqual(
      rel.split('\n').filter((l) => l.includes('::warning::') && /unsigned/i.test(l)),
      [],
    );
    assert.equal(rel.includes('continue-on-error'), false);
  });

  await t.test('release.yml: the release job needs every installer job and has no forcing if:', () => {
    const block = jobBlock(rel, 'release');
    const needs = field(block, 4, 'needs') ?? '';
    const list = needs.replace(/^\[|\]$/g, '').split(',').map((s) => s.trim());
    for (const j of ['macos-installer', 'linux-archive']) assert.ok(list.includes(j), `needs ${j}: ${needs}`);
    const cond = field(block, 4, 'if');
    if (cond !== null) for (const f of ['always()', 'failure()', 'cancelled()']) assert.equal(cond.includes(f), false);
  });

  await t.test('release.yml: the release job permissions and the signing environment (FR-046, PD-22)', () => {
    const block = jobBlock(rel, 'release');
    const perms = (nested(block, 4, 'permissions') ?? []).map((l) => l.trim()).filter((l) => l !== '');
    assert.deepEqual(perms.sort(), ['attestations: write', 'contents: write', 'id-token: write']);
    const withEnv = jobs.filter((j) => field(jobBlock(rel, j), 4, 'environment') !== null);
    assert.deepEqual(withEnv, ['release']);
    assert.equal(field(block, 4, 'environment'), 'signing');
  });

  await t.test('release.yml: every job that decrypts a key deletes it with if: always()', () => {
    const decrypting = [...jobSteps].filter(([, steps]) => steps.some((s) => s.text.includes('minisign.js decrypt-key')));
    assert.deepEqual(decrypting.map(([j]) => j), ['release']);
    for (const [job, steps] of decrypting) {
      const deleters = steps.filter(
        (s) => /\brm -f\b[^\n]*krate\.key/.test(runText(s)) && field(s.text, 8, 'if') === 'always()',
      );
      assert.equal(deleters.length, 1, `${job}: one if: always() step deletes krate.key`);
    }
  });

  await t.test('release.yml: the key lives only while Sign assets runs (PD-20)', () => {
    const del = idx('Delete signing key (after signing)');
    assert.equal(del, idx('Sign assets') + 1);
    assert.ok(del < idx('Write release.json and SHA256SUMS'));
    assert.ok(del < idx('Create Release'));
    const step = stepByName(release, 'Delete signing key (after signing)', 'release');
    assert.match(runText(step), /\brm -f "\$RUNNER_TEMP\/krate\.key"/);
  });

  await t.test('release.yml: Record macOS facts cross-checks lipo against release-facts', () => {
    const run = runText(stepByName(jobSteps.get('macos-installer'), 'Record macOS facts', 'macos-installer'));
    assert.ok(run.includes('lipo -archs'));
    assert.ok(run.includes('release-facts.js darwin'));
  });

  await t.test('release.yml: Record Linux facts prechecks before tar, then objdump and linux facts (PD-13, PD-26)', () => {
    const run = runText(stepByName(jobSteps.get('linux-archive'), 'Record Linux facts', 'linux-archive'));
    const at = (re) => {
      const m = re.exec(run);
      assert.ok(m, `Record Linux facts lacks ${re}`);
      return m.index;
    };
    const umask = at(/\bumask 000\b/);
    const precheck = at(/release-facts\.js linux-precheck\b/);
    const tar = at(/\btar -xzf\b/);
    const objdump = at(/\bobjdump -T\b/);
    const facts = at(/release-facts\.js linux(?![-\w])/);
    assert.ok(umask < precheck && precheck < tar, 'umask 000, then linux-precheck, then tar -xzf');
    assert.ok(tar < objdump && objdump < facts, 'tar -xzf, then objdump -T, then release-facts.js linux');
  });

  await t.test('release.yml: Verify signatures and sums runs both verifiers and sha256sum before Create Release', () => {
    const step = stepByName(release, 'Verify signatures and sums', 'release');
    const run = runText(step);
    for (const s of ['minisign.js verify', 'rsign verify', 'sha256sum -c']) assert.ok(run.includes(s), s);
    assert.ok(step.index < idx('Create Release'));
  });

  await t.test('release.yml: the dry-run artifact steps run on dry runs only', () => {
    for (const n of ['Stage dry-run artifact', 'Upload dry-run artifact', 'Verify dry-run artifact']) {
      assert.match(field(stepByName(release, n, 'release').text, 8, 'if') ?? '', ON_DRY, n);
    }
    // FR-048: the Node verifier over the staged assets (the plan quotes the path: `".../minisign.js" verify`).
    assert.match(runText(stepByName(release, 'Verify dry-run artifact', 'release')), /minisign\.js"? verify\b/);
  });

  await t.test('release.yml: release.json names the tag the workflow creates (PD-11)', () => {
    const run = runText(stepByName(release, 'Write release.json and SHA256SUMS', 'release'));
    assert.match(run, /test "\$\(jq -r \.tag installers\/release\.json\)" = "\$TAG"/);
  });

  await t.test('release.yml: Tag must not exist guards real runs before the key step (PD-21)', () => {
    const step = stepByName(release, 'Tag must not exist', 'release');
    assert.match(field(step.text, 8, 'if') ?? '', NOT_DRY);
    const run = runText(step);
    assert.ok(run.includes('git rev-parse -q --verify'));
    assert.match(run, /gh release list\b[^\n]*--limit 1000\b/);
    assert.ok(step.index < idx('Signing configuration and key'));
  });

  await t.test('release.yml: the Phase 2 blocks name no plugin (ROSTER, PD-7)', () => {
    const roster = rosterOptions(rel);
    const blocks = [['input dry_run', dispatchInputs(rel).blocks.get('dry_run') ?? '']];
    for (const [job, spec] of Object.entries(PHASE2_STEPS)) {
      for (const n of [...spec.names, ...(spec.tail || [])]) {
        blocks.push([`${job}: ${n}`, stepByName(jobSteps.get(job), n, job).text]);
      }
    }
    const hits = [];
    for (const [where, text] of blocks) {
      for (const name of roster) if (new RegExp(`\\b${name}\\b`, 'i').test(text)) hits.push(`${where}: ${name}`);
    }
    assert.deepEqual(hits, []);
  });

  // --- backfill-sign.yml (FR-058, FR-059, FR-059a, PD-13, PD-14, PD-20, PD-22), read inside each subtest so a missing
  // file fails these subtests and leaves the release.yml ones standing.
  let backfill = null;
  const bf = () => {
    if (backfill === null) {
      const text = readWorkflow('backfill-sign.yml');
      const steps = new Map(jobNames(text).map((j) => [j, stepBlocks(jobBlock(text, j))]));
      backfill = { text, steps };
    }
    return backfill;
  };

  await t.test('backfill-sign.yml: workflow_dispatch is the only trigger, dry_run defaults to true (FR-058, FR-059a)', () => {
    const { text } = bf();
    const on = nested(text, 0, 'on');
    assert.ok(on, 'no top-level on: block');
    const triggers = on.map((l) => /^ {2}([A-Za-z0-9_-]+):/.exec(l)).filter(Boolean).map((m) => m[1]);
    assert.deepEqual(triggers, ['workflow_dispatch']);
    assert.deepEqual(text.split('\n').filter((l) => /^\s*(?:push|pull_request(?:_target)?|schedule)\s*:/.test(l)), []);
    const { names, blocks } = dispatchInputs(text);
    assert.deepEqual(names, ['dry_run']);
    assert.equal(field(blocks.get('dry_run'), 8, 'type'), 'boolean');
    assert.equal(field(blocks.get('dry_run'), 8, 'default'), 'true');
  });

  await t.test('backfill-sign.yml: read-only at the top, apply-dry read-only, apply-live the only writer (FR-058)', () => {
    const { text } = bf();
    assert.deepEqual(topLevel(text, 'permissions'), ['contents: read']);
    assert.deepEqual(jobNames(text), BACKFILL_JOBS);
    const dry = jobBlock(text, 'apply-dry');
    assert.match(field(dry, 4, 'if') ?? '', ON_DRY);
    assert.deepEqual(jobPermissions(dry), ['contents: read']);
    assert.equal(/contents:\s*write/.test(dry), false, 'apply-dry carries contents: write');
    const live = jobBlock(text, 'apply-live');
    assert.match(field(live, 4, 'if') ?? '', NOT_DRY);
    assert.deepEqual(jobPermissions(live), ['contents: write']);
    const writers = jobNames(text).filter((j) => /contents:\s*write/.test(jobBlock(text, j)));
    assert.deepEqual(writers, ['apply-live']);
    assert.equal(count(text, /contents:\s*write/g), 1, 'contents: write outside the apply-live job');
  });

  await t.test('backfill-sign.yml: environment signing on apply-dry and apply-live and no other job (PD-22)', () => {
    const { text } = bf();
    const withEnv = jobNames(text).filter((j) => field(jobBlock(text, j), 4, 'environment') !== null);
    assert.deepEqual(withEnv, APPLY_JOBS);
    for (const j of APPLY_JOBS) assert.equal(field(jobBlock(text, j), 4, 'environment'), 'signing', j);
  });

  await t.test('backfill-sign.yml: both apply jobs check out with fetch-depth: 0 (PD-14)', () => {
    const { steps } = bf();
    for (const j of APPLY_JOBS) {
      const checkouts = steps.get(j).filter((s) => /^ {8}uses: actions\/checkout@/m.test(s.text));
      assert.equal(checkouts.length, 1, `${j}: one checkout step`);
      assert.equal(field(checkouts[0].text, 10, 'fetch-depth'), '0', `${j}: fetch-depth`);
    }
  });

  // FR-051: apply's git runs in --plugins-dir, so that directory must be the checkout's plugins/ (the step keeps the
  // default working directory, $GITHUB_WORKSPACE); --work is $RUNNER_TEMP/work, outside the checkout.
  await t.test('backfill-sign.yml: both apply steps pass --plugins-dir plugins from the checkout (FR-051)', () => {
    const { steps } = bf();
    for (const j of APPLY_JOBS) {
      const apply = steps.get(j).find((s) => /backfill-sign\.js apply\b/.test(runText(s)));
      assert.ok(apply, `${j}: a backfill-sign.js apply step`);
      assert.equal(count(runText(apply), /--plugins-dir plugins\s/g), 1, `${j}: --plugins-dir plugins`);
      assert.equal(/working-directory:/.test(apply.text), false, `${j}: the apply step changes directory`);
    }
  });

  await t.test('backfill-sign.yml: apply-dry runs the steps of apply-live plus one --dry-run (FR-058)', () => {
    const { text, steps } = bf();
    const dry = stepsText(jobBlock(text, 'apply-dry'));
    const live = stepsText(jobBlock(text, 'apply-live'));
    assert.equal(count(dry, APPLY_DRY_SWITCH), 1, 'apply-dry: one --dry-run switch');
    assert.equal(count(live, APPLY_DRY_SWITCH), 0, 'apply-live: no --dry-run switch');
    for (const j of APPLY_JOBS) {
      const appliers = steps.get(j).filter((s) => /backfill-sign\.js apply\b/.test(runText(s)));
      assert.equal(appliers.length, 1, `${j}: one step runs backfill-sign.js apply`);
      if (j === 'apply-dry') assert.equal(count(appliers[0].text, APPLY_DRY_SWITCH), 1, 'the switch is on apply');
    }
    assert.equal(dry.replace(APPLY_DRY_SWITCH, ''), live);
  });

  await t.test('backfill-sign.yml: the signing secrets reach one key step per apply job (FR-059)', () => {
    const { text, steps } = bf();
    assert.equal(count(text, KEY_TOKEN), 2);
    assert.equal(count(text, PASSWORD_TOKEN), 2);
    for (const j of APPLY_JOBS) {
      const keySteps = steps.get(j).filter((s) => count(s.text, KEY_TOKEN) > 0);
      assert.equal(keySteps.length, 1, `${j}: one step references the key secret`);
      const [key] = keySteps;
      assert.equal(count(key.text, KEY_TOKEN), 1, `${j}: ${key.name}`);
      assert.equal(count(key.text, PASSWORD_TOKEN), 1, `${j}: ${key.name}`);
      const run = runText(key);
      assert.ok(run.includes(SIGNING_CONFIG_LIVE), `${j}: ${key.name} checks the real signing configuration`);
      assert.ok(run.includes('minisign.js decrypt-key'), `${j}: ${key.name} decrypts with minisign.js`);
      const apply = steps.get(j).find((s) => /backfill-sign\.js apply\b/.test(runText(s)));
      assert.ok(apply && key.index < apply.index, `${j}: the key step runs before backfill-sign.js apply`);
    }
    const hits = [];
    for (const [job, list] of steps) {
      for (const s of list) {
        const run = runText(s);
        if (run.includes('KRATE_SIGNING_KEY') || run.includes('secrets.')) hits.push(`${job}: ${s.name}`);
      }
    }
    assert.deepEqual(hits, [], 'the key or its password inside a run: text');
  });

  await t.test('backfill-sign.yml: each apply job deletes the key in an if: always() step (PD-20)', () => {
    const { steps } = bf();
    const decrypting = [...steps].filter(([, list]) => list.some((s) => s.text.includes('minisign.js decrypt-key')));
    assert.deepEqual(decrypting.map(([j]) => j), APPLY_JOBS);
    for (const j of APPLY_JOBS) {
      const del = stepByName(steps.get(j), 'Delete signing key', j);
      assert.equal(field(del.text, 8, 'if'), 'always()', `${j}: Delete signing key`);
      assert.match(runText(del), /\brm -f "\$RUNNER_TEMP\/krate\.key"/);
    }
  });

  // Krate Manager Phase 11 R-15 (user ruling 2026-11-02): a job that decrypts the real key compiles rsign2 fresh; a cache
  // another workflow also writes could hand it a planted binary.
  await t.test('release.yml and backfill-sign.yml: no key-holding job restores a cache or skips the rsign2 install', () => {
    const jobs = [['release.yml', 'release', release], ...APPLY_JOBS.map((j) => ['backfill-sign.yml', j, bf().steps.get(j)])];
    for (const [file, job, steps] of jobs) {
      assert.ok(steps.some((s) => s.text.includes('minisign.js decrypt-key')), `${file} ${job} decrypts the key`);
      assert.deepEqual(steps.filter((s) => s.text.includes('actions/cache')).map((s) => s.name), [], `${file} ${job} cache`);
      const install = stepByName(steps, 'Install rsign2', job);
      assert.equal(field(install.text, 8, 'if'), null, `${file} ${job}: Install rsign2 has an if:`);
      assert.equal(runText(install).trim(), 'cargo install rsign2 --version 0.6.7 --locked');
    }
  });

  await t.test('backfill-sign.yml: no --clobber anywhere (FR-059)', () => {
    assert.equal(bf().text.includes('--clobber'), false);
  });

  await t.test('backfill-sign.yml: the Linux facts step sets umask 000 first (PD-13)', () => {
    const list = bf().steps.get('facts-linux');
    assert.ok(list, 'no facts-linux job');
    const factSteps = list.filter((s) => /backfill-sign\.js facts\b/.test(runText(s)));
    assert.equal(factSteps.length, 1, 'facts-linux: one step runs backfill-sign.js facts');
    const lines = runText(factSteps[0]).split('\n').map((l) => stripComment(l)).filter((l) => l !== '');
    assert.equal(lines[0], 'umask 000');
  });

  await t.test('backfill-sign.yml: names no plugin of release.yml options: (ROSTER, FR-059)', () => {
    const { text } = bf();
    const hits = rosterOptions(rel).filter((name) => new RegExp(`\\b${name}\\b`, 'i').test(text));
    assert.deepEqual(hits, []);
  });
});

// --- SC-029: keys/README.md states the secrets contract and the FR-062 merge order (FR-061, FR-062, PD-22).

const KEYS_README = path.join(REPO_ROOT, 'tools', 'feed-contract', 'keys', 'README.md');
// The three FR-062 merge steps, as numbered list items of the README.
const MERGE_STEPS = [
  ['create the environment and its secrets', /^1\. Create the GitHub Environment `signing`/m],
  ['commit krate-signing.pub', /^2\. Commit `tools\/feed-contract\/keys\/krate-signing\.pub`/m],
  ['merge', /^3\. Only then merge/m],
];
// An instruction to use an empty password: an empty -p / --password argument, or "empty password" without "non-".
const EMPTY_PASSWORD = [
  /(?:^|\s)(?:-p|--password)[\s=]*''/g,
  /(?:^|\s)(?:-p|--password)[\s=]*""/g,
  /(?<!non-)empty password/gi,
];

test('keys README states the secrets contract and the merge order', async (t) => {
  const text = fs.readFileSync(KEYS_README, 'utf8').replace(/\r\n/g, '\n');
  const at = MERGE_STEPS.map(([what, re]) => {
    const m = re.exec(text);
    return { what, index: m ? m.index : -1 };
  });

  await t.test('names both secrets and the committed public key path', () => {
    for (const s of ['TAURI_SIGNING_PRIVATE_KEY_PASSWORD', 'tools/feed-contract/keys/krate-signing.pub']) {
      assert.ok(text.includes(s), `README names ${s}`);
    }
    assert.ok(/TAURI_SIGNING_PRIVATE_KEY(?![A-Za-z0-9_])/.test(text), 'README names TAURI_SIGNING_PRIVATE_KEY alone');
  });

  await t.test('tauri signer generate with a non-empty password', () => {
    assert.match(text, /tauri signer generate[^\n]* -p '[^'\n]+'/);
    assert.match(text, /non-empty/);
  });

  await t.test('Phase 11 passes the same password through TAURI_SIGNING_PRIVATE_KEY_PASSWORD', () => {
    assert.match(text, /Phase 11[\s\S]{0,200}?TAURI_SIGNING_PRIVATE_KEY_PASSWORD/);
  });

  await t.test('merge order: environment and secrets, then krate-signing.pub, then merge (FR-062)', () => {
    for (const s of at) assert.ok(s.index >= 0, `README has the merge step: ${s.what}`);
    assert.ok(at[0].index < at[1].index && at[1].index < at[2].index, JSON.stringify(at));
  });

  await t.test('the E_SIGNING_CONFIG consequence is stated after the merge step', () => {
    assert.ok(at[2].index >= 0, 'no merge step');
    assert.ok(text.indexOf('E_SIGNING_CONFIG', at[2].index) > at[2].index);
  });

  await t.test('before the merge step: the signing environment, main, and environment secrets (PD-22)', () => {
    assert.ok(at[2].index >= 0, 'no merge step');
    const before = text.slice(0, at[2].index);
    assert.ok(before.includes('`signing`'), 'names the `signing` environment');
    assert.ok(before.includes('`main`'), 'names the deployment branch `main`');
    assert.ok(before.includes('environment secrets'), 'says "environment secrets"');
  });

  await t.test('no instruction to use an empty password', () => {
    const hits = EMPTY_PASSWORD.flatMap((re) => text.match(re) || []);
    assert.deepEqual(hits, []);
  });
});

// --- SC-010: feed.yml and the release.yml feed job keep the Phase 3 shape (spec specs/krate-manager-phase3/spec.md
// SC-010, FR-001 to FR-015, FR-030 to FR-033, FR-062; plan §4.3.1, §4.4, §7.1 T9, PD-1, PD-2). Same text helpers as
// above; every file is only read.

const FEED_STEPS = [
  'Checkout',
  'Setup Node',
  'Install feed-contract deps',
  'Collect releases',
  'Fetch live feed',
  'Generate feed',
  'Compare with the live feed',
  'Upload site',
  'Upload evidence',
];
const FEED_CONCURRENCY = ['group: krate-feed', 'cancel-in-progress: false', 'queue: max'];
const FEED_OUTPUTS = [
  'manager_degraded: ${{ steps.collect.outputs.manager_degraded }}',
  'feed_degraded: ${{ steps.generate.outputs.feed_degraded }}',
  'drift_warned: ${{ steps.compare.outputs.drift_warned }}',
];
const outcomeLine = (tool) =>
  `node tools/feed-publish.js outcome --tool ${tool} --exit "$rc" --github-output "$GITHUB_OUTPUT"`;
// Plan §4.3.1 (binding): the three capture steps, verbatim.
const CAPTURE = new Map([
  [
    'Collect releases',
    [
      'collect',
      'rc=0; node tools/feed-publish.js collect --plugins-dir plugins --repo "$GITHUB_REPOSITORY"' +
        ' --out-dir "$RUNNER_TEMP/feed-in" --annotate || rc=$?',
    ],
  ],
  [
    'Generate feed',
    [
      'generate',
      'rc=0; node tools/feed-publish.js generate --in-dir "$RUNNER_TEMP/feed-in" --plugins-dir plugins' +
        ' --repo "$GITHUB_REPOSITORY" --site-dir _site --annotate || rc=$?',
    ],
  ],
  [
    'Compare with the live feed',
    [
      'compare',
      'rc=0; node tools/feed-publish.js compare --live "$RUNNER_TEMP/feed-in/live-versions.json"' +
        ' --feed _site/versions.json --releases "$RUNNER_TEMP/feed-in/releases.json" --dry-run "$DRY_RUN"' +
        ' --report "$RUNNER_TEMP/feed-in/compare.txt" --summary "$GITHUB_STEP_SUMMARY" --annotate || rc=$?',
    ],
  ],
]);
const FETCH_LIVE =
  'curl --fail --silent --show-error --location --retry 3 https://krateaudio.com/versions.json' +
  ' --output "$RUNNER_TEMP/feed-in/live-versions.json"';
const VERDICT_STEPS = ['Checkout', 'Setup Node', 'Install feed-contract deps', 'Download evidence', 'Verdict'];
const VERDICT_ENV = [
  'DRY_RUN: ${{ inputs.dry_run }}',
  'REF: ${{ github.ref }}',
  'BUILD_RESULT: ${{ needs.build.result }}',
  'DEPLOY_RESULT: ${{ needs.deploy.result }}',
  'FEED_DEGRADED: ${{ needs.build.outputs.feed_degraded }}',
  'MANAGER_DEGRADED: ${{ needs.build.outputs.manager_degraded }}',
  'DRIFT_WARNED: ${{ needs.build.outputs.drift_warned }}',
];
const VERDICT_RUN =
  'node tools/feed-publish.js verdict --dry-run "$DRY_RUN" --ref "$REF" --build "$BUILD_RESULT"' +
  ' --deploy "$DEPLOY_RESULT" --feed-degraded "$FEED_DEGRADED" --manager-degraded "$MANAGER_DEGRADED"' +
  ' --drift-warned "$DRIFT_WARNED" --evidence "$RUNNER_TEMP/evidence" --annotate';
// A `|` that is not part of `||` (spec D13, FR-003; plan §4.3.1 notes).
const LONE_PIPE = /(?<!\|)\|(?!\|)/;
// Plan §4.4 (binding): the release.yml feed job and the two comment lines above it.
const RELEASE_FEED_COMMENT = [
  '  # versions.json (the in-plugin update checker and the manager read it) is built, checked and deployed by the',
  '  # reusable feed workflow (specs/krate-manager-phase3/spec.md FR-030; plan §4.4). On a dry run feed.yml checks only.',
];
const RELEASE_FEED_JOB = [
  '  feed:',
  '    needs: [release, deploy-pages]',
  "    if: ${{ !cancelled() && needs.release.result == 'success' }}",
  '    permissions:',
  '      contents: write',
  '    uses: ./.github/workflows/feed.yml',
  '    with:',
  '      dry_run: ${{ inputs.dry_run }}',
].join('\n');
const DEPLOY_PAGES_STEPS = [
  'Checkout',
  'Generate Plugin Landing Page',
  'Generate Signal Flow Diagram',
  'Generate Root Index',
  'Deploy to gh-pages',
];
const SNAPSHOT_PLUGINS = path.join(
  REPO_ROOT, 'tools', 'feed-contract', 'fixtures', 'gen-feed', 'snapshot-2026-10-29', 'plugins',
);

/** Trimmed, non-blank lines of a nested() result, or null. */
function trimmed(lines) {
  return lines === null ? null : lines.map((l) => l.trim()).filter((l) => l !== '');
}

/** The inputs of trigger `trigger` under the top-level `on:`: {names, blocks: name -> text}, or null. */
function triggerInputs(text, trigger) {
  const on = nested(text, 0, 'on');
  assert.ok(on, 'no top-level on: block');
  const trig = nested(on.join('\n'), 2, trigger);
  if (trig === null) return null;
  const inputs = nested(trig.join('\n'), 4, 'inputs');
  assert.ok(inputs, `on.${trigger} has no inputs: block`);
  const names = [];
  const blocks = new Map();
  let current = null;
  for (const line of inputs) {
    const m = /^ {6}([A-Za-z0-9_-]+):\s*$/.exec(line);
    if (m) {
      current = [line];
      names.push(m[1]);
      blocks.set(m[1], current);
    } else if (current) {
      current.push(line);
    }
  }
  return { names, blocks: new Map([...blocks].map(([k, v]) => [k, v.join('\n')])) };
}

/** A run: script's trimmed, non-blank lines. */
function scriptLines(step) {
  return runText(step).split('\n').map((l) => l.trim()).filter((l) => l !== '');
}

test('feed.yml and release.yml keep the Phase 3 shape', async (t) => {
  // Each file is read inside its subtests, so a missing feed.yml fails the feed.yml subtests and leaves the
  // release.yml and ci.yml ones standing.
  let feed = null;
  const fd = () => {
    if (feed === null) {
      const text = readWorkflow('feed.yml');
      const jobs = jobNames(text);
      const steps = new Map(jobs.map((j) => [j, stepBlocks(jobBlock(text, j))]));
      feed = { text, jobs, steps };
    }
    return feed;
  };

  await t.test('feed.yml: workflow_call and workflow_dispatch only, each with the one input dry_run (FR-001)', () => {
    const { text } = fd();
    const on = nested(text, 0, 'on');
    assert.ok(on, 'no top-level on: block');
    const triggers = on.map((l) => /^ {2}([A-Za-z0-9_-]+):/.exec(l)).filter(Boolean).map((m) => m[1]);
    assert.deepEqual(triggers, ['workflow_call', 'workflow_dispatch']);
    assert.deepEqual(text.split('\n').filter((l) => /^\s*(?:push|pull_request(?:_target)?|schedule)\s*:/.test(l)), []);
    const call = triggerInputs(text, 'workflow_call');
    assert.deepEqual(call.names, ['dry_run']);
    assert.equal(field(call.blocks.get('dry_run'), 8, 'type'), 'boolean');
    assert.equal(field(call.blocks.get('dry_run'), 8, 'required'), 'true');
    assert.equal(field(call.blocks.get('dry_run'), 8, 'default'), null);
    const dispatch = triggerInputs(text, 'workflow_dispatch');
    assert.deepEqual(dispatch.names, ['dry_run']);
    assert.equal(field(dispatch.blocks.get('dry_run'), 8, 'type'), 'boolean');
    assert.equal(field(dispatch.blocks.get('dry_run'), 8, 'default'), 'true');
  });

  await t.test('feed.yml: read-only at the top; krate-feed on build and deploy only, and in no other file (PD-1)', () => {
    const { text } = fd();
    assert.deepEqual(topLevel(text, 'permissions'), ['contents: read']);
    assert.equal(topLevel(text, 'concurrency'), null, 'feed.yml has a top-level concurrency');
    for (const j of ['build', 'deploy']) {
      assert.deepEqual(trimmed(nested(jobBlock(text, j), 4, 'concurrency')), FEED_CONCURRENCY, j);
    }
    assert.equal(nested(jobBlock(text, 'verdict'), 4, 'concurrency'), null, 'verdict has a concurrency block');
    assert.equal(count(text, /group: krate-feed/g), 2);
    const others = fs
      .readdirSync(WORKFLOWS)
      .filter((n) => /\.ya?ml$/.test(n) && n !== 'feed.yml')
      .filter((n) => readWorkflow(n).includes('group: krate-feed'));
    assert.deepEqual(others, []);
  });

  await t.test('feed.yml: jobs build, deploy, verdict; deploy the only writer; no environment, secret or plugin (FR-002, FR-015)', () => {
    const { text, jobs } = fd();
    assert.deepEqual(jobs, ['build', 'deploy', 'verdict']);
    assert.deepEqual(jobs.filter((j) => /contents:\s*write/.test(jobBlock(text, j))), ['deploy']);
    assert.equal(count(text, /contents:\s*write/g), 1, 'contents: write outside the deploy job');
    assert.deepEqual(jobPermissions(jobBlock(text, 'deploy')), ['contents: write']);
    assert.equal(/^\s*environment\s*:/m.test(text), false, 'an environment: key');
    assert.equal(text.includes('secrets.'), false, 'a secrets. reference');
    const names = fs
      .readdirSync(SNAPSHOT_PLUGINS)
      .filter((d) => fs.existsSync(path.join(SNAPSHOT_PLUGINS, d, 'version.json')))
      .map((d) => JSON.parse(fs.readFileSync(path.join(SNAPSHOT_PLUGINS, d, 'version.json'), 'utf8')).name);
    assert.equal(names.length, 9, 'the snapshot holds nine version.json copies');
    const lower = text.toLowerCase();
    assert.deepEqual(names.filter((n) => lower.includes(String(n).toLowerCase())), []);
  });

  await t.test('feed.yml: build runs on ubuntu-latest with GH_TOKEN only, three outputs, the nine steps in order (FR-003 to FR-011)', () => {
    const { text, steps } = fd();
    const block = jobBlock(text, 'build');
    assert.equal(field(block, 4, 'runs-on'), 'ubuntu-latest');
    assert.deepEqual(trimmed(nested(block, 4, 'env')), ['GH_TOKEN: ${{ github.token }}']);
    assert.deepEqual(trimmed(nested(block, 4, 'outputs')), FEED_OUTPUTS);
    const build = steps.get('build');
    assert.deepEqual(build.map((s) => s.name), FEED_STEPS);
    const checkout = stepByName(build, 'Checkout', 'build');
    assert.equal(field(checkout.text, 8, 'uses'), 'actions/checkout@v6');
    assert.equal(nested(checkout.text, 8, 'with'), null, 'build checkout has a with: block');
    assert.equal(/\b(?:ref|fetch-depth):/.test(checkout.text), false, 'build checkout sets ref or fetch-depth');
    const node = stepByName(build, 'Setup Node', 'build');
    assert.equal(field(node.text, 8, 'uses'), 'actions/setup-node@v4');
    assert.equal(field(node.text, 10, 'node-version'), "'24'");
    assert.equal(field(stepByName(build, 'Collect releases', 'build').text, 8, 'id'), 'collect');
    assert.equal(field(stepByName(build, 'Generate feed', 'build').text, 8, 'id'), 'generate');
    const compare = stepByName(build, 'Compare with the live feed', 'build');
    assert.equal(field(compare.text, 8, 'id'), 'compare');
    assert.deepEqual(trimmed(nested(compare.text, 8, 'env')), ['DRY_RUN: ${{ inputs.dry_run }}']);
  });

  await t.test('feed.yml: every run: step of build and verdict is bash, set -euo pipefail, no lone | and no ${{ (FR-003, FR-014)', () => {
    const { steps } = fd();
    const runs = [];
    for (const j of ['build', 'verdict']) for (const s of steps.get(j)) if (runText(s) !== '') runs.push([j, s]);
    assert.ok(runs.length >= 7, `only ${runs.length} run: step(s) in build and verdict`);
    for (const [j, s] of runs) {
      const where = `${j}: ${s.name}`;
      assert.equal(field(s.text, 8, 'shell'), 'bash', where);
      assert.equal(scriptLines(s)[0], 'set -euo pipefail', where);
      assert.equal(LONE_PIPE.test(runText(s)), false, `${where}: a | that is not ||`);
      assert.equal(runText(s).includes('${{'), false, `${where}: an expression inside the script`);
    }
  });

  await t.test('feed.yml: the capture steps, the live fetch, and no mkdir, cmp or _site/manager outside generate (D13, FR-005)', () => {
    const { steps } = fd();
    const build = steps.get('build');
    for (const [name, [tool, command]] of CAPTURE) {
      assert.deepEqual(scriptLines(stepByName(build, name, 'build')), ['set -euo pipefail', command, outcomeLine(tool)], name);
      assert.match(command, new RegExp(`^rc=0; node tools/feed-publish\\.js ${tool} .* \\|\\| rc=\\$\\?$`), name);
    }
    assert.deepEqual(scriptLines(stepByName(build, 'Fetch live feed', 'build')), ['set -euo pipefail', FETCH_LIVE]);
    const hits = [];
    for (const [j, list] of steps) {
      for (const s of list) {
        if (j === 'build' && s.name === 'Generate feed') continue;
        if (s.text.includes('_site/manager')) hits.push(`${j}: ${s.name}: _site/manager`);
        if (/\b(?:mkdir|cmp)\b/.test(runText(s))) hits.push(`${j}: ${s.name}: mkdir or cmp`);
      }
    }
    assert.deepEqual(hits, []);
  });

  await t.test('feed.yml: Upload site fails without files; Upload evidence always runs (FR-011)', () => {
    const build = fd().steps.get('build');
    const site = stepByName(build, 'Upload site', 'build');
    assert.equal(field(site.text, 8, 'uses'), 'actions/upload-artifact@v7');
    assert.equal(field(site.text, 10, 'name'), 'feed-site');
    assert.equal(field(site.text, 10, 'path'), '_site/');
    assert.equal(field(site.text, 10, 'if-no-files-found'), 'error');
    assert.equal(field(site.text, 8, 'if'), null);
    const evidence = stepByName(build, 'Upload evidence', 'build');
    assert.equal(field(evidence.text, 8, 'if'), 'always()');
    assert.equal(field(evidence.text, 8, 'uses'), 'actions/upload-artifact@v7');
    assert.equal(field(evidence.text, 10, 'name'), 'feed-evidence');
    assert.equal(field(evidence.text, 10, 'path'), '${{ runner.temp }}/feed-in/');
    assert.equal(field(evidence.text, 10, 'if-no-files-found'), 'warn');
  });

  await t.test('feed.yml: deploy downloads the site and pushes it with peaceiris on a real run from main (FR-012, FR-013)', () => {
    const { text, steps } = fd();
    const block = jobBlock(text, 'deploy');
    assert.equal(field(block, 4, 'needs'), 'build');
    assert.equal(field(block, 4, 'if'), "${{ !inputs.dry_run && github.ref == 'refs/heads/main' }}");
    assert.equal(field(block, 4, 'runs-on'), 'ubuntu-latest');
    assert.equal(/^\s*run:/m.test(block), false, 'deploy has a run: step');
    assert.equal(block.includes('actions/checkout@'), false, 'deploy checks out');
    const deploy = steps.get('deploy');
    assert.deepEqual(deploy.map((s) => s.name), ['Download site', 'Deploy to gh-pages']);
    const download = deploy[0];
    assert.equal(field(download.text, 8, 'uses'), 'actions/download-artifact@v8');
    assert.equal(field(download.text, 10, 'name'), 'feed-site');
    assert.equal(field(download.text, 10, 'path'), '_site');
    const push = deploy[1];
    assert.equal(field(push.text, 8, 'uses'), 'peaceiris/actions-gh-pages@v4');
    assert.equal(field(push.text, 8, 'if'), null);
    assert.deepEqual(trimmed(nested(push.text, 8, 'with')), [
      'github_token: ${{ github.token }}',
      'publish_dir: ./_site',
      'keep_files: true',
      'cname: krateaudio.com',
    ]);
    assert.equal(text.includes('allow_empty_commit'), false);
    assert.equal(count(text, /peaceiris\/actions-gh-pages@/g), 1);
  });

  await t.test('feed.yml: verdict always runs, read-only, values only through env: (FR-014)', () => {
    const { text, steps } = fd();
    const block = jobBlock(text, 'verdict');
    assert.equal(field(block, 4, 'needs'), '[build, deploy]');
    assert.equal(field(block, 4, 'if'), 'always()');
    assert.deepEqual(jobPermissions(block), ['contents: read']);
    const verdict = steps.get('verdict');
    assert.deepEqual(verdict.map((s) => s.name), VERDICT_STEPS);
    const evidence = stepByName(verdict, 'Download evidence', 'verdict');
    assert.equal(field(evidence.text, 8, 'uses'), 'actions/download-artifact@v8');
    assert.equal(field(evidence.text, 8, 'continue-on-error'), 'true');
    assert.equal(field(evidence.text, 10, 'name'), 'feed-evidence');
    assert.equal(field(evidence.text, 10, 'path'), '${{ runner.temp }}/evidence');
    const step = stepByName(verdict, 'Verdict', 'verdict');
    assert.deepEqual(trimmed(nested(step.text, 8, 'env')), VERDICT_ENV);
    assert.deepEqual(scriptLines(step), ['set -euo pipefail', VERDICT_RUN]);
    assert.equal(runText(step).includes('${{'), false);
  });

  await t.test('release.yml: the feed job replaces the inline versions.json step (FR-030 to FR-033)', () => {
    const rel = readWorkflow('release.yml');
    const jobs = jobNames(rel);
    assert.ok(jobs.includes('feed'), 'release.yml has no feed job');
    assert.equal(jobs.indexOf('feed'), jobs.indexOf('deploy-pages') + 1, 'feed is not placed after deploy-pages');
    assert.equal(jobBlock(rel, 'feed').trimEnd(), RELEASE_FEED_JOB);
    const lines = rel.split('\n');
    const head = lines.indexOf('  feed:');
    assert.deepEqual(lines.slice(head - 2, head), RELEASE_FEED_COMMENT);
    assert.deepEqual(lines.filter((l) => l.includes('versions.json')), [RELEASE_FEED_COMMENT[0]]);
    const named = jobs.filter((j) => stepBlocks(jobBlock(rel, j)).some((s) => s.name === 'Generate Update Checker Endpoint'));
    assert.deepEqual(named, []);
    const pages = jobBlock(rel, 'deploy-pages');
    assert.equal(field(pages, 4, 'if'), '${{ !inputs.dry_run }}');
    const pageSteps = stepBlocks(pages);
    assert.deepEqual(pageSteps.map((s) => s.name), DEPLOY_PAGES_STEPS);
    assert.equal(field(stepByName(pageSteps, 'Deploy to gh-pages', 'deploy-pages').text, 10, 'keep_files'), 'true');
  });

  await t.test("ci.yml: the paths-filter shared: block is still exactly - 'plugins/shared/**' (FR-062)", () => {
    const ci = readWorkflow('ci.yml');
    const filterSteps = [];
    for (const j of jobNames(ci)) {
      for (const s of stepBlocks(jobBlock(ci, j))) if (field(s.text, 8, 'uses') === 'dorny/paths-filter@v3') filterSteps.push(s);
    }
    assert.equal(filterSteps.length, 1, 'ci.yml: one dorny/paths-filter step');
    const filters = nested(filterSteps[0].text, 10, 'filters');
    assert.ok(filters, 'the paths-filter step has no filters: block');
    assert.deepEqual(trimmed(nested(filters.join('\n'), 12, 'shared')), ["- 'plugins/shared/**'"]);
  });
});
