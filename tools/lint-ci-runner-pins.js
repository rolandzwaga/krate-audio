#!/usr/bin/env node
//
// lint-ci-runner-pins.js
// =============================================================================
// Fails when a GitHub Actions job that compiles or runs the C++ tests runs on a floating Linux or Windows image
// label (`ubuntu-latest`, `windows-latest`), directly or through a matrix `os:` list.
//
// WHY. A floating label moves to a new image, and a new image brings a new compiler whose floating-point code
// generation differs. windows-latest moving to VS 2026 broke the bit-exact / tight-margin DSP tests (ci.yml pins
// windows-2022); ubuntu-latest moving to Ubuntu 26 (from 2026-10-19) contracted float multiply-adds into FMA and
// failed seven Linux shards on the first run (the arp shared-prefix golden read exactly the fused value). DSP
// numerical determinism needs a pinned compiler, and a pinned Linux image is also what the local WSL reproduces.
//
// A job counts as compiling/testing when its block mentions cmake, ctest, ci-test-shard, run-gate or valgrind.
// macos-latest is not flagged: pinning it picks an Xcode, which is a separate decision.
// A deliberate floating label carries `runner-pin-ok: <reason>` on the runs-on / os: line or the line above.
// Usage: node tools/lint-ci-runner-pins.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const WORKFLOWS = path.join(REPO_ROOT, '.github', 'workflows');
const FLOATING = /\b(?:ubuntu|windows)-latest\b/;
const BUILDS = /\bcmake\s+(?:--build|-S|-B|--preset)|\bctest\s+-|ci-test-shard\.js|run-gate\.js|\bvalgrind\s+-/;
const OK_MARK = 'runner-pin-ok:';
// Jobs that cannot carry the marker (none since release.yml generate-presets was pinned, 2026-11-14).
const KNOWN_FLOATING = new Set();

/** [{ line, job, text }] for one workflow's text. */
function findings(text) {
  const lines = text.split(/\r?\n/);
  const out = [];
  const jobsAt = lines.findIndex((l) => /^jobs:\s*$/.test(l));
  if (jobsAt < 0) return out;
  const jobStarts = [];
  for (let i = jobsAt + 1; i < lines.length; ++i) {
    if (/^\S/.test(lines[i])) break;
    const m = /^ {2}([\w-]+):\s*$/.exec(lines[i]);
    if (m) jobStarts.push({ name: m[1], at: i });
  }
  jobStarts.forEach((job, k) => {
    const end = k + 1 < jobStarts.length ? jobStarts[k + 1].at : lines.length;
    const block = lines.slice(job.at, end);
    if (!BUILDS.test(block.join('\n').replace(/#.*$/gm, ''))) return;
    block.forEach((raw, j) => {
      const code = raw.replace(/#.*$/, '');
      if (!/^\s*(?:runs-on:|os:)/.test(code) || !FLOATING.test(code)) return;
      const prev = block[j - 1] || '';
      if (raw.includes(OK_MARK) || prev.includes(OK_MARK)) return;
      out.push({ line: job.at + j + 1, job: job.name, text: raw.trim() });
    });
  });
  return out;
}

function selfTest() {
  const cases = [
    ['jobs:\n  build-linux:\n    runs-on: ubuntu-latest\n    steps:\n      - run: cmake --build build\n', 1],
    ['jobs:\n  build-linux:\n    runs-on: ubuntu-24.04\n    steps:\n      - run: cmake --build build\n', 0],
    ['jobs:\n  lint:\n    runs-on: ubuntu-latest\n    steps:\n      - run: node tools/lint-odr.js\n', 0],
    ['jobs:\n  long:\n    strategy:\n      matrix:\n        os: [windows-2022, macos-latest, ubuntu-latest]\n    runs-on: ${{ matrix.os }}\n    steps:\n      - run: node tools/ci-test-shard.js\n', 1],
    ['jobs:\n  mac:\n    runs-on: macos-latest\n    steps:\n      - run: cmake --build build\n', 0],
    ['jobs:\n  t:\n    # runner-pin-ok: smoke job, no numerics\n    runs-on: windows-latest\n    steps:\n      - run: ctest -j 4\n', 0],
  ];
  let failed = 0;
  for (const [src, want] of cases) {
    const got = findings(src).length;
    if (got !== want) { ++failed; console.log(`self-test FAIL: want ${want}, got ${got}:\n  ${src.replace(/\n/g, '\n  ')}`); }
  }
  console.log(failed ? `lint-ci-runner-pins self-test: ${failed} FAILED` : `lint-ci-runner-pins self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  let bad = 0;
  const files = fs.readdirSync(WORKFLOWS).filter((f) => /\.ya?ml$/.test(f));
  for (const f of files) {
    const rel = `.github/workflows/${f}`;
    for (const h of findings(fs.readFileSync(path.join(WORKFLOWS, f), 'utf8'))) {
      if (KNOWN_FLOATING.has(`${f}:${h.job}`)) continue;
      ++bad;
      console.log(`::error file=${rel},line=${h.line}::${rel}:${h.line}: job ${h.job} builds or tests on a floating image`);
      console.log(`  ${h.text}`);
    }
  }
  if (bad > 0) {
    console.log('lint-ci-runner-pins: FAILED. A floating ubuntu-latest / windows-latest label moves to a new compiler and\n' +
                'its floating-point codegen (FMA contraction, MSVC v144); pin the image (ubuntu-24.04, windows-2022), or\n' +
                `mark a job with no numerics "${OK_MARK} <reason>".`);
    return 1;
  }
  console.log(`lint-ci-runner-pins: OK (${files.length} workflows)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-ci-runner-pins: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
