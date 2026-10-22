#!/usr/bin/env node
// T021 (FR-071) twin check: every non-comment line of .github/workflows/ci.yml that names Vorago
// must either name Profundum itself or have a Profundum twin line elsewhere in the file.
// Twin = vorago->profundum, Vorago->Profundum, Vrgo->Prfd. Both sides are normalised by trimming
// whitespace and a trailing `\` or `; do`; runs of internal whitespace are collapsed so column
// alignment in `case` arms (`vorago)   CHANGED=` vs `profundum) CHANGED=`) does not count.
// Prints `CI-TWINS missing=<n>` and exits 1 when n > 0.
'use strict';
const fs = require('fs');
const path = require('path');

const repo = path.resolve(__dirname, '..', '..', '..', '..');
const ciPath = path.join(repo, '.github', 'workflows', 'ci.yml');
const lines = fs.readFileSync(ciPath, 'utf8').split(/\r?\n/);

const normalise = (s) =>
  s.trim().replace(/(\\|;\s*do)$/, '').trim().replace(/\s+/g, ' ');

const present = new Set(lines.map(normalise));
const missing = [];

lines.forEach((line, i) => {
  const t = line.trim();
  if (t.startsWith('#')) return;
  if (!/vorago/i.test(line)) return;
  if (/profundum/i.test(line)) return;
  const twin = line
    .replace(/vorago/g, 'profundum')
    .replace(/Vorago/g, 'Profundum')
    .replace(/VORAGO/g, 'PROFUNDUM')
    .replace(/Vrgo/g, 'Prfd');
  if (!present.has(normalise(twin))) missing.push({ line: i + 1, twin: normalise(twin) });
});

for (const m of missing) console.log(`MISSING ci.yml:${m.line} -> ${m.twin}`);
console.log(`CI-TWINS missing=${missing.length}`);
process.exit(missing.length > 0 ? 1 : 0);
