#!/usr/bin/env node
// Profundum Phase 9, task T026: the SC-030 shared-header diff check (FR-084, SC-030, plan S7, R-D).
//
// Run from anywhere:   node specs/profundum-phase9-voice-engine/evidence/close/sc030-diff.js
//
// 1. `git diff --name-only 0ae33b87 -- dsp/include dsp/src plugins tests/test_helpers` (tracked changes against the
//    phase base; brand-new Phase 9 headers are untracked/added files, not shared-header changes) must list only the
//    ruled headers. The ruled set is spec.md's SC-030 row (mass_dynamics.h; retrigger_declick.h, R-B; the Group 10b
//    / T029 / T032 headers, R-A) plus `systems/profundum_core.h`, which FR-084 (3) names (T032 append-only
//    `setControlsWithShape`) and the SC-030 row omits: that discrepancy is printed as a NOTE for comply to reconcile.
//    `core/db_utils.h` is a hard red (R-D: no change without a user ruling). Any other file is a red.
// 2. `git diff -U0 0ae33b87 -- dsp/include/krate/dsp/systems/mass_dynamics.h`: every REMOVED line must belong to
//    the two documented branch points of plan S7 -- `prepare` and `processBlock` (the T029 amendment moved the
//    mechanism into BassCompressor's sidechain, so the branch points of either class in that header count). Each
//    hunk is printed with its enclosing function (from the 0ae33b87 text) and a classification; each removed code
//    line is annotated "relocated" (its trimmed text reappears verbatim among the file's added lines) or
//    "rewritten". A removed line in any other function or at class scope is a RECORD (main loop, 2026-10-21, R-A:
//    the gate on this header is the Phase 6 consumer suites, FR-084; the T029 shape reads scRate_ in
//    BassCompressor::configureDetector and moves processBlock's loop body into a helper), never a red here.
// 3. The other ruled headers are listed with their hunk and +/- counts; their gate is the consumer suites (T025),
//    not this script.
// Output: stdout and evidence/run-log/close/sc030-diff.log; last line "SC030 GREEN" or "SC030 RED (...)".
'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const REPO = path.resolve(__dirname, '..', '..', '..', '..');
const SPEC = 'specs/profundum-phase9-voice-engine';
const LOG = path.join(REPO, SPEC, 'evidence', 'run-log', 'close', 'sc030-diff.log');
const BASE = '0ae33b87';
const MASS = 'dsp/include/krate/dsp/systems/mass_dynamics.h';
const FORBIDDEN = { 'dsp/include/krate/dsp/core/db_utils.h': 'R-D: no change to core/db_utils.h without a user ruling' };
const RULED = {
    [MASS]: 'FR-084 (1) FR-082 opt-in (Q4), plan S7 as amended by T029 / T032',
    'dsp/include/krate/dsp/primitives/retrigger_declick.h': 'FR-084 (2) R-B prepare(fs, declickMs)',
    'dsp/include/krate/dsp/primitives/polyphase_halfband_oversampler.h': 'FR-084 (3) R-A T029',
    'dsp/include/krate/dsp/primitives/tanh_adaa.h': 'FR-084 (3) R-A T029',
    'dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h': 'FR-084 (3) R-A T029 / T032',
    'dsp/include/krate/dsp/processors/oversampled_residual_path.h': 'FR-084 (3) R-A T029',
    'dsp/include/krate/dsp/systems/shape_morph.h': 'FR-084 (3) R-A T029 / T032',
    'dsp/include/krate/dsp/processors/true_peak_limiter.h': 'FR-084 (3) R-A T029 setHalfbandDetector (not the C-1 accessor)',
    'dsp/include/krate/dsp/processors/harmonic_fold.h': 'FR-084 (3) R-A T029 / T032',
    'dsp/include/krate/dsp/processors/dynamics_processor.h': 'FR-084 (3) R-A T032',
    'dsp/include/krate/dsp/processors/period_peak_detector.h': 'FR-084 (3) R-A T032',
    'dsp/include/krate/dsp/processors/keytracked_tilt.h': 'FR-084 (3) R-A T032',
    'dsp/include/krate/dsp/processors/spectral_shape_recipe.h': 'FR-084 (3) R-A T032',
    'dsp/include/krate/dsp/systems/profundum_core.h': 'FR-084 (3) R-A T032 setControlsWithShape',
};
const SC030_ROW_OMITS = new Set(['dsp/include/krate/dsp/systems/profundum_core.h']);
const BRANCH_FUNCTIONS = new Set(['prepare', 'processBlock']);

const out = [];
const say = (s = '') => { out.push(s); console.log(s); };
const git = (args) => execFileSync('git', args, { cwd: REPO, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
                                                  stdio: ['ignore', 'pipe', 'ignore'] });

// Strip // and /* */ comments and string / char literals from one line; returns [text, inBlockComment].
function stripLine(s, inBlockComment) {
    let t = '';
    for (let k = 0; k < s.length; ++k) {
        if (inBlockComment) {
            if (s[k] === '*' && s[k + 1] === '/') { inBlockComment = false; ++k; }
            continue;
        }
        if (s[k] === '/' && s[k + 1] === '*') { inBlockComment = true; ++k; continue; }
        if (s[k] === '/' && s[k + 1] === '/') break;
        if (s[k] === '"' || s[k] === '\'') {
            const q = s[k];
            ++k;
            while (k < s.length && s[k] !== q) { if (s[k] === '\\') ++k; ++k; }
            t += q + q;
            continue;
        }
        t += s[k];
    }
    return [t, inBlockComment];
}

// Enclosing function per line of a C++ header (1-based index), by brace tracking on comment/literal-stripped text.
function enclosingFunctions(text) {
    const lines = text.split(/\r?\n/);
    const result = new Array(lines.length + 1).fill(null);
    const stack = []; // { kind: 'fn' | 'class' | 'block', name, cls }
    let inBlockComment = false;
    let pending = null; // { kind, name }
    const ctl = /^(if|for|while|switch|return|else|do|catch|sizeof|static_assert|decltype|alignas|noexcept|static_cast|reinterpret_cast|const_cast)$/;
    const currentClass = () => {
        for (let i = stack.length - 1; i >= 0; --i) if (stack[i].kind === 'class') return stack[i].name;
        return null;
    };
    const currentFn = () => {
        for (let i = stack.length - 1; i >= 0; --i) if (stack[i].kind === 'fn') return stack[i];
        return null;
    };
    for (let i = 0; i < lines.length; ++i) {
        let t;
        [t, inBlockComment] = stripLine(lines[i], inBlockComment);
        if (!currentFn()) {
            const mc = /^\s*(?:template\s*<[^>]*>\s*)?(class|struct)\s+([A-Za-z_]\w*)[^;]*$/.exec(t);
            const mf = /([A-Za-z_~]\w*)\s*\(/.exec(t.replace(/^\s*(\[\[[^\]]*\]\]\s*)+/, ''));
            if (mc) pending = { kind: 'class', name: mc[2] };
            else if (mf && !ctl.test(mf[1]) && !/;\s*$/.test(t) && !/^\s*(#|:|,)/.test(t)) pending = { kind: 'fn', name: mf[1] };
        }
        let lineFn = currentFn();
        for (const ch of t) {
            if (ch === '{') {
                if (pending && !currentFn()) stack.push({ kind: pending.kind, name: pending.name, cls: currentClass() });
                else stack.push({ kind: 'block' });
                pending = null;
                if (!lineFn) lineFn = currentFn();
            } else if (ch === '}') {
                stack.pop();
            } else if (ch === ';' && pending && pending.kind === 'fn' && !currentFn()) {
                pending = null; // a declaration, not a definition
            }
        }
        const cls = currentClass();
        result[i + 1] = lineFn ? `${lineFn.cls ? lineFn.cls + '::' : ''}${lineFn.name}`
                               : (cls ? `${cls} (class scope)` : '(file scope)');
    }
    return result;
}

const reds = [];
say(`=== T026 SC-030 diff check ${new Date().toISOString()} base ${BASE} HEAD ${git(['rev-parse', '--short', 'HEAD']).trim()}`);
say();

// ---- 1. Name list ---------------------------------------------------------------------------------------------
const names = git(['diff', '--name-only', BASE, '--', 'dsp/include', 'dsp/src', 'plugins', 'tests/test_helpers'])
    .split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
say(`== 1. git diff --name-only ${BASE} -- dsp/include dsp/src plugins tests/test_helpers  (${names.length} file(s))`);
for (const n of names) {
    if (FORBIDDEN[n]) {
        say(`  RED      ${n}   [${FORBIDDEN[n]}]`);
        reds.push(`forbidden ${n}`);
    } else if (RULED[n]) {
        say(`  ruled    ${n}   [${RULED[n]}]`);
        if (SC030_ROW_OMITS.has(n))
            say('           NOTE: named by FR-084 (3) but missing from spec.md\'s SC-030 row; comply reconciles the row.');
    } else {
        say(`  RED      ${n}   [not a ruled exception]`);
        reds.push(`unruled ${n}`);
    }
}
for (const f of Object.keys(FORBIDDEN)) if (!names.includes(f)) say(`  absent   ${f}   (${FORBIDDEN[f]})`);
if (!names.includes(MASS)) say(`  NOTE: ${MASS} is not in the diff (the FR-082 opt-in is expected there).`);
say();

// ---- 2. mass_dynamics.h removed lines --------------------------------------------------------------------------
say(`== 2. git diff -U0 ${BASE} -- ${MASS}`);
if (names.includes(MASS)) {
    const fnOf = enclosingFunctions(git(['show', `${BASE}:${MASS}`]));
    const diff = git(['diff', '-U0', BASE, '--', MASS]).split(/\r?\n/);
    const hunks = [];
    let h = null;
    for (const l of diff) {
        const m = /^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(.*)$/.exec(l);
        if (m) {
            h = { oldStart: +m[1], oldCount: m[2] === undefined ? 1 : +m[2], newStart: +m[3],
                  newCount: m[4] === undefined ? 1 : +m[4], removed: [], added: [] };
            hunks.push(h);
        } else if (h && l.startsWith('-') && !l.startsWith('---')) {
            h.removed.push(l.slice(1));
        } else if (h && l.startsWith('+') && !l.startsWith('+++')) {
            h.added.push(l.slice(1));
        }
    }
    const addedSet = new Set(hunks.flatMap((x) => x.added).map((a) => a.trim()).filter(Boolean));
    const isCode = (l) => {
        const t = l.trim();
        return t !== '' && !t.startsWith('//') && !t.startsWith('*') && !t.startsWith('/*');
    };
    let removedTotal = 0;
    let addedTotal = 0;
    hunks.forEach((x, idx) => {
        removedTotal += x.removed.length;
        addedTotal += x.added.length;
        const header = `  hunk ${idx + 1}: -${x.oldStart},${x.oldCount} +${x.newStart},${x.newCount}`
                     + `  (+${x.added.length} / -${x.removed.length})`;
        if (x.removed.length === 0) { say(`${header}  ADDITION`); return; }
        const fns = new Set();
        x.removed.forEach((_, k) => fns.add(fnOf[x.oldStart + k] || '(unknown)'));
        const bad = [...fns].filter((f) => !BRANCH_FUNCTIONS.has(f.split('::').pop()));
        const verdict = bad.length === 0 ? 'BRANCH POINT'
                                         : 'RECORD: removed lines outside prepare / processBlock (R-A, gated by the Phase 6 suites)';
        say(`${header}  in ${[...fns].join(', ')}  ${verdict}`);
        x.removed.forEach((r, k) => {
            const tag = !isCode(r) ? 'comment/blank' : (addedSet.has(r.trim()) ? 'relocated' : 'rewritten');
            say(`      -${String(x.oldStart + k).padStart(4)} [${tag}] ${r.trimEnd()}`);
        });
        if (bad.length) say(`      record: hunk ${idx + 1} touches ${bad.join(', ')} (consumer-suite gate, FR-084)`);
    });
    say(`  total: ${hunks.length} hunk(s), +${addedTotal} / -${removedTotal} lines`);
} else {
    say('  (not in the diff)');
}
say();

// ---- 3. The other ruled headers ----------------------------------------------------------------------------------
say('== 3. Other ruled headers (gate: their consumer suites under the change-set rule, T025)');
for (const n of names) {
    if (n === MASS || !RULED[n]) continue;
    const d = git(['diff', '-U0', BASE, '--', n]).split(/\r?\n/);
    const hunks = d.filter((l) => l.startsWith('@@')).length;
    const plus = d.filter((l) => l.startsWith('+') && !l.startsWith('+++')).length;
    const minus = d.filter((l) => l.startsWith('-') && !l.startsWith('---')).length;
    say(`  ${n}: ${hunks} hunk(s), +${plus} / -${minus}`);
}
say();

say(reds.length === 0 ? 'SC030 GREEN' : `SC030 RED (${reds.join('; ')})`);
fs.mkdirSync(path.dirname(LOG), { recursive: true });
fs.writeFileSync(LOG, out.join('\n') + '\n');
process.exit(reds.length === 0 ? 0 : 1);
