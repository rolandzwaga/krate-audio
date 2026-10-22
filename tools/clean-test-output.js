#!/usr/bin/env node
// clean-test-output.js — remove regenerable test / build / trace output that accumulates on F:.
//
// Standing rule (user, 2026-10-20): every phase close and every long probe run ends by cleaning up its own output.
// Raw traces, scratch build directories, exe copies kept for A/B pairing and F:\tmp scratch dirs are all regenerable;
// the evidence that matters is the exported text (profiles, logs, probes), which this script never touches.
//
// Usage:  node tools/clean-test-output.js            # dry run: lists what would go, with sizes
//         node tools/clean-test-output.js --apply    # delete it
//         node tools/clean-test-output.js --apply --keep-prof   # keep build/windows-x64-prof (a trace is pending)
//
// Never walks a whole drive: every candidate is an explicit path or a bounded glob under the repo / F:\tmp.
'use strict';
const fs = require('fs');
const path = require('path');

const REPO = path.resolve(__dirname, '..');
const apply = process.argv.includes('--apply');
const keepProf = process.argv.includes('--keep-prof');

/** Bounded candidate list. */
function candidates() {
  const out = [];
  // 1. F:\tmp scratch (every entry).
  const tmp = 'F:\\tmp';
  if (fs.existsSync(tmp)) for (const e of fs.readdirSync(tmp)) out.push(path.join(tmp, e));
  // 2. build/: everything but the four working configurations.
  const keep = new Set(['windows-x64-release', 'windows-ninja', 'linux-release']);
  if (!keepProf) keep.delete('windows-x64-prof'); else keep.add('windows-x64-prof');
  const build = path.join(REPO, 'build');
  if (fs.existsSync(build)) for (const e of fs.readdirSync(build)) if (!keep.has(e)) out.push(path.join(build, e));
  // 3. exe copies kept for paired A/B timing.
  const bin = path.join(build, 'windows-x64-release', 'bin', 'Release');
  if (fs.existsSync(bin)) for (const e of fs.readdirSync(bin)) if (/_tests_[a-z0-9]+\.exe$/i.test(e) && !/_tests\.exe$/i.test(e)) out.push(path.join(bin, e));
  // 4. raw ETW traces and their NGENPDB folders under specs/*/evidence (the exported .txt profiles stay).
  const specs = path.join(REPO, 'specs');
  if (fs.existsSync(specs)) {
    for (const spec of fs.readdirSync(specs)) {
      const ev = path.join(specs, spec, 'evidence');
      if (!fs.existsSync(ev)) continue;
      walkBounded(ev, 4, (p) => { if (/\.etl$/i.test(p) || /\.etl\.NGENPDB$/i.test(p)) out.push(p); });
    }
  }
  return out;
}

/** Depth-bounded walk (evidence trees are shallow). */
function walkBounded(dir, depth, visit) {
  if (depth < 0) return;
  let entries;
  try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const e of entries) {
    const p = path.join(dir, e.name);
    visit(p);
    if (e.isDirectory() && !/\.NGENPDB$/i.test(e.name)) walkBounded(p, depth - 1, visit);
  }
}

function sizeOf(p) {
  let total = 0;
  const st = fs.statSync(p);
  if (!st.isDirectory()) return st.size;
  const stack = [p];
  while (stack.length) {
    const d = stack.pop();
    let entries;
    try { entries = fs.readdirSync(d, { withFileTypes: true }); } catch { continue; }
    for (const e of entries) {
      const q = path.join(d, e.name);
      if (e.isDirectory()) stack.push(q);
      else { try { total += fs.statSync(q).size; } catch { /* vanished */ } }
    }
  }
  return total;
}

const list = [...new Set(candidates())].filter((p) => fs.existsSync(p));
let total = 0;
for (const p of list) {
  const s = sizeOf(p);
  total += s;
  console.log(`${(s / 1048576).toFixed(1).padStart(9)} MB  ${p}`);
}
console.log(`${list.length} entries, ${(total / 1073741824).toFixed(2)} GB ${apply ? 'removed' : 'would be removed (add --apply)'}`);
if (apply) for (const p of list) fs.rmSync(p, { recursive: true, force: true });
