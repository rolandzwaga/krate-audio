#!/usr/bin/env node
// Close lane for Krate Manager Phase 4 (L-CLOSE; tasks.md T019; spec SC-001, SC-006, SC-014 Windows, SC-020, SC-021,
// SC-023; plan.md §7.4, §8). Run by the MAIN LOOP at close, never by an implementer or a gate: it holds the release
// `npm run tauri build` (LTO, plus the NSIS toolset download on first use), which runs well past five minutes.
//
// WHY one script: the close rows must come from one run whose steps share a clock and a log directory, with every
// independent step concurrent (CLAUDE.md "Optimise Every Build and Test Run") and every child under a wall-clock
// ceiling, so a hung step reads RED instead of stalling the close. Same form as
// specs/krate-manager-phase3/evidence/close-lane.js.
//
// WHY the workflow's own step text: wherever a row says "step <name>", the script extracts that step's `run: |` block
// from .github/workflows/manager-ci.yml, removes its indentation and runs it with Git Bash `bash.exe -c`, cwd
// apps/krate-manager/ (the job's defaults.run.working-directory), env RUNNER_OS=Windows and RUNNER_TEMP=<log dir>. Local
// and CI logic cannot drift. The step text, as run, is saved beside its log (<log>.sh).
//
// Usage (repo root, Windows dev box; Node only, no dependencies):
//   node specs/krate-manager-phase4/evidence/close-lane.js                       # the close (last line GREEN | RED)
//   node specs/krate-manager-phase4/evidence/close-lane.js --skip-tauri-build    # everything but B2-B5 (T019's short
//                                                                                 # run; last line CLOSE LANE PARTIAL)
// Rows:
//   A0   rustc --version, node --version, df -h of the repo root only (records only)
//   A0b  git status --porcelain --untracked-files=all, kept for C1
//   A (concurrent):
//     A1 one chain (the cargo commands share one target-dir lock):
//       A1a step `cargo fmt`; A1b step `clippy`; A1c step `clippy dev-feed`; A1d step `clippy-guard` (its ::notice;
//       $RUNNER_TEMP/guard.txt copied over evidence/clippy-guard.txt with a one-line provenance header); A1e step
//       `cargo test`; A1f cargo test -p krate-manager-core check_under_temp_symlink_escape -- --nocapture (records whether
//       the link was made); A1g cargo tree -p krate-manager-core -e normal --locked > evidence/core-tree.txt (no tauri,
//       dirs, directories, home)
//     A2 one chain: A2a step `npm ci`; A2b step `typecheck`; A2c step `lint`; A2d step `vitest`
//     A3 (repo root): A3a node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" to a TAP file (# fail 0,
//        plan 1..59, no Node warning line); A3b L-TAP on it ("TAP plan=1..59 top-level=59"); A3c L-VLINT
//     A4 L-YAML (yaml@2.6.1) on manager-ci.yml and feed-tools.yml
//     A5 L-LINT node tools/lint-serial-test-runs.js
//     A6 node tools/gen-specs-index.js --check
//     A7 A7a scope-check.js --self-test, then A7b L-SCOPE
//     A8 L-XCHECK (clippy of krate-manager-core for x86_64-unknown-linux-gnu and aarch64-apple-darwin) when both
//        targets are installed, else SKIPPED (ruling [P-3]). It starts after A1: cross clippy still builds host build
//        scripts under target/debug, whose lock A1 holds, so starting it beside A1 would only block. It overlaps B2,
//        which locks target/release.
//     A9 the SC-021 grep over apps/krate-manager (prints nothing)
//   B (after A1 and A2, only when every A1 / A2 row is green):
//     B1 step `required tests ran` (::notice ...::49 cargo + 3 vitest)
//     B2 step `tauri build` (40 min ceiling); B3 step `build warnings`; B4 step `dev-feed absent`; B5 step
//     `artifact version` (ProductVersion 0.1.0 and the NSIS file)
//   C (last): C1 git status again, required equal to A0b; C2 L-SCOPE again
// Step `Checkout unchanged` is not run locally: the phase's own uncommitted files fail it until the user commits. C1
// leaves out the files this script itself writes (EVIDENCE_OUTPUTS): they are close evidence, not build or test output.
// Raw logs: F:/tmp/gate/krate-manager-phase4-close/<timestamp>/. Summary: evidence/close-lane.log (written and printed);
// wall clocks: evidence/timings.md (plan §7.4, for the next run's planning). Exit 0 green (or partial with every run row
// green), 1 red.
'use strict';
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const APP = path.join(ROOT, 'apps', 'krate-manager');
const STAMP = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
// Forward slashes: the directory is also RUNNER_TEMP inside Git Bash.
const LOG_DIR = `F:/tmp/gate/krate-manager-phase4-close/${STAMP}`;
const SUMMARY = path.join(__dirname, 'close-lane.log');
const TIMINGS = path.join(__dirname, 'timings.md');
const GUARD_EVIDENCE = path.join(__dirname, 'clippy-guard.txt');
const CORE_TREE = path.join(__dirname, 'core-tree.txt');
const EVIDENCE_OUTPUTS = [SUMMARY, TIMINGS, GUARD_EVIDENCE, CORE_TREE]
  .map((f) => path.relative(ROOT, f).replace(/\\/g, '/'));
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'manager-ci.yml');
const BASH = 'C:/Program Files/Git/bin/bash.exe';
const MIN = 60 * 1000;
const SKIP_BUILD = process.argv.includes('--skip-tauri-build');

const EXPECTED_PLAN = 59; // tasks.md L-TAP (from T017 on)
const TAP_FILE = `${LOG_DIR}/A3a-node-test.tap`;
const XTARGETS = ['x86_64-unknown-linux-gnu', 'aarch64-apple-darwin'];
// tasks.md T019 A9 (spec SC-021), with the two --exclude-dir options moved before `--`: in the task's order they follow
// the end-of-options marker, so grep reads them as file operands and walks node_modules and target (measured
// 2026-10-30: "grep: --exclude-dir=node_modules: No such file or directory", exit 2, hundreds of vendored matches).
const SC021_GREP = 'grep -rnE --exclude-dir=node_modules --exclude-dir=target' +
  " -- '--test-threads|--no-file-parallelism|fileParallelism|maxWorkers|pool:' apps/krate-manager";

const results = []; // { id, ok, text, wallS }
function record(id, ok, text, wallS = '') {
  results.push({ id, ok, text, wallS });
  console.log(`${id.padEnd(4)} ${ok ? 'GREEN' : 'RED  '} ${text}`);
}

/** Kills a child and its descendants (bash -> cargo -> rustc, npx -> cmd -> node). */
function killTree(child) {
  if (process.platform === 'win32') {
    childProcess.spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true });
  } else {
    child.kill('SIGKILL');
  }
}

/**
 * Runs one command under a wall-clock ceiling, streams stdout+stderr to <LOG_DIR>/<log>, and resolves
 * { code, timedOut, wallS, out, err, log }. `stdinFile` feeds a file to stdin; `shell` is only for npx (a .cmd on
 * Windows); `env` replaces the environment; `cwd` defaults to the repo root.
 */
function run(cmd, args, { log, ceilingMs, stdinFile, shell = false, env, cwd = ROOT }) {
  return new Promise((resolve) => {
    const logPath = `${LOG_DIR}/${log}`;
    const sink = fs.createWriteStream(logPath);
    const stdin = stdinFile ? fs.openSync(path.join(ROOT, stdinFile), 'r') : 'ignore';
    const t0 = Date.now();
    const chunks = [];
    const errChunks = [];
    let timedOut = false;
    let child;
    let timer;
    const finish = (code) => {
      clearTimeout(timer);
      if (typeof stdin === 'number') fs.closeSync(stdin);
      sink.end(() => resolve({
        code, timedOut, wallS: ((Date.now() - t0) / 1000).toFixed(1), out: chunks.join(''), err: errChunks.join(''),
        log: logPath,
      }));
    };
    sink.write(`$ (cwd ${path.relative(ROOT, cwd) || '.'}) ${cmd} ${args.join(' ')}${stdinFile ? ` < ${stdinFile}` : ''}\n`);
    try {
      const spawnOpts = { cwd, shell, windowsHide: true, stdio: [stdin, 'pipe', 'pipe'] };
      if (env) spawnOpts.env = env;
      // A shell command is one fixed string with no args array (Node DEP0190 warns on args with shell: true).
      child = shell ? childProcess.spawn(cmd, spawnOpts) : childProcess.spawn(cmd, args, spawnOpts);
    } catch (e) {
      chunks.push(String(e.message));
      return finish(-1);
    }
    timer = setTimeout(() => { timedOut = true; killTree(child); }, ceilingMs);
    child.stdout.on('data', (d) => { chunks.push(d.toString()); sink.write(d); });
    child.stderr.on('data', (d) => { chunks.push(d.toString()); errChunks.push(d.toString()); sink.write(d); });
    child.on('error', (e) => { chunks.push(`spawn error: ${e.message}\n`); });
    child.on('close', (code) => finish(code === null ? -1 : code));
  });
}

const node = (script, args, opts) => run(process.execPath, [script, ...args], opts);
const lastLine = (text) => text.split(/\r?\n/).map((l) => l.trim()).filter(Boolean).pop() || '';
const fail = (r) => (r.timedOut ? `TIMEOUT (killed after ${r.wallS}s)` : `exit=${r.code}`);
const green = (r) => !r.timedOut && r.code === 0;
const lines = (text) => text.split(/\r?\n/);
const notices = (text) => lines(text).filter((l) => /^::(notice|error) /.test(l)).join(' | ');
// cargo colours nothing into a pipe, but the step's tee keeps whatever arrives; strip ANSI before counting.
const plain = (text) => text.replace(/\x1b\[[0-9;]*m/g, '');
const warningLines = (text) => lines(plain(text)).filter((l) => /^warning:/.test(l)).length;

/**
 * The `run: |` block of manager-ci.yml step `name`, with its indentation removed (a heredoc terminator inside it then
 * sits at column 0, as in the runner's script file). Refuses a step with its own `if:` or `working-directory:`, whose
 * local run would differ from the Windows leg's.
 */
function extractStep(yamlText, name) {
  const all = yamlText.split(/\r?\n/);
  const nameRe = /^(\s*)- name: (.*)$/;
  const unquote = (s) => s.trim().replace(/^'(.*)'$/, '$1').replace(/^"(.*)"$/, '$1');
  const at = all.findIndex((l) => { const m = nameRe.exec(l); return m && unquote(m[2]) === name; });
  if (at < 0) throw new Error(`manager-ci.yml has no step "${name}"`);
  const stepIndent = nameRe.exec(all[at])[1].length;
  for (let i = at + 1; i < all.length; i++) {
    const l = all[i];
    if (!l.trim()) continue;
    const ind = l.length - l.trimStart().length;
    if (ind <= stepIndent) break;
    if (/^\s*(if|working-directory):/.test(l)) throw new Error(`step "${name}" sets ${l.trim()}`);
    const m = /^(\s*)run: \|\s*$/.exec(l);
    if (!m) continue;
    const body = [];
    for (let j = i + 1; j < all.length; j++) {
      const b = all[j];
      if (b.trim() && b.length - b.trimStart().length <= m[1].length) break;
      body.push(b);
    }
    while (body.length && !body[body.length - 1].trim()) body.pop();
    const cut = Math.min(...body.filter((b) => b.trim()).map((b) => b.length - b.trimStart().length));
    return `${body.map((b) => b.slice(Math.min(cut, b.length))).join('\n')}\n`;
  }
  throw new Error(`step "${name}" has no "run: |" block`);
}

let yamlText = '';
/** Runs manager-ci.yml step `name` with Git Bash from apps/krate-manager/. */
function step(name, log, ceilingMs) {
  let script;
  try {
    script = extractStep(yamlText, name);
  } catch (e) {
    return Promise.resolve({ code: -1, timedOut: false, wallS: '0.0', out: e.message, err: e.message, log: '-' });
  }
  fs.writeFileSync(`${LOG_DIR}/${log}.sh`, script);
  const env = { ...process.env, RUNNER_OS: 'Windows', RUNNER_TEMP: LOG_DIR };
  return run(BASH, ['-c', script], { log, ceilingMs, env, cwd: APP });
}

function gitStatus() {
  const r = childProcess.spawnSync('git', ['-c', 'core.quotepath=off', 'status', '--porcelain', '--untracked-files=all'],
    { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  if (r.status !== 0) return `git status failed: ${r.stderr}`;
  return r.stdout.split('\n').filter((l) => l && !EVIDENCE_OUTPUTS.includes(l.slice(3))).join('\n');
}

// ---- A0: records ----------------------------------------------------------------------------------------------
async function a0Records() {
  const rc = await run('rustc', ['--version'], { log: 'A0-rustc.log', ceilingMs: 2 * MIN });
  const nv = await run(process.execPath, ['--version'], { log: 'A0-node.log', ceilingMs: 2 * MIN });
  const df = await run(BASH, ['-c', 'df -h .'], { log: 'A0-df.log', ceilingMs: 2 * MIN });
  const dfRow = lastLine(df.out).replace(/\s+/g, ' ');
  record('A0', green(rc) && green(nv) && green(df),
    `rustc="${lastLine(rc.out)}" node="${lastLine(nv.out)}" df(repo root)="${dfRow}"`, rc.wallS);
}

// ---- A1: the cargo chain ----------------------------------------------------------------------------------------
async function a1Cargo() {
  let r = await step('cargo fmt', 'A1a-cargo-fmt.log', 5 * MIN);
  record('A1a', green(r), `step cargo fmt ${fail(r)} last="${lastLine(r.out)}" wall=${r.wallS}s log=${r.log}`, r.wallS);

  for (const [id, name, log] of [['A1b', 'clippy', 'A1b-clippy.log'], ['A1c', 'clippy dev-feed', 'A1c-clippy-dev-feed.log']]) {
    r = await step(name, log, 20 * MIN);
    const w = warningLines(r.out);
    record(id, green(r) && w === 0, `step ${name} ${fail(r)} warning-lines=${w} notices="${notices(r.out)}"` +
      ` wall=${r.wallS}s log=${r.log}`, r.wallS);
  }

  r = await step('clippy-guard', 'A1d-clippy-guard.log', 15 * MIN);
  const guardNotice = lines(r.out).find((l) => l.startsWith('::notice title=clippy-guard (Windows)::')) || '';
  const guardTxt = `${LOG_DIR}/guard.txt`;
  let copied = 'guard.txt missing';
  if (fs.existsSync(guardTxt)) {
    const header = `# ${new Date().toISOString().slice(0, 10)} (Windows, T019 close lane A1d, from apps/krate-manager/,` +
      ` RUNNER_TEMP=${LOG_DIR}): manager-ci.yml step clippy-guard, its own run: text (${guardNotice || 'no notice'})\n`;
    fs.writeFileSync(GUARD_EVIDENCE, header + fs.readFileSync(guardTxt, 'utf8'));
    copied = `copied to ${path.relative(ROOT, GUARD_EVIDENCE).replace(/\\/g, '/')}`;
  }
  record('A1d', green(r) && guardNotice !== '', `step clippy-guard ${fail(r)} notice="${guardNotice || notices(r.out)}"` +
    ` ${copied} wall=${r.wallS}s log=${r.log}`, r.wallS);

  r = await step('cargo test', 'A1e-cargo-test.log', 20 * MIN);
  const summaries = lines(r.out).filter((l) => /^test result: /.test(l));
  const failedBins = summaries.filter((l) => !/ 0 failed;/.test(l)).length;
  const passed = summaries.reduce((n, l) => n + Number((/ (\d+) passed;/.exec(l) || [0, 0])[1]), 0);
  record('A1e', green(r) && summaries.length > 0 && failedBins === 0, `step cargo test ${fail(r)} binaries=${summaries.length}` +
    ` passed=${passed} binaries-with-failures=${failedBins} wall=${r.wallS}s log=${r.log}`, r.wallS);

  r = await run('cargo', ['test', '-p', 'krate-manager-core', 'check_under_temp_symlink_escape', '--', '--nocapture'],
    { log: 'A1f-symlink-case.log', ceilingMs: 10 * MIN, cwd: APP });
  const ran = lines(r.out).some((l) => /^test \S*check_under_temp_symlink_escape \.\.\. ok$/.test(l));
  const link = /symlink case skipped/.test(r.out) ? 'NOT made (os error 1314, no symlink privilege; the case returned early)'
    : 'made (the escape assertion ran)';
  record('A1f', green(r) && ran, `check_under_temp_symlink_escape ${fail(r)} ok-line=${ran ? 'found' : 'MISSING'}` +
    ` link ${link} wall=${r.wallS}s log=${r.log}`, r.wallS);

  // --locked (tasks.md rule 8): a resolve here must never rewrite Cargo.lock.
  r = await run('cargo', ['tree', '-p', 'krate-manager-core', '-e', 'normal', '--locked'],
    { log: 'A1g-cargo-tree.log', ceilingMs: 5 * MIN, cwd: APP });
  const tree = r.out.replace(/\r\n/g, '\n');
  const treeOut = lines(tree).filter((l) => /^[\s│├└─]*[\w-]+ v\d/.test(l)).join('\n');
  fs.writeFileSync(CORE_TREE, `${treeOut}\n`);
  const banned = lines(treeOut).filter((l) => /(^|[\s─])(tauri[\w-]*|dirs[\w-]*|directories[\w-]*|home) v\d/.test(l));
  const rootOk = treeOut.startsWith('krate-manager-core v');
  record('A1g', green(r) && rootOk && banned.length === 0, `cargo tree -p krate-manager-core -e normal ${fail(r)}` +
    ` crates=${lines(treeOut).length} root=${rootOk ? 'ok' : 'MISSING'} banned=${banned.length ? banned.join(';') : 'none'}` +
    ` (tauri, dirs, directories, home) -> ${path.relative(ROOT, CORE_TREE).replace(/\\/g, '/')} wall=${r.wallS}s`, r.wallS);
}

// ---- A2: the npm chain ------------------------------------------------------------------------------------------
async function a2Npm() {
  for (const [id, name, ceiling] of [['A2a', 'npm ci', 10], ['A2b', 'typecheck', 5], ['A2c', 'lint', 5], ['A2d', 'vitest', 10]]) {
    const r = await step(name, `${id}-${name.replace(/\s+/g, '-')}.log`, ceiling * MIN);
    record(id, green(r), `step ${name} ${fail(r)} last="${lastLine(r.out)}" wall=${r.wallS}s log=${r.log}`, r.wallS);
  }
}

// ---- A3: L-NODE whole set, L-TAP, L-VLINT -----------------------------------------------------------------------
async function a3Node() {
  // Quoted glob, expanded by Node; default concurrency and isolation (files in parallel child processes).
  const r = await run(process.execPath, ['--test', '--test-reporter=tap', 'tools/feed-contract/tests/*.test.js'],
    { log: 'A3a-node-test.log', ceilingMs: 10 * MIN });
  fs.writeFileSync(TAP_FILE, r.out);
  const num = (k) => { const m = r.out.match(new RegExp(`^# ${k} (\\d+)`, 'm')); return m ? Number(m[1]) : NaN; };
  const plan = (r.out.match(/^1\.\.(\d+)$/m) || [])[1];
  const nodeWarnings = lines(r.out).filter((l) => /\(node:\d+\) .*Warning|DeprecationWarning|ExperimentalWarning/.test(l)).length;
  record('A3a', green(r) && num('fail') === 0 && Number(plan) === EXPECTED_PLAN && nodeWarnings === 0,
    `node --test ${fail(r)} plan=1..${plan} tests=${num('tests')} pass=${num('pass')} fail=${num('fail')}` +
    ` skipped=${num('skipped')} nodeWarnings=${nodeWarnings} wall=${r.wallS}s tap=${TAP_FILE}`, r.wallS);

  const t = await node('specs/krate-manager-phase2/evidence/tap-check.js', [TAP_FILE], { log: 'A3b-tap-check.log', ceilingMs: 2 * MIN });
  const want = `TAP plan=1..${EXPECTED_PLAN} top-level=${EXPECTED_PLAN}`;
  record('A3b', green(t) && t.out.includes(want), `L-TAP ${fail(t)} want "${want}" ${t.out.includes(want) ? 'printed' : 'NOT printed'}` +
    ` last="${lastLine(t.out)}" wall=${t.wallS}s log=${t.log}`, t.wallS);

  const v = await node('tools/lint-manager-version.js', [], { log: 'A3c-version-lint.log', ceilingMs: 2 * MIN });
  record('A3c', green(v) && v.err.trim() === '', `L-VLINT ${fail(v)} stderr=${v.err.trim() ? `"${lastLine(v.err)}"` : 'none'}` +
    ` wall=${v.wallS}s log=${v.log}`, v.wallS);
}

// ---- A4..A7, A9: short lanes ------------------------------------------------------------------------------------
async function a4Yaml() {
  // One file after another: concurrent `npx --yes` runs race on the npx cache.
  for (const [i, wf] of ['manager-ci.yml', 'feed-tools.yml'].entries()) {
    const r = await run('npx --yes yaml@2.6.1 valid', [], {
      log: `A4-yaml-${i + 1}-${path.basename(wf, '.yml')}.log`, ceilingMs: 5 * MIN, stdinFile: `.github/workflows/${wf}`, shell: true,
    });
    record('A4', green(r), `L-YAML .github/workflows/${wf} ${fail(r)} wall=${r.wallS}s log=${r.log}`, r.wallS);
  }
}

async function a5Lint() {
  const r = await node('tools/lint-serial-test-runs.js', [], { log: 'A5-lint-serial.log', ceilingMs: 5 * MIN });
  record('A5', green(r), `L-LINT lint-serial-test-runs ${fail(r)} last="${lastLine(r.out)}" wall=${r.wallS}s log=${r.log}`, r.wallS);
}

async function a6SpecsIndex() {
  const r = await node('tools/gen-specs-index.js', ['--check'], { log: 'A6-specs-index.log', ceilingMs: 5 * MIN });
  record('A6', green(r), `gen-specs-index --check ${fail(r)} last="${lastLine(r.out)}" wall=${r.wallS}s log=${r.log}`, r.wallS);
}

async function scope(id, log) {
  const r = await node(path.join(__dirname, 'scope-check.js'), [], { log, ceilingMs: 2 * MIN });
  record(id, green(r), `L-SCOPE scope-check ${fail(r)} last="${lastLine(r.out)}" wall=${r.wallS}s log=${r.log}`, r.wallS);
}

async function a7Scope() {
  const s = await node(path.join(__dirname, 'scope-check.js'), ['--self-test'], { log: 'A7a-scope-self-test.log', ceilingMs: 2 * MIN });
  record('A7a', green(s), `scope-check --self-test ${fail(s)} last="${lastLine(s.out)}" wall=${s.wallS}s log=${s.log}`, s.wallS);
  await scope('A7b', 'A7b-scope-check.log');
}

async function a8XCheck() {
  const l = await run('rustup', ['target', 'list', '--installed'], { log: 'A8-targets.log', ceilingMs: 2 * MIN, cwd: APP });
  const installed = lines(l.out).map((x) => x.trim());
  if (!XTARGETS.every((t) => installed.includes(t))) {
    record('A8', true, `SKIPPED (targets not installed; ruling [P-3]) installed="${installed.filter(Boolean).join(', ')}"`);
    return;
  }
  for (const t of XTARGETS) {
    const r = await run('cargo', ['clippy', '-p', 'krate-manager-core', '--all-targets', '--locked', '--target', t, '--',
      '-D', 'warnings'], { log: `A8-xcheck-${t}.log`, ceilingMs: 20 * MIN, cwd: APP });
    const w = warningLines(r.out);
    // tasks.md L-XCHECK: a toolchain-caused failure (a build script needing the target's C compiler) is recorded
    // verbatim and the lane is skipped; a code error is a red.
    const toolchain = lines(plain(r.out)).find((x) => /failed to run custom build command|Failed to find tool|error: linker/.test(x));
    if (!green(r) && toolchain) {
      record('A8', true, `L-XCHECK ${t} SKIPPED (toolchain: "${toolchain.trim()}") ${fail(r)} wall=${r.wallS}s log=${r.log}`, r.wallS);
    } else {
      record('A8', green(r) && w === 0, `L-XCHECK ${t} ${fail(r)} warning-lines=${w} wall=${r.wallS}s log=${r.log}`, r.wallS);
    }
  }
}

async function a9Grep() {
  const r = await run(BASH, ['-c', SC021_GREP], { log: 'A9-sc021-grep.log', ceilingMs: 2 * MIN });
  // grep exits 1 when nothing matched: the pass.
  const ok = !r.timedOut && r.code === 1 && r.out.trim() === '';
  record('A9', ok, `SC-021 grep exit=${r.code} ${r.out.trim() ? `MATCHED (first 10 of ${lines(r.out.trim()).length}): ${lines(r.out.trim()).slice(0, 10).join(' ; ')}` : 'printed nothing'}` +
    ` wall=${r.wallS}s log=${r.log}`, r.wallS);
}

// ---- B: required tests, the release build and its three checks --------------------------------------------------
async function bBuild() {
  let r = await step('required tests ran', 'B1-required-tests-ran.log', 5 * MIN);
  const want = '::notice title=required tests ran (Windows)::49 cargo + 3 vitest';
  record('B1', green(r) && r.out.includes(want), `step required tests ran ${fail(r)} notices="${notices(r.out)}"` +
    ` want "${want}" wall=${r.wallS}s log=${r.log}`, r.wallS);
  if (SKIP_BUILD) {
    record('B2', true, 'DEFERRED (--skip-tauri-build): step tauri build and B3-B5 run only in the main loop close');
    return;
  }
  r = await step('tauri build', 'B2-tauri-build.log', 40 * MIN);
  record('B2', green(r), `step tauri build ${fail(r)} last="${lastLine(r.out)}" wall=${r.wallS}s log=${r.log}`, r.wallS);
  const checks = [
    ['B3', 'build warnings', '::notice title=build warnings (Windows)::0 warning lines in the release build'],
    ['B4', 'dev-feed absent', '::notice title=dev-feed absent (Windows)::'],
    ['B5', 'artifact version', '::notice title=artifact version (Windows)::0.1.0 (nsis=present)'],
  ];
  for (const [id, name, notice] of checks) {
    r = await step(name, `${id}-${name.replace(/\s+/g, '-')}.log`, 5 * MIN);
    record(id, green(r) && r.out.includes(notice), `step ${name} ${fail(r)} notices="${notices(r.out)}" want "${notice}"` +
      ` wall=${r.wallS}s log=${r.log}`, r.wallS);
  }
}

function writeTimings(totalS, verdict) {
  const rows = results.map((r) => `| ${r.id} | ${r.ok ? 'GREEN' : 'RED'} | ${r.wallS || '-'} | ${r.text.split(' wall=')[0].replace(/\|/g, '\\|')} |`);
  const text = [
    '# Krate Manager Phase 4: close-lane wall clocks',
    '',
    `Written by \`evidence/close-lane.js\`${SKIP_BUILD ? ' `--skip-tauri-build` (PARTIAL: B2-B5 deferred)' : ''},` +
      ` ${new Date().toISOString()}. Total wall ${totalS}s; ${verdict}. Raw logs (cleaned at close): \`${LOG_DIR}\`.`,
    'A1 (cargo) and A2 (npm) run as two chains beside A3-A7 and A9; A8 starts after A1; B starts after A1 and A2.',
    '',
    '| Row | Result | Wall (s) | Command |',
    '|---|---|---|---|',
    ...rows,
    '',
  ].join('\n');
  fs.writeFileSync(TIMINGS, text);
}

async function main() {
  fs.mkdirSync(LOG_DIR, { recursive: true });
  const t0 = Date.now();
  console.log(`close lane${SKIP_BUILD ? ' (--skip-tauri-build)' : ''}: logs ${LOG_DIR}`);
  yamlText = fs.readFileSync(WORKFLOW, 'utf8');
  await a0Records();
  const before = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/A0b-git-status.txt`, before);
  record('A0b', !before.startsWith('git status failed'), `git status --porcelain: ${lines(before).filter(Boolean).length} lines` +
    ` (without ${EVIDENCE_OUTPUTS.length} close-lane outputs) file=${LOG_DIR}/A0b-git-status.txt`);

  const a1 = a1Cargo();
  const a2 = a2Npm();
  const a8 = a1.then(a8XCheck);
  const b = Promise.all([a1, a2]).then(() => {
    const chain = results.filter((r) => /^A[12][a-g]$/.test(r.id));
    if (chain.every((r) => r.ok)) return bBuild();
    record('B', false, `skipped: not every A1 / A2 row is green (${chain.filter((r) => !r.ok).map((r) => r.id).join(', ')})`);
    return undefined;
  });
  await Promise.all([a1, a2, a8, b, a3Node(), a4Yaml(), a5Lint(), a6SpecsIndex(), a7Scope(), a9Grep()]);

  const after = gitStatus();
  fs.writeFileSync(`${LOG_DIR}/C1-git-status.txt`, after);
  const same = after === before;
  record('C1', same, `git status --porcelain ${same ? 'equal to A0b' : 'CHANGED from A0b'}` +
    ` (${lines(after).filter(Boolean).length} lines) file=${LOG_DIR}/C1-git-status.txt`);
  await scope('C2', 'C2-scope-check.log');

  const order = ['A0', 'A0b', 'A1a', 'A1b', 'A1c', 'A1d', 'A1e', 'A1f', 'A1g', 'A2a', 'A2b', 'A2c', 'A2d', 'A3a', 'A3b',
    'A3c', 'A4', 'A5', 'A6', 'A7a', 'A7b', 'A8', 'A9', 'B', 'B1', 'B2', 'B3', 'B4', 'B5', 'C1', 'C2'];
  // Stable sort: the two A4 and A8 rows keep their order.
  results.sort((x, y) => order.indexOf(x.id) - order.indexOf(y.id));
  const allGreen = results.every((r) => r.ok);
  const verdict = !allGreen ? 'CLOSE LANE RED'
    : SKIP_BUILD ? 'CLOSE LANE PARTIAL (B2-B5 deferred; not a close)' : 'CLOSE LANE GREEN';
  const totalS = ((Date.now() - t0) / 1000).toFixed(1);
  const summary = [
    `Krate Manager Phase 4 close lane${SKIP_BUILD ? ' --skip-tauri-build' : ''}, ${new Date().toISOString()},` +
      ` wall=${totalS}s, logs ${LOG_DIR}`,
    ...results.map((r) => `${r.id.padEnd(4)} ${r.ok ? 'GREEN' : 'RED  '} ${r.text}`),
    verdict,
  ].join('\n');
  fs.writeFileSync(SUMMARY, `${summary}\n`);
  writeTimings(totalS, verdict);
  console.log(`\n${summary}`);
  process.exit(allGreen ? 0 : 1);
}

main();
