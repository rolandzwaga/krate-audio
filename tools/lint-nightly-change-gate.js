#!/usr/bin/env node
// =============================================================================
// lint-nightly-change-gate.js - every scheduled heavy workflow runs only through tools/nightly-change-gate.js
// =============================================================================
// The nightlies (valgrind, long tests, sanitizers) cost hours of runner time each. They used to run whenever anything
// had been pushed to any branch in 24 h; tools/nightly-change-gate.js now runs them only when the code they test
// changed since their last tested run (user, 2026-11-14). This lint keeps a new or edited nightly from skipping the
// gate. For every workflow with a `schedule:` trigger and a heavy job (a cmake / ctest / ci-test-shard / run-gate /
// valgrind command, or a call to ci.yml):
//   - some job runs `node tools/nightly-change-gate.js --workflow <this file>` (its own file name: a gate copied from
//     another nightly would read that nightly's run history);
//   - every heavy job's block reads `outputs.should_run` (it is gated on that job's answer);
//   - the workflow grants `actions: read` (the gate lists previous runs and their jobs).
// Usage: node tools/lint-nightly-change-gate.js [--self-test]
// Exit codes: 0 = clean, 1 = violations found, 2 = internal error.
// =============================================================================
'use strict';

const fs = require('fs');
const path = require('path');

const WORKFLOWS = path.join(path.resolve(__dirname, '..'), '.github', 'workflows');
const HEAVY = /\bcmake\s+(?:--build|-S|-B|--preset)|\bctest\s+-|ci-test-shard\.js|run-gate\.js|\bvalgrind\s+-|uses:\s*\.\/\.github\/workflows\/ci\.yml/;
const GATE = /node\s+tools\/nightly-change-gate\.js\b[^\n]*--workflow\s+(\S+)/;

/** Violation messages for one workflow file's text. */
function check(file, text) {
  const out = [];
  const lines = text.split(/\r?\n/);
  const code = (l) => l.replace(/(^|\s)#.*$/, '');
  const onAt = lines.findIndex((l) => /^on:\s*$/.test(code(l)));
  const jobsAt = lines.findIndex((l) => /^jobs:\s*$/.test(code(l)));
  if (onAt < 0 || jobsAt < 0) return out;
  const onEnd = lines.findIndex((l, i) => i > onAt && /^\S/.test(l));
  if (!lines.slice(onAt, onEnd < 0 ? lines.length : onEnd).some((l) => /^\s+schedule:/.test(code(l)))) return out;

  const jobs = [];
  for (let i = jobsAt + 1; i < lines.length; ++i) {
    if (/^\S/.test(lines[i])) break;
    const m = /^ {2}([\w-]+):\s*$/.exec(code(lines[i]));
    if (m) jobs.push({ name: m[1], at: i });
  }
  jobs.forEach((j, k) => {
    const end = k + 1 < jobs.length ? jobs[k + 1].at : lines.length;
    j.body = lines.slice(j.at, end).map(code).join('\n');
  });
  const heavy = jobs.filter((j) => HEAVY.test(j.body));
  if (heavy.length === 0) return out;

  const gates = jobs.map((j) => ({ j, m: GATE.exec(j.body) })).filter((g) => g.m);
  if (gates.length === 0) {
    out.push(`${file}: scheduled workflow with heavy job(s) ${heavy.map((j) => j.name).join(', ')} but no job runs node tools/nightly-change-gate.js`);
  }
  for (const g of gates) {
    if (g.m[1] !== file) out.push(`${file}: job ${g.j.name} runs nightly-change-gate.js --workflow ${g.m[1]}, not its own file ${file}`);
  }
  for (const j of heavy) {
    if (!/outputs\.should_run/.test(j.body)) out.push(`${file}: heavy job ${j.name} is not gated on the change gate (no outputs.should_run in its if:)`);
  }
  const permsAt = lines.findIndex((l) => /^permissions:\s*$/.test(code(l)));
  const permsEnd = permsAt < 0 ? -1 : lines.findIndex((l, i) => i > permsAt && /^\S/.test(l));
  const perms = permsAt < 0 ? '' : lines.slice(permsAt, permsEnd < 0 ? lines.length : permsEnd).map(code).join('\n');
  if (!/^\s+actions:\s*(?:read|write)\b/m.test(perms)) out.push(`${file}: the change gate needs top-level permissions "actions: read"`);
  return out;
}

function selfTest() {
  const good = [
    'on:', '  schedule:', "    - cron: '1 2 * * *'", 'permissions:', '  contents: read', '  actions: read', 'jobs:',
    '  check-activity:', '    steps:', '      - run: node tools/nightly-change-gate.js --workflow x.yml -- dsp',
    '  heavy:', "    if: needs.check-activity.outputs.should_run == 'true'", '    steps:', '      - run: cmake --build b', '',
  ].join('\n');
  const cases = [
    ['gated nightly is clean', check('x.yml', good).length, 0],
    ['no gate', check('x.yml', good.replace('node tools/nightly-change-gate.js --workflow x.yml -- dsp', 'echo hi')).length, 1],
    ['gate names another workflow', check('y.yml', good.replace('actions: read', 'actions: read')).length, 1],
    ['heavy job not gated', check('x.yml', good.replace("    if: needs.check-activity.outputs.should_run == 'true'\n", '')).length, 1],
    ['no actions: read', check('x.yml', good.replace('  actions: read\n', '')).length, 1],
    ['a reusable ci.yml call is heavy', check('x.yml', good.replace('    steps:\n      - run: cmake --build b', '    uses: ./.github/workflows/ci.yml')).length, 0],
    ['ungated ci.yml call', check('x.yml', good.replace("    if: needs.check-activity.outputs.should_run == 'true'\n    steps:\n      - run: cmake --build b", '    uses: ./.github/workflows/ci.yml')).length, 1],
    ['not scheduled: not checked', check('x.yml', good.replace("  schedule:\n    - cron: '1 2 * * *'", '  workflow_dispatch: {}').replace('node tools/nightly-change-gate.js --workflow x.yml -- dsp', 'echo hi')).length, 0],
    ['scheduled, no heavy job: not checked', check('x.yml', good.replace('cmake --build b', 'echo light').replace('node tools/nightly-change-gate.js --workflow x.yml -- dsp', 'echo hi')).length, 0],
  ];
  let bad = 0;
  for (const [what, got, want] of cases) {
    const ok = got === want;
    if (!ok) bad += 1;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}: ${got} violation(s), want ${want}`);
  }
  console.log(bad === 0 ? `lint-nightly-change-gate self-test: OK (${cases.length})` : `lint-nightly-change-gate self-test: ${bad} FAILED`);
  return bad === 0 ? 0 : 1;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  const files = fs.readdirSync(WORKFLOWS).filter((f) => /\.ya?ml$/.test(f)).sort();
  const problems = files.flatMap((f) => check(f, fs.readFileSync(path.join(WORKFLOWS, f), 'utf8')));
  for (const p of problems) console.log(`::error file=.github/workflows/${p.split(':')[0]}::${p}`);
  if (problems.length > 0) {
    console.log(`lint-nightly-change-gate: FAILED (${problems.length}). A scheduled heavy workflow runs only through tools/nightly-change-gate.js.`);
    return 1;
  }
  console.log(`lint-nightly-change-gate: OK (${files.length} workflows)`);
  return 0;
}

try {
  process.exit(main());
} catch (e) {
  console.error(`lint-nightly-change-gate: internal error: ${e.stack || e}`);
  process.exit(2);
}
