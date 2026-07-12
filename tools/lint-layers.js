#!/usr/bin/env node
// ==============================================================================
// lint-layers.js — KrateDSP layer-dependency gate
// ==============================================================================
// The DSP library is a strict 5-layer stack; a header in layer N may only
// #include from layers <= N:
//
//   0 core  <  1 primitives  <  2 processors  <  3 systems  <  4 effects
//
// This lint reads every `#include <krate/dsp/{layer}/...>` in the DSP tree and
// fails if a lower-layer file reaches up into a higher layer (e.g. a core header
// including a primitives header). The rule was previously prose-only in
// dsp/CLAUDE.md and had already been silently violated
// (core/curve_table.h -> primitives/envelope_utils.h).
//
//   node tools/lint-layers.js          # exit 1 on any violation
//
// Heuristic, not a preprocessor: it strips // and /* */ comments so commented
// includes are ignored, but it does not evaluate #if/#ifdef. That is fine here —
// there are no layer-crossing includes hidden behind conditionals.
// ==============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const DSP_INC = path.join(REPO_ROOT, 'dsp', 'include', 'krate', 'dsp');
const LAYERS = ['core', 'primitives', 'processors', 'systems', 'effects'];
const layerIndex = (name) => LAYERS.indexOf(name);

function listSources(dir) {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...listSources(full));
    else if (e.name.endsWith('.h') || e.name.endsWith('.cpp')) out.push(full);
  }
  return out;
}

// Strip // line comments and /* */ block comments (multi-line aware) so commented
// includes are not flagged. Returns the comment-free text.
function stripComments(text) {
  let out = '';
  let inBlock = false;
  for (let i = 0; i < text.length; i++) {
    if (inBlock) {
      if (text[i] === '*' && text[i + 1] === '/') { inBlock = false; i++; }
      continue;
    }
    if (text[i] === '/' && text[i + 1] === '*') { inBlock = true; i++; continue; }
    if (text[i] === '/' && text[i + 1] === '/') { while (i < text.length && text[i] !== '\n') i++; out += '\n'; continue; }
    out += text[i];
  }
  return out;
}

const fileLayer = (rel) => LAYERS.find((l) => rel.includes(`/dsp/${l}/`)) || null;

const violations = [];

for (const file of listSources(DSP_INC).sort()) {
  const rel = path.relative(REPO_ROOT, file).split(path.sep).join('/');
  const fromLayer = fileLayer(rel);
  if (!fromLayer) continue; // file not under a recognized layer folder

  const lines = stripComments(fs.readFileSync(file, 'utf8')).split(/\r?\n/);
  lines.forEach((line, idx) => {
    const m = line.match(/#\s*include\s*[<"]krate\/dsp\/([a-z]+)\//);
    if (!m) return;
    const toLayer = m[1];
    if (!LAYERS.includes(toLayer)) return;
    if (layerIndex(toLayer) > layerIndex(fromLayer)) {
      violations.push({
        file: rel,
        line: idx + 1,
        fromLayer,
        toLayer,
        text: line.trim(),
      });
    }
  });
}

if (violations.length === 0) {
  console.log(`lint-layers: OK — no layer-dependency violations in ${LAYERS.length}-layer DSP tree.`);
  process.exit(0);
}

console.error('lint-layers: FAILED — lower-layer file(s) include from a higher layer:\n');
for (const v of violations) {
  console.error(
    `  ${v.file}:${v.line}\n` +
    `    layer '${v.fromLayer}' (${layerIndex(v.fromLayer)}) may not include layer '${v.toLayer}' (${layerIndex(v.toLayer)})\n` +
    `    ${v.text}`
  );
}
console.error(
  `\n${violations.length} violation(s). Move the shared symbol down to the lower layer, or ` +
  `re-home the file. See dsp/CLAUDE.md -> Layer Architecture.`
);
process.exit(1);
