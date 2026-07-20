#!/usr/bin/env node
//
// lint-float-bit-goldens.js
// =============================================================================
// Flags tests that pin a DSP render by hashing the raw bits of its float
// samples -- an FNV/rolling digest fed from `memcpy(&bits, &sample, 4)` or
// `std::bit_cast<uint32_t>(sample)` -- and compare it to a hard-coded constant.
//
// Why this is always a bug: such a digest asserts BIT-EXACT equality of a
// floating-point computation across every compiler that will ever build it.
// MSVC, GCC and Apple Clang differ in the last bits of every transcendental,
// and the macOS leg additionally builds with -ffast-math. Measured spread for
// the phaser/flanger renders in this repo:
//
//     worst per-sample absolute difference   2.9e-5   (signal peak 2.17)
//     worst aggregate relative difference    1.9e-7
//
// A single 1-ULP difference anywhere in the buffer changes the digest
// completely, so a digest generated on the developer's machine is GUARANTEED
// red on the other two CI legs. That is exactly how the phaser and flanger
// goldens broke both the Linux and macOS builds: green on Windows -- full
// build, all suites, pluginval, clang-tidy -- and structurally incapable of
// passing anywhere else.
//
// What to do instead: compare aggregate metrics (RMS, peak, mean absolute
// value, total variation) plus spaced sample checkpoints against a stored
// reference, at a tolerance derived from the measured cross-toolchain spread.
// tests/test_helpers/render_fingerprint.h does this and is the drop-in
// replacement.
//
// NOT flagged (deliberately):
//   * Digests over a SERIALIZED BYTE STREAM (e.g. an IBStream of saved plugin
//     state). Those bytes are stored values round-tripped through memory, not
//     the result of arithmetic, so they are bit-identical on every toolchain --
//     and they are the correct way to pin a preset format. They never
//     reinterpret a float's bits; they walk `char` data.
//   * Building NaN/Inf inputs from bit patterns, which tests must do under
//     -ffast-math (see reference in dsp/CLAUDE.md). That reinterprets float
//     bits but does not accumulate them into a hash.
//
// The rule therefore fires only when BOTH appear in one file: float->integer
// bit reinterpretation, AND a hash accumulation consuming it.
//
// Usage:
//   node tools/lint-float-bit-goldens.js          # report and exit 1 on hits
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

const SCAN_DIRS = ['dsp', 'plugins', 'tests'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);
const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);

// Reinterpreting the bits of a float/double as an integer.
const FLOAT_BITS_PATTERNS = [
  // memcpy(&bits, &sample, sizeof(bits)) where the source is float-ish
  /\bmemcpy\s*\(\s*&\s*\w*(?:bits|raw|word|u32|u64)\w*\s*,\s*&\s*\w+\s*,/i,
  /\bbit_cast\s*<\s*(?:std::)?(?:u?int(?:32|64)_t|unsigned)\s*>\s*\(/,
  /\breinterpret_cast\s*<\s*const\s+(?:std::)?u?int(?:32|64)_t\s*\*\s*>\s*\(\s*&/,
];

// Accumulating those bits into a rolling digest.
const HASH_ACCUM_PATTERNS = [
  /\b1099511628211(?:ULL|ull)?\b/,        // FNV-1a 64-bit prime
  /\b0x100000001b3\b/i,
  /\b1469598103934665603(?:ULL|ull)?\b/,  // FNV-1a 64-bit offset basis
  /\b0xcbf29ce484222325\b/i,
  /\b(?:hash|digest)\s*\*=/i,
];

function walk(dir, out) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const e of entries) {
    if (e.isDirectory()) {
      if (SKIP_DIR.has(e.name)) continue;
      walk(path.join(dir, e.name), out);
    } else if (SOURCE_EXT.has(path.extname(e.name))) {
      out.push(path.join(dir, e.name));
    }
  }
  return out;
}

function matchLines(lines, patterns) {
  const hits = [];
  lines.forEach((line, i) => {
    if (line.trimStart().startsWith('//')) return; // don't flag prose about the rule
    if (patterns.some((p) => p.test(line))) hits.push({ line: i + 1, text: line.trim() });
  });
  return hits;
}

function main() {
  const files = [];
  for (const d of SCAN_DIRS) walk(path.join(REPO_ROOT, d), files);

  const violations = [];
  for (const file of files) {
    let text;
    try {
      text = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }
    const lines = text.replace(/\r\n/g, '\n').split('\n');

    const bitHits = matchLines(lines, FLOAT_BITS_PATTERNS);
    if (bitHits.length === 0) continue;
    const hashHits = matchLines(lines, HASH_ACCUM_PATTERNS);
    if (hashHits.length === 0) continue;

    violations.push({ file: path.relative(REPO_ROOT, file), bitHits, hashHits });
  }

  if (violations.length === 0) {
    console.log(`lint-float-bit-goldens: clean (${files.length} files scanned)`);
    return 0;
  }

  console.error('lint-float-bit-goldens: bit-exact float digest(s) found\n');
  for (const v of violations) {
    console.error(`  ${v.file}`);
    for (const h of v.bitHits) console.error(`    L${h.line}: ${h.text}   <-- float bits`);
    for (const h of v.hashHits) console.error(`    L${h.line}: ${h.text}   <-- hashed`);
    console.error('');
  }
  console.error(
    'A digest over float bits asserts bit-exact equality of floating-point math\n' +
    'across MSVC, GCC and Apple Clang (the last of which builds with -ffast-math).\n' +
    'It cannot hold. Compare aggregate metrics + sample checkpoints at a measured\n' +
    'tolerance instead -- see tests/test_helpers/render_fingerprint.h.\n'
  );
  return 1;
}

try {
  process.exit(main());
} catch (err) {
  console.error('lint-float-bit-goldens: internal error:', err && err.message);
  process.exit(2);
}
