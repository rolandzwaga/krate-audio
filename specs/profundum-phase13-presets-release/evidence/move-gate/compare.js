#!/usr/bin/env node
// ==============================================================================
// compare.js -- Profundum Phase 13 FR-058 / SC-029 move gate verdict (plan §5.1 step 5)
// ==============================================================================
//   node specs/profundum-phase13-presets-release/evidence/move-gate/compare.js
//
// Inputs: move-gate/pre/ and move-gate/post/ as written by run-cases.sh <pre|post>.
// GREEN requires all of:
//   1. pre/ and post/ hold the same 16 case logs (every *.log except build.log);
//   2. each case log's last Catch2 summary (the non-empty lines after its last "=====" rule, the
//      "WALL ..." line run-cases.sh appends excluded) is identical pre vs post -- same assertion and
//      case counts -- and so is its exit code;
//   3. pre/evidence/** and post/evidence/** hold the same, non-empty file set and every pair is
//      byte-identical (no tolerance: the move is mechanical and the binary is the same toolchain);
//   4. post/warnings.txt reads 0;
//   5. evidence/move-check.js (run here, fresh) prints MOVE PURE.
// Prints `MOVE GATE GREEN` (exit 0) or `MOVE GATE RED <reason>` (exit 1); every reason is listed above it.
// ==============================================================================
'use strict';

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const GATE = __dirname;
const PRE = path.join(GATE, 'pre');
const POST = path.join(GATE, 'post');
const MOVE_CHECK = path.join(GATE, '..', 'move-check.js');
const EXPECTED_CASES = 16;

const reasons = [];
const red = (r) => {
    reasons.push(r);
    console.log(`RED: ${r}`);
};

function caseLogs(dir) {
    if (!fs.existsSync(dir)) return null;
    return fs
        .readdirSync(dir)
        .filter((f) => f.endsWith('.log') && f !== 'build.log')
        .sort();
}

function summaryOf(file) {
    const lines = fs.readFileSync(file, 'utf8').replace(/\r/g, '').split('\n');
    let rule = -1;
    for (let i = 0; i < lines.length; ++i) if (/^={20,}\s*$/.test(lines[i])) rule = i;
    const after = rule < 0 ? [] : lines.slice(rule + 1);
    const summary = after.filter((l) => l.trim() !== '' && !l.startsWith('WALL '));
    const wall = lines.filter((l) => l.startsWith('WALL ')).pop() || '';
    const m = /exit=(-?\d+)\s*$/.exec(wall);
    return { summary, exit: m ? Number(m[1]) : null };
}

function walk(dir, base = dir, out = []) {
    if (!fs.existsSync(dir)) return out;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) walk(p, base, out);
        else if (e.isFile()) out.push(path.relative(base, p).split(path.sep).join('/'));
    }
    return out.sort();
}

// ---- 1-2. case logs and their summaries ----
const preLogs = caseLogs(PRE);
const postLogs = caseLogs(POST);
if (!preLogs) red(`missing ${path.relative(process.cwd(), PRE)} (run run-cases.sh pre)`);
if (!postLogs) red(`missing ${path.relative(process.cwd(), POST)} (run run-cases.sh post)`);
if (preLogs && postLogs) {
    if (preLogs.length !== EXPECTED_CASES) red(`pre holds ${preLogs.length} case logs, expected ${EXPECTED_CASES}`);
    if (postLogs.length !== EXPECTED_CASES) red(`post holds ${postLogs.length} case logs, expected ${EXPECTED_CASES}`);
    const onlyPre = preLogs.filter((f) => !postLogs.includes(f));
    const onlyPost = postLogs.filter((f) => !preLogs.includes(f));
    if (onlyPre.length || onlyPost.length) red(`case log sets differ (pre only [${onlyPre}] post only [${onlyPost}])`);
    for (const f of preLogs.filter((x) => postLogs.includes(x))) {
        const a = summaryOf(path.join(PRE, f));
        const b = summaryOf(path.join(POST, f));
        if (!a.summary.length) red(`${f}: pre log has no Catch2 summary`);
        if (!b.summary.length) red(`${f}: post log has no Catch2 summary`);
        if (a.summary.join('\n') !== b.summary.join('\n'))
            red(`${f}: Catch2 summary differs (pre "${a.summary.join(' / ')}" post "${b.summary.join(' / ')}")`);
        if (a.exit !== b.exit) red(`${f}: exit code differs (pre ${a.exit} post ${b.exit})`);
        console.log(`case ${f.replace(/\.log$/, '')}: pre "${a.summary.join(' / ')}" exit=${a.exit} | post exit=${b.exit}`);
    }
}

// ---- 3. evidence copies ----
const preEv = walk(path.join(PRE, 'evidence'));
const postEv = walk(path.join(POST, 'evidence'));
if (!preEv.length) red('pre/evidence is empty');
if (!postEv.length) red('post/evidence is empty');
{
    const onlyPre = preEv.filter((f) => !postEv.includes(f));
    const onlyPost = postEv.filter((f) => !preEv.includes(f));
    if (onlyPre.length || onlyPost.length) red(`evidence file sets differ (pre only [${onlyPre}] post only [${onlyPost}])`);
    let identical = 0;
    for (const f of preEv.filter((x) => postEv.includes(x))) {
        const a = fs.readFileSync(path.join(PRE, 'evidence', f));
        const b = fs.readFileSync(path.join(POST, 'evidence', f));
        if (a.equals(b)) {
            ++identical;
            continue;
        }
        const la = a.toString('utf8').split('\n');
        const lb = b.toString('utf8').split('\n');
        let k = 0;
        while (k < la.length && k < lb.length && la[k] === lb[k]) ++k;
        red(`evidence ${f} differs at line ${k + 1} (pre "${(la[k] || '<eof>').trim()}" post "${(lb[k] || '<eof>').trim()}")`);
    }
    console.log(`evidence: ${identical}/${preEv.length} pre files byte-identical to post`);
}

// ---- 4. post warnings ----
const warnFile = path.join(POST, 'warnings.txt');
if (!fs.existsSync(warnFile)) red('post/warnings.txt missing');
else {
    const w = fs.readFileSync(warnFile, 'utf8').trim();
    if (w !== '0') red(`post build has ${w} warning lines (post/build.log)`);
}

// ---- 5. move purity ----
const mc = spawnSync(process.execPath, [MOVE_CHECK], { encoding: 'utf8' });
const mcLines = `${mc.stdout || ''}`.replace(/\r/g, '').split('\n').filter((l) => l.trim() !== '');
const mcLast = mcLines.length ? mcLines[mcLines.length - 1] : `(no output; ${mc.error ? mc.error.message : `exit ${mc.status}`})`;
console.log(`move-check.js: ${mcLast}`);
if (!mcLast.startsWith('MOVE PURE')) red(`move-check.js did not print MOVE PURE: ${mcLast}`);

if (reasons.length) {
    console.log(`MOVE GATE RED ${reasons[0]}`);
    process.exit(1);
}
console.log('MOVE GATE GREEN');
