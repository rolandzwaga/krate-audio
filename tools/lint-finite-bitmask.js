#!/usr/bin/env node
//
// lint-finite-bitmask.js
// =============================================================================
// Fails when a hand-written "exponent all ones" test appears outside
// core/db_utils.h:
//
//     (bits & 0x7F800000u) != 0x7F800000u               // float: finite?
//     (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull   // double: Inf/NaN?
//
// WHY. The macOS leg builds with -ffast-math. Newer Apple clang tracks that a
// value came from a float (LLVM nofpclass) and folds such a test to "finite",
// even through std::bit_cast or memcpy, so the guard or the assertion it backs
// silently disappears. db_utils.h reads the bits through opaqueFloatBits /
// opaqueDoubleBits, an empty asm barrier that severs that provenance; every
// other finiteness check must call Krate::DSP::detail::isFinite / isNaN / isInf.
// On 2026-10-25 seven test-helper clones (five Profundum, two Membrum) were
// vacuous on macOS this way: TransientShaperHelpers_SideMidAndBits read
// isFiniteBits(NaN) as true. A test of the exponent against ZERO (subnormal /
// zero detection) is not flagged: fast-math does not let the compiler fold it.
//
// A RATCHET, NOT A SWEEP. The tree held 80+ such tests when this gate was
// written, production oscillator headers among them. tools/lint-finite-bitmask-
// baseline.json records the per-file count; a file that is not listed, or whose
// count grows, fails. Converting a file to detail::isFinite lowers its count:
// run --update-baseline so the gate tightens with it.
//
// A line that reads its bits through opaqueFloatBits / opaqueDoubleBits is the
// barrier form and is not flagged. Comments are ignored.
//
// Usage:
//   node tools/lint-finite-bitmask.js                    # gate
//   node tools/lint-finite-bitmask.js --list             # print every hit
//   node tools/lint-finite-bitmask.js --update-baseline  # rewrite the baseline from the tree
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const BASELINE = path.join(__dirname, 'lint-finite-bitmask-baseline.json');
const SCAN_DIRS = ['dsp', 'plugins', 'tests'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);
const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);
// db_utils.h owns the barrier; float_guard.cpp is Membrum's out-of-line -fno-fast-math guard TU (membrum_float_guard),
// where the plain mask is correct because the TU is IEEE and nothing inlines it.
const ALLOWED = new Set(['dsp/include/krate/dsp/core/db_utils.h', 'plugins/membrum/src/dsp/float_guard.cpp']);

// A comparison against the all-ones exponent, float or double, with or without an integer suffix.
const PATTERNS = [
  /(==|!=)\s*\(?\s*0x7f800000(u|ul|ull)?\b/i,
  /(==|!=)\s*\(?\s*0x7ff0000000000000(u|ul|ull)?\b/i,
];
const BARRIER = /\bopaque(Float|Double)Bits\b/;

function walk(dir, out) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const e of entries) {
    if (e.isDirectory()) {
      if (!SKIP_DIR.has(e.name) && !e.name.startsWith('build')) walk(path.join(dir, e.name), out);
    } else if (SOURCE_EXT.has(path.extname(e.name))) {
      out.push(path.join(dir, e.name));
    }
  }
}

// Code text of one line with // comments and the parts of /* */ blocks removed; `state.inBlock` carries across lines.
function codeOf(line, state) {
  let code = '';
  let i = 0;
  while (i < line.length) {
    if (state.inBlock) {
      const end = line.indexOf('*/', i);
      if (end < 0) return code;
      state.inBlock = false;
      i = end + 2;
    } else if (line.startsWith('//', i)) {
      return code;
    } else if (line.startsWith('/*', i)) {
      state.inBlock = true;
      i += 2;
    } else {
      code += line[i];
      ++i;
    }
  }
  return code;
}

function scan() {
  const files = [];
  for (const d of SCAN_DIRS) walk(path.join(REPO_ROOT, d), files);
  const hits = new Map();  // rel -> [{ line, text }]
  for (const abs of files) {
    const rel = path.relative(REPO_ROOT, abs).split(path.sep).join('/');
    if (ALLOWED.has(rel)) continue;
    const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/);
    const state = { inBlock: false };
    lines.forEach((raw, idx) => {
      const code = codeOf(raw, state);
      if (BARRIER.test(code)) return;
      if (PATTERNS.some((re) => re.test(code))) {
        if (!hits.has(rel)) hits.set(rel, []);
        hits.get(rel).push({ line: idx + 1, text: raw.trim() });
      }
    });
  }
  return hits;
}

function main() {
  const args = new Set(process.argv.slice(2));
  const hits = scan();
  const counts = {};
  for (const [rel, list] of [...hits.entries()].sort()) counts[rel] = list.length;

  if (args.has('--list')) {
    for (const [rel, list] of [...hits.entries()].sort()) {
      for (const h of list) console.log(`${rel}:${h.line}: ${h.text}`);
    }
  }
  if (args.has('--update-baseline')) {
    fs.writeFileSync(BASELINE, JSON.stringify(counts, null, 2) + '\n');
    const total = Object.values(counts).reduce((a, b) => a + b, 0);
    console.log(`lint-finite-bitmask: baseline written, ${total} hits in ${Object.keys(counts).length} files`);
    return 0;
  }

  let baseline;
  try {
    baseline = JSON.parse(fs.readFileSync(BASELINE, 'utf8'));
  } catch (err) {
    console.error(`lint-finite-bitmask: cannot read ${path.relative(REPO_ROOT, BASELINE)}: ${err.message}`);
    return 2;
  }

  let bad = 0;
  for (const [rel, list] of [...hits.entries()].sort()) {
    const allowed = baseline[rel] || 0;
    if (list.length > allowed) {
      ++bad;
      console.log(`::error file=${rel}::${rel}: ${list.length} hand-written exponent-mask finiteness test(s), baseline ${allowed}.`);
      for (const h of list) console.log(`  ${rel}:${h.line}: ${h.text}`);
    }
  }
  const lowered = Object.keys(baseline).filter((rel) => (counts[rel] || 0) < baseline[rel]);
  if (bad > 0) {
    console.log(
      'lint-finite-bitmask: FAILED. Apple clang folds a bit-mask "is finite" test under -ffast-math; call\n' +
      'Krate::DSP::detail::isFinite / isNaN / isInf (core/db_utils.h), which read the bits through a barrier.');
    return 1;
  }
  if (lowered.length > 0) {
    console.log(`lint-finite-bitmask: ${lowered.length} file(s) below the baseline; run --update-baseline to tighten it.`);
  }
  console.log('lint-finite-bitmask: clean');
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-finite-bitmask: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
