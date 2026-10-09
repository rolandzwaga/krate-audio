// ==============================================================================
// vorago_perf_budget.h
// The Vorago Phase 10 CPU-budget constants, shared by the Phase 10 perf TU and
// the Phase 10a ghost CPU-delta TU
// ==============================================================================
// Spec:  specs/vorago-phase10a-ghost-extension/spec.md   (FR-041, FR-047,
//        SC-006 clause 1, Clarifications 2026-09-22 Q7, amendment R-3 2026-09-23)
// Plan:  specs/vorago-phase10a-ghost-extension/plan.md   S4.2, S8 ruling 3
// Tasks: specs/vorago-phase10a-ghost-extension/tasks.md  T001
//
// WHAT THIS FILE IS. Seven constant DEFINITIONS moved out of
// `dsp/tests/unit/systems/vorago_perf_test.cpp`, VALUE-UNEDITED, each with the
// provenance comment it carried there copied verbatim. The `:NNN` citations
// below are that file's line numbers at the Phase 10 base commit `374580d7`.
// Nothing here is new: no new value, no renamed constant, no new
// `static_assert`. FR-041 makes every number below READ-ONLY to Phase 10a -
// re-measure and re-transcribe, never hand-edit.
//
// WHY SEVEN AND NOT FOUR (spec R-3 / plan S8 ruling 3). FR-047 names four
// constants - `kReferenceNs`, `kEngineMeasuredNsAtPoly4`,
// `kEngineBaselineNsAtPoly4`, `kCavernMeasuredNsPerBlock`. `kReferenceNs` is
// DERIVED - `kBlockBudgetNs * 0.30` (`vorago_perf_test.cpp:168`) with
// `kBlockBudgetNs = (kBlockSize / kSr48) * 1e9` (`:160`) - so `kSr48`,
// `kBlockSize` and `kBlockBudgetNs` have to travel with it. Seven definitions
// moved: four of them the FR-047 four, three of them `kReferenceNs`'s
// derivation inputs. No value edited.
//
// WHAT DELIBERATELY STAYED IN `vorago_perf_test.cpp` (plan S4.2): `kSr192`
// (`:156`), `kRegressionFactor` (`:165`), `kMaxAdmissibleNs` (`:176`),
// `kCavernBaselineNsPerBlock` (`:221`), `kBaselineWithCavernNs` (`:258`),
// `kAvailableRegressionHeadroom` (`:259-260`) and all three `static_assert`s on
// these figures (`:263-266`, `:269`, `:272-274`). Those three static_asserts
// ARE the test for this move: they are evaluated at compile time against the
// values below, so if the move had changed a value the TU would stop compiling.
//
// NO CMAKE ENTRY. `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests` source list
// is enumerated and names `.cpp` only; a test-local header living beside its
// TUs is house-legal, with precedents in this directory:
//   dsp/tests/unit/systems/ecosystem_metrics_test_helpers.h
//   dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h
//
// HOW TO CONSUME IT. The constants live in `Krate::DSP::TestUtils::Vorago` -
// the namespace `tests/test_helpers/vorago_fixtures.h:70-73` already opens - so
// a TU pulls in the names it uses exactly the way it already pulls in
// `makeEngine`:
//   using Krate::DSP::TestUtils::Vorago::kBlockBudgetNs;
//   using Krate::DSP::TestUtils::Vorago::kBlockSize;
//   using Krate::DSP::TestUtils::Vorago::kCavernMeasuredNsPerBlock;
//   using Krate::DSP::TestUtils::Vorago::kEngineBaselineNsAtPoly4;
//   using Krate::DSP::TestUtils::Vorago::kEngineMeasuredNsAtPoly4;
//   using Krate::DSP::TestUtils::Vorago::kReferenceNs;
//   using Krate::DSP::TestUtils::Vorago::kSr48;
// They are `inline constexpr`, so every including TU shares one definition and
// a TU that names only some of them trips no unused-variable warning under /W4
// or -Wall.
// ==============================================================================
#pragma once

#include <cstddef>

namespace Krate::DSP::TestUtils::Vorago {

// =============================================================================
// Measurement basis (plan S12.1), inherited VERBATIM from
// dsp/tests/unit/effects/cavern_verb_perf_test.cpp:105-125 - with ONE constant
// changed, and deliberately: kReferenceNs is roadmap line 470's THIRTY percent
// global instrument budget, not Phase 9's five percent for one reverb.
// =============================================================================
// (`vorago_perf_test.cpp:148-153`, copied with the definitions it describes.)

/// Moved unedited from `vorago_perf_test.cpp:155`.
inline constexpr double kSr48 = 48000.0;
/// Moved unedited from `vorago_perf_test.cpp:157`.
inline constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
/// Moved unedited from `vorago_perf_test.cpp:160`.
inline constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// roadmap line 470's global ceiling: 30 % of one core, 3 200 000 ns/block.
/// Moved unedited from `vorago_perf_test.cpp:168`.
inline constexpr double kReferenceNs = kBlockBudgetNs * 0.30;

// =============================================================================
// The CavernVerb term - ADDED ARITHMETICALLY, NEVER INSTANTIATED
// =============================================================================
// AR-1 puts the reverb OUTSIDE this engine, owned by the caller, and this TU is
// a Layer 3 TU that may not name a Layer 4 type. The figure is therefore
// transcribed from the shipped Phase 9 ledger and added to the ladder as a
// constant. T022's composed-chain case (dsp_effects_tests) cross-checks the sum
// ONCE by measuring the real composed chain.
//
// PROVENANCE: specs/vorago-phase9-cavern-space/compliance.md:123, SC-009 pass 3
// (DATASET 2, interleaved trio, P-core-pinned, run alone), arm (a) DEFAULT -
// "(a) default : 124497" ns per 512-sample block at 48 kHz. Arm (a) is the right
// arm: Vorago drives the cavern at VoragoCavernTargets, which ARE CavernVerb's
// own FR-066 defaults, not arm (b)'s every-control-at-its-extreme worst case.
// (`vorago_perf_test.cpp:201-215`.)
inline constexpr double kCavernMeasuredNsPerBlock = 124497.0;

// =============================================================================
// SC-001b - the checked-in baseline (FR-083), transcribed 2026-09-21
// =============================================================================
// THE FIGURE IT PINS: VoragoEngine at the RULED shipped polyphony
// (kDefaultPolyphony = 4) with kMaxVoices = 6, every sub-component at the
// FR-090 defaults, macros at the FR-061 neutral, NO voicing levers, measured by
// the SAME arm VoragoEngine_CpuSurvey sweeps (buildEngineAtPolyphony, best-of-25
// x 500 blocks after 400 warm-up blocks): 2 566 170 ns per 512-sample block at
// 48 kHz. Baseline = ceil(2 566 170 x 1.05) = 2 694 479.
//
// PROVENANCE: dsp_systems_tests.exe pinned to the performance cores through
// tools/pin-perf-cores.ps1 (the helper node tools/run-cpu-tests.js uses), the
// [.perf] cases only with [long] excluded, run ALONE after a restart and a
// 15-minute idle - the protocol spec SC-001b names. The warm-machine survey the
// ruling was taken from (Q-H, 2026-09-19) read 2 579 723 for the same arm; the
// two agree within 0.6 %. A run of the same lane started two minutes after a
// 25-minute test lane, with the survey placed after three accelerated soaks,
// read 2 757 260 (+7.4 %) - which is why the protocol says "after a restart
// and cool-down" and why THAT figure was not transcribed.
//
// WHAT THE GATE IS (spec SC-001b as ruled, Q-H):
//   (i)  engine + Cavern <= kReferenceNs                - the roadmap ceiling;
//   (ii) engine          <= baseline x kRegressionFactor - the regression line.
// The Cavern term is the constant above (this TU may not name a Layer 4 type).
// Baseline + Cavern = 2 818 976 ns/block, which is ABOVE kMaxAdmissibleNs: the
// original clause (ii) - "baseline <= ceiling / 1.5" - is NOT met at polyphony
// 4, was measured to be unreachable at any admissible (polyphony, lever set)
// pair without a voicing lever the user refused (L-3), and was replaced by
// ruling. The ceiling therefore binds BEFORE the 1.5x line does, and the
// regression headroom actually available is recorded here, next to the
// baseline, rather than implied by kRegressionFactor:
//   (kReferenceNs - Cavern) / baseline = 1.141x.
// A regression larger than that trips clause (i), not clause (ii).
// (`vorago_perf_test.cpp:223-257`. `kRegressionFactor`, `kMaxAdmissibleNs`,
//  `kBaselineWithCavernNs` and `kAvailableRegressionHeadroom` stay in that TU;
//  only the two transcribed figures move.)
inline constexpr double kEngineMeasuredNsAtPoly4 = 2566170.0;
inline constexpr double kEngineBaselineNsAtPoly4 = 2694479.0;

}  // namespace Krate::DSP::TestUtils::Vorago
