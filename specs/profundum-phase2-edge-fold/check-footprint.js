#!/usr/bin/env node
// SC-021 footprint check for Profundum Phase 2 (specs/profundum-phase2-edge-fold, task T002).
// Also carries the SC-026 FIR-mode grep and the plan S8.1 layer grep.
//
// Usage: node specs/profundum-phase2-edge-fold/check-footprint.js [baseRef]
// Default baseRef: 47e43918 (Phase 1 harmonic core commit, the Phase 2 base).
//
// Diffs the working tree against baseRef:
//  1. every untouchable file (FR-048 shared headers, spectral_shape_recipe.h, the Phase 1 TUs and
//     helpers) shows 0 added and 0 removed lines; a missing file is a FAIL;
//  2. profundum_core.h: (i) every Phase 1 public declaration line is present verbatim; (ii) every
//     removed line is a banner line (:5-6) or lies inside the body of prepare / reset /
//     stateFinite / controlUpdate / applyNoteOn / processBlock in the BASE file (brace-matched
//     from each signature line); (iii) the FTZ note (:37-38) is present verbatim;
//  3. SC-026: the two Phase 2 headers and the Phase 2 additions to profundum_core.h reference no
//     FIR quality/mode (Standard, High, LinearPhase) and do not include the shared oversampler.h;
//  4. plan S8.1: no processors-test file new since base includes systems/; harmonic_fold.h
//     includes nothing from processors/, systems/ or effects/; polyphase_halfband_oversampler.h
//     includes only core/ and the standard library;
//  5. prints `git diff --stat base -- dsp/include`.
// One PASS/FAIL/SKIP line per check; a file not yet created reports SKIP. Exits non-zero on any FAIL.

'use strict';

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const base = process.argv[2] || '47e43918';
const repoRoot = path.resolve(__dirname, '..', '..');

const INC = 'dsp/include/krate/dsp/';
const UNTOUCHABLE = [
  // FR-048 shared headers (15)
  INC + 'primitives/tanh_adaa.h',
  INC + 'primitives/hard_clip_adaa.h',
  INC + 'primitives/oversampler.h',
  INC + 'primitives/biquad.h',
  INC + 'primitives/wavefolder.h',
  INC + 'core/wavefold_math.h',
  INC + 'processors/wavefolder_processor.h',
  INC + 'core/sigmoid.h',
  INC + 'core/fast_math.h',
  INC + 'primitives/dc_blocker.h',
  INC + 'primitives/smoother.h',
  INC + 'processors/envelope_follower.h',
  INC + 'processors/harmonic_oscillator_bank.h',
  INC + 'processors/harmonic_oscillator_bank_simd.h',
  INC + 'processors/harmonic_types.h',
  // Phase 1 recipe
  INC + 'processors/spectral_shape_recipe.h',
  // Phase 1 test TUs (5)
  'dsp/tests/unit/systems/profundum_core_test.cpp',
  'dsp/tests/unit/systems/profundum_core_spectral_test.cpp',
  'dsp/tests/unit/systems/profundum_core_nonfinite_test.cpp',
  'dsp/tests/unit/systems/profundum_core_perf_test.cpp',
  'dsp/tests/unit/processors/spectral_shape_recipe_test.cpp',
  // Phase 1 helpers (2)
  'dsp/tests/unit/systems/profundum_core_test_helpers.h',
  'dsp/tests/unit/processors/spectral_shape_recipe_test_helpers.h',
];

const CORE = INC + 'systems/profundum_core.h';
const OVERSAMPLER2 = INC + 'primitives/polyphase_halfband_oversampler.h';
const FOLD = INC + 'processors/harmonic_fold.h';
const PROCESSORS_TESTS = 'dsp/tests/unit/processors';

// Members whose bodies Phase 2 may edit (plan S5.6), matched on comment-stripped code.
const EDITABLE_MEMBERS = ['prepare', 'reset', 'stateFinite', 'controlUpdate', 'applyNoteOn', 'processBlock'];
const PUBLIC_DECL_PREFIXES = ['void ', '[[nodiscard]]', 'static constexpr', 'enum class', 'ProfundumCore(', 'ProfundumCore&'];

function git(args) {
  return execFileSync('git', args, { cwd: repoRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
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

// Original include line text (strings are blanked by codeOnly, so read the raw line).
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

console.log(`SC-021 / SC-026 / S8.1 footprint check against ${base}`);

// ---- 1. Untouchable files -------------------------------------------------------------------
console.log('-- 1. zero diff lines (FR-048 shared headers, recipe, Phase 1 TUs and helpers)');
for (const file of UNTOUCHABLE) {
  if (!exists(file)) {
    report('FAIL', file, 'missing from the working tree');
    continue;
  }
  const out = git(['diff', '--numstat', base, '--', file]).trim();
  let added = 0;
  let removed = 0;
  if (out !== '') {
    for (const line of splitLines(out)) {
      const [a, r] = line.split('\t');
      // Binary diffs report '-'; count as a change so they cannot slip through.
      added += a === '-' ? 1 : Number(a);
      removed += r === '-' ? 1 : Number(r);
    }
  }
  report(added === 0 && removed === 0 ? 'PASS' : 'FAIL', file, `+${added} -${removed}, must be +0 -0`);
}

// ---- 2. profundum_core.h ---------------------------------------------------------------------
console.log(`-- 2. ${CORE}`);
let coreAddedLines = [];
{
  let baseText;
  try {
    baseText = git(['show', `${base}:${CORE}`]);
  } catch (e) {
    baseText = null;
  }
  if (baseText === null) {
    report('FAIL', CORE, `not present at ${base}`);
  } else if (!exists(CORE)) {
    report('FAIL', CORE, 'missing from the working tree');
  } else {
    const baseLines = splitLines(baseText);
    const baseCode = codeOnly(baseLines);
    const workLines = splitLines(readWorking(CORE));
    const workSet = new Set(workLines);

    // (i) public declarations: lines between the class's first `public:` and the next `private:`.
    {
      const classIdx = baseCode.findIndex((l) => /\bclass\s+ProfundumCore\s*\{/.test(l));
      const pubIdx = classIdx < 0 ? -1 : baseCode.findIndex((l, i) => i > classIdx && /^\s*public:\s*$/.test(l));
      const privIdx = pubIdx < 0 ? -1 : baseCode.findIndex((l, i) => i > pubIdx && /^\s*private:\s*$/.test(l));
      if (pubIdx < 0 || privIdx < 0) {
        report('FAIL', '2(i) public declarations', 'cannot locate public:/private: in the base file');
      } else {
        const decls = [];
        for (let i = pubIdx + 1; i < privIdx; i++) {
          const t = baseLines[i].trim();
          if (PUBLIC_DECL_PREFIXES.some((p) => t.startsWith(p))) decls.push({ line: i + 1, text: baseLines[i] });
        }
        const missing = decls.filter((d) => !workSet.has(d.text));
        report(missing.length === 0 ? 'PASS' : 'FAIL', '2(i) Phase 1 public declarations verbatim',
               `${decls.length - missing.length}/${decls.length} present, base :${pubIdx + 1}-${privIdx + 1}`);
        for (const m of missing) console.log(`      missing base :${m.line}: "${m.text.trim()}"`);
      }
    }

    // (ii) removed lines confined to the banner and the editable member bodies.
    {
      const allowed = new Set();
      const problems = [];
      // Banner (:5-6): the "Includes Layer 0 / and Layer 2 only." pair.
      const bannerIdx = baseLines.findIndex((l, i) => i < 15 && /Includes Layer 0/.test(l));
      if (bannerIdx < 0) problems.push('banner line "Includes Layer 0" not found in the base file');
      else { allowed.add(bannerIdx + 1); allowed.add(bannerIdx + 2); }

      const ranges = [];
      for (const name of EDITABLE_MEMBERS) {
        const re = new RegExp(`^\\s*(\\[\\[nodiscard\\]\\]\\s*)?([\\w:<>*&]+\\s+)+${name}\\(`);
        const hits = [];
        baseCode.forEach((l, i) => { if (re.test(l)) hits.push(i); });
        if (hits.length !== 1) {
          problems.push(`signature of ${name}( found ${hits.length} times in the base file, need exactly 1`);
          continue;
        }
        const sig = hits[0];
        let depth = 0;
        let open = -1;
        let close = -1;
        for (let i = sig; i < baseCode.length && close < 0; i++) {
          for (const ch of baseCode[i]) {
            if (ch === ';' && open < 0) { close = -2; break; } // declaration only
            if (ch === '{') { if (open < 0) open = i; depth += 1; }
            else if (ch === '}') { depth -= 1; if (open >= 0 && depth === 0) { close = i; break; } }
          }
        }
        if (open < 0 || close < 0) {
          problems.push(`cannot brace-match the body of ${name}( from base :${sig + 1}`);
          continue;
        }
        // Body: after the opening-brace line (unless that line is a bare `{`) through the closing line.
        const first = baseCode[open].trim() === '{' ? open : open + 1;
        for (let i = first; i <= close; i++) allowed.add(i + 1);
        ranges.push(`${name} :${sig + 1}-${close + 1}`);
      }

      const diff = git(['diff', '-U0', base, '--', CORE]);
      const removed = [];
      let oldLine = 0;
      for (const raw of diff.split(/\r?\n/)) {
        const h = raw.match(/^@@ -(\d+)(?:,(\d+))? \+\d+(?:,\d+)? @@/);
        if (h) { oldLine = Number(h[1]); continue; }
        if (raw.startsWith('+++') || raw.startsWith('---')) continue;
        if (raw.startsWith('-')) { removed.push({ line: oldLine, text: raw.slice(1) }); oldLine += 1; }
        else if (raw.startsWith('+')) coreAddedLines.push(raw.slice(1));
      }
      for (const r of removed) {
        if (!allowed.has(r.line)) problems.push(`removed base :${r.line} outside the allow-list: "${r.text.trim()}"`);
      }
      report(problems.length === 0 ? 'PASS' : 'FAIL', '2(ii) removed lines within banner + member bodies',
             `+${coreAddedLines.length} -${removed.length}; ${ranges.join(', ')}`);
      for (const p of problems) console.log(`      ${p}`);
    }

    // (iii) FTZ note (:37-38) verbatim.
    {
      const ftzIdx = baseLines.findIndex((l) => /@note Precondition: .*FTZ\/DAZ/.test(l));
      if (ftzIdx < 0) {
        report('FAIL', '2(iii) FTZ note verbatim', 'FTZ note not found in the base file');
      } else {
        const note = [baseLines[ftzIdx], baseLines[ftzIdx + 1]];
        const missing = note.filter((l) => !workSet.has(l));
        report(missing.length === 0 ? 'PASS' : 'FAIL', '2(iii) FTZ note verbatim', `base :${ftzIdx + 1}-${ftzIdx + 2}`);
        for (const m of missing) console.log(`      missing: "${m.trim()}"`);
      }
    }
  }
}

// ---- 3. SC-026 FIR-mode grep -------------------------------------------------------------------
console.log('-- 3. SC-026: no FIR quality/mode, no shared oversampler.h include');
{
  const FIR_TOKEN = /\b(Standard|High|LinearPhase)\b/;
  const SHARED_OS_INCLUDE = /#\s*include\s*[<"](?:[^<">]*\/)?oversampler\.h[>"]/;

  function scan(label, lines) {
    const code = codeOnly(lines);
    const hits = [];
    lines.forEach((raw, i) => {
      const m = code[i].match(FIR_TOKEN);
      if (m) hits.push(`line ${i + 1}: token "${m[1]}"`);
      if (SHARED_OS_INCLUDE.test(raw) && /^\s*#\s*include/.test(code[i])) hits.push(`line ${i + 1}: shared oversampler.h include`);
    });
    report(hits.length === 0 ? 'PASS' : 'FAIL', label, hits.length === 0 ? 'no hits (comments excluded)' : `${hits.length} hit(s)`);
    for (const h of hits) console.log(`      ${h}`);
  }

  for (const file of [OVERSAMPLER2, FOLD]) {
    if (!exists(file)) report('SKIP', file, 'not created yet');
    else scan(file, splitLines(readWorking(file)));
  }
  if (coreAddedLines.length === 0) report('SKIP', `${CORE} Phase 2 additions`, 'no added lines');
  else scan(`${CORE} Phase 2 additions (added-line index)`, coreAddedLines);
}

// ---- 4. Layer grep (plan S8.1) --------------------------------------------------------------------
console.log('-- 4. layer grep (plan S8.1)');
{
  const added = splitLines(git(['diff', '--name-status', base, '--', PROCESSORS_TESTS]))
    .map((l) => l.split('\t'))
    .filter((p) => p[0] === 'A')
    .map((p) => p[1]);
  const untracked = splitLines(git(['ls-files', '--others', '--exclude-standard', '--', PROCESSORS_TESTS]));
  const newFiles = [...new Set([...added, ...untracked])].filter((f) => exists(f));
  if (newFiles.length === 0) {
    report('SKIP', `${PROCESSORS_TESTS}/ new files`, 'none new since base');
  } else {
    for (const f of newFiles) {
      const bad = rawIncludes(readWorking(f)).filter((inc) => inc.includes('systems/'));
      report(bad.length === 0 ? 'PASS' : 'FAIL', `${f} includes no systems/`, bad.length === 0 ? '' : bad.join(', '));
    }
  }

  if (!exists(FOLD)) {
    report('SKIP', FOLD, 'not created yet');
  } else {
    const bad = rawIncludes(readWorking(FOLD)).filter((inc) => /(processors|systems|effects)\//.test(inc));
    report(bad.length === 0 ? 'PASS' : 'FAIL', `${FOLD} includes only L0/L1`, bad.length === 0 ? '' : bad.join(', '));
  }

  if (!exists(OVERSAMPLER2)) {
    report('SKIP', OVERSAMPLER2, 'not created yet');
  } else {
    const ok = (inc) => /^<[a-z_]+>$/.test(inc) || /^[<"](krate\/dsp\/|\.\.\/)core\/[^>"]+[>"]$/.test(inc);
    const bad = rawIncludes(readWorking(OVERSAMPLER2)).filter((inc) => !ok(inc));
    report(bad.length === 0 ? 'PASS' : 'FAIL', `${OVERSAMPLER2} includes only core/ + std`, bad.length === 0 ? '' : bad.join(', '));
  }
}

// ---- 5. Stat for comply ------------------------------------------------------------------------
console.log(`-- 5. git diff --stat ${base} -- dsp/include`);
{
  const stat = git(['diff', '--stat', base, '--', 'dsp/include']).trimEnd();
  console.log(stat === '' ? '      (empty)' : stat.split(/\r?\n/).map((l) => `      ${l}`).join('\n'));
}

console.log(allPass ? 'OVERALL: PASS' : 'OVERALL: FAIL');
process.exit(allPass ? 0 : 1);
