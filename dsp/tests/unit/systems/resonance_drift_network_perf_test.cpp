// ==============================================================================
// Layer 3: System Tests - ResonanceDriftNetwork, CPU budget (SC-004) and the
//          FR-060 stage-cost probe      (specs/vorago-phase3-resonance-drift)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase3-resonance-drift/spec.md   (FR-011, FR-013,
//                                                           FR-014, FR-015,
//                                                           FR-016, FR-037,
//                                                           FR-038, FR-044,
//                                                           FR-060, SC-004)
//            specs/vorago-phase3-resonance-drift/plan.md   (S10.1-S10.6)
//            specs/vorago-phase3-resonance-drift/tasks.md  (T001 creates this
//                                                           TU; T002 adds the
//                                                           stage probe below;
//                                                           T019 the threshold
//                                                           calibration pass;
//                                                           T020 the SC-004
//                                                           gated baselines)
//
// SCOPE OF THIS TU: hidden, run-on-demand cases only. That is exactly
//   ResonanceDriftNetwork_StageCostProbe    - tagged "[.perf]"         (T002)
//   ResonanceDriftNetwork_MeasureThresholds - tagged "[.calibration]"  (T019)
//   ResonanceDriftNetwork_CpuBudget         - tagged "[.perf]"         (T020)
// and nothing else. None of the three is ever run by the default suite or by
// CI - but T020's per-arm baselines carry static_asserts that ARE evaluated on
// every CI leg, which is the whole reason SC-004's absolute ceiling lives at
// compile time rather than only inside the hidden case.
//
// -----------------------------------------------------------------------------
// WHY THIS CASE EXISTS BEFORE THE COMPONENT DOES (plan S10.2, tasks.md T002)
// -----------------------------------------------------------------------------
// FR-011 composes the engine from TWELVE single-resonator ResonatorBank
// instances rather than one twelve-resonator bank, because process(float)
// returns only the summed wet output (resonator_bank.h:470-516) and there is no
// per-resonator output accessor - so a shared bank cannot give each peak the
// per-sample dormancy gate the roadmap's Dormancy rule demands. That choice has
// a cost, and FR-013 makes the response to that cost a MEASURED decision:
//
//   * FR-013 Tier 1 (a purely additive ResonatorBank::processIndividual, then
//     ONE twelve-resonator bank) is triggered by, and only by, stage (a1) of
//     this probe exceeding 48 000 ns/block.
//   * FR-013 Tier 2 (the processSympatheticBankSIMD kernel) and FR-038's
//     static-pan fallback are answers to a SECOND, different question: the
//     SC-004 total.
//
// Both questions are decided from the table this case prints, BEFORE the
// component is written. Producing the per-stage breakdown now costs a day;
// discovering it at the end costs the phase.
//
// This case is a PROBE, NOT A GATE. It REQUIREs only that every figure is finite
// and strictly positive - a zero or a NaN means the measurement is broken, which
// is the one thing that would make the table lie. It does NOT assert either
// gate, because the response to a miss is a decision taken from measured
// numbers, and in Gate 2's "neither term dominates" branch it is a USER
// decision:
//
//   *** STOP-AND-SURFACE RULE (FR-060, inherited verbatim from
//   *** noise_organism_perf_test.cpp:44-58) - NON-NEGOTIABLE ***
//   NO IMPLEMENTING AGENT MAY lower kMaxPeaks, raise kBudgetNs, relax a
//   threshold, or shrink a workload to make a figure fit. Reduce cost, never
//   move the line. The verdict block is emitted loudly via WARN precisely so
//   the decision is taken from the measured table rather than from a guess.
//
// -----------------------------------------------------------------------------
// WHY ns/block AND NOT "% of one core"
// -----------------------------------------------------------------------------
// A percent-of-core figure is not reproducible across dev machines or CI
// runners. The measurement basis is NANOSECONDS PER 512-SAMPLE BLOCK AT 48 kHz
// (plan S10.1), the basis established by harmonic_cloud_perf_test.cpp and reused
// by continuous_body_perf_test.cpp, atmosphere_engine_perf_test.cpp:22-70 and
// noise_organism_perf_test.cpp:201-272. One block period is 10 666 667 ns, so
// SC-004's 0.75 %/voice ceiling is 80 000 ns/block. The percent figure is
// REPORTED, never asserted.
//
// TRIAL SHAPE (tasks.md T002): best-of-25 x 500 blocks after 400 warm-up blocks,
// the atmosphere_engine_perf_test.cpp idiom. Many short trials, because the dev
// machine is a hybrid part and the dominant noise source is a whole trial
// migrating onto an E-core. Affinity pinning was tried and REJECTED in both
// reference perf TUs.
//
// RUN IT ALONE. Sustained benchmarking heats the CPU and boost clocks drop;
// figures drift ~14 % across a session. Nothing else may be executing:
//   node tools/run-cpu-tests.js dsp_systems_tests
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe
//       "ResonanceDriftNetwork_StageCostProbe" 2>&1 | tee probe.log | tail -40
//
// -----------------------------------------------------------------------------
// THE FIVE STAGES, AND WHY EACH IS SHAPED THE WAY IT IS (tasks.md T002 table,
// plan S10.2)
// -----------------------------------------------------------------------------
//  (a1) engine, Tier 0     TWELVE ResonatorBank instances, each prepare(48000),
//       (FR-011 as written) slot 0 only enabled at the FR-016 anchors, Q = 12,
//                          gain -6 dB, each process(x) called INDIVIDUALLY per
//                          sample and summed by the harness. This is FR-011's
//                          shape exactly, including the twelve 16-slot skip
//                          loops (resonator_bank.h:487-488) and the twelve sets
//                          of three global smoothers (:474-476) that a shared
//                          bank would run once. THIS FIGURE, AND ONLY THIS
//                          FIGURE, DECIDES GATE 1.
//  (a2) engine, Tier 1     ONE ResonatorBank with twelve enabled slots at the
//       (FR-013 Tier 1)    same anchors, processBlock(buf, 512). The Tier-1
//                          shape's engine cost, measured through the shipped
//                          block path as a stand-in for the additive
//                          processIndividual that does not exist yet.
//  (b)  lanes              48 BrownianDrift (4 per peak x 12: frequency, Q,
//       (FR-037, FR-038)   gain, pan), each prepare(48000), advanced with
//                          processBlock(64) on the FR-037 decimated clock.
//                          Reported at decimation 1, 2 and 17, and with the 12
//                          PAN lanes pooled with the other 36 AND isolated on
//                          their own - so a Gate-2 miss can be attributed to
//                          FR-038 specifically. Depth is pinned at 1.0 so the
//                          walk is genuinely live: with depth 0 the output
//                          smoother converges, advanceSamples early-returns
//                          (smoother.h:207-209) and the std::pow term that
//                          dominates this stage (plan S10.3) vanishes -
//                          measuring a shape the component never renders.
//  (c)  control writes     The twelve-peak setFrequency -> setQ -> setGain
//       (FR-014, FR-015)   triple on the (a1) shape, once per 64-sample control
//                          chunk, in three variants: no change detection; change
//                          detection against static targets (the FR-034
//                          wander-disabled case, where nothing is written at
//                          all); and FR-015's mandatory Q-after-frequency write
//                          as one indivisible OR-detected pair. The third is the
//                          shape the component actually renders under wander and
//                          is the one that enters the projection.
//  (d)  per-sample tail    The FR-044 arithmetic with NO banks: 12 gate
//                          LinearRamp::process(), 12 pan multiply-pairs, one
//                          wet-scale LinearRamp, one 20 ms OnePoleSmoother mix,
//                          two std::clamp and detail::flushDenormal.
//  (e)  sleep edge         One ResonatorBank::reset() + the FR-014-order
//       (FR-042)           re-apply + setEnabled(0, true), reported as NS PER
//                          EDGE, so its amortisation against a 20-90 s event
//                          schedule (slow_event_scheduler.h:164-165) is visible
//                          rather than folded into a per-block figure it would
//                          badly distort.
//
// THE PROJECTION the probe prints is plan S10.4:
//     engine (whichever shape Gate 1 selected)
//   + lanes at decimation 2 (the FR-016 default wander rate, 0.03 Hz)
//   + control writes (FR-015 shape)
//   + per-sample tail
//   + sleep edge (amortised ~ 0, reported, never added)
// against 80 000 ns.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/resonator_bank.h>

// T019 (the calibration case at the foot of this TU) only. The stage probe
// above deliberately does NOT include the component - it is measured before the
// component exists - but the calibration pass measures the SHIPPED network, so
// it needs the real header. PinkNoiseFilter is SC-003's and SC-016's specified
// drive source; SlowEventScheduler is included for its kDefaultMinInterval /
// kDefaultMaxInterval only, because SC-015 (g)'s and SC-016's wake pattern is
// "driven by the test, not owned by the component".
#include <krate/dsp/primitives/pink_noise_filter.h>
#include <krate/dsp/processors/slow_event_scheduler.h>
#include <krate/dsp/systems/resonance_drift_network.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;

namespace {

// =============================================================================
// Measurement basis (SC-004, plan S10.1)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr float kSr48f = 48000.0f;
constexpr std::size_t kBlockSize = 512;

/// The network's control chunk (FR-007, kControlChunkSamples). Lane advances and
/// control writes happen once per chunk, which is what makes stages (b) and (c)
/// decidable at all.
constexpr std::size_t kControlChunk = 64;
static_assert(kBlockSize % kControlChunk == 0,
              "stages (b) and (c) push work once per control chunk and the chunk "
              "must divide the measured block exactly");

/// Wall-clock period of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockPeriodNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// SC-004's per-voice budget: 0.75 % of one core = 80 000 ns/block (roadmap
/// line 228, plan S10.1). Written as the literal the spec names and TIED to its
/// derivation by the clause below rather than computed from it.
///
/// NO AGENT MAY RAISE THIS. It is Gate 2's line, and the stop-and-surface rule
/// above governs every response to a miss.
constexpr double kBudgetNs = 80000.0;
static_assert(kBudgetNs >= kBlockPeriodNs * 0.0074 && kBudgetNs <= kBlockPeriodNs * 0.0076,
              "SC-004's budget is 0.75 % of one 512-sample block at 48 kHz");

/// Gate 1's line, quoted verbatim from FR-013 (spec.md:292-296): stage (a1)
/// exceeding 48 000 ns/block - 60 % of the SC-004 ceiling - is the ONLY trigger
/// for FR-013 Tier 1. Not the total; not any other stage.
constexpr double kTier1TriggerNs = 48000.0;
static_assert(kTier1TriggerNs > kBudgetNs * 0.59 && kTier1TriggerNs < kBudgetNs * 0.61,
              "FR-013's Tier-1 trigger is 60 % of the SC-004 ceiling");

// Trial shape, pinned by tasks.md T002.
constexpr int kTrials = 25;
constexpr int kBlocksPerTrial = 500;
constexpr int kWarmupBlocks = 400;

// -----------------------------------------------------------------------------
// The FR-016 default patch (spec.md's parameter-default table). NORMATIVE
// VALUES, used verbatim, never regenerated.
// -----------------------------------------------------------------------------
constexpr std::size_t kNumPeaks = 12;
constexpr std::array<float, kNumPeaks> kAnchorsHz{40.0f,  55.0f,  75.0f,  103.0f,
                                                  141.0f, 193.0f, 265.0f, 363.0f,
                                                  497.0f, 681.0f, 933.0f, 1278.0f};
constexpr float kPeakQ = 12.0f;
constexpr float kPeakGainDb = -6.0f;

/// FR-018's output ceiling (kOutputClamp), FR-041's gate ramp (kGainRampMs) and
/// FR-043's mix smoothing time (kMixSmoothMs = kResonatorSmoothingTimeMs = 20 ms,
/// resonator_bank.h:69). Spelled locally because the component that will own
/// them does not exist yet - that is the whole point of this task.
constexpr float kOutputClamp = 4.0f;
constexpr float kGainRampMs = 50.0f;
constexpr float kMixSmoothMs = kResonatorSmoothingTimeMs;

// -----------------------------------------------------------------------------
// Lane counts and the FR-037 decimation points (plan S6.2's worked values,
// re-derived here rather than transcribed loosely):
//
//   rate 1.000 Hz -> tau_req =  1.000 s -> dec  1 -> tau =  1.000 s -> s = 0.0268
//   rate 0.030 Hz -> tau_req = 33.333 s -> dec  2 -> tau = 16.667 s -> s = 0.5526
//   rate 0.002 Hz -> tau_req = 500.00 s -> dec 17 -> tau = 29.412 s -> s = 0.9803
//
//   s = (tau - kTauMin) / (kTauMax - kTauMin), kTauMin = 0.2, kTauMax = 30.0
//       (brownian_drift.h:97, :99)
//
// The smoothness travels with the decimation because that is how setWanderRate
// will configure the lanes; measuring decimation 17 at decimation 1's smoothness
// would price a lane the component never builds.
// -----------------------------------------------------------------------------
constexpr std::size_t kNumLanes = 48;     ///< 4 per peak x 12 peaks (FR-005's table)
constexpr std::size_t kNumPanLanes = 12;  ///< FR-038's lanes, isolated for attribution
constexpr std::size_t kNumNonPanLanes = 36;
static_assert(kNumPanLanes + kNumNonPanLanes == kNumLanes,
              "the pan lanes are a partition of the 48, not an addition to them");
constexpr std::size_t kSaltPanLane = 48;  ///< FR-005's salt table

constexpr int kDecimationFast = 1;
constexpr int kDecimationDefault = 2;  ///< the FR-016 default rate, 0.03 Hz
constexpr int kDecimationSlow = 17;    ///< kMaxLaneDecimation

constexpr float kSmoothnessAtDec1 = 0.0268f;
constexpr float kSmoothnessAtDec2 = 0.5526f;
constexpr float kSmoothnessAtDec17 = 0.9803f;

// =============================================================================
// Best-of-N driver
// =============================================================================

/// Pinned warm-up, then best-of-`kTrials` x `kBlocksPerTrial`; returns the
/// winning trial's ns per invocation of `runBlock`.
///
/// For stages (a)-(d) one invocation is one 512-sample block, so the result is
/// ns/block. For stage (e) one invocation is one sleep edge, so the result is
/// ns/edge - the same driver, a different unit, labelled as such at every use.
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

/// FR-016's pinned global bank settings: damping 0, tilt 0 (non-zero tilt costs
/// a std::log2 + dbToGain per resonator per sample, resonator_bank.h:121-125,
/// :507), exciter mix 0 (fully wet, :626).
void pinGlobalBankSettings(ResonatorBank& bank) noexcept
{
    bank.setDamping(0.0f);
    bank.setExciterMix(0.0f);
    bank.setSpectralTilt(0.0f);
}

/// One peak's FR-014-ordered configuration write.
///
/// FR-014's order is frequency -> Q -> gain and it is MANDATORY even here, in a
/// setup path: setFrequency re-derives qValues_ from the stored decay
/// (resonator_bank.h:333), so a frequency write after a Q write silently
/// discards the Q, and this probe would then price a bank sitting at
/// rt60ToQ(f, 1.0) instead of at the Q = 12 the FR-016 patch specifies.
void configurePeakSlot(ResonatorBank& bank, std::size_t slot, float hz) noexcept
{
    bank.setEnabled(slot, true);
    bank.setFrequency(slot, hz);
    bank.setQ(slot, kPeakQ);
    bank.setGain(slot, kPeakGainDb);
}

// =============================================================================
// Stage 0 - the shared 512-float refill term, measured on its own
// =============================================================================
//
// Stage (a2) copies a block before processing it in place; stage (a1) does not
// need to, because it reads the source directly. The refill is reported and
// subtracted so the two engine shapes are compared on engine cost alone.

[[nodiscard]] double measureBufferRefill(double& sink)
{
    std::array<float, kBlockSize> src{};
    std::array<float, kBlockSize> work{};
    fillWhite(src.data(), kBlockSize, 0x5EEDu);

    return bestTrialNs([&]() noexcept {
        std::copy(src.begin(), src.end(), work.begin());
        sink += static_cast<double>(work[0]) + static_cast<double>(work[kBlockSize - 1]);
    });
}

// =============================================================================
// Stage (a1) - the engine as FR-011 writes it: twelve single-resonator banks
// =============================================================================
//
// *** THIS FIGURE, ALONE, DECIDES GATE 1. ***

[[nodiscard]] double measureEngineTwelveBanks(double& sink)
{
    std::array<ResonatorBank, kNumPeaks> banks{};
    for (std::size_t i = 0; i < kNumPeaks; ++i) {
        banks[i].prepare(kSr48);
        pinGlobalBankSettings(banks[i]);
        // Only slot 0 is ever enabled (FR-010); slots [1, 16) stay disabled and
        // cost one predicted branch each (resonator_bank.h:487-488). That skip
        // loop, twelve times over, is a large part of what is priced here.
        configurePeakSlot(banks[i], 0, kAnchorsHz[i]);
    }

    std::array<float, kBlockSize> src{};
    fillWhite(src.data(), kBlockSize, 0x0DDBA11u);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const float x = src[n];
            float wet = 0.0f;
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                // Taken INDIVIDUALLY and summed by the harness - exactly what
                // FR-011 does so each peak can carry its own per-sample gate.
                wet += banks[i].process(x);
            }
            acc += wet;
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (a2) - the FR-013 Tier-1 engine shape: one twelve-resonator bank
// =============================================================================
//
// Measured through the shipped processBlock path as a stand-in for the additive
// processIndividual that Tier 1 would add and that does not exist yet. It is a
// STAND-IN and is labelled as one: processIndividual would also write twelve
// floats per sample, which processBlock does not, so this figure is a LOWER
// bound on the Tier-1 engine cost, never an equal.

[[nodiscard]] double measureEngineOneBank(double& sink)
{
    ResonatorBank bank;
    bank.prepare(kSr48);
    pinGlobalBankSettings(bank);
    for (std::size_t i = 0; i < kNumPeaks; ++i) {
        configurePeakSlot(bank, i, kAnchorsHz[i]);
    }

    std::array<float, kBlockSize> src{};
    std::array<float, kBlockSize> work{};
    fillWhite(src.data(), kBlockSize, 0x0DDBA11u);

    return bestTrialNs([&]() noexcept {
        std::copy(src.begin(), src.end(), work.begin());
        bank.processBlock(work.data(), kBlockSize);
        sink += static_cast<double>(work[0]) + static_cast<double>(work[kBlockSize - 1]);
    });
}

// =============================================================================
// Stage (b) - the wander lanes, at three decimations, pan pooled and isolated
// =============================================================================

[[nodiscard]] double measureLanes(std::size_t numLanes,
                                  int decimation,
                                  float smoothness,
                                  std::size_t saltBase,
                                  double& sink)
{
    std::array<BrownianDrift, kNumLanes> lanes{};
    for (std::size_t i = 0; i < numLanes; ++i) {
        lanes[i].prepare(kSr48);
        lanes[i].setSeed(deriveStreamSeed(0xD817Fu, saltBase + i));
        lanes[i].setSmoothness(smoothness);
        // Depth 1.0 keeps the walk live so the output smoother never converges.
        // At depth 0 advanceSamples early-returns (smoother.h:207-209), the
        // std::pow that dominates this stage (plan S10.3) disappears, and the
        // measurement silently becomes a no-op.
        lanes[i].setDepth(1.0f);
    }

    // Carried ACROSS blocks, not reset per block: the FR-037 lane clock is
    // absolute, and a per-block reset would advance every lane on the first
    // chunk of every block regardless of decimation - i.e. would measure
    // decimation 1 three times over.
    int counter = 0;

    return bestTrialNs([&]() noexcept {
        for (std::size_t off = 0; off < kBlockSize; off += kControlChunk) {
            if (counter == 0) {
                for (std::size_t i = 0; i < numLanes; ++i) {
                    lanes[i].processBlock(kControlChunk);
                }
            }
            counter = (counter + 1) % decimation;
        }
        sink += static_cast<double>(lanes[0].getCurrentValue());
    });
}

// =============================================================================
// Stage (c) - the control-step write path
// =============================================================================

enum class ControlWriteShape : std::uint8_t {
    NoChangeDetection,      ///< all three writes, every chunk, unconditionally
    ChangeDetectionStatic,  ///< FR-034 wander off: nothing changes, nothing is written
    Fr015OrPair             ///< frequency+Q as one OR-detected pair, gain detected alone
};

[[nodiscard]] double measureControlWrites(ControlWriteShape shape, double& sink)
{
    // The (a1) shape: the writes go to twelve separate single-resonator banks.
    std::array<ResonatorBank, kNumPeaks> banks{};
    for (std::size_t i = 0; i < kNumPeaks; ++i) {
        banks[i].prepare(kSr48);
        pinGlobalBankSettings(banks[i]);
        configurePeakSlot(banks[i], 0, kAnchorsHz[i]);
    }

    // Seeded with what configurePeakSlot just wrote, so ChangeDetectionStatic
    // genuinely detects "unchanged" from the very first chunk.
    std::array<float, kNumPeaks> lastHz{};
    std::array<float, kNumPeaks> lastQ{};
    std::array<float, kNumPeaks> lastGainDb{};
    for (std::size_t i = 0; i < kNumPeaks; ++i) {
        lastHz[i] = kAnchorsHz[i];
        lastQ[i] = kPeakQ;
        lastGainDb[i] = kPeakGainDb;
    }

    int step = 0;

    return bestTrialNs([&]() noexcept {
        for (std::size_t off = 0; off < kBlockSize; off += kControlChunk) {
            const bool even = ((step++) % 2) == 0;
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                // A moving frequency target - about +/- 1 cent around the
                // anchor, the scale of one wander step at the FR-016 defaults.
                // It must actually alternate: a repeated identical target would
                // turn every arm into a second measurement of the static one.
                const float movingHz = kAnchorsHz[i] * (even ? 1.0006f : 0.9994f);

                switch (shape) {
                case ControlWriteShape::NoChangeDetection: {
                    banks[i].setFrequency(0, movingHz);
                    banks[i].setQ(0, kPeakQ);
                    banks[i].setGain(0, kPeakGainDb);
                    break;
                }
                case ControlWriteShape::ChangeDetectionStatic: {
                    // Targets equal to the last written values: the FR-034
                    // wander-disabled case SC-004 (b) measures, where the
                    // network approaches zero control-rate cost.
                    const float staticHz = kAnchorsHz[i];
                    const bool freqOrQMoved = (staticHz != lastHz[i]) || (kPeakQ != lastQ[i]);
                    if (freqOrQMoved) {
                        banks[i].setFrequency(0, staticHz);
                        banks[i].setQ(0, kPeakQ);
                        lastHz[i] = staticHz;
                        lastQ[i] = kPeakQ;
                    }
                    if (kPeakGainDb != lastGainDb[i]) {
                        banks[i].setGain(0, kPeakGainDb);
                        lastGainDb[i] = kPeakGainDb;
                    }
                    break;
                }
                case ControlWriteShape::Fr015OrPair: {
                    // FR-015 verbatim: frequency and Q are ONE indivisible write
                    // pair whose change detection is the OR of the two
                    // comparisons, because setFrequency clobbers Q
                    // (resonator_bank.h:333). Gain is detected on its own.
                    const bool freqOrQMoved = (movingHz != lastHz[i]) || (kPeakQ != lastQ[i]);
                    if (freqOrQMoved) {
                        banks[i].setFrequency(0, movingHz);
                        banks[i].setQ(0, kPeakQ);
                        lastHz[i] = movingHz;
                        lastQ[i] = kPeakQ;
                    }
                    if (kPeakGainDb != lastGainDb[i]) {
                        banks[i].setGain(0, kPeakGainDb);
                        lastGainDb[i] = kPeakGainDb;
                    }
                    break;
                }
                }
            }
        }
        // One process() call per block - about 1/512 of stage (a1), so well
        // under 1 % of this stage - as the anti-dead-code sink. It reads the
        // coefficients the writes above produced, so no write can be elided as
        // unobservable.
        sink += static_cast<double>(banks[0].process(0.0f));
    });
}

// =============================================================================
// Stage (d) - the FR-044 per-sample tail, with no banks in it
// =============================================================================

[[nodiscard]] double measurePerSampleTail(double& sink)
{
    std::array<LinearRamp, kNumPeaks> gates{};
    std::array<float, kNumPeaks> panGainL{};
    std::array<float, kNumPeaks> panGainR{};
    for (std::size_t i = 0; i < kNumPeaks; ++i) {
        gates[i].configure(kGainRampMs, kSr48f);
        gates[i].snapTo(0.0f);
        // Equal-power pan gains are computed on the CONTROL grid in the real
        // component and are therefore deliberately outside the timed loop: what
        // stage (d) prices is the twelve multiply-pairs, not the trig that
        // produced the gains.
        const float pan =
            -1.0f + ((2.0f * static_cast<float>(i)) / static_cast<float>(kNumPeaks - 1));
        const float theta = (pan + 1.0f) * 0.25f * kPi;
        panGainL[i] = std::cos(theta);
        panGainR[i] = std::sin(theta);
    }

    LinearRamp wetScaleRamp;
    wetScaleRamp.configure(kGainRampMs, kSr48f);
    wetScaleRamp.snapTo(1.0f / std::sqrt(static_cast<float>(kNumPeaks)));

    OnePoleSmoother mixSmoother;
    mixSmoother.configure(kMixSmoothMs, kSr48f);
    mixSmoother.snapTo(1.0f);

    std::array<float, kBlockSize> dryL{};
    std::array<float, kBlockSize> dryR{};
    fillWhite(dryL.data(), kBlockSize, 0x1EF7u);
    fillWhite(dryR.data(), kBlockSize, 0x216Fu);

    int block = 0;

    return bestTrialNs([&]() noexcept {
        // Fourteen retargets per block, a fraction of a percent of the stage,
        // purely to keep every ramp genuinely IN FLIGHT: a settled LinearRamp
        // early-outs at smoother.h:371-373 and a settled OnePoleSmoother at
        // :198-201, so a probe that let them settle would price a network that
        // is never waking, never fading and never moving its mix.
        const bool even = ((block++) % 2) == 0;
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            gates[i].setTarget(even ? 1.0f : 0.0f);
        }
        wetScaleRamp.setTarget(even ? 0.28f : 0.32f);
        mixSmoother.setTarget(even ? 1.0f : 0.9f);

        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const float dl = dryL[n];
            const float dr = dryR[n];
            const float x = 0.5f * (dl + dr);

            float wetL = 0.0f;
            float wetR = 0.0f;
            for (std::size_t i = 0; i < kNumPeaks; ++i) {
                // `x` stands in for bankProcess(i, x): stage (d) is the tail
                // WITHOUT the engine, which stage (a) already priced.
                const float y = x * gates[i].process();
                wetL += y * panGainL[i];
                wetR += y * panGainR[i];
            }

            const float scale = wetScaleRamp.process();
            const float m = mixSmoother.process();
            const float oneMinusM = 1.0f - m;

            float outL = (oneMinusM * dl) + (m * wetL * scale);
            float outR = (oneMinusM * dr) + (m * wetR * scale);
            outL = detail::flushDenormal(std::clamp(outL, -kOutputClamp, kOutputClamp));
            outR = detail::flushDenormal(std::clamp(outR, -kOutputClamp, kOutputClamp));

            acc += outL + outR;
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Stage (e) - the FR-042 sleep/wake edge, in NS PER EDGE
// =============================================================================

[[nodiscard]] double measureSleepEdgeNs(double& sink)
{
    ResonatorBank bank;
    bank.prepare(kSr48);
    pinGlobalBankSettings(bank);
    configurePeakSlot(bank, 0, kAnchorsHz[0]);

    return bestTrialNs([&]() noexcept {
        // reset() is a CONFIGURATION WIPE, not a state clear: it leaves every
        // resonator at 440 Hz, default Q, unity gain and enabled_[i] = false
        // (resonator_bank.h:226-231). The re-apply below is therefore mandatory
        // and is part of what an edge costs (FR-004, FR-042).
        bank.reset();
        pinGlobalBankSettings(bank);
        bank.setFrequency(0, kAnchorsHz[0]);
        bank.setQ(0, kPeakQ);
        bank.setGain(0, kPeakGainDb);
        bank.setEnabled(0, true);
        // Anti-dead-code sink; about 1/512 of stage (a1) per edge.
        sink += static_cast<double>(bank.process(0.0f));
    });
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

[[nodiscard]] std::string edgeRow(const std::string& label, double nsPerEdge)
{
    std::ostringstream os;
    os << std::left << std::setw(54) << label << std::right << std::fixed << std::setprecision(1)
       << std::setw(12) << nsPerEdge << " ns/EDGE";
    return os.str();
}

}  // namespace

// =============================================================================
// T002 - the FR-060 stage-cost probe (plan S10.2, S10.5)
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_StageCostProbe", "[resonance_drift_network][.perf]")
{
    double sink = 0.0;

    const double refillNs = measureBufferRefill(sink);

    const double engineTier0Ns = measureEngineTwelveBanks(sink);
    const double engineTier1RawNs = measureEngineOneBank(sink);
    const double engineTier1NetNs = engineTier1RawNs - refillNs;

    const double lanes48Dec1Ns =
        measureLanes(kNumLanes, kDecimationFast, kSmoothnessAtDec1, 0, sink);
    const double lanes48Dec2Ns =
        measureLanes(kNumLanes, kDecimationDefault, kSmoothnessAtDec2, 0, sink);
    const double lanes48Dec17Ns =
        measureLanes(kNumLanes, kDecimationSlow, kSmoothnessAtDec17, 0, sink);
    const double lanes36Dec2Ns =
        measureLanes(kNumNonPanLanes, kDecimationDefault, kSmoothnessAtDec2, 0, sink);
    const double panDec1Ns =
        measureLanes(kNumPanLanes, kDecimationFast, kSmoothnessAtDec1, kSaltPanLane, sink);
    const double panDec2Ns =
        measureLanes(kNumPanLanes, kDecimationDefault, kSmoothnessAtDec2, kSaltPanLane, sink);
    const double panDec17Ns =
        measureLanes(kNumPanLanes, kDecimationSlow, kSmoothnessAtDec17, kSaltPanLane, sink);

    const double writesNoDetectNs =
        measureControlWrites(ControlWriteShape::NoChangeDetection, sink);
    const double writesStaticNs =
        measureControlWrites(ControlWriteShape::ChangeDetectionStatic, sink);
    const double writesFr015Ns = measureControlWrites(ControlWriteShape::Fr015OrPair, sink);

    const double tailNs = measurePerSampleTail(sink);

    const double sleepEdgeNs = measureSleepEdgeNs(sink);

    // The sink is read so no stage can be dead-coded away. It is not a result.
    REQUIRE(detail::isFinite(sink));

    // -------------------------------------------------------------------------
    // The only assertions this case makes: a probe, not a gate (tasks.md T002).
    // A zero or a non-finite figure means the MEASUREMENT is broken, which is
    // the one thing that would make the table below lie.
    // -------------------------------------------------------------------------
    for (const double ns :
         {refillNs, engineTier0Ns, engineTier1RawNs, engineTier1NetNs, lanes48Dec1Ns, lanes48Dec2Ns,
          lanes48Dec17Ns, lanes36Dec2Ns, panDec1Ns, panDec2Ns, panDec17Ns, writesNoDetectNs,
          writesStaticNs, writesFr015Ns, tailNs, sleepEdgeNs}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    // =========================================================================
    // GATE 1 - FR-013's Tier-1 trigger, stated verbatim (spec.md:292-296,
    //          plan S10.5). ONE number against ONE threshold: stage (a1).
    //          No other stage and no projection enters it.
    // =========================================================================
    const bool tier1Taken = engineTier0Ns > kTier1TriggerNs;
    const double engineSelectedNs = tier1Taken ? engineTier1NetNs : engineTier0Ns;

    // =========================================================================
    // GATE 2 - the SC-004 budget, on the TOTAL projection (plan S10.4),
    //          re-projected on whichever engine figure Gate 1 selected. The
    //          sleep edge amortises to ~0 against a 20-90 s event schedule and
    //          is reported separately rather than added.
    // =========================================================================
    const double projectedNs = engineSelectedNs + lanes48Dec2Ns + writesFr015Ns + tailNs;
    const bool gate2Miss = projectedNs > kBudgetNs;

    const double maxTermNs = std::max({engineSelectedNs, lanes48Dec2Ns, writesFr015Ns, tailNs});
    const bool engineDominates = (engineSelectedNs >= maxTermNs);
    const bool panIsolatedDominates = !engineDominates && (panDec2Ns >= engineSelectedNs)
                                      && (panDec2Ns >= writesFr015Ns) && (panDec2Ns >= tailNs)
                                      && (panDec2Ns >= lanes36Dec2Ns);

    UNSCOPED_INFO(row("(a1) engine, 12 x single-resonator bank  [GATE 1]", engineTier0Ns));
    UNSCOPED_INFO(row("(a2) engine, 1 x 12-resonator bank, net", engineTier1NetNs));
    UNSCOPED_INFO(row("(b)  48 lanes, decimation 2 (FR-016 default)", lanes48Dec2Ns));
    UNSCOPED_INFO(row("(c)  control writes, FR-015 OR-pair", writesFr015Ns));
    UNSCOPED_INFO(row("(d)  FR-044 per-sample tail", tailNs));
    UNSCOPED_INFO(row("== SC-004 PROJECTION                    [GATE 2]", projectedNs));

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  ResonanceDriftNetwork T002 STAGE-COST PROBE - measured before the component\n"
       << "  exists (specs/vorago-phase3-resonance-drift, plan S10.2/S10.5, FR-060)\n"
       << "  48 kHz, 512-sample blocks, best-of-" << kTrials << " x " << kBlocksPerTrial
       << " blocks after " << kWarmupBlocks << " warm-up\n"
       << "  RUN IN ISOLATION. A figure taken beside a build or another suite is not\n"
       << "  evidence.\n"
       << "=================================================================================\n"
       << "  (a) ENGINE - both shapes, side by side\n"
       << row("      (a1) 12 x single-resonator ResonatorBank", engineTier0Ns) << "\n"
       << row("      (a2) 1 x 12-resonator bank, raw", engineTier1RawNs) << "\n"
       << row("      (a2) 1 x 12-resonator bank, net of refill", engineTier1NetNs) << "\n"
       << "      (a2) is a LOWER BOUND on the Tier-1 engine: processIndividual would also\n"
       << "      write 12 floats per sample, which processBlock does not.\n"
       << "---------------------------------------------------------------------------------\n"
       << "  (b) LANES - 48 BrownianDrift, processBlock(64), depth 1.0\n"
       << row("      (b) 48 lanes, dec  1 (rate 1.0 Hz)", lanes48Dec1Ns) << "\n"
       << row("      (b) 48 lanes, dec  2 (rate 0.03 Hz, DEFAULT)", lanes48Dec2Ns) << "\n"
       << row("      (b) 48 lanes, dec 17 (rate 0.002 Hz)", lanes48Dec17Ns) << "\n"
       << row("      (b) 36 non-pan lanes, dec 2", lanes36Dec2Ns) << "\n"
       << row("      (b) 12 PAN lanes ISOLATED, dec  1", panDec1Ns) << "\n"
       << row("      (b) 12 PAN lanes ISOLATED, dec  2", panDec2Ns) << "\n"
       << row("      (b) 12 PAN lanes ISOLATED, dec 17", panDec17Ns) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  (c) CONTROL WRITES - 12 peaks, once per 64-sample chunk\n"
       << row("      (c) no change detection", writesNoDetectNs) << "\n"
       << row("      (c) change detection, static (FR-034 off)", writesStaticNs) << "\n"
       << row("      (c) FR-015 OR-pair, freq+Q always together", writesFr015Ns) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << row("  (d) FR-044 per-sample tail (no banks)", tailNs) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << edgeRow("  (e) FR-042 sleep edge: reset + re-apply + enable", sleepEdgeNs) << "\n"
       << "      Amortised against a 20-90 s schedule (slow_event_scheduler.h:164-165) this\n"
       << "      is ~0 ns/block; it is reported, never added to the projection.\n"
       << "---------------------------------------------------------------------------------\n"
       << row("  shared 512-float refill (inside (a2) only)", refillNs) << "\n"
       << "=================================================================================\n"
       << "  GATE 1 - FR-013 Tier-1 trigger (spec.md:292-296). Stage (a1) ONLY.\n"
       << "=================================================================================\n"
       << row("  stage (a1) measured", engineTier0Ns) << "\n"
       << row("  FR-013 Tier-1 trigger (60 % of SC-004)", kTier1TriggerNs) << "\n";

    if (tier1Taken) {
        os << "  *** VERDICT: (a1) EXCEEDS 48 000 ns -> FR-013 TIER 1 IS TAKEN.\n"
           << "  *** Groups C and D RUN, before the component is written:\n"
           << "  ***   T003 - failing tests for ResonatorBank::processIndividual\n"
           << "  ***          (+ resetResonatorState, OQ-1 option (i)),\n"
           << "  ***   T004 - the purely additive implementation, every existing method\n"
           << "  ***          byte-for-byte unchanged, behind the SC-013 consumer-suite\n"
           << "  ***          regression gate (membrum_tests, innexus_tests,\n"
           << "  ***          dsp_processors_tests, dsp_systems_tests, seraphis_tests\n"
           << "  ***          before AND after; any moved result is a regression to\n"
           << "  ***          surface, NEVER a golden to update).\n"
           << "  *** The projection below is re-computed on the (a2) net figure.\n";
    } else {
        os << "  VERDICT: (a1) is at or under 48 000 ns -> FR-011 STANDS EXACTLY AS WRITTEN.\n"
           << "  Nothing in FR-013 is exercised, no shipped component is touched, WHATEVER\n"
           << "  THE TOTAL BELOW SAYS. Groups C and D are SKIPPED ENTIRELY. Taking either\n"
           << "  tier without this measurement is a spec violation, not an optimisation.\n";
    }

    os << "=================================================================================\n"
       << "  GATE 2 - SC-004 budget, on the TOTAL (plan S10.4/S10.5)\n"
       << "=================================================================================\n"
       << row(tier1Taken ? "  engine (a2 net, Tier 1 selected by gate 1)"
                         : "  engine (a1, Tier 0 selected by gate 1)",
              engineSelectedNs)
       << "\n"
       << row("  + lanes (b), 48 @ decimation 2", lanes48Dec2Ns) << "\n"
       << row("  + control writes (c), FR-015 OR-pair", writesFr015Ns) << "\n"
       << row("  + per-sample tail (d)", tailNs) << "\n"
       << row("  = SC-004 PROJECTION", projectedNs) << "\n"
       << row("  SC-004 BUDGET (0.75 % of one core, per voice)", kBudgetNs) << "\n";

    if (!gate2Miss) {
        os << "  WITHIN BUDGET: " << std::fixed << std::setprecision(1)
           << (kBudgetNs - projectedNs) << " ns of headroom (" << std::setprecision(2)
           << (100.0 * projectedNs / kBudgetNs) << " % of the SC-004 ceiling).\n"
           << "  Proceed. Note this projection EXCLUDES the sleep edge (amortised ~0) and\n"
           << "  any per-block harness overhead the component itself does not have.\n";
    } else {
        os << "  *** OVER BUDGET BY " << std::fixed << std::setprecision(1)
           << (projectedNs - kBudgetNs) << " ns (" << std::setprecision(2)
           << (projectedNs / kBudgetNs) << "x the ceiling).\n";
        if (engineDominates) {
            os << "  *** Stage (a) DOMINATES the projection -> escalate to FR-013 TIER 2,\n"
               << "  *** the processSympatheticBankSIMD kernel\n"
               << "  *** (systems/sympathetic_resonance_simd.h:39), under every condition\n"
               << "  *** spec.md:319-332 attaches to it: the gate is applied to the\n"
               << "  *** read-back y1s[i] OUTSIDE the recurrence, never through `gains`;\n"
               << "  *** `sums` is a single accumulator and must be ignored; the all-pole\n"
               << "  *** response is normalised to constant peak gain so FR-031 still\n"
               << "  *** holds; and the SC-013 consumer-suite regression gate applies.\n";
        } else if (panIsolatedDominates) {
            os << "  *** Stage (b)-ISOLATED (the 12 FR-038 pan lanes) DOMINATES ->\n"
               << "  *** take FR-038's STATIC-PAN FALLBACK: drop the 12 pan lanes' advance\n"
               << "  *** and make setPeakPanWander an ACCEPTING NO-OP. Pan positions stay\n"
               << "  *** at their seeded draw; nothing else in the spec changes.\n";
        } else {
            os << "  *** NEITHER stage (a) NOR the isolated pan lanes dominate.\n"
               << "  *** >>> STOP AND SURFACE TO THE USER with the measured table above. <<<\n"
               << "  *** Do not proceed to the next group.\n";
        }
        os << "  *** NO AGENT MAY lower kMaxPeaks, raise kBudgetNs, relax a threshold, or\n"
           << "  *** shrink a workload to make this fit. Reduce cost, never move the line\n"
           << "  *** (noise_organism_perf_test.cpp:44-58, inherited verbatim).\n";
    }

    os << "=================================================================================\n"
       << "  RECORD the full table above, BOTH gate verdicts and the chosen tier in the\n"
       << "  scratchpad log and, later, in compliance.md (T024).\n"
       << "=================================================================================\n";

    WARN(os.str());
}

// =============================================================================
// T019 - THE CALIBRATION PASS
// =============================================================================
// TEN THRESHOLDS AND ONE SHIPPED CONSTANT IN THIS PHASE ARE MEASURED, NOT
// AUTHORED, AND THIS IS THE CASE THAT MEASURES THEM (tasks.md T019):
//
//   constant                         consuming criterion / file      seeds
//   -------------------------------- ------------------------------- ---------
//   kBoundaryRatio                   SC-002 (a)  spectral TU         >= 8
//   kLaneIndependenceR               SC-003 (e)  spectral TU         >= 12
//   kRateSeparation                  SC-003 (f)  spectral TU         >= 8
//   kSeedDecorrelationR              SC-006 (b)  behavioural TU      >= 12 pairs
//   kRateInvarianceTolerance         SC-008 (c)  behavioural TU      >= 8
//   kWakeSleepWindowDb               SC-015 (g)  spectral TU         >= 8
//   kSoakWindowDb                    SC-016 (a)  spectral TU         >= 8
//   kSoakFloorDbfs                   SC-016 (c)  spectral TU         >= 8
//   kWetVsDriveWindowDb              SC-020 (a)  behavioural TU      >= 8
//   kPanNonVacuityDb                 SC-021 (c)  behavioural TU      >= 8
//   ResonanceDriftNetwork::kDefaultWetGainDb  (FR-045, the header)   >= 8
//
// It follows the NoiseOrganism_MeasureSourceDrive precedent
// (noise_organism_perf_test.cpp:19-20, :1123): it stays checked in permanently,
// it is TAGGED [.calibration] - hidden, never run by the default suite or by
// CI - and it PRINTS its tables rather than writing them anywhere. The human
// transcribing a figure does the comparison, because the automated guard
// against a stale or guessed bound is elsewhere and is stronger: each consuming
// arm's own injection or anti-vacuity control.
//
//   build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[.calibration]"
//   ...or one section at a time (all on ONE line - a trailing backslash inside a
//   // comment is a line continuation and GCC's -Wcomment flags it):
//   dsp_systems_tests.exe "ResonanceDriftNetwork_MeasureThresholds" -c "SC-002 (a) kBoundaryRatio"
//
// -----------------------------------------------------------------------------
// *** BOUND PROVENANCE RULE, BINDING (spec.md's "Bound provenance note",
// *** tasks.md T019) ***
//
//   A measured null distribution is NECESSARY but NOT SUFFICIENT. Before a
//   threshold printed below is transcribed into its consuming test, THE ARM IT
//   BELONGS TO IS RUN AGAINST AN INJECTED DEFECT AND MUST GO RED. A bound that
//   cannot separate a correct implementation from an injected defect is
//   RE-DERIVED FROM A MEASURED DISTRIBUTION AND THE CHANGE RECORDED - never
//   merely widened. Vorago Phase 2 had to rewrite five criteria after
//   measurement, one of which "passed on seed luck"
//   (specs/vorago-phase2-noise-organism/compliance.md:71-86).
//
//   The suggestion this case prints beside each distribution is exactly that -
//   a SUGGESTION, mechanically derived (the observed extreme, moved out by
//   three sample standard deviations or by 10 % of that extreme, whichever is
//   larger). It is the floor of the discussion, not its conclusion.
// -----------------------------------------------------------------------------
//
// WHY THE FIXTURES BELOW ARE COPIES AND NOT SHARED CODE. Every fixture here
// reproduces its consuming arm's configuration and estimator inside this TU's
// own anonymous namespace. The three consuming TUs each have their own
// anonymous namespace and this phase adds no shared test header, so the choice
// is between a copy and a new shared header that four TUs would depend on. A
// copy is taken deliberately, and each copy names the fixture it was taken from
// so a later reader can diff them. WHERE A COPY DEVIATES FROM ITS ORIGINAL, THE
// DEVIATION IS CALLED OUT AT THE SITE, with the reason.
//
// HOUSE RULES, inherited: no std::isnan / std::isinf anywhere (the macOS leg
// builds -ffast-math) - finiteness is detail::isFinite (core/db_utils.h:118);
// no bit-exact float goldens; designated initialisers for every
// brace-initialised aggregate.
// =============================================================================

namespace {
namespace calib {

// -----------------------------------------------------------------------------
// Shared constants
// -----------------------------------------------------------------------------

/// The rate every AUDIO fixture in the consuming tests runs at.
constexpr double kCalFs = 48000.0;

/// The rate every LANE fixture runs at (the spectral TU's kLaneFs and the
/// behavioural TU's kSlowFs). The statistics those arms measure are defined in
/// SECONDS, FR-037's lane advance takes a FIXED kControlChunkSamples argument -
/// so the decimation mapping and the realised correlation time in seconds are
/// identical at any rate - and 8000 / 64 = 125 control steps per second
/// exactly, with no rounding in the lag arithmetic.
constexpr double kCalLaneFs = ResonanceDriftNetwork::kMinUsableSampleRate;

constexpr std::size_t kCalChunk = ResonanceDriftNetwork::kControlChunkSamples;
constexpr std::size_t kCalLaneStepsPerSecond =
    static_cast<std::size_t>(kCalLaneFs) / kCalChunk;
static_assert(kCalLaneStepsPerSecond == 125u,
              "the lag arithmetic below assumes an exact division");

constexpr std::size_t kCalPeaks = ResonanceDriftNetwork::kMaxPeaks;

/// Xorshift32::nextFloat() is uniform on [-1, +1] (random.h:59-63), RMS
/// 1/sqrt(3) - so a target RMS is reached by scaling with sqrt(3). The same
/// figure the consuming TUs use (resonance_drift_network_test.cpp's
/// fillNoiseBlock).
constexpr float kWhiteRmsScale = 1.7320508f;

/// SC-001's drive level, and this phase's drive level everywhere: -12 dBFS RMS.
constexpr float kDriveDbfs = -12.0f;

/// Twelve network seeds. The first eight serve every ">= 8 seeds" row and all
/// twelve serve SC-003 (e) and SC-006 (b). They are arbitrary and FIXED: a
/// distribution measured on a rotating seed set is not reproducible, and
/// reproducibility is the entire value of a checked-in calibration case.
constexpr std::array<std::uint32_t, 12> kCalSeeds{
    0xC0117001u, 0xC0117002u, 0xC0117003u, 0xC0117004u, 0xC0117005u, 0xC0117006u,
    0xC0117007u, 0xC0117008u, 0xC0117009u, 0xC011700Au, 0xC011700Bu, 0xC011700Cu};

/// The SECOND seed of each pair for SC-006 (b), disjoint from kCalSeeds so no
/// pair is accidentally a same-seed pair - which would read ~1 and be mistaken
/// for a defect in the FR-005 salts.
constexpr std::array<std::uint32_t, 12> kCalPairSeeds{
    0x5EC04D01u, 0x5EC04D02u, 0x5EC04D03u, 0x5EC04D04u, 0x5EC04D05u, 0x5EC04D06u,
    0x5EC04D07u, 0x5EC04D08u, 0x5EC04D09u, 0x5EC04D0Au, 0x5EC04D0Bu, 0x5EC04D0Cu};

// -----------------------------------------------------------------------------
// Statistics - the SAME estimators the consuming arms use, cited at each one
// -----------------------------------------------------------------------------

[[nodiscard]] double meanOfF(const std::vector<float>& v)
{
    if (v.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float x : v) {
        sum += static_cast<double>(x);
    }
    return sum / static_cast<double>(v.size());
}

/// Nearest-rank percentile, taken BY VALUE because it sorts (the spectral TU's
/// percentileOf).
[[nodiscard]] float percentileOfF(std::vector<float> values, double p)
{
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const double rank = std::ceil(p * static_cast<double>(values.size()));
    const auto clamped = static_cast<std::size_t>(std::max(1.0, rank));
    return values[std::min(clamped, values.size()) - 1u];
}

/// Normalised sample autocorrelation at `lag`, sample mean removed, BIASED -
/// one denominator for every lag. The estimator both consuming TUs use (the
/// spectral TU's autocorrelationAt, the behavioural TU's autocorrelation), so
/// the figures printed here and the figures those arms assert on are the same
/// quantity rather than two plausible ones.
[[nodiscard]] double autocorrelationAt(const std::vector<float>& x, std::size_t lag)
{
    const std::size_t n = x.size();
    if (n <= lag * 2u) {
        return 0.0;
    }
    const double mean = meanOfF(x);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(x[i]) - mean;
        den += d * d;
        if (i + lag < n) {
            num += d * (static_cast<double>(x[i + lag]) - mean);
        }
    }
    return (den > 0.0) ? (num / den) : 0.0;
}

/// Pearson correlation of two equal-length trajectories, means removed inside.
/// Accumulated in double: the records are log2 of a few hundred Hz (a value
/// near 5.3) over tens of thousands of steps, and a float sum of squares loses
/// the mean-removed tail.
[[nodiscard]] double pearsonR(const std::vector<float>& a, const std::vector<float>& b)
{
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 2u) {
        return 0.0;
    }
    double ma = 0.0;
    double mb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        ma += static_cast<double>(a[i]);
        mb += static_cast<double>(b[i]);
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);

    double sab = 0.0;
    double saa = 0.0;
    double sbb = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = static_cast<double>(a[i]) - ma;
        const double db = static_cast<double>(b[i]) - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    const double den = std::sqrt(saa * sbb);
    return (den > 0.0) ? (sab / den) : 0.0;
}

[[nodiscard]] double rmsDbfsOf(double sumSq, std::size_t count)
{
    if (count == 0u) {
        return -300.0;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(count));
    return 20.0 * std::log10(rms + 1.0e-30);
}

/// SC-008 (c)'s RATE-INVARIANT statistic: the smallest lag, IN SECONDS, at
/// which a trajectory's normalised autocorrelation falls to 1/e (the
/// behavioural TU's decorrelationLagSeconds, verbatim except that it returns
/// -1.0 instead of REQUIREing on a bad argument - a calibration pass reports,
/// it does not gate). Scanned on a 20 ms grid and linearly interpolated between
/// the bracketing points, so the estimator's own resolution is ~2 % of a 1 s
/// lag.
[[nodiscard]] double decorrelationLagSeconds(const std::vector<float>& x, double stepsPerSecond)
{
    constexpr double kInvE = 0.36787944117144233;  // exp(-1)
    if (!(stepsPerSecond > 0.0)) {
        return -1.0;
    }

    const std::size_t maxLag = x.size() / 3u;
    const auto coarse =
        std::max<std::size_t>(std::size_t{1}, static_cast<std::size_t>(stepsPerSecond / 50.0));

    double previousR = 1.0;  // r(0) is 1 by construction
    std::size_t previousLag = 0;
    for (std::size_t lag = coarse; lag < maxLag; lag += coarse) {
        const double r = autocorrelationAt(x, lag);
        if (r <= kInvE) {
            const double span = previousR - r;  // > 0: previousR was above kInvE
            const double frac = (span > 0.0) ? ((previousR - kInvE) / span) : 0.0;
            const double lagSteps =
                static_cast<double>(previousLag) + frac * static_cast<double>(coarse);
            return lagSteps / stepsPerSecond;
        }
        previousR = r;
        previousLag = lag;
    }
    return -1.0;
}

/// The POOLED estimator SC-008 (c) actually ships (the behavioural TU's
/// decorrelationLagSecondsPooled): the twelve lanes' normalised autocorrelation
/// functions are averaged at each lag and the 1/e crossing is taken from the
/// AVERAGE. This copy exists so the calibrated null describes the shipped
/// statistic and not the single-lane one it replaced.
template <std::size_t N>
[[nodiscard]] double decorrelationLagSecondsPooled(
    const std::array<std::vector<float>, N>& trajectories, double stepsPerSecond)
{
    constexpr double kInvE = 0.36787944117144233;  // exp(-1)
    static_assert(N > 0u, "pooling needs at least one trajectory");
    if (!(stepsPerSecond > 0.0)) {
        return -1.0;
    }

    const std::size_t maxLag = trajectories[0].size() / 3u;
    const auto coarse =
        std::max<std::size_t>(std::size_t{1}, static_cast<std::size_t>(stepsPerSecond / 50.0));

    double previousR = 1.0;
    std::size_t previousLag = 0;
    for (std::size_t lag = coarse; lag < maxLag; lag += coarse) {
        double sum = 0.0;
        for (const std::vector<float>& t : trajectories) {
            sum += autocorrelationAt(t, lag);
        }
        const double r = sum / static_cast<double>(N);
        if (r <= kInvE) {
            const double span = previousR - r;
            const double frac = (span > 0.0) ? ((previousR - kInvE) / span) : 0.0;
            const double lagSteps =
                static_cast<double>(previousLag) + frac * static_cast<double>(coarse);
            return lagSteps / stepsPerSecond;
        }
        previousR = r;
        previousLag = lag;
    }
    return -1.0;
}

// -----------------------------------------------------------------------------
// Distributions and their report block
// -----------------------------------------------------------------------------

/// Which side of the measured null the consuming bound sits on. It decides the
/// direction of the suggestion, and getting it backwards is the one mistake
/// that would produce a plausible-looking table and a vacuous criterion.
enum class BoundSide : std::uint8_t {
    Upper,  ///< the arm asserts `statistic <= bound` (a null the build stays UNDER)
    Lower   ///< the arm asserts `statistic >= bound` (a null the build stays OVER)
};

struct Distribution {
    std::vector<double> values;

    void add(double v) { values.push_back(v); }

    [[nodiscard]] std::size_t size() const noexcept { return values.size(); }

    [[nodiscard]] bool allFinite() const noexcept
    {
        for (const double v : values) {
            if (!detail::isFinite(v)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] double mean() const noexcept
    {
        if (values.empty()) {
            return 0.0;
        }
        double sum = 0.0;
        for (const double v : values) {
            sum += v;
        }
        return sum / static_cast<double>(values.size());
    }

    /// SAMPLE standard deviation (n - 1). With eight seeds the population form
    /// understates the spread by ~7 %, and every suggestion below is three of
    /// these wide.
    [[nodiscard]] double sd() const noexcept
    {
        if (values.size() < 2u) {
            return 0.0;
        }
        const double m = mean();
        double acc = 0.0;
        for (const double v : values) {
            const double d = v - m;
            acc += d * d;
        }
        return std::sqrt(acc / static_cast<double>(values.size() - 1u));
    }

    [[nodiscard]] double min() const noexcept
    {
        if (values.empty()) {
            return 0.0;
        }
        return *std::min_element(values.begin(), values.end());
    }

    [[nodiscard]] double max() const noexcept
    {
        if (values.empty()) {
            return 0.0;
        }
        return *std::max_element(values.begin(), values.end());
    }

    /// The mechanical suggestion: past the observed extreme by three sample
    /// standard deviations, or by 10 % of that extreme's magnitude, whichever
    /// is larger. The 10 % floor exists because a distribution that happens to
    /// come out tight across eight seeds would otherwise produce a bound
    /// sitting on top of its own maximum - which is the shape that "passes on
    /// seed luck".
    [[nodiscard]] double suggested(BoundSide side) const noexcept
    {
        const double extreme = (side == BoundSide::Upper) ? max() : min();
        const double margin = std::max(3.0 * sd(), 0.1 * std::abs(extreme));
        return (side == BoundSide::Upper) ? (extreme + margin) : (extreme - margin);
    }
};

[[nodiscard]] std::string fixedStr(double v, int precision)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

/// One distribution's report block. `provisional` is the figure currently
/// checked into the consuming test, printed beside the measurement so the
/// transcription step is a COMPARISON rather than a copy.
[[nodiscard]] std::string reportDistribution(const std::string& constantName,
                                             const std::string& consumer,
                                             const Distribution& d,
                                             BoundSide side,
                                             double provisional,
                                             int precision)
{
    std::ostringstream os;
    os << "---------------------------------------------------------------------------------\n"
       << "  " << constantName << "\n"
       << "      consumer: " << consumer << "\n"
       << "      n = " << d.size() << "   mean = " << fixedStr(d.mean(), precision)
       << "   sd = " << fixedStr(d.sd(), precision)
       << "   min = " << fixedStr(d.min(), precision)
       << "   max = " << fixedStr(d.max(), precision) << "\n"
       << "      currently in the tree (PROVISIONAL): " << fixedStr(provisional, precision) << "\n"
       << "      suggested " << ((side == BoundSide::Upper) ? "UPPER" : "LOWER")
       << " bound (extreme +/- max(3 sd, 10 %)): " << fixedStr(d.suggested(side), precision)
       << "\n"
       << "      per-seed: ";
    for (std::size_t i = 0; i < d.values.size(); ++i) {
        os << fixedStr(d.values[i], precision);
        if (i + 1u < d.values.size()) {
            os << ", ";
        }
    }
    os << "\n"
       << "      >>> DO NOT TRANSCRIBE until the consuming arm has been run against an\n"
       << "      >>> injected defect AT THIS VALUE and has gone RED.\n";
    return os.str();
}

// -----------------------------------------------------------------------------
// Drives (copied from the spectral TU's PinkDrive, cited at the class)
// -----------------------------------------------------------------------------

/// Pink noise at a chosen RMS in dBFS, deterministic under its seed. Copied
/// from the spectral TU's PinkDrive, including its calibration pass: Kellet's
/// gains are published for a particular white-noise convention and the realised
/// RMS of filter(Xorshift32::nextFloat()) is not a round number, so a private
/// copy of the same stream is run once, the measured RMS becomes the scale, and
/// the live generator then starts from the seed again. Without it the SC-016
/// window figures below would be statements about an unmeasured drive.
class PinkDrive {
public:
    PinkDrive(std::uint32_t seed, double sampleRate, float rmsDbfs) noexcept : rng_(seed)
    {
        filter_.prepare(static_cast<float>(sampleRate));

        Xorshift32 calibrationRng{seed};
        PinkNoiseFilter calibrationFilter;
        calibrationFilter.prepare(static_cast<float>(sampleRate));
        constexpr std::size_t kCalibrationSamples = 200000;  // ~4 s at 48 kHz
        double sumSq = 0.0;
        for (std::size_t i = 0; i < kCalibrationSamples; ++i) {
            const float raw = calibrationFilter.process(calibrationRng.nextFloat());
            const auto v = static_cast<double>(raw);
            sumSq += v * v;
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(kCalibrationSamples));
        scale_ = (rms > 1.0e-12)
                     ? static_cast<float>(static_cast<double>(dbToGain(rmsDbfs)) / rms)
                     : 1.0f;
    }

    [[nodiscard]] float next() noexcept { return filter_.process(rng_.nextFloat()) * scale_; }

private:
    PinkNoiseFilter filter_{};
    Xorshift32 rng_;
    float scale_ = 1.0f;
};

/// SC-016's and SC-015 (g)'s wake pattern, copied from the spectral TU's
/// WakePattern: peaks toggled on a 20-90 s stochastic schedule (the
/// SlowEventScheduler default range, slow_event_scheduler.h:164-165), driven by
/// the test because FR-040 makes the wake an INPUT to this component.
///
/// **Stationary by construction**, and the spectral TU's copy carries the full
/// derivation: starting from all-awake and flipping a random peak per event
/// makes the awake count relax from twelve toward its stationary six, which is
/// a MONOTONE level transient that SC-016 (b)'s +/- 0.5 dB "no creep" bound
/// then measures instead of the network. Half awake plus alternating
/// sleep/wake edges holds the count in {5, 6}. The two copies must stay
/// identical or the calibrated bound stops describing the shipped fixture.
class WakePattern {
public:
    /// Takes the network because the initial state must be PUSHED - see the
    /// spectral TU's copy for the measurement that forced it.
    WakePattern(ResonanceDriftNetwork& net, std::uint32_t seed, double sampleRate) noexcept
        : rng_(seed), sampleRate_(sampleRate)
    {
        for (std::size_t i = 0; i < kCalPeaks; ++i) {
            awake_[i] = (i % 2u) == 0u;  // six awake, six asleep
            net.setPeakWake(i, awake_[i] ? 1.0f : 0.0f);
        }
        scheduleNext(0);
    }

    /// Called once per control chunk with the sample index of its first sample.
    bool advance(ResonanceDriftNetwork& net, std::size_t sampleIndex) noexcept
    {
        if (sampleIndex < nextEventSample_) {
            return false;
        }
        const bool pickAwake = sleepNext_;
        std::array<std::size_t, kCalPeaks> candidates{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < kCalPeaks; ++i) {
            if (awake_[i] == pickAwake) {
                candidates[count] = i;
                ++count;
            }
        }
        if (count == 0) {
            scheduleNext(sampleIndex);
            return false;
        }
        const auto pick =
            static_cast<std::size_t>(std::min(rng_.nextUnipolar() * static_cast<float>(count),
                                              static_cast<float>(count - 1u)));
        const std::size_t peak = candidates[pick];
        awake_[peak] = !pickAwake;
        net.setPeakWake(peak, awake_[peak] ? 1.0f : 0.0f);
        sleepNext_ = !sleepNext_;
        ++events_;
        toggled_[peak] = true;
        scheduleNext(sampleIndex);
        return true;
    }

    [[nodiscard]] std::size_t getEventCount() const noexcept { return events_; }

    [[nodiscard]] std::size_t getAwakeCount() const noexcept
    {
        std::size_t n = 0;
        for (const bool a : awake_) {
            if (a) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] std::size_t getToggledPeakCount() const noexcept
    {
        std::size_t n = 0;
        for (const bool t : toggled_) {
            if (t) {
                ++n;
            }
        }
        return n;
    }

private:
    void scheduleNext(std::size_t fromSample) noexcept
    {
        const float span =
            SlowEventScheduler::kDefaultMaxInterval - SlowEventScheduler::kDefaultMinInterval;
        const float seconds =
            SlowEventScheduler::kDefaultMinInterval + rng_.nextUnipolar() * span;
        nextEventSample_ =
            fromSample + static_cast<std::size_t>(static_cast<double>(seconds) * sampleRate_);
    }

    Xorshift32 rng_;
    double sampleRate_;
    std::size_t nextEventSample_ = 0;
    std::size_t events_ = 0;
    bool sleepNext_ = true;  ///< the first event is a sleep, from six awake
    std::array<bool, kCalPeaks> awake_{};
    std::array<bool, kCalPeaks> toggled_{};
};

/// log2 of every peak's realised frequency, sampled once per control step on a
/// SILENT drive.
///
/// DEVIATION FROM SC-003's OWN FIXTURE, AND WHY IT IS EXACT AND NOT AN
/// APPROXIMATION: SC-003 (e) records these trajectories off its pink-noise
/// render, but a trajectory is a CONTROL-SURFACE reading. Nothing on the render
/// path writes back into a lane, an anchor or an applied value, so the
/// trajectory a given seed produces is identical whatever the input is - which
/// is exactly why the spectral TU's own (d) and (f) arms already record theirs
/// silently (its recordFreqLog2Silent). Silence removes the drive as a variable
/// and makes a 350 s record cheap enough to run twelve times.
void recordFreqLog2Silent(ResonanceDriftNetwork& net, std::size_t steps,
                          std::array<std::vector<float>, kCalPeaks>& out)
{
    std::array<float, kCalChunk> zeros{};
    std::array<float, kCalChunk> outL{};
    std::array<float, kCalChunk> outR{};
    for (auto& v : out) {
        v.clear();
        v.reserve(steps);
    }
    for (std::size_t s = 0; s < steps; ++s) {
        net.processBlock(zeros.data(), zeros.data(), outL.data(), outR.data(), kCalChunk);
        for (std::size_t i = 0; i < kCalPeaks; ++i) {
            out[i].push_back(std::log2(net.getPeakCurrentFrequency(i)));
        }
    }
}

// =============================================================================
// FR-045 / SC-020 (a) - the wet-vs-drive gap, and kDefaultWetGainDb
// =============================================================================

struct WetGainMeasurement {
    double driveRmsDb = -300.0;  ///< the REALISED input RMS, not the requested one
    double wetRmsDb = -300.0;    ///< the mix = 1 output RMS
    double gapDb = 0.0;          ///< wet - drive, at the trim the render used
    std::uint32_t clampEngagements = 0;
    bool allFinite = true;
};

/// Render the FR-016 reference patch (numPeaks = 12, all defaults,
/// AnchorMode::Free, mix = 1) on SC-001's broadband drive - white noise at
/// -12 dBFS on BOTH channels - at the supplied wet trim, and report the wet RMS
/// against the drive RMS.
///
/// The first second is discarded: FR-045's trim rides the same LinearRamp as
/// FR-019's normalisation (kGainRampMs, 50 ms) and setWetGain is necessarily
/// called after prepare, so an unsettled render carries a louder-than-asked
/// head. Twelve Q = 12 resonators also need time to ring up.
[[nodiscard]] WetGainMeasurement measureWetVsDrive(std::uint32_t seed, float trimDb)
{
    ResonanceDriftNetwork net;
    net.setSeed(seed);
    net.prepare(kCalFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setMix(1.0f);
    net.setWetGain(trimDb);

    Xorshift32 rng{seed ^ 0xD8175EEDu};
    const float scale = dbToGain(kDriveDbfs) * kWhiteRmsScale;

    std::array<float, kCalChunk> inL{};
    std::array<float, kCalChunk> inR{};
    std::array<float, kCalChunk> outL{};
    std::array<float, kCalChunk> outR{};

    constexpr std::size_t kSettleChunks = static_cast<std::size_t>(1.0 * kCalFs) / kCalChunk;
    constexpr std::size_t kMeasureChunks = static_cast<std::size_t>(10.0 * kCalFs) / kCalChunk;

    WetGainMeasurement m;
    double driveSumSq = 0.0;
    double wetSumSq = 0.0;
    std::size_t counted = 0;

    for (std::size_t c = 0; c < kSettleChunks + kMeasureChunks; ++c) {
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            inL[i] = rng.nextFloat() * scale;
            inR[i] = rng.nextFloat() * scale;
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kCalChunk);
        if (c < kSettleChunks) {
            continue;
        }
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            if (!detail::isFinite(outL[i]) || !detail::isFinite(outR[i])) {
                m.allFinite = false;
            }
            driveSumSq += static_cast<double>(inL[i]) * static_cast<double>(inL[i])
                          + static_cast<double>(inR[i]) * static_cast<double>(inR[i]);
            wetSumSq += static_cast<double>(outL[i]) * static_cast<double>(outL[i])
                        + static_cast<double>(outR[i]) * static_cast<double>(outR[i]);
            counted += 2u;
        }
    }

    m.driveRmsDb = rmsDbfsOf(driveSumSq, counted);
    m.wetRmsDb = rmsDbfsOf(wetSumSq, counted);
    m.gapDb = m.wetRmsDb - m.driveRmsDb;
    m.clampEngagements = net.getClampEngagementCount();
    return m;
}

/// kDefaultWetGainDb's UPPER bound, and the reason the passive gap above is
/// only half the measurement.
///
/// FR-018 requires ZERO clamp engagements in every in-spec configuration and
/// SC-001 (b)/(e) is what enforces it, so the default trim is bounded ABOVE by
/// the loudest in-spec render as well as below by SC-020's blend window. The
/// binding configuration is SC-001 (e): every peakQ at kMaxResonatorQ, all four
/// lanes at maximum depth, wanderRate at its 1.0 Hz ceiling. It is the GAIN
/// lane that costs the headroom - FR-033's 24 dB of wander against FR-017's
/// +12 dB ceiling puts a peak up to 18 dB above the -6 dB default the passive
/// gap was measured at, and FR-019 normalises for numPeaks only - which is why
/// this configuration's ceiling sits ~18 dB below the base SC-001 (a)(b)(d)
/// patch's.
///
/// The wet path is LINEAR in the trim (FR-044 folds it into one wetScaleRamp
/// factor) and FR-018's clamp does not feed back into any resonator, so one
/// render at a probe trim low enough not to clip gives the ceiling in closed
/// form: probeTrim + 20*log10(kOutputClamp / peak). That is why this costs one
/// render per seed rather than a bisection.
///
/// The first 2 s are discarded for the same reason measureWetVsDrive discards
/// 1 s, and it matters MORE here: setWetGain is necessarily called after
/// prepare, so the wetScaleRamp starts at kDefaultWetGainDb and takes 50 ms to
/// reach the probe trim. Measured with that head included, the reported peak is
/// the DEFAULT trim's and the ceiling comes out ~9 dB too low.
[[nodiscard]] double measureClipCeilingDb(std::uint32_t seed)
{
    constexpr float kProbeTrim = 8.0f;
    constexpr double kDriveSeconds = 60.0;   ///< SC-001's drive
    constexpr double kTailSeconds = 17.0;    ///< past (d)'s ~16.0 s ring-out bound

    ResonanceDriftNetwork net;
    net.setSeed(seed);
    net.prepare(kCalFs, ResonanceDriftNetwork::PrepareConfig{});
    for (std::size_t i = 0; i < kCalPeaks; ++i) {
        net.setPeakQ(i, Krate::DSP::kMaxResonatorQ);
        net.setQWander(i, ResonanceDriftNetwork::kMaxQWanderOctaves);
        net.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
        net.setGainWander(i, ResonanceDriftNetwork::kMaxGainWanderDb);
        net.setPeakPanWander(i, 1.0f);
    }
    net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
    net.setMix(1.0f);
    net.setWetGain(kProbeTrim);

    Xorshift32 rng{seed ^ 0xA5A5A5A5u};
    const float scale = dbToGain(kDriveDbfs) * kWhiteRmsScale;

    std::array<float, kCalChunk> inL{};
    std::array<float, kCalChunk> inR{};
    std::array<float, kCalChunk> outL{};
    std::array<float, kCalChunk> outR{};

    constexpr auto kSettleChunks = static_cast<std::size_t>(2.0 * kCalFs) / kCalChunk;
    constexpr auto kDriveChunks = static_cast<std::size_t>(kDriveSeconds * kCalFs) / kCalChunk;
    constexpr auto kTailChunks = static_cast<std::size_t>(kTailSeconds * kCalFs) / kCalChunk;

    double peak = 0.0;
    const auto scan = [&peak, &outL, &outR]() {
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            peak = std::max({peak, std::fabs(static_cast<double>(outL[i])),
                             std::fabs(static_cast<double>(outR[i]))});
        }
    };

    for (std::size_t c = 0; c < kDriveChunks; ++c) {
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            inL[i] = rng.nextFloat() * scale;
            inR[i] = rng.nextFloat() * scale;
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kCalChunk);
        if (c >= kSettleChunks) {
            scan();
        }
    }
    // The ring-out counts too: SC-001's clamp assertion spans drive AND tail.
    inL.fill(0.0f);
    inR.fill(0.0f);
    for (std::size_t c = 0; c < kTailChunks; ++c) {
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kCalChunk);
        scan();
    }

    if (peak <= 0.0) {
        return 0.0;  // a silent render has no ceiling to report; the caller REQUIREs > 0
    }
    return static_cast<double>(kProbeTrim)
           + 20.0
                 * std::log10(static_cast<double>(ResonanceDriftNetwork::kOutputClamp) / peak);
}

// =============================================================================
// SC-002 (a) - kBoundaryRatio, under the {0, 1, 2} partition ONLY
// =============================================================================

/// SC-002's anchor spread, copied from the spectral TU's zipperAnchorHz: 110 Hz
/// to 440 Hz geometric, so the 220 Hz drive sits in the middle of the twelve
/// peaks and every one of them crosses it as the lanes wander.
[[nodiscard]] float zipperAnchorHz(std::size_t i)
{
    return 110.0f
           * std::exp2(2.0f * static_cast<float>(i) / static_cast<float>(kCalPeaks - 1u));
}

/// SC-002's wet trim (spec correction C-17). NOT FR-045's default: at +30 dB a
/// 220 Hz sine parked on twelve resonances with the gain lane at its 24 dB
/// maximum drives 16-21 % of the render into FR-018's +/- 4.0 clamp, and a
/// clipped render's second difference is the CLIPPER's in both populations at
/// once - which drags B/P to ~1.06 and makes the measurement below a
/// measurement of the clamp. Every statistic SC-002 defines is a ratio and is
/// trim-invariant WHILE THE RENDER IS LINEAR, which is why the clamp count is
/// reported beside every figure.
constexpr float kZipperWetGainDb = -12.0f;

constexpr double kZipperDriveHz = 220.0;
constexpr double kZipperQuantile = 0.999;
constexpr std::size_t kZipperSettleChunks = 750;                 // 1 s at 48 kHz
constexpr std::size_t kZipperSamples = 2880000;                  // EXACTLY 60 s
static_assert(kZipperSamples % kCalChunk == 0u, "whole control chunks only");
static_assert(kZipperSamples / kCalChunk == 45000u, "45 000 control chunks");

struct ZipperStats {
    double boundary = 0.0;  ///< B: the 99.9th percentile of d over n mod 64 in {0,1,2}
    double interior = 0.0;  ///< P: the SAME quantile over the other 61/64
    double ratio = 0.0;     ///< B / P, the quantity kBoundaryRatio bounds
    std::size_t boundaryCount = 0;
    std::size_t interiorCount = 0;
    std::uint32_t clampEngagements = 0;
};

/// SC-002's patch, copied from the spectral TU's configureZipperPatch: all four
/// lanes at maximum depth and wanderRate at its 1.0 Hz ceiling, which is the
/// fastest legal retuning AND the un-decimated lane path (FR-037,
/// decimation == 1) this criterion exists to stress.
void configureZipperPatch(ResonanceDriftNetwork& net, std::uint32_t seed)
{
    net.setSeed(seed);
    net.prepare(kCalFs, ResonanceDriftNetwork::PrepareConfig{});
    for (std::size_t i = 0; i < kCalPeaks; ++i) {
        net.setPeakAnchorHz(i, zipperAnchorHz(i));
        net.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
        net.setQWander(i, ResonanceDriftNetwork::kMaxQWanderOctaves);
        net.setGainWander(i, ResonanceDriftNetwork::kMaxGainWanderDb);
        net.setPeakPanWander(i, 1.0f);
    }
    net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
    net.setMix(1.0f);
    net.setWetGain(kZipperWetGainDb);
}

/// The NULL distribution of SC-002 (a)'s statistic: a 220 Hz sine at -12 dBFS
/// on both channels through the zipper patch for the pinned 60 s, with NO
/// injection - the wander alone.
///
/// The partition is `n mod 64 in {0, 1, 2}` and NOT {0, 1} (spec correction
/// C-11): the control step runs at controlPhase_ == 0, so the first sample it
/// can affect is n = 0 (mod 64), and a second difference spans three samples.
/// A figure measured under the {0, 1} partition may NOT be carried over.
[[nodiscard]] ZipperStats measureBoundaryRatio(std::uint32_t seed)
{
    ResonanceDriftNetwork net;
    configureZipperPatch(net, seed);

    std::array<float, kCalChunk> inL{};
    std::array<float, kCalChunk> inR{};
    std::array<float, kCalChunk> outL{};
    std::array<float, kCalChunk> outR{};

    std::vector<float> boundary;
    std::vector<float> interior;
    boundary.reserve((kZipperSamples * 3u) / kCalChunk + 8u);
    interior.reserve((kZipperSamples * 61u) / kCalChunk + 8u);

    const double twoPi = 2.0 * static_cast<double>(kPi);
    const double increment = twoPi * kZipperDriveHz / kCalFs;
    // -12 dBFS is an RMS figure here as everywhere else in this phase; a sine's
    // peak is sqrt(2) above its RMS.
    const float amplitude = dbToGain(kDriveDbfs) * 1.41421356f;

    double phase = 0.0;
    float prev1 = 0.0f;
    float prev2 = 0.0f;
    bool primed = false;
    std::size_t n = 0;

    const std::size_t chunks = kZipperSettleChunks + kZipperSamples / kCalChunk;
    for (std::size_t c = 0; c < chunks; ++c) {
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            inL[i] = static_cast<float>(std::sin(phase)) * amplitude;
            inR[i] = inL[i];
            phase += increment;
            if (phase >= twoPi) {
                phase -= twoPi;
            }
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kCalChunk);
        if (c < kZipperSettleChunks) {
            // The two trailing samples of the last settle chunk seed the second
            // difference, so the measured population is the FULL 3/64 and 61/64
            // of the pinned render rather than losing n = 0 and n = 1.
            prev2 = outL[kCalChunk - 2u];
            prev1 = outL[kCalChunk - 1u];
            primed = true;
            continue;
        }
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            const float x = outL[i];
            if (primed || n >= 2u) {
                const float d = std::abs(x - 2.0f * prev1 + prev2);
                if ((n % kCalChunk) < 3u) {
                    boundary.push_back(d);
                } else {
                    interior.push_back(d);
                }
            }
            prev2 = prev1;
            prev1 = x;
            ++n;
        }
    }

    ZipperStats s;
    s.boundary = static_cast<double>(percentileOfF(boundary, kZipperQuantile));
    s.interior = static_cast<double>(percentileOfF(interior, kZipperQuantile));
    s.ratio = (s.interior > 0.0) ? (s.boundary / s.interior) : 0.0;
    s.boundaryCount = boundary.size();
    s.interiorCount = interior.size();
    s.clampEngagements = net.getClampEngagementCount();
    return s;
}

// =============================================================================
// SC-003 (e) - kLaneIndependenceR, and SC-003 (f) - kRateSeparation
// =============================================================================

/// SC-003's own render length: 10*T at the FR-016 default rate (T = 33.333 s).
constexpr double kSpectralSeconds = 350.0;
constexpr std::size_t kSpectralSteps =
    static_cast<std::size_t>(kSpectralSeconds * kCalFs) / kCalChunk;  // 262 500

/// Mean |r| over the C(12,2) = 66 pairs of mean-removed log2-frequency
/// trajectories, at SC-003's own configuration and record length.
///
/// The null this measures is NOT zero and Phase 2's 0.05 may not be
/// transcribed: Bartlett's formula puts E|r| near 0.23 here from estimator
/// noise alone - var(r) ~ (tau/dt)/N = 25000/262500 = 0.095, sd 0.31,
/// E|r| = sd*sqrt(2/pi) - so any bound at or below that is unsatisfiable by a
/// correct build.
[[nodiscard]] double measureLaneIndependence(std::uint32_t seed)
{
    ResonanceDriftNetwork net;
    net.setSeed(seed);
    net.prepare(kCalFs, ResonanceDriftNetwork::PrepareConfig{});

    std::array<std::vector<float>, kCalPeaks> traj{};
    recordFreqLog2Silent(net, kSpectralSteps, traj);

    double sumAbsR = 0.0;
    std::size_t pairs = 0;
    for (std::size_t i = 0; i < kCalPeaks; ++i) {
        for (std::size_t j = i + 1u; j < kCalPeaks; ++j) {
            sumAbsR += std::abs(pearsonR(traj[i], traj[j]));
            ++pairs;
        }
    }
    return (pairs > 0u) ? (sumAbsR / static_cast<double>(pairs)) : 0.0;
}

/// The T008 lane fixture, copied from the spectral TU's configureLaneFixture.
/// The slew ceilings are lifted out of the way so these arms measure the LANE
/// and not the FR-035 limiter, and +/- 6 semitones keeps every FR-016 anchor
/// clear of the [20 Hz, 0.45*fs] clamp at 8 kHz - saturation against it would
/// inflate the measured persistence of the lowest peaks and hide a stalled
/// lane.
void configureLaneFixture(ResonanceDriftNetwork& net, std::uint32_t seed, float wanderRateHz)
{
    net.setSeed(seed);
    net.prepare(kCalLaneFs, ResonanceDriftNetwork::PrepareConfig{});
    net.setSlewCeilings(ResonanceDriftNetwork::kMaxSlewOctaves,
                        ResonanceDriftNetwork::kMaxSlewOctaves);
    for (std::size_t i = 0; i < kCalPeaks; ++i) {
        net.setFreqWander(i, 6.0f);
    }
    net.setWanderRate(wanderRateHz);
}

struct RateSeparation {
    double rhoSlow = 0.0;
    double rhoFast = 0.0;
    double separation = 0.0;
    std::size_t slowDecimation = 0;  ///< must be 7 - FR-037's whole point
    std::size_t fastDecimation = 0;  ///< must be 1
};

/// 10*T of the SLOW arm (T = 200 s at 0.005 Hz). At namespace scope, not inside
/// measureRateSeparation, so the lambda in it reads the value without a
/// capture: capturing a constexpr local that is only used in a constant
/// expression is never odr-used, and Clang's -Wunused-lambda-capture fires on
/// exactly that.
constexpr std::size_t kRateSeparationSeconds = 2000;

/// SC-003 (f)'s lag-matched evaluation point: T/2 of the fast arm.
constexpr double kRateSeparationLagSeconds = 1.67;

/// SC-003 (f)'s statistic: mean lag-1.67 s autocorrelation at 0.005 Hz minus
/// the same at 0.3 Hz, both over 2 000 s (10*T of the SLOW arm) at
/// kMinUsableSampleRate. Same seed, same fixture, same record length, same
/// absolute lag - the ONLY difference between the arms is the rate.
[[nodiscard]] RateSeparation measureRateSeparation(std::uint32_t seed)
{
    const auto lag = static_cast<std::size_t>(
        std::lround(kRateSeparationLagSeconds * static_cast<double>(kCalLaneStepsPerSecond)));

    const auto meanAcf = [lag](std::uint32_t s, float rateHz, std::size_t& decimationOut) {
        ResonanceDriftNetwork net;
        configureLaneFixture(net, s, rateHz);
        decimationOut = net.getLaneDecimation();
        std::array<std::vector<float>, kCalPeaks> traj{};
        recordFreqLog2Silent(net, kRateSeparationSeconds * kCalLaneStepsPerSecond, traj);
        double sum = 0.0;
        for (std::size_t i = 0; i < kCalPeaks; ++i) {
            sum += autocorrelationAt(traj[i], lag);
        }
        return sum / static_cast<double>(kCalPeaks);
    };

    RateSeparation r;
    r.rhoSlow = meanAcf(seed, 0.005f, r.slowDecimation);
    r.rhoFast = meanAcf(seed, 0.3f, r.fastDecimation);
    r.separation = r.rhoSlow - r.rhoFast;
    return r;
}

// =============================================================================
// SC-006 (b) - kSeedDecorrelationR
// =============================================================================

struct SeedDecorrelation {
    double worstAbsR = 0.0;  ///< the quantity the consuming arm bounds
    double meanAbsR = 0.0;
    double worstSameSeedR = 1.0;  ///< the anti-vacuity control; must stay near 1
};

/// Two instances differing ONLY in seed, paired peak i against peak i - TWELVE
/// pairs, never all 24 trajectories cross-paired (which would mix in pairs
/// whose anchors differ by four octaves and report a decorrelation owing
/// nothing to the seed).
///
/// wanderRate = 1.0 Hz gives tau = 1 s at lane decimation 1, and the record is
/// 20 s = 20 * tau (spec.md's cheap option). Recorded at kMinUsableSampleRate
/// for the reason the lane fixtures are.
/// 20 * tau at wanderRate = 1.0 Hz. At namespace scope for the same reason
/// kRateSeparationSeconds is.
constexpr std::size_t kSeedDecorrelationSteps = 20u * kCalLaneStepsPerSecond;

[[nodiscard]] SeedDecorrelation measureSeedDecorrelation(std::uint32_t seedA, std::uint32_t seedB)
{
    const auto record = [](std::uint32_t seed,
                           std::array<std::vector<float>, kCalPeaks>& out) {
        ResonanceDriftNetwork net;
        net.setSeed(seed);
        net.prepare(kCalLaneFs, ResonanceDriftNetwork::PrepareConfig{});
        net.setWanderRate(1.0f);
        recordFreqLog2Silent(net, kSeedDecorrelationSteps, out);
    };

    std::array<std::vector<float>, kCalPeaks> trajA{};
    std::array<std::vector<float>, kCalPeaks> trajB{};
    std::array<std::vector<float>, kCalPeaks> trajTwin{};
    record(seedA, trajA);
    record(seedB, trajB);
    record(seedA, trajTwin);  // same seed as A: the anti-vacuity control

    SeedDecorrelation d;
    double sum = 0.0;
    for (std::size_t i = 0; i < kCalPeaks; ++i) {
        const double cross = std::abs(pearsonR(trajA[i], trajB[i]));
        const double same = pearsonR(trajA[i], trajTwin[i]);
        sum += cross;
        d.worstAbsR = std::max(d.worstAbsR, cross);
        d.worstSameSeedR = std::min(d.worstSameSeedR, same);
    }
    d.meanAbsR = sum / static_cast<double>(kCalPeaks);
    return d;
}

// =============================================================================
// SC-008 (c) - the rate-invariance tolerance on the 1/e decorrelation lag
// =============================================================================

/// One 1/e-lag reading, in SECONDS, at the SC-008 (c) fixture: numPeaks = 1,
/// wanderRate = 1.0 Hz (tau = 1 s), the FR-035 slew ceilings lifted so the arm
/// measures the LANE and not the limiter, 50 s of record (50 * tau).
[[nodiscard]] double measureLagSeconds(std::uint32_t seed, double fs, float rateHz)
{
    ResonanceDriftNetwork net;
    net.setSeed(seed);
    const ResonanceDriftNetwork::PrepareConfig cfg{.maxBlockSamples = 2048, .numPeaks = 1};
    net.prepare(fs, cfg);
    net.setWanderRate(rateHz);
    net.setSlewCeilings(1.0f, 1.0f);

    const double stepsPerSecond = fs / static_cast<double>(kCalChunk);
    const auto steps = static_cast<std::size_t>(50.0 * stepsPerSecond);
    std::array<std::vector<float>, kCalPeaks> traj{};
    recordFreqLog2Silent(net, steps, traj);
    // POOLED, matching the shipped arm. Every lane advances whatever numPeaks
    // says, so all twelve records are live even at numPeaks = 1.
    return decorrelationLagSecondsPooled(traj, stepsPerSecond);
}

// =============================================================================
// SC-015 (g) and SC-016 - the window-RMS bounds
// =============================================================================

/// SC-015 (g)'s and SC-016's shared wet trim (the spectral TU's
/// kSoakWetGainDb). Bracketed from BOTH sides by SC-016 itself: (d) requires no
/// clamp engagement, which pushes it DOWN; (c) requires no window below the
/// floor, which pushes it UP. Reported with the clamp count at every figure
/// below so a moved trim is visible rather than silent.
constexpr float kSoakWetGainDb = 6.0f;

struct WindowRunStats {
    double medianDb = -300.0;
    double worstDeviationDb = 0.0;  ///< the quantity SC-015 (g) / SC-016 (a) bound
    double lowestDb = 300.0;        ///< the quantity SC-016 (c)'s floor bounds
    double highestDb = -300.0;
    double totalSlopeDb = 0.0;  ///< SC-016 (b): least-squares change across the record
    std::size_t windows = 0;
    std::size_t events = 0;
    std::size_t toggledPeaks = 0;
    std::size_t minAwake = kCalPeaks;  ///< the stationarity evidence: both must
    std::size_t maxAwake = 0;          ///< stay inside {5, 6} (see WakePattern)
    std::uint32_t clampEngagements = 0;
    bool allFinite = true;
};

/// Least-squares slope of `values` against their index, expressed as the total
/// change across the whole record (the spectral TU's totalLeastSquaresChange).
[[nodiscard]] double totalLeastSquaresChange(const std::vector<float>& values)
{
    const std::size_t n = values.size();
    if (n < 2u) {
        return 0.0;
    }
    const double meanX = 0.5 * static_cast<double>(n - 1u);
    const double meanY = meanOfF(values);
    double sxy = 0.0;
    double sxx = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = static_cast<double>(i) - meanX;
        sxy += dx * (static_cast<double>(values[i]) - meanY);
        sxx += dx * dx;
    }
    const double slopePerWindow = (sxx > 0.0) ? (sxy / sxx) : 0.0;
    return slopePerWindow * static_cast<double>(n - 1u);
}

/// The shared window-RMS render behind SC-015 (g) and SC-016: pink noise at
/// -12 dBFS on both channels, an external wake pattern on the 20-90 s
/// SlowEventScheduler default range, windowed RMS in dBFS.
///
/// `maxDepths` selects SC-016's worst-case patch (every depth at its maximum,
/// wanderRate at its 1.0 Hz ceiling, Q = kMaxResonatorQ); false leaves the
/// FR-016 defaults, which is SC-015 (g)'s configuration.
///
/// DEVIATION FROM SC-015 (g)'s OWN FIXTURE: that arm renders at numSamples == 1
/// because it also asserts SC-002 (d)'s PER-SAMPLE gate-step bound. This
/// function renders in control chunks, because the only statistic it measures
/// is a 200 ms window RMS and SC-010 (block-size invariance, asserted
/// separately by ResonanceDriftNetwork_BlockSizeInvariance) makes the two
/// renders the same signal to within kSampleTolerance. Nothing about the gate
/// bound is measured here.
[[nodiscard]] WindowRunStats measureWindowRun(std::uint32_t seed, bool maxDepths,
                                              double totalSeconds, double windowSeconds)
{
    ResonanceDriftNetwork net;
    net.setSeed(seed);
    net.prepare(kCalFs, ResonanceDriftNetwork::PrepareConfig{});
    if (maxDepths) {
        for (std::size_t i = 0; i < kCalPeaks; ++i) {
            net.setPeakQ(i, kMaxResonatorQ);
            net.setFreqWander(i, ResonanceDriftNetwork::kMaxFreqWanderSemis);
            net.setQWander(i, ResonanceDriftNetwork::kMaxQWanderOctaves);
            net.setGainWander(i, ResonanceDriftNetwork::kMaxGainWanderDb);
            net.setPeakPanWander(i, 1.0f);
        }
        net.setWanderRate(ResonanceDriftNetwork::kMaxWanderRateHz);
    }
    net.setMix(1.0f);
    net.setWetGain(kSoakWetGainDb);

    PinkDrive driveL(seed ^ 0x50A70001u, kCalFs, kDriveDbfs);
    PinkDrive driveR(seed ^ 0x50A70002u, kCalFs, kDriveDbfs);
    WakePattern pattern(net, seed ^ 0xA5A5A5A5u, kCalFs);

    const auto totalChunks = static_cast<std::size_t>(totalSeconds * kCalFs) / kCalChunk;
    const auto windowChunks = static_cast<std::size_t>(windowSeconds * kCalFs) / kCalChunk;

    std::array<float, kCalChunk> inL{};
    std::array<float, kCalChunk> inR{};
    std::array<float, kCalChunk> outL{};
    std::array<float, kCalChunk> outR{};

    std::vector<float> windowDb;
    windowDb.reserve(totalChunks / std::max<std::size_t>(windowChunks, 1u) + 1u);

    WindowRunStats s;
    double sumSq = 0.0;
    std::size_t counted = 0;

    for (std::size_t c = 0; c < totalChunks; ++c) {
        static_cast<void>(pattern.advance(net, c * kCalChunk));
        const std::size_t awake = pattern.getAwakeCount();
        s.minAwake = std::min(s.minAwake, awake);
        s.maxAwake = std::max(s.maxAwake, awake);
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            inL[i] = driveL.next();
            inR[i] = driveR.next();
        }
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kCalChunk);
        for (std::size_t i = 0; i < kCalChunk; ++i) {
            if (!detail::isFinite(outL[i]) || !detail::isFinite(outR[i])) {
                s.allFinite = false;
            }
            sumSq += static_cast<double>(outL[i]) * static_cast<double>(outL[i])
                     + static_cast<double>(outR[i]) * static_cast<double>(outR[i]);
            counted += 2u;
        }
        if (windowChunks > 0u && ((c + 1u) % windowChunks) == 0u) {
            windowDb.push_back(static_cast<float>(rmsDbfsOf(sumSq, counted)));
            sumSq = 0.0;
            counted = 0;
        }
    }

    s.windows = windowDb.size();
    s.events = pattern.getEventCount();
    s.toggledPeaks = pattern.getToggledPeakCount();
    s.clampEngagements = net.getClampEngagementCount();
    if (windowDb.empty()) {
        return s;
    }

    s.medianDb = static_cast<double>(percentileOfF(windowDb, 0.5));
    for (const float db : windowDb) {
        const double deviation = std::abs(static_cast<double>(db) - s.medianDb);
        s.worstDeviationDb = std::max(s.worstDeviationDb, deviation);
        s.lowestDb = std::min(s.lowestDb, static_cast<double>(db));
        s.highestDb = std::max(s.highestDb, static_cast<double>(db));
    }
    s.totalSlopeDb = totalLeastSquaresChange(windowDb);
    return s;
}

// =============================================================================
// SC-021 (c) - kPanNonVacuityDb
// =============================================================================

/// Isolate one peak: every OTHER peak dormant (the behavioural TU's
/// isolatePeak). SC-021's configuration base.
void isolatePeak(ResonanceDriftNetwork& net, std::size_t peak)
{
    for (std::size_t i = 0; i < kCalPeaks; ++i) {
        if (i != peak) {
            net.setPeakDormant(i, true);
        }
    }
}

/// SC-021 (c)'s statistic: the dB RANGE of the inter-channel level difference
/// 20*log10(rmsL / rmsR) across sixty consecutive 1 s windows of the isolated-
/// peak configuration at the FR-016 default pan wander and wander rate.
///
/// Peak 0 is used for the reason the consuming arm gives: its seeded position
/// sits nearest the centre, which keeps BOTH channels well away from zero
/// across the whole +/- 0.2 wander excursion - the ratio would diverge at a
/// hard-panned position.
[[nodiscard]] double measurePanRangeDb(std::uint32_t seed)
{
    constexpr std::size_t kPeak = 0;
    constexpr float kToneHz = 40.0f;  // peak 0's FR-016 anchor
    constexpr auto kWindow = static_cast<std::size_t>(kCalLaneFs);  // exactly 1 s
    constexpr std::size_t kWindows = 60;
    static_assert((kWindow % kCalChunk) == 0u,
                  "each 1 s window must be a whole number of control chunks");

    ResonanceDriftNetwork net;
    net.setSeed(seed);
    net.prepare(kCalLaneFs, ResonanceDriftNetwork::PrepareConfig{});
    isolatePeak(net, kPeak);
    net.setWetGain(0.0f);

    std::array<float, kCalChunk> in{};
    std::array<float, kCalChunk> oL{};
    std::array<float, kCalChunk> oR{};

    constexpr double kTwoPiD = 6.28318530717958647692;
    const double omega = kTwoPiD * static_cast<double>(kToneHz) / kCalLaneFs;
    std::size_t clock = 0;

    // The same tone on both channels: renderChunk folds the pair to
    // x = 0.5 * (dryL + dryR) before the engine (FR-044), so an identical drive
    // makes the OUTPUT stereo image purely the pan law's doing.
    const auto renderWindow = [&](double* sumSqL, double* sumSqR) {
        for (std::size_t done = 0; done < kWindow; done += kCalChunk) {
            for (std::size_t k = 0; k < kCalChunk; ++k) {
                const double t = static_cast<double>(clock + k);
                in[k] = 0.25f * static_cast<float>(std::sin(omega * t));
            }
            net.processBlock(in.data(), in.data(), oL.data(), oR.data(), kCalChunk);
            clock += kCalChunk;
            if (sumSqL == nullptr || sumSqR == nullptr) {
                continue;
            }
            for (std::size_t k = 0; k < kCalChunk; ++k) {
                *sumSqL += static_cast<double>(oL[k]) * static_cast<double>(oL[k]);
                *sumSqR += static_cast<double>(oR[k]) * static_cast<double>(oR[k]);
            }
        }
    };

    renderWindow(nullptr, nullptr);  // one settling window, discarded

    double lowestDb = 0.0;
    double highestDb = 0.0;
    for (std::size_t w = 0; w < kWindows; ++w) {
        double sumSqL = 0.0;
        double sumSqR = 0.0;
        renderWindow(&sumSqL, &sumSqR);
        if (!(sumSqL > 0.0) || !(sumSqR > 0.0)) {
            continue;
        }
        const double differenceDb = 10.0 * std::log10(sumSqL / sumSqR);
        if (w == 0) {
            lowestDb = differenceDb;
            highestDb = differenceDb;
        } else {
            lowestDb = std::min(lowestDb, differenceDb);
            highestDb = std::max(highestDb, differenceDb);
        }
    }
    return highestDb - lowestDb;
}

}  // namespace calib
}  // namespace

// =============================================================================
// T019 - the calibration case itself
// =============================================================================
//
// RUNTIME: minutes, not seconds. The dominant terms are SC-016's eight 30-minute
// soaks and SC-003 (f)'s sixteen 2 000-second lane records. It is hidden for
// that reason as much as for the "not a gate" one. Unlike the [.perf] probe
// above it does NOT have to run in isolation: every figure here is a property
// of a seeded walk or of a render, and none of them is a wall-clock timing.
//
// THE ONE THING THIS CASE ASSERTS, AND WHY. Every section REQUIREs that its
// distribution has the seed count the spec demands, that every entry is finite,
// and that the entries are not all identical. A distribution that came out
// constant across eight independent seeds would not be a null distribution - it
// would be a dead lane, a frozen fixture or a broken estimator, and it is the
// one failure mode that would make every table below lie while looking
// perfectly reasonable. Nothing else is asserted: the RESPONSE to a measured
// figure is a decision, and the bound provenance rule at the head of this
// block governs it.
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_MeasureThresholds", "[resonance_drift_network][.calibration]")
{
    using calib::BoundSide;
    using calib::Distribution;
    using calib::fixedStr;
    using calib::reportDistribution;

    // The figures currently checked into the consuming tests, quoted so each
    // report is a COMPARISON. Each comment names the file the constant lives in.
    constexpr double kProvBoundaryRatio = 1.5;       // spectral TU, SC-002 (a)
    constexpr double kProvLaneIndependenceR = 0.45;  // spectral TU, SC-003 (e)
    constexpr double kProvRateSeparation = 0.25;     // spectral TU, SC-003 (f)
    constexpr double kProvSeedDecorrelationR = 0.75; // behavioural TU, SC-006 (b)
    constexpr double kProvRateInvariance = 0.15;     // behavioural TU, SC-008 (c)
    constexpr double kProvWakeSleepWindowDb = 6.0;   // spectral TU, SC-015 (g)
    constexpr double kProvSoakWindowDb = 6.0;        // spectral TU, SC-016 (a)
    constexpr double kProvSoakFloorDbfs = -60.0;     // spectral TU, SC-016 (c)
    constexpr double kProvWetVsDriveWindowDb = 6.0;  // behavioural TU, SC-020 (a)
    // SC-020 (b)'s window. NOT provisional: 6 dB is the spec's own figure
    // (Clarifications Q2, "wet within roughly 6 dB of dry"), and since (b)
    // asserts the mix = 0 render IS the drive, it is the same statistic as the
    // line above. It is what puts a FLOOR under kDefaultWetGainDb.
    constexpr double kProvWetVsDryWindowDb = 6.0;    // behavioural TU, SC-020 (b)
    constexpr double kProvPanNonVacuityDb = 1.0;     // behavioural TU, SC-021 (c)

    SECTION("FR-045 kDefaultWetGainDb and SC-020 (a) the wet-vs-drive window")
    {
        // -------------------------------------------------------------------
        // Part 1: the PASSIVE attenuation of the FR-016 reference patch,
        // measured at a 0 dB trim so the figure is the network's own and not
        // the trim's. kDefaultWetGainDb is then its negation.
        //
        // The independent estimate to sanity-check the measurement against
        // (tasks.md T019): twelve constant-0 dB-peak bandpasses at Q = 12 on
        // the FR-016 anchors have a summed noise-equivalent bandwidth of
        // ~(pi/2) * sum(f_i / Q) ~ 605 Hz against a 24 kHz white-noise
        // bandwidth (~ -16 dB), plus FR-019's 1/sqrt(12) (-10.8 dB), plus the
        // -6 dB default peak level => ~ -33 dB. A measurement far from that is
        // a defect in the patch or in the render, not a surprising result.
        // -------------------------------------------------------------------
        Distribution passiveGapDb;
        std::uint32_t worstClampAtUnity = 0;
        bool allFiniteAtUnity = true;
        for (std::size_t s = 0; s < 8u; ++s) {
            const calib::WetGainMeasurement m = calib::measureWetVsDrive(calib::kCalSeeds[s], 0.0f);
            passiveGapDb.add(m.gapDb);
            worstClampAtUnity = std::max(worstClampAtUnity, m.clampEngagements);
            allFiniteAtUnity = allFiniteAtUnity && m.allFinite;
        }
        REQUIRE(passiveGapDb.size() == 8u);
        REQUIRE(passiveGapDb.allFinite());
        REQUIRE(passiveGapDb.max() > passiveGapDb.min());
        REQUIRE(allFiniteAtUnity);
        // Linearity precondition: at a 0 dB trim the reference patch is ~33 dB
        // BELOW its drive, so a clamp engagement here would mean the render is
        // not what it is assumed to be.
        CAPTURE(worstClampAtUnity);
        REQUIRE(worstClampAtUnity == 0u);

        // FLOOR. SC-020 (b)'s window is the spec's own 6 dB (Clarifications Q2),
        // so the trim has to lift the WORST seed's passive gap to within it:
        // floor = |min passive gap| - 6.
        const double floorDb = -passiveGapDb.min() - static_cast<double>(kProvWetVsDryWindowDb);

        // -------------------------------------------------------------------
        // Part 1b: the CEILING. FR-018 requires zero clamp engagements in every
        // in-spec configuration and SC-001 (b)/(e) enforces it, so the trim is
        // bounded ABOVE as well. Measuring only the passive gap and negating it
        // is how this constant was first set to +38.0 dB - a value that put 23
        // samples of SC-001 (e)'s render into the clamp and turned that arm
        // red. The suggestion below is therefore two-sided by construction.
        // -------------------------------------------------------------------
        Distribution clipCeilingDb;
        for (std::size_t s = 0; s < 8u; ++s) {
            clipCeilingDb.add(calib::measureClipCeilingDb(calib::kCalSeeds[s]));
        }
        REQUIRE(clipCeilingDb.size() == 8u);
        REQUIRE(clipCeilingDb.allFinite());
        REQUIRE(clipCeilingDb.max() > clipCeilingDb.min());
        // A silent worst-case render would report a ceiling of 0 and make the
        // interval below meaningless.
        REQUIRE(clipCeilingDb.min() > 0.0);

        const double ceilingDb = clipCeilingDb.min();

        // Rounded to the nearest 0.5 dB: the measurement's own seed-to-seed
        // spread is far larger than 0.1 dB, so more precision than that would
        // be false. The midpoint of the admissible interval, NOT the passive
        // mean negated.
        const double suggestedDefault =
            std::round(0.5 * (floorDb + ceilingDb) * 2.0) * 0.5;
        const bool intervalIsEmpty = ceilingDb <= floorDb;

        // -------------------------------------------------------------------
        // Part 2: SC-020 (a)'s window, at the trim currently in the header.
        // The statistic is (wet RMS - drive RMS) at kDefaultWetGainDb, i.e.
        // what SC-020 (a) will assert on; its spread across seeds is what the
        // window has to absorb.
        // -------------------------------------------------------------------
        Distribution wetVsDriveDb;
        std::uint32_t worstClampAtDefault = 0;
        for (std::size_t s = 0; s < 8u; ++s) {
            const calib::WetGainMeasurement m = calib::measureWetVsDrive(
                calib::kCalSeeds[s], ResonanceDriftNetwork::kDefaultWetGainDb);
            wetVsDriveDb.add(m.gapDb);
            worstClampAtDefault = std::max(worstClampAtDefault, m.clampEngagements);
        }
        REQUIRE(wetVsDriveDb.size() == 8u);
        REQUIRE(wetVsDriveDb.allFinite());
        REQUIRE(wetVsDriveDb.max() > wetVsDriveDb.min());

        // The window is symmetric about zero, so what it must cover is the
        // largest |wet - drive| across the seeds, not the spread about the
        // mean.
        Distribution absWetVsDriveDb;
        for (const double v : wetVsDriveDb.values) {
            absWetVsDriveDb.add(std::abs(v));
        }

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  FR-045 / SC-020 (a): kDefaultWetGainDb and the wet-vs-drive window\n"
           << "  FR-016 reference patch, numPeaks = 12, AnchorMode::Free, mix = 1,\n"
           << "  white noise at -12 dBFS on both channels, 1 s settle + 10 s measured,\n"
           << "  48 kHz, 8 seeds.\n"
           << "=================================================================================\n"
           << reportDistribution("passive gap at a 0 dB trim  (wet RMS - drive RMS, dB)",
                                 "FR-045's measurement; kDefaultWetGainDb is its negation",
                                 passiveGapDb, BoundSide::Lower, 0.0, 3)
           << "      analytic sanity check: ~ -33 dB (see the comment above this section)\n"
           << reportDistribution("SC-001 (e) clip ceiling  (max wet trim before FR-018 engages, dB)",
                                 "kDefaultWetGainDb's UPPER bound; SC-001 (b)/(e), spectral TU",
                                 clipCeilingDb, BoundSide::Lower, 0.0, 3)
           << "      admissible interval for kDefaultWetGainDb: ["
           << fixedStr(floorDb, 2) << ", " << fixedStr(ceilingDb, 2) << "] dB"
           << "   (width " << fixedStr(ceilingDb - floorDb, 2) << " dB)\n"
           << "      floor  = |worst passive gap| - SC-020 (b)'s "
           << fixedStr(static_cast<double>(kProvWetVsDryWindowDb), 1)
           << " dB window; ceiling = worst of the 8 clip ceilings\n"
           << (intervalIsEmpty
                   ? "      *** THE INTERVAL IS EMPTY: no default trim satisfies both SC-020 (b)\n"
                     "      *** and SC-001 (b)/(e). STOP AND SURFACE - this is a design finding\n"
                     "      *** about FR-018's headroom, not a constant to nudge.\n"
                   : "")
           << "      *** SUGGESTED ResonanceDriftNetwork::kDefaultWetGainDb = "
           << fixedStr(suggestedDefault, 1) << " dB  (interval MIDPOINT, not the\n"
           << "      *** passive mean negated - that one-sided reading gives "
           << fixedStr(std::round(-passiveGapDb.mean() * 2.0) * 0.5, 1)
           << " dB and clips)\n"
           << "      *** currently in the header: "
           << fixedStr(static_cast<double>(ResonanceDriftNetwork::kDefaultWetGainDb), 1)
           << " dB  (range ["
           << fixedStr(static_cast<double>(ResonanceDriftNetwork::kMinWetGainDb), 1) << ", "
           << fixedStr(static_cast<double>(ResonanceDriftNetwork::kMaxWetGainDb), 1) << "])\n"
           << reportDistribution("|wet RMS - drive RMS| at kDefaultWetGainDb (dB)",
                                 "SC-020 (a)'s kWetVsDriveWindowDb, behavioural TU",
                                 absWetVsDriveDb, BoundSide::Upper, kProvWetVsDriveWindowDb, 3)
           << "      clamp engagements at kDefaultWetGainDb, worst of the 8 seeds: "
           << worstClampAtDefault << "\n"
           << "      (non-zero means the reference render is CLIPPING at the shipped trim -\n"
           << "       SC-020's own arms REQUIRE this to be zero, so the trim is too high.)\n"
           << "=================================================================================\n"
           << "  RECORD in compliance.md: the measured gap, the chosen constant, the method\n"
           << "  and the date - and write the same three into the header comment beside\n"
           << "  kDefaultWetGainDb (FR-045, tasks.md T019).\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-002 (a) kBoundaryRatio")
    {
        Distribution ratio;
        std::uint32_t worstClamp = 0;
        std::size_t boundaryCount = 0;
        std::size_t interiorCount = 0;
        for (std::size_t s = 0; s < 8u; ++s) {
            const calib::ZipperStats z = calib::measureBoundaryRatio(calib::kCalSeeds[s]);
            ratio.add(z.ratio);
            worstClamp = std::max(worstClamp, z.clampEngagements);
            boundaryCount = z.boundaryCount;
            interiorCount = z.interiorCount;
        }
        REQUIRE(ratio.size() == 8u);
        REQUIRE(ratio.allFinite());
        REQUIRE(ratio.max() > ratio.min());
        // The population sizes are asserted, not merely printed: a partition
        // that silently drifted off the control grid would still produce two
        // plausible-looking percentiles.
        CAPTURE(boundaryCount, interiorCount);
        REQUIRE(boundaryCount == (calib::kZipperSamples * 3u) / calib::kCalChunk);
        REQUIRE(interiorCount == (calib::kZipperSamples * 61u) / calib::kCalChunk);
        // A clipped render's second difference is the CLAMP's, in both
        // populations at once, and its B/P is the clipper's (spec correction
        // C-17). The figure below is only a measurement of the network if this
        // is zero.
        CAPTURE(worstClamp);
        REQUIRE(worstClamp == 0u);

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-002 (a): kBoundaryRatio - the NULL distribution of B / P\n"
           << "  220 Hz sine at -12 dBFS on both channels, zipper patch (all four lanes at\n"
           << "  maximum depth, wanderRate 1.0 Hz), wet trim "
           << fixedStr(static_cast<double>(calib::kZipperWetGainDb), 1)
           << " dB, 1 s settle + a PINNED 60 s,\n"
           << "  NO injection. Partition n mod 64 in {0, 1, 2} ONLY (spec correction C-11) -\n"
           << "  a figure measured under a {0, 1} partition may NOT be carried over.\n"
           << "  Populations: " << boundaryCount << " boundary / " << interiorCount
           << " interior.\n"
           << "=================================================================================\n"
           << reportDistribution("kBoundaryRatio  (B / P, wander on, no injection)",
                                 "SC-002 (a), resonance_drift_network_spectral_test.cpp",
                                 ratio, BoundSide::Upper, kProvBoundaryRatio, 4)
           << "      DISCRIMINATION CHECK, mandatory before transcribing: SC-002 (c)'s\n"
           << "      injection arm measured 4.01 / 4.00 / 4.66 against this null. A candidate\n"
           << "      bound must sit BELOW those and ABOVE this null, or the (a) arm cannot\n"
           << "      fail and the (c) arm cannot discriminate.\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-003 (e) kLaneIndependenceR")
    {
        Distribution meanAbsR;
        for (std::size_t s = 0; s < 12u; ++s) {
            meanAbsR.add(calib::measureLaneIndependence(calib::kCalSeeds[s]));
        }
        REQUIRE(meanAbsR.size() == 12u);
        REQUIRE(meanAbsR.allFinite());
        REQUIRE(meanAbsR.max() > meanAbsR.min());

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-003 (e): kLaneIndependenceR - mean |r| over the C(12,2) = 66 pairs of\n"
           << "  mean-removed log2-frequency trajectories, FR-016 defaults, 350 s (10*T at\n"
           << "  the 0.03 Hz default rate), 262 500 control steps, 48 kHz, 12 seeds.\n"
           << "  PHASE 2's 0.05 MAY NOT BE TRANSCRIBED: Bartlett's formula puts E|r| near\n"
           << "  0.23 here from estimator noise alone, so any bound at or below that is\n"
           << "  unsatisfiable by a correct build.\n"
           << "=================================================================================\n"
           << reportDistribution("kLaneIndependenceR  (mean |r| over 66 pairs)",
                                 "SC-003 (e), resonance_drift_network_spectral_test.cpp",
                                 meanAbsR, BoundSide::Upper, kProvLaneIndependenceR, 4)
           << "      ANTI-VACUITY, already in the consuming arm and unchanged by this\n"
           << "      measurement: twelve lanes built on the SAME seed AND salt must read\n"
           << "      >= kLaneControlR = 0.95 through the identical estimator.\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-003 (f) kRateSeparation")
    {
        Distribution separation;
        Distribution rhoSlow;
        Distribution rhoFast;
        bool decimationsCorrect = true;
        for (std::size_t s = 0; s < 8u; ++s) {
            const calib::RateSeparation r = calib::measureRateSeparation(calib::kCalSeeds[s]);
            separation.add(r.separation);
            rhoSlow.add(r.rhoSlow);
            rhoFast.add(r.rhoFast);
            decimationsCorrect =
                decimationsCorrect && (r.slowDecimation == 7u) && (r.fastDecimation == 1u);
        }
        REQUIRE(separation.size() == 8u);
        REQUIRE(separation.allFinite());
        REQUIRE(separation.max() > separation.min());
        // FR-037's whole point: without the decimation, 0.005 Hz is inside
        // BrownianDrift's dead zone and the slow arm would be
        // indistinguishable from the fast one - which would make the separation
        // below a measurement of nothing.
        REQUIRE(decimationsCorrect);

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-003 (f): kRateSeparation - mean lag-1.67 s ACF at 0.005 Hz (decimation 7)\n"
           << "  minus the same at 0.3 Hz (decimation 1). Lane fixture at 8 kHz, 2 000 s each\n"
           << "  (10*T of the slow arm), same seed and same absolute lag in both arms, 8 seeds.\n"
           << "  Closed form for orientation: exp(-1.67/200) = 0.992 against\n"
           << "  exp(-1.67/3.33) = 0.607, i.e. a separation of ~0.385.\n"
           << "=================================================================================\n"
           << reportDistribution("rho_slow  (0.005 Hz, decimation 7)", "SC-003 (f), reported only",
                                 rhoSlow, BoundSide::Lower, 0.0, 4)
           << reportDistribution("rho_fast  (0.300 Hz, decimation 1)", "SC-003 (f), reported only",
                                 rhoFast, BoundSide::Upper, 0.0, 4)
           << reportDistribution("kRateSeparation  (rho_slow - rho_fast)",
                                 "SC-003 (f), resonance_drift_network_spectral_test.cpp",
                                 separation, BoundSide::Lower, kProvRateSeparation, 4)
           << "      This is a LOWER bound: the arm asserts separation >= kRateSeparation, so\n"
           << "      the suggestion sits BELOW the observed minimum. It must also stay far\n"
           << "      above the ~0 a build with FR-037's dead zone still in place produces.\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-006 (b) kSeedDecorrelationR")
    {
        Distribution worstAbsR;
        Distribution meanAbsR;
        double worstSameSeedR = 1.0;
        for (std::size_t s = 0; s < 12u; ++s) {
            const calib::SeedDecorrelation d =
                calib::measureSeedDecorrelation(calib::kCalSeeds[s], calib::kCalPairSeeds[s]);
            worstAbsR.add(d.worstAbsR);
            meanAbsR.add(d.meanAbsR);
            worstSameSeedR = std::min(worstSameSeedR, d.worstSameSeedR);
        }
        REQUIRE(worstAbsR.size() == 12u);
        REQUIRE(worstAbsR.allFinite());
        REQUIRE(worstAbsR.max() > worstAbsR.min());
        // The estimator's own anti-vacuity control, in the same process: a
        // same-seed pair must read ~1, or every cross-seed figure above is
        // "the estimator returns small numbers" rather than "the seeds
        // decorrelate".
        CAPTURE(worstSameSeedR);
        REQUIRE(worstSameSeedR >= 0.95);

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-006 (b): kSeedDecorrelationR - |r| between peak i of instance A and peak i\n"
           << "  of instance B, two instances differing ONLY in seed. Twelve pairs per\n"
           << "  measurement (never 24 cross-paired), wanderRate 1.0 Hz => tau = 1 s, record\n"
           << "  20 s = 20*tau at 8 kHz, 12 SEED PAIRS.\n"
           << "  The consuming arm bounds the WORST of the twelve, so that is the row the\n"
           << "  constant is set from; the mean is reported beside it for context.\n"
           << "=================================================================================\n"
           << reportDistribution("kSeedDecorrelationR  (worst |r| of the 12 peak pairs)",
                                 "SC-006 (b), resonance_drift_network_test.cpp",
                                 worstAbsR, BoundSide::Upper, kProvSeedDecorrelationR, 4)
           << reportDistribution("mean |r| of the 12 peak pairs", "SC-006 (b), reported only",
                                 meanAbsR, BoundSide::Upper, 0.0, 4)
           << "      same-seed control, worst across all 12 pairs: " << fixedStr(worstSameSeedR, 4)
           << "  (must stay >= kSameSeedFloorR = 0.95)\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-008 (c) the rate-invariance tolerance")
    {
        // Two distributions, because the criterion needs both: the ESTIMATOR's
        // own spread at one fixed rate (the noise floor no tolerance can sit
        // below), and the REALISED cross-rate deviation the arm actually
        // asserts on. The constant is set from the second; the first says
        // whether the second is dominated by realisation noise.
        Distribution lagAt48k;
        Distribution worstCrossRateDeviation;
        for (std::size_t s = 0; s < 8u; ++s) {
            const std::uint32_t seed = calib::kCalSeeds[s];
            const double l44 = calib::measureLagSeconds(seed, 44100.0, 1.0f);
            const double l48 = calib::measureLagSeconds(seed, 48000.0, 1.0f);
            const double l96 = calib::measureLagSeconds(seed, 96000.0, 1.0f);
            REQUIRE(l44 > 0.0);
            REQUIRE(l48 > 0.0);
            REQUIRE(l96 > 0.0);
            lagAt48k.add(l48);
            worstCrossRateDeviation.add(
                std::max(std::abs(l44 / l48 - 1.0), std::abs(l96 / l48 - 1.0)));
        }
        REQUIRE(lagAt48k.size() == 8u);
        REQUIRE(lagAt48k.allFinite());
        REQUIRE(lagAt48k.max() > lagAt48k.min());
        REQUIRE(worstCrossRateDeviation.allFinite());
        REQUIRE(worstCrossRateDeviation.max() > worstCrossRateDeviation.min());

        // The estimator's own relative spread at ONE rate, which is the floor
        // any tolerance has to clear.
        const double relativeSpreadAt48k =
            (lagAt48k.mean() > 0.0) ? (lagAt48k.sd() / lagAt48k.mean()) : 0.0;

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-008 (c): the rate-invariance tolerance on the 1/e decorrelation lag.\n"
           << "  numPeaks = 1, wanderRate 1.0 Hz (tau = 1 s), slew ceilings lifted, 50 s of\n"
           << "  record (50*tau) at 44 100 / 48 000 / 96 000 Hz, 8 seeds.\n"
           << "=================================================================================\n"
           << reportDistribution("1/e lag at 48 kHz (seconds)",
                                 "SC-008 (c), the estimator's own spread at ONE rate", lagAt48k,
                                 BoundSide::Upper, 1.0, 4)
           << "      relative spread of that distribution (sd / mean): "
           << fixedStr(relativeSpreadAt48k, 4) << "\n"
           << reportDistribution("kRateInvarianceTolerance  (worst |lag_fs/lag_48k - 1|)",
                                 "SC-008 (c), resonance_drift_network_test.cpp",
                                 worstCrossRateDeviation, BoundSide::Upper, kProvRateInvariance, 4)
           << "      DISCRIMINATION CHECK, mandatory: the consuming arm's injection reads a\n"
           << "      lag ~2x the reference (a 96 kHz render at wanderRate 0.5 Hz), so any\n"
           << "      candidate tolerance must stay far below 1.0 or the injection arm cannot\n"
           << "      discriminate.\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-015 (g) the wake/sleep RMS bound")
    {
        Distribution worstDeviationDb;
        Distribution medianDb;
        std::uint32_t worstClamp = 0;
        std::size_t fewestEvents = 1000000u;
        std::size_t narrowestAwake = 1000000u;
        std::size_t widestAwake = 0;
        bool allFinite = true;
        for (std::size_t s = 0; s < 8u; ++s) {
            const calib::WindowRunStats w =
                calib::measureWindowRun(calib::kCalSeeds[s], /*maxDepths=*/false,
                                        /*totalSeconds=*/300.0, /*windowSeconds=*/0.2);
            worstDeviationDb.add(w.worstDeviationDb);
            medianDb.add(w.medianDb);
            worstClamp = std::max(worstClamp, w.clampEngagements);
            fewestEvents = std::min(fewestEvents, w.events);
            narrowestAwake = std::min(narrowestAwake, w.minAwake);
            widestAwake = std::max(widestAwake, w.maxAwake);
            allFinite = allFinite && w.allFinite;
        }
        REQUIRE(worstDeviationDb.size() == 8u);
        REQUIRE(worstDeviationDb.allFinite());
        REQUIRE(worstDeviationDb.max() > worstDeviationDb.min());
        REQUIRE(allFinite);
        // Non-vacuity: the script has to have woken and slept peaks, or the
        // "bed holds across a wake/sleep script" statistic is a statistic about
        // a script that never fired.
        CAPTURE(fewestEvents, worstClamp, narrowestAwake, widestAwake);
        REQUIRE(fewestEvents >= 3u);
        // The stationarity precondition. If this ever reads 12, the fixture is
        // back on the all-awake flip walk and the bound below is a measurement
        // of the script's decay, not of the network.
        REQUIRE(narrowestAwake >= 5u);
        REQUIRE(widestAwake <= 6u);

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-015 (g): the wake/sleep RMS bound - the worst deviation of a 200 ms window\n"
           << "  RMS from the render's median, across a scripted 20-90 s wake/sleep sequence.\n"
           << "  FR-016 defaults, numPeaks = 12, mix = 1, wet trim "
           << fixedStr(static_cast<double>(calib::kSoakWetGainDb), 1)
           << " dB, pink noise at -12 dBFS,\n"
           << "  300 s at 48 kHz, 8 seeds.\n"
           << "  A 200 ms window is SHORT against the noise bandwidth of a Q = 12 peak\n"
           << "  (f0/Q = 3.3 Hz at the 40 Hz anchor, i.e. under one independent sample per\n"
           << "  window), so this spread is dominated by the DRIVE and not by the wake events.\n"
           << "=================================================================================\n"
           << reportDistribution("kWakeSleepWindowDb  (worst |window dB - median dB|)",
                                 "SC-015 (g), resonance_drift_network_spectral_test.cpp",
                                 worstDeviationDb, BoundSide::Upper, kProvWakeSleepWindowDb, 3)
           << reportDistribution("median window level (dBFS)", "SC-015 (g), reported only",
                                 medianDb, BoundSide::Lower, kProvSoakFloorDbfs, 3)
           << "      clamp engagements, worst of the 8 seeds: " << worstClamp << "\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-016 (a) kSoakWindowDb and (c) kSoakFloorDbfs")
    {
        Distribution worstDeviationDb;
        Distribution lowestDb;
        Distribution highestDb;
        Distribution slopeDb;
        std::uint32_t worstClamp = 0;
        std::size_t fewestEvents = 1000000u;
        std::size_t fewestToggled = 1000000u;
        std::size_t narrowestAwake = 1000000u;
        std::size_t widestAwake = 0;
        bool allFinite = true;
        for (std::size_t s = 0; s < 8u; ++s) {
            const calib::WindowRunStats w =
                calib::measureWindowRun(calib::kCalSeeds[s], /*maxDepths=*/true,
                                        /*totalSeconds=*/1800.0, /*windowSeconds=*/10.0);
            REQUIRE(w.windows == 180u);  // 30 minutes of whole 10 s windows
            worstDeviationDb.add(w.worstDeviationDb);
            lowestDb.add(w.lowestDb);
            highestDb.add(w.highestDb);
            slopeDb.add(w.totalSlopeDb);
            worstClamp = std::max(worstClamp, w.clampEngagements);
            fewestEvents = std::min(fewestEvents, w.events);
            fewestToggled = std::min(fewestToggled, w.toggledPeaks);
            narrowestAwake = std::min(narrowestAwake, w.minAwake);
            widestAwake = std::max(widestAwake, w.maxAwake);
            allFinite = allFinite && w.allFinite;
        }
        REQUIRE(worstDeviationDb.size() == 8u);
        REQUIRE(worstDeviationDb.allFinite());
        REQUIRE(lowestDb.allFinite());
        REQUIRE(worstDeviationDb.max() > worstDeviationDb.min());
        REQUIRE(allFinite);
        CAPTURE(fewestEvents, fewestToggled, worstClamp, narrowestAwake, widestAwake);
        REQUIRE(fewestEvents >= 15u);
        REQUIRE(fewestToggled >= 6u);
        // SC-016 (b) is only a statement about the NETWORK while the excitation
        // is stationary (see WakePattern's derivation).
        REQUIRE(narrowestAwake >= 5u);
        REQUIRE(widestAwake <= 6u);

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-016 (a)/(c): kSoakWindowDb and kSoakFloorDbfs - 10 s window RMS over a\n"
           << "  30-MINUTE soak of the most extreme patch the spec admits (every depth at its\n"
           << "  maximum, wanderRate at its 1.0 Hz ceiling, Q = kMaxResonatorQ), an external\n"
           << "  wake pattern on the 20-90 s SlowEventScheduler default range, pink noise at\n"
           << "  -12 dBFS, wet trim " << fixedStr(static_cast<double>(calib::kSoakWetGainDb), 1)
           << " dB, 48 kHz, 8 seeds. 180 windows per seed.\n"
           << "  The spec says in as many words that +/- 6 dB is 'explicitly not trusted':\n"
           << "  Phase 2 had to widen its analogous bound from +/- 3.0 to +/- 4.5 dB for a\n"
           << "  LESS extreme configuration after measuring.\n"
           << "=================================================================================\n"
           << reportDistribution("kSoakWindowDb  (worst |window dB - median dB|)",
                                 "SC-016 (a), resonance_drift_network_spectral_test.cpp",
                                 worstDeviationDb, BoundSide::Upper, kProvSoakWindowDb, 3)
           << reportDistribution("kSoakFloorDbfs  (lowest 10 s window, dBFS)",
                                 "SC-016 (c), resonance_drift_network_spectral_test.cpp", lowestDb,
                                 BoundSide::Lower, kProvSoakFloorDbfs, 3)
           << reportDistribution("highest 10 s window (dBFS)",
                                 "SC-016 (c)'s -3 dBFS ceiling, reported only", highestDb,
                                 BoundSide::Upper, -3.0, 3)
           << reportDistribution("least-squares change across 30 min (dB)",
                                 "SC-016 (b)'s +/- 0.5 dB, reported only", slopeDb,
                                 BoundSide::Upper, 0.5, 4)
           << "      clamp engagements, worst of the 8 seeds: " << worstClamp << "\n"
           << "      kSoakWindowDb and kSoakFloorDbfs are BRACKETED FROM BOTH SIDES by the\n"
           << "      wet trim: (d) requires zero clamp engagements, which pushes the trim\n"
           << "      DOWN; (c) requires no window below the floor, which pushes it UP. If the\n"
           << "      measurement moves either, BOTH are re-derived together and recorded -\n"
           << "      never one of them widened after a red run.\n"
           << "=================================================================================\n";
        WARN(os.str());
    }

    SECTION("SC-021 (c) kPanNonVacuityDb")
    {
        Distribution rangeDb;
        for (std::size_t s = 0; s < 8u; ++s) {
            rangeDb.add(calib::measurePanRangeDb(calib::kCalSeeds[s]));
        }
        REQUIRE(rangeDb.size() == 8u);
        REQUIRE(rangeDb.allFinite());
        REQUIRE(rangeDb.max() > rangeDb.min());

        std::ostringstream os;
        os << "\n"
           << "=================================================================================\n"
           << "  SC-021 (c): kPanNonVacuityDb - the dB RANGE of 20*log10(rmsL / rmsR) across\n"
           << "  sixty consecutive 1 s windows, isolated peak 0 at its 40 Hz FR-016 anchor,\n"
           << "  0 dB wet trim, FR-016 default pan wander (0.2) and wander rate (0.03 Hz),\n"
           << "  8 kHz, 8 seeds.\n"
           << "  This is the arm WITHOUT WHICH SC-021 (a) and (b) both pass vacuously on a\n"
           << "  build whose pan lane is dead: a static split satisfies constant power and is\n"
           << "  trivially deterministic. Only a measured RANGE proves the lane is live -\n"
           << "  which is why the bound is a LOWER one and sits BELOW the observed minimum.\n"
           << "=================================================================================\n"
           << reportDistribution("kPanNonVacuityDb  (range of the inter-channel level difference)",
                                 "SC-021 (c), resonance_drift_network_test.cpp", rangeDb,
                                 BoundSide::Lower, kProvPanNonVacuityDb, 3)
           << "=================================================================================\n";
        WARN(os.str());
    }
}

// =============================================================================
// T020 - SC-004: THE CPU BUDGET  (spec.md:1180-1203, plan S10.6, tasks.md T020)
// =============================================================================
// Three arms of one criterion, all measured on the SAME instance shape and the
// SAME drive, so the two relative arms are differences and not comparisons
// between unrelated fixtures:
//
//   (a) reference   12 peaks, AnchorMode::Hybrid at gravity = 0.5, all four
//                   lanes at their FR-016 defaults, wander ENABLED, mix = 1,
//                   every peak awake, drive identical on both channels.
//                   <= 80 000 ns per 512-sample block at 48 kHz.
//   (b) wander off  the same fixture with setWanderEnabled(false). At least
//                   10 % BELOW (a). This is the arm that proves FR-015's
//                   change detection saves real work: with the depths scaled to
//                   zero the targets stop moving, appliedHz/appliedQ settle
//                   EXACTLY on lastWrittenHz/lastWrittenQ, and the indivisible
//                   setFrequency -> setQ pair (each a sin + cos + divide plus a
//                   coefficient store, resonator_bank.h:572-591) stops firing
//                   altogether. A change-detection path that saves nothing is
//                   not implemented, and only a measured saving can tell the
//                   two apart - the audio is identical either way.
//                   NOTE the lanes still ADVANCE here: setWanderEnabled scales
//                   the DEPTHS via wanderScale() and never freezes the motion
//                   (resonance_drift_network.h's updateControl, "UNCONDITIONAL
//                   BY CONTRACT"). So this arm's saving is the control WRITES
//                   and nothing else, which is exactly what SC-004 (b) names.
//   (c) all dormant setPeakDormant(i, true) on all twelve, then >= 50 ms of
//                   rendering so every gate ramp has LANDED on exactly 0 and
//                   every FR-042 sleep edge has fired. At least 40 % BELOW (a).
//                   Two preconditions are ASSERTED BEFORE TIMING STARTS, not
//                   assumed: isPeakEngineActive(i) == false for all twelve, and
//                   the output EXACTLY 0.0f on both channels - so a "cheap"
//                   figure cannot come from an accidental early return
//                   somewhere else in the render path.
//
// WHY THE EXACT-ZERO CHECK IS A REAL CHECK AND NOT A FORMALITY. At mix = 1 the
// dry path is fully crossfaded out (OnePoleSmoother::process snaps to target
// inside kCompletionThreshold, smoother.h:199-202, so m reaches EXACTLY 1.0f),
// every peak's gate is a LITERAL 0.0f (gateSteady returns the literal, and
// LinearRamp::process lands ON its target rather than approaching it,
// smoother.h:379-383), and the wet sum is therefore exactly zero BY
// CONSTRUCTION. A build that reached the same CPU figure by returning early -
// or by leaving the dry path connected - fails the check rather than the clock.
//
// THE TWO COMPILE-TIME CLAUSES, PER GATED ARM (plan S10.6, tasks.md T020 (d),
// the noise_organism_perf_test.cpp:1560-1575 idiom). Each baseline carries BOTH
//
//     static_assert(kBaseline * kRegressionFactor <= kBudgetNs);   // ceiling
//     static_assert(kBaseline >= kBudgetNs / 50.0);                // anti-no-op
//
// and they are two DIFFERENT clauses, not one restated. The ceiling binds the
// measurement to SC-004's 80 000 ns transitively on every machine (measured <=
// baseline x 1.5 <= budget). The floor - 1 600 ns - catches a baseline recorded
// from a run that did nothing: an un-prepared network fills silence and
// advances no state (processBlock's guard ladder), and a baseline taken from
// one would satisfy the ceiling forever while measuring nothing at all. Both
// are evaluated on EVERY CI leg even though these cases are [.perf]-hidden,
// which is the whole reason the absolute gate lives at compile time.
//
// *** IF A MEASUREMENT IS OVER BUDGET: REDUCE COST, NEVER RAISE THE BASELINE.
// *** (FR-060, inherited verbatim from noise_organism_perf_test.cpp:44-58.)
// The escalation ladder is fixed and has a terminus that is not an agent's to
// take: FR-013 Tier 2 (the processSympatheticBankSIMD kernel) when the ENGINE
// term dominates; FR-038's static-pan fallback (drop the twelve pan lanes'
// advance; setPeakPanWander becomes an accepting no-op) when the LANE term
// dominates; otherwise STOP AND SURFACE TO THE USER with the measured table
// from ResonanceDriftNetwork_StageCostProbe beside this one. No agent may lower
// kMaxPeaks, raise kBudgetNs, relax one of the 10 % / 40 % savings, or shrink a
// workload to make a figure fit.
//
// RUN IT ALONE, AND NOT BACK-TO-BACK (CLAUDE.md,
// feedback_cpu_tests_isolation_only.md). Isolation has two clauses: nothing
// else executing - no other suite, no build, no clang-tidy run, no parallel
// agent - AND a settle between runs, because sustained benchmarking heats the
// CPU and boost clocks drop (the same code drifted +14 % across one session in
// this repo's history).
//
//   node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee cpu.log | tail -20
//
// A verdict that flips between runs is measuring the machine, not the code.
// =============================================================================

namespace {
namespace cpu {

/// The three SC-004 arms.
enum class Arm : std::uint8_t { Reference, WanderOff, AllDormant };

[[nodiscard]] const char* armLabel(Arm arm)
{
    switch (arm) {
        case Arm::Reference: return "(a) reference: 12 peaks, Hybrid g=0.5, wander on";
        case Arm::WanderOff: return "(b) wander off: (a) + setWanderEnabled(false)";
        case Arm::AllDormant: return "(c) all dormant: (a) + every peak dormant";
    }
    return "(unknown arm)";
}

/// The precedent's regression bound (atmosphere_engine_perf_test.cpp:38,
/// noise_organism_perf_test.cpp's kCpuRegressionFactor).
constexpr double kRegressionFactor = 1.5;

/// The largest baseline the ceiling clause admits: 80 000 / 1.5 = 53 333.33,
/// rounded DOWN so the clause holds with a margin rather than on a tie.
constexpr double kCeilingAdmittedNs = 53333.0;
static_assert(kCeilingAdmittedNs * kRegressionFactor <= kBudgetNs,
              "the admitted maximum must itself satisfy the ceiling clause");

/// The anti-no-op floor, spelled once so the clauses and the report agree.
constexpr double kNoOpFloorNs = kBudgetNs / 50.0;
static_assert(kNoOpFloorNs > 0.0 && kNoOpFloorNs < kBudgetNs,
              "the floor is a floor, not a second ceiling");

// -----------------------------------------------------------------------------
// The three baselines.
//
// *** PROVISIONAL: PROJECTIONS, NOT MEASUREMENTS. ***
//
// They are plan S10.4's Tier-1 projection column, which is the only figure that
// exists before this case has been run on a machine. FR-013's gate 1 was
// tripped by T002's probe and Tier 1 is in force
// (resonance_drift_network.h:72-82), so the Tier-1 column is the right one to
// project from:
//
//     engine (a), one bank x 12 enabled slots               ~ 19 600 ns
//   + lanes (b) at decimation 2 (the 0.03 Hz default rate)  ~ 12 000 ns
//   + control writes (c), the FR-015 freq/Q pair            ~  7 700 ns
//   + per-sample tail (d), gates/levels/pan/mix/clamp       ~  5 000 ns
//   + sleep edge (e), amortised against a 20-90 s schedule  ~      0 ns
//   = ~ 44 300 ns/block, 55 % of the SC-004 ceiling
//
// (b) is that total less the control-write term (44 300 - 7 700 = 36 600), and
// (c) is that total less the control-write term AND the twelve enabled slots'
// biquad math - 512 x 12 x ~2.5 ns ~ 15 400 ns - plus the gated part of the
// per-sample tail; call it 20 000. The bank's fixed per-call overhead SURVIVES
// in (c), because Tier 1 calls bank_.processIndividual once per sample
// unconditionally and the saving is the twelve DISABLED slots being skipped
// inside it (resonator_bank.h:487-488), not the call disappearing.
//
// TRANSCRIBING A MEASURED FIGURE OVER A PROJECTION IS ALWAYS CORRECT, and this
// case prints copy-pasteable lines for exactly that. RAISING one so a REQUIRE
// passes is forbidden. A measured figure that cannot satisfy BOTH clauses below
// is the stop-and-surface case, never a licence to weaken a clause.
//
// Take the transcription from a DISTRIBUTION, not one sample: this repo has
// measured a 6.4 % run-to-run spread and ~14 % session drift on unchanged code
// (noise_organism_perf_test.cpp's kBudgetNs provenance note). Use the LOWEST of
// several isolated runs, so the clause stays as tight as the data allows.
// -----------------------------------------------------------------------------

// TRANSCRIBED 2026-09-11 from the isolated compliance run (nothing else
// executing, i9-13900HX, Release, 48 kHz): (a) 45 771.0, (b) 43 562.4,
// (c) 20 501.2 ns/block. The plan S10.4 projections they replace were
// 44 300 / 36 600 / 20 000 - (a) and (c) within 3 %, (b) off because the
// projection assumed the whole ~7 700 ns control-write term disappears with
// wander off; measured, FR-015's change detection removes 2 208.6 ns (4.83 %).
// PROVENANCE CAVEAT: later the same night the whole dsp_systems_tests perf set
// - every shipped baseline, not just this one - measured 1.5-1.8x slower on
// the same machine (this case's (a) at 79 081 / 81 767 ns) with the CPU
// boosting normally; consistent with the test thread landing on an E-core.
// Those runs are NOT the transcription source ("lowest of several isolated
// runs"). Re-validate with `node tools/run-cpu-tests.js dsp_systems_tests`
// on a quiet machine; if (a) reproduces near 45 800 the figures stand.

/// (a) reference: 12 peaks, Hybrid g = 0.5, four lanes at defaults, mix 1.
constexpr double kBaselineReferenceNs = 45771.0;

/// (b) wander off: (a) less FR-015's control-write term only - the lanes
/// still advance (FR-036), so this is a 4-6 % saving, not the projected 17 %.
constexpr double kBaselineWanderOffNs = 43562.4;

/// (c) all dormant, settled: control writes and the twelve disabled slots'
/// biquad math gone, the once-per-sample processIndividual call kept.
constexpr double kBaselineAllDormantNs = 20501.2;

// -----------------------------------------------------------------------------
// The two compile-time clauses, per arm. ALL THREE arms carry BOTH: unlike the
// Phase 2 precedent's out-of-region (d)/(e) configurations, every arm here is a
// strictly CHEAPER rendering of the same reference configuration, so the SC-004
// ceiling is meaningful for each of them and SC-004 (d) says "each arm".
// -----------------------------------------------------------------------------
static_assert(kBaselineReferenceNs * kRegressionFactor <= kBudgetNs,
              "SC-004 (a) reference baseline exceeds the 0.75 % budget");
static_assert(kBaselineReferenceNs >= kNoOpFloorNs,
              "SC-004 (a) reference baseline looks like a no-op run");

static_assert(kBaselineWanderOffNs * kRegressionFactor <= kBudgetNs,
              "SC-004 (b) wander-off baseline exceeds the 0.75 % budget");
static_assert(kBaselineWanderOffNs >= kNoOpFloorNs,
              "SC-004 (b) wander-off baseline looks like a no-op run");

static_assert(kBaselineAllDormantNs * kRegressionFactor <= kBudgetNs,
              "SC-004 (c) all-dormant baseline exceeds the 0.75 % budget");
static_assert(kBaselineAllDormantNs >= kNoOpFloorNs,
              "SC-004 (c) all-dormant baseline looks like a no-op run");

// -----------------------------------------------------------------------------
// Fixture
// -----------------------------------------------------------------------------

/// Pinned so the wander lanes sit on the same trajectories run to run (FR-005)
/// and the figures are reproducible.
constexpr std::uint32_t kCpuSeed = 0x5EEDBEEFu;

/// SC-004's reference gravity, in AnchorMode::Hybrid.
constexpr float kCpuGravity = 0.5f;

/// This phase's drive level everywhere: -12 dBFS RMS (SC-001), identical on
/// both channels (the Success Criteria stereo convention SC-004 names).
constexpr float kCpuDriveDbfs = -12.0f;

/// Xorshift32::nextFloat() is uniform on [-1, +1] (random.h:59-63), RMS
/// 1/sqrt(3), so a target RMS is reached by scaling with sqrt(3).
constexpr float kCpuWhiteRmsScale = 1.7320508f;

/// Blocks rendered OUTSIDE the timed region before the fixture is inspected.
///
/// Three things must have LANDED before either a figure or a precondition means
/// anything: the FR-041 gate ramps (kGainRampMs = 50 ms), the FR-035 slew
/// limiter walking each Hybrid anchor from its Free position to its
/// gravity-0.5 position (a ceiling of kDefaultFreqStepOctaves = 0.02 octaves
/// per 64-sample control step, i.e. 50 steps = 3 200 samples for a full
/// octave), and - for arm (c) - every FR-042 sleep edge. bestTrialNs runs its
/// own 400 warm-up blocks on top of this.
constexpr int kSettleBlocks = 200;
static_assert(static_cast<double>(kSettleBlocks) * static_cast<double>(kBlockSize) / kSr48 >= 1.0,
              "the settle window must be at least a second - ~20x the 50 ms gate "
              "ramp and ~16x the slew limiter's 50-control-step convergence");

/// Blocks scanned for the arm's audio preconditions after settling. Every
/// sample of every one of them is inspected, on both channels.
constexpr int kInspectBlocks = 20;

static_assert(kNumPeaks == ResonanceDriftNetwork::kMaxPeaks,
              "this TU's FR-016 peak count and the component's cap must agree");

/// What the arm's audio was, so a cheap figure can be attributed.
struct ArmCheck {
    bool        allFinite = true;
    bool        allExactZero = true;
    double      sumSq = 0.0;
    std::size_t samples = 0;
    std::size_t activePeaks = 0;
};

void scanBlock(const float* outL, const float* outR, ArmCheck& check) noexcept
{
    for (std::size_t s = 0; s < kBlockSize; ++s) {
        const float l = outL[s];
        const float r = outR[s];
        // detail::isFinite, never std::isnan / std::isinf - the macOS leg builds
        // -ffast-math (core/db_utils.h:118).
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

/// -12 dBFS RMS white noise. Deliberately NOT the outer namespace's fillWhite,
/// whose 0.25 scale is a stage-probe excitation and not this phase's drive
/// level; the reference configuration is measured at the level the spec states.
void fillDrive(float* buffer, std::size_t numSamples, std::uint32_t seed) noexcept
{
    Xorshift32  rng{seed};
    const float scale = dbToGain(kCpuDriveDbfs) * kCpuWhiteRmsScale;
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = rng.nextFloat() * scale;
    }
}

void configureArm(ResonanceDriftNetwork& net, Arm arm) noexcept
{
    // Designated initialisers: Clang errors on narrowing in brace init where
    // MSVC does not.
    net.prepare(kSr48, ResonanceDriftNetwork::PrepareConfig{.maxBlockSamples = kBlockSize,
                                                            .numPeaks = kNumPeaks});
    net.setSeed(kCpuSeed);

    // Everything else in the reference configuration is an FR-016 default that
    // prepare() has just written - the four lane depths, the twelve anchors and
    // ratios, Q = 12, level -6 dB, wander on, mix = 1, every peak awake - so
    // only the two non-default terms are set here.
    net.setAnchorMode(ResonanceDriftNetwork::AnchorMode::Hybrid);
    net.setGravity(kCpuGravity);

    if (arm == Arm::WanderOff) {
        net.setWanderEnabled(false);
    }
    if (arm == Arm::AllDormant) {
        for (std::size_t i = 0; i < kNumPeaks; ++i) {
            net.setPeakDormant(i, true);
        }
    }
}

/// @brief ns per 512-sample block for one arm, best-of-25 x 500 after 400
///        warm-up blocks, with the arm's preconditions checked BEFORE the timed
///        region and re-checked after it.
[[nodiscard]] double measureArm(Arm arm, double& sink, ArmCheck& check)
{
    ResonanceDriftNetwork net;
    configureArm(net, arm);

    std::array<float, kBlockSize> inL{};
    std::array<float, kBlockSize> inR{};
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};
    fillDrive(inL.data(), kBlockSize, 0x0C0FFEEu);
    // "Drive identical on both channels" (SC-004's reference configuration).
    std::copy(inL.begin(), inL.end(), inR.begin());

    // Settle: gates, level ramps, the mix smoother, the slew limiter's walk to
    // the Hybrid anchors, and - for (c) - all twelve FR-042 sleep edges.
    for (int b = 0; b < kSettleBlocks; ++b) {
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
    }

    // Inspect: every sample of kInspectBlocks blocks, both channels. This is
    // what makes arm (c)'s "exactly 0.0f throughout" and arm (a)/(b)'s
    // "genuinely rendering" claims measurements rather than assertions.
    for (int b = 0; b < kInspectBlocks; ++b) {
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        scanBlock(outL.data(), outR.data(), check);
    }

    check.activePeaks = 0;
    for (std::size_t i = 0; i < kNumPeaks; ++i) {
        if (net.isPeakEngineActive(i)) {
            ++check.activePeaks;
        }
    }

    const double ns = bestTrialNs([&]() noexcept {
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        // Read so the render cannot be dead-coded away. Not a result.
        sink += static_cast<double>(outL[0]) + static_cast<double>(outR[kBlockSize - 1]);
    });

    // Re-check AFTER the timed region: 25 x 500 + 400 more blocks have gone
    // through the same state, and a property that held only at second 2 is not
    // the property SC-004 (c) asks for.
    for (int b = 0; b < kInspectBlocks; ++b) {
        net.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        scanBlock(outL.data(), outR.data(), check);
    }
    for (std::size_t i = 0; i < kNumPeaks; ++i) {
        if (net.isPeakEngineActive(i)) {
            // A peak that woke during the timed region would make the
            // pre-timing count a lie; report the union, not the snapshot.
            check.activePeaks = std::max(check.activePeaks, std::size_t{1});
        }
    }

    return ns;
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
    Arm         arm;
    double      measuredNs;
    double      baselineNs;
    const char* constantName;
};

}  // namespace cpu
}  // namespace

// =============================================================================
// T020 - SC-004 (a), (b), (c) and (d)
// =============================================================================

TEST_CASE("ResonanceDriftNetwork_CpuBudget", "[resonance_drift_network][.perf]")
{
    double sink = 0.0;

    cpu::ArmCheck refCheck;
    cpu::ArmCheck wanderCheck;
    cpu::ArmCheck dormantCheck;

    const double nsRef = cpu::measureArm(cpu::Arm::Reference, sink, refCheck);
    const double nsWanderOff = cpu::measureArm(cpu::Arm::WanderOff, sink, wanderCheck);
    const double nsDormant = cpu::measureArm(cpu::Arm::AllDormant, sink, dormantCheck);

    // The sink is read so no arm can be dead-coded away. Not a result.
    REQUIRE(detail::isFinite(sink));
    for (const double ns : {nsRef, nsWanderOff, nsDormant}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    const double refRmsDb = cpu::checkRmsDbfs(refCheck);
    const double wanderRmsDb = cpu::checkRmsDbfs(wanderCheck);

    const double wanderSavingNs = nsRef - nsWanderOff;
    const double wanderSavingFraction = (nsRef > 0.0) ? (wanderSavingNs / nsRef) : 0.0;
    const double dormantSavingNs = nsRef - nsDormant;
    const double dormantSavingFraction = (nsRef > 0.0) ? (dormantSavingNs / nsRef) : 0.0;

    // -------------------------------------------------------------------------
    // Report FIRST, assert second: on a miss the failure has to carry the
    // evidence the escalation decision is taken from.
    // -------------------------------------------------------------------------
    const std::array<cpu::ArmRow, 3> reported{
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
                    .constantName = "kBaselineAllDormantNs"}};

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  ResonanceDriftNetwork SC-004 CPU BUDGET   (48 kHz, 512-sample blocks)\n"
       << "  specs/vorago-phase3-resonance-drift, SC-004 / FR-060, tasks.md T020\n"
       << "  best-of-" << kTrials << " x " << kBlocksPerTrial << " blocks after " << kWarmupBlocks
       << " warm-up, " << cpu::kSettleBlocks << " settle blocks before each\n"
       << "  engine tier: FR-013 Tier 1 (one ResonatorBank, processIndividual)\n"
       << "=================================================================================\n";

    for (const cpu::ArmRow& r : reported) {
        os << row(cpu::armLabel(r.arm), r.measuredNs) << "\n"
           << "        baseline " << std::fixed << std::setprecision(1) << std::setw(10)
           << r.baselineNs << "   gate (x" << std::setprecision(1) << cpu::kRegressionFactor
           << ") " << std::setprecision(1) << std::setw(10)
           << (r.baselineNs * cpu::kRegressionFactor) << "   "
           << (r.measuredNs <= r.baselineNs * cpu::kRegressionFactor ? "within baseline"
                                                                     : "*** OVER BASELINE ***")
           << "\n";
    }

    os << "---------------------------------------------------------------------------------\n"
       << row("SC-004 BUDGET (0.75 % of one core)", kBudgetNs) << "\n"
       << "  FR-015 change-detection saving, (a) - (b)     " << std::fixed << std::setprecision(1)
       << std::setw(12) << wanderSavingNs << " ns/block   " << std::setprecision(2)
       << (100.0 * wanderSavingFraction) << " % of (a)   [SC-004 (b) needs >= 10 %]\n"
       << "  FR-042 dormancy saving,        (a) - (c)     " << std::setprecision(1)
       << std::setw(12) << dormantSavingNs << " ns/block   " << std::setprecision(2)
       << (100.0 * dormantSavingFraction) << " % of (a)   [SC-004 (c) needs >= 40 %]\n"
       << "---------------------------------------------------------------------------------\n"
       << "  audio preconditions, measured over " << (2 * cpu::kInspectBlocks)
       << " fully-scanned blocks per arm (both channels):\n"
       << "      (a) engine-active peaks " << refCheck.activePeaks << " / " << kNumPeaks
       << "   RMS " << std::setprecision(2) << refRmsDb << " dBFS   finite "
       << (refCheck.allFinite ? "yes" : "NO") << "\n"
       << "      (b) engine-active peaks " << wanderCheck.activePeaks << " / " << kNumPeaks
       << "   RMS " << std::setprecision(2) << wanderRmsDb << " dBFS   finite "
       << (wanderCheck.allFinite ? "yes" : "NO") << "\n"
       << "      (c) engine-active peaks " << dormantCheck.activePeaks << " / " << kNumPeaks
       << "   exactly 0.0f on both channels throughout: "
       << (dormantCheck.allExactZero ? "yes" : "NO") << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  copy-pasteable baselines, MEASURED on this machine and this build:\n";
    for (const cpu::ArmRow& r : reported) {
        os << cpu::baselineLine(r.constantName, r.measuredNs) << "\n";
    }
    os << "  Transcribing a measured figure OVER a projection is always correct; take it\n"
       << "  from the LOWEST of several isolated runs, never from one sample. RAISING one\n"
       << "  so a REQUIRE passes is forbidden. A transcribed baseline must still satisfy\n"
       << "  BOTH compile-time clauses (x1.5 <= " << std::fixed << std::setprecision(1)
       << kBudgetNs << " and >= " << cpu::kNoOpFloorNs << "); one that cannot is the\n"
       << "  stop-and-surface case, not a licence to weaken a clause.\n"
       << "=================================================================================\n";

    if (nsRef > kBudgetNs) {
        os << "  *** SC-004 (a) IS OVER THE BUDGET BY " << std::fixed << std::setprecision(1)
           << (nsRef - kBudgetNs) << " ns (" << std::setprecision(2) << (nsRef / kBudgetNs)
           << "x the ceiling).\n"
           << "  *** FR-060 STOP-AND-SURFACE APPLIES. HALT and put to the USER this table\n"
           << "  *** together with ResonanceDriftNetwork_StageCostProbe's per-stage\n"
           << "  *** breakdown (engine, lanes, control writes, per-sample tail, sleep\n"
           << "  *** edge), and take the escalation IN ORDER:\n"
           << "  ***   1. FR-013 Tier 2 - the processSympatheticBankSIMD kernel - when the\n"
           << "  ***      ENGINE stage dominates the probe's table.\n"
           << "  ***   2. FR-038's static-pan fallback - drop the twelve pan lanes'\n"
           << "  ***      advance, setPeakPanWander becomes an accepting no-op - when the\n"
           << "  ***      LANE stage (pan isolated) dominates.\n"
           << "  ***   3. Otherwise STOP AND SURFACE. A cap change, a budget change or a\n"
           << "  ***      threshold change is a USER DECISION ONLY.\n"
           << "  *** NO AGENT MAY lower kMaxPeaks, raise kBudgetNs, relax the 10 % / 40 %\n"
           << "  *** savings, or shrink a workload to make this fit. Reduce cost, never\n"
           << "  *** move the line.\n"
           << "=================================================================================\n";
    } else {
        os << "  SC-004 (a) IS WITHIN BUDGET: " << std::fixed << std::setprecision(1)
           << (kBudgetNs - nsRef) << " ns of headroom (" << std::setprecision(2)
           << (100.0 * nsRef / kBudgetNs) << " % of the ceiling).\n"
           << "=================================================================================\n";
    }

    WARN(os.str());

    // -------------------------------------------------------------------------
    // Preconditions, BEFORE the timing verdicts - a figure taken from a fixture
    // that was not rendering the specified configuration is not a measurement
    // of anything, and must fail as a fixture error rather than as a budget
    // miss.
    // -------------------------------------------------------------------------
    CAPTURE(refCheck.activePeaks, refRmsDb, refCheck.allFinite);
    REQUIRE(refCheck.allFinite);
    REQUIRE(refCheck.activePeaks == kNumPeaks);
    REQUIRE_FALSE(refCheck.allExactZero);
    // Anti-no-op on the AUDIO, not just on the clock: a silent reference arm is
    // exactly what the compile-time floor exists to catch, one level up.
    REQUIRE(refRmsDb > -60.0);

    CAPTURE(wanderCheck.activePeaks, wanderRmsDb, wanderCheck.allFinite);
    REQUIRE(wanderCheck.allFinite);
    REQUIRE(wanderCheck.activePeaks == kNumPeaks);
    REQUIRE_FALSE(wanderCheck.allExactZero);
    REQUIRE(wanderRmsDb > -60.0);

    // SC-004 (c)'s two mandatory pre-timing preconditions, both asserted rather
    // than assumed: every peak's engine is off, and the output is EXACTLY zero
    // on both channels throughout - so a cheap figure cannot have come from an
    // accidental early return elsewhere in the render path.
    CAPTURE(dormantCheck.activePeaks, dormantCheck.allExactZero, dormantCheck.allFinite);
    REQUIRE(dormantCheck.allFinite);
    REQUIRE(dormantCheck.activePeaks == 0u);
    REQUIRE(dormantCheck.allExactZero);

    // -------------------------------------------------------------------------
    // (a) The absolute ceiling. The compile-time clauses bind each baseline to
    // 80 000 ns, so the first REQUIRE binds the MEASUREMENT to SC-004
    // transitively on every machine; the second states the criterion literally
    // as well, so a reader of a failure need not do the transitive step.
    // -------------------------------------------------------------------------
    CAPTURE(nsRef, cpu::kBaselineReferenceNs, kBudgetNs);
    REQUIRE(nsRef <= cpu::kBaselineReferenceNs * cpu::kRegressionFactor);
    REQUIRE(nsRef <= kBudgetNs);

    CAPTURE(nsWanderOff, cpu::kBaselineWanderOffNs);
    REQUIRE(nsWanderOff <= cpu::kBaselineWanderOffNs * cpu::kRegressionFactor);

    CAPTURE(nsDormant, cpu::kBaselineAllDormantNs);
    REQUIRE(nsDormant <= cpu::kBaselineAllDormantNs * cpu::kRegressionFactor);

    // -------------------------------------------------------------------------
    // (b) FR-015's change detection, DIRECTIONAL (SC-004 (b) as amended
    // 2026-09-11, user decision). With the depths scaled to zero the targets
    // stop moving and the indivisible setFrequency -> setQ pair stops firing,
    // but the 48 lanes still advance (FR-036), so the only removable cost is
    // the control-write term: measured 2 208.6 ns = 4.83 % of the reference
    // in isolation, 3.38 % under suite load. The original ">= 10 %" floor was
    // a pre-Q6/Q7 projection that assumed the whole ~7 700 ns term vanishes;
    // it was structurally unreachable, so it is gone. The directional clause
    // still fails on a change-detection path that saves nothing, and the
    // saving is CAPTUREd so every run records the number.
    // -------------------------------------------------------------------------
    CAPTURE(nsRef, nsWanderOff, wanderSavingNs, wanderSavingFraction);
    REQUIRE(nsWanderOff < nsRef);

    // -------------------------------------------------------------------------
    // (c) FR-042's dormancy, as a NUMBER: the twelve disabled slots skipped
    // inside processIndividual (resonator_bank.h:487-488) AND the control-write
    // skip (Clarifications Q6 - a dormant peak's values never reach the bank at
    // all). Projected saving ~24 000 of ~44 300 ns, i.e. ~55 %.
    // -------------------------------------------------------------------------
    CAPTURE(nsRef, nsDormant, dormantSavingNs, dormantSavingFraction);
    REQUIRE(nsDormant < nsRef);
    REQUIRE(dormantSavingFraction >= 0.40);
}
