#!/usr/bin/env node
//
// lint-workflow-triggers.js
// =============================================================================
// Fails on GitHub workflow triggers that run the same work twice, or let one run cancel an unrelated one:
//
//   1. push-and-pr    A workflow triggered by both `push` and `pull_request` must restrict `push` to named branches
//                     (`branches: [main]`). Otherwise every commit to a PR branch runs it twice, once per event.
//   2. no-concurrency A workflow triggered by `push` or `pull_request` must have a top-level `concurrency:` group, so
//                     a newer commit cancels the run it supersedes instead of both running to the end.
//   3. called-group   A reusable workflow (`workflow_call`) whose cancel-in-progress group uses `github.workflow` must
//                     also key it on `inputs.`: inside a called workflow `github.workflow` is the CALLER's name, so two
//                     calls from the same caller and ref share the group and the second cancels the first.
//
// WHY. feed-tools.yml and manager-ci.yml ran on push to any branch and on pull_request, so every PR commit ran both
// twice (the user, 2026-11-14); feed-tools.yml had no concurrency group; ci.yml's group was
// `${{ github.workflow }}-${{ github.ref }}`, and release.yml calls ci.yml per plugin, so two releases from main shared
// "Release-refs/heads/main" and cancelled each other's build.
//
// Usage: node tools/lint-workflow-triggers.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const REPO_ROOT = path.resolve(__dirname, '..');
const WORKFLOWS = path.join(REPO_ROOT, '.github', 'workflows');

/** The lines of a top-level `<key>:` block (the key line first), or null. */
function topBlock(lines, key) {
  const start = lines.findIndex((l) => new RegExp(`^${key}:`).test(l));
  if (start < 0) return null;
  let end = start + 1;
  while (end < lines.length && !/^\S/.test(lines[end])) ++end;
  return { start, lines: lines.slice(start, end) };
}

/** Triggers under `on:`: Map name -> { line, branches } (branches: array or null when unrestricted). */
function triggers(lines) {
  const block = topBlock(lines, 'on');
  const out = new Map();
  if (!block) return out;
  const inline = /^on:\s*(\S.*)$/.exec(block.lines[0].replace(/\s+#.*$/, ''));
  if (inline) {
    for (const name of inline[1].replace(/[[\]]/g, '').split(',').map((s) => s.trim()).filter(Boolean)) {
      out.set(name, { line: block.start + 1, branches: null });
    }
    return out;
  }
  let current = null;
  let inBranches = false;
  block.lines.forEach((raw, i) => {
    const l = raw.replace(/\s+#.*$/, '');
    const trig = /^ {2}([\w-]+):/.exec(l);
    if (trig) {
      current = { line: block.start + i + 1, branches: null };
      out.set(trig[1], current);
      inBranches = false;
      return;
    }
    if (!current) return;
    if (/^ {4}\S/.test(l)) {
      const br = /^ {4}branches:\s*(.*)$/.exec(l);
      inBranches = Boolean(br) && !br[1].trim();
      if (br) {
        current.branches = br[1].trim().replace(/[[\]'"]/g, '').split(',').map((s) => s.trim()).filter(Boolean);
      }
      return;
    }
    const item = inBranches && /^ {6}- ['"]?([^'"]+?)['"]?\s*$/.exec(l);
    if (item) current.branches.push(item[1]);
  });
  return out;
}

/** Top-level concurrency: { line, group, cancel } or null. */
function concurrency(lines) {
  const block = topBlock(lines, 'concurrency');
  if (!block) return null;
  const text = block.lines.join('\n');
  const group = (/^ {2}group:\s*(.+)$/m.exec(text) || /^concurrency:\s*(\S.+)$/m.exec(text) || [])[1] || '';
  return { line: block.start + 1, group: group.trim(), cancel: /^ {2}cancel-in-progress:\s*true\b/m.test(text) };
}

function check(text) {
  const lines = text.split(/\r?\n/);
  const trig = triggers(lines);
  const conc = concurrency(lines);
  const out = [];
  const push = trig.get('push');
  if (push && trig.has('pull_request')) {
    const b = push.branches;
    if (!b || b.length === 0 || b.some((x) => x.includes('*'))) {
      out.push({ line: push.line, rule: 'push-and-pr',
        msg: 'triggered by push and pull_request with push on every branch: each PR commit runs it twice. ' +
             'Add `branches: [main]` under push.' });
    }
  }
  if ((push || trig.has('pull_request')) && !(conc && conc.group)) {
    out.push({ line: (push || trig.get('pull_request')).line, rule: 'no-concurrency',
      msg: 'push/pull_request workflow without a top-level concurrency group: a newer commit does not cancel the ' +
           'run it supersedes. Add `concurrency: { group: <name>-${{ github.ref }}, cancel-in-progress: true }`.' });
  }
  if (trig.has('workflow_call') && conc && conc.cancel && /github\.workflow\b/.test(conc.group) &&
      !/\binputs\./.test(conc.group)) {
    out.push({ line: conc.line, rule: 'called-group',
      msg: 'reusable workflow whose cancel-in-progress group uses github.workflow (the CALLER\'s name when called) ' +
           'without inputs: two calls from one caller and ref cancel each other. Add the call inputs to the group.' });
  }
  return out;
}

function selfTest() {
  const conc = 'concurrency:\n  group: x-${{ github.ref }}\n  cancel-in-progress: true\n';
  const cases = [
    ['unrestricted push + pr', `on:\n  push:\n    paths:\n      - 'a/**'\n  pull_request:\n${conc}`, ['push-and-pr']],
    ['push on main + pr', `on:\n  push:\n    branches: [main]\n    paths:\n      - 'a/**'\n  pull_request:\n${conc}`, []],
    ['block-list branches', `on:\n  push:\n    branches:\n      - main\n  pull_request:\n${conc}`, []],
    ['paths only, block list', `on:\n  push:\n    paths:\n      - main\n  pull_request:\n${conc}`, ['push-and-pr']],
    ['wildcard branches', `on:\n  push:\n    branches: ['**']\n  pull_request:\n${conc}`, ['push-and-pr']],
    ['inline on list', `on: [push, pull_request]\n${conc}`, ['push-and-pr']],
    ['push only, main', `on:\n  push:\n    branches: [main]\n${conc}`, []],
    ['no concurrency', 'on:\n  push:\n    branches: [main]\n  pull_request:\n', ['no-concurrency']],
    ['dispatch only', 'on:\n  workflow_dispatch:\n', []],
    ['called, workflow-only group',
      'on:\n  push:\n    branches: [main]\n  pull_request:\n  workflow_call:\n' +
      'concurrency:\n  group: ${{ github.workflow }}-${{ github.ref }}\n  cancel-in-progress: true\n', ['called-group']],
    ['called, group with inputs',
      'on:\n  push:\n    branches: [main]\n  pull_request:\n  workflow_call:\n' +
      'concurrency:\n  group: ${{ github.workflow }}-${{ github.ref }}-${{ inputs.plugin }}\n  cancel-in-progress: true\n', []],
    ['called, no cancel', 'on:\n  workflow_call:\nconcurrency:\n  group: ${{ github.workflow }}\n  cancel-in-progress: false\n', []],
  ];
  let failed = 0;
  for (const [name, text, want] of cases) {
    const got = check(text).map((v) => v.rule).sort().join(',');
    if (got !== [...want].sort().join(',')) { ++failed; console.log(`self-test FAIL: ${name}: want [${want}], got [${got}]`); }
  }
  console.log(failed ? `lint-workflow-triggers self-test: ${failed} FAILED` : `lint-workflow-triggers self-test: ${cases.length} cases OK`);
  return failed ? 1 : 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  const files = fs.readdirSync(WORKFLOWS).filter((n) => /\.ya?ml$/.test(n));
  let bad = 0;
  for (const f of files) {
    const rel = `.github/workflows/${f}`;
    for (const v of check(fs.readFileSync(path.join(WORKFLOWS, f), 'utf8'))) {
      ++bad;
      console.log(`::error file=${rel},line=${v.line}::${rel}:${v.line}: [${v.rule}] ${v.msg}`);
    }
  }
  if (bad > 0) {
    console.log(`lint-workflow-triggers: FAILED (${bad} violation(s))`);
    return 1;
  }
  console.log(`lint-workflow-triggers: OK (${files.length} workflows)`);
  return 0;
}

try {
  process.exitCode = main();
} catch (err) {
  console.error(`lint-workflow-triggers: internal error: ${err.stack || err}`);
  process.exitCode = 2;
}
