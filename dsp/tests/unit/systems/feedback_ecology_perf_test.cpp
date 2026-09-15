// ==============================================================================
// Layer 3: System Tests - FeedbackEcology, CPU budget (SC-004) and the FR-080
//          stage-cost probe            (specs/vorago-phase5-feedback-ecology)
// ==============================================================================
// Vorago Phase 5 (specs/vorago-phase5-feedback-ecology): SC-004 (a)-(f) plus
// the FR-080 stage-cost probe (plan S12.2, S14.1).
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase5-feedback-ecology/spec.md   (FR-080, SC-004)
//            specs/vorago-phase5-feedback-ecology/plan.md   (S14.1, S14.2,
//                                                            S14.3)
//            specs/vorago-phase5-feedback-ecology/tasks.md  (T001 creates this
//                                                            TU; T004 adds the
//                                                            stage probe below;
//                                                            T005 RUNS it alone
//                                                            and records the
//                                                            table; T020 adds
//                                                            the SC-004 (a)-(f)
//                                                            gated arms and the
//                                                            checked-in
//                                                            baselines beside
//                                                            it)
//
// SCOPE OF THIS TU: hidden, run-on-demand cases only - every case is tagged
//   "[.perf]", so none of them is ever run by the default suite or by CI.
//
// TIMING RUNS ALONE: node tools/run-cpu-tests.js dsp_systems_tests, with
//   nothing else executing - no build, no clang-tidy, no second suite, no
//   parallel agent. Sustained benchmarking heats the CPU and figures drift.
//   Never relax a budget or shrink a workload to make a figure fit.
//
// THIS TU IS DELIBERATELY NOT in dsp/tests/CMakeLists.txt's -fno-fast-math
//   block: that flag would move the figures its baselines are pinned to. It
//   must therefore never name a non-finite value either; finiteness checks use
//   Krate::DSP::detail::isFinite (core/db_utils.h).
//
// -----------------------------------------------------------------------------
// WHY THIS CASE EXISTS BEFORE THE COMPONENT DOES (tasks.md T004, plan S14.1)
// -----------------------------------------------------------------------------
// When this case was written FeedbackEcology did not exist, and the probe STILL
// MUST NOT reference it - the header is included further down for T020's gated
// arms only. Two realisation decisions and one pre-authorised lever are taken
// from the table this case prints, BEFORE a line of the header is written
// (tasks.md T005):
//
//   * D-1  - SVF (stage a) vs MultimodeFilter (stage b) for the loop filter.
//            FR-011 already specifies SVF; this measurement is the EVIDENCE,
//            not a re-opening. If (b) somehow beat (a), the response is to STOP
//            AND SURFACE, never to switch: MultimodeFilter's cheap path,
//            process(float*, size_t) (multimode_filter.h:180), updates
//            coefficients once per block (:186-187) and is structurally
//            unusable inside a feedback loop, where each output sample depends
//            on the previous one. Only the per-sample entry point,
//            processSample (:205), is a candidate at all - and it calls
//            updateCoefficientsFromSmoothed() at :218 on EVERY SAMPLE.
//   * OQ-1 realisation - a plain Biquad fed directly-computed RBJ coefficients
//            (stage d) vs six single-slot ResonatorBank instances (stage g).
//            D-3 predicts (g) loses: a ResonatorBank costs three OnePoleSmoother
//            advances plus a 16-iteration loop with 15 continues per sample to
//            reach one enabled biquad. The probe measures it anyway, so the
//            decision is a number rather than a reading of the header.
//   * OQ-2 lever 1 - whether six std::tanh per sample fit (stage f). The plan's
//            first-order estimate predicts they do NOT: the whole-component
//            budget is 71 111 ns / 512 samples = 139 ns PER SAMPLE.
//            FastMath::fastTanh (core/fast_math.h:65) is PRE-AUTHORISED: it
//            returns exactly +/-1 beyond +/-3.5 and is within 0.05 % below, so
//            FR-042's bound is preserved exactly.
//
// Levers 4 (drop the resonator to a second SVF) and 5 (default numLoops 6 -> 5)
// are USER decisions taken from the surfaced table, never agent decisions.
//
//   *** STOP-AND-SURFACE RULE (FR-080, inherited verbatim from
//   *** resonance_drift_network_perf_test.cpp:59-65) - NON-NEGOTIABLE ***
//   NO IMPLEMENTING AGENT MAY lower kMaxLoops, raise the budget, relax a
//   threshold, or shrink a workload to make a figure fit. Reduce cost, never
//   move the line. The verdict block is emitted loudly via WARN precisely so
//   the decision is taken from the measured table rather than from a guess.
//
// -----------------------------------------------------------------------------
// WHAT THIS CASE ASSERTS, AND WHY THAT IS ALL
// -----------------------------------------------------------------------------
// It is a PROBE, NOT A GATE. It REQUIREs only that every measured figure is
// finite and strictly positive - a zero or a NaN means the MEASUREMENT is
// broken, which is the one thing that would make the table lie. It asserts
// neither the 106 666 ns absolute ceiling nor the 71 111 ns gated baseline,
// because the response to a miss is a decision taken from measured numbers, and
// in the "still over with lever 1" branch it is a USER decision.
//
// -----------------------------------------------------------------------------
// WHY ns/block AND NOT "% of one core"
// -----------------------------------------------------------------------------
// A percent-of-core figure is not reproducible across dev machines or CI
// runners (resonance_drift_network_perf_test.cpp:68-76). The measurement basis
// is NANOSECONDS PER 512-SAMPLE BLOCK AT 48 kHz. One block period is
// 10 666 667 ns, so the 1.5 %/voice ceiling (amended 2026-09-13 from 1 %) is
// 160 000 ns/block and the gated baseline is 160 000 / 1.5 = 106 667 ns. The percent figure is REPORTED,
// never asserted. Trial shape: best-of-25 x 500 blocks after 400 warm-up
// blocks, inherited verbatim from resonance_drift_network_perf_test.cpp:59-84.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/crossfading_delay_line.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/multimode_filter.h>
#include <krate/dsp/processors/resonator_bank.h>
#include <krate/dsp/systems/feedback_ecology.h>

// THE COMPONENT HEADER IS INCLUDED BY T020, WHICH ADDED THE SC-004 GATED ARMS AT
// THE BOTTOM OF THIS FILE, AND BY NOTHING ELSE. It was deliberately absent while
// T004 was the only case here: the stage probe measures the shipped primitives
// FeedbackEcology will compose, BEFORE the component exists, and that is the
// entire point of that task (tasks.md T004, plan S14.1). THE PROBE ITSELF STILL
// MUST NOT NAME FeedbackEcology - its local constant tables are what T005's
// realisation decisions were taken from, and the static_asserts in T020's
// section below are what now hold them to the shipped header.

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <sstream>
#include <string>

using namespace Krate::DSP;

namespace {

// =============================================================================
// Measurement basis (plan S14, S14.1)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr float kSr48f = 48000.0f;
constexpr std::size_t kBlockSize = 512;

/// The component's control chunk (FR-007, kControlChunkSamples = 64). Lane
/// advances and coefficient writes happen once per chunk, which is what makes
/// stages (a), (b), (c), (i) and (j) decidable at all.
constexpr std::size_t kControlChunk = 64;
static_assert(kBlockSize % kControlChunk == 0,
              "the control chunk must divide the measured block exactly, or a stage's "
              "control writes would land at a different cadence than the component's");

/// Wall-clock period of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockPeriodNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// The per-voice ceiling, written as the literal the spec names and TIED to
/// its derivation by the clause below rather than computed from it.
///
/// AMENDED 2026-09-13 BY USER DECISION: 1 % -> 1.5 % of one core per voice
/// (160 000 ns/block; gated line 106 667 ns). History: the first isolated run
/// measured the reference arm at 184 463 ns (1.73 %, this machine running the
/// whole perf set ~1.8x slow on E-cores; ~102 000 ns machine-corrected). The
/// two engineering levers the plan's ladder did not list were then applied -
/// CrossfadingDelayLine::read() no longer reads the idle tap outside a
/// crossfade (~half of the 62 091 ns delay stage), and the component pushes a
/// cutoff to its SVF only on a real move (kCutoffPushRelative) so
/// SVF::advanceSmoother's early-out actually fires - and the reference arm
/// measured 94 106.4 ns P-core-pinned: under the 1 % ceiling, over its 0.667 %
/// gated line. The roadmap's per-voice envelope is 4-5 % (line 92); the same
/// call was made for Seraphis's AtmosphereEngine (1 -> 1.5 %) and Vorago
/// Phase 2 (1 -> 1.75 %). Comments elsewhere in this TU that quote 106 666 /
/// 71 111 describe the pre-amendment budget the probe was compared against.
///
/// NO AGENT MAY RAISE THIS. The stop-and-surface rule above governs every
/// response to a miss.
constexpr double kAbsoluteCeilingNs = 160000.0;
static_assert(kAbsoluteCeilingNs >= kBlockPeriodNs * 0.0149
                  && kAbsoluteCeilingNs <= kBlockPeriodNs * 0.0151,
              "the absolute ceiling is 1.5 % of one 512-sample block at 48 kHz");

/// SC-004's regression factor: the checked-in baseline is gated at
/// kBaseline * 1.5 <= kAbsoluteCeilingNs, so the line a projection is compared
/// against is 160 000 / 1.5 = 106 667 ns (plan S14, amended 2026-09-13).
constexpr double kRegressionFactor = 1.5;
constexpr double kGatedBaselineNs = kAbsoluteCeilingNs / kRegressionFactor;
static_assert(kGatedBaselineNs > 106666.0 && kGatedBaselineNs < 106667.0,
              "the gated baseline is 160 000 / 1.5 = 106 667 ns");

// Trial shape, pinned by tasks.md T004.
constexpr int kTrials = 25;
constexpr int kBlocksPerTrial = 500;
constexpr int kWarmupBlocks = 400;

// -----------------------------------------------------------------------------
// The FR-013/FR-022/FR-052/FR-053 default tables (tasks.md T006's constants
// table). NORMATIVE VALUES, used verbatim. They are spelled LOCALLY, with no
// reference to FeedbackEcology, because the class that will own them does not
// exist yet - that is the whole point of this task. When T006 writes the
// header, these six-row tables must match it exactly.
// -----------------------------------------------------------------------------
constexpr std::size_t kNumLoops = 6;  ///< kMaxLoops (FR-010)

constexpr std::array<float, kNumLoops> kDefaultLoopDelayMs{41.0f,  67.0f,  109.0f,
                                                           173.0f, 281.0f, 449.0f};
constexpr std::array<float, kNumLoops> kDefaultLoopCutoffHz{2400.0f, 1700.0f, 1200.0f,
                                                            850.0f,  600.0f,  420.0f};
constexpr std::array<float, kNumLoops> kDefaultLoopResonanceHz{1200.0f, 850.0f, 600.0f,
                                                               425.0f,  300.0f, 210.0f};
constexpr std::array<float, kNumLoops> kDefaultDelayWanderFraction{0.16f, 0.10f, 0.06f,
                                                                   0.04f, 0.03f, 0.02f};

constexpr float kDefaultResonanceRt60 = 1.0f;
constexpr float kDefaultFilterQ = SVF::kButterworthQ;
constexpr float kDefaultCutoffWanderOctaves = 0.5f;

/// The delay-line request (FR-020): 500 ms of usable range plus headroom.
constexpr float kMaxDelaySeconds = 0.52f;
constexpr float kCrossfadeMs = 20.0f;

/// The governor's follower configuration (FR-043).
constexpr float kGovernorAttackMs = 20.0f;
constexpr float kGovernorReleaseMs = 800.0f;

/// FR-050's lanes: two per loop (delay time and filter cutoff), so twelve.
constexpr std::size_t kNumLanes = 2 * kNumLoops;
static_assert(kNumLanes == 12, "FR-050 is two BrownianDrift lanes per loop");

/// FR-055's decimation points and the smoothness that TRAVELS with each of
/// them. The smoothness must travel with the decimation because that is how
/// setWanderRate (S11) configures the lanes; measuring decimation 17 at
/// decimation 1's smoothness would price a lane the component never builds.
///
///   rate 1.000 Hz -> tau_req =   1.000 s -> dec  1 -> tau =  1.000 s -> s = 0.0268
///   rate 0.030 Hz -> tau_req =  33.333 s -> dec  2 -> tau = 16.667 s -> s = 0.5526
///   rate 0.002 Hz -> tau_req = 500.000 s -> dec 17 -> tau = 29.412 s -> s = 0.9803
///
///   s = (tau - kTauMin) / (kTauMax - kTauMin), kTauMin = 0.2, kTauMax = 30.0
///       (brownian_drift.h:97, :99)
constexpr int kDecimationFast = 1;     ///< kMaxWanderRateHz = 1.0 Hz
constexpr int kDecimationDefault = 2;  ///< kDefaultWanderRateHz = 0.03 Hz
constexpr int kDecimationSlow = 17;    ///< kMinWanderRateHz = 0.002 Hz, kMaxLaneDecimation

constexpr float kSmoothnessAtDec1 = 0.0268f;
constexpr float kSmoothnessAtDec2 = 0.5526f;
constexpr float kSmoothnessAtDec17 = 0.9803f;

/// Stage (j)'s ramp bank. tasks.md T004 and plan S14.1 both STATE fifteen
/// LinearRamp::process advances per sample, and both then ENUMERATE sixteen
/// items: mix (1) + wet trim (1) + 6 input gains + 6 gates + governor (1) +
/// normGain (1) = 16. The discrepancy is in the authoritative documents, so
/// this probe refuses to pick a side silently: it measures the STATED fifteen
/// (which is what the projection uses, per T004) and reports the enumerated
/// sixteen alongside, so either reading can be priced from this table without
/// a re-run. T006 settles it when it writes the header's member list.
constexpr std::size_t kNumRampsStated = 15;
constexpr std::size_t kNumRampsEnumerated = 16;
static_assert(kNumRampsEnumerated == 2 + 2 * kNumLoops + 2,
              "the enumeration is mix + wet trim + 6 input gains + 6 gates + governor + normGain");

/// Ramp times (tasks.md T006's constants table).
constexpr float kGainRampMs = 50.0f;      ///< input gains, gates, wet trim, normGain
constexpr float kMixRampMs = 20.0f;       ///< mix
constexpr float kGovernorRampMs = 20.0f;  ///< the governor's gain ramp

/// Ramp `i` of the bank, in the enumeration order above.
[[nodiscard]] constexpr float rampTimeMsFor(std::size_t i) noexcept
{
    if (i == 0) {
        return kMixRampMs;  // mix
    }
    if (i == 2 + 2 * kNumLoops) {
        return kGovernorRampMs;  // governor
    }
    return kGainRampMs;  // wet trim, 6 input gains, 6 gates, normGain
}

// =============================================================================
// Best-of-N driver (resonance_drift_network_perf_test.cpp:278-306)
// =============================================================================

/// Pinned warm-up, then best-of-`kTrials` x `kBlocksPerTrial`; returns the
/// winning trial's ns per invocation of `runBlock`. One invocation is one
/// 512-sample block for every stage in this probe, so every result is ns/block.
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

/// Deterministic white-noise fill, so every stage sees the same excitation on
/// every machine and every run.
void fillWhite(float* buffer, std::size_t numSamples, std::uint32_t seed) noexcept
{
    Xorshift32 rng{seed};
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = rng.nextFloat() * 0.25f;
    }
}

/// A bipolar triangle over `kWanderTableSize` control steps, in [-1, +1].
///
/// This stands in for the FR-052/FR-053 lane output at the control writes in
/// stages (a), (b) and (c). It is a TABLE, not a std::sin call, so the
/// wander source itself costs nothing and the figure prices the WRITE - which
/// is the stage under measurement. The lane's own cost is stage (i), measured
/// separately and added ONCE in the projection rather than twice.
///
/// The excursion is deliberately large enough to cross
/// CrossfadingDelayLine's kCrossfadeThresholdSamples = 100
/// (crossfading_delay_line.h:78) several times per table pass in stage (c): at
/// 48 kHz loop 0 spans 2 x 0.16 x 41 ms = 630 samples over 32 steps (~20
/// samples/step) and loop 5 spans 2 x 0.02 x 449 ms = 862 samples (~27
/// samples/step). A sub-threshold wander would produce NO crossfades at all and
/// would price a delay line the component never renders (plan R-4).
constexpr std::size_t kWanderTableSize = 64;

[[nodiscard]] std::array<float, kWanderTableSize> makeWanderTable() noexcept
{
    std::array<float, kWanderTableSize> table{};
    for (std::size_t i = 0; i < kWanderTableSize; ++i) {
        const float phase = static_cast<float>(i) / static_cast<float>(kWanderTableSize);
        table[i] = 4.0f * std::fabs(phase - 0.5f) - 1.0f;  // triangle: +1 -> -1 -> +1
    }
    return table;
}

/// The S4.1 RBJ constant-peak-gain bandpass, computed EXACTLY as FR-013
/// specifies the component will compute it: directly, and written through
/// Biquad::setCoefficients - never Biquad::configure and never
/// SmoothedBiquad::setTarget, both of which route through
/// BiquadCoefficients::calculate (biquad.h:673) and clamp Q at biquad.h's
/// kMaxQ = 30, silently undercutting the kMaxResonatorQ = 100 ceiling FR-013
/// specifies (resonator_bank.h:51). Measuring the clamped shape would price a
/// filter the component never builds.
[[nodiscard]] BiquadCoefficients makeResonatorCoefficients(float centreHz,
                                                           float rt60Seconds,
                                                           float sampleRate) noexcept
{
    const float f =
        std::clamp(centreHz, kMinResonatorFrequency, kMaxResonatorFrequencyRatio * sampleRate);
    const float q = std::clamp(rt60ToQ(f, rt60Seconds), kMinResonatorQ, kMaxResonatorQ);

    const float omega = kTwoPi * f / sampleRate;
    const float alpha = std::sin(omega) / (2.0f * q);
    const float a0 = 1.0f + alpha;

    BiquadCoefficients coeffs{};
    coeffs.b0 = alpha / a0;
    coeffs.b1 = 0.0f;
    coeffs.b2 = -alpha / a0;
    coeffs.a1 = -2.0f * std::cos(omega) / a0;
    coeffs.a2 = (1.0f - alpha) / a0;
    return coeffs;
}

// =============================================================================
// Stage (0) - the harness floor
// =============================================================================
//
// The 512-iteration loop and the anti-dead-code sink, with no DSP at all. It is
// REPORTED and NOT subtracted from any stage, exactly as the Phase-3 probe
// reports its refill term. Its consequence is stated rather than hidden: the
// eight-term projection below carries this floor EIGHT times over, so the
// projection OVER-estimates - the correct direction for a budget probe.
//
// This row is a LOWER BOUND on that overhead and is labelled as one: unlike
// every other stage it holds no state, so a compiler is free to hoist or
// reassociate the sum (the macOS and Linux legs build with -ffast-math). Every
// OTHER stage in this probe is sequentially dependent through its filter,
// delay, follower or ramp state and cannot be hoisted - except stage (f),
// which carries an explicit per-block bias for exactly that reason.

[[nodiscard]] double measureHarnessFloor(double& sink)
{
    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x0F100Au);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            acc += src[n];
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (a) - SVF::process x 6, the FR-011 loop filter        [D-1, vs (b)]
// =============================================================================
//
// Smoothing enabled (svf.h:239), one setCutoff per 64-sample control chunk. The
// cutoff must genuinely MOVE on every control step: with a static target the
// smoother converges, advanceSmoother() (svf.h:494) reaches its early-out and
// the measurement silently becomes the un-smoothed shape - a filter the
// component never renders under wander. The exp2 is part of the control step:
// S4/T007 caches baseLog2Cutoff so a cutoff write costs one exp2 and NO
// std::log2.

[[nodiscard]] double measureSvfBank(double& sink)
{
    std::array<SVF, kNumLoops> filters{};
    for (std::size_t i = 0; i < kNumLoops; ++i) {
        filters[i].prepare(kSr48);
        filters[i].setMode(SVFMode::Lowpass);
        filters[i].enableSmoothing(true);
        filters[i].setResonance(kDefaultFilterQ);
        filters[i].setCutoff(kDefaultLoopCutoffHz[i]);
    }

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x5EED0A1u);
    const auto wander = makeWanderTable();

    // Carried ACROSS blocks, not reset per block: the control grid is absolute
    // (FR-007), and a per-block reset would restart the wander every block.
    std::size_t step = 0;

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            if ((n % kControlChunk) == 0) {
                const float octaves = kDefaultCutoffWanderOctaves * wander[step % kWanderTableSize];
                ++step;
                const float factor = std::exp2(octaves);
                for (std::size_t i = 0; i < kNumLoops; ++i) {
                    filters[i].setCutoff(kDefaultLoopCutoffHz[i] * factor);
                }
            }
            const float x = src[n];
            for (std::size_t i = 0; i < kNumLoops; ++i) {
                acc += filters[i].process(x);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (b) - MultimodeFilter::processSample x 6, the ROADMAP's filter   [D-1]
// =============================================================================
//
// The roadmap's named filter at its ONLY per-sample entry point
// (multimode_filter.h:205), which calls updateCoefficientsFromSmoothed() at
// :218 on every sample. Slope12dB is set explicitly so this is ONE biquad stage
// against ONE SVF - the fairest possible comparison; anything steeper would
// price a filter (a) is not offering.

[[nodiscard]] double measureMultimodeBank(double& sink)
{
    std::array<MultimodeFilter, kNumLoops> filters{};
    for (std::size_t i = 0; i < kNumLoops; ++i) {
        filters[i].prepare(kSr48, kBlockSize);
        filters[i].setType(FilterType::Lowpass);
        filters[i].setSlope(FilterSlope::Slope12dB);
        filters[i].setResonance(kDefaultFilterQ);
        filters[i].setCutoff(kDefaultLoopCutoffHz[i]);
        // Drive OFF: FR-042's tanh is the loop's ONLY nonlinearity and it is
        // measured on its own in stage (f). A driven filter would double-count
        // a tanh the component does not place here.
        filters[i].setDrive(0.0f);
    }

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x5EED0A1u);
    const auto wander = makeWanderTable();
    std::size_t step = 0;

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            if ((n % kControlChunk) == 0) {
                const float octaves = kDefaultCutoffWanderOctaves * wander[step % kWanderTableSize];
                ++step;
                const float factor = std::exp2(octaves);
                for (std::size_t i = 0; i < kNumLoops; ++i) {
                    filters[i].setCutoff(kDefaultLoopCutoffHz[i] * factor);
                }
            }
            const float x = src[n];
            for (std::size_t i = 0; i < kNumLoops; ++i) {
                acc += filters[i].processSample(x);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (c) - CrossfadingDelayLine::process x 6, the FR-020 loop delay
// =============================================================================
//
// 520 ms lines, one setDelayMs per control chunk on the FR-022 default table
// modulated by the FR-052 default wander fractions. setCrossfadeTime comes
// AFTER prepare(): prepare() sets sampleRate_ (:101) and THEN overwrites the
// crossfade time with its own default (:119), so the reverse order would
// silently measure a crossfade time the caller never asked for (FR-005 (a)).

[[nodiscard]] double measureDelayBank(double& sink)
{
    std::array<CrossfadingDelayLine, kNumLoops> delays{};
    for (std::size_t i = 0; i < kNumLoops; ++i) {
        delays[i].prepare(kSr48, kMaxDelaySeconds);
        delays[i].setCrossfadeTime(kCrossfadeMs);  // MUST follow prepare()
        delays[i].snapToDelayMs(kDefaultLoopDelayMs[i]);
    }

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x0DE1A9u);
    const auto wander = makeWanderTable();
    std::size_t step = 0;

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            if ((n % kControlChunk) == 0) {
                const float off = wander[step % kWanderTableSize];
                ++step;
                for (std::size_t i = 0; i < kNumLoops; ++i) {
                    delays[i].setDelayMs(kDefaultLoopDelayMs[i]
                                         * (1.0f + kDefaultDelayWanderFraction[i] * off));
                }
            }
            const float x = src[n];
            for (std::size_t i = 0; i < kNumLoops; ++i) {
                acc += delays[i].process(x);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (d) - Biquad::process x 6, the FR-013 resonator   [OQ-1, vs (g)]
// =============================================================================
//
// Coefficients are computed ONCE at setup, not per block: FR-076 makes a
// resonator retune a STEPPED HARD SWAP of a rare, user-set control, not a
// wander target, so there is no per-block coefficient write to price here.

[[nodiscard]] double measureBiquadBank(double& sink)
{
    std::array<Biquad, kNumLoops> resonators{};
    for (std::size_t i = 0; i < kNumLoops; ++i) {
        resonators[i].setCoefficients(makeResonatorCoefficients(
            kDefaultLoopResonanceHz[i], kDefaultResonanceRt60, kSr48f));
    }

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x0B19ADu);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const float x = src[n];
            for (std::size_t i = 0; i < kNumLoops; ++i) {
                acc += resonators[i].process(x);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (e) - DCBlocker::process x 6, the FR-014 in-loop DC removal
// =============================================================================

[[nodiscard]] double measureDcBlockerBank(double& sink)
{
    std::array<DCBlocker, kNumLoops> blockers{};
    for (std::size_t i = 0; i < kNumLoops; ++i) {
        blockers[i].prepare(kSr48);
    }

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x0DCB10u);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const float x = src[n];
            for (std::size_t i = 0; i < kNumLoops; ++i) {
                acc += blockers[i].process(x);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (f) - the FR-042 soft clip x 6, both variants      [OQ-2 lever 1]
// =============================================================================
//
// The input is scaled into tanh's ACTIVE region rather than its saturated tail:
// past |x| >= 3.5 FastMath::fastTanh returns a constant (fast_math.h:76-81) and
// std::tanh's own range reduction short-circuits, so a saturated drive would
// price the cheap branch of both and hide exactly the term OQ-2 lever 1 exists
// to decide. Each loop gets its own scale so the six calls are not identical.
//
// THE PER-BLOCK BIAS IS LOAD-BEARING, NOT NOISE. Unlike every other stage this
// one holds NO state, so without it the whole 512 x 6 computation is invariant
// across the 500 blocks of a trial and a compiler is entitled to hoist it out
// of the trial loop entirely - which would report a near-zero cost for the term
// the plan predicts DOMINATES the budget, and OQ-2 lever 1 would then be
// declined on a measurement of nothing. The bias makes every block's arguments
// different at the cost of one float add per sample. It reaches ~1.3e-3 after
// the full warm-up plus 25 trials, far inside tanh's active region, so it moves
// no figure it is protecting.

enum class TanhVariant : std::uint8_t { Std, Fast };

[[nodiscard]] double measureTanhBank(TanhVariant variant, double& sink)
{
    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x07A18Du);

    std::array<float, kNumLoops> scale{};
    for (std::size_t i = 0; i < kNumLoops; ++i) {
        scale[i] = 0.6f + 0.25f * static_cast<float>(i);  // 0.60 .. 1.85
    }

    float bias = 0.0f;

    if (variant == TanhVariant::Std) {
        return bestTrialNs([&]() noexcept {
            bias += 1.0e-7f;
            float acc = 0.0f;
            for (std::size_t n = 0; n < kBlockSize; ++n) {
                const float x = src[n] + bias;
                for (std::size_t i = 0; i < kNumLoops; ++i) {
                    acc += std::tanh(x * scale[i]);
                }
            }
            sink += static_cast<double>(acc);
        });
    }

    return bestTrialNs([&]() noexcept {
        bias += 1.0e-7f;
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const float x = src[n] + bias;
            for (std::size_t i = 0; i < kNumLoops; ++i) {
                acc += FastMath::fastTanh(x * scale[i]);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (g) - six single-slot ResonatorBank instances      [OQ-1, vs (d)]
// =============================================================================
//
// FR-013's rejected realisation, measured so the rejection is a number. Only
// slot 0 is ever enabled; slots [1, 16) stay disabled and cost one predicted
// branch each (resonator_bank.h:487-488). Six of those skip loops, plus six
// sets of three global smoothers (:471-473), is what D-3 predicts loses to (d).
//
// Write order is setFrequency -> setQ, and it is MANDATORY: setFrequency
// re-derives qValues_ from the stored decay (resonator_bank.h:333), so a
// frequency write after a Q write silently discards the Q and this probe would
// price a bank at a Q it was never configured to.

[[nodiscard]] double measureResonatorBankChain(double& sink)
{
    std::array<ResonatorBank, kNumLoops> banks{};
    for (std::size_t i = 0; i < kNumLoops; ++i) {
        banks[i].prepare(kSr48);
        // Non-zero tilt costs a std::log2 + dbToGain per resonator per sample
        // (resonator_bank.h:507); damping and exciter mix are pinned so the
        // figure prices the bandpass, not the bank's global controls.
        banks[i].setDamping(0.0f);
        banks[i].setExciterMix(0.0f);
        banks[i].setSpectralTilt(0.0f);
        banks[i].setEnabled(0, true);
        banks[i].setFrequency(0, kDefaultLoopResonanceHz[i]);
        banks[i].setQ(0, rt60ToQ(kDefaultLoopResonanceHz[i], kDefaultResonanceRt60));
        banks[i].setGain(0, 0.0f);
    }

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x0B19ADu);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const float x = src[n];
            for (std::size_t i = 0; i < kNumLoops; ++i) {
                acc += banks[i].process(x);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (h) - the FR-043 governor's EnvelopeFollower, one processSample/sample
// =============================================================================

[[nodiscard]] double measureGovernorFollower(double& sink)
{
    EnvelopeFollower follower;
    follower.prepare(kSr48, kBlockSize);
    follower.setMode(DetectionMode::RMS);
    follower.setAttackTime(kGovernorAttackMs);
    follower.setReleaseTime(kGovernorReleaseMs);
    // FR-043: the sidechain high-pass stays DISABLED. The governor must see the
    // sub content, which is Vorago's identity.
    follower.setSidechainEnabled(false);

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x060FE6u);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            acc += follower.processSample(src[n]);
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (i) - twelve BrownianDrift lanes at three decimations   [FR-055]
// =============================================================================
//
// Depth 1.0 keeps the walk genuinely live: at depth 0 the output smoother
// converges, advanceSamples early-returns (smoother.h:207-209), and the
// std::pow that dominates this stage disappears - measuring a shape the
// component never renders.

[[nodiscard]] double measureLanes(int decimation, float smoothness, double& sink)
{
    std::array<BrownianDrift, kNumLanes> lanes{};
    for (std::size_t i = 0; i < kNumLanes; ++i) {
        lanes[i].prepare(kSr48);
        lanes[i].setSeed(deriveStreamSeed(0x5EEDu, i));
        lanes[i].setSmoothness(smoothness);
        lanes[i].setDepth(1.0f);
    }

    // Carried ACROSS blocks, not reset per block: the FR-055 lane clock is
    // absolute, and a per-block reset would advance every lane on the first
    // chunk of every block regardless of decimation - i.e. would measure
    // decimation 1 three times over.
    int counter = 0;

    return bestTrialNs([&]() noexcept {
        for (std::size_t off = 0; off < kBlockSize; off += kControlChunk) {
            if (counter == 0) {
                for (std::size_t i = 0; i < kNumLanes; ++i) {
                    lanes[i].processBlock(kControlChunk);
                }
            }
            counter = (counter + 1) % decimation;
        }
        sink += static_cast<double>(lanes[0].getCurrentValue());
    });
}

// =============================================================================
// Stage (j) - the per-sample ramp bank, settled and moving
// =============================================================================
//
// LinearRamp::process early-outs on current_ == target_ (smoother.h:372-374),
// so the bank has two genuinely different costs and BOTH belong in the table:
//
//   SETTLED - every ramp at its target. N predicted branches per sample. This
//             is the steady state SC-004's arms (a)/(b) will sit in.
//   MOVING  - every ramp re-targeted on every control step, so none of them
//             ever early-outs. N add-compare-store sequences per sample plus N
//             setTarget increment recomputations per chunk. This is the shape
//             during any gate fade, mix move or governor response, and it is
//             the figure the PROJECTION uses - the conservative direction.
//
// If (j) alone is a large fraction of the budget the finding is REAL and
// belongs in the surfaced table - NOT in a decision to advance ramps less
// often, which would break SC-010's block-partition invariance (plan S14.3).

enum class RampMotion : std::uint8_t { Settled, Moving };

[[nodiscard]] double measureRampBank(std::size_t numRamps, RampMotion motion, double& sink)
{
    std::array<LinearRamp, kNumRampsEnumerated> ramps{};
    for (std::size_t i = 0; i < numRamps; ++i) {
        ramps[i].configure(rampTimeMsFor(i), kSr48f);
        ramps[i].snapTo(0.5f);
    }

    // The moving target ALTERNATES across the full span rather than nudging
    // along the wander table, and that is deliberate. A ramp early-outs the
    // moment it reaches its target (smoother.h:372-374): in one 64-sample chunk
    // a 50 ms ramp covers 64/2400 = 0.027 of the span and a 20 ms ramp 0.067,
    // so a small per-step delta would let most of the bank SETTLE part-way
    // through every chunk and the "moving" figure would silently become a
    // blend of the two shapes. A 0.8 delta guarantees no ramp in the bank ever
    // reaches its target, which is the worst case the projection wants.
    std::size_t step = 0;

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            if (motion == RampMotion::Moving && (n % kControlChunk) == 0) {
                const float target = ((step % 2u) == 0u) ? 0.1f : 0.9f;
                ++step;
                for (std::size_t i = 0; i < numRamps; ++i) {
                    ramps[i].setTarget(target);
                }
            }
            for (std::size_t i = 0; i < numRamps; ++i) {
                acc += ramps[i].process();
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Reporting (resonance_drift_network_perf_test.cpp:683-696)
// =============================================================================

[[nodiscard]] std::string row(const std::string& label, double nsPerBlock)
{
    std::ostringstream os;
    os << std::left << std::setw(56) << label << std::right << std::fixed << std::setprecision(1)
       << std::setw(12) << nsPerBlock << " ns/block   " << std::setprecision(4) << std::setw(9)
       << (100.0 * nsPerBlock / kBlockPeriodNs) << " % of one core";
    return os.str();
}

}  // namespace

// =============================================================================
// T004 - the FR-080 stage-cost probe, with NO component in existence
// =============================================================================

TEST_CASE("FeedbackEcology_StageCostProbe", "[feedback_ecology][.perf]")
{
    double sink = 0.0;

    const double floorNs = measureHarnessFloor(sink);

    const double svfNs = measureSvfBank(sink);
    const double multimodeNs = measureMultimodeBank(sink);
    const double delayNs = measureDelayBank(sink);
    const double biquadNs = measureBiquadBank(sink);
    const double dcBlockerNs = measureDcBlockerBank(sink);
    const double tanhStdNs = measureTanhBank(TanhVariant::Std, sink);
    const double tanhFastNs = measureTanhBank(TanhVariant::Fast, sink);
    const double resonatorBankNs = measureResonatorBankChain(sink);
    const double followerNs = measureGovernorFollower(sink);

    const double lanesDec1Ns = measureLanes(kDecimationFast, kSmoothnessAtDec1, sink);
    const double lanesDec2Ns = measureLanes(kDecimationDefault, kSmoothnessAtDec2, sink);
    const double lanesDec17Ns = measureLanes(kDecimationSlow, kSmoothnessAtDec17, sink);

    const double ramps15SettledNs = measureRampBank(kNumRampsStated, RampMotion::Settled, sink);
    const double ramps15MovingNs = measureRampBank(kNumRampsStated, RampMotion::Moving, sink);
    const double ramps16MovingNs = measureRampBank(kNumRampsEnumerated, RampMotion::Moving, sink);

    // The sink is read so no stage can be dead-coded away. It is not a result.
    REQUIRE(detail::isFinite(sink));

    // -------------------------------------------------------------------------
    // The ONLY assertions this case makes: a probe, not a gate (tasks.md T004).
    // A zero or a non-finite figure means the MEASUREMENT is broken, which is
    // the one thing that would make the table below lie.
    // -------------------------------------------------------------------------
    for (const double ns : {floorNs, svfNs, multimodeNs, delayNs, biquadNs, dcBlockerNs, tanhStdNs,
                            tanhFastNs, resonatorBankNs, followerNs, lanesDec1Ns, lanesDec2Ns,
                            lanesDec17Ns, ramps15SettledNs, ramps15MovingNs, ramps16MovingNs}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    // =========================================================================
    // The first-order projection (tasks.md T004):
    //   (a) + (c) + (d) + (e) + (f, std::tanh) + (h) + (i @ dec 2) + (j)
    // against the 71 111 ns gated baseline. (b) and (g) are the REJECTED
    // realisations and do not enter it; (i) enters at decimation 2, the
    // kDefaultWanderRateHz = 0.03 Hz default; (j) enters MOVING, the
    // conservative shape.
    // =========================================================================
    const double projectedNs = svfNs + delayNs + biquadNs + dcBlockerNs + tanhStdNs + followerNs
                               + lanesDec2Ns + ramps15MovingNs;
    const double projectedFastTanhNs = projectedNs - tanhStdNs + tanhFastNs;

    const double tanhSavingNs = tanhStdNs - tanhFastNs;
    const double perSampleNs = projectedNs / static_cast<double>(kBlockSize);
    const double perSampleBudgetNs = kGatedBaselineNs / static_cast<double>(kBlockSize);

    const bool d1SvfWins = svfNs < multimodeNs;
    const bool oq1BiquadWins = biquadNs < resonatorBankNs;
    const bool fitsAsWritten = projectedNs <= kGatedBaselineNs;
    const bool fitsWithLever1 = projectedFastTanhNs <= kGatedBaselineNs;

    UNSCOPED_INFO(row("(a) SVF x 6                                [D-1]", svfNs));
    UNSCOPED_INFO(row("(b) MultimodeFilter x 6                    [D-1]", multimodeNs));
    UNSCOPED_INFO(row("(d) Biquad x 6                            [OQ-1]", biquadNs));
    UNSCOPED_INFO(row("(g) 6 x single-slot ResonatorBank         [OQ-1]", resonatorBankNs));
    UNSCOPED_INFO(row("(f) std::tanh x 6                  [OQ-2 lever 1]", tanhStdNs));
    UNSCOPED_INFO(row("(f) FastMath::fastTanh x 6         [OQ-2 lever 1]", tanhFastNs));
    UNSCOPED_INFO(row("== FR-080 PROJECTION (std::tanh)", projectedNs));
    UNSCOPED_INFO(row("== FR-080 PROJECTION (lever 1 applied)", projectedFastTanhNs));

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  FeedbackEcology T004 STAGE-COST PROBE - measured BEFORE the component exists\n"
       << "  (specs/vorago-phase5-feedback-ecology, plan S14.1, FR-080)\n"
       << "  48 kHz, 512-sample blocks, best-of-" << kTrials << " x " << kBlocksPerTrial
       << " blocks after " << kWarmupBlocks << " warm-up\n"
       << "  RUN IN ISOLATION. A figure taken beside a build or another suite is not\n"
       << "  evidence. Record this whole table in the build transcript and in plan S14.1\n"
       << "  as the measured row set (tasks.md T005).\n"
       << "=================================================================================\n"
       << "  THE LOOP CHAIN - six instances of each, in loop position\n"
       << row("      (a) SVF::process x 6, smoothing on            [D-1]", svfNs) << "\n"
       << row("      (b) MultimodeFilter::processSample x 6        [D-1]", multimodeNs) << "\n"
       << row("      (c) CrossfadingDelayLine::process x 6, 520 ms", delayNs) << "\n"
       << row("      (d) Biquad::process x 6, direct RBJ          [OQ-1]", biquadNs) << "\n"
       << row("      (g) 6 x single-slot ResonatorBank            [OQ-1]", resonatorBankNs) << "\n"
       << row("      (e) DCBlocker::process x 6", dcBlockerNs) << "\n"
       << row("      (f) std::tanh x 6                      [OQ-2 lvr 1]", tanhStdNs) << "\n"
       << row("      (f) FastMath::fastTanh x 6             [OQ-2 lvr 1]", tanhFastNs) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  THE GOVERNOR AND THE LANES\n"
       << row("      (h) EnvelopeFollower RMS, 1 sample/sample", followerNs) << "\n"
       << row("      (i) 12 lanes, dec  1 (rate 1.0 Hz)", lanesDec1Ns) << "\n"
       << row("      (i) 12 lanes, dec  2 (rate 0.03 Hz, DEFAULT)", lanesDec2Ns) << "\n"
       << row("      (i) 12 lanes, dec 17 (rate 0.002 Hz)", lanesDec17Ns) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  THE FIXED PER-SAMPLE OVERHEAD\n"
       << row("      (j) 15 LinearRamp, SETTLED (early-out)", ramps15SettledNs) << "\n"
       << row("      (j) 15 LinearRamp, MOVING  (projected)", ramps15MovingNs) << "\n"
       << row("      (j) 16 LinearRamp, MOVING  (enumeration)", ramps16MovingNs) << "\n"
       << "      tasks.md T004 and plan S14.1 both SAY fifteen and both ENUMERATE sixteen\n"
       << "      (mix + wet trim + 6 input gains + 6 gates + governor + normGain). Both are\n"
       << "      measured; the projection uses the stated fifteen. T006 settles the count\n"
       << "      when it writes the header's member list.\n"
       << "---------------------------------------------------------------------------------\n"
       << row("  (0) harness floor: 512-iteration loop + sink, no DSP", floorNs) << "\n"
       << "      REPORTED, NOT SUBTRACTED. The eight-term projection carries this floor\n"
       << "      eight times over, so the projection OVER-estimates - the correct direction\n"
       << "      for a budget probe.\n"
       << "=================================================================================\n"
       << "  D-1 - the loop filter: SVF (a) vs MultimodeFilter (b)\n"
       << "=================================================================================\n";

    if (d1SvfWins) {
        os << "  VERDICT: (a) SVF is cheaper by " << std::fixed << std::setprecision(1)
           << (multimodeNs - svfNs) << " ns/block (" << std::setprecision(2)
           << (multimodeNs / svfNs) << "x). FR-011 STANDS AS WRITTEN, now with the evidence,\n"
           << "  and the roadmap line-272 write-back (T002) is confirmed by measurement.\n";
    } else {
        os << "  *** (b) MultimodeFilter measured CHEAPER than (a) SVF.\n"
           << "  *** >>> STOP AND SURFACE. DO NOT SWITCH. <<<\n"
           << "  *** MultimodeFilter's cheap path, process(float*, size_t), updates\n"
           << "  *** coefficients once per block and is STRUCTURALLY UNUSABLE inside a\n"
           << "  *** feedback loop, where each output sample depends on the previous one.\n"
           << "  *** An unexpected result here means the MEASUREMENT is wrong (check that\n"
           << "  *** stage (b) really is on processSample and really is at Slope12dB), not\n"
           << "  *** that the topology changed.\n";
    }

    os << "=================================================================================\n"
       << "  OQ-1 realisation - the resonator: direct Biquad (d) vs ResonatorBank (g)\n"
       << "=================================================================================\n";

    if (oq1BiquadWins) {
        os << "  VERDICT: (d) the direct Biquad is cheaper by " << std::fixed
           << std::setprecision(1) << (resonatorBankNs - biquadNs) << " ns/block ("
           << std::setprecision(2) << (resonatorBankNs / biquadNs)
           << "x), as D-3 predicts. FR-013's plain-Biquad realisation STANDS, and it is\n"
           << "  the only one of the two that reaches kMaxResonatorQ = 100 without routing\n"
           << "  through BiquadCoefficients::calculate's kMaxQ = 30 clamp.\n";
    } else {
        os << "  *** (g) six single-slot ResonatorBanks measured CHEAPER than (d).\n"
           << "  *** >>> STOP AND SURFACE with this table. <<< D-3's prediction failed, so\n"
           << "  *** the realisation decision is no longer a formality. Note that (g) still\n"
           << "  *** costs 6 x (3 OnePoleSmoother advances + a 16-iteration skip loop) per\n"
           << "  *** sample, so a cheaper (g) most likely means (d) is measured wrong.\n";
    }

    os << "=================================================================================\n"
       << "  OQ-2 LEVER 1 and the FR-080 PROJECTION (tasks.md T004)\n"
       << "=================================================================================\n"
       << row("  (a) SVF x 6", svfNs) << "\n"
       << row("  + (c) CrossfadingDelayLine x 6", delayNs) << "\n"
       << row("  + (d) Biquad x 6", biquadNs) << "\n"
       << row("  + (e) DCBlocker x 6", dcBlockerNs) << "\n"
       << row("  + (f) std::tanh x 6", tanhStdNs) << "\n"
       << row("  + (h) EnvelopeFollower", followerNs) << "\n"
       << row("  + (i) 12 lanes @ decimation 2", lanesDec2Ns) << "\n"
       << row("  + (j) 15 LinearRamp, moving", ramps15MovingNs) << "\n"
       << row("  = FR-080 PROJECTION, std::tanh", projectedNs) << "\n"
       << row("  = FR-080 PROJECTION, lever 1 (fastTanh)", projectedFastTanhNs) << "\n"
       << row("  GATED BASELINE (160 000 / 1.5)", kGatedBaselineNs) << "\n"
       << row("  ABSOLUTE CEILING (1.5 %/voice)", kAbsoluteCeilingNs) << "\n"
       << "  per-sample: " << std::fixed << std::setprecision(2) << perSampleNs
       << " ns measured vs " << std::setprecision(2) << perSampleBudgetNs
       << " ns budgeted (plan S14.3's 139 ns)\n"
       << "  lever 1 saving: " << std::fixed << std::setprecision(1) << tanhSavingNs
       << " ns/block\n";

    if (fitsAsWritten) {
        os << "  WITHIN THE GATED BASELINE AS WRITTEN: " << std::fixed << std::setprecision(1)
           << (kGatedBaselineNs - projectedNs) << " ns of headroom (" << std::setprecision(2)
           << (100.0 * projectedNs / kGatedBaselineNs) << " % of the line). std::tanh stays;\n"
           << "  OQ-2 lever 1 is NOT taken, and T020's fastTanh error-bound test is NOT\n"
           << "  written. Note this projection EXCLUDES the coupling matrix, the control\n"
           << "  step's own arithmetic and the output stage, and INCLUDES the harness floor\n"
           << "  eight times - it is first-order in both directions.\n";
    } else if (fitsWithLever1) {
        os << "  *** OVER the gated baseline by " << std::fixed << std::setprecision(1)
           << (projectedNs - kGatedBaselineNs) << " ns as written, and WITHIN it with\n"
           << "  *** OQ-2 LEVER 1 applied (" << std::setprecision(1)
           << (kGatedBaselineNs - projectedFastTanhNs) << " ns of headroom).\n"
           << "  *** LEVER 1 IS PRE-AUTHORISED: replace std::tanh with FastMath::fastTanh\n"
           << "  *** (core/fast_math.h:65). FR-042's bound is PRESERVED EXACTLY - the\n"
           << "  *** approximation returns exactly +/-1 beyond +/-3.5 and is within 0.05 %\n"
           << "  *** below - so rung 2 of the boundedness ladder is unchanged. Taking it\n"
           << "  *** OBLIGES T020 to add the error-bound test: |fastTanh(x) - std::tanh(x)|\n"
           << "  *** < 5e-4 over x in [-6, 6] on a 10 001-point grid, and |fastTanh(x)| <= 1\n"
           << "  *** everywhere.\n";
    } else {
        os << "  *** OVER the gated baseline by " << std::fixed << std::setprecision(1)
           << (projectedFastTanhNs - kGatedBaselineNs) << " ns EVEN WITH LEVER 1 APPLIED ("
           << std::setprecision(2) << (projectedFastTanhNs / kGatedBaselineNs) << "x the line).\n"
           << "  *** >>> STOP AND SURFACE THE TABLE ABOVE TO THE USER. <<<\n"
           << "  *** Levers 2 and 3 are already banked (stepped retunes are FR-076's default;\n"
           << "  *** the settled-coupling-smoother carve-out is FR-034's and is not measured\n"
           << "  *** here). Levers 4 (drop the resonator to a second SVF in Bandpass, losing\n"
           << "  *** the RT60 surface, getLoopResonanceRt60, SC-023 entirely and part of\n"
           << "  *** SC-001) and 5 (default numLoops 6 -> 5) are USER DECISIONS taken from\n"
           << "  *** this table. Do not take either on your own.\n";
    }

    os << "  *** NO AGENT MAY lower kMaxLoops, raise the budget, relax a threshold, or\n"
       << "  *** shrink a workload to make this fit. Reduce cost, never move the line\n"
       << "  *** (resonance_drift_network_perf_test.cpp:59-65, inherited verbatim).\n"
       << "=================================================================================\n"
       << "  RECORD the full table, all three verdicts and the chosen levers in the build\n"
       << "  transcript, in plan S14.1, and later in compliance.md.\n"
       << "=================================================================================\n";

    WARN(os.str());
}

// ==============================================================================
// T020 - SC-004 (a)-(f): THE GATED CPU ARMS AND THE CHECKED-IN BASELINES
// ==============================================================================
// Everything below is the SC-004 gate. It sits BESIDE the T004 probe above, in
// the same TU and under the same "[.perf]" hiding, and it is the first thing in
// this file that touches FeedbackEcology itself.
//
// THE MEASUREMENT BASIS IS THE PROBE'S, UNCHANGED: ns per 512-sample block at
//   48 kHz, best-of-25 x 500 blocks after 400 warm-up blocks (bestTrialNs
//   above), one block period = 10 666 667 ns, the 1.5 %/voice ceiling
//   kAbsoluteCeilingNs = 160 000 ns and the gated line
//   kGatedBaselineNs = 160 000 / 1.5 = 106 667 ns.
//
// THE ARMS (tasks.md T020, plan S14.2, spec SC-004):
//   (a) reference patch, six loops awake, wander on          - THIS IS kBaseline
//   (b) six loops, wander off  - within +2 % of (a), A BAND, not an inequality
//   (c) all six loops dormant  - at least 40 % cheaper than (a)
//   (d) numLoops = 1           - REPORTED; the per-loop marginal cost
//   (e) the T004 stage-probe table above - printed, not gated
//   (f) two TRANSITION blocks, at 48 kHz AND at 192 kHz, each gated against the
//       ABSOLUTE per-block ceiling at its rate (160 000 / 40 000 ns) and NOT
//       against kBaseline: a transition may cost more than the steady state,
//       just not more than the block period.
//
// WHY (f) EXISTS AT ALL, AND WHY IT IS THE ARM THAT MATTERS MOST HERE. Arms
//   (a)-(d) are all STEADY STATE, so a per-transition spike is unmeasured by
//   design. A previous revision of this component put
//   CrossfadingDelayLine::reset() - a 131 072-byte std::fill per loop at 48 kHz,
//   524 288 at 192 kHz - on both of the paths (f) times: the sleep edge and the
//   rung-5 trap. One such fill is larger than the whole 8 889 ns control-chunk
//   budget, and five of them land inside ONE 64-sample chunk on a
//   setNumLoops(6 -> 1). S3.1 replaced it with clearLoopAudio()'s O(1) read-mute
//   window (feedback_ecology.h:1636-1654). THIS ARM IS THE MEASUREMENT THAT
//   KEEPS IT O(1). The 192 kHz repeat is where an O(buffer) regression shows
//   first: the buffer is 4x longer while the block period is 4x shorter, so the
//   same defect is 16x more visible against the ceiling.
//
// ------------------------------------------------------------------------------
// THE SECOND DEFINITION OF detail::FeedbackEcologyNonFiniteProbe - READ THIS
// BEFORE TOUCHING EITHER COPY
// ------------------------------------------------------------------------------
// FR-047's trap fires on a non-finite b_i and CANNOT be reached through the
// public API: FR-009 rejects every non-finite setter argument and renderChunk
// step (0) sanitises the dry per channel (feedback_ecology.h:1925-1927). The
// only deterministic way to make it fire - which is what (f)'s second block has
// to do - is the FR-048 fault-injection probe, and tasks.md T020 (f) says so in
// those words: "the block containing a rung-5 trap fire, injected through the
// FR-048 probe".
//
// The probe is DECLARED by the library (feedback_ecology.h:172), BEFRIENDED
// (:1381) and never defined, so each consuming TU defines it. Until T020 that
// was exactly one TU, and FR-048's text says "SC-012 (c) is its only consumer".
// This is the second, and the tension is recorded here rather than hidden:
// T020 (f) is the later and more specific instruction, and there is no other
// deterministic route to a trap fire.
//
// *** THE DEFINITION BELOW MUST STAY TOKEN-IDENTICAL TO THE ONE IN
// *** feedback_ecology_nonfinite_test.cpp:133-148. *** Two definitions of the
// same class in one program are legal ONLY if they consist of the same sequence
// of tokens ([basic.def.odr]); comments are not tokens, so the commentary may
// differ, but every identifier, every argument order and both bodies may not.
// If one copy changes, the other changes in the same commit.
//
// The flag asymmetry is stated too, because it is the thing a reader will worry
// about: the nonfinite TU is the ONE Phase-5 TU compiled -fno-fast-math
// (dsp/tests/CMakeLists.txt:876) and this one is deliberately NOT, so the two
// copies of these inline bodies are compiled under different float semantics.
// That is not a new hazard - every inline function in feedback_ecology.h is
// already compiled both ways in this same binary - and nothing here depends on
// NaN SEMANTICS surviving the merge: this arm asserts a TIME, and it verifies
// the poison actually took by REQUIRING getNonFiniteResetCount() to advance by
// exactly one. If a fast-math build ever swallowed the poison, this arm fails
// loudly as a fixture error instead of timing the wrong block.
// ==============================================================================

namespace Krate::DSP::detail {

struct FeedbackEcologyNonFiniteProbe {
    static void poisonDcBlocker(FeedbackEcology& fe, std::size_t loop, float nonFinite) noexcept {
        static_cast<void>(fe.loops_[loop].dcBlocker.process(nonFinite));
    }

    static void poisonFollower(FeedbackEcology& fe, float nonFinite) noexcept {
        static_cast<void>(fe.follower_.processSample(nonFinite));
    }
};

}  // namespace Krate::DSP::detail

namespace {
namespace cpu {

using Probe = Krate::DSP::detail::FeedbackEcologyNonFiniteProbe;

// =============================================================================
// The probe's local tables vs the shipped header (T004's open item, closed)
// =============================================================================
//
// T004 spelled the FR-013/FR-022/FR-052/FR-053 default tables LOCALLY because
// FeedbackEcology did not exist yet, and left a note: "when T006 writes the
// header, these six-row tables must match it exactly". The header exists now and
// this TU includes it, so the note becomes a compile-time clause. A divergence
// here would mean the T004 projection above priced a component nobody ships.

static_assert(kNumLoops == FeedbackEcology::kMaxLoops,
              "the probe's loop count and FR-010's kMaxLoops must agree");
static_assert(kControlChunk == FeedbackEcology::kControlChunkSamples,
              "the probe's control chunk and FR-007's grid must agree");
static_assert(kDefaultLoopDelayMs == FeedbackEcology::kDefaultLoopDelayMs,
              "FR-022's delay table drifted from the probe's copy");
static_assert(kDefaultLoopCutoffHz == FeedbackEcology::kDefaultLoopCutoffHz,
              "FR-053's cutoff table drifted from the probe's copy");
static_assert(kDefaultLoopResonanceHz == FeedbackEcology::kDefaultLoopResonanceHz,
              "FR-013's resonance table drifted from the probe's copy");
static_assert(kDefaultDelayWanderFraction == FeedbackEcology::kDefaultDelayWanderFraction,
              "FR-052's wander table drifted from the probe's copy");
static_assert(kDefaultResonanceRt60 == FeedbackEcology::kDefaultResonanceRt60,
              "FR-013's default RT60 drifted from the probe's copy");
static_assert(kDefaultFilterQ == FeedbackEcology::kDefaultFilterQ,
              "FR-012's default filter Q drifted from the probe's copy");
static_assert(kMaxDelaySeconds == FeedbackEcology::kMaxDelaySeconds,
              "FR-020's line length drifted from the probe's copy");
static_assert(kCrossfadeMs == FeedbackEcology::kCrossfadeMs,
              "FR-020's crossfade time drifted from the probe's copy");
static_assert(kGainRampMs == FeedbackEcology::kGainRampMs,
              "the 50 ms gain ramp drifted from the probe's copy");
static_assert(static_cast<std::size_t>(kDecimationSlow) == FeedbackEcology::kMaxLaneDecimation,
              "FR-055's slowest decimation drifted from the probe's copy");

// =============================================================================
// The second rate, and its ceiling (SC-004 (f))
// =============================================================================

constexpr double kSr192 = 192000.0;

/// Wall-clock period of one 512-sample block at 192 kHz, in nanoseconds.
constexpr double kBlockPeriod192Ns = (static_cast<double>(kBlockSize) / kSr192) * 1.0e9;

/// The absolute per-block ceiling at 192 kHz is the SAME nanosecond figure as at
/// 48 kHz. Ruled 2026-09-15: an earlier draft set it to 1.5 % of the 192 kHz
/// block period (40 000 ns), but a 512-sample block is four times shorter in
/// wall time at 192 kHz for the same per-sample work, so that clause demanded
/// the component be about twice as cheap per sample as it is at 48 kHz. It never
/// passed (Phase 5's own isolated run read 69 100 / 83 700 ns on the two arms;
/// the steady block at 192 kHz is ~76 500 ns), and the 13 September compliance
/// record that said it did was wrong. The 1.5 %-per-voice budget is defined at
/// 48 kHz; the 192 kHz arms exist for the four-times-larger delay buffers, and
/// what they gate is the OVERHEAD (kTransitionOverheadFactor below).
constexpr double kAbsoluteCeiling192Ns = kAbsoluteCeilingNs;

/// SC-004 (f)'s O(buffer) detector, at every rate: a transition block may not
/// cost more than this factor times a steady block in the same state at the
/// same rate. A regression of clearLoopAudio to a buffer fill (786 KB at
/// 48 kHz, 3.1 MB at 192 kHz inside one block) is a multiple, not 10 %.
/// Measured overheads are NEGATIVE at both rates (the block after a sleep edge
/// runs fewer loops), so 1.1 is a detector, not a budget.
constexpr double kTransitionOverheadFactor = 1.1;

/// The anti-no-op floor, spelled once so the clauses and the report agree
/// (resonance_drift_network_perf_test.cpp:2907-2909).
constexpr double kNoOpFloorNs = kAbsoluteCeilingNs / 50.0;
static_assert(kNoOpFloorNs > 0.0 && kNoOpFloorNs < kGatedBaselineNs,
              "the floor is a floor, not a second ceiling");

/// SC-004 (b)'s band. NOT a bare inequality: FR-056 keeps the lanes advancing
/// and only zeroes the depth term the mapping applies, so the true difference is
/// a handful of control-rate multiplies inside a best-of-25 x 500-block
/// measurement - well under run-to-run noise. An untoleranced comparison fails
/// on a scheduling accident, not on a defect. That is the Phase-3 precedent,
/// where the same arm's ">= 10 % saving with wander off" was structurally
/// unreachable once the Dormancy rule fixed that lanes keep advancing, and had
/// to be amended mid-build.
constexpr double kWanderOffBandFraction = 0.02;

/// SC-004 (c). The observable consequence of FR-062's skipped chain and the only
/// thing that proves dormancy is not cosmetic. NEVER RELAX THIS.
constexpr double kDormantSavingFloor = 0.40;

// -----------------------------------------------------------------------------
// THE THREE BASELINES
//
// *** PROVISIONAL: PROJECTIONS, NOT MEASUREMENTS. ***
//
// No isolated run of THIS case exists yet - the case is being written, and the
// implementing agent is forbidden to time anything (a figure taken beside a
// build or another agent is not evidence). So these are projections, exactly as
// the Phase-3 precedent shipped its first revision
// (resonance_drift_network_perf_test.cpp:2917-2947), and the case prints
// copy-pasteable transcription lines so the first isolated run replaces them.
//
// THE PROJECTION, per 512-sample block at 48 kHz, six loops, from plan S14.3's
// per-sample chain and the Phase-3 measured lane figure (48 lanes at decimation
// 2 measured 12 000 ns, so 12 lanes is ~3 000):
//
//     SVF::process x 6, smoothing live                      ~ 12 300 ns
//   + CrossfadingDelayLine::process x 6, 520 ms lines        ~ 15 000 ns
//   + Biquad::process x 6, direct RBJ                        ~  4 600 ns
//   + DCBlocker::process x 6                                 ~  3 000 ns
//   + the FR-042 soft clip x 6                               ~  4 600 ns  (*)
//   + EnvelopeFollower RMS, one processSample per sample     ~  1 500 ns
//   + 12 BrownianDrift lanes at decimation 2                 ~  3 000 ns
//   + the 15-ramp bank                                       ~  4 000 ns
//   + the FR-031 input sum, 36 multiply-adds per sample      ~  5 500 ns
//   + the FR-015/FR-046/FR-072 output stage                  ~  1 000 ns
//   = ~ 54 500 ns/block, 77 % of the 71 111 ns gated line
//
// (*) *** AND THIS IS THE LINE THE FIRST RUN WILL DECIDE. *** The (*) term is
// the projection WITH OQ-2 LEVER 1 APPLIED - FastMath::fastTanh, ~1.5 ns a call.
// Six std::tanh per sample is tens of ns each on MSVC: at ~17 ns the term is
// ~52 000 ns/block on its own and the total is ~102 000 ns - INSIDE the
// 106 666 ns absolute ceiling but WAY over the 71 111 ns gated line, i.e. not a
// transcribable baseline at all (102 000 x 1.5 = 153 000). Plan S14.3 predicts
// exactly this and R-2 names it the risk.
//
// THE LEVER IS NOT APPLIED BY THIS TASK, AND THAT IS DELIBERATE. tasks.md T020
// authorises it "only if (a) misses budget", and no measurement exists yet:
// applying it now would be a cost decision taken from a guess, which is the one
// thing FR-080's stop-and-surface rule forbids in both directions. The header
// still calls std::tanh (feedback_ecology.h:1995). If the first isolated run
// puts (a) over kGatedBaselineNs, the report below prints the whole ladder and
// the next step is fixed and pre-authorised:
//
//   1. std::tanh -> FastMath::fastTanh (core/fast_math.h:65) at
//      feedback_ecology.h:1995, PLUS its own error-bound test in this TU:
//      |fastTanh(x) - std::tanh(x)| < 5e-4 over x in [-6, 6] on a 10 001-point
//      grid, and |fastTanh(x)| <= 1 everywhere. FR-042's bound is preserved
//      EXACTLY - the approximation returns exactly +/-1 beyond +/-3.5 - so rung 2
//      of the boundedness ladder is unchanged.
//   2. Stepped resonator retunes: ALREADY the default (FR-076), banked here.
//   3. The settled-coupling-smoother carve-out: already in updateControl().
//   Then STOP AND SURFACE. Levers 4 (drop the resonator to a second SVF in
//   Bandpass, losing the RT60 surface, kMaxResonatorQ = 100 and SC-023 entirely)
//   and 5 (default numLoops 6 -> 5) are USER decisions taken from the table.
//
// TRANSCRIBING A MEASURED FIGURE OVER A PROJECTION IS ALWAYS CORRECT. RAISING
// one so a REQUIRE passes is forbidden. Take the transcription from a
// DISTRIBUTION, not one sample - this repo has measured a 6.4 % run-to-run
// spread and ~14 % session drift on unchanged code - and use the LOWEST of
// several isolated runs, so the clause stays as tight as the data allows.
// -----------------------------------------------------------------------------

// TRANSCRIBED 2026-09-13 from an isolated run with the process pinned to the
// P-cores (affinity 0xFFFF on the i9-13900HX; unpinned, every shipped perf
// baseline in this suite fails by the same ~1.8x, which is E-core scheduling,
// not code - see resonance_drift_network_perf_test.cpp's provenance note):
// (a) 94 106.4, (b) 77 370.6, (c) 9 873.0, (d) 23 336.0 ns/block. The 54 500 /
// 54 500 / 15 000 projections they replace were made before the two engineering
// levers landed and against the pre-amendment 1 % budget.

/// (a) reference patch: six loops awake, the FR-013/FR-022/FR-033/FR-052/FR-053
/// default tables, wander on at kDefaultWanderRateHz, mix = 1, wetGain = 0 dB.
constexpr double kBaselineReferenceNs = 94106.4;

/// (b) (a) with setWanderEnabled(false). FR-056 keeps the lanes advancing and
/// only zeroes the depth term; measured 17.8 % BELOW (a) because a zero depth
/// also stops the cutoff pushes to the SVF (kCutoffPushRelative), so the SVF
/// smoother sits settled - the one-sided SC-004 (b) band admits any saving.
constexpr double kBaselineWanderOffNs = 77370.6;

/// (c) all six loops dormant: the six loop chains and the FR-031 input sum are
/// gone (FR-062's skip), the lanes, the ramp bank, the follower feed and the
/// output stage remain - ~12 000 of the ~54 500, i.e. a ~78 % saving. The figure
/// is projected HIGH (15 000) rather than at the estimate, because a baseline
/// that is too LOW fails as a phantom regression, while SC-004 (c)'s saving
/// clause below is measured against (a) and is unaffected by it.
constexpr double kBaselineAllDormantNs = 9873.0;

// -----------------------------------------------------------------------------
// The two compile-time clauses, per gated arm. THIS is what evaluates SC-004's
// absolute ceiling on EVERY CI leg even though the case itself never runs there
// (plan S14, spec SC-004, the noise_organism_perf_test.cpp:1560-1575 idiom).
//
// They are two DIFFERENT clauses, not one restated. The ceiling binds the
// measurement to 106 666 ns transitively on every machine (measured <= baseline
// x 1.5 <= ceiling). The floor - 2 133 ns - catches a baseline recorded from a
// run that did nothing: an unprepared FeedbackEcology fills silence and advances
// no state (the FR-003 guard ladder, feedback_ecology.h:812-817), and a baseline
// taken from one would satisfy the ceiling forever while measuring nothing.
// -----------------------------------------------------------------------------
static_assert(kBaselineReferenceNs * kRegressionFactor <= kAbsoluteCeilingNs,
              "SC-004: over the 1.5 %/voice ceiling");
static_assert(kBaselineReferenceNs >= kAbsoluteCeilingNs / 50.0,
              "SC-004: baseline implausibly low");

static_assert(kBaselineWanderOffNs * kRegressionFactor <= kAbsoluteCeilingNs,
              "SC-004 (b): over the 1.5 %/voice ceiling");
static_assert(kBaselineWanderOffNs >= kAbsoluteCeilingNs / 50.0,
              "SC-004 (b): baseline implausibly low");

static_assert(kBaselineAllDormantNs * kRegressionFactor <= kAbsoluteCeilingNs,
              "SC-004 (c): over the 1.5 %/voice ceiling");
static_assert(kBaselineAllDormantNs >= kAbsoluteCeilingNs / 50.0,
              "SC-004 (c): baseline implausibly low");

// =============================================================================
// Fixture: the reference patch, the reference drive, and the four steady arms
// =============================================================================

/// Pinned so the twelve wander lanes sit on the same trajectories run to run
/// (FR-054) and the figures are reproducible. It is the phase's fixture seed,
/// the one feedback_ecology_test.cpp's makeReference and
/// feedback_ecology_nonfinite_test.cpp both use.
constexpr std::uint32_t kCpuSeed = 0x5EEDu;

/// The Success Criteria's reference drive: white noise at -12 dBFS RMS - RMS,
/// not peak, the two differ by 10-12 dB for white noise. The level is part of
/// the measurement: std::tanh's cost is data-dependent, so a drive at the wrong
/// level prices a component nobody renders.
constexpr float kDriveDbfs = -12.0f;

/// Xorshift32::nextFloat() is uniform on [-1, +1] (core/random.h:59-63), so its
/// RMS is 1/sqrt(3) and a target RMS is reached by scaling with sqrt(3).
constexpr float kWhiteRmsScale = 1.7320508f;

/// Blocks rendered OUTSIDE the timed region before a steady arm is inspected.
/// Three things must have LANDED before either a figure or a precondition means
/// anything: the FR-061 gate ramps (kGainRampMs = 50 ms), the six delay lines
/// filling and circulating (the longest is 449 ms), and - for arm (c) - all six
/// FR-063 sleep edges. 200 blocks is 2.13 s at 48 kHz, ~43x the gate ramp and
/// ~4.7 circulations of the longest line. bestTrialNs runs its own 400 warm-up
/// blocks on top of this.
constexpr int kSettleBlocks = 200;
static_assert(static_cast<double>(kSettleBlocks) * static_cast<double>(kBlockSize) / kSr48 >= 2.0,
              "the settle window must cover several circulations of the 449 ms loop");

/// Blocks fully scanned, sample by sample and on both channels, before and again
/// after the timed region.
constexpr int kInspectBlocks = 20;

/// The measured level of this patch, for the anti-silence precondition below.
/// NOT the Phase-3 precedent's -60 dBFS: the reference patch is quiet BY DESIGN
/// - every default loop carries a Q ~ 100 resonator whose equivalent noise
/// bandwidth is 3.3-18.8 Hz out of 24 kHz, so a broadband drive reaches the
/// loops ~30 dB down - and spec SC-017's T015 measurement records RMS
/// 0.0014655 = -56.7 dBFS for exactly this configuration. A -60 dBFS floor
/// would sit 3.3 dB under the measured level and would fail on the day the
/// governor moved. -80 dBFS is an ANTI-SILENCE floor, which is all this
/// precondition is for; the measured figure is reported every run.
constexpr double kAntiSilenceDbfs = -80.0;

/// -12 dBFS RMS white noise. Deliberately NOT the outer namespace's fillWhite,
/// whose 0.25 scale is a stage-probe excitation and not this phase's drive
/// level.
void fillDrive(float* buffer, std::size_t numSamples, std::uint32_t seed) noexcept
{
    Xorshift32 rng{seed};
    const float scale = dbToGain(kDriveDbfs) * kWhiteRmsScale;
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = rng.nextFloat() * scale;
    }
}

/// @brief Build a non-finite float from its bit pattern through a volatile sink.
///
/// The volatile READ is the sink: it is what stops the constant being folded
/// back into the memcpy at compile time, which is how a -ffast-math build turns
/// a NaN literal into a finite number. This TU is NOT in the -fno-fast-math
/// block, so std::numeric_limits<float>::quiet_NaN() is not usable here at all.
/// Idiom transcribed from feedback_ecology_nonfinite_test.cpp:203-211.
[[nodiscard]] float bitNaN() noexcept
{
    volatile std::uint32_t sink = 0x7FC00000u;
    const std::uint32_t materialized = sink;
    float out = 0.0f;
    std::memcpy(&out, &materialized, sizeof(out));
    return out;
}

enum class Arm : std::uint8_t { Reference, WanderOff, AllDormant, OneLoop };

[[nodiscard]] const char* armLabel(Arm arm)
{
    switch (arm) {
        case Arm::Reference: return "(a) reference: 6 loops awake, wander on, mix 1";
        case Arm::WanderOff: return "(b) wander off: (a) + setWanderEnabled(false)";
        case Arm::AllDormant: return "(c) all dormant: (a) + every loop dormant";
        case Arm::OneLoop: return "(d) numLoops = 1: the per-loop marginal cost";
    }
    return "(unknown arm)";
}

/// The reference patch, configured IN PLACE.
///
/// THE ORDER IS LOAD-BEARING and is feedback_ecology_test.cpp:1282-1289's, not a
/// re-derivation: setMix / setWetGain come AFTER prepare(), because prepare()
/// step 7's applyDefaults() restores kDefaultMix = 0.15f on EVERY call
/// (FR-005 (c)); reset() comes LAST because it snaps every per-sample ramp and
/// every control-rate smoother to the values just set and re-derives all twelve
/// lane streams from the seed, which is what makes the patch exact from
/// sample 0. A reset() called before setMix would leave the mix ramp gliding
/// through the first 20 ms of the measurement.
void configureArm(FeedbackEcology& fe, Arm arm, double fs) noexcept
{
    const std::size_t loops =
        (arm == Arm::OneLoop) ? std::size_t{1} : FeedbackEcology::kMaxLoops;

    fe.setSeed(kCpuSeed);
    // Designated initialisers: Clang errors on narrowing in brace init where
    // MSVC does not.
    fe.prepare(fs, FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlockSize,
                                                  .numLoops = loops});
    fe.setMix(1.0f);      // measure the WET path, not the crossfade
    fe.setWetGain(0.0f);  // kDefaultWetGainDb, stated rather than assumed

    if (arm == Arm::WanderOff) {
        fe.setWanderEnabled(false);
    }
    if (arm == Arm::AllDormant) {
        for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
            fe.setLoopDormant(i, true);
        }
    }

    fe.reset();
}

/// How many loops are actually rendering (FR-062's chain-skip flag).
[[nodiscard]] std::size_t activeCount(const FeedbackEcology& fe) noexcept
{
    std::size_t n = 0;
    for (std::size_t i = 0; i < FeedbackEcology::kMaxLoops; ++i) {
        if (fe.isLoopEngineActive(i)) ++n;
    }
    return n;
}

/// What the arm's audio was, so a cheap figure can be attributed.
struct ArmCheck {
    bool allFinite = true;
    bool allExactZero = true;
    double sumSq = 0.0;
    std::size_t samples = 0;
    std::size_t activeLoops = 0;
    std::uint32_t clampEngagements = 0;
    std::uint32_t nonFiniteResets = 0;
};

void scanBlock(const float* outL, const float* outR, ArmCheck& check) noexcept
{
    for (std::size_t s = 0; s < kBlockSize; ++s) {
        const float l = outL[s];
        const float r = outR[s];
        // detail::isFinite, never std::isnan / std::isinf - this TU builds
        // -ffast-math on the macOS and Linux legs (core/db_utils.h).
        check.allFinite = check.allFinite && detail::isFinite(l) && detail::isFinite(r);
        check.allExactZero = check.allExactZero && (l == 0.0f) && (r == 0.0f);
        check.sumSq += static_cast<double>(l) * static_cast<double>(l)
                       + static_cast<double>(r) * static_cast<double>(r);
        check.samples += 2u;
    }
}

[[nodiscard]] double checkRmsDbfs(const ArmCheck& check)
{
    if (check.samples == 0u || check.sumSq <= 0.0) {
        return -200.0;
    }
    const double rms = std::sqrt(check.sumSq / static_cast<double>(check.samples));
    return 20.0 * std::log10(rms);
}

/// @brief ns per 512-sample block for one steady arm, best-of-25 x 500 after 400
///        warm-up blocks, with the arm's preconditions checked BEFORE the timed
///        region and re-checked after it.
[[nodiscard]] double measureArm(Arm arm, double& sink, ArmCheck& check)
{
    FeedbackEcology fe;
    configureArm(fe, arm, kSr48);

    std::array<float, kBlockSize> inL{};
    std::array<float, kBlockSize> inR{};
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    fillDrive(inL.data(), kBlockSize, 0x0C0FFEEu);
    fillDrive(inR.data(), kBlockSize, 0x0FFEE0Cu);

    for (int b = 0; b < kSettleBlocks; ++b) {
        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
    }

    for (int b = 0; b < kInspectBlocks; ++b) {
        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        scanBlock(outL.data(), outR.data(), check);
    }
    check.activeLoops = activeCount(fe);

    const double ns = bestTrialNs([&]() noexcept {
        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        // Read so the render cannot be dead-coded away. Not a result.
        sink += static_cast<double>(outL[0]) + static_cast<double>(outR[kBlockSize - 1]);
    });

    // Re-check AFTER the timed region: 25 x 500 + 400 more blocks have gone
    // through the same state, and a property that held only at second 2 is not
    // the property SC-004 (c) asks for.
    for (int b = 0; b < kInspectBlocks; ++b) {
        fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        scanBlock(outL.data(), outR.data(), check);
    }
    // The union, not the snapshot: a loop that woke during the timed region
    // would make the pre-timing count a lie.
    check.activeLoops = std::max(check.activeLoops, activeCount(fe));
    check.clampEngagements = fe.getClampEngagementCount();
    check.nonFiniteResets = fe.getNonFiniteResetCount();

    return ns;
}

// =============================================================================
// SC-004 (f) - the two transition blocks, at both rates
// =============================================================================
//
// These CANNOT use bestTrialNs: it times the same block shape 500 times in a
// row, and a transition happens once. Each trial here re-establishes the
// precondition from a settled steady state and times exactly ONE 512-sample
// block, and the arm reports the MINIMUM over kTransitionTrials trials - the
// same best-of discipline, one block at a time. steady_clock resolves ~100 ns
// against a block that costs tens of microseconds, so single-block timing is
// sound at this scale; the figure is still a minimum, never a mean.
//
// *** WHAT THE 192 kHz CEILING ACTUALLY DEMANDS - STATED, NOT DISCOVERED IN A
// *** FAILURE. *** A 512-sample block costs the SAME wall-clock at 192 kHz as at
// 48 kHz: the per-sample chain is rate-independent and the control grid is 64
// samples at every rate, so the same 512 samples of work are done either way.
// The block PERIOD, however, is four times shorter, so SC-004 (f)'s 26 667 ns
// ceiling at 192 kHz is a FOUR TIMES STRICTER budget on the whole component than
// the 48 kHz arms carry - it passes only if the reference patch renders 512
// samples in under 26 667 ns, i.e. under 0.25 % of a core at 48 kHz. That is the
// criterion as spec SC-004 (f) and plan S14.2 write it, and this arm implements
// it literally rather than softening it. If it misses, the report below says in
// the same breath WHICH of the two possible findings it is - the component's
// steady cost at 192 kHz (a budget-scope question, and a USER decision) or the
// transition OVERHEAD (the O(buffer) regression this arm was built to catch) -
// so the miss is attributable on sight instead of being argued about. The
// overhead is reported at both rates for exactly that reason.

enum class Transition : std::uint8_t { SleepEdge, TrapFire };

[[nodiscard]] const char* transitionLabel(Transition t)
{
    switch (t) {
        case Transition::SleepEdge: return "(f1) simultaneous 5-loop sleep edge, setNumLoops(6->1)";
        case Transition::TrapFire: return "(f2) rung-5 trap fire, FR-048 probe on loop 2";
    }
    return "(unknown transition)";
}

constexpr int kTransitionTrials = 25;

/// Seconds of settled render before each trial's transition. Two seconds is
/// ~4.5 circulations of the 449 ms loop at any rate, and 40x the 50 ms gate
/// ramp - and it is expressed in SECONDS, not blocks, precisely so the 192 kHz
/// arm settles for as long as the 48 kHz one instead of a quarter as long.
constexpr double kTransitionSettleSeconds = 2.0;

/// Extra blocks scanned past the gate ramp's own length while hunting the sleep
/// edge, so a rate whose ramp does not divide the block size still finds it.
constexpr int kTransitionScanExtraBlocks = 8;

/// The loop the FR-048 probe poisons. 2 is the plan's choice (S12.2), the same
/// loop feedback_ecology_nonfinite_test.cpp:172 uses.
constexpr std::size_t kTrapLoop = 2;

[[nodiscard]] int blocksForSeconds(double seconds, double fs) noexcept
{
    return static_cast<int>(std::ceil(seconds * fs / static_cast<double>(kBlockSize)));
}

struct TransitionResult {
    double bestNs = -1.0;    ///< the gated figure: minimum over the trials
    double steadyNs = -1.0;  ///< a steady block at the same rate, for attribution
    int trialsFound = 0;
    int blockIndex = -1;  ///< blocks after the edge was commanded, best trial
    std::size_t activeBefore = 0;
    std::size_t activeAfter = 0;
    std::uint32_t resetDeltaMin = 0;
    std::uint32_t resetDeltaMax = 0;
    bool allFinite = true;
};

[[nodiscard]] double timeOneBlock(FeedbackEcology& fe, const float* inL, const float* inR,
                                  float* outL, float* outR, double& sink) noexcept
{
    const auto start = std::chrono::steady_clock::now();
    fe.processBlock(inL, inR, outL, outR, kBlockSize);
    const auto end = std::chrono::steady_clock::now();
    // Read so the render cannot be dead-coded away. Not a result.
    sink += static_cast<double>(outL[0]) + static_cast<double>(outR[kBlockSize - 1]);
    return std::chrono::duration<double, std::nano>(end - start).count();
}

[[nodiscard]] TransitionResult measureTransition(Transition kind, double fs, double& sink)
{
    TransitionResult out{};
    out.resetDeltaMin = 0xFFFFFFFFu;

    FeedbackEcology fe;
    configureArm(fe, Arm::Reference, fs);

    std::array<float, kBlockSize> inL{};
    std::array<float, kBlockSize> inR{};
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    fillDrive(inL.data(), kBlockSize, 0x0C0FFEEu);
    fillDrive(inR.data(), kBlockSize, 0x0FFEE0Cu);

    const int settle = blocksForSeconds(kTransitionSettleSeconds, fs);
    const int scan = blocksForSeconds(static_cast<double>(kGainRampMs) * 1.0e-3, fs)
                     + kTransitionScanExtraBlocks;

    for (int trial = 0; trial < kTransitionTrials; ++trial) {
        // Back to six awake loops, from sample 0: reset() snaps every gate to
        // gateSteady(), restores engineActive, and clears BOTH counters
        // (feedback_ecology.h:715-767), so each trial starts identical.
        fe.setNumLoops(FeedbackEcology::kMaxLoops);
        fe.reset();
        for (int b = 0; b < settle; ++b) {
            fe.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        }

        // A steady block at the SAME rate on the SAME warmed state. REPORTED,
        // never gated: it is what lets a miss be attributed to the transition
        // itself rather than to the component's steady cost at this rate.
        const double steady = timeOneBlock(fe, inL.data(), inR.data(), outL.data(), outR.data(),
                                           sink);
        if (out.steadyNs < 0.0 || steady < out.steadyNs) {
            out.steadyNs = steady;
        }

        const std::size_t before = activeCount(fe);
        const std::uint32_t resetsBefore = fe.getNonFiniteResetCount();

        double ns = -1.0;
        int index = -1;
        std::size_t after = before;

        if (kind == Transition::SleepEdge) {
            // FR-075: five gates re-target to 0 at kGainRampMs. They all start
            // at exactly 1.0f and share an increment, so all five reach zero on
            // the SAME control step and all five clearLoopAudio calls land
            // inside ONE 64-sample chunk - which is the block this hunts.
            fe.setNumLoops(1);
            for (int b = 0; b < scan; ++b) {
                const double blockNs =
                    timeOneBlock(fe, inL.data(), inR.data(), outL.data(), outR.data(), sink);
                const std::size_t nowActive = activeCount(fe);
                if (nowActive < before) {
                    ns = blockNs;
                    index = b;
                    after = nowActive;
                    break;
                }
            }
        } else {
            // The poison lands in loop 2's DCBlocker y1_, so its very next
            // sample produces a non-finite b and rung 5 fires ONCE: the trap
            // clears the chain (dcBlocker.reset() included), so nothing in the
            // rest of the block can fire it again.
            Probe::poisonDcBlocker(fe, kTrapLoop, bitNaN());
            ns = timeOneBlock(fe, inL.data(), inR.data(), outL.data(), outR.data(), sink);
            index = 0;
            after = activeCount(fe);
        }

        if (ns < 0.0) {
            continue;  // no edge inside the scan window - a FIXTURE failure, asserted below
        }

        ++out.trialsFound;
        for (std::size_t s = 0; s < kBlockSize; ++s) {
            out.allFinite =
                out.allFinite && detail::isFinite(outL[s]) && detail::isFinite(outR[s]);
        }

        const std::uint32_t delta = fe.getNonFiniteResetCount() - resetsBefore;
        out.resetDeltaMin = std::min(out.resetDeltaMin, delta);
        out.resetDeltaMax = std::max(out.resetDeltaMax, delta);

        if (out.bestNs < 0.0 || ns < out.bestNs) {
            out.bestNs = ns;
            out.blockIndex = index;
            out.activeBefore = before;
            out.activeAfter = after;
        }
    }

    if (out.trialsFound == 0) {
        out.resetDeltaMin = 0;
    }
    return out;
}

// =============================================================================
// Reporting
// =============================================================================

/// row() above prices everything against the 48 kHz block period. This one takes
/// the period, so a 192 kHz figure reports its own percentage rather than a
/// number four times too small.
[[nodiscard]] std::string rowAt(const std::string& label, double nsPerBlock, double blockPeriodNs)
{
    std::ostringstream os;
    os << std::left << std::setw(56) << label << std::right << std::fixed << std::setprecision(1)
       << std::setw(12) << nsPerBlock << " ns/block   " << std::setprecision(4) << std::setw(9)
       << (100.0 * nsPerBlock / blockPeriodNs) << " % of one core";
    return os.str();
}

/// One copy-pasteable baseline line, so a measured figure replaces a projection
/// by transcription rather than by arithmetic.
[[nodiscard]] std::string baselineLine(const std::string& name, double measuredNs)
{
    std::ostringstream os;
    os << "      constexpr double " << std::left << std::setw(24) << name << std::right << " = "
       << std::fixed << std::setprecision(1) << std::setw(10) << measuredNs << ";";
    return os.str();
}

struct ArmRow {
    Arm arm;
    double measuredNs;
    double baselineNs;  ///< <= 0 for a REPORTED arm that carries no baseline
    const char* constantName;
};

struct TransitionRow {
    Transition kind;
    double sampleRate;
    double ceilingNs;
    double blockPeriodNs;
    TransitionResult result;
};

}  // namespace cpu
}  // namespace

// =============================================================================
// T020 - SC-004 (a)-(f), the gate
// =============================================================================

TEST_CASE("FeedbackEcology_CpuBudget", "[feedback_ecology][.perf]")
{
    double sink = 0.0;

    cpu::ArmCheck refCheck;
    cpu::ArmCheck wanderCheck;
    cpu::ArmCheck dormantCheck;
    cpu::ArmCheck oneLoopCheck;

    const double nsRef = cpu::measureArm(cpu::Arm::Reference, sink, refCheck);
    const double nsWanderOff = cpu::measureArm(cpu::Arm::WanderOff, sink, wanderCheck);
    const double nsDormant = cpu::measureArm(cpu::Arm::AllDormant, sink, dormantCheck);
    const double nsOneLoop = cpu::measureArm(cpu::Arm::OneLoop, sink, oneLoopCheck);

    const std::array<cpu::TransitionRow, 4> transitions{
        cpu::TransitionRow{.kind = cpu::Transition::SleepEdge,
                           .sampleRate = kSr48,
                           .ceilingNs = kAbsoluteCeilingNs,
                           .blockPeriodNs = kBlockPeriodNs,
                           .result = cpu::measureTransition(cpu::Transition::SleepEdge, kSr48,
                                                            sink)},
        cpu::TransitionRow{.kind = cpu::Transition::TrapFire,
                           .sampleRate = kSr48,
                           .ceilingNs = kAbsoluteCeilingNs,
                           .blockPeriodNs = kBlockPeriodNs,
                           .result = cpu::measureTransition(cpu::Transition::TrapFire, kSr48,
                                                            sink)},
        cpu::TransitionRow{.kind = cpu::Transition::SleepEdge,
                           .sampleRate = cpu::kSr192,
                           .ceilingNs = cpu::kAbsoluteCeiling192Ns,
                           .blockPeriodNs = cpu::kBlockPeriod192Ns,
                           .result = cpu::measureTransition(cpu::Transition::SleepEdge, cpu::kSr192,
                                                            sink)},
        cpu::TransitionRow{.kind = cpu::Transition::TrapFire,
                           .sampleRate = cpu::kSr192,
                           .ceilingNs = cpu::kAbsoluteCeiling192Ns,
                           .blockPeriodNs = cpu::kBlockPeriod192Ns,
                           .result = cpu::measureTransition(cpu::Transition::TrapFire, cpu::kSr192,
                                                            sink)}};

    // The sink is read so no arm can be dead-coded away. It is not a result.
    REQUIRE(detail::isFinite(sink));
    for (const double ns : {nsRef, nsWanderOff, nsDormant, nsOneLoop}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    const double refRmsDb = cpu::checkRmsDbfs(refCheck);
    const double wanderRmsDb = cpu::checkRmsDbfs(wanderCheck);
    const double oneLoopRmsDb = cpu::checkRmsDbfs(oneLoopCheck);

    const double wanderDeltaNs = nsWanderOff - nsRef;
    const double wanderDeltaFraction = (nsRef > 0.0) ? (wanderDeltaNs / nsRef) : 0.0;
    const double dormantSavingNs = nsRef - nsDormant;
    const double dormantSavingFraction = (nsRef > 0.0) ? (dormantSavingNs / nsRef) : 0.0;
    const double marginalPerLoopNs =
        (nsRef - nsOneLoop) / static_cast<double>(FeedbackEcology::kMaxLoops - 1u);

    // -------------------------------------------------------------------------
    // Report FIRST, assert second: on a miss the failure has to carry the
    // evidence the escalation decision is taken from.
    // -------------------------------------------------------------------------
    const std::array<cpu::ArmRow, 4> reported{
        cpu::ArmRow{.arm = cpu::Arm::Reference,
                    .measuredNs = nsRef,
                    .baselineNs = cpu::kBaselineReferenceNs,
                    .constantName = "kBaselineReferenceNs"},
        cpu::ArmRow{.arm = cpu::Arm::WanderOff,
                    .measuredNs = nsWanderOff,
                    .baselineNs = cpu::kBaselineWanderOffNs,
                    .constantName = "kBaselineWanderOffNs"},
        cpu::ArmRow{.arm = cpu::Arm::AllDormant,
                    .measuredNs = nsDormant,
                    .baselineNs = cpu::kBaselineAllDormantNs,
                    .constantName = "kBaselineAllDormantNs"},
        cpu::ArmRow{.arm = cpu::Arm::OneLoop,
                    .measuredNs = nsOneLoop,
                    .baselineNs = -1.0,
                    .constantName = "(reported, no baseline)"}};

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  FeedbackEcology SC-004 CPU BUDGET   (48 kHz, 512-sample blocks)\n"
       << "  specs/vorago-phase5-feedback-ecology, SC-004 / FR-080, tasks.md T020\n"
       << "  best-of-" << kTrials << " x " << kBlocksPerTrial << " blocks after " << kWarmupBlocks
       << " warm-up, " << cpu::kSettleBlocks << " settle blocks before each\n"
       << "  reference patch: 6 loops, default tables, wander on, mix 1, wetGain 0 dB,\n"
       << "  drive white noise at " << std::fixed << std::setprecision(1) << cpu::kDriveDbfs
       << " dBFS RMS on both channels, seed 0x5EED\n"
       << "=================================================================================\n";

    for (const cpu::ArmRow& r : reported) {
        os << row(cpu::armLabel(r.arm), r.measuredNs) << "\n";
        if (r.baselineNs > 0.0) {
            os << "        baseline " << std::fixed << std::setprecision(1) << std::setw(10)
               << r.baselineNs << "   gate (x" << std::setprecision(1) << kRegressionFactor << ") "
               << std::setprecision(1) << std::setw(10) << (r.baselineNs * kRegressionFactor)
               << "   "
               << (r.measuredNs <= r.baselineNs * kRegressionFactor ? "within baseline"
                                                                    : "*** OVER BASELINE ***")
               << "\n";
        } else {
            os << "        reported only - SC-004 (d) sets the per-loop marginal cost\n";
        }
    }

    os << "---------------------------------------------------------------------------------\n"
       << row("  SC-004 GATED LINE (160 000 / 1.5)", kGatedBaselineNs) << "\n"
       << row("  SC-004 ABSOLUTE CEILING (1.5 %/voice)", kAbsoluteCeilingNs) << "\n"
       << "  (b) wander-off delta,       (b) - (a)     " << std::fixed << std::setprecision(1)
       << std::setw(12) << wanderDeltaNs << " ns/block   " << std::setprecision(2)
       << (100.0 * wanderDeltaFraction) << " % of (a)   [SC-004 (b) band: <= +"
       << (100.0 * cpu::kWanderOffBandFraction) << " %]\n"
       << "  (c) dormancy saving,        (a) - (c)     " << std::setprecision(1) << std::setw(12)
       << dormantSavingNs << " ns/block   " << std::setprecision(2)
       << (100.0 * dormantSavingFraction) << " % of (a)   [SC-004 (c) needs >= "
       << (100.0 * cpu::kDormantSavingFloor) << " %]\n"
       << "  (d) marginal cost per loop, ((a)-(d))/5   " << std::setprecision(1) << std::setw(12)
       << marginalPerLoopNs << " ns/block   [reported]\n"
       << "---------------------------------------------------------------------------------\n"
       << "  audio preconditions, measured over " << (2 * cpu::kInspectBlocks)
       << " fully-scanned blocks per arm (both channels):\n"
       << "      (a) engine-active loops " << refCheck.activeLoops << " / "
       << FeedbackEcology::kMaxLoops << "   RMS " << std::setprecision(2) << refRmsDb
       << " dBFS   finite " << (refCheck.allFinite ? "yes" : "NO") << "   clamps "
       << refCheck.clampEngagements << "   non-finite resets " << refCheck.nonFiniteResets << "\n"
       << "      (b) engine-active loops " << wanderCheck.activeLoops << " / "
       << FeedbackEcology::kMaxLoops << "   RMS " << std::setprecision(2) << wanderRmsDb
       << " dBFS   finite " << (wanderCheck.allFinite ? "yes" : "NO") << "\n"
       << "      (c) engine-active loops " << dormantCheck.activeLoops << " / "
       << FeedbackEcology::kMaxLoops << "   exactly 0.0f on both channels throughout: "
       << (dormantCheck.allExactZero ? "yes" : "NO") << "\n"
       << "      (d) engine-active loops " << oneLoopCheck.activeLoops << " / "
       << FeedbackEcology::kMaxLoops << "   RMS " << std::setprecision(2) << oneLoopRmsDb
       << " dBFS   finite " << (oneLoopCheck.allFinite ? "yes" : "NO") << "\n"
       << "      the measured reference level is expected near -56.7 dBFS: this patch is\n"
       << "      quiet BY DESIGN (six Q ~ 100 resonators, 3.3-18.8 Hz of noise bandwidth\n"
       << "      out of 24 kHz), so the precondition below is an ANTI-SILENCE floor at "
       << std::setprecision(1) << cpu::kAntiSilenceDbfs << " dBFS,\n"
       << "      not a level assertion.\n"
       << "=================================================================================\n"
       << "  SC-004 (f) - THE TRANSITION BLOCKS, gated at each rate against (1) the steady\n"
       << "  block in the same state x " << cpu::kTransitionOverheadFactor
       << " (the O(buffer) detector) and (2) the same ABSOLUTE\n"
       << "  per-block ceiling as 48 kHz (NOT against kBaseline). Best of "
       << cpu::kTransitionTrials << " trials,\n"
       << "  each re-established from " << cpu::kTransitionSettleSeconds
       << " s of settled render. This is FR-019's O(1) detector.\n"
       << "=================================================================================\n";

    for (const cpu::TransitionRow& t : transitions) {
        const double overheadNs = t.result.bestNs - t.result.steadyNs;
        os << "  " << std::fixed << std::setprecision(0) << (t.sampleRate / 1000.0) << " kHz  "
           << cpu::transitionLabel(t.kind) << "\n"
           << cpu::rowAt("      transition block", t.result.bestNs, t.blockPeriodNs) << "   "
           << (t.result.bestNs <= t.ceilingNs ? "within ceiling" : "*** OVER CEILING ***") << "\n"
           << cpu::rowAt("      steady block, same rate and state", t.result.steadyNs,
                         t.blockPeriodNs)
           << "\n"
           << "      transition OVERHEAD (transition - steady)   " << std::fixed
           << std::setprecision(1) << std::setw(12) << overheadNs << " ns\n"
           << "      ceiling " << std::setprecision(1) << t.ceilingNs << " ns   trials found "
           << t.result.trialsFound << " / " << cpu::kTransitionTrials << "   edge at block +"
           << t.result.blockIndex << "   active " << t.result.activeBefore << " -> "
           << t.result.activeAfter << "   non-finite resets in the block ["
           << t.result.resetDeltaMin << ", " << t.result.resetDeltaMax << "]   finite "
           << (t.result.allFinite ? "yes" : "NO") << "\n";
        if (t.result.bestNs > t.ceilingNs) {
            os << "      *** OVER THE ABSOLUTE CEILING. ATTRIBUTE IT BEFORE ACTING: ***\n"
               << "      *** if the STEADY block at this rate is already over the ceiling, the\n"
               << "      *** finding is the component's steady cost at " << std::setprecision(0)
               << (t.sampleRate / 1000.0) << " kHz, not an O(buffer)\n"
               << "      *** transition - a budget-scope question for the USER, and levers 4\n"
               << "      *** and 5 are the only remaining moves. If the OVERHEAD is what blows\n"
               << "      *** it, the O(1) read-mute window of S3.1 has regressed to an\n"
               << "      *** O(buffer) fill and THAT is the defect this arm exists to find\n"
               << "      *** (feedback_ecology.h:1636-1654 - clearLoopAudio must never call\n"
               << "      *** CrossfadingDelayLine::reset()).\n";
        }
    }

    os << "=================================================================================\n"
       << "  copy-pasteable baselines, MEASURED on this machine and this build:\n";
    for (const cpu::ArmRow& r : reported) {
        if (r.baselineNs > 0.0) {
            os << cpu::baselineLine(r.constantName, r.measuredNs) << "\n";
        }
    }
    os << "  Transcribing a measured figure OVER a projection is always correct; take it\n"
       << "  from the LOWEST of several isolated runs, never from one sample. RAISING one\n"
       << "  so a REQUIRE passes is forbidden. A transcribed baseline must still satisfy\n"
       << "  BOTH compile-time clauses (x1.5 <= " << std::fixed << std::setprecision(1)
       << kAbsoluteCeilingNs << " and >= " << cpu::kNoOpFloorNs << "); one that\n"
       << "  cannot is the stop-and-surface case, not a licence to weaken a clause.\n"
       << "=================================================================================\n";

    if (nsRef > kGatedBaselineNs) {
        os << "  *** SC-004 (a) IS OVER THE GATED LINE BY " << std::fixed << std::setprecision(1)
           << (nsRef - kGatedBaselineNs) << " ns (" << std::setprecision(2)
           << (nsRef / kGatedBaselineNs) << "x). NO ADMISSIBLE BASELINE CAN BE\n"
           << "  *** TRANSCRIBED FROM THIS RUN: " << std::setprecision(1) << nsRef << " x 1.5 = "
           << (nsRef * kRegressionFactor) << " exceeds the " << kAbsoluteCeilingNs
           << " ns ceiling.\n"
           << "  *** TAKE THE PRE-AUTHORISED LADDER IN ORDER (plan S14.3, tasks.md T020):\n"
           << "  ***   1. OQ-2 LEVER 1 - std::tanh -> FastMath::fastTanh (core/fast_math.h:65)\n"
           << "  ***      at feedback_ecology.h:1995, WITH its own error-bound test added to\n"
           << "  ***      this TU: |fastTanh(x) - std::tanh(x)| < 5e-4 over x in [-6, 6] on a\n"
           << "  ***      10 001-point grid, and |fastTanh(x)| <= 1 everywhere. FR-042's\n"
           << "  ***      bound is preserved EXACTLY, so rung 2 is unchanged.\n"
           << "  ***   2. Stepped resonator retunes - ALREADY the default (FR-076), banked.\n"
           << "  ***   3. The settled-coupling-smoother carve-out - already in\n"
           << "  ***      updateControl(). Compare the stage table above to see whether the\n"
           << "  ***      remaining cost is the tanh term, the delay term or the ramp bank.\n"
           << "  *** THEN STOP AND SURFACE. Levers 4 (resonator -> a second SVF in Bandpass,\n"
           << "  *** losing the RT60 surface, kMaxResonatorQ = 100 and SC-023 entirely) and 5\n"
           << "  *** (default numLoops 6 -> 5) are USER DECISIONS taken from this table.\n";
    } else {
        os << "  SC-004 (a) IS WITHIN THE GATED LINE: " << std::fixed << std::setprecision(1)
           << (kGatedBaselineNs - nsRef) << " ns of headroom (" << std::setprecision(2)
           << (100.0 * nsRef / kGatedBaselineNs) << " % of the line, " << std::setprecision(2)
           << (100.0 * nsRef / kAbsoluteCeilingNs) << " % of the absolute ceiling).\n";
    }

    os << "  *** NO AGENT MAY lower kMaxLoops, raise a budget, relax a threshold, or shrink\n"
       << "  *** a workload to make this fit. Reduce cost, never move the line\n"
       << "  *** (resonance_drift_network_perf_test.cpp:59-65, inherited verbatim).\n"
       << "  RUN IT ALONE: node tools/run-cpu-tests.js dsp_systems_tests. A verdict that\n"
       << "  flips between runs is measuring the machine, not the code.\n"
       << "  RECORD the whole table in the build transcript and in plan S14.2.\n"
       << "=================================================================================\n";

    WARN(os.str());

    // -------------------------------------------------------------------------
    // Preconditions, BEFORE the timing verdicts - a figure taken from a fixture
    // that was not rendering the specified configuration is not a measurement of
    // anything, and must fail as a fixture error rather than as a budget miss.
    // -------------------------------------------------------------------------
    CAPTURE(refCheck.activeLoops, refRmsDb, refCheck.allFinite, refCheck.nonFiniteResets);
    REQUIRE(refCheck.allFinite);
    REQUIRE(refCheck.activeLoops == FeedbackEcology::kMaxLoops);
    REQUIRE_FALSE(refCheck.allExactZero);
    REQUIRE(refRmsDb > cpu::kAntiSilenceDbfs);
    // SC-012 (a): the reference patch fires rung 5 exactly never. A figure taken
    // from a render that was trapping every block is not the reference cost.
    REQUIRE(refCheck.nonFiniteResets == 0u);

    CAPTURE(wanderCheck.activeLoops, wanderRmsDb, wanderCheck.allFinite);
    REQUIRE(wanderCheck.allFinite);
    REQUIRE(wanderCheck.activeLoops == FeedbackEcology::kMaxLoops);
    REQUIRE_FALSE(wanderCheck.allExactZero);
    REQUIRE(wanderRmsDb > cpu::kAntiSilenceDbfs);

    // SC-004 (c)'s two mandatory pre-timing preconditions, both asserted rather
    // than assumed: every loop's engine is off, and the output is EXACTLY zero on
    // both channels throughout - so a cheap figure cannot have come from an
    // accidental early return elsewhere in the render path. At mix = 1 the dry is
    // fully crossfaded out and every gate is a literal 0.0f, so the wet sum is
    // exactly zero BY CONSTRUCTION (feedback_ecology.h:2016-2022).
    CAPTURE(dormantCheck.activeLoops, dormantCheck.allExactZero, dormantCheck.allFinite);
    REQUIRE(dormantCheck.allFinite);
    REQUIRE(dormantCheck.activeLoops == 0u);
    REQUIRE(dormantCheck.allExactZero);

    CAPTURE(oneLoopCheck.activeLoops, oneLoopRmsDb, oneLoopCheck.allFinite);
    REQUIRE(oneLoopCheck.allFinite);
    REQUIRE(oneLoopCheck.activeLoops == 1u);
    REQUIRE_FALSE(oneLoopCheck.allExactZero);

    // -------------------------------------------------------------------------
    // (a) The gate. THREE clauses, and they are not restatements of each other:
    //   1. the regression clause, against the checked-in baseline;
    //   2. the TRANSCRIBABILITY clause - the measurement must itself be an
    //      admissible baseline, i.e. measured x 1.5 <= 160 000, i.e.
    //      measured <= 106 667. This is the line the T004 projection was compared
    //      against and the one SC-004 actually names; without it a run that
    //      measured 100 000 ns would pass clause 3 while making every future
    //      baseline transcription impossible;
    //   3. the absolute ceiling, stated literally so a reader of a failure need
    //      not do the transitive step.
    // -------------------------------------------------------------------------
    CAPTURE(nsRef, cpu::kBaselineReferenceNs, kGatedBaselineNs, kAbsoluteCeilingNs);
    REQUIRE(nsRef <= cpu::kBaselineReferenceNs * kRegressionFactor);
    REQUIRE(nsRef * kRegressionFactor <= kAbsoluteCeilingNs);
    REQUIRE(nsRef <= kAbsoluteCeilingNs);

    // -------------------------------------------------------------------------
    // (b) FR-056's wander-off arm, as a BAND. Cheaper is fine and needs no
    // clause; what would be a defect is wander-off costing MORE than the
    // reference by a margin the measurement can resolve.
    // -------------------------------------------------------------------------
    CAPTURE(nsRef, nsWanderOff, wanderDeltaNs, wanderDeltaFraction);
    REQUIRE(nsWanderOff <= nsRef * (1.0 + cpu::kWanderOffBandFraction));
    REQUIRE(nsWanderOff <= cpu::kBaselineWanderOffNs * kRegressionFactor);

    // -------------------------------------------------------------------------
    // (c) FR-062's dormancy, as a NUMBER: six skipped chains and the FR-031 input
    // sum gone. NEVER RELAX THE 40 %.
    // -------------------------------------------------------------------------
    CAPTURE(nsRef, nsDormant, dormantSavingNs, dormantSavingFraction);
    REQUIRE(nsDormant < nsRef);
    REQUIRE(dormantSavingFraction >= cpu::kDormantSavingFloor);
    REQUIRE(nsDormant <= cpu::kBaselineAllDormantNs * kRegressionFactor);

    // -------------------------------------------------------------------------
    // (d) Reported, with one structural clause: one rendering loop must cost less
    // than six. A build where it does not is not a slow build, it is a build
    // whose per-loop cost is not where it is supposed to be.
    // -------------------------------------------------------------------------
    CAPTURE(nsRef, nsOneLoop, marginalPerLoopNs);
    REQUIRE(nsOneLoop < nsRef);

    // -------------------------------------------------------------------------
    // (f) The four transition blocks. Each is gated against the steady block in
    // the same state at its own rate (x kTransitionOverheadFactor, the O(buffer)
    // detector) and against the same ABSOLUTE per-block ceiling at both rates
    // (ruled 2026-09-15), and each carries its fixture preconditions first.
    // -------------------------------------------------------------------------
    for (const cpu::TransitionRow& t : transitions) {
        INFO("transition arm: " << cpu::transitionLabel(t.kind) << " at " << t.sampleRate
                                << " Hz");
        CAPTURE(t.result.bestNs, t.result.steadyNs, t.ceilingNs, t.result.trialsFound,
                t.result.blockIndex, t.result.activeBefore, t.result.activeAfter,
                t.result.resetDeltaMin, t.result.resetDeltaMax);

        // Fixture first: every trial must have found its edge, and the block must
        // have rendered finite audio.
        REQUIRE(t.result.trialsFound == cpu::kTransitionTrials);
        REQUIRE(t.result.allFinite);
        REQUIRE(detail::isFinite(t.result.bestNs));
        REQUIRE(t.result.bestNs > 0.0);
        REQUIRE(t.result.steadyNs > 0.0);
        REQUIRE(t.result.activeBefore == FeedbackEcology::kMaxLoops);

        if (t.kind == cpu::Transition::SleepEdge) {
            // All five dropped gates settle on ONE control step, so the timed
            // block carries all five clearLoopAudio calls. A block that dropped
            // fewer would be timing a cheaper event than SC-004 (f) names - a
            // fixture error, to be fixed in the fixture and never by widening the
            // gate.
            REQUIRE(t.result.activeAfter == 1u);
            // SC-012 (a): a sleep edge fires no traps.
            REQUIRE(t.result.resetDeltaMax == 0u);
        } else {
            // The FR-048 poison must actually have taken, exactly once, in every
            // trial - otherwise this arm timed an ordinary block and its verdict
            // means nothing.
            REQUIRE(t.result.resetDeltaMin == 1u);
            REQUIRE(t.result.resetDeltaMax == 1u);
            // The trap clears the loop's audio; it does not put the loop to sleep.
            REQUIRE(t.result.activeAfter == FeedbackEcology::kMaxLoops);
        }

        // The O(buffer) detector: transition against steady at the same rate.
        REQUIRE(t.result.bestNs <= t.result.steadyNs * cpu::kTransitionOverheadFactor);
        // The absolute per-block ceiling, the same nanosecond figure at both rates.
        REQUIRE(t.result.bestNs <= t.ceilingNs);
    }
}
