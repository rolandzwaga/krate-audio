#!/usr/bin/env node
//
// lint-allocation-operator-overrides.js
// =============================================================================
// Flags any file other than tests/test_helpers/allocation_operator_overrides.h
// that defines a global `operator new` / `operator delete`.
//
// Why this is always a bug when hand-rolled: replacing these operators is only
// well-defined if the WHOLE matched set is replaced and the replacements are
// visible program-wide. Partial sets corrupt the heap, and the corruption is
// invisible where you develop:
//
//   * Windows/MSVC never shows it (different CRT).
//   * On Linux it surfaces as an intermittent, test-order-dependent SIGSEGV in
//     whatever test happens to run later -- so it never reproduces in isolation
//     and looks like a flaky test in an unrelated subsystem.
//
// Three hand-rolled copies had drifted into two distinct broken shapes:
//   1. new + delete under -fvisibility=hidden, which does NOT interpose
//      libstdc++, so a std::string allocated inside libstdc++ and destroyed in
//      executable code was freed by the TU's free().
//   2. Only new / new[] replaced, delete left at the library default. On glibc
//      the default delete does call free, so it looks fine -- but [new.delete]
//      requires the replaced pair to agree on which allocator owns the block.
//      AddressSanitizer on membrum_tests reported 694,513
//      "alloc-dealloc-mismatch (malloc vs operator delete)" errors, the first
//      during Catch2's static test registration.
//
// The shared header does it correctly once: throwing, nothrow, array and sized
// forms, all malloc/free, all default visibility. Include it from exactly one
// TU per test binary instead of writing your own.
//
// Usage:
//   node tools/lint-allocation-operator-overrides.js   # exit 1 on hits
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

const SCAN_DIRS = ['plugins', 'dsp', 'tests', 'tools'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);
const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);

// The single file allowed to define them.
const ALLOWED = new Set([
  path.join('tests', 'test_helpers', 'allocation_operator_overrides.h'),
]);

// A *definition* of a global allocation operator: `operator new` / `operator
// delete` at file scope, followed eventually by a brace. Declarations inside a
// class (e.g. a class-specific operator new) are namespaced by their class and
// are not global replacements, so require the line to start at column 0.
const DEF_RE = /^[A-Za-z_][\w:<>,\s*&\[\]()]*\boperator\s+(new|delete)\s*(\[\s*\])?\s*\(/;

function walk(dir, out) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return;
  }
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (SKIP_DIR.has(entry.name)) continue;
      walk(full, out);
    } else if (SOURCE_EXT.has(path.extname(entry.name))) {
      out.push(full);
    }
  }
}

function main() {
  const files = [];
  for (const d of SCAN_DIRS) walk(path.join(REPO_ROOT, d), files);

  const violations = [];
  for (const file of files) {
    const rel = path.relative(REPO_ROOT, file);
    if (ALLOWED.has(rel)) continue;

    let text;
    try {
      text = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }
    if (!/operator\s+(new|delete)/.test(text)) continue;

    text.split(/\r?\n/).forEach((line, i) => {
      if (line.trimStart().startsWith('//') || line.trimStart().startsWith('*')) return;
      if (DEF_RE.test(line)) {
        violations.push({ rel, line: i + 1, text: line.trim() });
      }
    });
  }

  if (violations.length === 0) {
    console.log(
      `lint-allocation-operator-overrides: clean -- ${files.length} file(s) scanned.`);
    return 0;
  }

  console.error('lint-allocation-operator-overrides: FAILED\n');
  console.error(
    'Do not hand-roll global operator new / delete. A partial or\n' +
    'hidden-visibility replacement set corrupts the heap, is invisible on\n' +
    'Windows, and surfaces on Linux as an intermittent SIGSEGV in an unrelated\n' +
    'test that never reproduces in isolation.\n\n' +
    'Include the shared definition from exactly one TU per test binary:\n\n' +
    '    #include <allocation_operator_overrides.h>\n'
  );
  console.error('\nViolations:\n');
  for (const v of violations) {
    console.error(`  ${v.rel}:${v.line}`);
    console.error(`      ${v.text}`);
  }
  console.error(`\n${violations.length} violation(s).`);
  return 1;
}

try {
  process.exit(main());
} catch (err) {
  console.error('lint-allocation-operator-overrides: internal error:', err);
  process.exit(2);
}
