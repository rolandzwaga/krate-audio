#!/usr/bin/env node
//
// lint-unordered-serialization.js
// =============================================================================
// Fails when a serializer iterates a std::unordered_map / std::unordered_set: a range-for over (or a begin() walk of)
// a container declared unordered in the same file or its paired header, inside a function whose name says it writes
// state (serialize*, getState, save*, write*, encode*, toJson*, dump*, store*).
//
// WHY. Unordered iteration order belongs to the standard library: MSVC's and libc++'s hash tables walk the same keys
// in different orders, and the order also follows insertion history. Bytes written in that order differ per
// platform: Disrumpo's controller wrote its MIDI CC blob back in a different order than it read it on macOS only
// (controller_state_codec_test D-117, 2026-11-14), while Windows happened to round-trip. Write in key order (walk
// the key range, or copy the keys and sort them).
//
// A deliberate order-free walk (summing, counting) inside such a function carries
// `unordered-serialization-ok: <reason>` on the loop's line or the line above.
// Usage: node tools/lint-unordered-serialization.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const ROOTS = ['dsp/include', 'dsp/src'];
const WRITER_RE = /^(?:serialize\w*|\w*Serialize\w*|getState|getComponentState|save\w*|write\w*|encode\w*|toJson\w*|dump\w*|store\w*)$/;
const DECL_RE = /std::unordered_(?:map|set|multimap|multiset)\s*<[^;{}]*?>\s+(\w+)\s*[;{=]/g;
const OK_MARK = 'unordered-serialization-ok:';

function sourceRoots() {
  const roots = ROOTS.map((r) => path.join(REPO_ROOT, r));
  const plugins = path.join(REPO_ROOT, 'plugins');
  for (const d of fs.existsSync(plugins) ? fs.readdirSync(plugins) : []) roots.push(path.join(plugins, d, 'src'));
  return roots.filter((r) => fs.existsSync(r));
}

function listSources(dir, out) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) listSources(p, out);
    else if (/\.(h|hpp|cpp|cc|mm)$/.test(e.name)) out.push(p);
  }
  return out;
}

function codeOnly(src) {
  return src.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\\n])*"/g, (m) => m.replace(/[^\n]/g, ' '));
}

function unorderedNames(code) {
  const names = new Set();
  for (const m of code.matchAll(DECL_RE)) names.add(m[1]);
  return names;
}

// [{ name, start, end }] for every function body: `name(...) [const] [noexcept] [-> T] {` ... matching `}`.
function functionBodies(code) {
  const out = [];
  const re = /\b(\w+)\s*\([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:->\s*[\w:<>,\s*&]+)?\{/g;
  for (const m of code.matchAll(re)) {
    if (/^(?:if|for|while|switch|catch|return|sizeof)$/.test(m[1])) continue;
    const open = m.index + m[0].length - 1;
    let depth = 0;
    let end = code.length;
    for (let i = open; i < code.length; ++i) {
      if (code[i] === '{') ++depth;
      else if (code[i] === '}' && --depth === 0) { end = i; break; }
    }
    out.push({ name: m[1], start: open, end });
  }
  return out;
}

/** Findings in one file: [{ line, fn, container, text }]. `extraNames`: unordered members from the paired header. */
function findings(src, extraNames = new Set()) {
  const code = codeOnly(src);
  const names = new Set([...unorderedNames(code), ...extraNames]);
  if (names.size === 0) return [];
  const lines = src.split(/\r?\n/);
  const out = [];
  const seen = new Set();
  for (const fn of functionBodies(code)) {
    if (!WRITER_RE.test(fn.name)) continue;
    const body = code.slice(fn.start, fn.end);
    const loopRe = /for\s*\([^;()]*:\s*(?:\*?\s*)?([\w.>-]+)\s*\)|\b([\w]+)\s*\.\s*c?begin\s*\(/g;
    for (const m of body.matchAll(loopRe)) {
      const ident = (m[1] || m[2] || '').split(/\.|->/).pop();
      if (!names.has(ident)) continue;
      const at = fn.start + m.index;
      const line = code.slice(0, at).split('\n').length;
      if (seen.has(line)) continue;
      seen.add(line);
      if ((lines[line - 1] || '').includes(OK_MARK) || (lines[line - 2] || '').includes(OK_MARK)) continue;
      out.push({ line, fn: fn.name, container: ident, text: (lines[line - 1] || '').trim() });
    }
  }
  return out;
}

function pairedHeaderNames(file) {
  if (!/\.(cpp|cc|mm)$/.test(file)) return new Set();
  for (const ext of ['.h', '.hpp']) {
    const h = file.replace(/\.(cpp|cc|mm)$/, ext);
    if (fs.existsSync(h)) return unorderedNames(codeOnly(fs.readFileSync(h, 'utf8')));
  }
  return new Set();
}

function selfTest() {
  const header = 'class M {\n  std::unordered_map<uint8_t, Mapping> globalMappings_;\n};';
  const cases = [
    ['std::vector<uint8_t> M::serializeGlobalMappings() const {\n  for (const auto& [cc, m] : globalMappings_) { out.push_back(cc); }\n}', 1],
    ['void M::getState(Stream* s) {\n  for (auto it = globalMappings_.begin(); it != globalMappings_.end(); ++it) {}\n}', 1],
    ['void M::serialize() const {\n  for (int cc = 0; cc < 256; ++cc) { auto it = globalMappings_.find(cc); }\n}', 0],
    ['void M::processEvents() {\n  for (const auto& [cc, m] : globalMappings_) {}\n}', 0],
    ['void M::saveAll() {\n  // unordered-serialization-ok: only counts\n  for (const auto& kv : globalMappings_) ++n;\n}', 0],
    ['void M::writeIds() {\n  std::unordered_set<int> ids;\n  for (int id : ids) out(id);\n}', 1],
  ];
  const extra = unorderedNames(codeOnly(header));
  let failed = 0;
  for (const [src, want] of cases) {
    const got = findings(src, extra).length;
    if (got !== want) { ++failed; console.log(`self-test FAIL: want ${want}, got ${got}:\n  ${src.replace(/\n/g, '\n  ')}`); }
  }
  console.log(failed ? `lint-unordered-serialization self-test: ${failed} FAILED` : `lint-unordered-serialization self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  let bad = 0;
  let files = 0;
  for (const root of sourceRoots()) {
    for (const file of listSources(root, [])) {
      ++files;
      const rel = path.relative(REPO_ROOT, file).split(path.sep).join('/');
      for (const f of findings(fs.readFileSync(file, 'utf8'), pairedHeaderNames(file))) {
        ++bad;
        console.log(`::error file=${rel},line=${f.line}::${rel}:${f.line}: ${f.fn}() iterates unordered container ${f.container}`);
        console.log(`  ${f.text}`);
      }
    }
  }
  if (bad > 0) {
    console.log('lint-unordered-serialization: FAILED. Unordered iteration order is the standard library\'s (MSVC and libc++\n' +
                'differ), so state written in it differs per platform. Write in key order, or mark an order-free walk with\n' +
                `"${OK_MARK} <reason>".`);
    return 1;
  }
  console.log(`lint-unordered-serialization: OK (${files} files)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-unordered-serialization: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
