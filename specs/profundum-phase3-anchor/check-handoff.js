#!/usr/bin/env node
// SC-021 deferred-clause handoff check for Profundum Phase 3 (specs/profundum-phase3-anchor, task T003).
//
// Usage: node specs/profundum-phase3-anchor/check-handoff.js <pass|fail> [roadmapPath]
// Default roadmapPath: specs/Profundum-roadmap.md. The first argument is SC-022's recorded outcome.
//
// Scopes are found by headings and line prefixes, never by line numbers. A phase section runs from its
// `### Phase N:` heading to the next `### ` heading (or a higher-level `## ` / `# ` heading, so Phase 9
// does not run on into the "Part B" preamble). Within a section:
//   P9-SC   = from the line starting `**Success criteria:**` to the next blank line (Phase 9);
//   P5-BCS  = the bullet starting "- `BassCharacterStage`" up to the next line starting `- ` or `**Success`;
//   P4-NM   = the bullet starting `- **Note Memory**` up to the next line starting `- ` or `**Success`;
//   P6-MASS = Phase 6 section text before its `**Success criteria:**` line;
//   P8-MS   = the bullet starting "- `MorphState`" up to the next `- ` line;
//   P11     = Phase 11's whole section; P9 = Phase 9's whole section.
// Scoped lines are trimmed and joined with single spaces, so a required string may wrap across lines.
// Matching is case-sensitive; `–` is U+2013.
//
// One PASS/FAIL line per string per section; exits non-zero on any FAIL (or on a missing section).

'use strict';

const fs = require('fs');
const path = require('path');

const mode = process.argv[2];
if (mode !== 'pass' && mode !== 'fail') {
  console.error('usage: node check-handoff.js <pass|fail> [roadmapPath]');
  process.exit(2);
}
const repoRoot = path.resolve(__dirname, '..', '..');
const roadmapPath = process.argv[3]
  ? path.resolve(process.argv[3])
  : path.join(repoRoot, 'specs', 'Profundum-roadmap.md');

const lines = fs.readFileSync(roadmapPath, 'utf8').split(/\r?\n/);

// --- scoping helpers -------------------------------------------------------------------------------

const isSectionEnd = (l) => /^#{1,3} /.test(l);

function phaseSection(n) {
  const start = lines.findIndex((l) => l.startsWith(`### Phase ${n}:`));
  if (start < 0) return null;
  let end = start + 1;
  while (end < lines.length && !isSectionEnd(lines[end])) ++end;
  return lines.slice(start, end);
}

// From the first line satisfying `isStart` up to (excluding) the first later line satisfying `isStop`.
function span(section, isStart, isStop) {
  if (!section) return null;
  const start = section.findIndex(isStart);
  if (start < 0) return null;
  let end = start + 1;
  while (end < section.length && !isStop(section[end])) ++end;
  return section.slice(start, end);
}

const join = (ls) => (ls ? ls.map((l) => l.trim()).filter((l) => l.length > 0).join(' ') : null);

const p4 = phaseSection(4);
const p5 = phaseSection(5);
const p6 = phaseSection(6);
const p8 = phaseSection(8);
const p9 = phaseSection(9);
const p11 = phaseSection(11);

const bulletStop = (l) => l.startsWith('- ') || l.startsWith('**Success');

const scopes = {
  'P9-SC': join(span(p9, (l) => l.startsWith('**Success criteria:**'), (l) => l.trim() === '')),
  'P5-BCS': join(span(p5, (l) => l.startsWith('- `BassCharacterStage`'), bulletStop)),
  'P4-NM': join(span(p4, (l) => l.startsWith('- **Note Memory**'), bulletStop)),
  'P6-MASS': (() => {
    if (!p6) return null;
    const sc = p6.findIndex((l) => l.startsWith('**Success criteria:**'));
    return join(sc < 0 ? null : p6.slice(0, sc));
  })(),
  'P8-MS': join(span(p8, (l) => l.startsWith('- `MorphState`'), (l) => l.startsWith('- '))),
  P11: join(p11),
  P9: join(p9),
};

// --- checks ----------------------------------------------------------------------------------------

let failures = 0;
function report(ok, scope, what) {
  if (!ok) ++failures;
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${scope.padEnd(8)} ${what}`);
}

function requireStrings(scope, strings) {
  const text = scopes[scope];
  for (const s of strings) {
    if (text === null) report(false, scope, `contains "${s}" (scope not found)`);
    else report(text.includes(s), scope, `contains "${s}"`);
  }
}

console.log(`SC-021 handoff check, SC-022 outcome = ${mode}, roadmap = ${path.relative(repoRoot, roadmapPath)}`);

requireStrings('P9-SC', [
  'within 1 dB',
  'every Character algorithm',
  'E0–E2',
  'FOLLOW',
  'pitch-moving stage',
  'capability',
  'after Character',
  'Drop',
]);

// P5-BCS: one sentence (split on ". ") must hold "r-law" and each of Rectify, Crush, Fracture.
{
  const text = scopes['P5-BCS'];
  const need = ['r-law', 'Rectify', 'Crush', 'Fracture'];
  const what = `one sentence holds ${need.map((s) => `"${s}"`).join(' + ')}`;
  if (text === null) report(false, 'P5-BCS', `${what} (scope not found)`);
  else report(text.split('. ').some((sent) => need.every((s) => sent.includes(s))), 'P5-BCS', what);
}

requireStrings('P4-NM', ['re-arm']);
requireStrings('P6-MASS', ['h1 amplitude']);

for (const scope of ['P9', 'P8-MS', 'P11']) {
  const text = scopes[scope];
  if (text === null) {
    report(false, scope, '"anchorFoldFeed" check (scope not found)');
    continue;
  }
  const present = text.includes('anchorFoldFeed');
  if (mode === 'pass') report(present, scope, 'contains "anchorFoldFeed"');
  else report(!present, scope, 'does not contain "anchorFoldFeed" (feed not shipped)');
}

console.log(failures === 0 ? 'RESULT: PASS' : `RESULT: FAIL (${failures} failing line(s))`);
process.exit(failures === 0 ? 0 : 1);
