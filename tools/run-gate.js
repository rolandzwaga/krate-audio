#!/usr/bin/env node
// Parallel test gate: runs one Catch2 filter over one or more test executables, every executable split into many
// Catch2 shards, all shards drawn from one worker pool sized to the machine.
//
// WHY: phase-workflow gates ran each executable as ONE process, one executable after the other (2026-10-26,
// Profundum Phase 14: each gate took 13-14 min on a single core of a 32-core box, ~16 gates per phase). Catch2
// runs one shard per process from a case list the runner deals (below), so no test changes.
//
// WHY INTERLEAVED, NOT CATCH2'S --shard-index: Catch2 cuts the filtered list into CONTIGUOUS chunks in declaration
// order, and slow cases declared next to each other land in one shard that sets the gate's wall clock (2026-11-09:
// dsp_systems_tests shard 21/28 ran ~600 s of a 631 s gate on two adjacent Vorago cases, 245 s + 217 s). The runner
// lists each executable's cases once per pass (--list-tests --verbosity quiet --order decl, same filter), deals case i
// to shard i mod S and passes each shard its names in a Catch2 input file (-f; never the command line: length limits,
// commas, quotes). A name the input file cannot express exactly (a leading/trailing '*' is always a wildcard) that
// also selects another listed case, or a list that does not round-trip through -f, falls back to --shard-index for
// that executable and pass; the per-executable line says so.
//
// WHY MORE SHARDS THAN WORKERS: a few cases (renders, sweeps) cost far more than the rest. Cutting each executable
// into ~4 x workers shards and feeding them to a pool lets the workers that drew cheap shards pick up the rest, so
// the wall clock approaches the single slowest case.
//
// WALL-CLOCK BARS RUN IN A QUIET SECOND PASS: a per-push case that asserts on its own wall clock (a render guard's
// 15 s bar, a per-block teardown budget) reads 1.5-2x slower beside 30 busy shards and goes red on untouched code
// (Profundum Phase 14 G7: render guards 17-23 s against 15 s; a Seraphis voice-steal budget). Such cases carry the
// [wallclock] tag (tools/lint-wallclock-tags.js keeps it that way). Pass 1 runs the filter minus [wallclock] on the
// full pool; pass 2 runs the filter's [wallclock] cases after pass 1 drains, on --wallclock-workers (default 4).
//
// CRASH DUMPS (Windows): a shard that crashes (0xC0000005 and the like) leaves a minidump. Every shard runs under
// tools/test-crash-dumps.cs, a minimal debugger that passes exceptions to the test untouched and dumps only one
// nothing handled, to f:/tmp/crashdumps/<exe>.<pid>.dmp; the red line names the dump. Read it with
// tools/analyze-crashdump.js, then tools/resolve_pdb_addrs.js for file:line (tools/test-crash-dumps.js has the
// details). No admin rights or registry change; off on Linux / macOS, in CI, and with KRATE_CRASH_DUMPS=0.
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
//   Defaults: workers = cpu count - 2, shards-per-exe = 4 x workers, out = f:/tmp/gate/<timestamp> (Windows) or <os tmpdir>/gate/<timestamp>,
//             bin = build/windows-x64-release/bin/Release.
// Writes <out>/<exe>-<k>.log per shard (its case list in <exe>-<k>.cases) and <out>/shard-times.tsv (one row per
// shard: exe, pass, index, cases, assertions, wall, exit). Prints one line per executable, every failed assertion, and a last line
//   GATE GREEN <exe> cases=<n> assertions=<m> | ... wall=<s>s workers=<w>
//   GATE RED <reason> ...
// Exit code 0 when green, 1 when red, 2 on bad usage.
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { parseCatch2Failures } = require('./ci-test-shard.js');
const crashDumps = require('./test-crash-dumps.js');

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
// f:/tmp is the Windows dev box's scratch drive; elsewhere 'f:/tmp' is a relative path and made a folder named "f:"
// inside the repo (WSL run, 2026-11-05), so other platforms use the OS temp dir.
const OUT_ROOT = process.platform === 'win32' ? 'f:/tmp/gate' : path.join(os.tmpdir(), 'gate');
const OUT = path.resolve(opts.out || path.join(OUT_ROOT, stamp));
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

// stdout + exit code of one executable run (listings).
function capture(exe, argv) {
    return new Promise((resolve) => {
        const child = spawn(exePath(exe), argv, { cwd: process.cwd(), windowsHide: true });
        const chunks = [];
        child.stdout.on('data', (d) => chunks.push(d));
        child.stderr.resume();
        child.on('close', (rc) => resolve({ rc, text: Buffer.concat(chunks).toString('utf8') }));
        child.on('error', (err) => resolve({ rc: -1, text: String(err) }));
    });
}
const LIST = ['--list-tests', '--verbosity', 'quiet', '--order', 'decl'];
// Quiet listing prints one name per line, a name starting with '#' in quotes (catch_reporter_helpers.cpp).
const parseList = (text) => text.split(/\r?\n/).filter((l) => l.trim() !== '').map((l) => (/^"#.*"$/.test(l) ? l.slice(1, -1) : l));
// One input-file line (catch_test_spec_parser.cpp): inside a quoted name '\' escapes the next character, an unescaped
// ',' makes the spec invalid and '"' ends the name.
const specLine = (name) => `"${name.replace(/[\\",]/g, (c) => `\\${c}`)}"`;
// Catch2 NamePattern: trimmed, ASCII case-insensitive, a leading/trailing '*' is a wildcard no escape removes.
const lowerAscii = (s) => s.replace(/[A-Z]/g, (c) => c.toLowerCase());
function nameMatches(pattern, name) {
    let p = lowerAscii(pattern.trim());
    const n = lowerAscii(name.trim());
    const atStart = p.startsWith('*'); if (atStart) p = p.slice(1);
    const atEnd = p.endsWith('*'); if (atEnd) p = p.slice(0, -1);
    if (atStart && atEnd) return n.includes(p);
    if (atStart) return n.endsWith(p);
    if (atEnd) return n.startsWith(p);
    return n === p;
}
// Shards of one executable for one pass: interleaved (case i -> shard i mod S) when the listed names round-trip
// through a Catch2 input file, else Catch2's contiguous --shard-index chunks (with the reason).
async function planShards(exe, pass, passFilter, count) {
    const tag = `${exe}-${pass === 2 ? 'wallclock-' : ''}`;
    const contiguous = (why) => ({
        exe, pass, mode: `contiguous (${why})`,
        jobs: Array.from({ length: count }, (_, k) => ({ exe, k, pass, argv: [passFilter, '--shard-count', String(count), '--shard-index', String(k)] })),
    });
    const listed = await capture(exe, [passFilter, ...LIST]);
    if (listed.rc !== 0) return contiguous(`listing exit ${listed.rc}`);
    const names = parseList(listed.text);
    // No shard at all: an EMPTY input file selects every case of the executable (measured).
    if (names.length === 0) return { exe, pass, mode: 'interleaved', jobs: [] };
    // Catch2 allows one name twice (disrumpo_tests has such a pair) and matches case-insensitively: one pattern runs
    // every case it names, so cases sharing a name form one group that goes to one shard, named once.
    const groups = new Map();
    for (const n of names) {
        const key = lowerAscii(n.trim());
        if (!groups.has(key)) groups.set(key, n);
    }
    const unique = [...groups.values()];
    for (const n of unique) {
        if (!/^\s*\*|\*\s*$/.test(n)) continue;
        const other = unique.find((m) => m !== n && nameMatches(n, m));
        if (other) return contiguous(`wildcard name "${n}" also selects "${other}"`);
    }
    const allFile = path.join(OUT, `${tag}all.cases`);
    fs.writeFileSync(allFile, unique.map(specLine).join('\n') + '\n');
    const back = await capture(exe, ['-f', allFile, ...LIST]);
    const got = parseList(back.text);
    if (back.rc !== 0 || got.length !== names.length || got.some((n, i) => n !== names[i])) {
        return contiguous(`input file did not round-trip: ${names.length} listed, ${got.length} read back`);
    }
    const S = Math.min(count, unique.length);
    const jobs = [];
    for (let k = 0; k < S; ++k) {
        const file = path.join(OUT, `${tag}${k}.cases`);
        fs.writeFileSync(file, unique.filter((_, i) => i % S === k).map(specLine).join('\n') + '\n');
        jobs.push({ exe, k, pass, argv: ['-f', file] });
    }
    return { exe, pass, mode: 'interleaved', jobs };
}

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
        // --order decl: Catch2 >= 3.9 defaults to random order, which re-seeds every shard and breaks disjoint coverage.
        const argv = [...job.argv, '--allow-running-no-tests', '--order', 'decl'];
        const s0 = Date.now();
        const run = crashDumps.wrap(exePath(job.exe), argv);
        const child = spawn(run.cmd, run.args, { cwd: process.cwd(), env: run.env, windowsHide: true });
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
    if (crashDumps.wrapper()) console.log(`crash dumps: a crashing shard writes a minidump to ${crashDumps.DUMP_DIR}`);
    const plans = await Promise.all([
        ...exes.map((exe) => planShards(exe, 1, PASS1, SHARDS)),
        ...(TWO_PASS ? exes.map((exe) => planShards(exe, 2, PASS2, WALL_SHARDS)) : []),
    ]);
    const pass1Jobs = plans.filter((p) => p.pass === 1).flatMap((p) => p.jobs);
    const pass2Jobs = plans.filter((p) => p.pass === 2).flatMap((p) => p.jobs);
    await runPool(pass1Jobs, WORKERS);
    const wall1 = ((Date.now() - t0) / 1000).toFixed(1);
    await runPool(pass2Jobs, WALL_WORKERS);
    const wall = ((Date.now() - t0) / 1000).toFixed(1);
    const wallCases = results.filter((r) => r.pass === 2).reduce((n, r) => n + ((totals(r.text) || {}).cases || 0), 0);
    if (TWO_PASS) console.log(`pass 1 (~[wallclock], ${WORKERS} workers): ${wall1}s | pass 2 ([wallclock], ${WALL_WORKERS} workers): ${(wall - wall1).toFixed(1)}s, ${wallCases} case(s)`);
    const perExe = [];
    const problems = [];
    const timeRows = ['exe\tpass\tshard\tcases\tassertions\twall_s\texit'];
    for (const exe of exes) {
        const mine = results.filter((r) => r.exe === exe).sort((a, b) => a.pass - b.pass || a.k - b.k);
        let cases = 0, assertions = 0, failedCases = 0, slowest = 0;
        for (const r of mine) {
            slowest = Math.max(slowest, r.wall);
            const t = totals(r.text);
            timeRows.push([exe, r.pass, r.k, t ? t.cases : '', t ? t.assertions : '', r.wall.toFixed(1), r.rc].join('\t'));
            for (const d of crashDumps.dumpsIn(r.text)) problems.push(`${exe} shard ${r.k}: CRASH ${d.code}, minidump ${d.path} (node tools/analyze-crashdump.js "${d.path}")`);
            if (!t) { problems.push(`${exe} shard ${r.k}: no Catch2 summary (exit ${r.rc}, crash or abort) -> ${r.log}`); continue; }
            cases += t.cases; assertions += t.assertions; failedCases += t.failedCases;
            if (r.rc !== 0) {
                const fails = parseCatch2Failures(r.text);
                if (fails.length === 0) problems.push(`${exe} shard ${r.k}: exit ${r.rc} with no parsed assertion -> ${r.log}`);
                for (const f of fails) problems.push(`${exe} FAILED "${f.testCase}" ${f.file}:${f.line} ${f.detail}`);
            }
        }
        perExe.push({ exe, cases, assertions, failedCases, slowest });
        const walls = mine.filter((r) => r.pass === 1).map((r) => r.wall).sort((a, b) => a - b);
        const median = walls.length ? walls[Math.floor((walls.length - 1) / 2)] : 0;
        const modes = plans.filter((p) => p.exe === exe).map((p) => (TWO_PASS ? `pass${p.pass} ${p.mode}` : p.mode)).join(', ');
        console.log(`${exe}: cases=${cases} assertions=${assertions} failed_cases=${failedCases} shards=${mine.length} slowest_shard=${slowest.toFixed(1)}s median_shard=${median.toFixed(1)}s sharding=${modes}`);
    }
    fs.writeFileSync(path.join(OUT, 'shard-times.tsv'), timeRows.join('\n') + '\n');
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
