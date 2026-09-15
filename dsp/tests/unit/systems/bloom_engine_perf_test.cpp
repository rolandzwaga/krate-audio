// ==============================================================================
// Layer 3: System Tests - BloomEngine CPU budget and stage-cost probe
// ==============================================================================
// Vorago Phase 7 (specs/vorago-phase7-harmonic-bloom): BloomEngine.
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase7-harmonic-bloom/spec.md   (FR-072, FR-073, SC-011)
//            specs/vorago-phase7-harmonic-bloom/plan.md   (S10.2, S12.1-S12.3)
//            specs/vorago-phase7-harmonic-bloom/tasks.md  (T001 creates this TU,
//                                                          T003 landed the FR-073
//                                                          stage probe against
//                                                          STAND-INS, T020 - this
//                                                          pass - re-points its
//                                                          four arms at the real
//                                                          engine and adds the
//                                                          SC-011 gate)
//
// SCOPE OF THIS TU (plan S10.2): SC-011 [.perf] + the FR-073 stage probe [.perf].
//
// Every case here is tagged [.perf] so the per-push CI filter
//   ~[performance]~[perf]~[benchmark]~[!benchmark]~[long] excludes it.
//   RUN IT ALONE: node tools/run-cpu-tests.js dsp_systems_tests - nothing else
//   executing, no build, no clang-tidy, no second suite, no parallel agent.
//
// This TU is DELIBERATELY NOT in the "-fno-fast-math -fno-finite-math-only"
//   block of dsp/tests/CMakeLists.txt: -fno-fast-math would change the figures
//   its baselines are pinned to.
//
// -----------------------------------------------------------------------------
// WHAT T020 CHANGED, AND WHY THE SHAPE OF THE ARMS MOVED
// -----------------------------------------------------------------------------
// T003 priced four STAND-INS (a raw 16-entry table walk, a 48-float store loop,
// a transcribed event body, a LinearRamp + Xorshift32 pair) because
// `bloom_engine.h` did not exist yet - that was the whole point of measuring
// before implementing (plan S15 step 1). The component now exists, so every arm
// below drives the REAL `BloomEngine` through its ONE public entry point,
// `processChunk()`.
//
//   *** THE ARMS ARE CONFIGURATIONS, NOT DIRECT CALLS, AND THAT IS FORCED. ***
//   `advanceChildren()`, `applyOutput()` and `runEvent()` are PRIVATE
//   (bloom_engine.h:1063, :1525, :1380). The header's only friend is
//   `detail::BloomEngineNonFiniteProbe` (bloom_engine.h:1652), which belongs to
//   `bloom_engine_nonfinite_test.cpp`, and T020's file list is THIS FILE ONLY -
//   the header is read-only here. So each stage is priced the way this repo's
//   other perf TUs price stages: as a CONFIGURATION of the real component that
//   isolates the term, plus a DIFFERENCE against the configuration one rung
//   below it (resonance_drift_network_perf_test.cpp's wander-off / all-dormant
//   arms are the same idiom). Every arm below states exactly what it contains.
//
//     (d) clock only          numChildSlots = 0 -> the FR-054 pass-through, never
//                             engaged. RAW figure: the clock draw, the depth-ramp
//                             step, advanceChildren() over an all-Idle table and
//                             applyOutput()'s `!engaged_` early return
//                             (bloom_engine.h:1527). This is exactly what a
//                             caller pays for having the component in the chain.
//     (b) owned-slot writes   ENGAGED with ZERO live children (a short-lifecycle
//                             child is spawned and drained; engaged_ is STICKY,
//                             Clarification Q8). RAW figure minus (d) = the
//                             applyOutput() write phase alone.
//     (a) child bookkeeping   ENGAGED with all 16 children live, spawn clock OFF.
//                             RAW figure minus (b)'s raw = advanceChildren() over
//                             16 live children PLUS applyOutput()'s live-child
//                             write loop. The two cannot be split through the
//                             public surface and the label says so.
//     (c) one full runEvent   (a)'s configuration with ONE event armed per
//                             control step. (RAW minus (a)'s raw) / 8 = ns per
//                             EVENT, reported raw and amortised.
//
// -----------------------------------------------------------------------------
// WHY ns/block AND NOT "% of one core"
// -----------------------------------------------------------------------------
// A percent-of-core figure is not reproducible across dev machines or CI runners
// (resonance_drift_network_perf_test.cpp:67-76). The measurement basis is
// NANOSECONDS PER 512-SAMPLE BLOCK AT 48 kHz (plan S12.1), the basis established
// by harmonic_cloud_perf_test.cpp and reused by continuous_body_perf_test.cpp,
// atmosphere_engine_perf_test.cpp and noise_organism_perf_test.cpp. One block
// period is 10 666 667 ns, so FR-072's 0.1 %-of-one-core ceiling is
// 10 667 ns/block. The percent figure is REPORTED, never asserted.
//
// TRIAL SHAPE (plan S12.1, tasks.md T003/T020): best-of-25 x 500 blocks after
// 400 warm-up blocks, the atmosphere_engine_perf_test.cpp idiom. Many short
// trials, because the dev machine is a hybrid part and the dominant noise source
// is a whole trial migrating onto an E-core. Affinity pinning was tried and
// REJECTED in both reference perf TUs.
//
//   *** STOP-AND-SURFACE RULE (FR-072, inherited verbatim from
//   *** resonance_drift_network_perf_test.cpp:57-64) - NON-NEGOTIABLE ***
//   NO IMPLEMENTING AGENT MAY lower kMaxChildren, raise kBudgetNs, raise a
//   baseline to make a REQUIRE pass, relax a threshold, shrink a workload, or
//   widen a tolerance. Reduce cost, never move the line. If the gated figure
//   misses, the build STOPS and surfaces the measured per-arm table for a USER
//   ruling - the route that amended Vorago Phase 2 (1 -> 1.75 %) and Phase 5
//   (1 -> 1.5 %). Two levers are pre-authorised (plan S12.2): call processChunk
//   at a larger numSamples (one 512-sample call is exactly equivalent to eight
//   64-sample calls, FR-006, asserted by SC-008 (b) - the lever arm below
//   MEASURES it), and narrow `capacity - parentCount`. The DIRTY-FLAG SKIP IS
//   UNAVAILABLE and must not be implemented: the arrays are the CALLER's, the
//   engine cannot know they are unchanged, and skipping the rewrite fails
//   SC-003's padding arms and SC-016's poisoned-gap arm.
//
// DIAGNOSTIC ANCHOR (tasks.md T020, plan S12.3): ~400 stores and ~16 polynomial
// evaluations per block - roughly two orders below the ceiling. IF THE MEASURED
// FIGURE IS ANYWHERE NEAR 10 667 ns, SOMETHING STRUCTURAL IS WRONG (most likely
// a per-chunk parent scan violating FR-013), and SC-005's
// getParentScanCount() == getSpawnEventCount() clause is the test that names it.
//
// HOUSE RULES, inherited: no std::isnan / std::isinf / std::isfinite anywhere
// (the macOS leg builds -ffast-math) - finiteness is detail::isFinite
// (core/db_utils.h:118); no bit-exact float goldens; designated initialisers for
// every brace-initialised aggregate; every literal carries its f/u suffix;
// processChunk() is [[nodiscard]] and every return is BOUND.
//
// FALSIFICATION (this is a test-only task, so it carries one instead of a
// fail-first step): lower kBaselineWorstCaseNs below the measured figure, or add
// a dummy 10 000-store loop inside the timed lambda, and
// BloomEngine_CpuBudget's two REQUIREs must go red. Restore afterwards.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bloom_engine.h>

#include <catch2/catch_all.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <sstream>
#include <string>

using namespace Krate::DSP;

namespace {

// =============================================================================
// Measurement basis (FR-072, plan S12.1)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;

/// The shared 64-sample control grid (FR-007).
constexpr std::size_t kControlChunk = BloomEngine::kControlChunkSamples;
static_assert(kControlChunk == 64u, "FR-007's grid is 64 samples");
static_assert(kBlockSize % kControlChunk == 0,
              "the worst-case call shape is whole control chunks");

/// Control steps inside one measured block, and the worst-case call count:
/// eight processChunk() calls of 64 samples each (plan S12.3's call shape).
constexpr std::size_t kStepsPerBlock = kBlockSize / kControlChunk;
static_assert(kStepsPerBlock == 8u, "plan S12.3 prices the worst case at 8 calls/block");

/// Wall-clock period of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockPeriodNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// FR-072's ceiling: 0.1 % of one core = 10 667 ns/block (plan S12.1). Written
/// as the literal the spec names and TIED to its derivation by the clause below
/// rather than computed from it.
///
/// *** NO AGENT MAY RAISE THIS. *** The stop-and-surface rule in the banner
/// governs every response to a miss, and the answer to a miss is a CALLER-SIDE
/// lever (plan S12.2), never a smaller kMaxChildren.
constexpr double kBudgetNs = 10667.0;
static_assert(kBudgetNs >= kBlockPeriodNs * 0.00099 && kBudgetNs <= kBlockPeriodNs * 0.00101,
              "FR-072's budget is 0.1 % of one 512-sample block at 48 kHz");

// Trial shape, pinned by plan S12.1 and tasks.md T020.
constexpr int kTrials = 25;
constexpr int kBlocksPerTrial = 500;
constexpr int kWarmupBlocks = 400;

/// Total blocks any one arm renders: the warm-up plus every trial. Used by the
/// lifetime clause below - the children must not retire mid-measurement, or the
/// workload changes under the clock.
constexpr double kBlocksPerArm =
    static_cast<double>(kWarmupBlocks) + (static_cast<double>(kTrials) *
                                          static_cast<double>(kBlocksPerTrial));

// =============================================================================
// The SC-011 worst-case configuration (plan S12.2/S12.3, tasks.md T020)
// =============================================================================
// numChildSlots = 16 with all 16 LIVE, parentCount = 48, K = 8,
// childrenPerEvent = 4, maximum spawn rate. Every value below is the
// CLASS-SCOPED constant wherever one exists - T003 spelled them locally because
// the class did not exist; that indirection is gone.

constexpr std::size_t kCapacity = BloomEngine::kMaxSlots;              ///< 64
constexpr std::size_t kNumChildSlots = BloomEngine::kMaxChildren;      ///< 16
constexpr std::size_t kStrongParents = BloomEngine::kMaxParents;       ///< K = 8
constexpr std::size_t kChildrenPerEvent = BloomEngine::kMaxChildrenPerEvent;  ///< 4
constexpr std::size_t kParentCount = 48;

static_assert(kCapacity == 64u && kNumChildSlots == 16u && kStrongParents == 8u &&
                  kChildrenPerEvent == 4u,
              "the SC-011 worst case, transcribed not re-derived");

/// reserveBase() for this configuration: the owned region is [48, 64).
constexpr std::size_t kReserveBase = kCapacity - kNumChildSlots;
static_assert(kReserveBase == kParentCount,
              "IN THE SC-011 WORST CASE THE FR-051 GAP IS EMPTY: pc == reserveBase, so "
              "applyOutput's first loop writes nothing and plan S12.3's "
              "(capacity - parentCount) term is already inside its 2 * numChildSlots "
              "term. The real per-call store count is 2 * 16 = 32, not 48 - see the "
              "probe's report");

/// Stores the engaged write phase actually performs per call, and per block at
/// the worst-case call shape. Plan S3.4's ceiling is 1 152 stores/block.
constexpr std::size_t kStoresPerCall = 2u * kNumChildSlots;
static_assert(kStoresPerCall == 32u, "two floats (ratio, amplitude) per owned slot");
static_assert(kStoresPerCall * kStepsPerBlock <= 1152u,
              "plan S3.4's per-block store ceiling is not exceeded by the priced shape");

/// FR-040's maximum spawn rate, and the amortisation it implies for arm (c):
///
///   p(step) = kMaxSpawnRateHz * kControlChunkSamples / sampleRate
///           = 0.05 * 64 / 48000 = 6.6667e-5
///   mean interval = 1 / p = 15 000 CONTROL STEPS = 1 875 blocks at 8 steps/block.
///
/// The CONSERVATIVE amortisation (plan S12.2's 15 000 control steps) is the one
/// that enters the summed projection; tasks.md T003's arm table said "15 000
/// BLOCKS", 8x more generous, and is printed beside it. A probe that flatters
/// its cheapest arm is worth nothing.
constexpr double kEventIntervalSteps = kSr48 / (0.05 * static_cast<double>(kControlChunk));
constexpr double kEventIntervalBlocks = kEventIntervalSteps / static_cast<double>(kStepsPerBlock);
constexpr double kTasksTableIntervalBlocks = 15000.0;
static_assert(kEventIntervalSteps > 14999.0 && kEventIntervalSteps < 15001.0,
              "plan S12.2's 15 000 control steps at kMaxSpawnRateHz");
static_assert(kEventIntervalBlocks > 1874.0 && kEventIntervalBlocks < 1876.0,
              "1 875 blocks at 8 control steps per block");

/// Pinned so every arm draws the same children run to run (FR-005).
constexpr std::uint32_t kPerfSeed = 0xB1005EEDu;

// =============================================================================
// The SC-011 gate (tasks.md T020)
// =============================================================================

/// The precedent's regression bound (atmosphere_engine_perf_test.cpp:38,
/// resonance_drift_network_perf_test.cpp's kRegressionFactor).
constexpr double kRegressionFactor = 1.5;

/// The largest baseline the ceiling clause admits: 10 667 / 1.5 = 7 111.33,
/// rounded DOWN so the clause holds with a margin rather than on a tie. Printed
/// in the transcription block so a future transcriber can check a figure against
/// it WITHOUT recomputing the ceiling.
constexpr double kCeilingAdmittedNs = 7111.0;
static_assert(kCeilingAdmittedNs * kRegressionFactor <= kBudgetNs,
              "the admitted maximum must itself satisfy the ceiling clause");

/// THE ANTI-NO-OP FLOOR, and it is NOT kBudgetNs / 50.
///
/// The Phase-5 precedent's floor is budget/50 because its baseline sits at ~55 %
/// of its budget. THIS component is predicted at roughly TWO ORDERS below its
/// ceiling (plan S12.3), so budget/50 = 213 ns would be a floor ABOVE the
/// expected measurement - a booby trap that would reject a correct transcription.
/// The floor is derived from the WORKLOAD instead: one engaged block performs
/// 8 x 32 = 256 owned-slot float stores, 8 x 16 = 128 double-precision
/// smoothstep evaluations each carrying a division (bloom_engine.h:1063-1088),
/// 8 clock draws and 8 depth-ramp steps. No supported machine executes that in
/// under 100 ns; a pass-through, unprepared or disengaged run measures an order
/// below it (arm (d) is exactly that run, and the probe prints it for
/// comparison). A baseline under this floor was recorded from a run that did
/// nothing.
constexpr double kNoOpFloorNs = 100.0;
static_assert(kNoOpFloorNs > 0.0 && kNoOpFloorNs < kBudgetNs,
              "the floor is a floor, not a second ceiling");

// -----------------------------------------------------------------------------
// The SC-011 baseline.
//
// *** PROVISIONAL: A PROJECTION, NOT A MEASUREMENT. *** (The Phase-5 precedent
// shipped its three baselines the same way - resonance_drift_network_perf_test
// .cpp's "PROVISIONAL: PROJECTIONS, NOT MEASUREMENTS" block - and transcribed
// measured figures over them afterwards.)
//
// Derivation, from plan S12.3's arithmetic and the shapes above:
//
//     applyOutput, 8 calls x (32 stores + a 16-entry table walk)     ~  300 ns
//   + advanceChildren, 8 steps x 16 live children, each one double
//     division + a smoothstep + a float narrowing                    ~  640 ns
//   + the clock, 8 x (Xorshift32::nextUnipolar + a settled
//     LinearRamp::process early-out)                                 ~   40 ns
//   + events at kMaxSpawnRateHz, amortised over 1 875 blocks         ~    1 ns
//   = ~ 1 000 ns/block, ~9 % of FR-072's ceiling - which is plan S12.3's
//     "roughly two orders below the ceiling", restated in nanoseconds.
//
// The figure below carries ~2.5x headroom over that projection, because a
// projection that has never been run is not a tight bound and a spuriously red
// perf gate teaches an agent to move lines. It STILL satisfies both clauses with
// margin (2 500 x 1.5 = 3 750 <= 10 667).
//
// TRANSCRIBING A MEASURED FIGURE OVER A PROJECTION IS ALWAYS CORRECT, and
// BloomEngine_CpuBudget prints a copy-pasteable line for exactly that. RAISING
// one so a REQUIRE passes is FORBIDDEN. A measured figure that cannot satisfy
// both clauses below is the stop-and-surface case, never a licence to weaken a
// clause.
//
// Take the transcription from a DISTRIBUTION, not one sample: this repo has
// measured a 6.4 % run-to-run spread and ~14 % session drift on unchanged code.
// Use the LOWEST of several isolated runs, so the clause stays as tight as the
// data allows.
// -----------------------------------------------------------------------------

/// The SC-011 worst case at the 8 x 64-sample call shape, ns per 512-sample
/// block at 48 kHz. PROVISIONAL - see the block above.
constexpr double kBaselineWorstCaseNs = 2500.0;

// The two compile-time clauses. They are EVALUATED ON EVERY CI LEG even though
// this case is [.perf]-hidden, which is the whole reason FR-072's absolute
// ceiling lives at compile time as well as in a REQUIRE.
static_assert(kBaselineWorstCaseNs * kRegressionFactor <= kBudgetNs,
              "SC-011's baseline exceeds FR-072's 0.1 %-of-one-core ceiling");
static_assert(kBaselineWorstCaseNs >= kNoOpFloorNs,
              "SC-011's baseline looks like a no-op run");

// =============================================================================
// Child lifetimes: the workload must be IDENTICAL on every measured block
// =============================================================================
// Every live-child arm latches the LONGEST configurable fade-in
// (kMaxFadeInSeconds = 300 s), so all 16 children stay in Phase::FadeIn - the
// most expensive branch of advanceChildren(), one division plus one smoothstep -
// for the whole measurement, and NONE of them retires under the clock. At
// 48 kHz the control rate is 750 steps/s, so the fade-in is 225 000 steps while
// one arm spends 12 900 blocks x 8 = 103 200 steps.
constexpr double kControlRateHz = kSr48 / static_cast<double>(kControlChunk);
static_assert(static_cast<double>(BloomEngine::kMaxFadeInSeconds) * kControlRateHz >
                  kBlocksPerArm * static_cast<double>(kStepsPerBlock) * 2.0,
              "the fade-in must outlast one whole arm with a factor-of-two margin, or "
              "children retire mid-measurement and the workload changes under the clock");

/// Short-lifecycle settings for arm (b)'s drain: 1 s fade-in, no hold, 1 s
/// fade-out = 1 500 control steps at 48 kHz.
constexpr std::size_t kDrainSteps = 4000;
static_assert(static_cast<double>(kDrainSteps) >
                  2.0 * (static_cast<double>(BloomEngine::kMinFadeInSeconds) +
                         static_cast<double>(BloomEngine::kMinFadeOutSeconds)) *
                      kControlRateHz,
              "the drain window must be at least twice the shortest lifecycle");

/// Events allowed while filling the owned region. One event offers at most
/// kMaxChildrenPerEvent children, so 4 perfect events would do it; the pool is
/// 16 octave/fifth candidates (see kParentRatios) and the draws are random, so
/// the bound is a coupon-collector margin, not an expectation.
constexpr std::size_t kMaxFillEvents = 512;

// =============================================================================
// The anti-elision sink
// =============================================================================
// A volatile STORE is an observable side effect, so every computation feeding it
// must survive. Each arm pushes processChunk()'s (bound) return through it once
// per invocation.

volatile double gProbeSink = 0.0;

// =============================================================================
// Best-of-N driver (resonance_drift_network_perf_test.cpp:286-308, the idiom)
// =============================================================================

/// Pinned warm-up, then best-of-`kTrials` x `kBlocksPerTrial`; returns the
/// winning trial's ns per invocation of `runBlock`, i.e. ns per 512-sample block.
///
/// `runBlock` is taken by const reference, not by forwarding reference: it is
/// INVOKED, many times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] double bestTrialNs(const BlockFn& runBlock)
{
    for (int i = 0; i < kWarmupBlocks; ++i) {
        runBlock();
    }

    double best = -1.0;
    for (int trial = 0; trial < kTrials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kBlocksPerTrial; ++i) {
            runBlock();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        const double nsPerCall = elapsedNs / static_cast<double>(kBlocksPerTrial);
        if (best < 0.0 || nsPerCall < best) {
            best = nsPerCall;
        }
    }
    return best;
}

// =============================================================================
// The parent spectrum every arm drives
// =============================================================================
//
// EIGHT eligible parents in [0, 8) - exactly K, so the strongest-K scan retains
// all of them - and the cloud's own padding form (ratio = i + 1, amplitude = 0,
// harmonic_cloud.h:825-826) on [8, 48). A padded slot is INELIGIBLE as a parent
// (its amplitude is below kSilentParentAmplitude) but its RATIO is still in the
// FR-022 occupancy set, exactly as it would be in a real caller's array.
//
// THE RATIOS ARE CHOSEN SO THE OWNED REGION CAN ACTUALLY FILL, and that is
// load-bearing: the arms measure "all 16 children live", so a spectrum on which
// candidates collide would measure a half-full table instead. Verified this
// session over the occupancy set {the 8 parents} u {9, 10, ... 48}: the sixteen
// octave (x2) and fifth (x1.5) candidates span 1.5825 - 2.716, sit >= 264 cents
// from every occupied ratio and >= 60.9 cents from each other, against FR-022's
// 24-cent rule. The pool is therefore 16 positions for 16 slots, plus whatever
// detuned neighbours land - so filling is certain given enough events, and
// fillLiveChildren() reports rather than assumes it.
constexpr std::array<float, kStrongParents> kParentRatios{1.055f, 1.094f, 1.134f, 1.175f,
                                                          1.219f, 1.263f, 1.310f, 1.358f};

/// Strictly descending, so the FR-010 selection order is unambiguous and the
/// FR-011 tie-break never has to fire.
constexpr std::array<float, kStrongParents> kParentAmps{1.00f, 0.94f, 0.88f, 0.82f,
                                                        0.76f, 0.70f, 0.64f, 0.58f};

void fillPerfParents(float* ratios, float* amplitudes) noexcept
{
    for (std::size_t i = 0; i < kCapacity; ++i) {
        ratios[i] = static_cast<float>(i + 1);
        amplitudes[i] = 0.0f;
    }
    for (std::size_t i = 0; i < kStrongParents; ++i) {
        ratios[i] = kParentRatios[i];
        amplitudes[i] = kParentAmps[i];
    }
}

[[nodiscard]] bool allFinite(const std::array<float, kCapacity>& ratios,
                             const std::array<float, kCapacity>& amplitudes) noexcept
{
    for (std::size_t i = 0; i < kCapacity; ++i) {
        // detail::isFinite, never std::isnan / std::isinf - the macOS leg builds
        // -ffast-math (core/db_utils.h:118).
        if (!detail::isFinite(ratios[i]) || !detail::isFinite(amplitudes[i])) {
            return false;
        }
    }
    return true;
}

// =============================================================================
// Fixtures
// =============================================================================

/// prepare() the SC-011 configuration with the FR-041 clock OFF, so an arm that
/// wants events gets them from triggerBloom() and an arm that does not gets
/// none. setSeed() before prepare() is safe: prepare()'s step (6) re-applies
/// seed_, which survives (FR-004).
void preparePerfEngine(BloomEngine& engine, std::size_t numChildSlots, float fadeInSec,
                       float holdSec, float fadeOutSec) noexcept
{
    engine.setSeed(kPerfSeed);
    engine.prepare(kSr48, BloomEngine::PrepareConfig{.capacity = kCapacity,
                                                     .numChildSlots = numChildSlots});
    engine.setSpawnRateHz(0.0f);
    engine.setParentCount(kStrongParents);
    engine.setChildrenPerEvent(kChildrenPerEvent);
    engine.setFadeInSeconds(fadeInSec);
    engine.setHoldSeconds(holdSec);
    engine.setFadeOutSeconds(fadeOutSec);
    engine.setHoldJitterFraction(0.0f);
}

/// Arm one event per control step until every owned slot holds a live child.
/// @return true when getLiveChildCount() reached kNumChildSlots.
[[nodiscard]] bool fillLiveChildren(BloomEngine& engine, float* ratios, float* amplitudes) noexcept
{
    for (std::size_t e = 0; e < kMaxFillEvents; ++e) {
        if (engine.getLiveChildCount() >= kNumChildSlots) {
            break;
        }
        engine.triggerBloom();
        const std::size_t returned =
            engine.processChunk(ratios, amplitudes, kParentCount, kControlChunk);
        gProbeSink = static_cast<double>(returned);
    }
    return engine.getLiveChildCount() == kNumChildSlots;
}

/// Spawn one short-lifecycle event and run until every child has retired.
/// engaged_ is STICKY (Clarification Q8), so what is left is an ENGAGED engine
/// with an empty table - applyOutput()'s write phase with no child loop body.
/// @return true when the engine is engaged and no child is live.
[[nodiscard]] bool engageThenDrain(BloomEngine& engine, float* ratios, float* amplitudes) noexcept
{
    engine.triggerBloom();
    gProbeSink =
        static_cast<double>(engine.processChunk(ratios, amplitudes, kParentCount, kControlChunk));
    for (std::size_t s = 0; s < kDrainSteps && engine.getLiveChildCount() > 0; ++s) {
        gProbeSink = static_cast<double>(
            engine.processChunk(ratios, amplitudes, kParentCount, kControlChunk));
    }
    return engine.isEngaged() && engine.getLiveChildCount() == 0;
}

// =============================================================================
// One arm's result: the figure plus everything needed to attribute it
// =============================================================================
// No REQUIRE fires inside a measurement helper - a helper is `noexcept`-adjacent
// and a throwing assertion inside one would terminate rather than report. Every
// observation travels back here and is asserted in the TEST_CASE.

struct ArmResult {
    double nsPerBlock = 0.0;
    bool setupOk = false;        ///< the arm's precondition held BEFORE timing
    bool engagedBefore = false;  ///< FR-051 state before timing
    std::size_t liveBefore = 0;
    std::size_t liveAfter = 0;  ///< must equal liveBefore: no child retired mid-run
    bool stateFinite = false;
    bool outputFinite = false;
    std::uint64_t spawnEvents = 0;
    std::uint64_t parentScans = 0;
    std::size_t returnedCount = 0;
};

/// The engaged, child-bearing arms: (a), (c), the SC-011 gate and the plan
/// S12.2 lever measurement.
///
/// @param callSamples   Samples per processChunk() call. 64 is the worst case
///                      (eight calls per block); 512 is lever 1 (one call).
/// @param spawnRateHz   FR-040 clock rate during the timed region.
/// @param driveEvents   Arm one event per CALL via triggerBloom() (arm (c)).
[[nodiscard]] ArmResult measureEngagedArm(std::size_t callSamples, float spawnRateHz,
                                          bool driveEvents)
{
    BloomEngine engine;
    preparePerfEngine(engine, kNumChildSlots, BloomEngine::kMaxFadeInSeconds,
                      BloomEngine::kMaxHoldSeconds, BloomEngine::kMaxFadeOutSeconds);

    std::array<float, kCapacity> ratios{};
    std::array<float, kCapacity> amplitudes{};
    fillPerfParents(ratios.data(), amplitudes.data());

    ArmResult out;
    out.setupOk = fillLiveChildren(engine, ratios.data(), amplitudes.data());
    engine.setSpawnRateHz(spawnRateHz);

    out.setupOk = out.setupOk && engine.isPrepared() && engine.isEngaged() &&
                  engine.capacity() == kCapacity && engine.numChildSlots() == kNumChildSlots &&
                  engine.reserveBase() == kReserveBase &&
                  engine.getParentCount() == kStrongParents &&
                  engine.getChildrenPerEvent() == kChildrenPerEvent &&
                  engine.getSpawnRateHz() == spawnRateHz &&
                  engine.getSmoothedDepth() == 1.0f;
    out.engagedBefore = engine.isEngaged();
    out.liveBefore = engine.getLiveChildCount();

    const std::size_t callsPerBlock = kBlockSize / callSamples;
    out.nsPerBlock = bestTrialNs([&]() noexcept {
        std::size_t returned = 0;
        for (std::size_t c = 0; c < callsPerBlock; ++c) {
            if (driveEvents) {
                engine.triggerBloom();
            }
            returned = engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                           callSamples);
        }
        gProbeSink = static_cast<double>(returned);
    });

    out.liveAfter = engine.getLiveChildCount();
    out.stateFinite = engine.stateFinite();
    out.outputFinite = allFinite(ratios, amplitudes);
    out.spawnEvents = engine.getSpawnEventCount();
    out.parentScans = engine.getParentScanCount();
    out.returnedCount =
        engine.processChunk(ratios.data(), amplitudes.data(), kParentCount, kControlChunk);
    return out;
}

/// Arm (b)'s raw figure: ENGAGED, table EMPTY, clock off. applyOutput()'s write
/// phase with an all-Idle child loop.
[[nodiscard]] ArmResult measureEngagedEmptyArm()
{
    BloomEngine engine;
    preparePerfEngine(engine, kNumChildSlots, BloomEngine::kMinFadeInSeconds, 0.0f,
                      BloomEngine::kMinFadeOutSeconds);

    std::array<float, kCapacity> ratios{};
    std::array<float, kCapacity> amplitudes{};
    fillPerfParents(ratios.data(), amplitudes.data());

    ArmResult out;
    out.setupOk = engageThenDrain(engine, ratios.data(), amplitudes.data()) &&
                  engine.numChildSlots() == kNumChildSlots;
    out.engagedBefore = engine.isEngaged();
    out.liveBefore = engine.getLiveChildCount();

    out.nsPerBlock = bestTrialNs([&]() noexcept {
        std::size_t returned = 0;
        for (std::size_t c = 0; c < kStepsPerBlock; ++c) {
            returned = engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                           kControlChunk);
        }
        gProbeSink = static_cast<double>(returned);
    });

    out.liveAfter = engine.getLiveChildCount();
    out.stateFinite = engine.stateFinite();
    out.outputFinite = allFinite(ratios, amplitudes);
    out.spawnEvents = engine.getSpawnEventCount();
    out.parentScans = engine.getParentScanCount();
    out.returnedCount =
        engine.processChunk(ratios.data(), amplitudes.data(), kParentCount, kControlChunk);
    return out;
}

/// Arm (d): numChildSlots = 0, the FR-054 pass-through. Never engaged, so
/// applyOutput() returns at bloom_engine.h:1527 and the figure is the clock, the
/// ramp step and the all-Idle advanceChildren() walk - the floor a caller pays
/// for having the component in the chain at all.
[[nodiscard]] ArmResult measureDisengagedClockArm()
{
    BloomEngine engine;
    preparePerfEngine(engine, 0u, BloomEngine::kMaxFadeInSeconds, BloomEngine::kMaxHoldSeconds,
                      BloomEngine::kMaxFadeOutSeconds);

    std::array<float, kCapacity> ratios{};
    std::array<float, kCapacity> amplitudes{};
    fillPerfParents(ratios.data(), amplitudes.data());

    ArmResult out;
    out.setupOk = engine.isPrepared() && engine.numChildSlots() == 0u && !engine.isEngaged();
    out.engagedBefore = engine.isEngaged();
    out.liveBefore = engine.getLiveChildCount();

    out.nsPerBlock = bestTrialNs([&]() noexcept {
        std::size_t returned = 0;
        for (std::size_t c = 0; c < kStepsPerBlock; ++c) {
            returned = engine.processChunk(ratios.data(), amplitudes.data(), kParentCount,
                                           kControlChunk);
        }
        gProbeSink = static_cast<double>(returned);
    });

    out.liveAfter = engine.getLiveChildCount();
    out.stateFinite = engine.stateFinite();
    out.outputFinite = allFinite(ratios, amplitudes);
    out.spawnEvents = engine.getSpawnEventCount();
    out.parentScans = engine.getParentScanCount();
    out.returnedCount =
        engine.processChunk(ratios.data(), amplitudes.data(), kParentCount, kControlChunk);
    return out;
}

// =============================================================================
// Reporting
// =============================================================================

[[nodiscard]] std::string row(const std::string& label, double nsPerBlock)
{
    std::ostringstream os;
    os << std::left << std::setw(54) << label << std::right << std::fixed << std::setprecision(1)
       << std::setw(12) << nsPerBlock << " ns/block   " << std::setprecision(4) << std::setw(9)
       << (100.0 * nsPerBlock / kBlockPeriodNs) << " % of one core";
    return os.str();
}

[[nodiscard]] std::string eventRow(const std::string& label, double nsPerEvent)
{
    std::ostringstream os;
    os << std::left << std::setw(54) << label << std::right << std::fixed << std::setprecision(1)
       << std::setw(12) << nsPerEvent << " ns/EVENT";
    return os.str();
}

/// Everything an arm must satisfy before its figure means anything.
[[nodiscard]] bool armIsSound(const ArmResult& arm)
{
    return arm.setupOk && arm.stateFinite && arm.outputFinite && arm.liveAfter == arm.liveBefore &&
           detail::isFinite(arm.nsPerBlock) && arm.nsPerBlock > 0.0;
}

}  // namespace

// =============================================================================
// T020 - SC-011: the gated CPU budget (FR-072)
// =============================================================================

TEST_CASE("BloomEngine_CpuBudget", "[bloom_engine][.perf]")
{
    // The SC-011 worst case, at the worst-case CALL SHAPE: eight 64-sample
    // processChunk() calls per 512-sample block, all 16 children live,
    // parentCount = 48, K = 8, childrenPerEvent = 4, MAXIMUM spawn rate.
    const ArmResult worst =
        measureEngagedArm(kControlChunk, BloomEngine::kMaxSpawnRateHz, /*driveEvents=*/false);

    // Plan S12.2 lever 1, MEASURED rather than assumed: the same configuration
    // driven with ONE 512-sample call per block. FR-006 / SC-008 (b) make the
    // two exactly equivalent in OUTPUT; this is what they cost.
    const ArmResult lever =
        measureEngagedArm(kBlockSize, BloomEngine::kMaxSpawnRateHz, /*driveEvents=*/false);

    // ---- preconditions: a cheap figure must not come from a broken fixture ---
    CAPTURE(worst.setupOk, worst.liveBefore, worst.liveAfter, worst.engagedBefore);
    REQUIRE(worst.setupOk);
    REQUIRE(worst.engagedBefore);
    REQUIRE(worst.liveBefore == kNumChildSlots);
    REQUIRE(worst.liveAfter == kNumChildSlots);  // no child retired under the clock
    REQUIRE(worst.stateFinite);
    REQUIRE(worst.outputFinite);
    REQUIRE(worst.returnedCount == kCapacity);  // FR-051: an engaged engine returns capacity()
    // FR-013 / SC-005: one parent scan per spawn event, never per chunk. If this
    // fails, the figure below is measuring a structural defect (plan S12.3).
    REQUIRE(worst.parentScans == worst.spawnEvents);

    CAPTURE(lever.setupOk, lever.liveBefore, lever.liveAfter);
    REQUIRE(lever.setupOk);
    REQUIRE(lever.liveAfter == kNumChildSlots);
    REQUIRE(lever.stateFinite);
    REQUIRE(lever.outputFinite);
    REQUIRE(lever.parentScans == lever.spawnEvents);

    REQUIRE(detail::isFinite(worst.nsPerBlock));
    REQUIRE(worst.nsPerBlock > 0.0);
    REQUIRE(detail::isFinite(lever.nsPerBlock));
    REQUIRE(lever.nsPerBlock > 0.0);

    const double sinkValue = gProbeSink;
    REQUIRE(detail::isFinite(sinkValue));

    // ---- the report, BEFORE the gate, so a failing run still prints it -------
    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  BloomEngine SC-011 CPU BUDGET (FR-072) - tasks.md T020\n"
       << "  48 kHz, 512-sample blocks, best-of-" << kTrials << " x " << kBlocksPerTrial
       << " blocks after " << kWarmupBlocks << " warm-up\n"
       << "  Worst case: numChildSlots = " << kNumChildSlots << " ALL LIVE, parentCount = "
       << kParentCount << ", K = " << kStrongParents << ",\n"
       << "  childrenPerEvent = " << kChildrenPerEvent << ", spawn rate = "
       << BloomEngine::kMaxSpawnRateHz << " Hz (maximum).\n"
       << "  RUN IN ISOLATION. A figure taken beside a build or another suite is not\n"
       << "  evidence.\n"
       << "=================================================================================\n"
       << row("  WORST CASE  (8 x 64-sample processChunk calls)", worst.nsPerBlock) << "\n"
       << row("  FR-072 BUDGET (0.1 % of one core)", kBudgetNs) << "\n"
       << row("  baseline x " + std::to_string(kRegressionFactor).substr(0, 3),
              kBaselineWorstCaseNs * kRegressionFactor)
       << "\n"
       << "    worst case as a fraction of the budget: " << std::fixed << std::setprecision(2)
       << (100.0 * worst.nsPerBlock / kBudgetNs) << " %\n"
       << "---------------------------------------------------------------------------------\n"
       << row("  PLAN S12.2 LEVER 1 (1 x 512-sample call)", lever.nsPerBlock) << "\n"
       << "      Output-identical by FR-006 / SC-008 (b). Saving against the worst case: "
       << std::fixed << std::setprecision(1)
       << (100.0 * (worst.nsPerBlock - lever.nsPerBlock) / worst.nsPerBlock) << " %.\n"
       << "      This is the FIRST lever if a future change misses the ceiling, and it is\n"
       << "      CALLER-SIDE: Phase 10's voice chooses it, and it costs this component\n"
       << "      nothing. The second is a narrower capacity - parentCount. The DIRTY-FLAG\n"
       << "      SKIP IS UNAVAILABLE (plan S12.2, normative) and must not be implemented.\n"
       << "=================================================================================\n"
       << "  BASELINE TRANSCRIPTION (copy-paste over the PROVISIONAL projection)\n"
       << "=================================================================================\n"
       << "    constexpr double kBaselineWorstCaseNs = " << std::fixed << std::setprecision(1)
       << worst.nsPerBlock << ";\n"
       << "    A transcribed baseline must satisfy BOTH compile-time clauses: <= "
       << std::setprecision(1) << kCeilingAdmittedNs << " (ceiling)\n"
       << "    and >= " << kNoOpFloorNs << " (anti-no-op floor). One that cannot is the\n"
       << "    STOP-AND-SURFACE case, never a licence to weaken a clause.\n"
       << "    Take it from the LOWEST of several ISOLATED runs - this repo has measured a\n"
       << "    6.4 % run-to-run spread and ~14 % session drift on unchanged code.\n";

    if (worst.nsPerBlock > kBudgetNs) {
        os << "  *** OVER BUDGET BY " << std::fixed << std::setprecision(1)
           << (worst.nsPerBlock - kBudgetNs) << " ns (" << std::setprecision(2)
           << (worst.nsPerBlock / kBudgetNs) << "x the ceiling).\n"
           << "  *** >>> STOP AND SURFACE TO THE USER with this table and the per-arm\n"
           << "  *** table from BloomEngine_StageCostProbe beside it. <<<\n"
           << "  *** Plan S12.3 predicts ~two orders of headroom, so a figure anywhere\n"
           << "  *** near the ceiling means something STRUCTURAL is wrong - most likely a\n"
           << "  *** per-chunk parent scan violating FR-013, which the\n"
           << "  *** parentScans == spawnEvents REQUIRE above is the test that names.\n";
    } else {
        os << "  WITHIN BUDGET: " << std::fixed << std::setprecision(1)
           << (kBudgetNs - worst.nsPerBlock) << " ns of headroom ("
           << std::setprecision(2) << (100.0 * worst.nsPerBlock / kBudgetNs)
           << " % of the ceiling).\n";
    }

    os << "  *** NO AGENT MAY lower kMaxChildren, raise kBudgetNs, raise a baseline to\n"
       << "  *** make a REQUIRE pass, relax a threshold, shrink a workload, or widen a\n"
       << "  *** tolerance. Reduce cost, never move the line\n"
       << "  *** (resonance_drift_network_perf_test.cpp:57-64, inherited verbatim).\n"
       << "=================================================================================\n";

    WARN(os.str());

    // ---- THE GATE -----------------------------------------------------------
    // Two clauses, both absolute. The budget clause is SC-011's line; the
    // baseline clause is the regression bound, and its compile-time twins
    // (kBaselineWorstCaseNs's two static_asserts) tie the pair to FR-072 on
    // every CI leg even though this case never runs there.
    CAPTURE(worst.nsPerBlock, kBaselineWorstCaseNs, kBudgetNs);
    REQUIRE(worst.nsPerBlock <= kBudgetNs);
    REQUIRE(worst.nsPerBlock <= kBaselineWorstCaseNs * kRegressionFactor);

    // The percent-of-core figure is REPORTED, never asserted (plan S12.1).
}

// =============================================================================
// T020 - the FR-073 stage-cost probe, RE-POINTED AT THE REAL ENGINE
// =============================================================================
// A PROBE, NOT A GATE. It REQUIREs only that every RAW figure is finite and
// strictly positive and that every arm's fixture held - a zero, a NaN or a
// half-filled table means the measurement is broken, which is the one thing that
// would make the table lie. No arm is asserted against any threshold, because
// the response to a miss is a DECISION taken from measured numbers.
// =============================================================================

TEST_CASE("BloomEngine_StageCostProbe", "[bloom_engine][.perf]")
{
    const ArmResult clockOnly = measureDisengagedClockArm();
    const ArmResult engagedEmpty = measureEngagedEmptyArm();
    const ArmResult engagedLive =
        measureEngagedArm(kControlChunk, 0.0f, /*driveEvents=*/false);
    const ArmResult eventDriven = measureEngagedArm(kControlChunk, 0.0f, /*driveEvents=*/true);

    // ---- fixtures first: an arm whose setup failed is not a measurement ------
    CAPTURE(clockOnly.setupOk, engagedEmpty.setupOk, engagedLive.setupOk, eventDriven.setupOk);
    REQUIRE(armIsSound(clockOnly));
    REQUIRE(armIsSound(engagedEmpty));
    REQUIRE(armIsSound(engagedLive));
    REQUIRE(armIsSound(eventDriven));

    REQUIRE_FALSE(clockOnly.engagedBefore);  // FR-054 pass-through
    REQUIRE(engagedEmpty.engagedBefore);     // engaged_ is STICKY
    REQUIRE(engagedEmpty.liveBefore == 0u);  // ... with an empty table
    REQUIRE(engagedLive.liveBefore == kNumChildSlots);
    REQUIRE(eventDriven.liveBefore == kNumChildSlots);

    // Arm (c) really did run one event per call: 8 per block over every warm-up
    // and trial block, and FR-013's identity held throughout.
    REQUIRE(eventDriven.spawnEvents > engagedLive.spawnEvents);
    REQUIRE(eventDriven.parentScans == eventDriven.spawnEvents);
    REQUIRE(engagedLive.parentScans == engagedLive.spawnEvents);

    const double sinkValue = gProbeSink;
    REQUIRE(detail::isFinite(sinkValue));

    // ---- the four arms, as differences against the rung below ---------------
    const double armDNs = clockOnly.nsPerBlock;
    const double armBNs = engagedEmpty.nsPerBlock - clockOnly.nsPerBlock;
    const double armANs = engagedLive.nsPerBlock - engagedEmpty.nsPerBlock;
    const double armCRawNs =
        (eventDriven.nsPerBlock - engagedLive.nsPerBlock) / static_cast<double>(kStepsPerBlock);
    const double armCAmortisedNs = armCRawNs / kEventIntervalBlocks;
    const double armCAmortisedTasksNs = armCRawNs / kTasksTableIntervalBlocks;
    const double sumNs = armANs + armBNs + armCAmortisedNs + armDNs;

    // A DIFFERENCE is not required to be positive - that is an ORDERING claim,
    // and plan S12.2's ordering is reported, never asserted. Finiteness is
    // required of every figure, because a non-finite one means broken arithmetic.
    for (const double ns : {armANs, armBNs, armCRawNs, armCAmortisedNs, armCAmortisedTasksNs,
                            armDNs, sumNs}) {
        REQUIRE(detail::isFinite(ns));
    }

    // -------------------------------------------------------------------------
    // The ordering plan S12.2 predicts: (b) >> (a) >> (d) >> amortised (c).
    // REPORTED, NEVER ASSERTED - a violated prediction is information about
    // where the cost actually is, which is the entire purpose of this case, not
    // a test failure.
    // -------------------------------------------------------------------------
    const bool orderBoverA = armBNs > armANs;
    const bool orderAoverD = armANs > armDNs;
    const bool orderDoverC = armDNs > armCAmortisedNs;
    const bool orderingHolds = orderBoverA && orderAoverD && orderDoverC;

    UNSCOPED_INFO(row("(a) child bookkeeping + live-child writes", armANs));
    UNSCOPED_INFO(row("(b) owned-slot write phase", armBNs));
    UNSCOPED_INFO(row("(c) spawn event, amortised (plan S12.2)", armCAmortisedNs));
    UNSCOPED_INFO(row("(d) disengaged clock", armDNs));
    UNSCOPED_INFO(row("== FR-073 SUM", sumNs));

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  BloomEngine FR-073 STAGE-COST PROBE - RE-POINTED AT THE REAL ENGINE (T020)\n"
       << "  (specs/vorago-phase7-harmonic-bloom, plan S12.2/S12.3)\n"
       << "  48 kHz, 512-sample blocks, best-of-" << kTrials << " x " << kBlocksPerTrial
       << " blocks after " << kWarmupBlocks << " warm-up\n"
       << "  Every arm drives the REAL BloomEngine through processChunk(). The private\n"
       << "  stages cannot be called directly (advanceChildren/applyOutput/runEvent are\n"
       << "  private and the header is read-only in T020), so each arm is a CONFIGURATION\n"
       << "  minus the configuration one rung below it. Compare against T003's STAND-IN\n"
       << "  table in the compliance notes - same basis, same trial shape.\n"
       << "  RUN IN ISOLATION. A figure taken beside a build or another suite is not\n"
       << "  evidence.\n"
       << "=================================================================================\n"
       << "  RAW CONFIGURATION FIGURES (what the clock actually saw)\n"
       << "---------------------------------------------------------------------------------\n"
       << row("  disengaged, numChildSlots = 0", clockOnly.nsPerBlock) << "\n"
       << row("  engaged, table EMPTY, clock off", engagedEmpty.nsPerBlock) << "\n"
       << row("  engaged, 16 children LIVE, clock off", engagedLive.nsPerBlock) << "\n"
       << row("  engaged, 16 LIVE, one event per call", eventDriven.nsPerBlock) << "\n"
       << "=================================================================================\n"
       << "  THE FOUR ARMS\n"
       << "=================================================================================\n"
       << row("  (a) child bookkeeping + live-child writes", armANs) << "\n"
       << "      = (16 live) - (table empty). advanceChildren() over 16 children in\n"
       << "      Phase::FadeIn - one double division plus one smoothstep each, 16 x 8 per\n"
       << "      block - PLUS applyOutput()'s live-child loop (2 stores + flushDenormal per\n"
       << "      child per call). Those two cannot be separated through the public surface.\n"
       << "---------------------------------------------------------------------------------\n"
       << row("  (b) owned-slot write phase", armBNs) << "\n"
       << "      = (engaged, empty) - (disengaged). *** PLAN S12.2 PREDICTS THIS ARM\n"
       << "      DOMINATES. *** " << kStoresPerCall << " stores per call x " << kStepsPerBlock
       << " calls = " << (kStoresPerCall * kStepsPerBlock) << " stores/block.\n"
       << "      FINDING, recorded rather than silently resolved: plan S12.3 prices this at\n"
       << "      (capacity - parentCount) + 2*numChildSlots = (64-48) + 2*16 = 48 stores per\n"
       << "      call. IN THE SC-011 WORST CASE THAT DOUBLE-COUNTS: parentCount (48) IS\n"
       << "      reserveBase(), so the FR-051 gap loop [pc, base) is EMPTY and the region\n"
       << "      [48, 64) is written exactly once, by the owned-region loop - 2 floats per\n"
       << "      slot, 32 stores per call. Plan S3.4's 1 152-store/block ceiling needs a\n"
       << "      SHORTER parentCount, which is plan S12.2's lever 2 in reverse.\n"
       << "---------------------------------------------------------------------------------\n"
       << eventRow("  (c) one full spawn event, RAW", armCRawNs) << "\n"
       << row("  (c) amortised / " + std::to_string(static_cast<int>(kEventIntervalBlocks)) +
                  " blocks  (plan S12.2)",
              armCAmortisedNs)
       << "\n"
       << row("  (c) amortised / 15000 blocks (tasks.md T003)", armCAmortisedTasksNs) << "\n"
       << "      = ((one event per call) - (16 live, no events)) / " << kStepsPerBlock
       << " calls per block.\n"
       << "      THE EVENT SHAPE THE SC-011 CONFIGURATION ACTUALLY RUNS: the strongest-K\n"
       << "      scan over " << kParentCount << " parents, the " << kParentCount
       << "-entry occupied set (one std::log2 each)\n"
       << "      plus one per live child, then " << kChildrenPerEvent
       << " slot-exhausted refusals - with all 16\n"
       << "      owned slots live, peekSlot() fails BEFORE any draw (FR-025), so the\n"
       << "      candidate-draw path is NOT in this figure. It cannot be: a steady state\n"
       << "      with free slots is a steady state whose child count is not 16.\n"
       << "      Max rate " << BloomEngine::kMaxSpawnRateHz
       << " Hz => p = rate * 64 / 48000 => a mean interval of\n"
       << "      " << static_cast<int>(kEventIntervalSteps) << " CONTROL STEPS = "
       << static_cast<int>(kEventIntervalBlocks) << " blocks. plan S12.2 says 15 000 control\n"
       << "      steps; tasks.md T003's arm table says 15 000 BLOCKS (8x more generous).\n"
       << "      BOTH are printed; the CONSERVATIVE one enters the sum.\n"
       << "---------------------------------------------------------------------------------\n"
       << row("  (d) disengaged clock", armDNs) << "\n"
       << "      The floor a caller pays for having the component in the chain at all:\n"
       << "      the unconditional clock draw and depth-ramp step of plan S3.3 (1)-(2), the\n"
       << "      all-Idle advanceChildren() walk, and applyOutput()'s !engaged_ early\n"
       << "      return. NOTE vs T003's stand-in (d): the real depth ramp is SETTLED here\n"
       << "      (prepare() snaps it, SC-014 (a)), so LinearRamp::process() takes its\n"
       << "      early-out; T003 deliberately held a stand-in ramp in flight.\n"
       << "=================================================================================\n"
       << "  ORDERING CHECK - plan S12.2 predicts (b) >> (a) >> (d) >> amortised (c)\n"
       << "=================================================================================\n"
       << "    (b) > (a)                : " << (orderBoverA ? "yes" : "NO") << "\n"
       << "    (a) > (d)                : " << (orderAoverD ? "yes" : "NO") << "\n"
       << "    (d) > amortised (c)      : " << (orderDoverC ? "yes" : "NO") << "\n";

    if (orderingHolds) {
        os << "    PREDICTED ORDERING HOLDS. Arm (b) is the lever, and the lever is\n"
           << "    CALLER-SIDE (plan S12.2): one processChunk per 512-sample block instead\n"
           << "    of eight (exact by FR-006 / SC-008 (b)), and a narrower\n"
           << "    capacity - parentCount. BloomEngine_CpuBudget MEASURES the first one.\n";
    } else {
        os << "    *** THE PREDICTED ORDERING DOES NOT HOLD. This is INFORMATION, not a\n"
           << "    *** failure: it says the cost is somewhere plan S12.2 did not expect,\n"
           << "    *** and the caller-side levers named there may not be the right ones.\n"
           << "    *** RECORD IT and re-read plan S12.3 before choosing a lever.\n";
    }

    os << "=================================================================================\n"
       << "  CROSS-CHECK against the FR-072 ceiling\n"
       << "=================================================================================\n"
       << row("  = FR-073 SUM (a)+(b)+amortised(c)+(d)", sumNs) << "\n"
       << row("  engaged, 16 live (the same thing, measured)", engagedLive.nsPerBlock) << "\n"
       << row("  FR-072 BUDGET (0.1 % of one core)", kBudgetNs) << "\n"
       << "    The sum telescopes back to the raw 16-live figure by construction; a large\n"
       << "    divergence between those two lines means measurement noise is the same size\n"
       << "    as an arm, and the table should be re-taken on an idle machine.\n"
       << "    sum as a fraction of the budget: " << std::fixed << std::setprecision(2)
       << (100.0 * sumNs / kBudgetNs) << " %\n"
       << "  *** NO AGENT MAY lower kMaxChildren, raise kBudgetNs, relax a threshold,\n"
       << "  *** shrink a workload, or widen a tolerance to make a figure fit. Reduce\n"
       << "  *** cost, never move the line (resonance_drift_network_perf_test.cpp:57-64,\n"
       << "  *** inherited verbatim).\n"
       << "=================================================================================\n"
       << "  RECORD the four arm figures and the sum in compliance.md, beside T003's\n"
       << "  stand-in table. The two are directly comparable: same basis, same trial shape.\n"
       << "=================================================================================\n";

    WARN(os.str());
}
