#!/usr/bin/env node
// Which tests can a change reach? Lists the test TUs that (transitively) include a MODIFIED tracked file and do NOT
// carry the phase tag, with the executable each belongs to.
//
// WHY: the phase gates ran the WHOLE per-push set of an executable whenever a tracked file under dsp/include changed.
// dsp_systems_tests holds every instrument's systems tests, so editing profundum_engine.h (consumed only by
// [profundum] TUs) ran Seraphis and Vorago suites at every gate: ~11 min each, three times in one afternoon
// (Profundum Phase 14, 2026-10-26). The root CLAUDE.md rule is "the full suites of every CONSUMER of a modified
// shared file" — a consumer is a TU that includes it, not every TU linked into the same executable.
//
// Usage:
//   node tools/header-consumers.js --tag '[profundum]' [file ...]
//   With no files: the tracked files that `git status` shows MODIFIED under dsp/include, dsp/src, plugins/shared,
//   tests/test_helpers (new, untracked files never count: no existing test can include them unchanged).
// Last line:
//   CONSUMERS OUTSIDE <tag>: none                     -> the tag run already covers every consumer; run nothing else
//   CONSUMERS OUTSIDE <tag>: <exe> <exe> ...          -> run those executables' per-push sets, sharded (run-gate.js)
'use strict';
const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const ti = args.indexOf('--tag');
if (ti < 0 || !args[ti + 1]) { console.log("usage: node tools/header-consumers.js --tag '[profundum]' [file ...]"); process.exit(2); }
const TAG = args[ti + 1];
let changed = args.filter((a, i) => i !== ti && i !== ti + 1).map((f) => f.replace(/\\/g, '/'));
if (changed.length === 0) {
  const out = execSync('git status --porcelain -- dsp/include dsp/src plugins/shared tests/test_helpers', { cwd: ROOT }).toString();
  changed = out.split(/\r?\n/).filter((l) => /^( M|M |MM)/.test(l)).map((l) => l.slice(3).trim());
}
if (changed.length === 0) { console.log(`modified tracked shared files: none\nCONSUMERS OUTSIDE ${TAG}: none`); process.exit(0); }

// Every source file under dsp/, plugins/, tests/ (no build trees, no vendored code).
const files = [];
(function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (e.name.startsWith('.') || e.name === 'build' || e.name === 'extern' || e.name === 'node_modules') continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p);
    else if (/\.(h|hpp|cpp|cc|inl)$/.test(e.name)) files.push(path.relative(ROOT, p).replace(/\\/g, '/'));
  }
})(ROOT);
const keep = files.filter((f) => /^(dsp|plugins|tests)\//.test(f));

// Reverse include graph: an include string matches every file whose path ends with "/<string>" (or equals it), but
// only inside the includer's own plugin or the shared areas (dsp/, plugins/shared/, tests/): every plugin has its own
// "processor/processor.h" and "plugin_ids.h", and a plugin never includes another plugin's sources.
const area = (f) => { const m = /^plugins\/([^/]+)\//.exec(f); return m && m[1] !== 'shared' ? `plugins/${m[1]}` : 'shared'; };
const includers = new Map();   // file -> Set of files that include it
const byTail = new Map();      // basename -> files
for (const f of keep) { const b = path.posix.basename(f); if (!byTail.has(b)) byTail.set(b, []); byTail.get(b).push(f); }
const text = new Map();
for (const f of keep) {
  const s = fs.readFileSync(path.join(ROOT, f), 'utf8');
  text.set(f, s);
  for (const m of s.matchAll(/^\s*#\s*include\s*[<"]([^>"]+)[>"]/gm)) {
    const inc = m[1].replace(/\\/g, '/');
    for (const t of byTail.get(path.posix.basename(inc)) || []) {
      if (area(t) !== 'shared' && area(t) !== area(f)) continue;
      if (t === inc || t.endsWith('/' + inc)) { if (!includers.has(t)) includers.set(t, new Set()); includers.get(t).add(f); }
    }
  }
}

// Transitive closure from the changed files.
const reached = new Set(changed);
const queue = [...changed];
while (queue.length) {
  for (const g of includers.get(queue.pop()) || []) if (!reached.has(g)) { reached.add(g); queue.push(g); }
}

// Test TUs among them; a TU is covered when every TEST_CASE in it carries the tag.
const exeOf = (tu) => {
  const d = /^dsp\/tests\/unit\/(core|primitives|processors|systems|effects)\//.exec(tu);
  if (d) return `dsp_${d[1]}_tests (or dsp_ieee_tests if listed there)`;
  const p = /^plugins\/([^/]+)\/tests\//.exec(tu);
  if (p) return p[1] === 'iterum' ? 'plugin_tests / approval_tests' : `${p[1]}_tests`;
  if (/^tests\//.test(tu)) return 'every executable that compiles it';
  return '(unknown)';
};
const tus = [...reached].filter((f) => /\.cpp$/.test(f) && /(^|\/)tests\//.test(f)).sort();
const outside = [];
for (const tu of tus) {
  const cases = [...text.get(tu).matchAll(/^\s*(?:TEST_CASE|TEMPLATE_TEST_CASE|SCENARIO|TEST_CASE_METHOD)\s*\(([^)]*)\)/gm)].map((m) => m[1]);
  if (cases.length === 0) continue;
  const untagged = cases.filter((c) => !c.includes(TAG)).length;
  if (untagged) outside.push({ tu, untagged, total: cases.length, exe: exeOf(tu) });
}
console.log(`modified tracked shared files: ${changed.join(', ')}`);
console.log(`test TUs reaching them: ${tus.length}; carrying ${TAG} on every case: ${tus.length - outside.length}`);
for (const o of outside) console.log(`  OUTSIDE ${TAG}: ${o.tu} (${o.untagged}/${o.total} cases untagged) -> ${o.exe}`);
const exes = [...new Set(outside.map((o) => o.exe))];
console.log(`CONSUMERS OUTSIDE ${TAG}: ${exes.length ? exes.join(' ') : 'none'}`);
