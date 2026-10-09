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
//      `atmosphere_engine.h` (Phase 10a, FR-045) is the one entry whose
//      allowed deletions are not a handful of clamp lines: the ghost
//      extension rewrites two private member-function bodies in place. Its
//      whole measured anchor set, with the per-anchor counts and how they
//      compare with SC-006 clause 2's prediction, is tabulated on the entry
//      itself below.
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
  {
    // -------------------------------------------------------------------------
    // Vorago Phase 10a (specs/vorago-phase10a-ghost-extension), FR-045 and
    // SC-006 clauses 1-2, 5-6. `AtmosphereEngine` is the SHIPPED Seraphis
    // atmosphere; the ghost extension appends reverse grains, an external
    // `triggerGrain()` and a separate blur RNG stream to it.
    //
    // Every `count` below was MEASURED from the real
    // `git diff HEAD -U0 -- dsp/include/krate/dsp/systems/atmosphere_engine.h`
    // (121 deleted lines, 380 added) and is now FROZEN. The bar is exact on
    // both sides: a deletion anywhere else in the header either matches no
    // pattern (the `unmatched` arm) or pushes one pattern's tally off its
    // frozen count (the `offCount` arm). Both fail the gate. Do not "fix" a
    // red run by bumping a count -- the count IS the assertion.
    //
    // MEASURED anchor set, by PRE-change line range, against the six anchors
    // SC-006 clause 2 predicted:
    //
    //   (i)   :1651-1652    2   the wUp/wDown rate lines       AS PREDICTED
    //   (ii)  :1872-1880    9   the advance() lambda and the   WIDER than
    //         :1884-1928   45   two renderGrainSpan loops      predicted
    //         :1931-1941   11   that call it                   (87 lines over
    //         :1943-1950    8                                   5 ranges, not
    //         :1953-1966   14                                   25 over 3:
    //                                                           :1876-1882,
    //                                                           :1888-1891,
    //                                                           :1917-1930)
    //   (iii) :2110-2114    5   the pass-A scheduler tick      WIDER than
    //         :2116-2134   19                                  predicted
    //                                                          (:2115 alone;
    //                                                           :2115 itself
    //                                                           is CONTEXT,
    //                                                           not deleted)
    //   (iv)  :555-556      0   the reset()/setSeed() blur-    NARROWER:
    //         :1015-1017    0   seed lines                     pure appends,
    //                                                          zero deletions
    //   (v)   :437-441      5   the pass-A scratch sizing      within the
    //                           comment + the two assigns      predicted
    //                                                          :436-441
    //   (vi)  :1183         1   readFrac's range comment       AS PREDICTED
    //         :2595-2596    2   the scratch decl comment       AS PREDICTED
    //                    ----
    //                     121
    //
    // (ii) and (iii) are private member-function bodies (`renderGrainSpan`
    // and the pass-A birth loop) rewritten in place to carry the reversed
    // flag and the external-trigger drain. No public declaration, no default,
    // no constant and no Seraphis-observable value is among the 121; that is
    // what keeps the append-only argument standing despite the width. The
    // (iv) row is kept at `count: 0` deliberately: it is a live negative
    // assertion that the blur-seed lines stay pure appends.
    // -------------------------------------------------------------------------
    file: 'dsp/include/krate/dsp/systems/atmosphere_engine.h',
    what: 'the Phase 10a ghost extension (reverse grains + triggerGrain)',
    expected: [
      // --- (v) :437-441 -- prepare()'s pass-A scratch sizing -----------------
      {
        count: 3,
        name: 'the pass-A scratch sizing comment (:437-439)',
        pattern:
          /^\s+\/\/\s+(here: <= kMaxGrains grains active at a chunk start plus|<= kControlChunkSamples births can retire inside one chunk, and|kControlChunkSamples == kMaxGrains, so 2 \* kMaxGrains bounds both\.)$/,
      },
      {
        count: 2,
        name: 'the pass-A scratch assign lines (:440-441)',
        pattern:
          /^\s+(retiredScratch_\.assign\(kMaxGrains \* 2, RetiredGrainSpan\{\}\);|dueScratch_\.assign\(kMaxGrains \* 2, DueEntry\{\}\);)$/,
      },

      // --- (vi) :1183 and :2595-2596 -- the two corrected comments -----------
      {
        count: 1,
        name: "AtmosphereGrain::readFrac's range comment (:1183)",
        pattern: /^\s+float readFrac = 0\.0f;\s+\/\/\/< absolute source index, fraction in \[0,1\)$/,
      },
      {
        count: 2,
        name: 'the pass-A scratch declaration comment (:2595-2596)',
        pattern:
          /^\s+\/\/ (Sized once in prepare\(\) \(2 \* kMaxGrains each\); indexed by count, never|pushed on the audio thread\.)$/,
      },

      // --- (i) :1651-1652 -- the blur-width rate lines -----------------------
      {
        count: 2,
        name: 'the wUp/wDown rate lines (:1651-1652)',
        pattern: /^\s+const double w(Up|Down) = std::max\(/,
      },

      // --- (iv) :555-556 and :1015-1017 -- MEASURED ZERO ---------------------
      {
        count: 0,
        name: 'the reset()/setSeed() blur-seed lines (MEASURED ZERO: pure appends)',
        pattern: /blurRng_\.seed\(deriveStreamSeed\(/,
      },

      // --- (ii) :1872-1880, :1884-1928, :1931-1941, :1943-1950, :1953-1966 ---
      {
        count: 4,
        name: "the advance lambda's truncation comment (:1872-1875)",
        pattern:
          /^\s+\/\/ (rate\. TRUNCATION, NOT std::floor: readFrac is non-negative for the|grain's whole life, so truncation toward zero IS the floor - and|std::floor\(float\) is a CRT call on MSVC's default \/arch\. The carry|is in \[0, 8\] because ratio <= 8\.)$/,
      },
      {
        count: 5,
        name: 'the advance lambda itself (:1876-1880)',
        pattern:
          /^\s+(const auto advance = \[&\]\(\) noexcept \{|readFrac \+= ratio;|const auto carryInt = static_cast<std::int32_t>\(readFrac\);|readIndexInt \+= static_cast<std::uint64_t>\(carryInt\);|readFrac -= static_cast<float>\(carryInt\);)$/,
      },
      {
        count: 2,
        name: 'the two advance() call sites inside renderGrainSpan',
        pattern: /^\s+advance\(\);$/,
      },
      {
        count: 5,
        name: "renderGrainSpan's cold-path comment",
        pattern:
          /^\s+\/\/ (Cold path: every read yields 0 \(the reader's own rule\), so the|span contributes nothing - but state, folds and ages must still|advance exactly\. Unreachable while any grain is admitted \(FR-014|demands >= kMinAgeSamples available at birth\); kept for the same|defensive reason readStereo\(\) zero-fills\.)$/,
      },
      {
        count: 4,
        name: "renderGrainSpan's cold-path branch and loop",
        pattern:
          /^\s+(if \(!reader\.isValid\(\) \|\| envelope == nullptr\) \{|for \(std::size_t i = start; i < spanEnd; \+\+i\) \{|foldAt\(i, ageAt\(i\)\);|\} else \{)$/,
      },
      {
        count: 9,
        name: "renderGrainSpan's Phase 1 comment",
        pattern:
          /^\s+\/\/ (--- Phase 1 \(scalar\): exact per-sample recurrences --------------|Ring indices\/weights via LinearReader::indexAt - the SAME|clamp\/truncate\/rebase arithmetic as readStereoOffset\(\), so every|position and weight is bit-identical to the pre-SIMD shape\.|Envelope index\/weight is GrainEnvelope::lookup's arithmetic|verbatim \(core\/grain_envelope\.h:165-196, including the NaN-safe|clamp and the index1-at-the-boundary rule, so no gather can|overread the envelope bank\)\. Everything lands in stack arrays|sized for one control chunk \(~2\.3 KB\)\.)$/,
      },
      {
        count: 9,
        name: "renderGrainSpan's nine Phase 1 stack arrays",
        pattern:
          /^\s+alignas\(32\) std::array<(std::int32_t|float), kControlChunkSamples> (idxL0|idxL1|fracL|idxR0|idxR1|fracR|envI0|envI1|envF);$/,
      },
      {
        count: 3,
        name: "renderGrainSpan's Phase 1 locals",
        pattern:
          /^\s+(const bool decorr = decorrAge > 0\.0f;|const auto lastEnv = static_cast<std::ptrdiff_t>\(kEnvelopeTableSize - 1\);|std::size_t m = 0;)$/,
      },
      {
        count: 5,
        name: "renderGrainSpan's reader-snapshot comment",
        pattern:
          /^\s+\/\/ (The reader snapshot is END-of-chunk; the index is rebased by|how many samples newer than sample i that snapshot is, so|position and weights match a per-sample snapshot bit for bit\.|The R channel reads a DIFFERENT point of the ring;|skipped entirely at decorrelation = 0\.)$/,
      },
      {
        count: 7,
        name: "renderGrainSpan's Phase 1 loop head and ring reads",
        pattern:
          /^\s+(for \(std::size_t i = start; i < spanEnd; \+\+i, \+\+m\) \{|const float ageNow = ageAt\(i\);|const std::size_t newerOffset = numSamples - 1u - i;|reader\.indexAt\(ageNow, newerOffset, idxL0\[m\], idxL1\[m\], fracL\[m\]\);|if \(decorr\) \{|reader\.indexAt\(ageNow \+ decorrAge, newerOffset, idxR0\[m\], idxR1\[m\],|fracR\[m\]\);)$/,
      },
      {
        count: 5,
        name: "renderGrainSpan's envelope-phase comment",
        pattern:
          /^\s+\/\/ (Envelope phase is MULTIPLIED, never accumulated: ageSamples|is exact to 2\^24, so this costs one rounding, whereas a|`phase \+= 1\/L'` accumulator over 1\.44 M additions drifts by|up to ~4 % of full scale and would retire a grain at|envelope ~0\.02 instead of 0 - a click\.)$/,
      },
      {
        count: 10,
        name: "renderGrainSpan's envelope index arithmetic",
        pattern:
          /^\s+(float phase = static_cast<float>\(age\) \* envPhaseInc;|if \(!\(phase >= 0\.0f\)\) \{|phase = 0\.0f;|if \(phase > 1\.0f\) \{|phase = 1\.0f;|const float indexFloat = phase \* static_cast<float>\(lastEnv\);|const auto e0 = static_cast<std::ptrdiff_t>\(indexFloat\);|envI0\[m\] = static_cast<std::int32_t>\(e0\);|envI1\[m\] = static_cast<std::int32_t>\(\(e0 < lastEnv\) \? e0 \+ 1 : lastEnv\);|envF\[m\] = indexFloat - static_cast<float>\(e0\);)$/,
      },
      {
        count: 1,
        name: "renderGrainSpan's Phase 1 fold call",
        pattern: /^\s+foldAt\(i, ageNow\);$/,
      },
      {
        count: 6,
        name: "renderGrainSpan's Phase 2 comment",
        pattern:
          /^\s+\/\/ (--- Phase 2 \(vector\): gathers \+ lerps \+ accumulate --------------|Six gathers, three lerps, two FMAs per sample, all PER-LANE - no|cross-lane reduction - so vector grouping \(and therefore the|caller's block partition\) cannot change any sample's value; see|grain_span_simd\.h\. A non-decorrelated grain hands the L index|arrays to the R reads: same positions, R channel data\.)$/,
      },
      {
        count: 8,
        name: "renderGrainSpan's accumulateGrainSpanSIMD call",
        pattern:
          /^\s+(accumulateGrainSpanSIMD\(reader\.leftData\(\), reader\.rightData\(\), idxL0\.data\(\),|idxL1\.data\(\), fracL\.data\(\),|decorr \? idxR0\.data\(\) : idxL0\.data\(\),|decorr \? idxR1\.data\(\) : idxL1\.data\(\),|decorr \? fracR\.data\(\) : fracL\.data\(\), envelope,|envI0\.data\(\), envI1\.data\(\), envF\.data\(\), grain\.panL,|grain\.panR, m, busL_\.data\(\) \+ start,|busR_\.data\(\) \+ start\);)$/,
      },

      // --- (iii) :2110-2114 and :2116-2134 -- the pass-A scheduler tick ------
      {
        count: 5,
        name: 'the pass-A scheduling comment (:2110-2114)',
        pattern:
          /^\s+\/\/\s+(--- Scheduling \(FR-021\)\. GrainScheduler::process\(\) draws exactly|one rng value on a trigger \(grain_scheduler\.h:82\)\. The|admission tests inside tryBirthGrain\(\) read the capture ring|AS OF THIS SAMPLE - this pass stays per-sample for exactly|that reason\.)$/,
      },
      {
        count: 5,
        name: 'the scheduler-tick birth block (:2116-2120)',
        pattern:
          /^\s+(const std::size_t before = activeCount_;|tryBirthGrain\(\);|if \(activeCount_ > before\) \{|const std::size_t slot = activeIdx_\[activeCount_ - 1\];|bornAt\[slot\] = static_cast<std::uint32_t>\(i\) \+ 1u;)$/,
      },
      {
        count: 3,
        name: "the scheduler tick's newborn-due comment (:2121-2123)",
        pattern:
          /^\s+\/\/ (A newborn can retire inside this same chunk \(lifetime is|only bounded below by 2\): insert its due entry into the|unconsumed, still-sorted suffix\.)$/,
      },
      {
        count: 9,
        name: "the scheduler tick's due-entry insertion (:2124-2133)",
        pattern:
          /^\s+(const auto lifetime = static_cast<std::size_t>\(grains_\[slot\]\.lifetime\);|if \(i \+ lifetime <= numSamples\) \{|const auto r = static_cast<std::uint32_t>\(i \+ lifetime - 1u\);|std::size_t k = dueCount;|while \(k > dueCursor && dueScratch_\[k - 1\]\.r > r\) \{|dueScratch_\[k\] = dueScratch_\[k - 1\];|--k;|dueScratch_\[k\] = DueEntry\{r, static_cast<std::uint8_t>\(slot\)\};|\+\+dueCount;)$/,
      },

      // --- the six structural lines the rewritten spans also carry -----------
      // Deliberately indentation-exact: a stray brace deleted at any OTHER
      // depth matches nothing, and a second one at the same depth breaks the
      // frozen count.
      { count: 2, name: 'the two blank lines inside the rewritten spans', pattern: /^$/ },
      { count: 1, name: 'the 12-space closing brace (cold-path loop)', pattern: /^ {12}\}$/ },
      // TWO of these, at the two 16-space depths the rewrite closes: the
      // envelope clamp inside renderGrainSpan's Phase 1 loop, and the
      // `if (activeCount_ > before) {` that closed the pass-A scheduler tick's
      // newborn bookkeeping. The second appeared when FR-022's consumption moved
      // out of the per-sample body into the `consumePendingTrigger` lambda
      // (SC-006 clause 6 anchor 2's token rule); before that the diff aligned the
      // brace against the trigger block's own closer and reported one.
      { count: 2, name: 'the 16-space closing braces (envelope clamp, scheduler-tick if)', pattern: /^ {16}\}$/ },
      { count: 1, name: 'the 20-space closing brace (newborn-due if)', pattern: /^ {20}\}$/ },
      { count: 1, name: 'the 24-space closing brace (insertion while)', pattern: /^ {24}\}$/ },
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
  // Phase 10a FR-047 -- the only edit is replacing seven constant definitions
  // with `#include "vorago_perf_budget.h"`; every other line is unchanged and
  // its own static_asserts stay in place.
  'vorago_perf_test.cpp',
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
