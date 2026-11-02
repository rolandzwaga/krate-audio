#!/usr/bin/env node
// Local TAP check for Krate Manager Phase 2 (FR-073, PD-15, PD-27, SC-017 local half; plan.md §4.10; tasks.md T019).
//
// WHY: the CI check lives as a heredoc inside the `node --test` step of .github/workflows/feed-tools.yml. A local copy
// of that logic would drift from it, so this script runs THE SAME TEXT: it extracts the lines between the `<<'TAPCHECK'`
// line and the closing `TAPCHECK` line, removes the step's indentation, and runs them in a vm sandbox whose `process.env`
// is `{CI: '1'}`, so the local close applies the CI rule (a skip of the rsign cases, top-level or in a subtest, fails).
//
// Usage (repo root or anywhere inside the repo):
//   node specs/krate-manager-phase2/evidence/tap-check.js f:/tmp/km2-all.tap  # exit 0 pass, 1 check failed, 2 usage
//   node specs/krate-manager-phase2/evidence/tap-check.js --self-test         # the extracted check on four synthetic TAPs
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const WORKFLOW = path.join(ROOT, '.github', 'workflows', 'feed-tools.yml');

class ExitSignal {
  constructor(code) {
    this.code = code;
  }
}

// The heredoc body, de-indented by the indentation of its `node - ... <<'TAPCHECK'` line. Throws when it is absent.
function extractCheck(yamlText) {
  const lines = yamlText.split(/\r?\n/);
  const opens = [];
  lines.forEach((l, i) => {
    if (/<<'TAPCHECK'\s*$/.test(l)) opens.push(i);
  });
  if (opens.length !== 1) throw new Error(`want exactly one <<'TAPCHECK' line in feed-tools.yml, found ${opens.length}`);
  const open = opens[0];
  const indent = /^ */.exec(lines[open])[0];
  const close = lines.findIndex((l, i) => i > open && l === `${indent}TAPCHECK`);
  if (close < 0) throw new Error(`no closing "${indent}TAPCHECK" line after feed-tools.yml:${open + 1}`);
  return lines
    .slice(open + 1, close)
    .map((l, k) => {
      if (l.trim() === '') return '';
      if (!l.startsWith(indent)) throw new Error(`feed-tools.yml:${open + 2 + k} is indented less than the heredoc`);
      return l.slice(indent.length);
    })
    .join('\n');
}

// Runs the check over `tapPath` with `fsImpl` as the only module `require` admits. Returns {code, out, err}.
function runCheck(code, tapPath, { fsImpl = fs, echo = false } = {}) {
  const out = [];
  const err = [];
  const sink = (buf, stream) => (...a) => {
    const s = a.map(String).join(' ');
    buf.push(s);
    if (echo) stream.write(`${s}\n`);
  };
  const sandbox = {
    require: (name) => {
      if (name !== 'fs') throw new Error(`tap-check sandbox: require("${name}") refused; only fs is admitted`);
      return fsImpl;
    },
    process: {
      env: { CI: '1' },
      argv: [process.execPath, '-', tapPath],
      exit: (c) => {
        throw new ExitSignal(c === undefined ? 0 : c);
      },
    },
    console: { log: sink(out, process.stdout), error: sink(err, process.stderr) },
  };
  let status = 0;
  try {
    vm.runInNewContext(code, sandbox, { filename: 'feed-tools.yml#TAPCHECK' });
  } catch (e) {
    if (e instanceof ExitSignal) status = e.code;
    else {
      sink(err, process.stderr)(`tap-check: the check threw: ${e && e.stack ? e.stack : e}`);
      status = 1;
    }
  }
  return { code: status, out: out.join('\n'), err: err.join('\n') };
}

// ---- self-test -------------------------------------------------------------------------------------------------------

// plan.md §7.1, the 22 names; RSIGN_NAMES = SC-003, SC-025, SC-027.
const NAMES = [
  'minisign.js verifies the rsign2 KAT',
  'minisign.js sign and verify round trip',
  'minisign.js and rsign2 interoperate',
  'release-facts reads Mach-O arch sets and minimum macOS',
  'release-facts reads the glibc floor',
  'gen-release-manifest builds a verified release.json',
  'gen-release-manifest refuses what it cannot bind',
  "Linux payload digest equals the extracted tree's listing",
  'gen-release-manifest prints the C3 comments',
  'backfill signs nine current releases',
  'backfill second run uploads nothing',
  'backfill reports defective releases by name',
  'backfill dry run uploads nothing',
  'release.yml and backfill-sign.yml keep the Phase 2 shape',
  'signing configuration is throwaway, on or a failure',
  'minisign.js verify CLI prints and exits by contract',
  'release-facts darwin CLI writes facts and lipo lines',
  'backfill production signer drives rsign2',
  'Phase 2 tools follow the tools/*.js form',
  'minisign.js decrypts the rsign2 secret-key KAT',
  'minisign.js decrypt-key writes only under RUNNER_TEMP and cleans up',
  'keys README states the secrets contract and the merge order',
];
const RSIGN_NAMES = [
  'minisign.js and rsign2 interoperate',
  'backfill production signer drives rsign2',
  'minisign.js decrypts the rsign2 secret-key KAT',
];

// A Node `--test-reporter=tap` shaped text: one block per top-level test, each with two subtests.
function syntheticTap({ names, plan, skipSubtestIn = null }) {
  const L = ['TAP version 13'];
  names.forEach((n, i) => {
    L.push(`# Subtest: ${n}`);
    for (let s = 1; s <= 2; s++) {
      L.push(`    # Subtest: ${n} sub ${s}`);
      const skip = n === skipSubtestIn && s === 2 ? ' # SKIP rsign2 0.6.7 not found' : '';
      L.push(`    ok ${s} - ${n} sub ${s}${skip}`, '      ---', '      duration_ms: 1.0', "      type: 'test'", '      ...');
    }
    L.push('    1..2', `ok ${i + 1} - ${n}`, '  ---', '  duration_ms: 2.0', "  type: 'test'", '  ...');
  });
  L.push(plan, `# tests ${names.length}`, '# suites 0', `# pass ${names.length}`, '# fail 0', '# cancelled 0');
  return `${L.join('\n')}\n`;
}

function selfTest() {
  const code = extractCheck(fs.readFileSync(WORKFLOW, 'utf8'));
  const fillers = Array.from({ length: 24 }, (_, i) => `phase 1 test ${i + 1}`);
  const all = [...fillers, ...NAMES];
  const missing = NAMES[7];
  const cases = [
    { label: '46 tests, 22 names', tap: syntheticTap({ names: all, plan: '1..46' }), want: 0, msg: null },
    {
      label: 'one name missing',
      tap: syntheticTap({ names: all.map((n) => (n === missing ? 'phase 1 test 25' : n)), plan: '1..46' }),
      want: 1,
      msg: `missing: ${missing}`,
    },
    {
      label: 'subtest skip in the SC-027 block',
      tap: syntheticTap({ names: all, plan: '1..46', skipSubtestIn: RSIGN_NAMES[2] }),
      want: 1,
      msg: `skipped rsign case: ${RSIGN_NAMES[2]}: ok 2 - ${RSIGN_NAMES[2]} sub 2 # SKIP`,
    },
    { label: 'plan 1..45', tap: syntheticTap({ names: all.slice(1), plan: '1..45' }), want: 1, msg: 'top-level plan ["1..45"], want 1..46' },
  ];
  let failed = 0;
  for (const c of cases) {
    const tapPath = `synthetic://${c.label}`;
    const fsImpl = {
      readFileSync: (p) => {
        if (p !== tapPath) throw new Error(`self-test fs: unexpected read of ${p}`);
        return c.tap;
      },
    };
    const r = runCheck(code, tapPath, { fsImpl });
    const msgOk = c.msg === null ? r.err === '' : r.err.includes(c.msg);
    const ok = r.code === c.want && msgOk;
    if (!ok) failed++;
    console.log(`${ok ? 'PASS' : 'FAIL'} ${c.label}: exit ${r.code} (want ${c.want})${c.msg ? `, stderr has "${c.msg}": ${msgOk}` : ''}`);
    if (!ok) console.log(`  stdout: ${r.out}\n  stderr: ${r.err}`);
  }
  console.log(failed ? `self-test: ${failed} of ${cases.length} failed` : `self-test: ${cases.length} of ${cases.length} passed`);
  return failed ? 1 : 0;
}

function main(argv) {
  if (argv[0] === '--self-test' && argv.length === 1) return selfTest();
  if (argv.length !== 1 || argv[0].startsWith('--')) {
    console.error('usage: node specs/krate-manager-phase2/evidence/tap-check.js <tap-file> | --self-test');
    return 2;
  }
  const code = extractCheck(fs.readFileSync(WORKFLOW, 'utf8'));
  return runCheck(code, path.resolve(argv[0]), { echo: true }).code;
}

if (require.main === module) {
  try {
    process.exitCode = main(process.argv.slice(2));
  } catch (e) {
    console.error(`tap-check: ${e.message}`);
    process.exitCode = 1;
  }
}
