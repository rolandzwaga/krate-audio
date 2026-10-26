#!/usr/bin/env node
// T030 scope fence (FR-061, FR-070): the Phase 11 change set must stay inside the allow-list.
// Union of `git diff --name-only 565f195d` and `git status --porcelain --untracked-files=all`.
// Any dsp/ path is RED. Prints offending paths and `SCOPE GREEN|RED`.
'use strict';
const { execFileSync } = require('child_process');
const path = require('path');

const BASE = '565f195d';
const repoRoot = execFileSync('git', ['rev-parse', '--show-toplevel'], { encoding: 'utf8' }).trim();
const git = (args) => execFileSync('git', args, { cwd: repoRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe'] });

const paths = new Set();
for (const p of git(['diff', '--name-only', '-z', BASE]).split('\0')) if (p) paths.add(p);

// Porcelain v1 with -z: "XY path\0", renames/copies carry the original path as the next NUL field.
const entries = git(['status', '--porcelain', '-z', '--untracked-files=all']).split('\0');
for (let i = 0; i < entries.length; ++i) {
  const e = entries[i];
  if (!e) continue;
  const xy = e.slice(0, 2);
  paths.add(e.slice(3));
  if (xy[0] === 'R' || xy[0] === 'C') {
    const orig = entries[++i];
    if (orig) paths.add(orig);
  }
}

const excludedByName = new Set(['.serena/project.yml']);
const allowPrefixes = ['plugins/profundum/', 'specs/profundum-phase11-parameters/'];
const allowExact = new Set([
  'plugins/shared/src/midi/sustain_latch.h',
  'plugins/shared/tests/test_sustain_latch.cpp',
  'plugins/shared/CMakeLists.txt',
  'plugins/shared/tests/CMakeLists.txt',
  'plugins/vorago/src/processor/sustain_latch.h',
  'specs/Profundum-roadmap.md',
  'tools/check-portability.js', // FR-073, user ruling 2026-10-22 (T032)
]);

const offending = [];
const dspPaths = [];
const sorted = [...paths].map((p) => p.split(path.sep).join('/')).sort();
for (const p of sorted) {
  if (excludedByName.has(p)) continue;
  if (p.startsWith('dsp/')) { dspPaths.push(p); offending.push(p); continue; }
  if (allowExact.has(p)) continue;
  if (allowPrefixes.some((pre) => p.startsWith(pre))) continue;
  offending.push(p);
}

console.log(`base ${BASE}; ${sorted.length} changed path(s) in the union (incl. excluded-by-name)`);
for (const p of sorted) console.log(`  ${excludedByName.has(p) ? '[excluded] ' : ''}${p}`);
if (dspPaths.length) {
  console.log(`dsp/ paths (FR-070 RED): ${dspPaths.length}`);
  for (const p of dspPaths) console.log(`  ${p}`);
}
console.log(`offending paths: ${offending.length}`);
for (const p of offending) console.log(`  ${p}`);
const green = offending.length === 0;
console.log(green ? 'SCOPE GREEN' : 'SCOPE RED');
process.exit(green ? 0 : 1);
