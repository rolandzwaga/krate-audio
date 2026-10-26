#!/usr/bin/env node
// Parallel test gate: runs one Catch2 filter over one or more test executables, every executable split into many
// Catch2 shards, all shards drawn from one worker pool sized to the machine.
//
// WHY: phase-workflow gates ran each executable as ONE process, one executable after the other (2026-10-26,
// Profundum Phase 14: each gate took 13-14 min on a single core of a 32-core box, ~16 gates per phase). Catch2
// partitions the filtered case list deterministically with --shard-count / --shard-index, so no test changes.
//
// WHY MORE SHARDS THAN WORKERS: Catch2 cuts the list into contiguous chunks and a few cases (renders, sweeps) cost
// far more than the rest. Cutting each executable into ~4 x workers chunks and feeding them to a pool lets the
// workers that drew cheap chunks pick up the rest, so the wall clock approaches the single slowest case.
//
// WALL-CLOCK BARS RUN IN A QUIET SECOND PASS: a per-push case that asserts on its own wall clock (a render guard's
// 15 s bar, a per-block teardown budget) reads 1.5-2x slower beside 30 busy shards and goes red on untouched code
// (Profundum Phase 14 G7: render guards 17-23 s against 15 s; a Seraphis voice-steal budget). Such cases carry the
// [wallclock] tag (tools/lint-wallclock-tags.js keeps it that way). Pass 1 runs the filter minus [wallclock] on the
// full pool; pass 2 runs the filter's [wallclock] cases after pass 1 drains, on --wallclock-workers (default 4).
//
// NOT FOR TIMING TESTS: everything here runs concurrently. Filters must exclude [performance] [perf] [benchmark]
// [!benchmark] (tools/run-cpu-tests.js runs those alone). The runner refuses a filter that does not.
//
// Usage:
//   node tools/run-gate.js --filter '<catch2 filter>' [--out <dir>] [--workers N] [--shards-per-exe K]
//                          [--share N] [--bin <dir>] <exe> [<exe> ...]
//   <exe>    basename without extension (profundum_tests, dsp_systems_tests, ...)
//   --share N  N gates run at the same time (parallel agents, lanes): this one takes (cpu count - 2) / N workers.
//   --wallclock-workers W  width of the quiet [wallclock] pass (default 4).
//   Defaults: workers = cpu count - 2, shards-per-exe = 4 x workers, out = f:/tmp/gate/<timestamp>,
//             bin = build/windows-x64-release/bin/Release.
// Writes <out>/<exe>-<k>.log per shard. Prints one line per executable, every failed assertion, and a last line
//   GATE GREEN <exe> cases=<n> assertions=<m> | ... wall=<s>s workers=<w>
//   GATE RED <reason> ...
// Exit code 0 when green, 1 when red, 2 on bad usage.
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { parseCatch2Failures } = require('./ci-test-shard.js');

const args = process.argv.slice(2);
const exes = [];
const opts = {};
for (let i = 0; i < args.length; ++i) {
    if (args[i].startsWith('--')) opts[args[i].slice(2)] = args[++i];
    else exes.push(args[i]);
}
const usage = (msg) => { console.log(`GATE RED usage: ${msg}`); process.exit(2); };
const filter = opts.filter;
if (!filter) usage('--filter is required');
if (exes.length === 0) usage('name at least one test executable');
for (const t of ['~[performance]', '~[perf]', '~[benchmark]', '~[!benchmark]']) {
    if (!filter.includes(t)) usage(`the filter must exclude timing tests (${t} missing); run those with tools/run-cpu-tests.js`);
}
const SHARE = Math.max(1, Number(opts.share || 1));   // N concurrent gates each take 1/N of the machine
const WORKERS = Math.max(1, Number(opts.workers || Math.max(1, Math.floor((os.cpus().length - 2) / SHARE))));
const SHARDS = Math.max(1, Number(opts['shards-per-exe'] || WORKERS * 4));
const WALL_WORKERS = Math.max(1, Number(opts['wallclock-workers'] || 4));
const WALL_SHARDS = WALL_WORKERS * 2;
// Pass 1 excludes [wallclock]; pass 2 selects it (appended tag terms AND in Catch2). A filter that already names
// [wallclock] runs as a single pass, as given.
const TWO_PASS = !filter.includes('[wallclock]');
const PASS1 = TWO_PASS ? `${filter}~[wallclock]` : filter;
const PASS2 = `${filter}[wallclock]`;
const BIN = path.resolve(opts.bin || 'build/windows-x64-release/bin/Release');
const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '').replace('T', '-');
const OUT = path.resolve(opts.out || path.join('f:/tmp/gate', stamp));
fs.mkdirSync(OUT, { recursive: true });

const exePath = (n) => path.join(BIN, process.platform === 'win32' ? `${n}.exe` : n);
// Only Catch2 test executables are sharded. Gate prompts list every built target, and a non-test tool launched with
// the filter as its first argument does its own thing with it (2026-10-26: profundum_preset_generator took the filter
// as its output directory and 120 shards wrote presets into a folder named after it). Plugins have no exe at all.
const notTests = exes.filter((e) => !/_tests$/.test(e));
if (notTests.length) console.log(`skipped (not a *_tests executable, build-only target): ${notTests.join(', ')}`);
exes.splice(0, exes.length, ...exes.filter((e) => /_tests$/.test(e)));
if (exes.length === 0) usage('no *_tests executable among the targets');
for (const e of exes) if (!fs.existsSync(exePath(e))) usage(`missing ${exePath(e)}`);

const pass1Jobs = [];
for (const exe of exes) for (let k = 0; k < SHARDS; ++k) pass1Jobs.push({ exe, k, pass: 1, filter: PASS1, count: SHARDS });
const pass2Jobs = [];
if (TWO_PASS) for (const exe of exes) for (let k = 0; k < WALL_SHARDS; ++k) pass2Jobs.push({ exe, k, pass: 2, filter: PASS2, count: WALL_SHARDS });

// Catch2 totals: "All tests passed (A assertions in C test case(s))" or
// "test cases: C | P passed | F failed" + "assertions: A | ...". An empty shard prints "No tests ran".
function totals(text) {
    let m = /All tests passed \((\d+) assertions? in (\d+) test cases?\)/.exec(text);
    if (m) return { cases: +m[2], assertions: +m[1], failedCases: 0 };
    m = /test cases:\s*(\d+)\s*\|[^\n]*?(?:(\d+) failed)?\s*$/m.exec(text);
    const a = /assertions:\s*(\d+)/.exec(text);
    if (m) return { cases: +m[1], assertions: a ? +a[1] : 0, failedCases: m[2] ? +m[2] : 0 };
    if (/No tests ran/.test(text)) return { cases: 0, assertions: 0, failedCases: 0 };
    return null;
}

const t0 = Date.now();
const results = [];
function runOne(job) {
    return new Promise((resolve) => {
        const log = path.join(OUT, `${job.exe}-${job.pass === 2 ? 'wallclock-' : ''}${job.k}.log`);
        const argv = [job.filter, '--allow-running-no-tests', '--shard-count', String(job.count), '--shard-index', String(job.k)];
        const s0 = Date.now();
        const child = spawn(exePath(job.exe), argv, { cwd: process.cwd(), windowsHide: true });
        const chunks = [];
        child.stdout.on('data', (d) => chunks.push(d));
        child.stderr.on('data', (d) => chunks.push(d));
        child.on('close', (rc) => {
            const text = Buffer.concat(chunks).toString('utf8');
            fs.writeFileSync(log, text);
            resolve({ ...job, rc, wall: (Date.now() - s0) / 1000, text, log });
        });
        child.on('error', (err) => resolve({ ...job, rc: -1, wall: 0, text: String(err), log }));
    });
}
async function runPool(jobs, width) {
    let next = 0;
    const worker = async () => { while (next < jobs.length) results.push(await runOne(jobs[next++])); };
    await Promise.all(Array.from({ length: Math.min(width, jobs.length) }, worker));
}

(async () => {
    await runPool(pass1Jobs, WORKERS);
    const wall1 = ((Date.now() - t0) / 1000).toFixed(1);
    await runPool(pass2Jobs, WALL_WORKERS);
    const wall = ((Date.now() - t0) / 1000).toFixed(1);
    const wallCases = results.filter((r) => r.pass === 2).reduce((n, r) => n + ((totals(r.text) || {}).cases || 0), 0);
    if (TWO_PASS) console.log(`pass 1 (~[wallclock], ${WORKERS} workers): ${wall1}s | pass 2 ([wallclock], ${WALL_WORKERS} workers): ${(wall - wall1).toFixed(1)}s, ${wallCases} case(s)`);
    const perExe = [];
    const problems = [];
    for (const exe of exes) {
        const mine = results.filter((r) => r.exe === exe).sort((a, b) => a.pass - b.pass || a.k - b.k);
        let cases = 0, assertions = 0, failedCases = 0, slowest = 0;
        for (const r of mine) {
            slowest = Math.max(slowest, r.wall);
            const t = totals(r.text);
            if (!t) { problems.push(`${exe} shard ${r.k}: no Catch2 summary (exit ${r.rc}, crash or abort) -> ${r.log}`); continue; }
            cases += t.cases; assertions += t.assertions; failedCases += t.failedCases;
            if (r.rc !== 0) {
                const fails = parseCatch2Failures(r.text);
                if (fails.length === 0) problems.push(`${exe} shard ${r.k}: exit ${r.rc} with no parsed assertion -> ${r.log}`);
                for (const f of fails) problems.push(`${exe} FAILED "${f.testCase}" ${f.file}:${f.line} ${f.detail}`);
            }
        }
        perExe.push({ exe, cases, assertions, failedCases, slowest });
        console.log(`${exe}: cases=${cases} assertions=${assertions} failed_cases=${failedCases} shards=${mine.length} slowest_shard=${slowest.toFixed(1)}s`);
    }
    for (const p of problems) console.log(p);
    const tally = perExe.map((e) => `${e.exe} cases=${e.cases} assertions=${e.assertions}`).join(' | ');
    if (problems.length === 0 && perExe.every((e) => e.failedCases === 0)) {
        if (perExe.some((e) => e.cases === 0)) {
            console.log(`GATE RED filter matched no case in: ${perExe.filter((e) => e.cases === 0).map((e) => e.exe).join(', ')} (wall=${wall}s, logs ${OUT})`);
            process.exit(1);
        }
        console.log(`GATE GREEN ${tally} wall=${wall}s workers=${WORKERS} logs=${OUT}`);
        process.exit(0);
    }
    console.log(`GATE RED ${problems.length} problem line(s) above | ${tally} wall=${wall}s workers=${WORKERS} logs=${OUT}`);
    process.exit(1);
})();
