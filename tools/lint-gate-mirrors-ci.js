#!/usr/bin/env node
//
// lint-gate-mirrors-ci.js
// =============================================================================
// Fails when a cheap node check that a GitHub workflow runs is not also run by the commit gate
// (tools/hooks/guard-ci-gates.js), so a check added to CI cannot be forgotten locally.
//
// Mirrored kinds, read from every .github/workflows/*.yml `run:` command:
//   node [../]*tools/lint-*.js [--self-test]  ->  a guard LINTS entry with that script (and args)
//   node [../]*tools/gen-*.js --check         ->  a guard GENERATORS entry with that script
//   node --test ... "<glob>"                  ->  a guard NODE_SUITES entry whose args hold that glob
//
// WHY. Every CI red on this branch's PR came from a check that ran in CI and not before the commit: six ci.yml lints
// the gate did not list, then feed-tools.yml's `node --test` suite (an action-pin commit broke its shape tests), each
// found only after a push (2026-11-13/14). The gate's lists were kept by hand; this lint makes CI the source of truth.
//
// Commands that need build output or the network (check-bundle.js over built bundles, cargo, cmake, ctest) cannot run
// in a commit hook; they are named in CI_ONLY with the reason.
// Usage: node tools/lint-gate-mirrors-ci.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const WORKFLOWS = path.join(REPO_ROOT, '.github', 'workflows');
const GATE = path.join(REPO_ROOT, 'tools', 'hooks', 'guard-ci-gates.js');
const CI_ONLY = new Map([
  ['check-bundle.js', 'validates built .vst3 bundles, which only exist after the CI build'],
]);

/** The checks a workflow text runs: [{ kind, script, args, glob, line }]. */
function workflowChecks(text) {
  const out = [];
  text.split(/\r?\n/).forEach((raw, i) => {
    const line = raw.replace(/(^|\s)#.*$/, '');
    for (const m of line.matchAll(/\bnode\s+(?:\.\.\/)*tools\/((?:lint|gen)-[\w-]+\.js)((?:\s+--[\w-]+)*)/g)) {
      const args = m[2].trim().split(/\s+/).filter(Boolean);
      // A gen-*.js run is a CHECK only with --check; without it the workflow is producing an artifact.
      if (m[1].startsWith('gen-')) {
        if (args.includes('--check')) out.push({ kind: 'gen', script: m[1], args: ['--check'], line: i + 1 });
        continue;
      }
      out.push({ kind: 'lint', script: m[1], args, line: i + 1 });
    }
    const t = /\bnode\s+--test\b[^"']*["']([^"']+)["']/.exec(line);
    if (t) out.push({ kind: 'suite', glob: t[1], line: i + 1 });
  });
  return out;
}

/** What the gate mirrors, read from guard-ci-gates.js's source. */
function gateMirrors(src) {
  const lints = new Set();
  for (const m of src.matchAll(/\{\s*script:\s*'((?:lint)-[\w-]+\.js)'(?:,\s*args:\s*\[([^\]]*)\])?/g)) {
    const args = (m[2] || '').split(',').map((s) => s.trim().replace(/^'|'$/g, '')).filter(Boolean);
    lints.add([m[1], ...args].join(' '));
  }
  const gens = new Set([...src.matchAll(/\{\s*script:\s*'(gen-[\w-]+\.js)'/g)].map((m) => m[1]));
  const globs = new Set([...src.matchAll(/'([\w./-]*\*[\w./*-]*\.test\.js)'/g)].map((m) => m[1]));
  return { lints, gens, globs };
}

function missing(checks, mirrors) {
  const out = [];
  for (const c of checks) {
    if (c.kind === 'lint' && !mirrors.lints.has([c.script, ...c.args].join(' '))) out.push(c);
    if (c.kind === 'gen' && !CI_ONLY.has(c.script) && !mirrors.gens.has(c.script)) out.push(c);
    if (c.kind === 'suite' && !mirrors.globs.has(c.glob)) out.push(c);
  }
  return out;
}

const describe = (c) => (c.kind === 'suite' ? `node --test "${c.glob}"` : `node tools/${[c.script, ...c.args].join(' ')}`);

function selfTest() {
  const gate = "const GENERATORS = [{ script: 'gen-repo-map.js', artifact: 'x' }];\n" +
    "const LINTS = [{ script: 'lint-odr.js', what: 'x' }, { script: 'lint-x.js', args: ['--self-test'], what: 'y' }];\n" +
    "const NODE_SUITES = [{ workflow: 'feed-tools.yml', args: ['--test', 'tools/feed-contract/tests/*.test.js'] }];\n";
  const cases = [
    ['        run: node tools/lint-odr.js\n', 0],
    ['        run: node tools/lint-new.js\n', 1],
    ['        run: |\n          node tools/lint-x.js --self-test\n          node tools/lint-x.js\n', 1],
    ['        run: node tools/gen-repo-map.js --check\n', 0],
    ['        run: node tools/gen-symbols.js --check\n', 1],
    ['          node --test --test-reporter=tap "tools/feed-contract/tests/*.test.js" | tee x\n', 0],
    ['          if node --test --test-reporter=tap "scripts/*.test.js"; then\n', 1],
    ['        run: node ../../tools/lint-manager-version.js\n', 1],
    ['      # node tools/lint-commented.js\n', 0],
    ['        run: node tools/gen-release-manifest.js --version-json x.json\n', 0],
  ];
  const mirrors = gateMirrors(gate);
  let failed = 0;
  for (const [wf, want] of cases) {
    const got = missing(workflowChecks(wf), mirrors).length;
    if (got !== want) { ++failed; console.log(`self-test FAIL: want ${want}, got ${got}:\n  ${wf.trim()}`); }
  }
  console.log(failed ? `lint-gate-mirrors-ci self-test: ${failed} FAILED` : `lint-gate-mirrors-ci self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  const mirrors = gateMirrors(fs.readFileSync(GATE, 'utf8'));
  let bad = 0;
  let total = 0;
  for (const f of fs.readdirSync(WORKFLOWS).filter((n) => /\.ya?ml$/.test(n))) {
    const rel = `.github/workflows/${f}`;
    const checks = workflowChecks(fs.readFileSync(path.join(WORKFLOWS, f), 'utf8'));
    total += checks.length;
    for (const c of missing(checks, mirrors)) {
      ++bad;
      console.log(`::error file=${rel},line=${c.line}::${rel}:${c.line}: \`${describe(c)}\` runs in CI but not in tools/hooks/guard-ci-gates.js`);
    }
  }
  if (bad > 0) {
    console.log('lint-gate-mirrors-ci: FAILED. Add each to guard-ci-gates.js (LINTS with its args, GENERATORS, or a\n' +
                'NODE_SUITES entry with the workflow and glob) so it runs before the commit, not first on CI. A check\n' +
                'that cannot run in a hook goes in CI_ONLY here, with the reason.');
    return 1;
  }
  console.log(`lint-gate-mirrors-ci: OK (${total} workflow checks, all mirrored)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-gate-mirrors-ci: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
