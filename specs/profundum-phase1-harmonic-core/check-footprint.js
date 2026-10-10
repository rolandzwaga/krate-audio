#!/usr/bin/env node
// SC-023 footprint check for Profundum Phase 1 (specs/profundum-phase1-harmonic-core, task T002).
//
// Usage: node specs/profundum-phase1-harmonic-core/check-footprint.js [baseRef]
// Default baseRef: aa4788bb (the commit that added the Profundum roadmap, the phase base).
//
// Diffs the working tree against baseRef:
//  - four forbidden bank-family files must show 0 added and 0 removed lines;
//  - harmonic_oscillator_bank.h may only change within the restoreCenterPan() allow-list.
// Prints one PASS/FAIL line per file plus the overall verdict; exits non-zero on any FAIL.

'use strict';

const { execFileSync } = require('child_process');
const path = require('path');

const base = process.argv[2] || 'aa4788bb';
const repoRoot = path.resolve(__dirname, '..', '..');

const FORBIDDEN = [
  'dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.h',
  'dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.cpp',
  'dsp/include/krate/dsp/processors/harmonic_types.h',
  'dsp/include/krate/dsp/processors/additive_oscillator.h',
];

const BANK = 'dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h';

const ALLOWED_REMOVED = new Set([
  'constexpr float kCenterGain = 0.7071067811865476f; // sqrt(2)/2',
  'panLeft_.fill(kCenterGain);',
  'panRight_.fill(kCenterGain);',
]);
const MAX_REMOVED = 3;

const ALLOWED_ADDED = new Set([
  'static constexpr float kCenterPanGain = 0.7071067811865476f;',
  'panLeft_.fill(kCenterPanGain);',
  'panRight_.fill(kCenterPanGain);',
  'void restoreCenterPan() noexcept {',
  'void restoreCenterPan() noexcept',
  '{',
  '}',
]);

function git(args) {
  return execFileSync('git', args, { cwd: repoRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
}

// Returns { added, removed } from `git diff --numstat base -- file`.
function numstat(file) {
  const out = git(['diff', '--numstat', base, '--', file]).trim();
  if (out === '') return { added: 0, removed: 0 };
  let added = 0;
  let removed = 0;
  for (const line of out.split(/\r?\n/)) {
    const [a, r] = line.split('\t');
    // Binary diffs report '-'; treat as a change so they cannot slip through.
    added += a === '-' ? 1 : Number(a);
    removed += r === '-' ? 1 : Number(r);
  }
  return { added, removed };
}

// Returns { added: string[], removed: string[] } from `git diff -U0 base -- file`.
function diffLines(file) {
  const out = git(['diff', '-U0', base, '--', file]);
  const added = [];
  const removed = [];
  for (const raw of out.split(/\r?\n/)) {
    if (raw.startsWith('+++') || raw.startsWith('---')) continue;
    if (raw.startsWith('+')) added.push(raw.slice(1));
    else if (raw.startsWith('-')) removed.push(raw.slice(1));
  }
  return { added, removed };
}

let allPass = true;

function report(pass, file, detail) {
  if (!pass) allPass = false;
  console.log(`${pass ? 'PASS' : 'FAIL'}  ${file}  (${detail})`);
}

try {
  git(['rev-parse', '--verify', `${base}^{commit}`]);
} catch (e) {
  console.error(`FAIL  cannot resolve base ref '${base}'`);
  process.exit(2);
}

console.log(`SC-023 footprint check against ${base}`);

for (const file of FORBIDDEN) {
  const { added, removed } = numstat(file);
  report(added === 0 && removed === 0, file, `+${added} -${removed}, must be +0 -0`);
}

{
  const { added, removed } = diffLines(BANK);
  const problems = [];

  if (removed.length > MAX_REMOVED) {
    problems.push(`${removed.length} removed lines > ${MAX_REMOVED}`);
  }
  for (const line of removed) {
    const t = line.trim();
    if (!ALLOWED_REMOVED.has(t)) problems.push(`removed line not allowed: "${t}"`);
  }
  for (const line of added) {
    const t = line.trim();
    if (t === '' || t.startsWith('//')) continue; // blank or comment ('//' covers '///')
    if (!ALLOWED_ADDED.has(t)) problems.push(`added line not allowed: "${t}"`);
  }

  const summary = `+${added.length} -${removed.length}`;
  report(problems.length === 0, BANK, problems.length === 0 ? `${summary}, all within allow-list` : summary);
  for (const p of problems) console.log(`      ${p}`);
}

console.log(allPass ? 'OVERALL: PASS' : 'OVERALL: FAIL');
process.exit(allPass ? 0 : 1);
