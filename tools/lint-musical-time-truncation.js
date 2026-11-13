#!/usr/bin/env node
//
// lint-musical-time-truncation.js
// =============================================================================
// Fails when DSP or plugin source truncates host musical time to an integer: a static_cast to an integer type (or a
// C-style integer cast) whose argument reads a PPQ position (`ppq`, `projectTimeMusic`) or a step fraction
// (`stepFraction`) and is not rounded with std::round / lround / llround / rint.
//
// WHY. A PPQ position converts to a sample count that is very often an exact integer: a 1/8 step at 120 BPM and
// 44.1 kHz is 11025 samples, so `stepFraction * stepDuration` at a block start is exactly `pos % 11025`. The double
// product lands an ulp below that integer at a large share of block starts (208 of 703 64-sample starts in one
// second), and a truncating cast then returns integer - 1. Which block starts miss depends on where the buffers
// start and on the floating-point mode, so the result moves by a sample with the buffer size, and the macOS
// -ffast-math leg moves it again: ArpeggiatorCore's PPQ resync failed Ruinae's 2048-vs-64 arp render there
// (2026-11-14, 0.76 at sample 44951) after passing on Windows and Linux. Round to nearest instead.
//
// A deliberate truncation (a step INDEX from a fraction, say) carries `musical-time-truncation-ok: <reason>` on the
// cast's line or the line above. Usage: node tools/lint-musical-time-truncation.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const ROOTS = ['dsp/include', 'dsp/src'];
const INT_TYPES = '(?:std::)?(?:size_t|ptrdiff_t|u?int(?:8|16|32|64)?_t|int|long(?:\\s+long)?|unsigned(?:\\s+int)?|' +
    'Steinberg::(?:u?int32|u?int64|TSamples))';
const CAST_RE = new RegExp(`static_cast<\\s*${INT_TYPES}\\s*>\\s*\\(|\\(\\s*${INT_TYPES}\\s*\\)\\s*\\(`, 'g');
const TIME_RE = /\b\w*(?:ppq|projectTimeMusic|stepFraction)\w*\b/i;
const ROUND_RE = /\b(?:std::)?(?:l?l?round|l?l?rint|nearbyint)[fl]?\s*\(/;
const OK_MARK = 'musical-time-truncation-ok:';

function sourceRoots() {
  const roots = ROOTS.map((r) => path.join(REPO_ROOT, r));
  const plugins = path.join(REPO_ROOT, 'plugins');
  for (const d of fs.existsSync(plugins) ? fs.readdirSync(plugins) : []) roots.push(path.join(plugins, d, 'src'));
  return roots.filter((r) => fs.existsSync(r));
}

function listSources(dir, out) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) listSources(p, out);
    else if (/\.(h|hpp|cpp|cc|mm)$/.test(e.name)) out.push(p);
  }
  return out;
}

// Blank out // and /* */ comments and string literals, keeping offsets and newlines.
function codeOnly(src) {
  return src.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\\n])*"/g, (m) => m.replace(/[^\n]/g, ' '));
}

// The argument of the cast whose opening '(' is at `open`, up to its matching ')'.
function argumentAt(code, open) {
  let depth = 0;
  for (let i = open; i < code.length; ++i) {
    if (code[i] === '(') ++depth;
    else if (code[i] === ')' && --depth === 0) return code.slice(open + 1, i);
  }
  return code.slice(open + 1);
}

/** Findings in one file's text: [{ line, text }]. */
function findings(src) {
  const code = codeOnly(src);
  const lines = src.split(/\r?\n/);
  const out = [];
  for (const m of code.matchAll(CAST_RE)) {
    const open = m.index + m[0].length - 1;
    const arg = argumentAt(code, open);
    if (!TIME_RE.test(arg) || ROUND_RE.test(arg)) continue;
    const line = code.slice(0, m.index).split('\n').length;
    if ((lines[line - 1] || '').includes(OK_MARK) || (lines[line - 2] || '').includes(OK_MARK)) continue;
    out.push({ line, text: (lines[line - 1] || '').trim() });
  }
  return out;
}

function selfTest() {
  const cases = [
    ['const size_t c = static_cast<size_t>(\n    pos.stepFraction * static_cast<double>(dur));', 1],
    ['x = static_cast<int64_t>(pc->projectTimeMusic * (60.0 / bpm) * sr);', 1],
    ['int s = (int)(hostPpq * 4.0);', 1],
    ['const auto n = static_cast<size_t>(std::llround(pos.stepFraction * d));', 0],
    ['return static_cast<int64_t>(std::llround(ppq * (60.0 / bpm) * sr));', 0],
    ['const int step = static_cast<int>(posInPattern / beatsPerStep) % n;', 0],
    ['// static_cast<size_t>(stepFraction * d) in a comment', 0],
    ['// musical-time-truncation-ok: a step index\nconst int i = static_cast<int>(stepFraction * 4.0);', 0],
    ['const float f = static_cast<float>(projectTimeMusic);', 0],
  ];
  let failed = 0;
  for (const [src, want] of cases) {
    const got = findings(src).length;
    if (got !== want) { ++failed; console.log(`self-test FAIL: want ${want}, got ${got}:\n  ${src.replace(/\n/g, '\n  ')}`); }
  }
  console.log(failed ? `lint-musical-time-truncation self-test: ${failed} FAILED` : `lint-musical-time-truncation self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  let bad = 0;
  let files = 0;
  for (const root of sourceRoots()) {
    for (const file of listSources(root, [])) {
      ++files;
      const rel = path.relative(REPO_ROOT, file).split(path.sep).join('/');
      for (const f of findings(fs.readFileSync(file, 'utf8'))) {
        ++bad;
        console.log(`::error file=${rel},line=${f.line}::${rel}:${f.line}: truncating cast of musical time`);
        console.log(`  ${f.text}`);
      }
    }
  }
  if (bad > 0) {
    console.log('lint-musical-time-truncation: FAILED. A PPQ position or step fraction converted to samples is often an exact\n' +
                'integer that the double lands an ulp below; a truncating cast then loses a sample, differently per buffer\n' +
                'size and per FP mode (macOS -ffast-math). Use std::llround, or mark a deliberate truncation with\n' +
                `"${OK_MARK} <reason>".`);
    return 1;
  }
  console.log(`lint-musical-time-truncation: OK (${files} files)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-musical-time-truncation: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
