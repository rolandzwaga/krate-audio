#!/usr/bin/env node
// SC-019 footprint check for Profundum Phase 3 (specs/profundum-phase3-anchor, task T002).
// Also carries the layer grep (plan S3 / SC-019) and the C-13 static API grep (SC-024(d) teeth).
// Pattern: specs/profundum-phase2-edge-fold/check-footprint.js.
//
// Usage: node specs/profundum-phase3-anchor/check-footprint.js [baseRef] [--fallback]
// Default baseRef: 9b1a4cf5 (the Phase 3 base on synth/profundum).
//
// Diffs the working tree against baseRef:
//  1. shared headers: (a) `git diff --numstat base -- dsp/include` is empty apart from the new
//     anchor_oscillator.h; with --fallback (FR-032 / plan S5.4) harmonic_oscillator_bank.h and
//     profundum_core.h may differ but append-only (no removed line in `git diff -U0`);
//     (b) the untracked files under dsp/include are at most anchor_oscillator.h (none -> SKIP);
//  2. every Phase 1/2 test file in tasks.md's "Untouchable" list shows 0 added and 0 removed
//     lines; a missing file is a FAIL;
//  3. layer grep: every #include of anchor_oscillator.h is <krate/dsp/core|primitives|processors/...>
//     or a standard header (no '/'); none names systems/ or effects/;
//  4. API grep (C-13): anchor_oscillator.h has no `ProfundumCore` token outside comments, and no
//     public method takes `const float*` except processBlock's two trajectory pointers;
//  5. prints `git diff --stat base -- dsp/include` and the untracked list.
// One PASS/FAIL/SKIP line per check; a file not yet created reports SKIP. Exits non-zero on any FAIL.

'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
const fallback = args.includes('--fallback');
const positional = args.filter((a) => !a.startsWith('--'));
const base = positional[0] || '9b1a4cf5';
const repoRoot = path.resolve(__dirname, '..', '..');

const INC = 'dsp/include/krate/dsp/';
const ANCHOR = INC + 'systems/anchor_oscillator.h';
const FALLBACK_APPEND_ONLY = [
  INC + 'processors/harmonic_oscillator_bank.h',
  INC + 'systems/profundum_core.h',
];

const SYS_TESTS = 'dsp/tests/unit/systems/';
const PROC_TESTS = 'dsp/tests/unit/processors/';
const UNTOUCHABLE_TESTS = [
  // Phase 1/2 systems TUs and helpers (12)
  SYS_TESTS + 'profundum_core_test.cpp',
  SYS_TESTS + 'profundum_core_spectral_test.cpp',
  SYS_TESTS + 'profundum_core_nonfinite_test.cpp',
  SYS_TESTS + 'profundum_core_perf_test.cpp',
  SYS_TESTS + 'profundum_core_excitation_test.cpp',
  SYS_TESTS + 'profundum_core_excitation_spectral_test.cpp',
  SYS_TESTS + 'profundum_core_excitation_nonfinite_test.cpp',
  SYS_TESTS + 'profundum_edge_fold_chain_test.cpp',
  SYS_TESTS + 'profundum_edge_fold_perf_test.cpp',
  SYS_TESTS + 'profundum_core_test_helpers.h',
  SYS_TESTS + 'profundum_excitation_test_helpers.h',
  SYS_TESTS + 'profundum_phase1_core_ref.h',
  // Phase 1/2 processors TUs and helpers (5)
  PROC_TESTS + 'spectral_shape_recipe_test.cpp',
  PROC_TESTS + 'spectral_shape_recipe_test_helpers.h',
  PROC_TESTS + 'harmonic_fold_test.cpp',
  PROC_TESTS + 'harmonic_fold_nonfinite_test.cpp',
  PROC_TESTS + 'harmonic_fold_test_helpers.h',
  // Phase 2 primitives TU (1)
  'dsp/tests/unit/primitives/polyphase_halfband_oversampler_test.cpp',
];

function git(gitArgs) {
  return execFileSync('git', gitArgs, { cwd: repoRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
}

function splitLines(text) {
  const lines = text.split(/\r?\n/);
  if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();
  return lines;
}

function exists(file) {
  return fs.existsSync(path.join(repoRoot, file));
}

function readWorking(file) {
  return fs.readFileSync(path.join(repoRoot, file), 'utf8');
}

// Strips // and /* */ comments and the contents of string/char literals, preserving line count.
function codeOnly(lines) {
  const out = [];
  let inBlock = false;
  for (const line of lines) {
    let s = '';
    let i = 0;
    while (i < line.length) {
      const c = line[i];
      const n = line[i + 1];
      if (inBlock) {
        if (c === '*' && n === '/') { inBlock = false; i += 2; } else i += 1;
        continue;
      }
      if (c === '/' && n === '/') break;
      if (c === '/' && n === '*') { inBlock = true; i += 2; continue; }
      if (c === '"' || c === "'") {
        const q = c;
        s += q;
        i += 1;
        while (i < line.length && line[i] !== q) i += line[i] === '\\' ? 2 : 1;
        s += q;
        i += 1;
        continue;
      }
      s += c;
      i += 1;
    }
    out.push(s);
  }
  return out;
}

// Original include targets (strings are blanked by codeOnly, so read the raw line).
function rawIncludes(text) {
  const raw = splitLines(text);
  const code = codeOnly(raw);
  const res = [];
  for (let i = 0; i < raw.length; i++) {
    if (/^\s*#\s*include\b/.test(code[i])) {
      const m = raw[i].match(/#\s*include\s*([<"][^>"]*[>"])/);
      res.push(m ? m[1] : raw[i].trim());
    }
  }
  return res;
}

function numstat(file) {
  const out = git(['diff', '--numstat', base, '--', file]).trim();
  let added = 0;
  let removed = 0;
  for (const line of out === '' ? [] : splitLines(out)) {
    const [a, r] = line.split('\t');
    // Binary diffs report '-'; count as a change so they cannot slip through.
    added += a === '-' ? 1 : Number(a);
    removed += r === '-' ? 1 : Number(r);
  }
  return { added, removed };
}

let allPass = true;

function report(status, label, detail) {
  if (status === 'FAIL') allPass = false;
  console.log(`${status.padEnd(4)}  ${label}${detail ? `  (${detail})` : ''}`);
}

try {
  git(['rev-parse', '--verify', `${base}^{commit}`]);
} catch (e) {
  console.error(`FAIL  cannot resolve base ref '${base}'`);
  process.exit(2);
}

console.log(`SC-019 / layer / C-13 API footprint check against ${base}${fallback ? ' (--fallback: FR-032 append-only allowed)' : ''}`);

// ---- 1. Shared headers ------------------------------------------------------------------------
console.log('-- 1. shared headers under dsp/include');
const untrackedInc = splitLines(git(['ls-files', '--others', '--exclude-standard', '--', 'dsp/include']));
{
  // (a) tracked diff: every changed path except the new header.
  const changed = splitLines(git(['diff', '--numstat', base, '--', 'dsp/include']))
    .map((l) => l.split('\t'))
    .map(([a, r, file]) => ({ file, added: a === '-' ? 1 : Number(a), removed: r === '-' ? 1 : Number(r) }))
    .filter((c) => c.file !== ANCHOR);
  const problems = [];
  for (const c of changed) {
    if (fallback && FALLBACK_APPEND_ONLY.includes(c.file)) {
      const removedLines = git(['diff', '-U0', base, '--', c.file])
        .split(/\r?\n/)
        .filter((l) => l.startsWith('-') && !l.startsWith('---'));
      if (removedLines.length === 0) {
        console.log(`      append-only ${c.file}: +${c.added} -0`);
      } else {
        problems.push(`${c.file}: ${removedLines.length} removed line(s), must be append-only`);
        for (const l of removedLines) problems.push(`  removed: "${l.slice(1).trim()}"`);
      }
    } else {
      problems.push(`${c.file}: +${c.added} -${c.removed}, must be +0 -0`);
    }
  }
  report(problems.length === 0 ? 'PASS' : 'FAIL', '1(a) git diff dsp/include (except anchor_oscillator.h)',
         problems.length === 0 ? `${changed.length} changed path(s) outside the new header` : `${problems.length} problem line(s)`);
  for (const p of problems) console.log(`      ${p}`);

  // (b) untracked: only the new header may appear.
  if (untrackedInc.length === 0) {
    report('SKIP', '1(b) untracked under dsp/include', 'none (anchor_oscillator.h not created yet)');
  } else {
    const extra = untrackedInc.filter((f) => f !== ANCHOR);
    report(extra.length === 0 ? 'PASS' : 'FAIL', '1(b) untracked under dsp/include is only anchor_oscillator.h',
           extra.length === 0 ? untrackedInc.join(', ') : `unexpected: ${extra.join(', ')}`);
  }
}

// ---- 2. Phase 1/2 test files -------------------------------------------------------------------
console.log(`-- 2. zero diff lines in the ${UNTOUCHABLE_TESTS.length} Phase 1/2 test files`);
for (const file of UNTOUCHABLE_TESTS) {
  if (!exists(file)) {
    report('FAIL', file, 'missing from the working tree');
    continue;
  }
  const { added, removed } = numstat(file);
  report(added === 0 && removed === 0 ? 'PASS' : 'FAIL', file, `+${added} -${removed}, must be +0 -0`);
}

// ---- 3. Layer grep ------------------------------------------------------------------------------
console.log(`-- 3. layer grep: ${ANCHOR} includes L0-L2 + std only`);
let anchorText = null;
if (!exists(ANCHOR)) {
  report('SKIP', ANCHOR, 'not created yet');
} else {
  anchorText = readWorking(ANCHOR);
  const incs = rawIncludes(anchorText);
  const ok = (inc) => (/^<[^/>"]+>$/.test(inc)) || /^<krate\/dsp\/(core|primitives|processors)\/[^>]+>$/.test(inc);
  const bad = incs.filter((inc) => !ok(inc) || /(systems|effects)\//.test(inc));
  report(bad.length === 0 ? 'PASS' : 'FAIL', `${ANCHOR} includes`,
         bad.length === 0 ? `${incs.length} include(s), all L0-L2 or std` : `offending: ${bad.join(', ')}`);
}

// ---- 4. API grep (C-13 static teeth) -------------------------------------------------------------
console.log('-- 4. API grep (C-13): no ProfundumCore, no const float* input outside processBlock trajectories');
if (anchorText === null) {
  report('SKIP', `${ANCHOR} API`, 'not created yet');
} else {
  const raw = splitLines(anchorText);
  const code = codeOnly(raw);

  // (a) ProfundumCore token outside comments (string contents are blanked too).
  const coreHits = [];
  code.forEach((l, i) => { if (/\bProfundumCore\b/.test(l)) coreHits.push(`line ${i + 1}: "${raw[i].trim()}"`); });
  report(coreHits.length === 0 ? 'PASS' : 'FAIL', '4(a) no ProfundumCore token outside comments',
         coreHits.length === 0 ? '' : `${coreHits.length} hit(s)`);
  for (const h of coreHits) console.log(`      ${h}`);

  // (b) public section of class AnchorOscillator: from the first `public:` after the class head to
  //     the next `private:` (both at class-body depth).
  const classIdx = code.findIndex((l) => /\bclass\s+AnchorOscillator\b[^;]*$/.test(l));
  let pubIdx = -1;
  let privIdx = -1;
  if (classIdx >= 0) {
    let depth = 0;
    let opened = false;
    for (let i = classIdx; i < code.length; i++) {
      const atBody = opened && depth === 1;
      if (atBody && pubIdx < 0 && /^\s*public\s*:/.test(code[i])) pubIdx = i;
      else if (atBody && pubIdx >= 0 && /^\s*private\s*:/.test(code[i])) { privIdx = i; break; }
      for (const ch of code[i]) {
        if (ch === '{') { depth += 1; opened = true; } else if (ch === '}') depth -= 1;
      }
      if (opened && depth === 0) break;
    }
  }
  if (pubIdx < 0 || privIdx < 0) {
    report('FAIL', '4(b) public API const float* scan', 'cannot locate class AnchorOscillator public:/private:');
  } else {
    // Join the public section, find every parameter list (identifier followed by '(' ... ')'), and
    // list those taking a const-float pointer.
    const text = code.slice(pubIdx + 1, privIdx).join('\n');
    const CONST_FLOAT_PTR = /\bconst\s+float\s*\*/g;
    const sigs = [];
    const re = /\b([A-Za-z_]\w*)\s*\(/g;
    let m;
    while ((m = re.exec(text)) !== null) {
      const open = m.index + m[0].length - 1;
      let depth = 0;
      let close = -1;
      for (let i = open; i < text.length; i++) {
        if (text[i] === '(') depth += 1;
        else if (text[i] === ')') { depth -= 1; if (depth === 0) { close = i; break; } }
      }
      if (close < 0) continue;
      const params = text.slice(open + 1, close);
      const n = (params.match(CONST_FLOAT_PTR) || []).length;
      if (n > 0) sigs.push({ name: m[1], n, sig: `${m[1]}(${params.replace(/\s+/g, ' ').trim()})` });
      re.lastIndex = close + 1;
    }
    const bad = sigs.filter((s) => !(s.name === 'processBlock' && s.n <= 2));
    report(bad.length === 0 ? 'PASS' : 'FAIL', '4(b) public const float* parameters only in processBlock (<= 2)',
           `public :${pubIdx + 1}-${privIdx + 1}, ${sigs.length} signature(s) take const float*`);
    for (const s of sigs) console.log(`      ${bad.includes(s) ? 'BAD ' : 'ok  '} ${s.sig}  [${s.n} const float*]`);
  }
}

// ---- 5. Stat for comply ------------------------------------------------------------------------
console.log(`-- 5. git diff --stat ${base} -- dsp/include`);
{
  const stat = git(['diff', '--stat', base, '--', 'dsp/include']).trimEnd();
  console.log(stat === '' ? '      (empty)' : stat.split(/\r?\n/).map((l) => `      ${l}`).join('\n'));
  console.log('      untracked under dsp/include:');
  console.log(untrackedInc.length === 0 ? '      (none)' : untrackedInc.map((l) => `      ${l}`).join('\n'));
}

console.log(allPass ? 'OVERALL: PASS' : 'OVERALL: FAIL');
process.exit(allPass ? 0 : 1);
