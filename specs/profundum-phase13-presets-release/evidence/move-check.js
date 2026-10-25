#!/usr/bin/env node
// ==============================================================================
// move-check.js -- Profundum Phase 13 FR-057 move purity check (plan §5.1 steps 2 and 2b; SC-029)
// ==============================================================================
//   node specs/profundum-phase13-presets-release/evidence/move-check.js
//
// Source  : git show e6095b24:dsp/tests/unit/systems/profundum_engine_audibility_test.cpp (the Phase 9 TU)
// New     : dsp/tests/unit/systems/profundum_audibility_readers.h (header) and the edited TU
// Manifest: specs/profundum-phase13-presets-release/evidence/move-lifts.json (written by T003)
//
// Normalisation applied to both sides (the mechanical edits 1-3 of plan §4.1):
//   - CR and trailing whitespace stripped;
//   - a leading `inline ` (after indentation, attributes or a template head) removed;
//   - in the header's moved block: `namespace Krate::DSP::ProfundumAudibility {` -> `namespace {`, its closing
//     `}  // namespace Krate::DSP::ProfundumAudibility` -> `}  // namespace`, and the P-1 alias
//     `namespace auPt = Krate::DSP::ProfundumAudibility::render;` -> `namespace auPt = Krate::DSP::ProfundumTest;`
//     (counted as `changed`; exactly 1 is required).
//
// Layout required:
//   TU     = source lines before the include block (header comment), unchanged
//            + only these lines: blank, #include <catch2/catch_test_macros.hpp>, #include "profundum_audibility_readers.h",
//              using namespace Krate::DSP;, using namespace Krate::DSP::ProfundumAudibility; (all four required)
//            + source lines after the first anonymous namespace's close, unchanged (auRunShard + the 16 TEST_CASEs).
//   Header = prologue (#pragma once, comments, blanks, the source include block contiguous and unchanged, and the
//            render shim inside `namespace Krate::DSP::ProfundumAudibility::render { ... }`)
//            + the moved block, which must equal the source's first anonymous namespace line by line except
//              inside the lift regions of the manifest.
//
// Manifest (move-lifts.json): an array (or { "lifts": [...] }) of exactly L1..L11:
//   { "id": "L1", "function": "auCellOnsetESub" | [names...],
//     "source": [from, to] | [[from, to], line, ...],   // Phase 9 TU lines at e6095b24 the lift replaces
//     "calls":  [[from, to], ...],                       // optional: further e6095b24 lines rewritten as call
//                                                        //   sites (e.g. the four dropCell( calls of L4)
//     "header": [from, to] | [[from, to], ...] }         // header lines holding the lifted function(s)
//   A two-number array is one inclusive range. Header-range lines are removed from the header's moved block;
//   every source/calls line becomes a wildcard that may be replaced, in place, by any header text (the call
//   site). Every other line must match. Any difference outside the manifest -> MOVE IMPURE (a lift region not
//   in the manifest). Per lift, the source-vs-lifted text diff and the call-site replacement text are written
//   to evidence/move-gate/lifts.diff for review; the FR-058 run (move-gate/compare.js) proves the behaviour.
//
// Step 2b: `git status --porcelain -uall -- dsp/` must list exactly
//   " M dsp/tests/unit/systems/profundum_engine_audibility_test.cpp" and
//   "?? dsp/tests/unit/systems/profundum_audibility_readers.h"; the output is written to evidence/move-gate/paths.txt.
//
// Prints `MOVE PURE changed=1 lifted=11` (exit 0) or `MOVE IMPURE <first difference>` (exit 1).
// ==============================================================================
'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const BASE = 'e6095b24';
const TU_REL = 'dsp/tests/unit/systems/profundum_engine_audibility_test.cpp';
const HDR_REL = 'dsp/tests/unit/systems/profundum_audibility_readers.h';
const EVID = __dirname;
const GATE = path.join(EVID, 'move-gate');
const MANIFEST = path.join(EVID, 'move-lifts.json');
const LIFTS_DIFF = path.join(GATE, 'lifts.diff');
const PATHS_TXT = path.join(GATE, 'paths.txt');
const EXPECTED_LIFTS = ['L1', 'L2', 'L3', 'L4', 'L5', 'L6', 'L7', 'L8', 'L9', 'L10', 'L11'];
const EXPECTED_PATHS = [
    ' M dsp/tests/unit/systems/profundum_engine_audibility_test.cpp',
    '?? dsp/tests/unit/systems/profundum_audibility_readers.h',
];

const NS_OPEN_NEW = 'namespace Krate::DSP::ProfundumAudibility {';
const NS_CLOSE_RE = /^}\s*\/\/\s*namespace(\s+Krate::DSP::ProfundumAudibility)?$/;
const SRC_CLOSE_RE = /^}\s*\/\/\s*namespace$/;
const SHIM_OPEN = 'namespace Krate::DSP::ProfundumAudibility::render {';
const SHIM_CLOSE_RE = /^}\s*\/\/\s*namespace\s+Krate::DSP::ProfundumAudibility::render$/;
const ALIAS_OLD = 'namespace auPt = Krate::DSP::ProfundumTest;';
const ALIAS_NEW = 'namespace auPt = Krate::DSP::ProfundumAudibility::render;';
const READERS_INCLUDE = '#include "profundum_audibility_readers.h"';
const CATCH_INCLUDE = '#include <catch2/catch_test_macros.hpp>';
const USING_DSP = 'using namespace Krate::DSP;';
const USING_AU = 'using namespace Krate::DSP::ProfundumAudibility;';

class Impure extends Error {}
function impure(msg) {
    throw new Impure(msg);
}

function git(args) {
    return execFileSync('git', args, { cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
}

function splitLines(text) {
    const lines = text.replace(/\r/g, '').split('\n');
    if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();
    return lines;
}

// Edit 2 (the `inline` prefix) and trailing whitespace; applied symmetrically to both sides.
function norm(line) {
    return line
        .replace(/\s+$/, '')
        .replace(/^(\s*(?:\[\[[^\]]*\]\]\s*|template\s*<.*>\s+)*)inline\s+/, '$1');
}

function rangesOf(value, what) {
    if (value === undefined || value === null) return [];
    const one = (v) => {
        if (Number.isInteger(v)) return [v, v];
        if (Array.isArray(v) && v.length === 2 && v.every(Number.isInteger) && v[0] <= v[1]) return [v[0], v[1]];
        return impure(`manifest: bad range ${JSON.stringify(v)} in ${what}`);
    };
    if (Number.isInteger(value)) return [one(value)];
    if (Array.isArray(value) && value.length === 2 && value.every(Number.isInteger)) return [one(value)];
    if (Array.isArray(value)) return value.map(one);
    return impure(`manifest: bad range list ${JSON.stringify(value)} in ${what}`);
}

function loadManifest() {
    if (!fs.existsSync(MANIFEST)) impure(`manifest missing: ${path.relative(ROOT, MANIFEST)}`);
    let data;
    try {
        data = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));
    } catch (e) {
        impure(`manifest unreadable: ${e.message}`);
    }
    const list = Array.isArray(data) ? data : data && Array.isArray(data.lifts) ? data.lifts : null;
    if (!list) impure('manifest: expected an array of lifts (or { "lifts": [...] })');
    const ids = list.map((l) => l.id);
    const missing = EXPECTED_LIFTS.filter((id) => !ids.includes(id));
    const extra = ids.filter((id) => !EXPECTED_LIFTS.includes(id));
    if (missing.length || extra.length || ids.length !== EXPECTED_LIFTS.length)
        impure(`manifest: ids must be exactly L1..L11 (missing [${missing}] extra [${extra}] count ${ids.length})`);
    return list.map((l) => {
        const fns = Array.isArray(l.function) ? l.function : [l.function];
        if (!fns.length || fns.some((f) => typeof f !== 'string' || !f)) impure(`manifest: ${l.id} has no function name`);
        const source = rangesOf(l.source, `${l.id}.source`);
        const calls = rangesOf(l.calls, `${l.id}.calls`);
        const header = rangesOf(l.header, `${l.id}.header`);
        if (!source.length) impure(`manifest: ${l.id} has no source range`);
        if (!header.length) impure(`manifest: ${l.id} has no header range`);
        return { id: l.id, functions: fns, source, calls, header };
    });
}

function checkTu(src, tu, incStart, srcClose) {
    const head = src.slice(0, incStart);
    const tail = src.slice(srcClose + 1);
    for (let i = 0; i < head.length; ++i) {
        if (i >= tu.length || norm(tu[i]) !== norm(head[i]))
            impure(`TU line ${i + 1} differs from the header comment at ${BASE}:${i + 1}: "${i < tu.length ? tu[i] : '<eof>'}"`);
    }
    if (tu.length < head.length + tail.length) impure('TU is shorter than its kept head + tail');
    const tailStart = tu.length - tail.length;
    for (let i = 0; i < tail.length; ++i) {
        if (norm(tu[tailStart + i]) !== norm(tail[i]))
            impure(
                `TU line ${tailStart + i + 1} differs from ${BASE}:${srcClose + 2 + i} ` +
                    `("${tu[tailStart + i]}" vs "${tail[i]}") -- auRunShard and the 16 TEST_CASEs must be unchanged`
            );
    }
    const middle = tu.slice(head.length, tailStart).map((l) => l.trim());
    const seen = new Set();
    for (let i = 0; i < middle.length; ++i) {
        const l = middle[i];
        if (l === '') continue;
        const parts = l === `${USING_DSP} ${USING_AU}` ? [USING_DSP, USING_AU] : [l];
        for (const p of parts) {
            if (![CATCH_INCLUDE, READERS_INCLUDE, USING_DSP, USING_AU].includes(p))
                impure(`TU line ${head.length + i + 1} is not an allowed kept line: "${l}"`);
            seen.add(p);
        }
    }
    for (const req of [CATCH_INCLUDE, READERS_INCLUDE, USING_DSP, USING_AU])
        if (!seen.has(req)) impure(`TU lacks "${req}"`);
}

function checkPrologue(hdr, hOpen, includeBlock, out) {
    const pro = hdr.slice(0, hOpen);
    const inc = includeBlock.map(norm);
    let at = -1;
    for (let i = 0; i + inc.length <= pro.length && at < 0; ++i) {
        let ok = true;
        for (let k = 0; k < inc.length && ok; ++k) ok = norm(pro[i + k]) === inc[k];
        if (ok) at = i;
    }
    if (at < 0)
        impure(`header prologue does not hold the ${BASE} include block (${inc.length} lines) contiguous and unchanged`);
    let inShim = false;
    let shims = 0;
    for (let i = 0; i < pro.length; ++i) {
        if (i >= at && i < at + inc.length) continue;
        const t = pro[i].trim();
        if (inShim) {
            if (SHIM_CLOSE_RE.test(t)) inShim = false;
            continue;
        }
        if (t === '' || t.startsWith('//') || t === '#pragma once') continue;
        if (t === SHIM_OPEN) {
            inShim = true;
            ++shims;
            continue;
        }
        impure(`header line ${i + 1} (prologue) is neither the include block, a comment nor the render shim: "${pro[i]}"`);
    }
    if (inShim) impure('header: the render shim namespace is not closed before the moved block');
    if (shims !== 1) impure(`header: expected exactly one render shim namespace, found ${shims}`);
    out.push(`=== header prologue (new: #pragma once, comments, render shim of plan §4.1), header lines 1-${hOpen}`);
    for (let i = 0; i < pro.length; ++i)
        if (!(i >= at && i < at + inc.length)) out.push(`+       ${String(i + 1).padStart(5)}  ${pro[i]}`);
    out.push('');
}

// Plain LCS line diff for the review file.
function lcsDiff(a, b) {
    const n = a.length;
    const m = b.length;
    const dp = Array.from({ length: n + 1 }, () => new Int32Array(m + 1));
    for (let i = n - 1; i >= 0; --i)
        for (let j = m - 1; j >= 0; --j)
            dp[i][j] = a[i].t === b[j].t ? dp[i + 1][j + 1] + 1 : Math.max(dp[i + 1][j], dp[i][j + 1]);
    const out = [];
    let i = 0;
    let j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i].t === b[j].t) {
            out.push(`  ${String(a[i].n).padStart(5)} ${String(b[j].n).padStart(5)}  ${a[i].raw}`);
            ++i;
            ++j;
        } else if (j < m && (i >= n || dp[i][j + 1] >= dp[i + 1][j])) {
            out.push(`+       ${String(b[j].n).padStart(5)}  ${b[j].raw}`);
            ++j;
        } else {
            out.push(`- ${String(a[i].n).padStart(5)}        ${a[i].raw}`);
            ++i;
        }
    }
    return out;
}

const fmtRanges = (rs) => rs.map(([a, b]) => (a === b ? `${a}` : `${a}-${b}`)).join(',');

function main() {
    const src = splitLines(git(['show', `${BASE}:${TU_REL}`]));
    const tuPath = path.join(ROOT, TU_REL);
    const hdrPath = path.join(ROOT, HDR_REL);
    if (!fs.existsSync(hdrPath)) impure(`${HDR_REL} does not exist`);
    const tu = splitLines(fs.readFileSync(tuPath, 'utf8'));
    const hdr = splitLines(fs.readFileSync(hdrPath, 'utf8'));
    const lifts = loadManifest();

    // ---- source landmarks (0-based indices) ----
    const usingIdx = src.findIndex((l) => l.trim() === USING_DSP);
    const incStart = src.findIndex((l) => l.trim().startsWith('#include'));
    if (usingIdx < 0 || incStart < 0 || incStart > usingIdx) impure(`${BASE}: cannot locate the include block / using line`);
    let incEnd = usingIdx - 1;
    while (incEnd > incStart && src[incEnd].trim() === '') --incEnd;
    const sOpen = src.findIndex((l, i) => i > usingIdx && l.trim() === 'namespace {');
    if (sOpen < 0) impure(`${BASE}: cannot locate the first anonymous namespace`);
    let sClose = -1;
    for (let i = sOpen + 1; i < src.length && sClose < 0; ++i) if (SRC_CLOSE_RE.test(src[i].trim())) sClose = i;
    if (sClose < 0) impure(`${BASE}: cannot locate the first anonymous namespace's close`);

    // ---- TU ----
    checkTu(src, tu, incStart, sClose);

    // ---- header landmarks ----
    const hOpen = hdr.findIndex((l) => l.trim() === NS_OPEN_NEW);
    if (hOpen < 0) impure(`header lacks "${NS_OPEN_NEW}"`);
    let hClose = -1;
    for (let i = hdr.length - 1; i > hOpen && hClose < 0; --i) if (NS_CLOSE_RE.test(hdr[i].trim())) hClose = i;
    if (hClose < 0) impure('header: the moved block is not closed');
    for (let i = hClose + 1; i < hdr.length; ++i)
        if (hdr[i].trim() !== '' && !hdr[i].trim().startsWith('//'))
            impure(`header line ${i + 1} follows the moved block: "${hdr[i]}"`);

    const review = [`# move-check.js review file: ${BASE}:${TU_REL} vs ${HDR_REL}`, ''];
    checkPrologue(hdr, hOpen, src.slice(incStart, incEnd + 1), review);

    // ---- manifest bounds and overlap (1-based line numbers) ----
    const srcOwner = new Map();
    const hdrOwner = new Map();
    for (const l of lifts) {
        for (const [a, b] of [...l.source, ...l.calls]) {
            if (a <= sOpen + 1 || b >= sClose + 1)
                impure(`manifest: ${l.id} source/calls range ${a}-${b} is outside the moved block (${sOpen + 2}-${sClose})`);
            for (let n = a; n <= b; ++n) {
                if (srcOwner.has(n) && srcOwner.get(n) !== l.id)
                    impure(`manifest: source line ${n} claimed by ${srcOwner.get(n)} and ${l.id}`);
                srcOwner.set(n, l.id);
            }
        }
        for (const [a, b] of l.header) {
            if (a <= hOpen + 1 || b >= hClose + 1)
                impure(`manifest: ${l.id} header range ${a}-${b} is outside the moved block (${hOpen + 2}-${hClose})`);
            for (let n = a; n <= b; ++n) {
                if (hdrOwner.has(n)) impure(`manifest: header line ${n} claimed by ${hdrOwner.get(n)} and ${l.id}`);
                hdrOwner.set(n, l.id);
            }
        }
        const text = l.header.flatMap(([a, b]) => hdr.slice(a - 1, b)).join('\n');
        for (const f of l.functions) {
            const re = new RegExp(`\\b${f.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`);
            if (!re.test(text)) impure(`manifest: ${l.id} header range does not contain "${f}"`);
        }
    }

    // ---- normalise the header's moved block (edits 1 and 3), lift ranges removed ----
    let changed = 0;
    const hb = [];
    for (let i = hOpen; i <= hClose; ++i) {
        const n = i + 1;
        if (hdrOwner.has(n)) continue;
        let t = norm(hdr[i]);
        if (i === hOpen) t = 'namespace {';
        else if (i === hClose) t = '}  // namespace';
        else if (t.trim() === ALIAS_NEW) {
            t = t.replace(ALIAS_NEW, ALIAS_OLD);
            ++changed;
        }
        hb.push({ n, t, raw: hdr[i] });
    }
    if (changed !== 1) impure(`expected exactly one P-1 alias line "${ALIAS_NEW}", found ${changed}`);

    // ---- the source's moved block as fixed segments and lift wildcards ----
    const segs = [];
    for (let i = sOpen; i <= sClose; ++i) {
        const n = i + 1;
        const id = srcOwner.get(n);
        const kind = id ? 'wild' : 'fixed';
        const last = segs[segs.length - 1];
        const item = { n, t: norm(src[i]), raw: src[i] };
        if (last && last.kind === kind && last.id === id) last.lines.push(item);
        else segs.push({ kind, id, lines: [item] });
    }

    // ---- glob match: fixed segments verbatim and in order; a wildcard absorbs any header text in place ----
    const fixedAt = (seg, p) => {
        if (p + seg.lines.length > hb.length) return false;
        for (let k = 0; k < seg.lines.length; ++k) if (hb[p + k].t !== seg.lines[k].t) return false;
        return true;
    };
    const prefixLen = (seg, p) => {
        let k = 0;
        while (k < seg.lines.length && p + k < hb.length && hb[p + k].t === seg.lines[k].t) ++k;
        return k;
    };
    // The furthest source line any alignment reached before mismatching (the diagnostic of a failed match).
    let best = { n: -1, s: null, h: null };
    const note = (seg, p) => {
        const k = prefixLen(seg, p);
        const s = seg.lines[Math.min(k, seg.lines.length - 1)];
        if (s.n > best.n) best = { n: s.n, s, h: p + k < hb.length ? hb[p + k] : null };
    };
    const memo = new Map();
    const match = (si, p) => {
        if (si === segs.length) {
            if (p !== hb.length && sClose + 2 > best.n)
                best = { n: sClose + 2, s: { n: sClose + 1, raw: src[sClose] }, h: hb[p] };
            return p === hb.length ? [] : null;
        }
        const key = si * (hb.length + 1) + p;
        if (memo.has(key)) return memo.get(key);
        let res = null;
        const seg = segs[si];
        if (seg.kind === 'fixed') {
            if (fixedAt(seg, p)) {
                const rest = match(si + 1, p + seg.lines.length);
                if (rest) res = [{ si, from: p, to: p + seg.lines.length }, ...rest];
            } else {
                note(seg, p);
            }
        } else {
            const next = segs[si + 1];
            for (let q = p; q <= hb.length && !res; ++q) {
                if (next && next.kind === 'fixed' && !fixedAt(next, q)) {
                    note(next, q);
                    continue;
                }
                const rest = match(si + 1, q);
                if (rest) res = [{ si, from: p, to: q }, ...rest];
            }
        }
        memo.set(key, res);
        return res;
    };
    const result = match(0, 0);
    if (!result) {
        const s = best.s || { n: sOpen + 1, raw: src[sOpen] };
        const h = best.h;
        impure(
            `${BASE}:${s.n} "${s.raw.trim()}" vs header:${h ? h.n : '<eof>'} "${h ? h.raw.trim() : ''}" -- ` +
                'a lift region not in the manifest (a rewritten call site belongs under that lift\'s "calls")'
        );
    }

    // ---- review file ----
    for (const l of lifts) {
        review.push(
            `=== ${l.id} ${l.functions.join(', ')}  source ${fmtRanges(l.source)}` +
                `${l.calls.length ? `  calls ${fmtRanges(l.calls)}` : ''}  header ${fmtRanges(l.header)}`
        );
        review.push(`--- lifted function(s): ${BASE}:${TU_REL} source regions (-) vs header regions (+)`);
        const a = l.source.flatMap(([x, y]) => src.slice(x - 1, y).map((raw, k) => ({ n: x + k, t: norm(raw).trim(), raw })));
        const b = l.header.flatMap(([x, y]) => hdr.slice(x - 1, y).map((raw, k) => ({ n: x + k, t: norm(raw).trim(), raw })));
        review.push(...lcsDiff(a, b));
        review.push('--- call sites: source/calls lines (-) replaced in place by header text (+)');
        for (const r of result) {
            const seg = segs[r.si];
            if (seg.kind !== 'wild' || seg.id !== l.id) continue;
            for (const s of seg.lines) review.push(`- ${String(s.n).padStart(5)}        ${s.raw}`);
            for (let k = r.from; k < r.to; ++k) review.push(`+       ${String(hb[k].n).padStart(5)}  ${hb[k].raw}`);
            review.push('  ...');
        }
        review.push('');
    }
    fs.mkdirSync(GATE, { recursive: true });
    fs.writeFileSync(LIFTS_DIFF, review.join('\n') + '\n');

    // ---- step 2b: the path set ----
    const status = git(['status', '--porcelain', '-uall', '--', 'dsp/']).replace(/\r/g, '');
    fs.writeFileSync(PATHS_TXT, status);
    const got = status.split('\n').filter((l) => l !== '');
    const same = got.length === EXPECTED_PATHS.length && EXPECTED_PATHS.every((e) => got.includes(e));
    if (!same)
        impure(`path set: git status --porcelain -- dsp/ lists [${got.join(' | ')}], expected [${EXPECTED_PATHS.join(' | ')}]`);

    console.log(`lifts.diff -> ${path.relative(ROOT, LIFTS_DIFF)}; paths.txt -> ${path.relative(ROOT, PATHS_TXT)}`);
    console.log(`MOVE PURE changed=${changed} lifted=${lifts.length}`);
}

try {
    main();
} catch (e) {
    if (e instanceof Impure) {
        console.log(`MOVE IMPURE ${e.message}`);
        process.exit(1);
    }
    console.log(`MOVE IMPURE internal error: ${e && e.stack ? e.stack : e}`);
    process.exit(1);
}
