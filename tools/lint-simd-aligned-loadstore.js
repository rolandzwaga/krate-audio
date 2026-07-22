#!/usr/bin/env node
//
// lint-simd-aligned-loadstore.js
// =============================================================================
// Flags aligned Highway Load/Store calls (`hn::Load(`, `hn::Store(`) in any
// C++ source. All Highway kernels in this repo must use LoadU/StoreU.
//
// Why aligned Load/Store is always a latent crash here: our kernels use
// hn::ScalableTag, which dispatches to the widest ISA available AT RUNTIME.
// Aligned Load requires the pointer to be aligned to the FULL VECTOR WIDTH of
// whatever target the dispatcher picked -- 16 bytes on SSE, 32 on AVX2, 64 on
// AVX-512. An `alignas(32)` array satisfies AVX2 but faults (#GP -> SIGSEGV)
// on AVX-512 whenever the array lands at 32-mod-64.
//
// The failure mode is maximally deceptive:
//   * Local dev machines without AVX-512 never execute the AVX-512 path, so
//     it never reproduces locally -- valgrind and ASan runs are green.
//   * GitHub's ubuntu runner fleet is mixed hardware (some Xeons with
//     AVX-512, some AMD without), so CI crashes intermittently depending on
//     which runner the job lands on. It looks exactly like a flaky test.
//
// This has now bitten twice: c2f76e50 ("Use unaligned SIMD loads/stores in
// all Highway code (fixes Linux SIGSEGV)") fixed it repo-wide in April 2026,
// and a later perf pass reintroduced aligned Load in
// modal_resonator_bank_simd.cpp, causing months of intermittent Linux CI
// SIGSEGVs. On post-2011 x86, LoadU/StoreU on an actually-aligned address
// costs the same as Load/Store, so there is no performance argument for the
// aligned forms with runtime dispatch.
//
// Usage:
//   node tools/lint-simd-aligned-loadstore.js   # exit 1 on hits
//
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');

const SCAN_DIRS = ['plugins', 'dsp', 'tests'];
const SOURCE_EXT = new Set(['.h', '.hpp', '.cpp', '.cc', '.mm']);
const SKIP_DIR = new Set(['build', 'node_modules', '.git', 'extern', '_deps']);

// Aligned Load/Store through the conventional `hn::` alias or the raw
// namespace. Deliberately does NOT match LoadU/StoreU/LoadN/StoreN/
// LoadDup128/MaskedLoad/etc. -- `(Load|Store)` must be followed directly by
// an open paren.
const CALL_RE = /\b(?:hn|hwy::HWY_NAMESPACE)::(Load|Store)\s*\(/;

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
    let text;
    try {
      text = fs.readFileSync(file, 'utf8');
    } catch {
      continue;
    }
    if (!/::(Load|Store)\s*\(/.test(text)) continue;

    text.split(/\r?\n/).forEach((line, i) => {
      const t = line.trimStart();
      if (t.startsWith('//') || t.startsWith('*')) return;
      if (CALL_RE.test(line)) {
        violations.push({
          rel: path.relative(REPO_ROOT, file),
          line: i + 1,
          text: line.trim(),
        });
      }
    });
  }

  if (violations.length === 0) {
    console.log(
      `lint-simd-aligned-loadstore: clean -- ${files.length} file(s) scanned.`);
    return 0;
  }

  console.error('lint-simd-aligned-loadstore: FAILED\n');
  console.error(
    'Aligned hn::Load / hn::Store with runtime dispatch (ScalableTag) faults\n' +
    'on AVX-512 runners when the buffer is only alignas(32). This crashes\n' +
    'Linux CI intermittently (runner hardware lottery) and never reproduces\n' +
    'on machines without AVX-512. Use hn::LoadU / hn::StoreU -- identical\n' +
    'cost on aligned addresses, cannot fault.\n'
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
  console.error('lint-simd-aligned-loadstore: internal error:', err);
  process.exit(2);
}
