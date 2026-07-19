#!/usr/bin/env node
//
// lint-arch-guarded-includes.js
// =============================================================================
// Flags `#include <krate/...>` that sits inside an architecture-conditional
// preprocessor block.
//
// Why this is always a bug: every KrateDSP header is portable by construction.
// Headers that wrap platform intrinsics (e.g. core/scoped_denormal_mode.h) do
// their own per-architecture handling internally and degrade to a zero-cost
// no-op elsewhere. So an arch guard around the *include* buys nothing, and it
// silently removes the declarations on the other architecture -- while the code
// that uses them is virtually always written unconditionally.
//
// The failure mode is invisible on the machine you develop on. A Windows or
// Intel-Mac build takes the true branch and compiles fine; the arm64 leg of CI
// takes the false branch and dies with "no type named X in namespace Krate::DSP".
// That is exactly how PR #262 broke the macOS build: two <xmmintrin.h> includes
// living inside an x86 #if were swapped for a portable krate header, and the
// guard was left in place.
//
// Usage:
//   node tools/lint-arch-guarded-includes.js          # report and exit 1 on hits
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

const SCAN_DIRS = ['dsp', 'plugins', 'tools'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);

const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);

// Macros that mean "this branch only exists on some architectures".
const ARCH_MACROS = [
  '__x86_64__', '_M_X64', '__i386__', '_M_IX86', '_M_IX86_FP',
  '__SSE__', '__SSE2__', '__SSE3__', '__SSSE3__', '__SSE4_1__', '__SSE4_2__',
  '__AVX__', '__AVX2__', '__AVX512F__',
  '__aarch64__', '_M_ARM64', '__arm__', '_M_ARM', '__ARM_NEON', '__ARM_NEON__',
  '__wasm__', '__EMSCRIPTEN__',
];

const ARCH_RE = new RegExp(`\\b(${ARCH_MACROS.join('|')})\\b`);
const KRATE_INCLUDE_RE = /^\s*#\s*include\s*[<"]krate\//;

function* walk(dir) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const entry of entries) {
    if (entry.name.startsWith('.') && entry.name !== '.') continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (SKIP_DIR.has(entry.name)) continue;
      yield* walk(full);
    } else if (SOURCE_EXT.has(path.extname(entry.name))) {
      yield full;
    }
  }
}

/// Scan one file, returning a list of {line, text, guard} violations.
///
/// Tracks the #if/#ifdef nesting as a stack of booleans: "is this level (or any
/// enclosing level) architecture-conditional". A krate include is a violation
/// when any enclosing level is arch-conditional.
function scanFile(file) {
  const violations = [];
  let lines;
  try {
    lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);
  } catch {
    return violations;
  }

  // Each entry: { arch: bool, line: number, text: string }
  const stack = [];

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const directive = /^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$/.exec(line);

    if (directive) {
      const kind = directive[1];
      const rest = directive[2] || '';

      if (kind === 'if' || kind === 'ifdef' || kind === 'ifndef') {
        stack.push({ arch: ARCH_RE.test(rest), line: i + 1, text: line.trim() });
        continue;
      }
      if (kind === 'elif' || kind === 'else') {
        // An #else/#elif of an arch #if is still architecture-conditional.
        if (stack.length > 0 && (kind === 'elif' && ARCH_RE.test(rest))) {
          stack[stack.length - 1].arch = true;
        }
        continue;
      }
      if (kind === 'endif') {
        stack.pop();
        continue;
      }
    }

    if (!KRATE_INCLUDE_RE.test(line)) continue;

    const guard = stack.find((frame) => frame.arch);
    if (guard) {
      violations.push({
        line: i + 1,
        text: line.trim(),
        guardLine: guard.line,
        guardText: guard.text,
      });
    }
  }

  return violations;
}

function main() {
  let total = 0;
  const report = [];

  for (const dir of SCAN_DIRS) {
    const abs = path.join(REPO_ROOT, dir);
    if (!fs.existsSync(abs)) continue;
    for (const file of walk(abs)) {
      const violations = scanFile(file);
      if (violations.length === 0) continue;
      const rel = path.relative(REPO_ROOT, file).replace(/\\/g, '/');
      for (const v of violations) {
        total++;
        report.push(
          `${rel}:${v.line}: ${v.text}\n` +
          `    guarded by ${rel}:${v.guardLine}: ${v.guardText}`
        );
      }
    }
  }

  if (total === 0) {
    console.log('lint-arch-guarded-includes: OK — no krate includes behind architecture guards.');
    process.exit(0);
  }

  console.error(
    `lint-arch-guarded-includes: ${total} krate include(s) behind an architecture guard.\n`
  );
  for (const entry of report) console.error(entry + '\n');
  console.error(
    'KrateDSP headers are portable by construction; ones that wrap intrinsics handle\n' +
    'the per-architecture split internally and become a zero-cost no-op elsewhere.\n' +
    'Guarding the include removes the declarations on the other architecture while\n' +
    'the using code stays unconditional -- which compiles on x86 and fails on arm64.\n' +
    'Fix: include unconditionally and let the header decide.'
  );
  process.exit(1);
}

try {
  main();
} catch (err) {
  console.error('lint-arch-guarded-includes: internal error:', err);
  process.exit(2);
}
