#!/usr/bin/env node
//
// lint-allocation-probes.js
// =============================================================================
// Fails when a test's allocation-detector non-vacuity check ("the detector counts in this binary") allocates through a
// local object the optimiser may remove: an assertion that allocations happened (getAllocationCount() > 0 / >= 1 /
// != 0, or hadAllocations()) preceded within a few lines by a local std::vector / std::string / make_unique /
// make_shared / new-expression, and not by TestHelpers::allocateObservably().
//
// WHY. A new-expression whose result never escapes may be elided together with its allocation (C++14
// [expr.new]/10). Clang does it at -O2: Disrumpo's D-003 probe `std::vector<int> v(16); v[0] = 1;` read 0 allocations
// on macOS (2026-11-14) while MSVC kept the call. allocateObservably() stores the pointer through a volatile, so the
// allocation is observable on every compiler.
//
// Checks that read allocations made by the code under test (another translation unit) are not probes: they carry
// `allocation-probe-ok: <reason>` on the assertion's line or the line above when this heuristic catches them.
// Usage: node tools/lint-allocation-probes.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const ROOTS = ['tests', 'dsp/tests'];
const POSITIVE_RE = /(?:getAllocationCount\s*\(\s*\)\s*(?:>\s*0|>=\s*1|!=\s*0)|(?<![!\w])\w+\s*\.\s*hadAllocations\s*\(\s*\)\s*\))/;
const LOCAL_ALLOC_RE = /\bstd::(?:vector|string|make_unique|make_shared|deque|list|map)\b|\bnew\s+[\w:<]/;
const OBSERVABLE = 'allocateObservably(';
const OK_MARK = 'allocation-probe-ok:';
const WINDOW = 8;

function testRoots() {
  const roots = ROOTS.map((r) => path.join(REPO_ROOT, r));
  const plugins = path.join(REPO_ROOT, 'plugins');
  for (const d of fs.existsSync(plugins) ? fs.readdirSync(plugins) : []) roots.push(path.join(plugins, d, 'tests'));
  return roots.filter((r) => fs.existsSync(r));
}

function listSources(dir, out) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) listSources(p, out);
    else if (/\.(h|hpp|cpp|cc)$/.test(e.name)) out.push(p);
  }
  return out;
}

function stripComment(line) {
  return line.replace(/\/\/.*$/, '');
}

/** Findings in one file's text: [{ line, text }]. */
function findings(src) {
  const lines = src.split(/\r?\n/);
  const out = [];
  lines.forEach((raw, i) => {
    const code = stripComment(raw);
    if (!/\b(?:REQUIRE|CHECK)\s*\(/.test(code) || !POSITIVE_RE.test(code)) return;
    if (raw.includes(OK_MARK) || (lines[i - 1] || '').includes(OK_MARK)) return;
    const before = lines.slice(Math.max(0, i - WINDOW), i).map(stripComment);
    if (before.some((l) => l.includes(OBSERVABLE))) return;
    if (!before.some((l) => LOCAL_ALLOC_RE.test(l))) return;
    out.push({ line: i + 1, text: raw.trim() });
  });
  return out;
}

function selfTest() {
  const cases = [
    ['TestHelpers::ThreadScopedAllocationScope probe;\nstd::vector<int> v(16);\nv[0] = 1;\nREQUIRE(TestHelpers::AllocationDetector::instance().getAllocationCount() > 0u);', 1],
    ['TestHelpers::ThreadScopedAllocationScope probe;\nTestHelpers::allocateObservably();\nREQUIRE(TestHelpers::AllocationDetector::instance().getAllocationCount() > 0u);', 0],
    ['auto p = std::make_unique<int>(1);\nCHECK(scope.hadAllocations());', 1],
    ['std::vector<float> buf(512);\nprocessor.process(buf);\nREQUIRE(detector.getAllocationCount() == 0);', 0],
    ['proc.setState(stream);\nCHECK(scope.hadAllocations());', 0],
    ['std::vector<float> buf(512);\n// allocation-probe-ok: setState allocates in the plugin TU\nCHECK(scope.hadAllocations());', 0],
    ['std::string s(100, \'x\');\nREQUIRE_FALSE(scope.hadAllocations());', 0],
  ];
  let failed = 0;
  for (const [src, want] of cases) {
    const got = findings(src).length;
    if (got !== want) { ++failed; console.log(`self-test FAIL: want ${want}, got ${got}:\n  ${src.replace(/\n/g, '\n  ')}`); }
  }
  console.log(failed ? `lint-allocation-probes self-test: ${failed} FAILED` : `lint-allocation-probes self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  let bad = 0;
  let files = 0;
  for (const root of testRoots()) {
    for (const file of listSources(root, [])) {
      ++files;
      const rel = path.relative(REPO_ROOT, file).split(path.sep).join('/');
      for (const f of findings(fs.readFileSync(file, 'utf8'))) {
        ++bad;
        console.log(`::error file=${rel},line=${f.line}::${rel}:${f.line}: allocation probe the optimiser may remove`);
        console.log(`  ${f.text}`);
      }
    }
  }
  if (bad > 0) {
    console.log('lint-allocation-probes: FAILED. A local allocation that never escapes may be elided (clang -O2 does), so a\n' +
                '"the detector counts" check reads 0. Allocate with TestHelpers::allocateObservably(), or mark a check\n' +
                `that reads the code under test's allocations with "${OK_MARK} <reason>".`);
    return 1;
  }
  console.log(`lint-allocation-probes: OK (${files} files)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-allocation-probes: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
