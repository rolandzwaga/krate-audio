#!/usr/bin/env node
// ==============================================================================
// lint-odr.js — duplicate class/struct (ODR) gate
// ==============================================================================
// Two classes/structs with the SAME name in the SAME namespace, defined in two
// different files, are an ODR violation: silent undefined behavior (garbage
// values, mystery test failures) — flagged as the highest-severity failure class
// in dsp/CLAUDE.md, yet previously guarded only by "remember to grep".
//
// This lint extracts every class/struct DEFINITION under dsp/include and
// plugins/*/src, fully qualifies it by its enclosing namespace + class scope,
// and fails if one qualified name is defined in more than one file.
//
//   node tools/lint-odr.js          # exit 1 on any cross-file collision
//
// Heuristic (regex + brace-tracked scope stack), NOT a compiler. Deliberately
// conservative to avoid false positives:
//   - forward declarations, template (partial) specializations are skipped;
//   - nested types are qualified by their enclosing class, so a private nested
//     struct reused across classes does NOT collide;
//   - types inside an anonymous namespace are skipped (internal linkage — a
//     same-named file-local helper in two .cpp TUs is not an ODR violation).
// grep remains the source of truth; this is a cheap net for the obvious cases.
// ==============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const SCAN_ROOTS = [
  path.join(REPO_ROOT, 'dsp', 'include'),
  ...fs.readdirSync(path.join(REPO_ROOT, 'plugins'), { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => path.join(REPO_ROOT, 'plugins', e.name, 'src'))
    .filter((p) => fs.existsSync(p)),
];

function listSources(dir) {
  const out = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...listSources(full));
    else if (/\.(h|hpp|cpp)$/.test(e.name)) out.push(full);
  }
  return out;
}

function stripComments(line) {
  return line.replace(/\/\*.*?\*\//g, '').replace(/\/\/.*$/, '');
}

const symbols = []; // { name, qualified, file, line }

for (const root of SCAN_ROOTS) {
  for (const file of listSources(root).sort()) {
    const rel = path.relative(REPO_ROOT, file).split(path.sep).join('/');
    const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);

    // Unified scope stack of namespaces AND classes. Each entry:
    // { name, depth, anon } where depth is the brace depth of its interior.
    const scopeStack = [];
    let depth = 0;
    let pending = null;

    const inAnon = () => scopeStack.some((s) => s.anon);

    for (let i = 0; i < lines.length; i++) {
      const line = stripComments(lines[i]);

      // Anonymous namespace opener: `namespace {`
      if (/\bnamespace\s*\{/.test(line) && !/namespace\s+[\w:]/.test(line)) {
        pending = { name: '(anon)', anon: true };
      } else {
        // Named namespace (qualified allowed: namespace A::B). Exclude aliases / using.
        const ns = line.match(/\bnamespace\s+([A-Za-z_][\w:]*)\s*(\{)?\s*$/);
        if (ns && !/using\s+namespace/.test(line) && !/namespace\s+[\w:]+\s*=/.test(line)) {
          pending = { name: ns[1], anon: false };
        }
      }

      // class/struct definition (not a forward decl, not a specialization)
      const cs = line.match(/^\s*(?:template\s*<[^>]*>\s*)?(class|struct)\s+([A-Za-z_]\w*)\b(.*)$/);
      if (cs) {
        const rest = cs[3].trim();
        const isForwardDecl = /^;/.test(rest);
        const isSpecialization = /^</.test(rest);
        const isDefinition =
          rest === '' || rest.startsWith('{') || rest.startsWith(':') || rest.startsWith('final');
        if (!isForwardDecl && !isSpecialization && isDefinition && !inAnon()) {
          const scope = scopeStack.filter((s) => !s.anon).map((s) => s.name).join('::');
          symbols.push({
            name: cs[2],
            qualified: (scope ? scope + '::' : '') + cs[2],
            file: rel,
            line: i + 1,
          });
        }
        if (!isForwardDecl && !isSpecialization && isDefinition) {
          pending = { name: cs[2], anon: false }; // opens a member scope
        }
      }

      for (const ch of line) {
        if (ch === '{') {
          depth++;
          if (pending) { scopeStack.push({ ...pending, depth }); pending = null; }
        } else if (ch === '}') {
          depth--;
          while (scopeStack.length && scopeStack[scopeStack.length - 1].depth > depth) scopeStack.pop();
        }
      }
    }
  }
}

// Collisions: same qualified name defined in more than one distinct file.
const byQualified = {};
for (const s of symbols) (byQualified[s.qualified] ||= []).push(s);

const collisions = Object.entries(byQualified)
  .filter(([, arr]) => new Set(arr.map((s) => s.file)).size > 1)
  .sort((a, b) => a[0].localeCompare(b[0]));

if (collisions.length === 0) {
  console.log(`lint-odr: OK — ${symbols.length} definitions scanned, no cross-file name collisions.`);
  process.exit(0);
}

console.error('lint-odr: FAILED — same qualified name defined in multiple files (ODR hazard):\n');
for (const [qualified, arr] of collisions) {
  console.error(`  ${qualified}`);
  for (const s of arr) console.error(`    ${s.file}:${s.line}`);
  console.error('');
}
console.error(
  `${collisions.length} collision(s). Two types with the same name in the same namespace = ` +
  `undefined behavior. Rename one, move to a distinct namespace, or merge them.`
);
process.exit(1);
