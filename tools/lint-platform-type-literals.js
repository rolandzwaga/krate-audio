#!/usr/bin/env node
//
// lint-platform-type-literals.js
// =============================================================================
// Flags test code that names a `kPlatformType*` constant directly instead of
// calling Krate::TestSupport::nativePlatformType().
//
// Why this is always a bug: these constants are plain strings, so naming the
// wrong one COMPILES CLEANLY ON EVERY PLATFORM. Nothing in the build catches
// it -- not the compiler, not clang-tidy, and not tools/check-portability.js,
// which only compiles translation units under GCC. It fails at *runtime*:
// IPlugView::attached() gates on isPlatformTypeSupported(type), so passing
// another platform's constant returns kResultFalse and every assertion that
// follows collapses.
//
// The failure mode is therefore invisible on the machine you develop on. A
// Windows run passes with kPlatformTypeHWND; the Linux and macOS legs of CI go
// red on `attached(...) == kResultTrue` with the useless expansion `1 == 0`.
// That is exactly how the Gradus ring-edit routing tests broke both legs: the
// test copied the shape of editor_lifecycle_harness.h but pinned the Windows
// constant instead of reproducing its #if ladder.
//
// The fix is never to re-write the #if ladder in the test. Call
// Krate::TestSupport::nativePlatformType() from
// tests/test_helpers/editor_lifecycle_harness.h, which owns the one correct
// mapping for all three platforms.
//
// Usage:
//   node tools/lint-platform-type-literals.js        # report and exit 1 on hits
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

// Test trees only. Production code legitimately names these constants (a plugin
// really does report its own platform type to the host).
const SCAN_DIRS = ['plugins', 'dsp', 'tests'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);
const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);

// The single file allowed to name them: it owns the platform mapping.
const ALLOWED = new Set([
  path.join('tests', 'test_helpers', 'editor_lifecycle_harness.h'),
]);

const PLATFORM_CONSTANTS = [
  'kPlatformTypeHWND',
  'kPlatformTypeNSView',
  'kPlatformTypeUIView',
  'kPlatformTypeX11EmbedWindowID',
  'kPlatformTypeHIView',
];

const CONSTANT_RE = new RegExp(`\\b(${PLATFORM_CONSTANTS.join('|')})\\b`);

/** True when this path is inside a tests/ tree (that is where the hazard lives). */
function isTestFile(relPath) {
  const parts = relPath.split(path.sep);
  return parts.includes('tests') || parts.includes('test_helpers');
}

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
    if (!isTestFile(rel)) continue;
    if (ALLOWED.has(rel)) continue;

    let text;
    try {
      text = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }
    if (!CONSTANT_RE.test(text)) continue;

    text.split(/\r?\n/).forEach((line, i) => {
      // Ignore comments -- prose about the constants is fine and this file's
      // own docs mention them.
      const stripped = line.replace(/\/\/.*$/, '').replace(/\/\*.*?\*\//g, '');
      const m = stripped.match(CONSTANT_RE);
      if (m) violations.push({ rel, line: i + 1, name: m[1], text: line.trim() });
    });
  }

  if (violations.length === 0) {
    console.log(`lint-platform-type-literals: clean -- ${files.length} file(s) scanned.`);
    return 0;
  }

  console.error('lint-platform-type-literals: FAILED\n');
  console.error(
    'Test code must not name a platform-type constant directly. These are plain\n' +
    'strings, so the wrong one compiles everywhere and only fails at runtime on\n' +
    'the other OS -- IPlugView::attached() returns kResultFalse and CI goes red\n' +
    'with "1 == 0". A Windows-only run cannot catch it.\n\n' +
    'Use the platform mapping that already exists:\n\n' +
    '    #include <editor_lifecycle_harness.h>\n' +
    '    view->attached(nullptr, Krate::TestSupport::nativePlatformType());\n'
  );
  console.error('\nViolations:\n');
  for (const v of violations) {
    console.error(`  ${v.rel}:${v.line}  ${v.name}`);
    console.error(`      ${v.text}`);
  }
  console.error(`\n${violations.length} violation(s).`);
  return 1;
}

try {
  process.exit(main());
} catch (err) {
  console.error('lint-platform-type-literals: internal error:', err);
  process.exit(2);
}
