#!/usr/bin/env node
// Run the timing-sensitive tests (CPU budgets, benchmarks, [long] renders) that
// every concurrent gate deliberately excludes.
//
// WHY THIS EXISTS: these cases assert wall-clock against audio time, so any
// competing load inflates the number and produces a false red on code nobody
// touched. Observed twice: "RuinaeVoice SC-002 CPU < 3%" read 3.047 % beside a
// 32-job clang-tidy (passed 5/5 alone), and "Rungler CPU usage is within budget"
// read 0.637 % vs 0.5 % inside a workflow running agents (passed 3/3 alone).
// CI (.github/workflows/ci.yml) and .claude/workflows/*-phase.js therefore filter
// them out with '~[performance]~[perf]~[benchmark]~[!benchmark]~[long]'. This
// script is the other half of that bargain: the one place they DO run.
//
// The isolation guarantee is structural, not advisory -- suites run strictly one
// at a time, in-process, and nothing else may be running while you invoke it.
//
// ISOLATION HAS A SECOND CLAUSE: not concurrently, and not BACK-TO-BACK either.
// Measured on this repo 2026-09-01: running the timing lane continuously drove
// NoiseOrganism's SC-004 (c) from 142,794 to 162,840 ns on identical code (+14 %),
// and successive runs of the same suite failed 1 -> 3 -> 7 cases -- atmosphere,
// seraphis, continuous_body, harmonic_cloud and spectral_morph all went red
// without a line of their code changing, then all went green again after the
// machine idled. Sustained benchmarking heats the package and sustained boost
// clocks drop, so a suite run immediately after another measures the cooldown of
// the previous one. Hence SETTLE_MS between suites; raise it if you see the same
// suite flip verdicts across runs.
//
// CORE PLACEMENT (Windows, hybrid CPUs): on a part with performance and
// efficiency cores the other dominant noise source is the whole process landing
// on an efficiency core. Measured on identical code: EcosystemEngine SC-011 (a)
// read 28 528 ns/block on a performance core and 50 281 ns/block unpinned, a
// 1.76x swing against a 53 333 ns/block ceiling. So on Windows every suite is
// launched through tools/pin-perf-cores.ps1, which pins it to the cores of the
// highest efficiency class (detected, not indexed). Single-class parts, Linux
// and macOS run unpinned. The mask is printed at the top of each suite's log.
//
// Usage: node tools/run-cpu-tests.js [target ...]     (default: all suites)
//        SETTLE_MS=30000 node tools/run-cpu-tests.js  (longer cooldown)
//        node tools/run-cpu-tests.js [--scope affected|all] [--base <ref>] [target ...]
//
// ONLY AFFECTED CASES BY DEFAULT (--scope affected): each suite runs only the timing cases whose defining source
// file the change reaches (tools/affected-tests.js; changed vs --base, default HEAD). A suite with none is skipped.
// No change at all is a usage error. --scope all runs every timing case and needs a "# gate-all-ok: <reason>"
// comment (tools/hooks/guard-test-scope.js). Under CI the default is --scope all.

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const affectedTests = require('./affected-tests.js');

const BIN = 'build/windows-x64-release/bin/Release';
const DEFAULT_TARGETS = [
  'dsp_core_tests', 'dsp_primitives_tests', 'dsp_processors_tests',
  'dsp_systems_tests', 'dsp_effects_tests', 'shared_tests',
  'plugin_tests', 'approval_tests', 'disrumpo_tests', 'ruinae_tests',
  'innexus_tests', 'gradus_tests', 'membrum_tests', 'seraphis_tests',
  'vorago_tests', 'profundum_tests',
];

// Catch2 unions comma-separated tags. [!benchmark] and hidden [.perf] cases are
// excluded from a default run, so they must be named explicitly to run at all.
// Phase 14 R-6: the Vorago preset sweep never shares a CPU run
const FILTER = '[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]';

const argv = process.argv.slice(2);
const optVal = (name) => { const i = argv.indexOf(name); if (i < 0) return null; const v = argv[i + 1]; argv.splice(i, 2); return v; };
const SCOPE = optVal('--scope') || (process.env.CI ? 'all' : 'affected');
const BASE = optVal('--base') || 'HEAD';
if (!['affected', 'all'].includes(SCOPE)) { console.log(`usage: --scope must be affected or all, got ${SCOPE}`); process.exit(2); }
const targets = argv.length ? argv : DEFAULT_TARGETS;
let REACH = null;
if (SCOPE === 'affected') {
  REACH = affectedTests.reach(BASE);
  if (REACH.changed.length === 0) {
    console.log(`usage: nothing changed since ${BASE}, so no timing case is affected. Name what to measure from (--base main, --base <plugin>/vX.Y.Z) or use --scope all with a "# gate-all-ok: <reason>" comment.`);
    process.exit(2);
  }
  console.log(REACH.buildWide ? `scope: build-wide change (${REACH.buildWide}): every timing case runs`
    : `scope: affected by ${REACH.changed.length} changed file(s) since ${BASE}`);
}
// One input-file line (Catch2 test-spec syntax): '\' escapes, an unescaped ',' would split the name.
const specLine = (name) => `"${name.replace(/[\\",]/g, (c) => `\\${c}`)}"`;
const results = [];

// Synchronous cooldown between suites. Atomics.wait blocks this thread outright,
// which is what we want: nothing of ours should run during a settle, not even an
// event loop turn.
// 20 s was measured to be INSUFFICIENT on this hardware: with a 20 s settle the
// batched lane still produced a different failure set on every pass (aether_reverb,
// spectral_morph, seraphis -- none of them twice), while each suite passed when run
// alone. If you need a verdict you can rely on, run ONE suite per invocation with
// real idle time between them; batching is a convenience, not a gate.
const SETTLE_MS = Number(process.env.SETTLE_MS || 60000);
function settle(ms) {
  const buf = new Int32Array(new SharedArrayBuffer(4));
  Atomics.wait(buf, 0, 0, ms);
}

let first = true;
for (const t of targets) {
  const exe = path.join(BIN, `${t}.exe`);
  if (!fs.existsSync(exe)) {
    console.log(`SKIP ${t} (not built)`);
    continue;
  }
  // Narrow the suite to the affected timing cases (a Catch2 -f name file), or skip it.
  let caseFile = '';
  if (REACH && !REACH.buildWide) {
    const cases = affectedTests.listCases(exe, FILTER);
    if (cases) {
      const keep = cases.filter((c) => affectedTests.isAffected(REACH, c.file));
      if (keep.length === 0) { console.log(`SKIP ${t} (no timing case affected; ${cases.length} in the suite)`); continue; }
      caseFile = path.resolve('f:/tmp', `cpu_${t}.cases`);
      fs.writeFileSync(caseFile, [...new Set(keep.map((c) => specLine(c.name.trim())))].join('\n') + '\n');
      console.log(`scope: ${t}: ${keep.length} of ${cases.length} timing case(s) affected`);
    }
  }
  if (!first && SETTLE_MS > 0) {
    process.stdout.write(`settle ${SETTLE_MS / 1000}s ... `);
    settle(SETTLE_MS);
  }
  first = false;
  process.stdout.write(`RUN  ${t} ... `);
  // Strictly sequential: spawnSync blocks, so no two suites are ever in flight.
  // On Windows the suite goes through the pinning helper (see CORE PLACEMENT).
  const opts = { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 };
  const r = process.platform === 'win32'
    ? spawnSync('pwsh', ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
        '-File', path.join('tools', 'pin-perf-cores.ps1'), '-Exe', exe,
        ...(caseFile ? ['-InputFile', caseFile] : ['-ExeArgs', FILTER])], opts)
    : spawnSync(exe, caseFile ? ['-f', caseFile, '--order', 'decl'] : [FILTER], opts);
  const out = `${r.stdout || ''}${r.stderr || ''}`;
  const log = path.join('f:/tmp', `cpu_${t}.log`);
  try { fs.writeFileSync(log, out); } catch { /* log dir optional */ }
  const summary = out.trim().split(/\r?\n/).filter(Boolean).pop() || '(no output)';
  const ok = r.status === 0;
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${summary}`);
  if (!ok) console.log(`      full output: ${log}`);
  results.push({ t, ok });
}

const failed = results.filter((r) => !r.ok);
console.log(`\n${results.length - failed.length}/${results.length} suites passed.`);
if (failed.length) {
  console.log(`FAILED: ${failed.map((r) => r.t).join(', ')}`);
  console.log('Before treating any of these as a defect: confirm nothing else was');
  console.log('running, then re-run that suite ALONE, after the machine has idled.');
  console.log('A suite that flips verdicts between runs is measuring the machine,');
  console.log('not the code -- raise SETTLE_MS. Never relax a budget to fit.');
  process.exit(1);
}
