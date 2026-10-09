#!/usr/bin/env node
// Phase-close lane runner: runs the NON-timing test lanes concurrently instead of back to back.
//
// WHY: a phase close used to run the eight per-push suites, the two [long] lanes and the preset sweep one after
// another (~8 h measured at Vorago 13c, specs/vorago-phase13c-capability-audibility/artifacts/final_g16_summary.txt).
// Only the timing lane needs an idle machine; every other lane asserts on rendered audio or state, so they can
// share the box. The Vorago sweep already partitions preset indices by VORAGO_SWEEP_SHARD=i/N and writes one record
// per preset to VORAGO_SWEEP_OUT, which the [vorago-aggregate] cases read back from VORAGO_SWEEP_IN.
//
// WHAT IT DOES NOT RUN: anything tagged [performance] [perf] [benchmark] [!benchmark]. Those stay with
// tools/run-cpu-tests.js, alone, pinned, after the box has idled.
//
// Usage:
//   node tools/run-close-lanes.js [--out <dir>] [--shards N] [--parallel P] [--bin <dir>] [--skip-sweep] [--dry-run]
// Defaults: out = f:/tmp/close-lanes/<timestamp>, shards = 8, parallel = cpu count / 2, bin = the Release bin dir.
// Writes <out>/<lane>.log per lane and <out>/summary.txt (one line per lane: wall clock, Catch2 totals, FAILED lines).
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const args = process.argv.slice(2);
const opt = (name, dflt) => { const i = args.indexOf(name); return i >= 0 && args[i + 1] !== undefined ? args[i + 1] : dflt; };
const flag = (name) => args.includes(name);
const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const OUT = path.resolve(opt('--out', path.join('f:/tmp/close-lanes', stamp)));
const BIN = path.resolve(opt('--bin', 'build/windows-x64-release/bin/Release'));
const SHARDS = Number(opt('--shards', 8));
const PARALLEL = Number(opt('--parallel', Math.max(2, Math.floor(os.cpus().length / 2))));
const DRY = flag('--dry-run');
const SKIP_SWEEP = flag('--skip-sweep');
const EXE = (n) => path.join(BIN, process.platform === 'win32' ? `${n}.exe` : n);
const TIMING = '~[performance]~[perf]~[benchmark]~[!benchmark]';

const lanes = [];
for (const s of ['dsp_core_tests', 'dsp_primitives_tests', 'dsp_processors_tests', 'dsp_systems_tests', 'dsp_effects_tests',
                 'vorago_tests', 'seraphis_tests', 'shared_tests']) {
  lanes.push({ name: `suite_${s}`, exe: EXE(s), argv: [`${TIMING}~[long]`], env: {} });
}
lanes.push({ name: 'long_dsp_systems_tests', exe: EXE('dsp_systems_tests'), argv: [`[long]${TIMING}`], env: {} });
lanes.push({ name: 'long_vorago_nonsweep', exe: EXE('vorago_tests'), argv: [`[long]~[vorago-sweep]${TIMING}`], env: {} });
const sweepOut = path.join(OUT, 'sweep-out');
if (!SKIP_SWEEP) {
  for (let i = 0; i < SHARDS; i++) {
    lanes.push({ name: `sweep_shard_${i}`, exe: EXE('vorago_tests'), argv: ['[vorago-sweep]~[vorago-aggregate]', '-d', 'yes'],
                 env: { VORAGO_SWEEP_SHARD: `${i}/${SHARDS}`, VORAGO_SWEEP_OUT: sweepOut, VORAGO_SWEEP_THREADS: '1' } });
  }
}
// The aggregate reads every shard's records, so it runs after the shards (see below), never alongside them.
const aggregate = SKIP_SWEEP ? null
  : { name: 'sweep_aggregate', exe: EXE('vorago_tests'), argv: ['[vorago-aggregate]', '-d', 'yes'], env: { VORAGO_SWEEP_IN: sweepOut } };

function runLane(lane) {
  return new Promise((resolve) => {
    const log = path.join(OUT, `${lane.name}.log`);
    const t0 = Date.now();
    const header = `START ${new Date().toISOString()} ${path.basename(lane.exe)} ${lane.argv.join(' ')} env=${JSON.stringify(lane.env)}\n`;
    if (DRY) { fs.writeFileSync(log, header + 'DRY RUN\n'); return resolve({ lane, rc: 0, wall: 0, log }); }
    const fd = fs.openSync(log, 'w');
    fs.writeSync(fd, header);
    const child = spawn(lane.exe, lane.argv, { env: { ...process.env, ...lane.env }, stdio: ['ignore', fd, fd] });
    child.on('exit', (rc) => {
      const wall = Math.round((Date.now() - t0) / 1000);
      fs.writeSync(fd, `exit=${rc} END ${new Date().toISOString()} wall=${wall}s\n`);
      fs.closeSync(fd);
      resolve({ lane, rc, wall, log });
    });
  });
}

function summarise(r) {
  let totals = '', failed = '';
  try {
    const lines = fs.readFileSync(r.log, 'utf8').split(/\r?\n/);
    totals = lines.filter((l) => /^(All tests passed|test cases:)/.test(l)).pop() || '(no Catch2 summary)';
    failed = lines.filter((l) => /FAILED:/.test(l)).slice(0, 3).map((l) => l.trim().slice(0, 110)).join(' ; ');
  } catch (e) { totals = `(unreadable: ${e.message})`; }
  return `${r.lane.name}: rc=${r.rc} wall=${r.wall}s | ${totals}${failed ? ' | ' + failed : ''}`;
}

async function pool(items, width, fn) {
  const results = [];
  let next = 0;
  const workers = Array.from({ length: Math.min(width, items.length) }, async () => {
    while (next < items.length) { const i = next++; results[i] = await fn(items[i]); }
  });
  await Promise.all(workers);
  return results;
}

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  if (!SKIP_SWEEP) fs.mkdirSync(sweepOut, { recursive: true });
  for (const l of lanes) if (!DRY && !fs.existsSync(l.exe)) { console.error(`missing binary: ${l.exe}`); process.exit(2); }
  const t0 = Date.now();
  console.log(`close lanes: ${lanes.length} lanes, ${PARALLEL} at a time -> ${OUT}`);
  const results = await pool(lanes, PARALLEL, runLane);
  if (aggregate) results.push(await runLane(aggregate));
  const wall = Math.round((Date.now() - t0) / 1000);
  const summary = [`close lanes ${DRY ? '(dry run) ' : ''}total wall=${wall}s parallel=${PARALLEL} shards=${SHARDS}`, ...results.map(summarise)];
  fs.writeFileSync(path.join(OUT, 'summary.txt'), summary.join('\n') + '\n');
  console.log(summary.join('\n'));
  process.exit(results.some((r) => r.rc !== 0) ? 1 : 0);
})();
