'use strict';
require('./helpers/write-guard.js');

// Hygiene tests for the feed-contract tooling (spec FR-061, SC-015; plan §2, §4.6, PD-15).
// `tools stay offline`: (a) source scan, (b) first-statement check, (c) the write guard has teeth.
// (d) (git status --porcelain unchanged) is a CI step and a close-lane record, not a Node test.
// Also: `generator ships no plugin roster` (SC-014), `no serial test flags` (SC-019), `fixtures are in sync with
// make-fixtures.js` (PD-7) and `CONTRACT.md documents every code and field` (PD-17).

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
// Built from pieces so the SC-015(a) scan below does not match this file; the teeth probes need spawnSync / execSync.
const childProcess = require('child' + '_process');

const { loadCase, runMain, v2Argv } = require('./helpers/fixtures.js');

const TESTS_DIR = path.resolve(__dirname);
const CONTRACT_DIR = path.resolve(TESTS_DIR, '..');
const TOOLS_DIR = path.resolve(CONTRACT_DIR, '..');
const REPO_ROOT = path.resolve(TOOLS_DIR, '..');

function listFiles(dir, predicate) {
  if (!fs.existsSync(dir)) return [];
  return fs
    .readdirSync(dir, { withFileTypes: true })
    .filter((d) => d.isFile() && predicate(d.name))
    .map((d) => path.join(dir, d.name));
}

function scannedFiles() {
  const single = [
    path.join(TOOLS_DIR, 'gen-feed.js'),
    path.join(TOOLS_DIR, 'minisign.js'),
    path.join(TOOLS_DIR, 'release-facts.js'),
    path.join(TOOLS_DIR, 'gen-release-manifest.js'),
    path.join(TOOLS_DIR, 'backfill-sign.js'),
    path.join(TOOLS_DIR, 'feed-publish.js'),
    path.join(TOOLS_DIR, 'lint-manager-version.js'),
    path.join(TOOLS_DIR, 'gen-manager-latest.js'),
    path.join(TOOLS_DIR, 'manager-release-preflight.js'),
    path.join(TOOLS_DIR, 'gen-root-index.js'),
    path.join(TOOLS_DIR, 'manager-readiness-counts.js'),
    path.join(CONTRACT_DIR, 'fixtures', 'make-fixtures.js'),
    path.join(CONTRACT_DIR, 'fixtures', 'listings', 'make-bad.js'),
    path.join(CONTRACT_DIR, 'fixtures', 'phase3', 'make-phase3.js'),
  ].filter((f) => fs.existsSync(f));
  return [
    ...single,
    ...listFiles(CONTRACT_DIR, (n) => n.endsWith('.js')),
    ...listFiles(TESTS_DIR, (n) => n.endsWith('.test.js')),
    ...listFiles(path.join(TESTS_DIR, 'helpers'), (n) => n.endsWith('.js')),
  ];
}

function rel(f) {
  return path.relative(path.resolve(TOOLS_DIR, '..'), f).split(path.sep).join('/');
}

// Every pattern is assembled from pieces so this file's own source never matches it.
const NET_MODULES = ['ht' + 'tp', 'ht' + 'tps', 'ht' + 'tp2', 'n' + 'et', 't' + 'ls', 'd' + 'gram'];
const NET_RE = new RegExp(
  '(require\\s*\\(|import\\s*\\(|from\\s+|import\\s+)\\s*[\'"](node:)?(' + NET_MODULES.join('|') + ')[\'"]',
);
const FETCH_RE = new RegExp('\\b' + 'fet' + 'ch' + '\\s*\\(');
const CHILD_RE = new RegExp('(node:)?' + 'child' + '_process');
const DESTRUCTURE_FS_RE = new RegExp(
  '\\{[^}]*\\}\\s*=\\s*require\\s*\\(\\s*[\'"](node:)?' + 'f' + 's(/promises)?[\'"]\\s*\\)',
);
const DESTRUCTURE_CHILD_RE = new RegExp(
  '\\{[^}]*\\}\\s*=\\s*require\\s*\\(\\s*[\'"](node:)?' + 'child' + '_process[\'"]\\s*\\)',
);
const CHILD_EXEMPT = new Set([
  'tools/feed-contract/tests/git-versionsort.test.js',
  'tools/feed-contract/tests/c4-listing-dir.test.js',
  'tools/feed-contract/tests/helpers/write-guard.js',
  'tools/backfill-sign.js',
  'tools/feed-contract/tests/helpers/external-tools.js',
  'tools/feed-publish.js',
  'tools/gen-root-index.js',
]);
const GUARD_REL = 'tools/feed-contract/tests/helpers/write-guard.js';
const GUARD_LINE = "require('./helpers/write-guard.js');";

// Returns the first statement line after leading comments, blank lines and one optional 'use strict'; line.
function firstStatement(source) {
  const lines = source.split(/\r?\n/);
  let inBlock = false;
  let sawStrict = false;
  for (const raw of lines) {
    let line = raw.trim();
    if (inBlock) {
      const end = line.indexOf('*/');
      if (end < 0) continue;
      inBlock = false;
      line = line.slice(end + 2).trim();
    }
    if (line.startsWith('/*')) {
      const end = line.indexOf('*/', 2);
      if (end < 0) {
        inBlock = true;
        continue;
      }
      line = line.slice(end + 2).trim();
    }
    if (line === '' || line.startsWith('//')) continue;
    if (!sawStrict && (line === "'use strict';" || line === '"use strict";')) {
      sawStrict = true;
      continue;
    }
    return line;
  }
  return null;
}

test('tools stay offline', async (t) => {
  await t.test('(a) source scan: no network modules, no fetch, child processes and fs only as allowed', () => {
    const files = scannedFiles();
    // Completeness: scannedFiles() drops a missing path, so every named file must exist and the test glob must reach
    // at least ten files, or the scan could pass on an empty or partial list.
    const scanned = new Set(files.map(rel));
    const missing = [
      'tools/gen-feed.js',
      'tools/minisign.js',
      'tools/release-facts.js',
      'tools/gen-release-manifest.js',
      'tools/backfill-sign.js',
      'tools/feed-publish.js',
      'tools/lint-manager-version.js',
      'tools/gen-manager-latest.js',
      'tools/manager-release-preflight.js',
      'tools/gen-root-index.js',
      'tools/manager-readiness-counts.js',
      'tools/feed-contract/contract.js',
      'tools/feed-contract/listing.js',
      'tools/feed-contract/tests/helpers/write-guard.js',
      'tools/feed-contract/tests/helpers/fixtures.js',
      'tools/feed-contract/fixtures/make-fixtures.js',
      'tools/feed-contract/fixtures/listings/make-bad.js',
      'tools/feed-contract/fixtures/phase3/make-phase3.js',
    ].filter((r) => !scanned.has(r));
    assert.deepEqual(missing, [], 'named files missing from the scan list');
    const testFiles = [...scanned].filter((r) => r.startsWith('tools/feed-contract/tests/') && r.endsWith('.test.js'));
    assert.ok(testFiles.length >= 10, `the scan reaches ${testFiles.length} test file(s), expected at least 10`);
    const hits = [];
    for (const f of files) {
      const r = rel(f);
      const src = fs.readFileSync(f, 'utf8');
      if (NET_RE.test(src)) hits.push(`${r}: network module`);
      if (FETCH_RE.test(src)) hits.push(`${r}: fetch call`);
      if (!CHILD_EXEMPT.has(r) && CHILD_RE.test(src)) hits.push(`${r}: child process module`);
      if (r !== GUARD_REL && DESTRUCTURE_FS_RE.test(src)) hits.push(`${r}: destructured fs require`);
      if (r !== GUARD_REL && DESTRUCTURE_CHILD_RE.test(src)) hits.push(`${r}: destructured child process require`);
    }
    assert.deepEqual(hits, []);
  });

  await t.test('(b) every test file requires the write guard as its first statement', () => {
    const files = listFiles(TESTS_DIR, (n) => n.endsWith('.test.js'));
    assert.ok(files.length > 0, 'no test files found');
    const bad = files
      .map((f) => [rel(f), firstStatement(fs.readFileSync(f, 'utf8'))])
      .filter(([, first]) => first !== GUARD_LINE)
      .map(([r, first]) => `${r}: ${first}`);
    assert.deepEqual(bad, []);
  });

  await t.test('(c) the guard has teeth', async () => {
    const outside = { code: 'E_WRITE_OUTSIDE_TMP' };
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km1-guard-'));
    const repoDir = TESTS_DIR;
    const name = crypto.randomBytes(6).toString('hex');
    const repoTarget = path.join(repoDir, name);
    try {
      assert.throws(() => fs.writeFileSync(repoTarget, 'x'), outside);
      assert.equal(fs.existsSync(repoTarget), false);
      await assert.rejects(() => fs.promises.writeFile(repoTarget, 'x'), outside);
      assert.equal(fs.existsSync(repoTarget), false);
      assert.throws(() => fs.mkdirSync(repoTarget), outside);
      assert.equal(fs.existsSync(repoTarget), false);
      fs.writeFileSync(path.join(tmp, 'a'), 'x');
      assert.throws(() => fs.renameSync(path.join(tmp, 'a'), repoTarget), outside);
      assert.equal(fs.existsSync(repoTarget), false);
      assert.throws(() => fs.openSync(repoTarget, 'w'), outside);
      assert.equal(fs.existsSync(repoTarget), false);
      assert.throws(() => childProcess.spawnSync('git', ['status'], { cwd: repoDir }), outside);

      // PD-15 (a): an intermediate link that points into the repo.
      const link = path.join(tmp, 'L');
      fs.symlinkSync(repoDir, link, process.platform === 'win32' ? 'junction' : 'dir');
      assert.throws(() => fs.writeFileSync(tmp + '/L/../' + name, 'x'), outside);
      assert.throws(() => fs.writeFileSync(path.join(tmp, 'L', name), 'x'), outside);
      assert.equal(fs.existsSync(repoTarget), false);
      assert.equal(fs.existsSync(path.join(tmp, name)), false);

      // PD-15 (b): a hard link from a repo file into tmp.
      assert.throws(() => fs.linkSync(path.resolve(__filename), path.join(tmp, name)), outside);
      assert.equal(fs.existsSync(path.join(tmp, name)), false);

      // PD-15 (c): spawning is an allowlist (git and tar in fixed shapes, cwd under tmp, no shell).
      assert.throws(() => childProcess.execSync('echo x'), outside);
      assert.throws(() => childProcess.spawnSync('node', [], { cwd: tmp }), outside);
      assert.throws(() => childProcess.spawnSync('git', ['-C', repoDir, 'status'], { cwd: tmp }), outside);
      assert.throws(() => childProcess.spawnSync('tar', ['-xzf', 'x.tar.gz', '-C', repoDir], { cwd: tmp }), outside);
      assert.throws(
        () => childProcess.spawnSync('tar', ['-xzf', 'x.tar.gz', '-C', tmp], { cwd: tmp, shell: true }),
        outside,
      );

      // FR-071: rsign only in its four fixed shapes, with a cwd under tmp.
      const k = path.join(tmp, 'k.key');
      const s = path.join(tmp, 'f.minisig');
      const f = path.join(tmp, 'f');
      assert.throws(
        () => childProcess.spawnSync('rsign', ['sign', '-W', '-s', k, '-t', 'x', '-x', s, f, '--extra'], { cwd: tmp }),
        outside,
      );
      assert.throws(() => childProcess.spawnSync('rsign', ['--version'], { cwd: repoDir }), outside);
      // Positive control: the version shape passes the guard (the spawn itself may report ENOENT without rsign).
      childProcess.spawnSync('rsign', ['--version'], { cwd: tmp, stdio: ['ignore', 'pipe', 'pipe'] });

      // Positive control: a write under the temp root succeeds.
      fs.writeFileSync(path.join(tmp, 'ok'), 'x');
      assert.equal(fs.readFileSync(path.join(tmp, 'ok'), 'utf8'), 'x');
    } finally {
      fs.rmSync(tmp, { recursive: true, force: true });
    }
  });

  // Phase 3 (SC-011, plan PD-16, tasks TA-4): main takes a required third deps argument, so the Phase 2 form test's
  // two-argument main([], io) call cannot cover it; the git-status check named (d) in the header stays a CI step.
  await t.test('(d) tools/feed-publish.js follows the tools/*.js form', () => {
    const { CODES } = require('../contract.js');
    const toolPath = path.join(TOOLS_DIR, 'feed-publish.js');
    const mod = require(toolPath);
    const src = fs.readFileSync(toolPath, 'utf8');
    assert.equal(src.split(/\r?\n/)[0], '#!/usr/bin/env node', 'feed-publish.js: first line');
    assert.equal(typeof mod.main, 'function', 'feed-publish.js: exports main');
    const used = [];
    const stubDeps = {
      gh: (argv) => {
        used.push('gh');
        throw new Error(`stubDeps.gh used: ${JSON.stringify(argv)}`);
      },
      genFeedMain: (argv) => {
        used.push('genFeedMain');
        throw new Error(`stubDeps.genFeedMain used: ${JSON.stringify(argv)}`);
      },
    };
    let stderr = '';
    const io = {
      stdout: { write: () => true },
      stderr: { write: (s) => ((stderr += s), true) },
      env: {},
    };
    assert.equal(mod.main([], io, stubDeps), 2, `main([], io, stubDeps) exit code; stderr ${JSON.stringify(stderr)}`);
    assert.deepEqual(used, [], 'main([], io, stubDeps) used a dependency');
    const usage = 'usage: node tools/feed-publish.js';
    assert.ok(
      stderr.split(/\r?\n/).some((l) => l.startsWith(usage)),
      `no stderr line starts "${usage}"; stderr ${JSON.stringify(stderr)}`,
    );
    const literals = [...src.matchAll(/'(E_[A-Z0-9_]+)'/g)].map((m) => m[1]);
    assert.deepEqual(
      [...new Set(literals.filter((c) => !Object.hasOwn(CODES, c)))],
      [],
      'feed-publish.js: code literals that contract.js CODES does not define',
    );
    // The seven Phase 3 codes with their plan §3.1 class.
    const PHASE3_CODES = {
      E_FEED_LIST_TRUNCATED: 'none',
      E_FEED_DOWNLOAD: 'none',
      E_FEED_SCHEMA: 'none',
      E_FEED_NONDETERMINISTIC: 'none',
      E_STEP_FAILED: 'none',
      E_V1_DRIFT: 'none',
      E_MANAGER_LATEST: 'degrade',
    };
    assert.equal(Object.keys(PHASE3_CODES).length, 7);
    assert.deepEqual(
      Object.entries(PHASE3_CODES)
        .filter(([c, cls]) => CODES[c] !== cls)
        .map(([c, cls]) => `${c}: want ${cls}, CODES has ${CODES[c]}`),
      [],
      'Phase 3 codes missing from CODES or with the wrong class',
    );
  });
});

// --- SC-014: the plugin list comes from the plugins directory, never from the tools.

/** Number of plugins.<Name> entries gen-feed writes for a case in v2 mode and in legacy mode (CLI, in-process). */
function entryCounts(caseName) {
  const info = loadCase(caseName);
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'km1-roster-'));
  try {
    const empty = path.join(tmp, 'empty-manifests');
    fs.mkdirSync(empty);
    const v2Out = path.join(tmp, 'v2.json');
    const v2 = runMain(v2Argv(info, v2Out, empty));
    assert.equal(v2.code, 0, `${caseName} v2: ${v2.stderr}`);
    const legacyOut = path.join(tmp, 'legacy.json');
    const legacy = runMain(['--legacy', '--plugins-dir', info.pluginsDir, '--tags', info.tagsPath, '--out', legacyOut]);
    assert.equal(legacy.code, 0, `${caseName} legacy: ${legacy.stderr}`);
    const count = (f) => Object.keys(JSON.parse(fs.readFileSync(f, 'utf8')).plugins).length;
    return { v2: count(v2Out), legacy: count(legacyOut) };
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

test('generator ships no plugin roster', async (t) => {
  await t.test('a tenth plugin directory yields ten entries', () => {
    assert.deepEqual(entryCounts('tenth-plugin'), { v2: 10, legacy: 10 });
  });

  await t.test('the 2026-10-29 snapshot yields nine entries', () => {
    assert.deepEqual(entryCounts('snapshot-2026-10-29'), { v2: 9, legacy: 9 });
  });

  await t.test('no product file names a shipped plugin', () => {
    // The nine names come from the snapshot's version.json copies, so this file never spells them.
    const snapPlugins = loadCase('snapshot-2026-10-29').pluginsDir;
    const names = fs
      .readdirSync(snapPlugins)
      .filter((d) => fs.existsSync(path.join(snapPlugins, d, 'version.json')))
      .map((d) => JSON.parse(fs.readFileSync(path.join(snapPlugins, d, 'version.json'), 'utf8')).name);
    assert.equal(names.length, 9, 'the snapshot holds nine version.json copies');
    for (const n of names) assert.ok(typeof n === 'string' && n.length > 0, `version.json name ${JSON.stringify(n)}`);
    const products = [
      path.join(TOOLS_DIR, 'gen-feed.js'),
      path.join(TOOLS_DIR, 'minisign.js'),
      path.join(TOOLS_DIR, 'release-facts.js'),
      path.join(TOOLS_DIR, 'gen-release-manifest.js'),
      path.join(TOOLS_DIR, 'backfill-sign.js'),
      path.join(TOOLS_DIR, 'feed-publish.js'),
      path.join(CONTRACT_DIR, 'contract.js'),
      path.join(CONTRACT_DIR, 'listing.js'),
      path.join(CONTRACT_DIR, 'versions.v2.schema.json'),
      path.join(CONTRACT_DIR, 'versions.v1.schema.json'),
      path.join(CONTRACT_DIR, 'release.schema.json'),
    ];
    const hits = [];
    for (const f of products) {
      const src = fs.readFileSync(f, 'utf8').toLowerCase();
      for (const n of names) if (src.includes(n.toLowerCase())) hits.push(`${rel(f)}: ${n}`);
    }
    assert.deepEqual(hits, []);
  });
});

// --- SC-019: nothing in the CI workflow or the docs serialises node --test.

function markdownFilesUnder(dir) {
  const out = [];
  for (const d of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, d.name);
    if (d.isDirectory()) out.push(...markdownFilesUnder(full));
    else if (d.isFile() && d.name.endsWith('.md')) out.push(full);
  }
  return out;
}

test('no serial test flags', () => {
  const SERIAL_FLAGS = ['--test-concurrency=1', '--test-concurrency 1', '--test-isolation=none', '--test-isolation none'];
  const workflow = path.join(REPO_ROOT, '.github', 'workflows', 'feed-tools.yml');
  assert.ok(fs.existsSync(workflow), 'the feed-tools workflow is missing');
  const docs = markdownFilesUnder(CONTRACT_DIR);
  assert.ok(docs.length > 0, 'no *.md file found under tools/feed-contract/');
  const hits = [];
  for (const f of [workflow, ...docs]) {
    const src = fs.readFileSync(f, 'utf8');
    for (const flag of SERIAL_FLAGS) if (src.includes(flag)) hits.push(`${rel(f)}: ${flag}`);
  }
  assert.deepEqual(hits, []);
});

// --- PD-7: the committed fixture inputs are exactly what the builder writes.

test('fixtures are in sync with make-fixtures.js', () => {
  // check() builds into an fs.mkdtemp dir in-process (under the write guard) and compares byte for byte.
  const { ok, diff } = require('../fixtures/make-fixtures.js').check();
  assert.equal(ok, true, `first differing file: ${diff}`);
});

// --- CONTRACT.md content (plan PD-17, §4.4, §7.1; spec FR-013, FR-017, FR-018, FR-034, FR-046)

const CONTRACT_HEADINGS = [
  '## 1. Scope and readers',
  '## 2. C1: versions.json schema_version 2',
  '## 3. Reader rules',
  '## 4. C2: release.json',
  '## 5. C3: trusted comments and the known-answer fixture',
  '## 6. C4: Linux archive layout and canonical listing',
  '## 7. gen-feed: modes, rules, codes and exit codes',
  '## 8. Fixture signatures',
];

// Splits Markdown into its '## ' sections (heading line -> body lines), ignoring headings inside ``` fences.
function markdownSections(text) {
  const sections = new Map();
  let current = null;
  let inFence = false;
  for (const line of text.split(/\r?\n/)) {
    if (line.startsWith('```')) inFence = !inFence;
    if (!inFence && line.startsWith('## ')) {
      assert.equal(sections.has(line), false, `heading appears twice: ${line}`);
      current = [];
      sections.set(line, current);
      continue;
    }
    if (current) current.push(line);
  }
  return sections;
}

// Cells of a Markdown table row ('| a | b |' -> ['a', 'b']); escaped pipes ('\|') stay inside their cell.
function tableCells(line) {
  return line
    .trim()
    .replace(/^\|/, '')
    .replace(/\|$/, '')
    .split(/(?<!\\)\|/)
    .map((c) => c.trim());
}

// Dotted property paths of a JSON Schema: walks properties, patternProperties (placeholder <key>),
// additionalProperties (placeholder <Name>: in C1 only plugins.<Name> uses it) and local $refs into $defs.
// Combinators (allOf / if / then / else / not) are not walked: they only restate paths declared above.
function schemaPaths(schema) {
  const paths = new Set();
  const resolve = (ref) => {
    const m = /^#\/\$defs\/([^/]+)$/.exec(ref);
    assert.ok(m && schema.$defs[m[1]], `unresolvable $ref ${ref}`);
    return schema.$defs[m[1]];
  };
  function walk(node, prefix, refChain) {
    if (node === null || typeof node !== 'object') return; // true / false schemas
    assert.ok(prefix.split('.').length < 16, `schema walk too deep at ${prefix}`);
    if (typeof node.$ref === 'string' && !refChain.includes(node.$ref)) {
      walk(resolve(node.$ref), prefix, [...refChain, node.$ref]);
    }
    const child = (segment, sub) => {
      const p = prefix === '' ? segment : `${prefix}.${segment}`;
      paths.add(p);
      walk(sub, p, []);
    };
    for (const [k, sub] of Object.entries(node.properties ?? {})) child(k, sub);
    for (const sub of Object.values(node.patternProperties ?? {})) child('<key>', sub);
    if (node.additionalProperties !== null && typeof node.additionalProperties === 'object') {
      child('<Name>', node.additionalProperties);
    }
  }
  walk(schema, '', []);
  return paths;
}

test('CONTRACT.md documents every code and field', () => {
  const text = fs.readFileSync(path.join(CONTRACT_DIR, 'CONTRACT.md'), 'utf8');
  const { CODES } = require('../contract.js');
  const sections = markdownSections(text);
  const section = (heading) => {
    assert.ok(sections.has(heading), `missing heading: ${heading}`);
    return sections.get(heading);
  };

  // (d) the eight fixed headings, each exactly once (duplicates are caught in markdownSections).
  for (const h of CONTRACT_HEADINGS) section(h);

  // (a) the code table (section 7): one row per CODES key whose first cell is the code in backticks and which has
  // a cell holding the class exactly as CODES spells it; no row names a code that CODES lacks.
  const s7 = section(CONTRACT_HEADINGS[6]);
  const codeRows = new Map();
  for (const line of s7.filter((l) => l.startsWith('|'))) {
    const cells = tableCells(line);
    const m = /^`(E_[A-Z0-9_]+)`$/.exec(cells[0]);
    if (!m) continue;
    assert.equal(codeRows.has(m[1]), false, `code table names ${m[1]} twice`);
    codeRows.set(m[1], cells);
  }
  const missingCodes = [];
  for (const [code, cls] of Object.entries(CODES)) {
    const cells = codeRows.get(code);
    if (!cells) missingCodes.push(`${code}: no row`);
    else if (!cells.slice(1).some((c) => c === cls || c === `\`${cls}\``)) missingCodes.push(`${code}: class ${cls}`);
  }
  assert.deepEqual(missingCodes, []);
  assert.deepEqual(
    [...codeRows.keys()].filter((c) => !Object.hasOwn(CODES, c)),
    [],
    'the code table names codes that contract.js CODES does not define',
  );

  // (b) the exit-code list (section 7): one list item per exit code, with its meaning.
  const exitMeaning = { 0: /clean/i, 1: /abort/i, 2: /usage/i, 3: /degrad/i };
  for (const [n, meaning] of Object.entries(exitMeaning)) {
    const item = s7.find((l) => new RegExp(`^- \`${n}\`: \\S`).test(l));
    assert.ok(item, `exit code ${n} has no list item "- \`${n}\`: <meaning>"`);
    assert.match(item, meaning, `exit code ${n}`);
  }

  // (c) every property path of versions.v2.schema.json has a row in the C1 field table (section 2).
  const schema = JSON.parse(fs.readFileSync(path.join(CONTRACT_DIR, 'versions.v2.schema.json'), 'utf8'));
  const tablePaths = new Set(
    section(CONTRACT_HEADINGS[1])
      .filter((l) => l.startsWith('|'))
      .map((l) => /^`([^`]+)`$/.exec(tableCells(l)[0]))
      .filter(Boolean)
      .map((m) => m[1]),
  );
  const walked = schemaPaths(schema);
  for (const p of ['plugins.<Name>.version', 'plugins.<Name>.platforms.<key>.payload.sha256', 'manager.latest_json_url']) {
    assert.ok(walked.has(p), `schema walk lost ${p}`); // the walk itself has teeth
  }
  assert.deepEqual(
    [...walked].filter((p) => !tablePaths.has(p)),
    [],
    'schema paths without a C1 field-table row',
  );

  // (e) the statements FR-016, FR-018, FR-034, FR-014 / PD-13 and the Phase 8 obligations require.
  const s3 = section(CONTRACT_HEADINGS[2]).join('\n');
  assert.match(s3, /`E_INSTALL_DUPLICATE`/);
  assert.match(s3, /case[- ]fold/i);
  const s4 = section(CONTRACT_HEADINGS[3]).join('\n');
  assert.match(s4, /`min_os`[^\n]*departure|departure[^\n]*`min_os`/i, 'section 4 states the min_os departure');
  assert.match(s4, /FR-016/);
  assert.match(s4, /no `channel`/i, 'section 4 states that C2 has no channel');
  assert.match(s4, /FR-018/);
  const s6 = section(CONTRACT_HEADINGS[5]).join('\n');
  assert.match(s6, /Windows and macOS have no archive layout/i);
  assert.match(s6, /FR-034/);
  assert.match(s6, /Phase 8/);
  assert.match(s6, /realpath/);
  assert.match(s6, /std::fs::canonicalize/);
});

// --- SC-026 (FR-076, FR-074) and SC-021 clause 2: the Phase 2 tools' form and codes; no serial test option.

const PHASE2_TOOLS = ['minisign.js', 'release-facts.js', 'gen-release-manifest.js', 'backfill-sign.js'];
const FR074_CODES = [
  'E_MINISIG_PUBKEY',
  'E_MINISIG_SECRET_KEY',
  'E_MINISIG_PASSWORD',
  'E_MINISIG_KEY_ID',
  'E_MINISIG_SIGNATURE',
  'E_MINISIG_GLOBAL',
  'E_FACTS_NO_MACHO',
  'E_FACTS_ARCH',
  'E_FACTS_ARCH_MISMATCH',
  'E_FACTS_NO_MINOS',
  'E_FACTS_MACHO_CORRUPT',
  'E_FACTS_NO_GLIBC',
  'E_FACTS_LISTING_MISMATCH',
  'E_FACTS_LIPO_MISMATCH',
  'E_FACTS_STALE',
  'E_FACTS_MISSING',
  'E_ASSET_MISSING',
  'E_VERSION_MISMATCH',
  'E_SIGNING_CONFIG',
  'E_BACKFILL_LIST_TRUNCATED',
  'E_BACKFILL_ASSET_MISSING',
  'E_BACKFILL_IMMUTABLE',
  'E_BACKFILL_PARTIAL',
  'E_BACKFILL_SIZE',
  'E_BACKFILL_DIGEST',
  'E_BACKFILL_ASSETS_CHANGED',
  'E_SIGN_KEY_MISMATCH',
];

function jsFilesUnder(dir) {
  const out = [];
  for (const d of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, d.name);
    if (d.isDirectory()) out.push(...jsFilesUnder(full));
    else if (d.isFile() && d.name.endsWith('.js')) out.push(full);
  }
  return out;
}

test('Phase 2 tools follow the tools/*.js form', async (t) => {
  const { CODES } = require('../contract.js');
  for (const file of PHASE2_TOOLS) {
    await t.test(file, () => {
      const toolPath = path.join(TOOLS_DIR, file);
      const mod = require(toolPath);
      const src = fs.readFileSync(toolPath, 'utf8');
      assert.equal(src.split(/\r?\n/)[0], '#!/usr/bin/env node', `${file}: first line`);
      assert.equal(typeof mod.main, 'function', `${file}: exports main`);
      let stderr = '';
      const io = {
        stdout: { write: () => true },
        stderr: { write: (s) => ((stderr += s), true) },
        env: {},
      };
      assert.equal(mod.main([], io), 2, `${file}: main([], io) exit code; stderr ${JSON.stringify(stderr)}`);
      const usage = `usage: node tools/${file}`;
      assert.ok(
        stderr.split(/\r?\n/).some((l) => l.startsWith(usage)),
        `${file}: no stderr line starts "${usage}"; stderr ${JSON.stringify(stderr)}`,
      );
      const literals = [...src.matchAll(/'(E_[A-Z0-9_]+)'/g)].map((m) => m[1]);
      assert.deepEqual(
        [...new Set(literals.filter((c) => !Object.hasOwn(CODES, c)))],
        [],
        `${file}: code literals that contract.js CODES does not define`,
      );
    });
  }

  await t.test('the 27 FR-074 codes are CODES keys with class none', () => {
    assert.equal(new Set(FR074_CODES).size, 27);
    assert.deepEqual(
      FR074_CODES.filter((c) => CODES[c] !== 'none'),
      [],
      'FR-074 codes missing from CODES or with a class other than none',
    );
  });

  await t.test('SC-021: no test file sets a serial concurrency option', () => {
    // Assembled from pieces so this file's own source never matches it.
    const serialRe = new RegExp('concurren' + 'cy:\\s*(1|false)\\b');
    const files = jsFilesUnder(TESTS_DIR);
    assert.ok(files.length >= 10, `the scan reaches ${files.length} file(s), expected at least 10`);
    assert.deepEqual(
      files.filter((f) => serialRe.test(fs.readFileSync(f, 'utf8'))).map(rel),
      [],
    );
  });
});
