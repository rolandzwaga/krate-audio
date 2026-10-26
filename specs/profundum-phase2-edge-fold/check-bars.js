#!/usr/bin/env node
// SC-027 bar-integrity check and SC-023 presence check (FR-055).
// Profundum Phase 2 (specs/profundum-phase2-edge-fold), task T003. Node, no dependencies.
//
// Usage (from any directory):
//   node check-bars.js baseline [spec.md]   extract every numeric token of each SC threshold cell
//                                           and write bars-baseline.json next to this script
//   node check-bars.js check    [spec.md]   re-extract, diff against bars-baseline.json; a changed SC is
//                                           allowed only if a Clarifications entry names the SC id, the
//                                           word "re-rul" (case-insensitive) and a measurement figure.
//                                           Exit 1 on any CHANGED-UNRULED.
//   node check-bars.js sc023    [plan.md]   assert plan.md has a "SC-023 — compression mechanism record"
//                                           section whose table has a row with "Chosen" in its Status cell.
//
// The optional 2nd argument replaces the default spec.md / plan.md in this folder (used to run the
// positive control on an edited copy). The baseline file is always the one next to this script.

'use strict';

const fs = require('fs');
const path = require('path');

const HERE = __dirname;
const BASELINE_PATH = path.join(HERE, 'bars-baseline.json');

// Numeric token: signed decimal with an optional unit (tasks.md T003). U+2212 is normalised to '-'
// before matching. 'dBFS' is listed before 'dB' so it wins; a unit followed by a letter is not a unit
// ("0 samples" yields "0", not "0 s").
const TOKEN_RE = /-?\d+(?:\.\d+)?(?:\s*(?:dBFS|dB|%|°|oct|ms|s|×)(?![A-Za-z]))?/g;
// A measurement figure in a re-ruling entry: a number carrying a unit, or a decimal number.
const FIGURE_RE = /-?\d+(?:\.\d+)?\s*(?:dBFS|dB|%|°|oct|ms|s|×)(?![A-Za-z])|-?\d+\.\d+/;

function fail(msg) {
  console.error(`check-bars: ${msg}`);
  process.exit(2);
}

function readText(p) {
  if (!fs.existsSync(p)) fail(`file not found: ${p}`);
  return fs.readFileSync(p, 'utf8').replace(/\r\n/g, '\n');
}

// Lines of the level-2 section whose heading starts with `title`, up to the next level-2 heading.
function sectionLines(text, title) {
  const lines = text.split('\n');
  const start = lines.findIndex((l) => l.startsWith(`## ${title}`));
  if (start < 0) return null;
  let end = lines.length;
  for (let i = start + 1; i < lines.length; ++i) {
    if (/^## /.test(lines[i])) { end = i; break; }
  }
  return lines.slice(start + 1, end);
}

// Split a markdown table row into cells; '\|' is an escaped pipe inside a cell.
function splitRow(line) {
  const cells = line.split(/(?<!\\)\|/).map((c) => c.trim());
  // Leading and trailing pipes produce empty first/last cells.
  return cells.slice(1, cells.length - 1);
}

function extractTokens(cell) {
  const norm = cell.replace(/−/g, '-');
  return (norm.match(TOKEN_RE) || []).map((t) => t.replace(/\s+/g, ' '));
}

function extractBars(specPath) {
  const sc = sectionLines(readText(specPath), 'Success Criteria');
  if (!sc) fail(`no "## Success Criteria" section in ${specPath}`);
  const bars = {};
  for (const line of sc) {
    if (!line.startsWith('|')) continue;
    const cells = splitRow(line);
    const m = /^\*\*(SC-0\d\d)\*\*$/.exec(cells[0] || '');
    if (!m) continue;
    if (cells.length < 3) fail(`${m[1]}: row has no threshold cell`);
    if (bars[m[1]]) fail(`${m[1]}: duplicate row`);
    bars[m[1]] = extractTokens(cells[2]);
  }
  if (Object.keys(bars).length === 0) fail(`no **SC-0NN** rows in the Success Criteria table of ${specPath}`);
  return bars;
}

// Clarifications entries: each bullet ('- ') with its continuation lines is one logical line, so a
// re-ruling wrapped across physical lines still counts as one entry.
function clarificationEntries(specPath) {
  const lines = sectionLines(readText(specPath), 'Clarifications') || [];
  const entries = [];
  for (const line of lines) {
    if (/^\s*[-*] /.test(line)) entries.push(line.trim());
    else if (line.trim() === '' || line.startsWith('#')) entries.push(null);
    else if (entries.length && entries[entries.length - 1] !== null) entries[entries.length - 1] += ' ' + line.trim();
    else entries.push(line.trim());
  }
  return entries.filter((e) => e);
}

function isRuled(id, entries) {
  const idRe = new RegExp(`${id}(?!\\d)`);
  return entries.some((e) => idRe.test(e) && /re-rul/i.test(e) && FIGURE_RE.test(e.replace(/−/g, '-')));
}

// Multiset difference a − b.
function minus(a, b) {
  const counts = new Map();
  for (const t of b) counts.set(t, (counts.get(t) || 0) + 1);
  const out = [];
  for (const t of a) {
    const n = counts.get(t) || 0;
    if (n > 0) counts.set(t, n - 1);
    else out.push(t);
  }
  return out;
}

function sameList(a, b) {
  return a.length === b.length && a.every((t, i) => t === b[i]);
}

function sortIds(ids) {
  return [...ids].sort((x, y) => Number(x.slice(3)) - Number(y.slice(3)));
}

function runBaseline(specPath) {
  const bars = extractBars(specPath);
  const ordered = {};
  for (const id of sortIds(Object.keys(bars))) ordered[id] = bars[id];
  fs.writeFileSync(BASELINE_PATH, JSON.stringify(ordered, null, 2) + '\n');
  const total = Object.values(ordered).reduce((s, v) => s + v.length, 0);
  console.log(`baseline: ${Object.keys(ordered).length} SC rows, ${total} tokens from ${specPath}`);
  console.log(`wrote ${BASELINE_PATH}`);
}

function runCheck(specPath) {
  if (!fs.existsSync(BASELINE_PATH)) fail(`no baseline at ${BASELINE_PATH}; run "node check-bars.js baseline" first`);
  const base = JSON.parse(fs.readFileSync(BASELINE_PATH, 'utf8'));
  const now = extractBars(specPath);
  const entries = clarificationEntries(specPath);
  let unruled = 0;
  for (const id of sortIds(new Set([...Object.keys(base), ...Object.keys(now)]))) {
    const b = base[id];
    const n = now[id];
    if (b && n && sameList(b, n)) {
      console.log(`${id} SAME`);
      continue;
    }
    let detail;
    if (!b) detail = 'row added';
    else if (!n) detail = 'row removed';
    else {
      const removed = minus(b, n);
      const added = minus(n, b);
      detail = removed.length || added.length
        ? `removed [${removed.join(', ')}] added [${added.join(', ')}]`
        : 'token order changed';
    }
    if (isRuled(id, entries)) {
      console.log(`${id} CHANGED+RULED (${detail})`);
    } else {
      console.log(`${id} CHANGED-UNRULED (${detail})`);
      ++unruled;
    }
  }
  console.log(unruled ? `FAIL: ${unruled} SC row(s) changed without a re-ruling entry` : 'PASS');
  process.exit(unruled ? 1 : 0);
}

function runSc023(planPath) {
  const lines = readText(planPath).split('\n');
  const start = lines.findIndex((l) => /^#+ .*SC-023 [—-] compression mechanism record/.test(l));
  if (start < 0) {
    console.log(`FAIL: no "SC-023 — compression mechanism record" section in ${planPath}`);
    process.exit(1);
  }
  let end = lines.length;
  for (let i = start + 1; i < lines.length; ++i) {
    if (/^#+ /.test(lines[i])) { end = i; break; }
  }
  const rows = lines.slice(start + 1, end).filter((l) => l.startsWith('|')).map(splitRow);
  const header = rows[0] || [];
  const statusCol = header.findIndex((c) => c === 'Status');
  if (statusCol < 0) {
    console.log(`FAIL: the SC-023 section has no table with a Status column (${planPath}:${start + 1})`);
    process.exit(1);
  }
  const data = rows.slice(1).filter((r) => !r.every((c) => /^:?-+:?$/.test(c)));
  const chosen = data.filter((r) => (r[statusCol] || '').includes('Chosen'));
  if (chosen.length === 0) {
    console.log(`FAIL: no row of the SC-023 table has "Chosen" in its Status cell (${data.length} rows)`);
    process.exit(1);
  }
  console.log(`PASS: SC-023 table at ${planPath}:${start + 1}, ${data.length} rows, chosen: ${chosen.map((r) => r[0]).join('; ')}`);
}

const [mode, override] = process.argv.slice(2);
switch (mode) {
  case 'baseline': runBaseline(override ? path.resolve(override) : path.join(HERE, 'spec.md')); break;
  case 'check': runCheck(override ? path.resolve(override) : path.join(HERE, 'spec.md')); break;
  case 'sc023': runSc023(override ? path.resolve(override) : path.join(HERE, 'plan.md')); break;
  default: fail('usage: node check-bars.js <baseline|check|sc023> [spec.md|plan.md]');
}
