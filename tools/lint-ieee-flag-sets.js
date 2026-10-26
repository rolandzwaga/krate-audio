#!/usr/bin/env node
//
// lint-ieee-flag-sets.js
// =============================================================================
// Fails when a tracked CMakeLists.txt / *.cmake line passes -fno-fast-math without -ffp-contract=off on the same line.
//
// WHY. -fno-fast-math turns reassociation, reciprocal math and finite-math-only off, but it leaves clang's DEFAULT
// -ffp-contract=on, which fuses `a * b + c * d` into fma(a, b, c * d) wherever the CPU has FMA. arm64 always has it,
// so on the macOS leg an "IEEE" TU still contracts, and two sites of the same expression may fuse DIFFERENT products
// (arm64 and x86 clang pick different ones for `p * gd + q * gd`). ProfundumVoice_OutputIdentity went red inside
// dsp_ieee_tests that way on 2026-10-25: 31 of 144 declick-dip samples one ulp off the voice's own sum. Windows (no
// FMA without /arch:AVX2) and Linux x86 (no FMA by default) never show it. An IEEE flag set is therefore
// "-fno-fast-math -fno-finite-math-only -ffp-contract=off", written on one line so this gate can see it.
//
// Comments (# to end of line) are ignored. Usage: node tools/lint-ieee-flag-sets.js
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

function trackedCMakeFiles() {
  const out = execSync('git ls-files -- "*CMakeLists.txt" "*.cmake"', { cwd: REPO_ROOT, encoding: 'utf8' });
  return out.split(/\r?\n/).filter((f) => f && !f.startsWith('extern/'));
}

// The code part of a CMake line: everything before an unquoted '#'.
function codeOf(line) {
  let inQuote = false;
  for (let i = 0; i < line.length; ++i) {
    const ch = line[i];
    if (ch === '"' && line[i - 1] !== '\\') inQuote = !inQuote;
    else if (ch === '#' && !inQuote) return line.slice(0, i);
  }
  return line;
}

function main() {
  let bad = 0;
  let sites = 0;
  for (const rel of trackedCMakeFiles()) {
    const lines = fs.readFileSync(path.join(REPO_ROOT, rel), 'utf8').split(/\r?\n/);
    lines.forEach((raw, idx) => {
      const code = codeOf(raw);
      if (!/-fno-fast-math\b/.test(code)) return;
      ++sites;
      if (!/-ffp-contract=off\b/.test(code)) {
        ++bad;
        console.log(`::error file=${rel},line=${idx + 1}::${rel}:${idx + 1}: -fno-fast-math without -ffp-contract=off`);
        console.log(`  ${raw.trim()}`);
      }
    });
  }
  if (bad > 0) {
    console.log('lint-ieee-flag-sets: FAILED. -fno-fast-math keeps clang\'s -ffp-contract=on, which fuses a*b + c*d into an\n' +
                'fma on arm64; an IEEE flag set is "-fno-fast-math -fno-finite-math-only -ffp-contract=off" on one line.');
    return 1;
  }
  console.log(`lint-ieee-flag-sets: clean (${sites} IEEE flag sets)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-ieee-flag-sets: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
