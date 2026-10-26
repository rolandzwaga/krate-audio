#!/usr/bin/env node
// Profundum Phase 7, task T015: aggregate the HARVEST lines of T013's calibration cases into
// evidence/harvest-wide-floor.txt (plan S9.1 / S9.2; C-13 escalation). Read by paste-harvest.js wide-floor.
//
// Usage (from harvest.sh; --root <dir> overrides the repository root, the self-test uses it):
//   node aggregate-harvest.js wide-scale <BassStereoField_WideScaleHarvest log>
//   node aggregate-harvest.js side-floor [--wide-log <step-1 log>] [--expect-shards N] <shard logs...> <lever log>
//   node aggregate-harvest.js --self-test
//
// Lines parsed (bass_stereo_field_harvest_test.cpp header comment; T013 run-log):
//   HARVEST wide-scale <K> PASS|FAIL
//   HARVEST wide-scale-chosen <K>
//   STOP AND SURFACE: K_wide < 0.75 needed (plan C-13)
//   HARVEST side-floor-config <arm> K_wide <k> committed_floor <g>
//   HARVEST side-floor <arm> <g> PASS|FAIL <worst Q_sub dB> <worst rho_sub>
//   HARVEST side-floor-lever <g> PASS|FAIL
//   BSF_HARVEST_WIDE_ROW,... / BSF_HARVEST_FLOOR_ROW,... / BSF_HARVEST_FLOOR_SHARD,... / BSF_HARVEST_FLOOR_LEVER,...
//     (records, copied into the evidence file verbatim)
//
// wide-scale: every K in {1, 0.75} must appear exactly once; the chosen K is the largest PASS (cross-checked
//   against the wide-scale-chosen line). Writes the evidence file with the chosen K and the CURRENTLY COMMITTED
//   kSideFloorGain (read from bass_stereo_field.h) between BEGIN-INITIALIZER / END-INITIALIZER wide-floor.
// side-floor: every shard log must carry the side-floor-config line and every g in {0.71, 0.5, 0.35, 0.25, 0.18,
//   0.125, 0.1, 0.071, 0.05, 0.035, 0.03} exactly once; the config K_wide must equal the committed kWideOffsetScale (step 3 pasted it); the
//   lever must carry every g exactly once. The chosen g_floor is the largest candidate that is PASS in EVERY shard
//   and in the lever case. Rewrites the evidence file: the candidate tables, both chosen values, the commit, the date.
// Exit codes: 0 written; 1 malformed / inconsistent input (nothing written); 3 STOP AND SURFACE (the line and the
// per-candidate table are printed; nothing written). CommonJS, no dependencies.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const SPEC_EVIDENCE = 'specs/profundum-phase7-stereo-field/evidence';
const EVIDENCE_FILE = 'harvest-wide-floor.txt';
const HEADER = 'dsp/include/krate/dsp/systems/bass_stereo_field.h';
// G8-1 (2026-10-18): K_wide in {1, 0.75}; the floor candidates extend to 0.03 and the escalation floor is 0.03.
const WIDE_CANDIDATES = [1.0, 0.75];
const FLOOR_CANDIDATES = [0.71, 0.5, 0.35, 0.25, 0.18, 0.125, 0.1, 0.071, 0.05, 0.035, 0.03];
const K_WIDE_MIN = 0.75;  // plan C-13 escalation floor (G8-1)
const G_FLOOR_MIN = 0.03; // plan C-13 escalation floor (G8-1)
const DEFAULT_SHARDS = 14;
const STOP_WIDE = 'STOP AND SURFACE: K_wide < 0.75 needed (plan C-13)';
const STOP_FLOOR = 'STOP AND SURFACE: no g_floor >= 0.03 passes (plan C-13)';

class AggError extends Error {}
class StopError extends Error {
    constructor(line, table) {
        super(line);
        this.table = table;
    }
}

function fail(msg) {
    throw new AggError(msg);
}

const NUM_RE = /^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?$/;

function num(tok, where) {
    if (!NUM_RE.test(tok))
        fail(`${where}: "${tok}" is not a number`);
    const v = Number(tok);
    if (!Number.isFinite(v))
        fail(`${where}: "${tok}" is not finite`);
    return v;
}

// Matches a printed float (setprecision(6)) against a candidate.
function same(a, b) {
    return Math.abs(a - b) <= 1e-6 * Math.max(1, Math.abs(b));
}

function candidateIndex(list, v) {
    return list.findIndex((c) => same(v, c));
}

// A C++ float literal for a candidate value: 1 -> "1.0f", 0.75 -> "0.75f".
function floatLiteral(v) {
    let s = String(v);
    if (!/[.eE]/.test(s))
        s += '.0';
    return `${s}f`;
}

function readLines(file) {
    if (!fs.existsSync(file))
        fail(`${file}: not found`);
    return fs.readFileSync(file, 'utf8').replace(/\r/g, '').split('\n');
}

function readCommitted(root) {
    const abs = path.join(root, HEADER);
    if (!fs.existsSync(abs))
        fail(`${HEADER}: not found`);
    const text = fs.readFileSync(abs, 'utf8');
    const get = (name) => {
        const m = [...text.matchAll(new RegExp(`static constexpr float ${name} = ([^;\\s]+);`, 'g'))];
        if (m.length !== 1)
            fail(`${HEADER}: ${name} declared ${m.length} times, expected 1`);
        const tok = m[0][1];
        if (!/f$/.test(tok))
            fail(`${HEADER}: ${name} = ${tok} has no f suffix`);
        return { tok, v: num(tok.slice(0, -1), `${HEADER} ${name}`) };
    };
    return { floor: get('kSideFloorGain'), wide: get('kWideOffsetScale') };
}

function gitCommit(root) {
    const r = spawnSync('git', ['rev-parse', '--short', 'HEAD'], { cwd: root, encoding: 'utf8' });
    return r.status === 0 && r.stdout.trim() !== '' ? r.stdout.trim() : 'unknown';
}

// ---------------------------------------------------------------------------------------------------------------------
// Step 1: K_wide.

function parseWide(file) {
    const verdicts = new Map();   // candidate index -> PASS|FAIL
    const records = [];
    let chosenLine = null;
    let stop = false;
    for (const raw of readLines(file)) {
        const l = raw.trim();
        let m;
        if ((m = /^HARVEST wide-scale (\S+) (PASS|FAIL)$/.exec(l))) {
            const k = num(m[1], `${file} wide-scale`);
            const i = candidateIndex(WIDE_CANDIDATES, k);
            if (i < 0)
                fail(`${file}: wide-scale candidate ${m[1]} is not one of ${WIDE_CANDIDATES.join(', ')}`);
            if (verdicts.has(i))
                fail(`${file}: wide-scale candidate ${m[1]} printed twice`);
            verdicts.set(i, m[2]);
        } else if ((m = /^HARVEST wide-scale-chosen (\S+)$/.exec(l))) {
            if (chosenLine !== null)
                fail(`${file}: wide-scale-chosen printed twice`);
            chosenLine = num(m[1], `${file} wide-scale-chosen`);
        } else if (l.startsWith('STOP AND SURFACE')) {
            stop = true;
        } else if (l.startsWith('BSF_HARVEST_WIDE_ROW,')) {
            records.push(l);
        }
    }
    if (verdicts.size !== WIDE_CANDIDATES.length) {
        const missing = WIDE_CANDIDATES.filter((_, i) => !verdicts.has(i));
        fail(`${file}: wide-scale candidates missing: ${missing.join(', ')} (did the case run to the end?)`);
    }
    const passing = WIDE_CANDIDATES.filter((_, i) => verdicts.get(i) === 'PASS');
    const chosen = passing.length > 0 ? Math.max(...passing) : null;
    if (chosen !== null && stop)
        fail(`${file}: a STOP AND SURFACE line beside a passing candidate ${chosen}`);
    if (chosen === null && chosenLine !== null)
        fail(`${file}: wide-scale-chosen ${chosenLine} printed but no candidate passes`);
    if (chosen !== null && (chosenLine === null || !same(chosenLine, chosen)))
        fail(`${file}: wide-scale-chosen ${chosenLine} disagrees with the largest PASS ${chosen}`);
    const table = ['K_wide  verdict', ...WIDE_CANDIDATES.map((k, i) => `${String(k).padEnd(7)} ${verdicts.get(i)}`)];
    return { chosen, table, records };
}

function aggregateWide(root, logFile, isoDate) {
    const r = parseWide(logFile);
    if (r.chosen === null || r.chosen < K_WIDE_MIN)
        throw new StopError(STOP_WIDE, r.table);
    const committed = readCommitted(root);
    const commit = gitCommit(root);
    const out = [
        '# Profundum Phase 7 -- harvest-wide-floor (plan S9.1 / S9.2, C-13; tasks.md T015)',
        `# stage: step 1 (K_wide) harvested; g_floor is still the committed ${committed.floor.tok} until step 2 rewrites this file`,
        `# date ${isoDate} commit ${commit}`,
        'BEGIN-INITIALIZER wide-floor',
        `static constexpr float kSideFloorGain = ${committed.floor.tok};`,
        `static constexpr float kWideOffsetScale = ${floatLiteral(r.chosen)};`,
        'END-INITIALIZER wide-floor',
        '',
        `## Step 1: K_wide candidates at g_floor 0.5, WIDE dirty arm (source ${path.basename(logFile)})`,
        ...r.table,
        `chosen K_wide ${r.chosen}`,
        '',
        '## Step 1 records',
        ...r.records,
        '',
    ];
    return { text: out.join('\n'), summary: [...r.table, `chosen K_wide ${r.chosen} (committed floor ${committed.floor.tok})`] };
}

// ---------------------------------------------------------------------------------------------------------------------
// Step 2: g_floor.

function parseFloorLogs(files) {
    const arms = new Map();   // arm -> { config, verdicts: Map(idx -> {v, q, rho}), file }
    let lever = null;         // Map(idx -> PASS|FAIL)
    let leverFile = null;
    const records = [];
    const stops = [];
    for (const file of files) {
        for (const raw of readLines(file)) {
            const l = raw.trim();
            let m;
            if ((m = /^HARVEST side-floor-config (\S+) K_wide (\S+) committed_floor (\S+)$/.exec(l))) {
                const arm = m[1];
                if (arms.has(arm) && arms.get(arm).file !== file)
                    fail(`${file}: arm ${arm} also reported by ${arms.get(arm).file}`);
                if (arms.has(arm) && arms.get(arm).config)
                    fail(`${file}: side-floor-config for ${arm} printed twice`);
                const entry = arms.get(arm) || { verdicts: new Map(), file };
                entry.config = { kWide: num(m[2], `${file} ${arm} K_wide`), floor: num(m[3], `${file} ${arm} committed_floor`) };
                arms.set(arm, entry);
            } else if ((m = /^HARVEST side-floor (\S+) (\S+) (PASS|FAIL) (\S+) (\S+)$/.exec(l))) {
                const arm = m[1];
                const g = num(m[2], `${file} ${arm} g`);
                const i = candidateIndex(FLOOR_CANDIDATES, g);
                if (i < 0)
                    fail(`${file}: side-floor candidate ${m[2]} for ${arm} is not one of ${FLOOR_CANDIDATES.join(', ')}`);
                if (arms.has(arm) && arms.get(arm).file !== file)
                    fail(`${file}: arm ${arm} also reported by ${arms.get(arm).file}`);
                const entry = arms.get(arm) || { verdicts: new Map(), file };
                if (entry.verdicts.has(i))
                    fail(`${file}: side-floor ${arm} ${m[2]} printed twice`);
                entry.verdicts.set(i, { v: m[3], q: num(m[4], `${file} ${arm} Q`), rho: num(m[5], `${file} ${arm} rho`) });
                arms.set(arm, entry);
            } else if ((m = /^HARVEST side-floor-lever (\S+) (PASS|FAIL)$/.exec(l))) {
                const g = num(m[1], `${file} lever g`);
                const i = candidateIndex(FLOOR_CANDIDATES, g);
                if (i < 0)
                    fail(`${file}: side-floor-lever candidate ${m[1]} is not one of ${FLOOR_CANDIDATES.join(', ')}`);
                if (leverFile !== null && leverFile !== file)
                    fail(`${file}: side-floor-lever also reported by ${leverFile}`);
                leverFile = file;
                lever = lever || new Map();
                if (lever.has(i))
                    fail(`${file}: side-floor-lever ${m[1]} printed twice`);
                lever.set(i, m[2]);
            } else if (l.startsWith('STOP AND SURFACE')) {
                stops.push(`${path.basename(file)}: ${l}`);
            } else if (/^BSF_HARVEST_FLOOR_(ROW|SHARD|LEVER),/.test(l)) {
                records.push(l);
            }
        }
    }
    return { arms, lever, records, stops };
}

function floorTable(arms, lever) {
    const names = [...arms.keys()].sort();
    const rows = ['g_floor  shards_pass  lever  worst_Q_sub_dB  worst_rho_sub  failing'];
    const pass = [];
    FLOOR_CANDIDATES.forEach((g, i) => {
        const failing = names.filter((a) => arms.get(a).verdicts.get(i).v !== 'PASS');
        const leverV = lever.get(i);
        const q = Math.min(...names.map((a) => arms.get(a).verdicts.get(i).q));
        const rho = Math.min(...names.map((a) => arms.get(a).verdicts.get(i).rho));
        const ok = failing.length === 0 && leverV === 'PASS';
        pass.push(ok);
        const failText = [...failing, ...(leverV === 'PASS' ? [] : ['lever'])].join(' ') || '-';
        rows.push(`${String(g).padEnd(8)} ${`${names.length - failing.length}/${names.length}`.padEnd(12)} ${leverV.padEnd(6)} `
            + `${String(q).padEnd(15)} ${String(rho).padEnd(14)} ${failText}`);
    });
    const perArm = ['arm' + ' '.repeat(15) + FLOOR_CANDIDATES.map((g) => String(g).padEnd(6)).join(' ')];
    for (const a of names)
        perArm.push(a.padEnd(18) + FLOOR_CANDIDATES.map((_, i) => arms.get(a).verdicts.get(i).v.padEnd(6)).join(' '));
    perArm.push('lever'.padEnd(18) + FLOOR_CANDIDATES.map((_, i) => lever.get(i).padEnd(6)).join(' '));
    return { rows, perArm, pass };
}

function aggregateFloor(root, files, opts, isoDate) {
    if (files.length === 0)
        fail('side-floor: no logs given');
    const p = parseFloorLogs(files);
    if (p.stops.length > 0)
        fail(`side-floor: a log carries a STOP AND SURFACE line (${p.stops.join('; ')}); harvest.sh should have stopped`);
    if (p.arms.size !== opts.expectShards)
        fail(`side-floor: ${p.arms.size} shard arms reported (${[...p.arms.keys()].sort().join(', ')}), expected ${opts.expectShards}`);
    const committed = readCommitted(root);
    for (const [arm, e] of p.arms) {
        if (!e.config)
            fail(`side-floor: ${arm} (${e.file}) has no side-floor-config line`);
        if (!same(e.config.kWide, committed.wide.v))
            fail(`side-floor: ${arm} ran at K_wide ${e.config.kWide}, but the committed kWideOffsetScale is ${committed.wide.tok} (paste step 1 and rebuild first)`);
        const missing = FLOOR_CANDIDATES.filter((_, i) => !e.verdicts.has(i));
        if (missing.length > 0)
            fail(`side-floor: ${arm} (${e.file}) is missing candidates ${missing.join(', ')} (did the shard run to the end?)`);
    }
    if (p.lever === null)
        fail('side-floor: no side-floor-lever lines (BassStereoField_SideFloorLeverHarvest log not given?)');
    const leverMissing = FLOOR_CANDIDATES.filter((_, i) => !p.lever.has(i));
    if (leverMissing.length > 0)
        fail(`side-floor: lever is missing candidates ${leverMissing.join(', ')}`);

    const t = floorTable(p.arms, p.lever);
    const table = [...t.rows, '', ...t.perArm];
    if (committed.wide.v < K_WIDE_MIN)
        throw new StopError(STOP_WIDE, table);
    const idx = t.pass.findIndex((ok) => ok);   // candidates run largest first
    if (idx < 0 || FLOOR_CANDIDATES[idx] < G_FLOOR_MIN)
        throw new StopError(STOP_FLOOR, table);
    const g = FLOOR_CANDIDATES[idx];

    let wideSection = ['(step 1 log not given)'];
    if (opts.wideLog) {
        const w = parseWide(opts.wideLog);
        if (w.chosen === null || !same(w.chosen, committed.wide.v))
            fail(`side-floor: step 1 log ${opts.wideLog} chose ${w.chosen}, but the committed kWideOffsetScale is ${committed.wide.tok}`);
        wideSection = [...w.table, `chosen K_wide ${w.chosen}`, '', '## Step 1 records', ...w.records];
    }
    const commit = gitCommit(root);
    const out = [
        '# Profundum Phase 7 -- harvest-wide-floor (plan S9.1 / S9.2, C-13; tasks.md T015)',
        `# chosen pair: g_floor ${g} (kSideFloorGain), K_wide ${committed.wide.v} (kWideOffsetScale)`,
        `# date ${isoDate} commit ${commit} (harvested on this commit; step 2 ran at committed floor ${committed.floor.tok})`,
        'BEGIN-INITIALIZER wide-floor',
        `static constexpr float kSideFloorGain = ${floatLiteral(g)};`,
        `static constexpr float kWideOffsetScale = ${committed.wide.tok};`,
        'END-INITIALIZER wide-floor',
        '',
        '## Step 1: K_wide candidates at g_floor 0.5, WIDE dirty arm',
        ...wideSection,
        '',
        `## Step 2: g_floor candidates at K_wide ${committed.wide.v}, SC-001 grid at Gravity 0, depth 0 (${p.arms.size} shards + lever)`,
        ...table,
        `chosen g_floor ${g}`,
        '',
        '## Step 2 records',
        ...p.records,
        '',
    ];
    return { text: out.join('\n'), summary: [...table, `chosen g_floor ${g}, K_wide ${committed.wide.tok}`] };
}

// ---------------------------------------------------------------------------------------------------------------------

function run(argv) {
    let root = path.resolve(__dirname, '..', '..', '..', '..');
    let wideLog = null;
    let expectShards = DEFAULT_SHARDS;
    const pos = [];
    for (let i = 0; i < argv.length; ++i) {
        const a = argv[i];
        if (a === '--root' || a === '--wide-log' || a === '--expect-shards') {
            if (i + 1 >= argv.length)
                fail(`${a} needs a value`);
            const v = argv[++i];
            if (a === '--root')
                root = path.resolve(v);
            else if (a === '--wide-log')
                wideLog = v;
            else {
                expectShards = Number(v);
                if (!Number.isInteger(expectShards) || expectShards < 1)
                    fail(`--expect-shards ${v}: not a positive integer`);
            }
        } else {
            pos.push(a);
        }
    }
    const mode = pos.shift();
    const isoDate = new Date().toISOString().slice(0, 10);
    let r;
    if (mode === 'wide-scale') {
        if (pos.length !== 1)
            fail('wide-scale takes exactly one log');
        r = aggregateWide(root, pos[0], isoDate);
    } else if (mode === 'side-floor') {
        r = aggregateFloor(root, pos, { wideLog, expectShards }, isoDate);
    } else {
        fail(`unknown mode "${mode}"; expected wide-scale, side-floor or --self-test`);
    }
    const dest = path.join(root, SPEC_EVIDENCE, EVIDENCE_FILE);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.writeFileSync(dest, r.text);
    for (const l of r.summary)
        console.log(l);
    console.log(`aggregate-harvest: ${mode} wrote ${path.relative(root, dest).replace(/\\/g, '/')}`);
}

// ---------------------------------------------------------------------------------------------------------------------
// --self-test: synthetic logs (CRLF, like the Windows test exe) in a temp directory.

const ARMS = ['CleanSaw', 'CleanGrowl', ...['Dirty', 'Anchor'].flatMap((p) =>
    ['Tube', 'Fold', 'Rectify', 'Bite', 'Crush', 'Fracture'].map((a) => p + a))];

function synHeader(floor, wide) {
    return [
        '#pragma once',
        'class BassStereoField {',
        '    // HARVEST-BEGIN wide-floor',
        `    static constexpr float kSideFloorGain = ${floor};    // seed (plan S1 C-11), harvested in T015 (plan S9.1)`,
        `    static constexpr float kWideOffsetScale = ${wide};  // seed (plan S1 C-11), harvested in T015 (plan S9.1)`,
        '    // HARVEST-END wide-floor',
        '};',
        '',
    ].join('\n');
}

function synWideLog(verdicts) {
    const out = ['Filters: BassStereoField_WideScaleHarvest'];
    let chosen = null;
    WIDE_CANDIDATES.forEach((k, i) => {
        out.push(`HARVEST-CELL wide-scale Wide_Tube_E0_s0.5_fc60 pass 1 ms 12.5`);
        out.push(`BSF_HARVEST_WIDE_ROW,k,${k},pass,144,cells,144,worst_sub_q_db,-0.4`);
        out.push(`HARVEST wide-scale ${k} ${verdicts[i]}`);
        if (verdicts[i] === 'PASS' && chosen === null)
            chosen = k;
    });
    out.push(chosen !== null ? `HARVEST wide-scale-chosen ${chosen}` : STOP_WIDE);
    out.push('===============================================================================', 'All tests passed (2 assertions in 1 test case)', '');
    return out.join('\r\n');
}

// failAt(arm) -> number of top candidates that FAIL for that arm.
function synShardLog(arm, kWide, failTop) {
    const out = [`Filters: BassStereoField_SideFloorHarvest_${arm}`,
        `HARVEST side-floor-config ${arm} K_wide ${kWide} committed_floor 0.5`,
        `HARVEST-CELL side-floor ${arm}_Solid_E0_s0_fc60_c0 render_ms 900 restage_ms 300 total_ms 1200`,
        `BSF_HARVEST_FLOOR_SHARD,${arm},cells,216,mean_cell_ms,1200,shard_ms,259200`];
    FLOOR_CANDIDATES.forEach((g, i) => {
        const ok = i >= failTop;
        out.push(`BSF_HARVEST_FLOOR_ROW,${arm},g,${g},pass,${ok ? 216 : 200},cells,216`);
        out.push(`HARVEST side-floor ${arm} ${g} ${ok ? 'PASS' : 'FAIL'} ${ok ? -0.5 : -1.4} ${ok ? 0.995 : 0.97}`);
    });
    out.push('All tests passed (1 assertion in 1 test case)', '');
    return out.join('\r\n');
}

function synLeverLog(failTop) {
    const out = ['Filters: BassStereoField_SideFloorLeverHarvest'];
    FLOOR_CANDIDATES.forEach((g, i) => {
        out.push(`BSF_HARVEST_FLOOR_LEVER,g,${g},E1_s2m_db,4.1,E1_rho,0.2`);
        out.push(`HARVEST side-floor-lever ${g} ${i >= failTop ? 'PASS' : 'FAIL'}`);
    });
    out.push('All tests passed', '');
    return out.join('\r\n');
}

function selfTest() {
    const base = fs.mkdtempSync(path.join(os.tmpdir(), 'aggregate-harvest-p7-'));
    const cases = [];
    const check = (label, ok, detail) => {
        cases.push({ label, ok });
        console.log(`${ok ? 'PASS' : 'FAIL'} ${label}${detail ? ` -- ${detail}` : ''}`);
    };
    let n = 0;
    const tree = (floor, wide) => {
        const dir = path.join(base, `case${n++}`);
        fs.mkdirSync(path.join(dir, path.dirname(HEADER)), { recursive: true });
        fs.writeFileSync(path.join(dir, HEADER), synHeader(floor, wide));
        fs.mkdirSync(path.join(dir, 'logs'), { recursive: true });
        return dir;
    };
    const log = (dir, name, text) => {
        const f = path.join(dir, 'logs', name);
        fs.writeFileSync(f, text);
        return f;
    };
    const runChild = (dir, args) => {
        const r = spawnSync(process.execPath, [__filename, '--root', dir, ...args], { encoding: 'utf8' });
        return { status: r.status, out: (r.stdout || '') + (r.stderr || '') };
    };
    const evidence = (dir) => {
        const f = path.join(dir, SPEC_EVIDENCE, EVIDENCE_FILE);
        return fs.existsSync(f) ? fs.readFileSync(f, 'utf8') : null;
    };
    const lastLine = (out) => out.trim().split('\n').pop();
    // Writes the 14 shard logs + the lever log; failTop(arm) gives that arm's failing-top count.
    const floorLogs = (dir, kWide, failTop, leverFailTop, arms = ARMS) => [
        ...arms.map((a) => log(dir, `shard-${a}.log`, synShardLog(a, kWide, failTop(a)))),
        log(dir, 'lever.log', synLeverLog(leverFailTop)),
    ];

    try {
        // ---- wide-scale ----
        {
            const dir = tree('0.5f', '1.0f');
            const r = runChild(dir, ['wide-scale', log(dir, 'wide.log', synWideLog(['PASS', 'PASS', 'PASS']))]);
            const e = evidence(dir);
            const ok = r.status === 0 && e !== null && e.includes('static constexpr float kSideFloorGain = 0.5f;\n')
                && e.includes('static constexpr float kWideOffsetScale = 1.0f;\n') && !e.includes('STOP AND SURFACE');
            check('wide-scale all pass -> exit 0, K_wide 1.0f, committed floor 0.5f kept', ok, `exit ${r.status}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const r = runChild(dir, ['wide-scale', log(dir, 'wide.log', synWideLog(['FAIL', 'PASS', 'PASS']))]);
            const e = evidence(dir);
            const ok = r.status === 0 && e !== null && e.includes('static constexpr float kWideOffsetScale = 0.75f;\n');
            check('wide-scale top candidate failing -> exit 0, K_wide 0.75f', ok, `exit ${r.status}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const r = runChild(dir, ['wide-scale', log(dir, 'wide.log', synWideLog(['FAIL', 'FAIL', 'FAIL']))]);
            const ok = r.status === 3 && r.out.includes(STOP_WIDE) && evidence(dir) === null;
            check('wide-scale no candidate passing -> exit 3, STOP AND SURFACE printed, nothing written', ok, `exit ${r.status}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const text = synWideLog(['PASS', 'PASS']).replace('HARVEST wide-scale 0.75 PASS\r\n', '');
            const r = runChild(dir, ['wide-scale', log(dir, 'wide.log', text)]);
            check('wide-scale candidate missing -> exit 1, nothing written', r.status === 1 && evidence(dir) === null,
                `exit ${r.status}: ${lastLine(r.out)}`);
        }
        // ---- side-floor ----
        {
            const dir = tree('0.5f', '0.75f');
            const wide = log(dir, 'wide.log', synWideLog(['FAIL', 'PASS', 'PASS']));
            const r = runChild(dir, ['side-floor', '--wide-log', wide, '--expect-shards', '14', ...floorLogs(dir, 0.75, () => 0, 0)]);
            const e = evidence(dir);
            const ok = r.status === 0 && e !== null && e.includes('static constexpr float kSideFloorGain = 0.71f;\n')
                && e.includes('static constexpr float kWideOffsetScale = 0.75f;\n') && e.includes('chosen g_floor 0.71')
                && e.includes('chosen K_wide 0.75') && /# date \d{4}-\d{2}-\d{2} commit \S+/.test(e);
            check('side-floor all pass -> exit 0, g_floor 0.71f, K_wide kept, both tables, commit and date', ok, `exit ${r.status}`);
            // The written file is what paste-harvest.js consumes: paste it into the synthetic header.
            const paste = spawnSync(process.execPath, [path.join(__dirname, 'paste-harvest.js'), '--root', dir, 'wide-floor'],
                { encoding: 'utf8' });
            const h = fs.readFileSync(path.join(dir, HEADER), 'utf8');
            check('side-floor evidence -> paste-harvest.js exit 0, header carries the pair',
                paste.status === 0 && h.includes('    static constexpr float kSideFloorGain = 0.71f;\n')
                && h.includes('    static constexpr float kWideOffsetScale = 0.75f;\n'), `paste exit ${paste.status}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const r = runChild(dir, ['side-floor', ...floorLogs(dir, 1, (a) => (a === 'AnchorFracture' ? 1 : 0), 0)]);
            const e = evidence(dir);
            const ok = r.status === 0 && e !== null && e.includes('static constexpr float kSideFloorGain = 0.5f;\n')
                && /0\.71\s+13\/14\s+PASS.*AnchorFracture/.test(e);
            check('side-floor one shard failing the top candidate -> exit 0, g_floor 0.5f, failing arm named', ok, `exit ${r.status}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const r = runChild(dir, ['side-floor', ...floorLogs(dir, 1, () => 0, 2)]);
            const e = evidence(dir);
            const ok = r.status === 0 && e !== null && e.includes('static constexpr float kSideFloorGain = 0.35f;\n');
            check('side-floor lever failing the top two candidates -> exit 0, g_floor 0.35f', ok, `exit ${r.status}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const r = runChild(dir, ['side-floor', ...floorLogs(dir, 1, (a) => (a.startsWith('Dirty') ? FLOOR_CANDIDATES.length : 0), 0)]);
            const ok = r.status === 3 && r.out.includes(STOP_FLOOR) && r.out.includes('g_floor  shards_pass') && evidence(dir) === null;
            check('side-floor no candidate passing -> exit 3, STOP AND SURFACE + table printed, nothing written', ok, `exit ${r.status}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const r = runChild(dir, ['side-floor', ...floorLogs(dir, 1, () => 0, 0, ARMS.slice(1))]);
            check('side-floor 13 of 14 shards -> exit 1, nothing written', r.status === 1 && evidence(dir) === null,
                `exit ${r.status}: ${lastLine(r.out)}`);
        }
        {
            const dir = tree('0.5f', '0.75f');
            const r = runChild(dir, ['side-floor', ...floorLogs(dir, 1, () => 0, 0)]);
            check('side-floor shards ran at K_wide 1 but 0.75f committed -> exit 1, nothing written',
                r.status === 1 && evidence(dir) === null, `exit ${r.status}: ${lastLine(r.out)}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const logs = floorLogs(dir, 1, () => 0, 0).slice(0, -1);
            const r = runChild(dir, ['side-floor', ...logs]);
            check('side-floor without the lever log -> exit 1, nothing written', r.status === 1 && evidence(dir) === null,
                `exit ${r.status}: ${lastLine(r.out)}`);
        }
        {
            const dir = tree('0.5f', '1.0f');
            const logs = floorLogs(dir, 1, () => 0, 0);
            fs.writeFileSync(logs[0], fs.readFileSync(logs[0], 'utf8').replace(/HARVEST side-floor CleanSaw 0\.1 PASS[^\r]*\r\n/, ''));
            const r = runChild(dir, ['side-floor', ...logs]);
            check('side-floor shard missing a candidate (truncated log) -> exit 1, nothing written',
                r.status === 1 && evidence(dir) === null, `exit ${r.status}: ${lastLine(r.out)}`);
        }
    } finally {
        fs.rmSync(base, { recursive: true, force: true });
    }
    const failed = cases.filter((c) => !c.ok).length;
    console.log(`self-test: ${cases.length - failed}/${cases.length} passed`);
    return failed === 0 ? 0 : 1;
}

function main(argv) {
    if (argv.includes('--self-test'))
        return selfTest();
    try {
        run(argv);
        return 0;
    } catch (err) {
        if (err instanceof StopError) {
            for (const l of err.table)
                console.log(l);
            console.log(err.message);
            return 3;
        }
        if (err instanceof AggError) {
            console.error(`aggregate-harvest: ${err.message}`);
            return 1;
        }
        throw err;
    }
}

process.exitCode = main(process.argv.slice(2));
