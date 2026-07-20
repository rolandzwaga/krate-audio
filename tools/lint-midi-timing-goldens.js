#!/usr/bin/env node
//
// lint-midi-timing-goldens.js
// =============================================================================
// Flags tests that pin a captured MIDI/event stream by comparing the formatted
// dump to a golden file with a raw string equality -- `REQUIRE(actual == golden)`.
//
// Why this is always a bug: the dump looks like text, but every sample offset
// in it is floating-point arithmetic truncated to an integer. Truncation turns
// a 1-ULP difference into a whole sample, so string equality quietly demands
// bit-identical FP math from every compiler that will ever build the test.
//
// It cannot hold. The VST3 SDK enables -ffast-math globally on the GCC and
// Clang legs, so those builds reassociate products and use reciprocal
// multiplies where MSVC does not. ArpeggiatorCore's step and gate durations
// land exactly on integers by construction -- the most fragile possible input
// to a truncating cast:
//
//     baseDuration = (size_t)(secondsPerBeat * beatsPerStep * sampleRate);
//     swung        = (size_t)((double)baseDuration * (1.0 + swing));
//
// 11025 * 1.36 is exactly 14994 in real arithmetic but evaluates to
// 14993.999999999998 or 14994.000000000002 depending on how the product is
// formed, truncating to 14993 or 14994. That is precisely how Ruinae's SC-004b
// goldens -- generated on Windows, green through every Windows gate including
// the full suite, pluginval and clang-tidy -- went red on both the Linux and
// macOS legs. Measured on the real 60-second Tape_Shuffle sequence: 504 events
// emitted on both legs, zero differing in kind/pitch/order, 146 differing in
// timing, worst difference 2 samples (~45 us).
//
// What to do instead: compare event count, order, kind, pitch and velocity
// exactly, and timestamps within a measured tolerance.
// tests/test_helpers/midi_golden_compare.h does this and is the drop-in
// replacement.
//
// The rule fires only when BOTH appear in one file: a formatted note event
// stream (the thing whose offsets are FP-derived), AND a raw equality
// assertion against something named like a golden. Reading a golden, or
// asserting equality on unrelated values, is not flagged on its own.
//
// NOT flagged (deliberately):
//   * Equality over a SERIALIZED BYTE STREAM (saved plugin state, a parameter
//     save prefix). Those bytes are stored values round-tripped through memory,
//     not arithmetic results, so they are bit-identical everywhere -- and byte
//     identity is the correct way to pin a state format. See
//     tests/test_helpers/arp_shared_prefix_golden.h.
//
// Usage:
//   node tools/lint-midi-timing-goldens.js      # report and exit 1 on hits
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

const SCAN_DIRS = ['dsp', 'plugins', 'tests'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);
const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);

// Formatting a note event together with a sample offset -- i.e. the dump whose
// timestamps are FP-derived. Matches the "[%lld] noteOn  %d %d" family used by
// the arp golden harnesses.
const EVENT_DUMP_PATTERNS = [
  /"\s*\[%[a-z]*ll?d\]\s*note(?:On|Off)/i,
  /\[%lld\]\s*note(?:On|Off)/i,
];

// A raw equality assertion against a golden-ish operand.
const GOLDEN_EQUALITY_PATTERNS = [
  /\b(?:REQUIRE|CHECK|REQUIRE_THAT|CHECK_THAT)\s*\(\s*\w*golden\w*\s*==/i,
  /\b(?:REQUIRE|CHECK)\s*\(\s*\w+\s*==\s*\w*golden\w*\s*\)/i,
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

    const dumpHits = matchLines(lines, EVENT_DUMP_PATTERNS);
    if (dumpHits.length === 0) continue;
    const eqHits = matchLines(lines, GOLDEN_EQUALITY_PATTERNS);
    if (eqHits.length === 0) continue;

    violations.push({ file: path.relative(REPO_ROOT, file), dumpHits, eqHits });
  }

  if (violations.length === 0) {
    console.log(`lint-midi-timing-goldens: clean (${files.length} files scanned)`);
    return 0;
  }

  console.error('lint-midi-timing-goldens: byte-exact MIDI timing golden(s) found\n');
  for (const v of violations) {
    console.error(`  ${v.file}`);
    for (const h of v.dumpHits) console.error(`    L${h.line}: ${h.text}   <-- FP-derived event dump`);
    for (const h of v.eqHits) console.error(`    L${h.line}: ${h.text}   <-- compared byte-for-byte`);
    console.error('');
  }
  console.error(
    'Sample offsets in an event dump are floating-point math truncated to an\n' +
    'integer, so string equality asserts bit-exact FP across MSVC, GCC and\n' +
    'Apple Clang -- and the GCC/Clang legs build with -ffast-math. It cannot\n' +
    'hold: this is how the Ruinae SC-004b goldens broke Linux and macOS while\n' +
    'passing every Windows gate. Compare structure exactly and timestamps at a\n' +
    'measured tolerance -- see tests/test_helpers/midi_golden_compare.h.\n'
  );
  return 1;
}

try {
  process.exit(main());
} catch (err) {
  console.error('lint-midi-timing-goldens: internal error:', err && err.message);
  process.exit(2);
}
