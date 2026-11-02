'use strict';
require('./helpers/write-guard.js');

// tools/feed-publish.js: the steps that turn tool exits into workflow decisions, and the guarantee that no test builds
// the real effects (spec specs/krate-manager-phase3/spec.md FR-009, FR-020, FR-023a, FR-028, FR-029a, FR-029b,
// FR-029c, FR-029e, SC-002 unit half, SC-017, SC-018; plan §4.1.1, §4.1.4, §4.1.5, §4.1.6, §4.1.7, PD-4, PD-5, PD-9,
// PD-12, PD-18, §7.1 T10, T11).
// Every output lands in an fs.mkdtemp dir; nothing spawns: the deps handed to main are stubs that throw when called,
// except that the generate subtests set genFeedMain to gen-feed's exported main (in-process) or a recording wrapper
// around it.

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const feedPublish = require('../../feed-publish.js');
const genFeed = require('../../gen-feed.js');

const TESTS_DIR = path.resolve(__dirname);
const BACKFILL = path.join(__dirname, '..', 'fixtures', 'phase2', 'backfill');
const MANAGER_URL = 'https://krateaudio.com/manager/latest.json';
const RESULTS = ['success', 'failure', 'cancelled', 'skipped'];
const FLAG_VALUES = ['true', 'false', ''];
const REFS = ['refs/heads/main', 'refs/heads/other'];
const EVIDENCE_FILES = ['releases.json', 'collect-report.json', 'gen-feed.log', 'live-versions.json', 'compare.txt'];
// The fixed annotation titles of plan §4.1.6: the only lines the tool may start with "::".
const ANNOTATION_RE =
  /^::(notice|warning|error) title=(feed-collect|feed-generate|feed-degrade|feed-validate|feed-compare|feed-drift|feed-verdict)::/u;

function withTemp(t, name) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `km3-${name}-`));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

/** Stub effects: any use of them is a test failure (outcome and verdict need neither). */
const stubDeps = Object.freeze({
  gh: () => {
    throw new Error('stub gh called');
  },
  genFeedMain: () => {
    throw new Error('stub genFeedMain called');
  },
});

const linesOf = (text) => (text === '' ? [] : text.replace(/\n$/u, '').split('\n'));

/**
 * Runs main with captured output and checks the output rule on every call (PD-18 (c), FR-029e): no stdout or stderr
 * line starts with "::" unless it is an annotation with a fixed title on stdout, and no line holds a CR.
 */
function run(argv, deps = stubDeps) {
  let stdout = '';
  let stderr = '';
  const io = {
    stdout: { write: (s) => ((stdout += s), true) },
    stderr: { write: (s) => ((stderr += s), true) },
  };
  const code = feedPublish.main(argv, io, deps);
  const out = linesOf(stdout);
  const err = linesOf(stderr);
  for (const l of out) {
    assert.ok(!l.startsWith('::') || ANNOTATION_RE.test(l), `stdout line starts "::" without a fixed title: ${l}`);
    assert.ok(!l.includes('\r'), `stdout line holds a CR: ${JSON.stringify(l)}`);
  }
  for (const l of err) {
    assert.ok(!l.startsWith('::'), `stderr line starts "::": ${l}`);
    assert.ok(!l.includes('\r'), `stderr line holds a CR: ${JSON.stringify(l)}`);
  }
  return { code, stdout, stderr, out, err };
}

// --- static scan helpers (SC-017 (b)); every token is assembled from pieces so this file's source does not match.

const MODULE_FILE = 'feed-' + 'publish\\.js';
const PRODUCTION_TOKEN = 'production' + 'Deps(';
const BIND_RE = new RegExp(
  '\\b(?:const|let|var)\\s+([A-Za-z_$][\\w$]*)\\s*=\\s*require\\s*\\(\\s*[\'"][^\'"]*' + MODULE_FILE + '[\'"]\\s*\\)',
  'gu',
);
const DESTRUCTURE_RE = new RegExp(
  '\\b(?:const|let|var)\\s*\\{([^}]*)\\}\\s*=\\s*require\\s*\\(\\s*[\'"][^\'"]*' + MODULE_FILE + '[\'"]\\s*\\)',
  'gu',
);
const INLINE_RE = new RegExp('require\\s*\\(\\s*[\'"][^\'"]*' + MODULE_FILE + '[\'"]\\s*\\)\\s*\\.\\s*main\\s*\\(', 'gu');
const escapeRe = (s) => s.replace(/[$]/gu, '\\$&');

/** Number of arguments of the call whose "(" is at src[open]: top-level commas + 1 (a trailing comma not counted). */
function argumentCount(src, open) {
  let depth = 0;
  let commas = 0;
  let lastComma = -1;
  for (let i = open; i < src.length; i += 1) {
    const c = src[i];
    if (c === "'" || c === '"' || c === '`') {
      for (i += 1; i < src.length && src[i] !== c; i += 1) if (src[i] === '\\') i += 1;
      continue;
    }
    if (c === '/' && src[i + 1] === '/') {
      while (i < src.length && src[i] !== '\n') i += 1;
      continue;
    }
    if (c === '/' && src[i + 1] === '*') {
      i = src.indexOf('*/', i + 2) + 1;
      continue;
    }
    if (c === '(' || c === '[' || c === '{') depth += 1;
    else if (c === ')' || c === ']' || c === '}') {
      depth -= 1;
      if (depth === 0) {
        const inner = src.slice(open + 1, i);
        if (inner.trim() === '') return 0;
        const tail = lastComma >= 0 ? src.slice(lastComma + 1, i) : inner;
        return commas + 1 - (lastComma >= 0 && tail.trim() === '' ? 1 : 0);
      }
    } else if (c === ',' && depth === 1) {
      commas += 1;
      lastComma = i;
    }
  }
  throw new Error(`unbalanced call at offset ${open}`);
}

/** Every main call on the feed-publish.js module object in src: [{at, args}]. */
function mainCalls(src) {
  const callRes = [];
  for (const m of src.matchAll(BIND_RE)) callRes.push(new RegExp('\\b' + escapeRe(m[1]) + '\\s*\\.\\s*main\\s*\\(', 'gu'));
  for (const m of src.matchAll(DESTRUCTURE_RE)) {
    for (const part of m[1].split(',')) {
      const bind = /^\s*main\s*(?::\s*([A-Za-z_$][\w$]*))?\s*$/u.exec(part);
      if (bind) callRes.push(new RegExp('(?<![.\\w$])' + escapeRe(bind[1] || 'main') + '\\s*\\(', 'gu'));
    }
  }
  callRes.push(INLINE_RE);
  const calls = [];
  for (const re of callRes) {
    for (const m of src.matchAll(re)) {
      const open = m.index + m[0].length - 1;
      calls.push({ at: src.slice(0, open).split('\n').length, args: argumentCount(src, open) });
    }
  }
  return calls;
}

test('feed-publish tests never build real effects', async (t) => {
  await t.test('(a) main without deps, or with null deps, throws a TypeError naming deps and writes nothing', (s) => {
    const tmp = withTemp(s, 'nodeps');
    const out = path.join(tmp, 'github-output');
    fs.writeFileSync(out, 'prior=1\n');
    const argv = ['outcome', '--tool', 'collect', '--exit', '0', '--github-output', out];
    let written = '';
    const io = { stdout: { write: (x) => ((written += x), true) }, stderr: { write: (x) => ((written += x), true) } };
    const namesDeps = (err) => err instanceof TypeError && /\bdeps\b/u.test(err.message);
    // The deliberate two-argument call goes through Reflect.apply, so the three-argument rule of (b) stays absolute.
    assert.throws(() => Reflect.apply(feedPublish.main, undefined, [argv, io]), namesDeps);
    assert.throws(() => feedPublish.main(argv, io, null), namesDeps);
    // Before argv is read: an argv whose every access throws still yields the deps TypeError.
    const poisoned = new Proxy([], {
      get() {
        throw new Error('argv was read');
      },
    });
    assert.throws(() => feedPublish.main(poisoned, io, null), namesDeps);
    assert.equal(fs.readFileSync(out, 'utf8'), 'prior=1\n', 'the --github-output file is untouched');
    assert.equal(written, '', 'nothing written to io');
  });

  await t.test('(b) the Phase 3 test files never construct production deps and call main with three arguments', () => {
    const files = [
      'feed-publish-collect.test.js',
      'feed-publish-compare.test.js',
      'feed-publish-steps.test.js',
      'feed-pipeline.test.js',
      'phase3-fixtures.test.js',
      path.join('helpers', 'fake-gh.js'),
    ];
    const missing = files.filter((f) => !fs.existsSync(path.join(TESTS_DIR, f)));
    assert.deepEqual(missing, [], 'Phase 3 test files missing');
    const productionHits = [];
    const badCalls = [];
    let total = 0;
    for (const f of files) {
      const src = fs.readFileSync(path.join(TESTS_DIR, f), 'utf8');
      const count = src.split(PRODUCTION_TOKEN).length - 1;
      if (count > 0) productionHits.push(`${f}: ${count}`);
      for (const call of mainCalls(src)) {
        total += 1;
        if (call.args !== 3) badCalls.push(`${f}:${call.at}: ${call.args} argument(s)`);
      }
    }
    assert.deepEqual(productionHits, [], `files holding the ${PRODUCTION_TOKEN} token`);
    assert.ok(total > 0, 'the scan found no main call on the feed-publish.js module object');
    assert.deepEqual(
      badCalls,
      [],
      'rule: every main( / .main( call on the feed-publish.js module object passes exactly three arguments ' +
        '(top-level commas + 1)',
    );
  });
});

// --- SC-018

/** The FR-029c oracle, written from the spec text (not from the tool): green only in these two shapes. */
function oracleExit({ dryRun, ref, build, deploy, flags, evidenceComplete }) {
  const noFlag = flags.every((f) => f !== 'true');
  const dryGreen = dryRun && build === 'success' && (deploy === 'success' || deploy === 'skipped');
  const realGreen = !dryRun && ref === 'refs/heads/main' && build === 'success' && deploy === 'success';
  return (dryGreen || realGreen) && noFlag && evidenceComplete ? 0 : 1;
}

function writeEvidence(dir, overrides = {}) {
  fs.mkdirSync(dir, { recursive: true });
  const files = {
    'releases.json': '[]\n',
    'collect-report.json':
      JSON.stringify({ plugins: [], manager: { status: 'degraded', tag: 'manager/v0.1.0', message: 'version 9 not 1' } }) +
      '\n',
    'gen-feed.log': 'gen-feed: WARN E_TC_NAME: alpha one\nsomething else\ngen-feed: WARN E_LAYOUT: bravo two\n',
    'live-versions.json': '{"plugins":{}}\n',
    'compare.txt': 'drift Alpha download_url changed without a newer release warn\nsame Bravo 1.0.0\n',
    ...overrides,
  };
  for (const [name, text] of Object.entries(files)) if (text !== null) fs.writeFileSync(path.join(dir, name), text);
  return dir;
}

function verdictArgv({ dryRun, ref, build, deploy, flags, evidence, annotate }) {
  const argv = ['verdict', '--dry-run', String(dryRun), '--ref', ref, '--build', build, '--deploy', deploy];
  argv.push('--feed-degraded', flags[0], '--manager-degraded', flags[1], '--drift-warned', flags[2]);
  if (evidence !== undefined) argv.push('--evidence', evidence);
  if (annotate) argv.push('--annotate');
  return argv;
}

const greenReal = { dryRun: false, ref: 'refs/heads/main', build: 'success', deploy: 'success', flags: ['', '', ''] };

// --- generate (FR-029a, PD-9): an in-dir as collect leaves it, written by the test itself

/**
 * The Phase 2 backfill world as generate's input: the plugins of fixtures/phase2/backfill/plugins/ copied into the temp
 * dir (each version.json given the https `url` gen-feed's v2 mode requires as the v1 download_url, which the Phase 2
 * copies lack), releases.json = the state.json rows in the `gh release list` field set, an empty manifests/, and a
 * collect-report.json with `manager`; `latestJson` (a Buffer) becomes <in-dir>/manager/latest.json. The site dir is
 * two levels below a directory that does not exist yet.
 */
function generateWorld(t, name, { manager = { status: 'none' }, latestJson } = {}) {
  const tmp = withTemp(t, name);
  const state = JSON.parse(fs.readFileSync(path.join(BACKFILL, 'state.json'), 'utf8'));
  const pluginsDir = path.join(tmp, 'plugins');
  fs.cpSync(path.join(BACKFILL, 'plugins'), pluginsDir, { recursive: true });
  for (const dir of fs.readdirSync(pluginsDir)) {
    const file = path.join(pluginsDir, dir, 'version.json');
    const doc = JSON.parse(fs.readFileSync(file, 'utf8'));
    doc.url = `https://krateaudio.com/${doc.name.toLowerCase()}/`;
    fs.writeFileSync(file, JSON.stringify(doc, null, 2) + '\n');
  }
  const inDir = path.join(tmp, 'in');
  fs.mkdirSync(path.join(inDir, 'manifests'), { recursive: true });
  const rows = state.releases.map(({ tagName, isPrerelease, isDraft, publishedAt }) => ({
    tagName,
    isPrerelease,
    isDraft,
    publishedAt,
  }));
  fs.writeFileSync(path.join(inDir, 'releases.json'), JSON.stringify(rows, null, 2) + '\n');
  const report = { plugins: [], manager, gh_calls: 1 };
  fs.writeFileSync(path.join(inDir, 'collect-report.json'), JSON.stringify(report, null, 2) + '\n');
  if (latestJson !== undefined) {
    fs.mkdirSync(path.join(inDir, 'manager'));
    fs.writeFileSync(path.join(inDir, 'manager', 'latest.json'), latestJson);
  }
  return { tmp, inDir, pluginsDir, repo: state.repo, siteDir: path.join(tmp, 'out', '_site') };
}

function generateArgv(w, annotate = false) {
  const argv = ['generate', '--in-dir', w.inDir, '--plugins-dir', w.pluginsDir, '--repo', w.repo, '--site-dir', w.siteDir];
  if (annotate) argv.push('--annotate');
  return argv;
}

/** gen-feed's argv as plan §4.1.4 step 3 builds it, for run 1 (`out` = site) or run 2 (`out` = versions.again.json). */
function genFeedArgv(w, { copied, out }) {
  const argv = ['--plugins-dir', w.pluginsDir, '--releases', path.join(w.inDir, 'releases.json')];
  argv.push('--manifests', path.join(w.inDir, 'manifests'), '--repo', w.repo);
  if (copied) argv.push('--manager-latest-url', MANAGER_URL);
  return [...argv, '--out', out];
}

const outOf = (argv) => argv[argv.indexOf('--out') + 1];

/**
 * deps whose genFeedMain records every call ({argv, code, stderr}) and runs `behave(n, argv, io)` (n counts from 1);
 * by default the real gen-feed main, in-process. gh is the throwing stub: generate never calls it.
 */
function recordingGenFeed(behave = (n, argv, io) => genFeed.main(argv, io)) {
  const calls = [];
  const genFeedMain = (argv, io) => {
    let stderr = '';
    const tee = { stdout: io.stdout, stderr: { write: (s) => ((stderr += s), io.stderr.write(s)) } };
    const code = behave(calls.length + 1, argv, tee);
    calls.push({ argv: [...argv], code, stderr });
    return code;
  };
  return { calls, deps: { gh: stubDeps.gh, genFeedMain } };
}

const copiedManager = { status: 'copied', tag: 'manager/v0.1.0' };
// Bytes generate must copy as they are (a CRLF and no trailing newline normalisation).
const LATEST_BYTES = Buffer.from('{"version": "0.1.0", "platforms": {}}\r\n', 'utf8');

test('feed-publish decides every workflow branch', async (t) => {
  await t.test('(1) outcome appends the flag for 0 and 3 and fails the step otherwise', (s) => {
    const tmp = withTemp(s, 'outcome');
    const flags = { collect: 'manager_degraded', generate: 'feed_degraded', compare: 'drift_warned' };
    for (const [tool, flag] of Object.entries(flags)) {
      for (const [exit, value] of [
        ['0', 'false'],
        ['3', 'true'],
      ]) {
        const file = path.join(tmp, `${tool}-${exit}`);
        fs.writeFileSync(file, 'prior=1\n');
        const r = run(['outcome', '--tool', tool, '--exit', exit, '--github-output', file]);
        assert.equal(r.code, 0, `${tool} --exit ${exit}: ${r.stderr}`);
        assert.equal(fs.readFileSync(file, 'utf8'), `prior=1\n${flag}=${value}\n`, `${tool} --exit ${exit}`);
        assert.equal(r.stdout + r.stderr, '');
      }
      for (const exit of ['1', '2', '127']) {
        const file = path.join(tmp, `${tool}-${exit}`);
        fs.writeFileSync(file, 'prior=1\n');
        const r = run(['outcome', '--tool', tool, '--exit', exit, '--github-output', file]);
        assert.equal(r.code, 1, `${tool} --exit ${exit}`);
        assert.equal(fs.readFileSync(file, 'utf8'), 'prior=1\n', `${tool} --exit ${exit}: nothing appended`);
        assert.equal(r.stderr, `feed-publish: E_STEP_FAILED: ${tool} exited ${exit}\n`);
        assert.equal(r.stdout, '');
      }
      const file = path.join(tmp, `${tool}-x`);
      fs.writeFileSync(file, 'prior=1\n');
      const r = run(['outcome', '--tool', tool, '--exit', 'x', '--github-output', file]);
      assert.equal(r.code, 2, `${tool} --exit x`);
      assert.equal(fs.readFileSync(file, 'utf8'), 'prior=1\n');
      assert.ok(r.err.some((l) => l.startsWith('usage: node tools/feed-publish.js')), r.stderr);
    }
  });

  // The dimensions the task lists (dry run 2 x ref 2 x build 4 x deploy 4 x flags 3^3 x evidence 2) multiply to 3,456;
  // tasks.md and plan §7.1 print 1,728, which leaves out the evidence factor. Every listed dimension is kept.
  await t.test('(2) verdict: the full 3,456-row table against the FR-029c oracle', (s) => {
    const tmp = withTemp(s, 'verdict-table');
    const complete = writeEvidence(path.join(tmp, 'complete'));
    const partial = writeEvidence(path.join(tmp, 'partial'), { 'compare.txt': null });
    let rows = 0;
    const wrong = [];
    for (const dryRun of [true, false]) {
      for (const ref of REFS) {
        for (const build of RESULTS) {
          for (const deploy of RESULTS) {
            for (const f0 of FLAG_VALUES) {
              for (const f1 of FLAG_VALUES) {
                for (const f2 of FLAG_VALUES) {
                  for (const evidenceComplete of [true, false]) {
                    rows += 1;
                    const flags = [f0, f1, f2];
                    const row = { dryRun, ref, build, deploy, flags, evidenceComplete };
                    const r = run(verdictArgv({ ...row, evidence: evidenceComplete ? complete : partial, annotate: true }));
                    const expected = oracleExit(row);
                    const label = JSON.stringify(row);
                    if (r.code !== expected) wrong.push(`${label}: exit ${r.code}, oracle ${expected}`);
                    assert.equal(r.stderr, '', label);
                    const red = r.out.filter((l) => l.startsWith('verdict: red '));
                    const errors = r.out.filter((l) => l.startsWith('::error title=feed-verdict::'));
                    if (expected === 0) {
                      assert.deepEqual(r.out, ['verdict: green', '::notice title=feed-verdict::green'], label);
                    } else {
                      assert.ok(red.length > 0, `${label}: no "verdict: red" line`);
                      assert.equal(errors.length, red.length, `${label}: one ::error annotation per reason`);
                      assert.ok(!r.out.includes('verdict: green'), label);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    assert.equal(rows, 2 * 2 * 4 * 4 * 27 * 2);
    assert.deepEqual(wrong, []);
  });

  await t.test('(3) verdict reason text', (s) => {
    const tmp = withTemp(s, 'verdict-text');
    const ev = writeEvidence(path.join(tmp, 'ev'));
    const realOther = { ...greenReal, ref: 'refs/heads/other', deploy: 'skipped', evidence: ev };

    let r = run(verdictArgv(realOther));
    assert.equal(r.code, 1);
    assert.equal(r.stdout, 'verdict: red real run from refs/heads/other\n');
    r = run(verdictArgv({ ...realOther, annotate: true }));
    assert.deepEqual(r.out, [
      'verdict: red real run from refs/heads/other',
      '::error title=feed-verdict::real run from refs/heads/other',
    ]);

    r = run(verdictArgv({ ...greenReal, flags: ['', '', 'true'], evidence: ev, annotate: true }));
    assert.equal(r.code, 1);
    assert.deepEqual(r.out, [
      'verdict: red v1 drift deployed: drift Alpha download_url changed without a newer release warn',
      '::error title=feed-verdict::v1 drift deployed: drift Alpha download_url changed without a newer release warn',
    ]);

    r = run(verdictArgv({ ...greenReal, flags: ['true', '', ''], evidence: ev, annotate: true }));
    assert.equal(r.code, 1);
    assert.deepEqual(r.out, [
      'verdict: red feed degraded:',
      'gen-feed: WARN E_TC_NAME: alpha one',
      'gen-feed: WARN E_LAYOUT: bravo two',
      '::error title=feed-verdict::feed degraded: gen-feed: WARN E_TC_NAME: alpha one gen-feed: WARN E_LAYOUT: bravo two',
    ]);

    r = run(verdictArgv({ ...greenReal, flags: ['', 'true', ''], evidence: ev, annotate: true }));
    assert.equal(r.code, 1);
    assert.deepEqual(r.out, [
      'verdict: red manager latest.json degraded: version 9 not 1',
      '::error title=feed-verdict::manager latest.json degraded: version 9 not 1',
    ]);

    const gaps = writeEvidence(path.join(tmp, 'gaps'), { 'releases.json': null, 'compare.txt': null });
    r = run(verdictArgv({ ...greenReal, evidence: gaps, annotate: true }));
    assert.equal(r.code, 1);
    assert.deepEqual(r.out, [
      'verdict: red evidence missing releases.json',
      'verdict: red evidence missing compare.txt',
      '::error title=feed-verdict::evidence missing releases.json',
      '::error title=feed-verdict::evidence missing compare.txt',
    ]);

    // Reason order (PD-12): build, ref, deploy, feed, manager, drift, evidence.
    r = run(verdictArgv({ ...greenReal, deploy: 'failure', flags: ['true', 'true', 'true'], evidence: gaps }));
    assert.deepEqual(
      r.out.filter((l) => l.startsWith('verdict: red ')),
      [
        'verdict: red deploy failure',
        'verdict: red feed degraded:',
        'verdict: red manager latest.json degraded: version 9 not 1',
        'verdict: red v1 drift deployed: (unreadable)',
        'verdict: red evidence missing releases.json',
        'verdict: red evidence missing compare.txt',
      ],
    );
    r = run(verdictArgv({ ...greenReal, ref: 'refs/heads/other', build: 'failure', deploy: 'skipped', evidence: gaps }));
    assert.deepEqual(r.out, ['verdict: red build failure', 'verdict: red real run from refs/heads/other']);
    r = run(verdictArgv({ ...greenReal, dryRun: true, deploy: 'cancelled', evidence: ev }));
    assert.deepEqual(r.out, ['verdict: red deploy cancelled']);

    r = run(verdictArgv({ ...greenReal, evidence: ev }));
    assert.equal(r.code, 0);
    assert.equal(r.stdout, 'verdict: green\n');
    r = run(verdictArgv({ ...greenReal, evidence: ev, annotate: true }));
    assert.equal(r.stdout, 'verdict: green\n::notice title=feed-verdict::green\n');
    r = run(verdictArgv({ ...greenReal, dryRun: true, deploy: 'skipped', ref: 'refs/heads/other', evidence: ev }));
    assert.equal(r.code, 0);
    assert.equal(r.stdout, 'verdict: green\n');
  });

  await t.test('(4) usage errors return 2 with the usage block', (s) => {
    const ev = writeEvidence(path.join(withTemp(s, 'verdict-usage'), 'ev'));
    const base = verdictArgv({ ...greenReal, evidence: ev });
    const swap = (flag, value) => {
      const argv = [...base];
      argv[argv.indexOf(flag) + 1] = value;
      return argv;
    };
    const cases = {
      'flag value maybe': swap('--feed-degraded', 'maybe'),
      'drift flag TRUE': swap('--drift-warned', 'TRUE'),
      'build result done': swap('--build', 'done'),
      'deploy result empty': swap('--deploy', ''),
      'dry-run maybe': swap('--dry-run', 'maybe'),
      'ref empty': swap('--ref', ''),
      'ref missing': base.filter((a, i) => a !== '--ref' && base[i - 1] !== '--ref'),
      'manager flag missing': base.filter((a, i) => a !== '--manager-degraded' && base[i - 1] !== '--manager-degraded'),
      'flag twice': [...base, '--build', 'success'],
      'unknown flag': [...base, '--deploy-all'],
      'no subcommand': [],
      'unknown subcommand': ['deploy'],
    };
    for (const [label, argv] of Object.entries(cases)) {
      const r = run(argv);
      assert.equal(r.code, 2, `${label}: ${r.stderr}`);
      assert.match(r.err[0], /^feed-publish: E_USAGE: /u, label);
      assert.ok(r.err.some((l) => l.startsWith('usage: node tools/feed-publish.js')), `${label}: ${r.stderr}`);
      assert.equal(r.stdout, '', label);
    }
  });

  await t.test('(5) PD-18 (c): evidence lines that hold a CR, start "::" or hold a LF print in JSON form', (s) => {
    const tmp = withTemp(s, 'verdict-pd18');
    const forged = '::notice title=feed-verdict::green';
    const message = 'line one\n::stop-commands::x';
    const ev = writeEvidence(path.join(tmp, 'ev'), {
      'gen-feed.log': `gen-feed: WARN E_TC_NAME: a\rb\n${forged}\ngen-feed: WARN E_LAYOUT: plain\n`,
      'collect-report.json': JSON.stringify({ manager: { status: 'degraded', message } }) + '\n',
      'compare.txt': `drift A\rB download_url changed without a newer release warn\n${forged}\n`,
    });
    const r = run(verdictArgv({ ...greenReal, flags: ['true', 'true', 'true'], evidence: ev, annotate: true }));
    assert.equal(r.code, 1);
    const crLine = JSON.stringify('gen-feed: WARN E_TC_NAME: a\rb');
    const driftLine = JSON.stringify('drift A\rB download_url changed without a newer release warn');
    assert.deepEqual(r.out, [
      'verdict: red feed degraded:',
      crLine,
      'gen-feed: WARN E_LAYOUT: plain',
      `verdict: red manager latest.json degraded: ${JSON.stringify(message)}`,
      `verdict: red v1 drift deployed: ${driftLine}`,
      `::error title=feed-verdict::feed degraded: ${crLine} gen-feed: WARN E_LAYOUT: plain`,
      `::error title=feed-verdict::manager latest.json degraded: ${JSON.stringify(message)}`,
      `::error title=feed-verdict::v1 drift deployed: ${driftLine}`,
    ]);
    // A message that itself starts "::" is quoted too.
    const ev2 = writeEvidence(path.join(tmp, 'ev2'), {
      'collect-report.json': JSON.stringify({ manager: { status: 'degraded', message: forged } }) + '\n',
    });
    const r2 = run(verdictArgv({ ...greenReal, flags: ['', 'true', ''], evidence: ev2 }));
    assert.deepEqual(r2.out, [`verdict: red manager latest.json degraded: ${JSON.stringify(forged)}`]);
    // A ref holding a LF cannot start a line of its own.
    const r3 = run(verdictArgv({ ...greenReal, ref: `refs/heads/x\n${forged}`, deploy: 'skipped', evidence: ev2 }));
    assert.equal(r3.code, 1);
    assert.equal(r3.out.length, 1);
  });

  await t.test('(6) withGhRetry: 3 attempts, 5 s then 15 s, logged, partial download removed', (s) => {
    const tmp = withTemp(s, 'retry');
    const listArgv = ['release', 'list', '--repo', 'o/r', '--limit', '1000', '--json', 'tagName,isPrerelease,isDraft,publishedAt'];
    const stub = (statuses, onCall = () => {}) => {
      const calls = [];
      const gh = (argv) => {
        const status = statuses[calls.length];
        calls.push(argv);
        onCall(calls.length, argv);
        return { status, stdout: status === 0 ? '[]' : '', stderr: status === 0 ? '' : 'boom' };
      };
      return { gh, calls };
    };
    const recorder = () => {
      const sleeps = [];
      const logs = [];
      return { sleeps, logs, sleep: (ms) => sleeps.push(ms), log: (line) => logs.push(line) };
    };

    let rec = recorder();
    let st = stub([1, 0]);
    let res = feedPublish.withGhRetry(st.gh, rec)(listArgv);
    assert.equal(res.status, 0);
    assert.equal(res.attempts, 2);
    assert.equal(res.stdout, '[]');
    assert.deepEqual(rec.sleeps, [5000]);
    assert.deepEqual(rec.logs, ['feed-publish: WARN gh attempt 1/3 failed: list (exit 1); retrying in 5 s']);
    assert.deepEqual(st.calls, [listArgv, listArgv]);

    rec = recorder();
    st = stub([1, 1, 0]);
    res = feedPublish.withGhRetry(st.gh, rec)(listArgv);
    assert.equal(res.attempts, 3);
    assert.equal(res.status, 0);
    assert.deepEqual(rec.sleeps, [5000, 15000]);
    assert.deepEqual(rec.logs, [
      'feed-publish: WARN gh attempt 1/3 failed: list (exit 1); retrying in 5 s',
      'feed-publish: WARN gh attempt 2/3 failed: list (exit 1); retrying in 15 s',
    ]);

    rec = recorder();
    st = stub([1, 1, 1, 0]);
    res = feedPublish.withGhRetry(st.gh, rec)(listArgv);
    assert.equal(res.status, 1);
    assert.equal(res.attempts, 3);
    assert.equal(st.calls.length, 3);
    assert.deepEqual(rec.sleeps, [5000, 15000]);
    assert.equal(rec.logs.length, 2);

    rec = recorder();
    st = stub([0]);
    res = feedPublish.withGhRetry(st.gh, rec)(listArgv);
    assert.equal(res.attempts, 1);
    assert.deepEqual(rec.sleeps, []);
    assert.deepEqual(rec.logs, []);

    rec = recorder();
    st = stub([2, 0]);
    const viewArgv = ['release', 'view', 'alphaplug/v1.2.0', '--repo', 'o/r', '--json', 'tagName,assets'];
    feedPublish.withGhRetry(st.gh, rec)(viewArgv);
    assert.deepEqual(rec.logs, ['feed-publish: WARN gh attempt 1/3 failed: view alphaplug/v1.2.0 (exit 2); retrying in 5 s']);

    const outFile = path.join(tmp, 'alphaplug-v1.2.0.json');
    const dlArgv = ['release', 'download', 'alphaplug/v1.2.0', '--repo', 'o/r', '--pattern', 'release.json', '--output', outFile];
    const seen = [];
    rec = recorder();
    st = stub([1, 0], (n) => {
      seen.push(fs.existsSync(outFile));
      if (n === 1) fs.writeFileSync(outFile, 'partial');
      else fs.writeFileSync(outFile, 'whole');
    });
    res = feedPublish.withGhRetry(st.gh, rec)(dlArgv);
    assert.equal(res.attempts, 2);
    assert.deepEqual(seen, [false, false], 'the partial file is gone when the second attempt starts');
    assert.equal(fs.readFileSync(outFile, 'utf8'), 'whole');
    assert.deepEqual(rec.logs, [
      'feed-publish: WARN gh attempt 1/3 failed: download alphaplug/v1.2.0 (exit 1); retrying in 5 s',
    ]);

    // A gh that failed to run (production maps a spawn error to status -1) is retried like a non-zero exit.
    rec = recorder();
    st = stub([-1, 0]);
    res = feedPublish.withGhRetry(st.gh, rec)(listArgv);
    assert.equal(res.attempts, 2);
    assert.deepEqual(rec.logs, ['feed-publish: WARN gh attempt 1/3 failed: list (exit -1); retrying in 5 s']);

    assert.equal(feedPublish.GH_ATTEMPTS, 3);
    assert.deepEqual(feedPublish.GH_BACKOFF_MS, [5000, 15000]);
  });

  await t.test('(7) generate creates a missing --site-dir, logs gen-feed stderr and runs gen-feed twice', (s) => {
    const w = generateWorld(s, 'gen-site');
    assert.equal(fs.existsSync(path.dirname(w.siteDir)), false, 'precondition: the site dir does not exist');
    const rec = recordingGenFeed();
    const r = run(generateArgv(w), rec.deps);
    assert.equal(r.code, 0, r.stderr);
    assert.equal(r.stdout, '');
    assert.equal(r.stderr, '');
    const feedFile = path.join(w.siteDir, 'versions.json');
    const againFile = path.join(w.inDir, 'versions.again.json');
    const feed = fs.readFileSync(feedFile);
    const doc = JSON.parse(feed.toString('utf8'));
    assert.equal(doc.schema_version, 2);
    assert.ok(Object.keys(doc.plugins).length > 0, 'the backfill world selects stable releases');
    assert.equal(Object.hasOwn(doc, 'manager'), false, 'no manager block without a copied latest.json');
    assert.deepEqual(
      rec.calls.map((c) => c.argv),
      [genFeedArgv(w, { copied: false, out: feedFile }), genFeedArgv(w, { copied: false, out: againFile })],
    );
    assert.deepEqual(rec.calls.map((c) => c.code), [0, 0]);
    assert.equal(fs.readFileSync(path.join(w.inDir, 'gen-feed.log'), 'utf8'), rec.calls[0].stderr);
    assert.ok(fs.readFileSync(againFile).equals(feed), 'versions.again.json Buffer-equals versions.json');
    assert.deepEqual(fs.readdirSync(w.siteDir), ['versions.json'], 'nothing else in the site dir');
  });

  await t.test('(8) generate passes gen-feed 1 and 2 through before a second run, and 3 after it', (s) => {
    for (const code of [1, 2]) {
      const w = generateWorld(s, `gen-exit${code}`, { manager: copiedManager, latestJson: LATEST_BYTES });
      const rec = recordingGenFeed(() => code); // writes nothing, returns the code
      const r = run(generateArgv(w), rec.deps);
      assert.equal(r.code, code, `gen-feed ${code}: ${r.stderr}`);
      assert.equal(rec.calls.length, 1, `gen-feed ${code}: no second run`);
      assert.equal(fs.existsSync(path.join(w.siteDir, 'manager')), false, `gen-feed ${code}: no <site-dir>/manager`);
      assert.equal(fs.existsSync(path.join(w.siteDir, 'versions.json')), false);
      assert.equal(fs.existsSync(path.join(w.inDir, 'versions.again.json')), false);
    }

    const warn = 'gen-feed: WARN E_LAYOUT: plugin Alphaplug: a fixture degrade';
    const w = generateWorld(s, 'gen-exit3');
    const rec = recordingGenFeed((n, argv, io) => {
      assert.equal(genFeed.main(argv, io), 0);
      io.stderr.write(`${warn}\n`);
      return 3;
    });
    const r = run(generateArgv(w), rec.deps);
    assert.equal(r.code, 3, r.stderr);
    assert.equal(rec.calls.length, 2);
    assert.ok(fs.existsSync(path.join(w.siteDir, 'versions.json')), 'versions.json written');
    assert.equal(fs.readFileSync(path.join(w.inDir, 'gen-feed.log'), 'utf8'), `${warn}\n`);
    assert.deepEqual(r.err, [warn], 'the WARN line echoed to stderr once (the first run)');
    assert.equal(r.stdout, '');
  });

  await t.test('(9) generate passes --manager-latest-url and stages latest.json exactly when the report says copied', (s) => {
    const w = generateWorld(s, 'gen-copied', { manager: copiedManager, latestJson: LATEST_BYTES });
    const rec = recordingGenFeed();
    const r = run(generateArgv(w), rec.deps);
    assert.equal(r.code, 0, r.stderr);
    const feedFile = path.join(w.siteDir, 'versions.json');
    const againFile = path.join(w.inDir, 'versions.again.json');
    assert.deepEqual(
      rec.calls.map((c) => c.argv),
      [genFeedArgv(w, { copied: true, out: feedFile }), genFeedArgv(w, { copied: true, out: againFile })],
    );
    for (const call of rec.calls) {
      const i = call.argv.indexOf('--manager-latest-url');
      assert.deepEqual(call.argv.slice(i, i + 2), ['--manager-latest-url', MANAGER_URL]);
    }
    assert.equal(feedPublish.MANAGER_LATEST_URL, MANAGER_URL);
    assert.deepEqual(fs.readdirSync(path.join(w.siteDir, 'manager')), ['latest.json']);
    const staged = fs.readFileSync(path.join(w.siteDir, 'manager', 'latest.json'));
    assert.ok(staged.equals(LATEST_BYTES), '<site-dir>/manager/latest.json Buffer-equals the in-dir file');
    assert.equal(JSON.parse(fs.readFileSync(feedFile, 'utf8')).manager.latest_json_url, MANAGER_URL);

    // "none" and "degraded": the status decides, even with a latest.json lying in the in-dir.
    for (const manager of [{ status: 'none' }, { status: 'degraded', tag: 'manager/v0.1.0', message: 'no platforms' }]) {
      const v = generateWorld(s, `gen-${manager.status}`, { manager, latestJson: LATEST_BYTES });
      const rv = recordingGenFeed();
      const res = run(generateArgv(v), rv.deps);
      assert.equal(res.code, 0, `${manager.status}: ${res.stderr}`);
      assert.equal(rv.calls.length, 2);
      for (const call of rv.calls) assert.ok(!call.argv.includes('--manager-latest-url'), manager.status);
      assert.equal(fs.existsSync(path.join(v.siteDir, 'manager')), false, `${manager.status}: no <site-dir>/manager`);
      const doc = JSON.parse(fs.readFileSync(path.join(v.siteDir, 'versions.json'), 'utf8'));
      assert.equal(Object.hasOwn(doc, 'manager'), false, `${manager.status}: no manager block`);
    }
  });

  await t.test('(10) generate refuses an existing <site-dir>/manager untouched, before gen-feed runs', (s) => {
    const w = generateWorld(s, 'gen-sentinel', { manager: copiedManager, latestJson: LATEST_BYTES });
    const sentinel = Buffer.from('sentinel\r\nbytes', 'utf8');
    fs.mkdirSync(path.join(w.siteDir, 'manager'), { recursive: true });
    fs.writeFileSync(path.join(w.siteDir, 'manager', 'sentinel'), sentinel);
    const rec = recordingGenFeed();
    const r = run(generateArgv(w), rec.deps);
    assert.equal(r.code, 1);
    assert.match(r.err[0], /^feed-publish: E_INPUT: /u, r.stderr);
    assert.deepEqual(fs.readdirSync(path.join(w.siteDir, 'manager')), ['sentinel'], 'no latest.json beside it');
    assert.ok(fs.readFileSync(path.join(w.siteDir, 'manager', 'sentinel')).equals(sentinel), 'sentinel byte-identical');
    assert.equal(rec.calls.length, 0, 'genFeedMain never called');
    assert.equal(fs.existsSync(path.join(w.siteDir, 'versions.json')), false);
    assert.equal(fs.existsSync(path.join(w.inDir, 'gen-feed.log')), false);
  });

  await t.test('(11) generate: a second gen-feed run that differs is E_FEED_NONDETERMINISTIC (PD-9)', (s) => {
    const variants = {
      bytes: (n, argv, io) => {
        const code = genFeed.main(argv, io);
        if (n === 2) fs.appendFileSync(outOf(argv), ' ');
        return code;
      },
      'exit code': (n, argv, io) => {
        const code = genFeed.main(argv, io);
        return n === 2 ? 3 : code;
      },
      stderr: (n, argv, io) => {
        const code = genFeed.main(argv, io);
        if (n === 2) io.stderr.write('gen-feed: WARN E_LAYOUT: plugin Alphaplug: only in the second run\n');
        return code;
      },
    };
    for (const [label, behave] of Object.entries(variants)) {
      const w = generateWorld(s, 'gen-nondet', { manager: copiedManager, latestJson: LATEST_BYTES });
      const rec = recordingGenFeed(behave);
      const r = run(generateArgv(w), rec.deps);
      assert.equal(r.code, 1, `${label}: ${r.stderr}`);
      assert.equal(rec.calls.length, 2, label);
      assert.match(r.err[r.err.length - 1], /^feed-publish: E_FEED_NONDETERMINISTIC: /u, `${label}: ${r.stderr}`);
      assert.equal(fs.existsSync(path.join(w.siteDir, 'manager')), false, `${label}: no <site-dir>/manager left`);
    }
  });

  await t.test('(12) generate: a missing, non-JSON or unknown-status collect-report.json is E_INPUT', (s) => {
    const cases = {
      missing: null,
      'not JSON': '{"manager": {"status": "none"\n',
      'status other': JSON.stringify({ plugins: [], manager: { status: 'other' }, gh_calls: 1 }) + '\n',
      'no manager': JSON.stringify({ plugins: [], gh_calls: 1 }) + '\n',
    };
    for (const [label, text] of Object.entries(cases)) {
      const w = generateWorld(s, 'gen-report');
      const report = path.join(w.inDir, 'collect-report.json');
      if (text === null) fs.rmSync(report);
      else fs.writeFileSync(report, text);
      const r = run(generateArgv(w)); // stubDeps: genFeedMain throws if called
      assert.equal(r.code, 1, `${label}: ${r.stderr}`);
      assert.match(r.err[0], /^feed-publish: E_INPUT: /u, `${label}: ${r.stderr}`);
      assert.equal(fs.existsSync(path.dirname(w.siteDir)), false, `${label}: the site dir is not created`);
    }
  });

  await t.test('(13) generate --annotate: the exit and one feed-degrade warning per WARN line, single-lined', (s) => {
    let w = generateWorld(s, 'gen-annotate0');
    let r = run(generateArgv(w, true), recordingGenFeed().deps);
    assert.equal(r.code, 0, r.stderr);
    assert.deepEqual(r.out, ['::notice title=feed-generate::exit 0, deterministic']);

    const warnCr = 'gen-feed: WARN E_LAYOUT: plugin Alphaplug: a\rb';
    const other = 'gen-feed: a line that is not a WARN';
    const warnPlain = 'gen-feed: WARN E_TC_NAME: plugin Betaplug: plain';
    const text = `${warnCr}\n${other}\n${warnPlain}\n`;
    w = generateWorld(s, 'gen-annotate3');
    r = run(
      generateArgv(w, true),
      recordingGenFeed((n, argv, io) => {
        genFeed.main(argv, io);
        io.stderr.write(text);
        return 3;
      }).deps,
    );
    assert.equal(r.code, 3, r.stderr);
    assert.deepEqual(r.out, [
      '::notice title=feed-generate::exit 3, deterministic',
      `::warning title=feed-degrade::${JSON.stringify(warnCr)}`,
      `::warning title=feed-degrade::${warnPlain}`,
    ]);
    assert.deepEqual(r.err, [JSON.stringify(warnCr), other, warnPlain], 'stderr echoed through the PD-18 writer');
    assert.equal(fs.readFileSync(path.join(w.inDir, 'gen-feed.log'), 'utf8'), text, 'the log holds the raw stderr');
  });
});
