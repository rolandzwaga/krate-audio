#!/usr/bin/env node
// T028 (SC-019): scope fence. The union of `git diff --name-only 285613c5` and
// `git status --porcelain --untracked-files=all` must match the Phase 10 allow-list.
// Prints each offending path and `SCOPE GREEN|RED`; exits 1 on RED.
'use strict';
const { execFileSync } = require('child_process');
const path = require('path');

const repo = path.resolve(__dirname, '..', '..', '..', '..');
const BASE = '285613c5';
const git = (args) => execFileSync('git', args, { cwd: repo, encoding: 'utf8', maxBuffer: 64 << 20 });

const paths = new Set();
for (const p of git(['diff', '--name-only', '-z', BASE]).split('\0')) if (p) paths.add(p);
// -z porcelain: "XY path\0"; a rename/copy entry is followed by its source path as a separate field.
const st = git(['status', '--porcelain', '--untracked-files=all', '-z']).split('\0');
for (let i = 0; i < st.length; i++) {
  const e = st[i];
  if (e.length < 4) continue;
  paths.add(e.slice(3));
  if (e[0] === 'R' || e[0] === 'C') paths.add(st[++i]);
}

const EXACT = new Set([
  'CMakeLists.txt', '.gitignore',
  '.github/workflows/ci.yml', '.github/workflows/release.yml', '.github/workflows/valgrind-nightly.yml',
  'tools/run-clang-tidy.ps1', 'tools/run-clang-tidy.sh', 'tools/check-changelog-coverage.js',
  'tools/gen-specs-index.js', 'tools/run-cpu-tests.js', 'tools/ci-test-shard.js', 'tools/run-close-lanes.js',
  '.claude/workflows/release-readiness.js', // FR-080, Q-B ruled 2026-10-22
  'dsp/include/krate/dsp/core/closed_form_adaa2.h', 'dsp/include/krate/dsp/systems/profundum_engine.h', // FR-006 D-3 renames, ruled 2026-10-22 (FR-090 exception)
  'specs/INDEX.md', 'specs/_architecture_/repo-map.json',
  'CLAUDE.md',
]);
const PREFIXES = ['plugins/profundum/', 'specs/profundum-phase10-plugin-scaffold/'];
const EXCLUDED = new Set(['.serena/project.yml']); // predates this phase (git status at 285613c5)
const FORBIDDEN = /^(dsp\/|tests\/test_helpers\/|plugins\/(?!profundum\/))/;

const sorted = [...paths].sort();
const offending = sorted.filter(
  (p) => !EXCLUDED.has(p) && !EXACT.has(p) && !PREFIXES.some((pre) => p.startsWith(pre)));
for (const p of offending) {
  console.log(`OFFENDING ${p}${FORBIDDEN.test(p) ? '  (FR-090: forbidden area -- stop and report)' : ''}`);
}
console.log(`scope-fence: ${sorted.length} path(s) checked against ${BASE}, ${offending.length} offending`);
console.log(offending.length === 0 ? 'SCOPE GREEN' : 'SCOPE RED');
process.exit(offending.length === 0 ? 0 : 1);
