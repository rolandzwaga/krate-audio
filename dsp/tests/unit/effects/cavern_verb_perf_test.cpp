// ==============================================================================
// Layer 4: Effect Tests - CavernVerb, CPU budget (SC-009), [.perf]
//                                        (specs/vorago-phase9-cavern-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase9-cavern-space/spec.md   (SC-009, :1151-1175)
//            specs/vorago-phase9-cavern-space/plan.md   (S12.1 - S12.3,
//                                                        :1509-1550)
//            specs/vorago-phase9-cavern-space/tasks.md  (T001 creates this TU,
//                                                        T016 fills it)
//
// SCOPE OF THIS TU: SC-009 only. The case carries the [.perf] tag so it is
//   excluded from every default run and selected deliberately:
//     node tools/run-cpu-tests.js dsp_effects_tests
//     build/windows-x64-release/bin/Release/dsp_effects_tests.exe "[.perf][cavern]"
//
// COMPILE FLAGS: none, deliberately. This TU is absent from BOTH the
//   -fno-fast-math list and the -O2 cap list in dsp/tests/CMakeLists.txt
//   (:533-537 lists it in the plain enumerated source list): either flag would
//   change the figures the CPU baselines are pinned to.
//
// WHY ns/block AND NOT "% of one core":
//   inherited verbatim from dsp/tests/unit/effects/aether_reverb_perf_test.cpp
//   (:23-30, :133-147). A percent-of-core figure is not reproducible across dev
//   machines or CI runners - identical code passes or fails by hardware. SC-009
//   therefore pins the basis to NANOSECONDS PER 512-SAMPLE BLOCK at 48 kHz and
//   gates against a checked-in baseline as a relative regression bound (fail if
//   > baseline x 1.5). The percent-of-budget figure is REPORTED via WARN, never
//   asserted.
//
// HOW THE ABSOLUTE ROADMAP FIGURE IS STILL BOUND (spec.md:1151-1156):
//   roadmap line 430 makes "CPU <= 5 % of one core, global" a FUNCTIONAL
//   requirement, so a purely relative gate would not discharge SC-009. Each
//   checked-in baseline therefore carries BOTH compile-time clauses
//
//       static_assert(baseline * kRegressionFactor <= kReferenceNs, ...);
//       static_assert(baseline <= kMaxAdmissibleNs, ...);
//
//   alongside the run-time REQUIRE(measured <= baseline * kRegressionFactor).
//   The two COMPOSE: a baseline that would let `measured` exceed the reference
//   does not COMPILE, so the run-time REQUIRE transitively binds the absolute
//   figure on every machine and every run. The "[.perf]" tag keeps the TIMING
//   out of CI, but the static_asserts are evaluated by every CI leg regardless
//   of tags - which is exactly why the gate is placed there.
//
// CavernVerb IS GLOBAL - ONE INSTANCE (spec.md:1151, roadmap line 430). The
//   figure does NOT multiply by polyphony.
//
// IF AN ARM IS OVER BUDGET: REDUCE COST, NEVER RAISE THE BASELINE.
//   plan S12.3's ORDERED lever list (plan.md:1535-1549), applied in order:
//     L-1. In applyDamperOffsets, hoist 1 - dampCoeff_[i] and recompute only
//          when either dampCoeff_[i] or damperOffset_[i] actually changed.
//     L-2. Replace pow(1-c, p) with exp2(p * log2(1-c)), hoisting log2(1-c)
//          into the updateDecayAndDamping recompute - one exp2 per line per
//          chunk in the steady state, to the same float law.
//     L-3. Hoist the equal-power cos/sin to the control grid and lerp both
//          gains per sample. ITS ACCEPTANCE GATE IS CavernVerb_MixLaw
//          (plan.md:962-964, :1418): a lerp whose error exceeds the claimed
//          0.1 % of full scale shows up there as a mix-sweep power dip.
//     L-4. Skip the per-tap absorption one-pole for taps whose fc_i has been
//          Nyquist-clamped to fcMax.
//     L-5. IF AND ONLY IF L-1 ... L-4 are insufficient: STOP AND SURFACE the
//          measurement with the arm, the configuration and the number.
//   Do NOT reduce kEarlyTapCount, do NOT shrink a workload, do NOT relax the
//   reference, and never renegotiate kRegressionFactor at implementation time
//   (FR-082).
//
// FR-071: no std::isnan / std::isinf / std::isfinite anywhere in this TU - the
//   macOS leg builds with -ffast-math, which folds them. Finiteness is checked
//   on the IEEE-754 exponent field instead (isFiniteValue below).
//
// NO ALLOCATION-TRACKING INCLUDES HERE: the global operator new/delete
//   replacement for this image already lives in
//   dsp/tests/unit/effects/aether_reverb_test.cpp; a second
//   <allocation_operator_overrides.h> is a duplicate-symbol link error. This TU
//   needs neither.
//
// FTZ/DAZ: dsp/tests/dsp_test_main.cpp:12-13 calls enableFTZDAZ() before any
//   case runs, so every figure below is measured with denormals flushed BY THE
//   PROCESS - the same environment the audio thread runs in. That matters most
//   for arm (c), where the frozen loop is very nearly lossless.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/effects/cavern_verb.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

using Krate::DSP::CavernVerb;

namespace {

// =============================================================================
// Measurement basis (SC-009, spec.md:1151-1156)
// Inherited VERBATIM from aether_reverb_perf_test.cpp:133-147.
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockBudgetNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// Relative regression bound applied to every checked-in baseline (SC-009).
constexpr double kRegressionFactor = 1.5;

/// The roadmap's 5 % GLOBAL ceiling (roadmap line 430), 533,333.33 ns/block.
/// One instance for the whole engine, so this does NOT multiply by polyphony.
constexpr double kReferenceNs = kBlockBudgetNs * 0.05;

/// The largest baseline the static_asserts can accept. A measurement above this
/// means the phase is OVER BUDGET: the response is plan S12.3's ordered lever
/// list L-1 ... L-4, then L-5 (stop and surface) - NEVER a raised baseline.
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;

// --- Structural clauses: what these numbers describe -------------------------
// If any of these moves, the measurement no longer describes the configuration
// SC-009 specified, and the TU stops compiling rather than silently reporting a
// figure for a different one.

static_assert(CavernVerb::kControlChunkSamples == 64,
              "SC-009's arms render on the 64-sample absolute control grid");
static_assert(kBlockSize % CavernVerb::kControlChunkSamples == 0u,
              "the measured block must be a whole number of control chunks");
static_assert(CavernVerb::kEarlyTapCount == 12u,
              "SC-009 measures the shipped 12-tap ER bus; reducing it is forbidden by FR-082");
static_assert(CavernVerb::kMaxChannels == 16u,
              "arm (b) is the N = 16 worst case");
// Arm (b) drives setEarlySizeMs to kEarlySizeMaxMs while prepared at
// maxEarlySeconds = 0.60. setEarlySizeMs clamps into
// [kEarlySizeMinMs, min(kEarlySizeMaxMs, maxEarlySeconds * 1000)]
// (cavern_verb.h:636-648), so the maximum is only REACHABLE while the ceiling
// fits inside that 600 ms window. Stated as a bound, not an equality, so no
// float `==` appears in a constant expression.
static_assert(CavernVerb::kEarlySizeMaxMs <= 600.0f,
              "arm (b) prepares at maxEarlySeconds = 0.60; a larger kEarlySizeMaxMs would be "
              "clamped away and the arm would silently measure a smaller ER bus");
static_assert(CavernVerb::kEarlySizeMaxMs > CavernVerb::kDefaultEarlySizeMs * 2.0f,
              "arm (b)'s 'last ER tap moved out by more than 2x' precondition needs real room "
              "between the default ER size and the maximum");

// =============================================================================
// BASELINE PROVENANCE (SC-009)
// =============================================================================
//   Machine    : hybrid performance/efficiency-core dev machine, MSVC Release,
//                pinned to the performance cores by tools/pin-perf-cores.ps1
//                (the same pinning tools/run-cpu-tests.js applies), run alone
//   Build      : MSVC Release, build/windows-x64-release
//   Trial shape: best-of-25 x 500 blocks after 400 warm-up blocks; arms (a),
//                (c) and (d) INTERLEAVED in one trial loop (see Trial shape)
//
//   DATASET 1 (2026-09-17) - the step-1 measurement these constants come from,
//   per 512-sample block, transcribed as ceil(measured x 1.05) (step 2):
//     (a) default      123 000 ns  -> kBaselineDefaultNsPerBlock   = 129 150
//     (b) worst case   181 279 ns  -> kBaselineWorstNsPerBlock     = 190 343
//     (c) frozen       110 811 ns  \ measured separately in that run; their
//     (d) damper off   144 737 ns  /  baselines come from DATASET 2 below
//   The cap (step 3) does NOT bind for any arm: the worst arm sits at 51 % of
//   kMaxAdmissibleNs. The two earlier figures that suggested it might (arm (b)
//   343 935 ns unpinned; arm (a) 308 768 ns contended) were the machine, not
//   the code: on identical code an efficiency-core placement reads up to 1.76x
//   slower (tools/run-cpu-tests.js, CORE PLACEMENT).
//
//   DATASET 1 ALSO SHOWED WHY (a)/(c)/(d) ARE NOW INTERLEAVED. Two isolated,
//   pinned runs of the same binary, one minute apart:
//                 run 1      run 2     spread
//     (a)       123 000    117 687     -4 %
//     (b)       181 279    192 119     +6 %
//     (c)       110 811    128 338    +16 %
//     (d)       144 737    115 759    -20 %
//   Each arm was timed in its own best-of-25 after the previous arm had heated
//   the package, so the (c)/(d) figures moved by more than the 10 % window of
//   their relative clauses while the code did not change - run 1 failed (d)'s
//   clause, run 2 passed. Ruled 2026-09-17: the three arms that are compared
//   against each other are timed in ONE interleaved trial loop so all three see
//   the same thermal state; the 1.10 tolerance is unchanged (spec SC-009).
//
//   DATASET 2 (2026-09-17, interleaved trio, same machine and pinning):
//     (c) frozen       112 003 ns  -> kBaselineFrozenNsPerBlock    = 117 604
//     (d) damper off   119 509 ns  -> kBaselineDamperOffNsPerBlock = 125 485
//     (a) default      124 497 ns  | (b) worst case 205 107 ns - reported, not
//                                   | transcribed: DATASET 1 holds (a) and (b)
//
//   HISTORY: before DATASET 1 the four constants were plan S12.2's PROJECTIONS
//   (plan.md:1518-1533; arm (a) ~150 000, arm (b) ~250 000), pinned before the
//   first measurement existed, exactly as aether_reverb_perf_test.cpp's own
//   T014 did before its DATASET 1 replaced them (:227-231).
//
//   REPLACEMENT PROCEDURE, and it is the only legal way these constants change:
//   1. Run the case ALONE on an idle machine, on AC, Release (P-4):
//        node tools/run-cpu-tests.js dsp_effects_tests
//      A test that flips verdicts between runs is measuring the machine, not
//      the code: confirm nothing else was running, let the machine idle, re-run
//      that suite alone, and only THEN treat it as a defect.
//   2. baseline = ceil(firstCleanMeasurement x 1.05), per arm, and record the
//      machine, the date and every measured figure in this block.
//   3. baseline = min(that figure, kMaxAdmissibleNs). If the cap BINDS for any
//      arm - i.e. the measurement exceeds kMaxAdmissibleNs - the phase is OVER
//      BUDGET: work L-1 ... L-4, then L-5. The static_asserts will not compile a
//      baseline above the cap, which is the point.
//   4. Transcribe all four figures into the compliance ledger verbatim.
//   BASELINES ARE NEVER RAISED AFTERWARDS. A measured figure may only ever move
//   a baseline DOWN relative to the figure it replaces, or fail the build.
//
//   KNOWN LIMIT OF A SINGLE QUIET WINDOW, carried over from
//   aether_reverb_perf_test.cpp:203-255: on the reference laptop, eight runs
//   taken inside one quiet window measured 2.1x-2.7x optimistically against the
//   same binary re-run thermally soaked ~40 minutes later. That is the machine,
//   not the code, and it is an ACCEPTED RISK of the [.perf] lane: if a gate
//   fails on a loaded or soaked machine, WAIT AND RE-RUN ON A QUIET ONE. It is
//   not a licence to loosen these constants.
//
// WHAT EACH ARM IS, and why it is in the set (spec.md:1157-1163):
//   (a) DEFAULT. FR-066's pinned default table, untouched - every control is
//       left at its constructed value (cavern_verb.h:1330-1343 shows the shadow
//       copies are exactly that table), prepared at diffusionFftSize = 1024 and
//       maxEarlySeconds = 0.30. The line item every other arm is read against.
//   (b) WORST CASE. setSize(1), setDamperDepth(1), setDamperRate(1), setFog(1),
//       setEarlyLevel(1), setEarlySend(1), setEarlySizeMs(max), setBreath(1),
//       N = 16, prepared at diffusionFftSize = 4096 and maxEarlySeconds = 0.60.
//       PINNED AT 4096 PRECISELY so the comparison with the shipped
//       AetherReverb worst baseline (200 114 ns/block,
//       aether_reverb_perf_test.cpp:330 - which is shimmer/bloom-INCLUSIVE and
//       4096-point) is apples to apples. At the PrepareConfig default of 1024
//       the stated room would not be the margin this arm consumes.
//   (c) FROZEN. Arm (a) with setFreeze(true), entered AFTER the network holds a
//       real tail. FR-034 skips the engine's geometry and decay/damping
//       recompute while frozen, so this should sit at or below arm (a); a
//       frozen figure ABOVE (a) is a defect, not a budget item.
//   (d) DAMPER DELTA. Arm (a) at setDamperDepth(0). Reported as a DIFFERENCE
//       (a) - (d), so the cost attributable to the moving dampers is a number
//       rather than an assertion in the abstract. At depth 0 every published
//       offset is exactly 0.0f (cavern_verb.h:1057-1061), and the engine's
//       applyDamperOffsets then takes the `off == 0.0f` assignment branch
//       (aether_reverb.h:3278-3282) instead of the exp2 + pow pair - which is
//       precisely the term L-1 and L-2 attack.
//
// EVERY ARM PINS ITS PREPARE-TIME FIELDS (spec.md:1157-1159). diffusionFftSize
//   and maxEarlySeconds dominate the STFT and ER cost respectively, and an
//   unpinned arm is not reproducible.
// =============================================================================

/// (a) FR-066 defaults, spectral @1024, maxEarlySeconds = 0.30.
/// DATASET 1: ceil(123 000 x 1.05).
constexpr double kBaselineDefaultNsPerBlock = 129150.0;

/// (b) N = 16, every control at its extreme, spectral @4096,
/// maxEarlySeconds = 0.60. DATASET 1: ceil(181 279 x 1.05).
constexpr double kBaselineWorstNsPerBlock = 190343.0;

/// (c) arm (a), frozen and settled. DATASET 2: ceil(112 003 x 1.05). Freeze
/// removes control-rate work and adds none, so it cannot legitimately exceed (a).
constexpr double kBaselineFrozenNsPerBlock = 117604.0;

/// (d) arm (a) at setDamperDepth(0). DATASET 2: ceil(119 509 x 1.05). Depth 0
/// is a strict SUBSET of arm (a)'s work (the transcendental pair is skipped).
constexpr double kBaselineDamperOffNsPerBlock = 125485.0;

// --- The eight SC-009 compile-time clauses -----------------------------------
// Two per baseline. They are equivalent by construction; both are written
// because each fails with the message that names the actual rule, and because
// the second is the form spec.md states.

static_assert(kBaselineDefaultNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-009 (a) default: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineDefaultNsPerBlock <= kMaxAdmissibleNs,
              "SC-009 (a) default: a baseline above kMaxAdmissibleNs means the phase is over "
              "budget - work plan S12.3's L-1 ... L-4 then L-5, never raise the baseline");

static_assert(kBaselineWorstNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-009 (b) worst case: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineWorstNsPerBlock <= kMaxAdmissibleNs,
              "SC-009 (b) worst case: a baseline above kMaxAdmissibleNs means the phase is over "
              "budget - work plan S12.3's L-1 ... L-4 then L-5, never raise the baseline");

static_assert(kBaselineFrozenNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-009 (c) frozen: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineFrozenNsPerBlock <= kMaxAdmissibleNs,
              "SC-009 (c) frozen: a baseline above kMaxAdmissibleNs means the phase is over "
              "budget - work plan S12.3's L-1 ... L-4 then L-5, never raise the baseline");

static_assert(kBaselineDamperOffNsPerBlock * kRegressionFactor <= kReferenceNs,
              "SC-009 (d) damper delta: baseline must be no weaker than the 5 % reference");
static_assert(kBaselineDamperOffNsPerBlock <= kMaxAdmissibleNs,
              "SC-009 (d) damper delta: a baseline above kMaxAdmissibleNs means the phase is over "
              "budget - work plan S12.3's L-1 ... L-4 then L-5, never raise the baseline");

/// (c)'s structural clause, stated as a constant relation so a future edit that
/// quietly budgets freeze ABOVE the unfrozen default does not compile.
static_assert(kBaselineFrozenNsPerBlock <= kBaselineDefaultNsPerBlock,
              "SC-009 (c): freeze must not be MORE expensive than arm (a) - FR-034 skips the "
              "engine's geometry and decay/damping recompute while frozen, so a larger frozen "
              "budget would be budgeting for a defect");

/// (d)'s structural clause: depth 0 is a strict subset of arm (a)'s work.
static_assert(kBaselineDamperOffNsPerBlock <= kBaselineDefaultNsPerBlock,
              "SC-009 (d): setDamperDepth(0) skips the exp2 + pow pair in the engine's "
              "applyDamperOffsets, so it cannot legitimately be budgeted above arm (a)");

// =============================================================================
// Trial shape
// =============================================================================
// Best-of-N: the minimum is the least OS-noise-contaminated estimate of the real
// cost, which is what a regression bound wants.
//
// MANY SHORT trials (25 x 500 blocks), copied deliberately from
// aether_reverb_perf_test.cpp:429-449 and continuous_body_perf_test.cpp:288-313.
// The dominant noise source on a hybrid CPU is not scheduling jitter smeared
// across a trial, it is the WHOLE TRIAL being migrated onto an E-core - a ~20 %
// step in ns/block that best-of-N cannot reject when N is small and each trial
// is long enough to be migrated. 500 blocks (~5.3 s of audio, ~15 ms of wall
// clock) x 25 makes it very likely at least one trial runs start-to-finish on a
// boosted P-core, which is the figure the baseline wants to describe.
//
// All four arms are SUSTAINED - continuous excitation, no transient - so every
// trial measures identical steady-state work.
//
// INTERLEAVED TRIALS FOR THE THREE ARMS THAT ARE COMPARED WITH EACH OTHER.
// (c)'s and (d)'s relative clauses read them against (a) inside a 10 % window,
// and DATASET 1 (BASELINE PROVENANCE above) measured a 16-20 % run-to-run
// spread on exactly those arms when each was timed in its own best-of-25 after
// the previous arm had heated the package. So (a), (c) and (d) are prepared and
// warmed first, and then ONE trial loop times 500 blocks of each in turn,
// 25 times, taking the best per arm: whatever the package is doing at trial t,
// all three arms see it. Arm (b) has no relative clause and keeps its own loop.
// The absolute baselines are unaffected - every arm is still best-of-25 x 500.

constexpr int kTrials = 25;

/// ~4.3 s: past the 300 ms ER-size smoother, the 100 ms absorption smoother, the
/// 50 ms mix/gate ramps, the engine's own 50 ms freeze latch, the spectral
/// warm-up and the whole reverb build-up.
constexpr int kWarmupBlocks = 400;

constexpr int kBlocksPerTrial = 500;

/// Arm (c) only: blocks rendered UNFROZEN before setFreeze(true).
///
/// This is not padding. The owned engine scales its input injection by
/// (1 - freezeRamp), so once the latch completes almost nothing new enters the
/// late network - a freeze entered at t = 0 would hold, and measure, an empty
/// loop. It would still pay the same arithmetic (there is no silence early-out
/// in the render path), so the ns/block figure would look plausible while
/// describing a state no player can reach.
constexpr int kPreFreezeBlocks = 400;

/// Finite check WITHOUT std::isnan (FR-071): the macOS leg builds with
/// -ffast-math, which folds the classifiers. Inspect the IEEE-754 exponent
/// field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// =============================================================================
// Excitation and buffers
// =============================================================================

/// One block of input plus its output scratch. Separate arrays: the perf lane
/// never relies on in-place support.
struct Buffers {
    std::array<float, kBlockSize> inLeft{};
    std::array<float, kBlockSize> inRight{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};
};

/// Deterministic decorrelated stereo noise at ~-12 dBFS, built once and replayed
/// every block.
///
/// Sustained broadband excitation, not an impulse: SC-009 measures the cost of a
/// space that is BEING DRIVEN. It also keeps the recirculating state well above
/// the denormal floor. Xorshift32 rather than <random> so the sequence is
/// identical on every toolchain (std::uniform_real_distribution is not
/// portable).
void fillExcitation(Buffers& buf) noexcept
{
    std::uint32_t state = 0x9E3779B9u;
    const auto next = [&state]() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // [-0.25, 0.25): 24 mantissa bits scaled, no division by a magic float.
        return (static_cast<float>(state >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.5f;
    };
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        buf.inLeft[i] = next();
        buf.inRight[i] = next();
    }
}

// =============================================================================
// The four arms
// =============================================================================

/// Everything that distinguishes one SC-009 arm from another.
struct ArmSpec {
    CavernVerb::PrepareConfig prepare{};
    bool maxControls = false;  ///< arm (b): every control driven to its extreme
    bool freeze = false;       ///< arm (c)
    bool damperOff = false;    ///< arm (d): setDamperDepth(0)
};

/// prepare() + the control history that defines the arm.
///
/// Arm (a) sets NOTHING: FR-066's pinned default table IS the constructed state
/// (cavern_verb.h:1330-1343), so overriding any control here would measure a
/// configuration nobody ships.
void buildEngine(CavernVerb& r, const ArmSpec& s) noexcept
{
    r.prepare(kSr48, s.prepare);

    if (s.maxControls) {
        r.setSize(1.0f);
        r.setDamperDepth(1.0f);
        r.setDamperRate(1.0f);
        r.setFog(1.0f);
        r.setEarlyLevel(1.0f);
        r.setEarlySend(1.0f);
        // Clamped into [kEarlySizeMinMs, min(kEarlySizeMaxMs, maxEarlySeconds *
        // 1000)] (cavern_verb.h:636-648); this arm prepares at 0.60 s precisely
        // so the maximum is reachable.
        r.setEarlySizeMs(CavernVerb::kEarlySizeMaxMs);
        r.setBreath(1.0f);
    }
    if (s.damperOff) {
        r.setDamperDepth(0.0f);
    }
    // ArmSpec::freeze is deliberately NOT applied here: measureArm() has to
    // excite the network first (see kPreFreezeBlocks).
}

/// (a) FR-066's pinned defaults, spectral @1024, maxEarlySeconds = 0.30.
[[nodiscard]] ArmSpec specDefault() noexcept
{
    ArmSpec s{};
    s.prepare = CavernVerb::PrepareConfig{
        .numChannels = std::size_t{8},
        .maxBlockSamples = kBlockSize,
        .maxEarlySeconds = 0.30f,
        .maxDelaySeconds = 0.50f,
        .spectralDiffusionEnabled = true,
        .diffusionFftSize = std::size_t{1024},
        .seed = std::uint32_t{1},
    };
    return s;
}

/// (b) N = 16, every control at its extreme, spectral @4096,
/// maxEarlySeconds = 0.60.
[[nodiscard]] ArmSpec specWorst() noexcept
{
    ArmSpec s{};
    s.prepare = CavernVerb::PrepareConfig{
        .numChannels = std::size_t{16},
        .maxBlockSamples = kBlockSize,
        .maxEarlySeconds = 0.60f,
        .maxDelaySeconds = 0.50f,
        .spectralDiffusionEnabled = true,
        .diffusionFftSize = std::size_t{4096},
        .seed = std::uint32_t{1},
    };
    s.maxControls = true;
    return s;
}

/// (c) arm (a), frozen and settled.
[[nodiscard]] ArmSpec specFrozen() noexcept
{
    ArmSpec s = specDefault();
    s.freeze = true;
    return s;
}

/// (d) arm (a) at setDamperDepth(0).
[[nodiscard]] ArmSpec specDamperOff() noexcept
{
    ArmSpec s = specDefault();
    s.damperOff = true;
    return s;
}

// =============================================================================
// Measurement
// =============================================================================

struct Measurement {
    double nsPerBlock = 0.0;
    double sink = 0.0;
    bool finite = false;
    bool frozen = false;
    bool shimmerActive = true;  ///< must be FALSE everywhere: FR-010 never builds it
    std::size_t latencySamples = 0;
    std::size_t recoveries = 0;
    std::size_t earlyTapCount = 0;
    /// Lines whose PUBLISHED damper offset is non-zero at the end of the run.
    /// Arms (a)-(c) require > 0 (the dampers really are moving); arm (d)
    /// requires exactly 0 (cavern_verb.h:1057-1061 makes that exact, not a
    /// tolerance).
    std::size_t movingDampers = 0;
    /// The last tap's CURRENT, size-scaled delay. Proves setEarlySizeMs really
    /// landed in arm (b) rather than the arm silently running arm (a)'s ER size.
    float lastTapDelaySamples = 0.0f;
};

/// One prepared arm: the engine, its buffers and the sink that keeps the render
/// alive. The engine is heap-held because three arms live at once during the
/// interleaved trio and CavernVerb's fixed state is not small.
struct Arm {
    ArmSpec spec{};
    std::unique_ptr<CavernVerb> r;
    Buffers buf{};
    double sink = 0.0;

    /// Exactly one 512-sample block of work. Reading two samples per block is
    /// what stops the optimizer dead-coding the render away; a real consumer
    /// reads the whole buffer, so this is not artificial overhead.
    void renderBlock() noexcept
    {
        r->processStereoBlock(buf.inLeft.data(), buf.inRight.data(), buf.outLeft.data(),
                              buf.outRight.data(), kBlockSize);
        sink += static_cast<double>(buf.outLeft[0])
                + static_cast<double>(buf.outRight[kBlockSize - 1]);
    }
};

/// prepare(), the control history that defines the arm, arm (c)'s pre-freeze
/// fill, then the warm-up. After this the arm is in the steady state a trial
/// measures.
[[nodiscard]] Arm prepareArm(const ArmSpec& s)
{
    Arm a;
    a.spec = s;
    a.r = std::make_unique<CavernVerb>();
    buildEngine(*a.r, s);
    fillExcitation(a.buf);

    if (s.freeze) {
        // Fill the network with a real tail FIRST, then latch. The engine's
        // 50 ms freeze latch closes inside the first blocks of the warm-up
        // below, so the measurement starts long after isFrozen() is true -
        // which the REQUIRE on Measurement::frozen then confirms.
        for (int i = 0; i < kPreFreezeBlocks; ++i) {
            a.renderBlock();
        }
        a.r->setFreeze(true);
    }

    for (int i = 0; i < kWarmupBlocks; ++i) {
        a.renderBlock();
    }
    return a;
}

/// One trial: `blocks` blocks of one arm, as wall-clock nanoseconds per block.
[[nodiscard]] double trialNsPerBlock(Arm& a, int blocks)
{
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i) {
        a.renderBlock();
    }
    const auto end = std::chrono::steady_clock::now();
    const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
    return elapsedNs / static_cast<double>(blocks);
}

/// The figure plus everything that proves it describes the named configuration.
[[nodiscard]] Measurement snapshot(const Arm& a, double nsPerBlock)
{
    Measurement out{};
    out.nsPerBlock = nsPerBlock;
    out.sink = a.sink;
    out.finite = isFiniteValue(static_cast<float>(a.sink));
    out.frozen = a.r->isFrozen();
    out.shimmerActive = a.r->isShimmerActive();
    out.latencySamples = a.r->getLatencySamples();
    out.recoveries = a.r->getNonFiniteRecoveryCount();
    out.earlyTapCount = a.r->getEarlyTapCount();
    out.lastTapDelaySamples = a.r->getEarlyTapDelaySamples(CavernVerb::kEarlyTapCount - 1u);

    const std::size_t lines = a.spec.prepare.numChannels;
    for (std::size_t i = 0; i < lines; ++i) {
        const float off = a.r->getDamperOffsetOctaves(i);
        if (off != 0.0f) {
            ++out.movingDampers;
        }
    }
    return out;
}

/// One arm on its own: prepare, warm up, then best-of-25 x 500 blocks. Arm (b).
[[nodiscard]] Measurement measureArm(const ArmSpec& s)
{
    Arm a = prepareArm(s);
    double best = std::numeric_limits<double>::max();
    for (int trial = 0; trial < kTrials; ++trial) {
        best = std::min(best, trialNsPerBlock(a, kBlocksPerTrial));
    }
    return snapshot(a, best);
}

/// Arms (a), (c) and (d) together: all three prepared and warmed, then one
/// trial loop times 500 blocks of each in turn, 25 times, best per arm. The
/// three figures are taken under the same thermal state, which is what their
/// relative clauses need (see Trial shape). Returned in the order given.
[[nodiscard]] std::array<Measurement, 3> measureTrio(const ArmSpec& first, const ArmSpec& second,
                                                     const ArmSpec& third)
{
    std::array<Arm, 3> arms{prepareArm(first), prepareArm(second), prepareArm(third)};
    std::array<double, 3> best{};
    best.fill(std::numeric_limits<double>::max());

    for (int trial = 0; trial < kTrials; ++trial) {
        for (std::size_t k = 0; k < arms.size(); ++k) {
            best[k] = std::min(best[k], trialNsPerBlock(arms[k], kBlocksPerTrial));
        }
    }
    return {snapshot(arms[0], best[0]), snapshot(arms[1], best[1]), snapshot(arms[2], best[2])};
}

// =============================================================================
// Reporting
// =============================================================================

[[nodiscard]] std::string reportBlock(const char* armName, double measuredNs, double baselineNs)
{
    std::ostringstream os;
    os << "SC-009 " << armName << " - CavernVerb, ns per 512-sample block @ 48 kHz\n"
       << "  block budget    : " << kBlockBudgetNs << " ns\n"
       << "  reference (5 %) : " << kReferenceNs
       << " ns/block  (roadmap line 430, GLOBAL - does not multiply by polyphony)\n"
       << "  measured        : " << measuredNs << " ns/block  ("
       << ((measuredNs / kBlockBudgetNs) * 100.0) << " % of one core)\n"
       << "  checked-in base : " << baselineNs << " ns/block  (ceil(measured x 1.05), "
                                                  "BASELINE PROVENANCE; gate: x"
       << kRegressionFactor << " = " << (baselineNs * kRegressionFactor) << " ns/block)\n"
       << "  headroom vs ref : " << ((measuredNs / kReferenceNs) * 100.0) << " % of the reference\n"
       << "  vs cap          : " << ((measuredNs / kMaxAdmissibleNs) * 100.0)
       << " % of kMaxAdmissibleNs (" << kMaxAdmissibleNs << " ns)";
    return os.str();
}

}  // namespace

// =============================================================================
// SC-009: CPU <= 5 % of one core, global
// =============================================================================
// Four arms, gated against four checked-in constants. See BASELINE PROVENANCE
// above for how the constants are pinned and what to do when one is exceeded
// (work L-1 ... L-4, then L-5; never raise the baseline).
//
// EVERY MEASUREMENT AND EVERY REPORT COMES BEFORE ANY GATE. A REQUIRE aborts the
// case, so gating each arm where it is measured would let the first over-budget
// arm hide the other three - and the compliance ledger needs all four numbers,
// including the ones that failed.

TEST_CASE("CavernVerb_CpuBudget", "[.perf][cavern]")
{
    // -------------------------------------------------------------------------
    // (a), (c), (d): ONE interleaved measurement (see Trial shape). (a) is the
    // line item the other two are read against, so the three are timed under
    // the same thermal state.
    // -------------------------------------------------------------------------
    const std::array<Measurement, 3> trio =
        measureTrio(specDefault(), specFrozen(), specDamperOff());
    const Measurement& dflt = trio[0];
    const Measurement& frozen = trio[1];
    const Measurement& damperOff = trio[2];

    // -------------------------------------------------------------------------
    // (a) FR-066 defaults, spectral @1024, maxEarlySeconds = 0.30
    // -------------------------------------------------------------------------
    // Guards against the whole loop being optimized out (a zero-cost "pass") and
    // against a figure measured on an engine that had already blown up.
    REQUIRE(dflt.finite);
    REQUIRE(dflt.recoveries == 0u);
    // Preconditions, not perf assertions: a figure for a configuration that is
    // not the one named would be the wrong number wearing the right label.
    REQUIRE_FALSE(dflt.shimmerActive);  // FR-010: shimmer is never constructed
    REQUIRE(dflt.latencySamples == 1024u);
    REQUIRE(dflt.earlyTapCount == 12u);
    REQUIRE(dflt.lastTapDelaySamples > 0.0f);
    // The dampers really are moving, so the engine's exp2 + pow branch is the
    // one being timed - not the `off == 0.0f` assignment shortcut.
    REQUIRE(dflt.movingDampers > 0u);
    WARN(reportBlock("(a) default - FR-066 table, N=8, spectral @1024, maxEarly 0.30",
                     dflt.nsPerBlock, kBaselineDefaultNsPerBlock));

    // -------------------------------------------------------------------------
    // (c) arm (a), frozen and settled (measured in the trio above)
    // -------------------------------------------------------------------------
    REQUIRE(frozen.finite);
    REQUIRE(frozen.recoveries == 0u);
    REQUIRE_FALSE(frozen.shimmerActive);
    REQUIRE(frozen.latencySamples == 1024u);
    // isFrozen() is true only once the 50 ms latch has COMPLETED. If it is false
    // the measurement caught a mid-latch engine, which is a different (and
    // partly lossy) configuration.
    REQUIRE(frozen.frozen);
    // FR-036: freeze suspends the APPLICATION of the offsets inside the engine,
    // never their GENERATION (cavern_verb.h:1107-1113), so the dampers are still
    // published and still moving here.
    REQUIRE(frozen.movingDampers > 0u);
    WARN(reportBlock("(c) frozen - arm (a) with setFreeze(true) settled", frozen.nsPerBlock,
                     kBaselineFrozenNsPerBlock));

    // -------------------------------------------------------------------------
    // (d) arm (a) at setDamperDepth(0) - the damper delta (measured in the trio)
    // -------------------------------------------------------------------------
    REQUIRE(damperOff.finite);
    REQUIRE(damperOff.recoveries == 0u);
    REQUIRE_FALSE(damperOff.shimmerActive);
    REQUIRE(damperOff.latencySamples == 1024u);
    // EXACTLY zero, by value and not by tolerance: at depth 0 every drift's
    // outputTarget() is exactly 0.0f, so every published offset is exactly 0.0f
    // (cavern_verb.h:1057-1061). This is what makes the difference below a
    // measurement of the damper path rather than of two noisy runs.
    REQUIRE(damperOff.movingDampers == 0u);
    WARN(reportBlock("(d) damper off - arm (a) with setDamperDepth(0)", damperOff.nsPerBlock,
                     kBaselineDamperOffNsPerBlock));

    // -------------------------------------------------------------------------
    // (b) N = 16, every control at its extreme, spectral @4096, maxEarly 0.60
    // -------------------------------------------------------------------------
    const Measurement worst = measureArm(specWorst());
    REQUIRE(worst.finite);
    REQUIRE(worst.recoveries == 0u);
    REQUIRE_FALSE(worst.shimmerActive);
    REQUIRE(worst.latencySamples == 4096u);
    REQUIRE(worst.earlyTapCount == 12u);
    REQUIRE(worst.movingDampers > 0u);
    // setEarlySizeMs(600) really landed: the last tap sits materially further
    // out than arm (a)'s 220 ms default ER size. Without this, an arm whose ER
    // size had been silently clamped back to the default would still report a
    // plausible figure under the "worst case" label.
    INFO("SC-009 (b): last ER tap at " << worst.lastTapDelaySamples << " samples, vs arm (a) "
                                       << dflt.lastTapDelaySamples);
    REQUIRE(worst.lastTapDelaySamples > dflt.lastTapDelaySamples * 2.0f);
    WARN(reportBlock("(b) worst case - N=16, all controls max, spectral @4096, maxEarly 0.60",
                     worst.nsPerBlock, kBaselineWorstNsPerBlock));

    // -------------------------------------------------------------------------
    // Cross-arm reporting (no gate - the gates are below)
    // -------------------------------------------------------------------------
    {
        std::ostringstream os;
        os << "SC-009 summary, ns/block @ 48 kHz (transcribe ALL FOUR into the compliance ledger "
              "verbatim - the ledger tallies measurements, not ceilings):\n"
           << "  (a) default    : " << dflt.nsPerBlock << "\n"
           << "  (b) worst case : " << worst.nsPerBlock << "\n"
           << "  (c) frozen     : " << frozen.nsPerBlock << "\n"
           << "  (d) damper off : " << damperOff.nsPerBlock << "\n"
           << "  DAMPER DELTA (a) - (d) : " << (dflt.nsPerBlock - damperOff.nsPerBlock)
           << " ns/block  [the cost attributable to the moving dampers: the engine's exp2 + pow "
              "pair per line per control chunk, which levers L-1 and L-2 attack]\n"
           << "  worst headroom (b) - (a): " << (worst.nsPerBlock - dflt.nsPerBlock)
           << "  [N 8->16, spectral 1024->4096, ER size 220->600 ms, every control at max]\n"
           << "  freeze delta (c) - (a) : " << (frozen.nsPerBlock - dflt.nsPerBlock)
           << "  [must be <= 0 in the mean; see the gate below]\n"
           << "  reference vs shipped AetherReverb: its default arm is 114 595 ns/block and its "
              "worst arm 200 114 ns/block, BOTH shimmer/bloom-inclusive and the worst one "
              "4096-point (aether_reverb_perf_test.cpp:326, :330)";
        WARN(os.str());
    }

    // -------------------------------------------------------------------------
    // THE GATES
    // -------------------------------------------------------------------------
    INFO("SC-009 (a) default");
    REQUIRE(dflt.nsPerBlock <= kBaselineDefaultNsPerBlock * kRegressionFactor);

    INFO("SC-009 (b) worst case, N = 16");
    REQUIRE(worst.nsPerBlock <= kBaselineWorstNsPerBlock * kRegressionFactor);

    INFO("SC-009 (c) frozen");
    REQUIRE(frozen.nsPerBlock <= kBaselineFrozenNsPerBlock * kRegressionFactor);

    INFO("SC-009 (d) damper off");
    REQUIRE(damperOff.nsPerBlock <= kBaselineDamperOffNsPerBlock * kRegressionFactor);

    // -------------------------------------------------------------------------
    // (c)'s RELATIVE clause: freeze must not be MORE expensive than arm (a)
    // -------------------------------------------------------------------------
    // FR-034 skips the engine's geometry and decay/damping recompute while
    // frozen, so the frozen figure is structurally a subset of arm (a)'s work.
    // This is a MEASURED clause and not only the static_assert on the two
    // baselines, because both baselines could be over-generous and hide a freeze
    // that quietly recomputed the latched geometry every chunk.
    //
    // The tolerance is 10 % of (a), not 0: the two figures are two best-of-25
    // series on a hybrid CPU. They are timed INTERLEAVED (measureTrio) because,
    // timed separately, DATASET 1 measured them 16-20 % apart run to run with
    // no code change (BASELINE PROVENANCE). A freeze that recomputes what FR-034
    // says it skips costs far more than 10 %.
    INFO("SC-009 (c) relative clause: frozen " << frozen.nsPerBlock << " ns/block vs (a) "
                                               << dflt.nsPerBlock << " ns/block");
    REQUIRE(frozen.nsPerBlock <= dflt.nsPerBlock * 1.10);

    // -------------------------------------------------------------------------
    // (d)'s RELATIVE clause: depth 0 is a strict SUBSET of arm (a)'s work
    // -------------------------------------------------------------------------
    // The delta itself is REPORTED, never asserted to be any particular size
    // (spec.md:1161-1163 - "reported as a difference rather than asserted in the
    // abstract"). What IS asserted is its SIGN, within the same 10 % measurement
    // tolerance: every published offset is exactly zero here, so the engine
    // takes the `off == 0.0f` assignment branch on every line of every control
    // chunk instead of the exp2 + pow pair (aether_reverb.h:3278-3286). A
    // depth-0 arm measuring materially ABOVE arm (a) would mean that branch is
    // not the one being taken.
    INFO("SC-009 (d) relative clause: damper off " << damperOff.nsPerBlock << " ns/block vs (a) "
                                                   << dflt.nsPerBlock << " ns/block");
    REQUIRE(damperOff.nsPerBlock <= dflt.nsPerBlock * 1.10);
}
