#!/usr/bin/env node
// CI test shard runner: runs one slice of the per-push (or nightly [long]) test lane.
//
// WHY: the per-push lane ran every test executable one after another inside the build job. That was 9 min
// when the repository had six plugins and 45+ min once the Vorago suites landed (2026-10-09, measured on a
// 32-core box; the GitHub runner is slower), against a 20-minute step cap. Each plugin added makes it worse.
// The build job now uploads the test binaries once and a matrix of shard jobs runs them in parallel; the time
// per shard stays flat as suites are added because a new suite is a new slice, not new minutes.
//
// HOW THE SLICES ARE CUT: every executable in the manifest is one unit, except the big suites, which are split
// with Catch2's own --shard-count / --shard-index (kSuiteShards below; no test edits, Catch2 partitions the
// filtered case list deterministically). Units are assigned to the N job shards by longest-processing-time-
// first over the measured seconds in tools/ci-test-times.json, so the slowest unit goes first and the shards
// end up roughly even. A stale timing entry only unbalances the shards; it never changes what runs.
//
// WITHIN A SHARD the executables still run one at a time: concurrent heavy DSP suites on a 4-vCPU / 7 GB
// runner caused memory-pressure segfaults (the reason the old lane was sequential). Parallelism comes from
// jobs, never from threads inside one job.
//
// Usage:
//   node tools/ci-test-shard.js --os <windows|macos|linux> --bin <dir> --manifest <file>
//                               --shard <i> --count <N> [--long true|false] [--list] [--dry-run]
//   --manifest  one executable basename per line (no extension), written by the build job from the same
//               change-detection selection the build used
//   --list      print the assignment of every shard and exit (no tests run)
//   --long      run the nightly [long]~[vorago-sweep] lane instead of the per-push lane
//   --log-dir   also write everything this shard prints to <dir>/<os>-shard<i>.log (uploaded as an artifact)
//   --skip-presets  do not mirror the Windows factory-preset install (local runs: the build already did it)
//
// FAILURES ARE ANNOTATED: every failed Catch2 assertion becomes a ::error annotation (file, line, test case,
// expression and expansion) and the failed test-case names go into one ::notice. The annotations are readable
// through the check-runs API with a token that cannot download job logs, so a red shard names its failing
// tests without anyone opening the log.
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

// --- Catch2 console-reporter parsing ----------------------------------------------------------------------
// The console reporter prints, for each test case with a failure:
//   -------------------------------------------------------------------------------
//   <test case name>            (continuation lines unindented; section names indented by two spaces)
//   -------------------------------------------------------------------------------
//   <file>:<line>
//   ...............................................................................
//
//   <file>:<line>: FAILED:      (MSVC builds print <file>(<line>): FAILED:)
//     REQUIRE( expr )
//   with expansion:
//     1 == 2
// Returns [{ file, line, testCase, detail }] for every FAILED assertion in `text`.
function parseCatch2Failures(text) {
    const lines = text.split(/\r?\n/);
    const dash = /^-{40,}$/;
    const failed = /^(.+?)(?::(\d+)|\((\d+)\)): FAILED:$/;
    const out = [];
    let testCase = '(unknown test case)';
    for (let i = 0; i < lines.length; ++i) {
        if (dash.test(lines[i])) {
            // Header: dash line, name (plus indented section names), dash line. Skip to the closing dash so
            // it is not read as another opening one.
            let j = i + 1;
            const name = [];
            for (; j < lines.length && !dash.test(lines[j]); ++j) {
                if (lines[j] !== '' && !/^\s/.test(lines[j])) name.push(lines[j]);
            }
            if (j < lines.length && name.length > 0) { testCase = name.join(' '); i = j; }
            continue;
        }
        const m = failed.exec(lines[i]);
        if (!m) continue;
        const detail = [];
        for (let j = i + 1; j < lines.length && lines[j] !== '' && detail.length < 8; ++j) detail.push(lines[j].trim());
        out.push({ file: m[1], line: Number(m[2] || m[3]), testCase, detail: detail.join(' ') });
    }
    return out;
}
module.exports = { parseCatch2Failures };
if (require.main !== module) return;

const args = process.argv.slice(2);
const opt = (name, dflt) => { const i = args.indexOf(name); return i >= 0 && args[i + 1] !== undefined ? args[i + 1] : dflt; };
const flag = (name) => args.includes(name);

const osName = opt('--os', process.platform === 'win32' ? 'windows' : process.platform === 'darwin' ? 'macos' : 'linux');
const binDir = opt('--bin', 'build/bin/Release');
const manifestPath = opt('--manifest', 'build/test-manifest.txt');
const shardIndex = Number(opt('--shard', '0'));
const shardCount = Number(opt('--count', '8'));
const longLane = String(opt('--long', 'false')) === 'true';
const listOnly = flag('--list');
const dryRun = flag('--dry-run');
const logDir = opt('--log-dir', '');

// Everything printed goes to stdout and, with --log-dir, to the shard's log file as well.
let logFile = null;
if (logDir) {
    fs.mkdirSync(logDir, { recursive: true });
    logFile = fs.openSync(path.join(logDir, `${osName}-shard${shardIndex}.log`), 'w');
}
const emit = (s) => { process.stdout.write(s); if (logFile !== null) fs.writeSync(logFile, s); };
const log = (line) => emit(`${line}\n`);

// How many Catch2 shards each executable is cut into. Everything else runs whole. Keep a unit under about
// ten minutes on a GitHub runner; when a suite grows past that, raise its count here and refresh the times.
const kSuiteShards = longLane
    ? { dsp_systems_tests: 8, vorago_tests: 2, seraphis_tests: 2 }
    : { dsp_systems_tests: 6, vorago_tests: 2, seraphis_tests: 2 };

// Per-push: everything except perf/benchmark and the [long] renders (those run nightly, and always locally).
// Nightly: ONLY the [long] renders, minus the Vorago factory sweep, which has its own sharded nightly job.
const kFilter = longLane ? '[long]~[vorago-sweep]' : '~[performance]~[perf]~[benchmark]~[!benchmark]~[long]';
const kWarnSeconds = 900;  // a unit over this prints a ::warning:: - rebalance kSuiteShards, do not wait for red

if (!Number.isInteger(shardIndex) || !Number.isInteger(shardCount) || shardCount < 1 || shardIndex < 0 || shardIndex >= shardCount) {
    console.error(`ci-test-shard: bad --shard ${shardIndex} / --count ${shardCount}`);
    process.exit(2);
}

const timesFile = path.join(__dirname, 'ci-test-times.json');
const times = JSON.parse(fs.readFileSync(timesFile, 'utf8'));
const laneTimes = (longLane ? times.long : times.per_push) || {};

if (!fs.existsSync(manifestPath)) {
    log(`ci-test-shard: no manifest at ${manifestPath} - nothing to run`);
    process.exit(0);
}
const suites = fs.readFileSync(manifestPath, 'utf8').split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
if (suites.length === 0) {
    log('ci-test-shard: empty manifest - nothing to run');
    process.exit(0);
}

// --- cut the units ---------------------------------------------------------------------------------------
const units = [];
for (const suite of suites) {
    const k = kSuiteShards[suite] || 1;
    const total = laneTimes[suite] !== undefined ? laneTimes[suite] : 60;  // unknown suite: assume a minute
    for (let idx = 0; idx < k; ++idx) {
        units.push({ suite, idx, k, est: total / k });
    }
}
// Longest-processing-time first onto the least-loaded shard. Deterministic: ties keep manifest order.
units.sort((a, b) => b.est - a.est || a.suite.localeCompare(b.suite) || a.idx - b.idx);
const shards = Array.from({ length: shardCount }, () => ({ load: 0, units: [] }));
for (const u of units) {
    let best = 0;
    for (let s = 1; s < shardCount; ++s) { if (shards[s].load < shards[best].load) best = s; }
    shards[best].units.push(u);
    shards[best].load += u.est;
}

const describe = (u) => (u.k > 1 ? `${u.suite} [${u.idx + 1}/${u.k}]` : u.suite);
if (listOnly) {
    log(`lane: ${longLane ? 'long' : 'per-push'}  filter: ${kFilter}  shards: ${shardCount}`);
    shards.forEach((s, i) => {
        log(`shard ${i}: est ${Math.round(s.load)} s  ${s.units.map(describe).join(', ') || '(empty)'}`);
    });
    process.exit(0);
}

// --- Windows: mirror the POST_BUILD factory-preset install (cmake/KratePlugin.cmake) ----------------------
// krate_plugin_install_presets() copies plugins/<dir>/resources/presets[/<src>] into
// %PROGRAMDATA%\Krate Audio\<Target>[\<dest>] as a POST_BUILD step of the plugin target, and the preset tests
// read that tree back (Platform::getFactoryPresetDirectory). The test job never builds, so it copies the same
// trees from the checkout. Windows only, exactly like the cmake function.
function installWindowsPresets() {
    if (osName !== 'windows' || flag('--skip-presets')) return;
    const programData = process.env.PROGRAMDATA;
    if (!programData) { log('::warning::PROGRAMDATA is unset - factory presets not installed'); return; }
    const plugins = [
        ['iterum', 'Iterum'], ['disrumpo', 'Disrumpo'], ['ruinae', 'Ruinae'], ['innexus', 'Innexus'],
        ['gradus', 'Gradus'], ['membrum', 'Membrum', 'Kit Presets', 'Kits'], ['seraphis', 'Seraphis'], ['vorago', 'Vorago'],
    ];
    for (const [dir, target, srcSub, destSub] of plugins) {
        let src = path.join('plugins', dir, 'resources', 'presets');
        if (srcSub) src = path.join(src, srcSub);
        if (!fs.existsSync(src)) continue;
        let dest = path.join(programData, 'Krate Audio', target);
        if (destSub) dest = path.join(dest, destSub);
        try {
            fs.mkdirSync(dest, { recursive: true });
            fs.cpSync(src, dest, { recursive: true, force: true });
            log(`installed ${target} factory presets -> ${dest}`);
        } catch (e) {
            // A developer box may hold a protected ProgramData tree the build already filled; the CI runner
            // is elevated. Report and carry on: a missing tree fails the preset tests with a clear message.
            log(`::warning::could not install ${target} factory presets to ${dest}: ${e.message}`);
        }
    }
}

// --- run this shard --------------------------------------------------------------------------------------
const mine = shards[shardIndex].units;
log(`ci-test-shard: ${osName} shard ${shardIndex}/${shardCount}, ${longLane ? 'long' : 'per-push'} lane, filter ${kFilter}`);
log(`units (est ${Math.round(shards[shardIndex].load)} s): ${mine.map(describe).join(', ') || '(empty)'}`);
if (mine.length === 0) process.exit(0);

installWindowsPresets();

// Streams the executable's output as it arrives (a hung test still shows how far it got when the step
// timeout kills it) and returns it whole for the failure parse.
function runExe(exe, argv) {
    return new Promise((resolve) => {
        const chunks = [];
        const child = spawn(exe, argv, { stdio: ['ignore', 'pipe', 'pipe'] });
        const onData = (d) => { const s = d.toString(); emit(s); chunks.push(s); };
        child.stdout.on('data', onData);
        child.stderr.on('data', onData);
        child.on('error', (e) => resolve({ rc: 127, error: e.message, out: chunks.join('') }));
        child.on('close', (code, signal) => resolve({ rc: code === null ? 128 : code, signal, out: chunks.join('') }));
    });
}

// Catch2 prints absolute __FILE__ paths; annotations want them relative to the checkout.
const cwd = process.cwd();
function repoRelative(file) {
    const norm = (p) => p.replace(/\\/g, '/');
    const f = norm(file), root = norm(cwd).replace(/\/$/, '') + '/';
    const same = process.platform === 'win32' ? f.toLowerCase().startsWith(root.toLowerCase()) : f.startsWith(root);
    return same ? f.slice(root.length) : f;
}
// GitHub keeps at most ten error annotations per step; the rest are in the log artifact and the ::notice.
const kMaxErrorAnnotations = 10;
const annotation = (s) => s.replace(/%/g, '%25').replace(/\r/g, '%0D').replace(/\n/g, '%0A');
const property = (s) => annotation(s).replace(/:/g, '%3A').replace(/,/g, '%2C');

(async () => {
    let failed = 0;
    let errorAnnotations = 0;
    const results = [];
    const failedCases = [];
    for (const u of mine) {
        const exe = path.join(binDir, osName === 'windows' ? `${u.suite}.exe` : u.suite);
        const argv = [kFilter, '--skip-benchmarks'];
        // A Catch2 shard of a small [long] set can hold zero cases; so can a whole suite with no [long] cases.
        if (longLane || u.k > 1) argv.push('--allow-running-no-tests');
        if (u.k > 1) argv.push('--shard-count', String(u.k), '--shard-index', String(u.idx));
        log(`::group::${describe(u)}`);
        log(`$ ${exe} ${argv.join(' ')}`);
        const t0 = Date.now();
        let rc = 0;
        let out = '';
        if (!fs.existsSync(exe)) {
            log(`::error::${exe} is not in the test artifact`);
            rc = 127;
        } else if (!dryRun) {
            const r = await runExe(exe, argv);
            rc = r.rc;
            out = r.out;
            if (r.error) log(`::error::${describe(u)}: ${r.error}`);
            if (r.signal) log(`::error::${describe(u)} was killed by ${r.signal}`);
        }
        const wall = (Date.now() - t0) / 1000;
        log('::endgroup::');
        results.push({ name: describe(u), rc, wall });
        if (rc !== 0) {
            failed += 1;
            log(`::error::FAILED ${describe(u)} (exit ${rc}, ${wall.toFixed(0)} s)`);
            for (const f of parseCatch2Failures(out)) {
                const name = `${u.suite}: ${f.testCase}`;
                if (!failedCases.includes(name)) failedCases.push(name);
                if (errorAnnotations < kMaxErrorAnnotations) {
                    errorAnnotations += 1;
                    log(`::error file=${property(repoRelative(f.file))},line=${f.line},title=${property(name)}::${annotation(f.detail)}`);
                }
            }
        }
        if (wall > kWarnSeconds) {
            log(`::warning::${describe(u)} took ${wall.toFixed(0)} s - raise its entry in kSuiteShards (tools/ci-test-shard.js) and refresh tools/ci-test-times.json`);
        }
    }

    log('--- shard summary ---');
    for (const r of results) log(`${r.rc === 0 ? 'ok  ' : 'FAIL'} ${r.wall.toFixed(0).padStart(6)} s  ${r.name}`);
    log(`total ${results.reduce((a, r) => a + r.wall, 0).toFixed(0)} s, ${failed} failed`);
    if (failedCases.length > 0) {
        log(`::notice title=${failedCases.length} failed test case(s) on ${osName} shard ${shardIndex}::${annotation(failedCases.join('\n'))}`);
    }
    if (logFile !== null) fs.closeSync(logFile);
    process.exit(failed === 0 ? 0 : 1);
})();
