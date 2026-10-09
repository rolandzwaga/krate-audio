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
//   --skip-presets  do not mirror the Windows factory-preset install (local runs: the build already did it)
'use strict';
const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

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
    console.log(`ci-test-shard: no manifest at ${manifestPath} - nothing to run`);
    process.exit(0);
}
const suites = fs.readFileSync(manifestPath, 'utf8').split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
if (suites.length === 0) {
    console.log('ci-test-shard: empty manifest - nothing to run');
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
    console.log(`lane: ${longLane ? 'long' : 'per-push'}  filter: ${kFilter}  shards: ${shardCount}`);
    shards.forEach((s, i) => {
        console.log(`shard ${i}: est ${Math.round(s.load)} s  ${s.units.map(describe).join(', ') || '(empty)'}`);
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
    if (!programData) { console.log('::warning::PROGRAMDATA is unset - factory presets not installed'); return; }
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
            console.log(`installed ${target} factory presets -> ${dest}`);
        } catch (e) {
            // A developer box may hold a protected ProgramData tree the build already filled; the CI runner
            // is elevated. Report and carry on: a missing tree fails the preset tests with a clear message.
            console.log(`::warning::could not install ${target} factory presets to ${dest}: ${e.message}`);
        }
    }
}

// --- run this shard --------------------------------------------------------------------------------------
const mine = shards[shardIndex].units;
console.log(`ci-test-shard: ${osName} shard ${shardIndex}/${shardCount}, ${longLane ? 'long' : 'per-push'} lane, filter ${kFilter}`);
console.log(`units (est ${Math.round(shards[shardIndex].load)} s): ${mine.map(describe).join(', ') || '(empty)'}`);
if (mine.length === 0) process.exit(0);

installWindowsPresets();

let failed = 0;
const results = [];
for (const u of mine) {
    const exe = path.join(binDir, osName === 'windows' ? `${u.suite}.exe` : u.suite);
    const argv = [kFilter, '--skip-benchmarks'];
    // A Catch2 shard of a small [long] set can hold zero cases; so can a whole suite with no [long] cases.
    if (longLane || u.k > 1) argv.push('--allow-running-no-tests');
    if (u.k > 1) argv.push('--shard-count', String(u.k), '--shard-index', String(u.idx));
    console.log(`::group::${describe(u)}`);
    console.log(`$ ${exe} ${argv.join(' ')}`);
    const t0 = Date.now();
    let rc = 0;
    if (!fs.existsSync(exe)) {
        console.log(`::error::${exe} is not in the test artifact`);
        rc = 127;
    } else if (!dryRun) {
        const r = spawnSync(exe, argv, { stdio: 'inherit' });
        rc = r.status === null ? 128 : r.status;
        if (r.error) { console.log(`::error::${describe(u)}: ${r.error.message}`); rc = 127; }
    }
    const wall = (Date.now() - t0) / 1000;
    console.log('::endgroup::');
    results.push({ name: describe(u), rc, wall });
    if (rc !== 0) { failed += 1; console.log(`::error::FAILED ${describe(u)} (exit ${rc}, ${wall.toFixed(0)} s)`); }
    if (wall > kWarnSeconds) {
        console.log(`::warning::${describe(u)} took ${wall.toFixed(0)} s - raise its entry in kSuiteShards (tools/ci-test-shard.js) and refresh tools/ci-test-times.json`);
    }
}

console.log('--- shard summary ---');
for (const r of results) console.log(`${r.rc === 0 ? 'ok  ' : 'FAIL'} ${r.wall.toFixed(0).padStart(6)} s  ${r.name}`);
console.log(`total ${results.reduce((a, r) => a + r.wall, 0).toFixed(0)} s, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
