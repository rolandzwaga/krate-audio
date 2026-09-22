#!/usr/bin/env node
//
// check-seraphis-green.js
// =============================================================================
// Vorago Phase 10 (specs/vorago-phase10-voice-engine), SC-016 clauses 1-2.
//
// Phase 10 widens `ContinuousBody::BodyMaterial` from five materials to eleven
// by APPENDING six dark ones (AR-4). `ContinuousBody` is a SHIPPED Seraphis
// component, so the whole safety argument for that append is that it changes
// nothing a Seraphis consumer can observe. That argument is only worth
// anything if the blast radius is mechanically bounded, which is what this
// script does -- three git-diff scope checks, no compilation, no rendering.
//
// Why a diff check and not just "the tests are green"
// ---------------------------------------------------
// The dangerous failure here does NOT break the build. Four of the seven
// `kNumMaterials`-sized sites are `constexpr std::array<T, kNumMaterials>`
// written with five initialisers. At `kNumMaterials = 11` they STILL COMPILE
// and zero-fill: six extra `BodyMaterial{0}` (= Glass) entries get surveyed,
// and six null `const char*` get streamed (`os << kMaterialNames[i]`). That is
// undefined behaviour wearing a green test suite. The only reliable guard is
// to enumerate the files the phase is allowed to touch and check the diff
// against that list.
//
// The three checks
// ----------------
//   1. The MODIFIED files under `dsp/tests/unit/systems/` are drawn from
//      exactly {continuous_body_perf_test.cpp, continuous_body_test.cpp,
//      continuous_body_spectral_test.cpp, seraphis_perf_test.cpp} -- and once
//      the header widening has landed (check 2 below sees its two deletions),
//      all four must be present. Any other modified TU in that directory
//      means the append leaked into a Seraphis-owned test that was supposed
//      to be untouched.
//
//   2. `continuous_body.h` deletes EXACTLY the `BodyMaterial` enum line and
//      the `kNumMaterials` line, and nothing else. Those two are unavoidable
//      (the enum gains enumerators, the count becomes 11); a third deletion
//      means the change stopped being append-only.
//
//   3. `feedback_ecology.h` deletes NOTHING (the silenceAudio() append, B-7).
//      `multi_stage_envelope.h` and `growth_envelope.h` each gain a
//      per-instance ceiling setter (ruled 2026-09-18) whose only non-append
//      effect is that the existing clamps read the instance ceiling instead
//      of the constant: exactly those clamp lines may be deleted (three and
//      two respectively, matched by pattern) and nothing else, so the change
//      stays default-inert for Seraphis and Ruinae.
//
//   4. The MODIFIED files under `plugins/seraphis/` are drawn from exactly
//      {src/parameters/dropdown_mappings.h, tests/integration/param_perf_test.cpp}
//      -- the two count-sized sites in the shipped plugin (ruled 2026-09-18:
//      the Body Material dropdown stays at five, re-pointed at
//      `kNumSeraphisMaterials`) -- and once the widening has landed both must
//      be present. Anything else modified under the plugin is out of scope.
//
// Why `feedback_ecology.h` is in scope for check 3 but OUT of scope for the
// Seraphis gate
// ------------------------------------------------------------------------
// SC-016 is specifically "Seraphis stays green", and `FeedbackEcology` has no
// Seraphis consumer. Verified by a repo-wide grep for
// `#include <krate/dsp/systems/feedback_ecology.h>` (2026-09-17): the only
// includers are `dsp/lint_all_headers.cpp:185` and the component's own four
// test TUs --
//     dsp/tests/unit/systems/feedback_ecology_test.cpp:34
//     dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp:49
//     dsp/tests/unit/systems/feedback_ecology_perf_test.cpp:115
//     dsp/tests/unit/systems/feedback_ecology_nonfinite_test.cpp:91
// -- joined this phase by Vorago's own Layer 3 headers. Nothing under
// `plugins/` and no `seraphis_*` header or TU names it. So its green gate is
// its own four suites (tasks.md T030 step 2), NOT this script's Seraphis
// scope checks: check 1 deliberately scans only `dsp/tests/unit/systems/` for
// the three named TUs, and check 2 scans only `continuous_body.h`. Widening
// SC-016 to cover `feedback_ecology.h` would assert a Seraphis relationship
// that does not exist.
//
// Diff baseline
// -------------
// Every check diffs against HEAD (`git diff HEAD`), not the index, so a change
// is caught whether it is staged or only in the working tree. A gate that
// `git add` can silence is not a gate.
//
// Usage:
//   node tools/check-seraphis-green.js
//
// Exit codes:
//   0  in scope (this includes the clean tree, where nothing is modified)
//   1  out of scope -- the offending paths and lines are printed
//   2  usage / internal error (git unavailable, not a repository, ...)
// =============================================================================

const path = require('path');
const { execFileSync } = require('child_process');

const REPO_ROOT = path.resolve(__dirname, '..');

const SYSTEMS_TEST_DIR = 'dsp/tests/unit/systems';
const BODY_HEADER = 'dsp/include/krate/dsp/systems/continuous_body.h';
const ECOLOGY_HEADER = 'dsp/include/krate/dsp/systems/feedback_ecology.h';
/// Every shipped header Phase 10 appends to; each must diff with zero deletions.
/// `expected` lists the ONLY deletions each header may show: every deleted
/// line must match one pattern, and each pattern must match exactly `count`
/// lines. An empty list is the plain zero-deletion bar.
const APPEND_ONLY_HEADERS = [
  { file: ECOLOGY_HEADER, what: 'the silenceAudio() addition (B-7)', expected: [] },
  {
    file: 'dsp/include/krate/dsp/processors/multi_stage_envelope.h',
    what: 'the setMaxStageTimeMs() ceiling (ruled 2026-09-18)',
    expected: [{ count: 3, name: 'the kMaxStageTimeMs clamp lines', pattern: /std::clamp\(ms, 0\.0f, kMaxStageTimeMs\)/ }],
  },
  {
    file: 'dsp/include/krate/dsp/processors/growth_envelope.h',
    what: 'the setMaxDuration() ceiling (ruled 2026-09-18)',
    expected: [
      { count: 1, name: 'the setDuration doc line', pattern: /clamped to \[1, 60\]/ },
      { count: 1, name: 'the kMaxDuration clamp line', pattern: /std::clamp\(seconds, kMinDuration, kMaxDuration\)/ },
    ],
  },
];
const SERAPHIS_PLUGIN_DIR = 'plugins/seraphis';
/// The only files under the shipped Seraphis plugin Phase 10 may modify.
const ALLOWED_SERAPHIS_PLUGIN_FILES = [
  'plugins/seraphis/src/parameters/dropdown_mappings.h',
  'plugins/seraphis/tests/integration/param_perf_test.cpp',
];

/// The only Seraphis-owned test TUs Phase 10 may modify (tasks.md T009).
const ALLOWED_SYSTEMS_TUS = [
  'continuous_body_perf_test.cpp',
  'continuous_body_spectral_test.cpp',
  'continuous_body_test.cpp',
  'seraphis_perf_test.cpp',
];

/// The two lines the header widening is allowed to delete, as they stand at
/// continuous_body.h:81 and :84 before the change.
const ALLOWED_DELETIONS = [
  { name: 'the BodyMaterial enum line', pattern: /enum\s+class\s+BodyMaterial\b/ },
  { name: 'the kNumMaterials line', pattern: /\bkNumMaterials\b/ },
];

function git(args) {
  return execFileSync('git', args, {
    cwd: REPO_ROOT,
    encoding: 'utf8',
    maxBuffer: 32 * 1024 * 1024,
  });
}

/// `git diff HEAD --numstat` rows for one pathspec, as
/// [{ added, deleted, file }]. Binary files report `-`/`-`; they are surfaced
/// with NaN counts so a binary blob can never pass a "zero deletions" bar
/// silently.
function numstat(pathspec, filterModifiedOnly) {
  const args = ['diff', 'HEAD', '--numstat'];
  if (filterModifiedOnly) args.push('--diff-filter=M');
  args.push('--', pathspec);
  return git(args)
    .replace(/\r\n/g, '\n')
    .split('\n')
    .filter((line) => line.trim().length > 0)
    .map((line) => {
      const [added, deleted, file] = line.split('\t');
      return {
        added: added === '-' ? NaN : Number(added),
        deleted: deleted === '-' ? NaN : Number(deleted),
        file,
      };
    });
}

/// The removed lines of one file's diff, without the `---` file header.
function deletedLines(file) {
  return git(['diff', 'HEAD', '-U0', '--', file])
    .replace(/\r\n/g, '\n')
    .split('\n')
    .filter((line) => line.startsWith('-') && !line.startsWith('---'))
    .map((line) => line.slice(1));
}

// -----------------------------------------------------------------------------
// Check 2 runs first: its result tells check 1 whether the header widening has
// landed, and therefore whether all three TU edits must already be present.
// -----------------------------------------------------------------------------
function checkContinuousBodyHeader(failures) {
  const rows = numstat(BODY_HEADER, false);
  if (rows.length === 0) {
    console.log(`  [2] ${BODY_HEADER}: unmodified -- append not yet made, nothing to bound`);
    return { widened: false };
  }

  const row = rows[0];
  const removed = deletedLines(BODY_HEADER);

  if (Number.isNaN(row.deleted)) {
    failures.push(
      `${BODY_HEADER}: diffs as BINARY. The header must stay a text file with an ` +
        'auditable diff.'
    );
    return { widened: false };
  }

  if (removed.length === 0) {
    console.log(`  [2] ${BODY_HEADER}: modified, 0 deletions -- append-only so far`);
    return { widened: false };
  }

  if (removed.length !== ALLOWED_DELETIONS.length) {
    failures.push(
      `${BODY_HEADER}: ${removed.length} deleted line(s), expected exactly ` +
        `${ALLOWED_DELETIONS.length} (${ALLOWED_DELETIONS.map((d) => d.name).join(' and ')}):\n` +
        removed.map((l) => `        -${l}`).join('\n')
    );
    return { widened: false };
  }

  // Exactly two: each must match a DIFFERENT one of the two allowed patterns.
  const unmatched = removed.filter(
    (line) => !ALLOWED_DELETIONS.some((d) => d.pattern.test(line))
  );
  const matchedNames = new Set();
  for (const line of removed) {
    for (const d of ALLOWED_DELETIONS) {
      if (d.pattern.test(line)) matchedNames.add(d.name);
    }
  }

  if (unmatched.length > 0 || matchedNames.size !== ALLOWED_DELETIONS.length) {
    failures.push(
      `${BODY_HEADER}: the two deleted lines are not ` +
        `${ALLOWED_DELETIONS.map((d) => d.name).join(' and ')}:\n` +
        removed.map((l) => `        -${l}`).join('\n')
    );
    return { widened: false };
  }

  console.log(
    `  [2] ${BODY_HEADER}: exactly 2 deletions, both expected ` +
      '(BodyMaterial enum + kNumMaterials)'
  );
  return { widened: true };
}

function checkSeraphisOwnedTus(failures, widened) {
  const rows = numstat(`${SYSTEMS_TEST_DIR}/`, true);
  const modified = rows.map((r) => path.posix.basename(r.file.replace(/\\/g, '/')));

  const unexpected = modified.filter((f) => !ALLOWED_SYSTEMS_TUS.includes(f));
  if (unexpected.length > 0) {
    failures.push(
      `${SYSTEMS_TEST_DIR}/: modified TU(s) outside the allowed four:\n` +
        unexpected.map((f) => `        ${SYSTEMS_TEST_DIR}/${f}`).join('\n') +
        `\n      Allowed: ${ALLOWED_SYSTEMS_TUS.join(', ')}`
    );
  }

  if (widened) {
    const missing = ALLOWED_SYSTEMS_TUS.filter((f) => !modified.includes(f));
    if (missing.length > 0) {
      failures.push(
        `${SYSTEMS_TEST_DIR}/: kNumMaterials has been widened but these ` +
          'count-sized TU(s) are unmodified -- they would zero-fill rather than ' +
          'break the build:\n' +
          missing.map((f) => `        ${SYSTEMS_TEST_DIR}/${f}`).join('\n')
      );
    }
  }

  if (unexpected.length === 0 && (!widened || modified.length === ALLOWED_SYSTEMS_TUS.length)) {
    console.log(
      `  [1] ${SYSTEMS_TEST_DIR}/: ${modified.length} modified TU(s), all in scope` +
        (widened ? ' (all four present)' : '')
    );
  }
}

function checkAppendOnlyHeader(failures, { file, what, expected }) {
  const rows = numstat(file, false);
  if (rows.length === 0) {
    console.log(`  [3] ${file}: unmodified`);
    return;
  }

  const removed = deletedLines(file);
  if (Number.isNaN(rows[0].deleted)) {
    failures.push(`${file}: diffs as BINARY; the header must stay text.`);
    return;
  }

  const allowedTotal = expected.reduce((n, e) => n + e.count, 0);
  const unmatched = removed.filter((line) => !expected.some((e) => e.pattern.test(line)));
  const offCount = expected.filter((e) => removed.filter((line) => e.pattern.test(line)).length !== e.count);
  if (removed.length === 0 && allowedTotal === 0) {
    console.log(`  [3] ${file}: modified, 0 deletions -- append-only`);
    return;
  }
  if (removed.length !== allowedTotal || unmatched.length > 0 || offCount.length > 0) {
    failures.push(
      `${file}: ${removed.length} deleted line(s); ${what} ` +
        (allowedTotal === 0
          ? 'must be strictly append-only:\n'
          : `may delete only ${expected.map((e) => `${e.count} x ${e.name}`).join(' and ')}:\n`) +
        removed.map((l) => `        -${l}`).join('\n')
    );
    return;
  }

  console.log(`  [3] ${file}: modified, ${removed.length} deletion(s), all expected -- default-inert`);
}

function checkSeraphisPluginFiles(failures, widened) {
  const rows = numstat(`${SERAPHIS_PLUGIN_DIR}/`, true);
  const modified = rows.map((r) => r.file.replace(/\\/g, '/'));

  const unexpected = modified.filter((f) => !ALLOWED_SERAPHIS_PLUGIN_FILES.includes(f));
  if (unexpected.length > 0) {
    failures.push(
      `${SERAPHIS_PLUGIN_DIR}/: modified file(s) outside the two count-sized sites:\n` +
        unexpected.map((f) => `        ${f}`).join('\n') +
        `\n      Allowed: ${ALLOWED_SERAPHIS_PLUGIN_FILES.join(', ')}`
    );
  }

  if (widened) {
    const missing = ALLOWED_SERAPHIS_PLUGIN_FILES.filter((f) => !modified.includes(f));
    if (missing.length > 0) {
      failures.push(
        `${SERAPHIS_PLUGIN_DIR}/: kNumMaterials has been widened but these ` +
          'count-sized plugin file(s) are unmodified -- the label list and the ' +
          'perf survey would desynchronise or zero-fill:\n' +
          missing.map((f) => `        ${f}`).join('\n')
      );
    }
  }

  if (unexpected.length === 0 && (!widened || modified.length === ALLOWED_SERAPHIS_PLUGIN_FILES.length)) {
    console.log(
      `  [4] ${SERAPHIS_PLUGIN_DIR}/: ${modified.length} modified file(s), all in scope` +
        (widened ? ' (both present)' : '')
    );
  }
}

function main() {
  git(['rev-parse', '--git-dir']); // throws (-> exit 2) outside a repository

  console.log('check-seraphis-green: SC-016 clauses 1-2, diffed against HEAD\n');

  const failures = [];
  const { widened } = checkContinuousBodyHeader(failures);
  checkSeraphisOwnedTus(failures, widened);
  for (const h of APPEND_ONLY_HEADERS) checkAppendOnlyHeader(failures, h);
  checkSeraphisPluginFiles(failures, widened);

  if (failures.length === 0) {
    console.log('\ncheck-seraphis-green: in scope');
    return 0;
  }

  console.error('\ncheck-seraphis-green: OUT OF SCOPE\n');
  for (const f of failures) console.error(`  ${f}\n`);
  console.error(
    'The ContinuousBody material append (AR-4) is only safe because it is\n' +
      'append-only and touches exactly four Seraphis-owned test TUs and two\n' +
      'count-sized files in the shipped plugin. Each\n' +
      'violation above breaks that argument -- fix the change, do not widen the\n' +
      'allowed list. See specs/vorago-phase10-voice-engine/spec.md, SC-016.\n'
  );
  return 1;
}

try {
  process.exit(main());
} catch (err) {
  console.error('check-seraphis-green: internal error:', err && err.message);
  process.exit(2);
}
