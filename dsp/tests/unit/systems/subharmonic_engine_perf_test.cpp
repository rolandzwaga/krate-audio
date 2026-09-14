// ==============================================================================
// Layer 3: System Tests - SubharmonicEngine CPU budget and stage-cost probe
// ==============================================================================
// Vorago Phase 6 (specs/vorago-phase6-subharmonic): SubharmonicEngine.
//
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase6-subharmonic/spec.md   (FR-071, SC-013, SC-018)
//            specs/vorago-phase6-subharmonic/plan.md   (S10.2, S12.1-S12.4)
//            specs/vorago-phase6-subharmonic/tasks.md  (T001 creates this TU,
//                                                       T004 lands the primitive
//                                                       stage-cost probe, T005
//                                                       transcribes its table,
//                                                       T020 lands the gated
//                                                       engine arms)
//
// SCOPE OF THIS TU (plan S10.2): SC-013 plus the FR-071 stage/placement probe.
//   Every case here is tagged [.perf] and is therefore hidden from a default
//   run; run it ALONE with `node tools/run-cpu-tests.js dsp_systems_tests`,
//   nothing else executing.
//
// Deliberately OUT of the "-fno-fast-math" block in dsp/tests/CMakeLists.txt:
//   -fno-fast-math would change the figures the baselines are pinned to.
//
// -----------------------------------------------------------------------------
// WHY THIS CASE EXISTS BEFORE THE COMPONENT DOES (plan S15 T1, tasks.md T004)
// -----------------------------------------------------------------------------
// FR-071 arms (a)-(h) price the COMPOSED PRIMITIVES, and they are measured
// before `SubharmonicEngine` is written - there is no such class in the tree
// when this case first runs. The reason is plan S12.4: the three `std::sin`
// inside the three `SubOscillator::process` calls and the divide inside the
// Pade `tanh` are the two candidates for an SC-013 miss, and the pre-authorised
// levers (plan S12.4 (1) and (2)) are only cheap to take while the header is
// still unwritten. Discovering the cost after the component exists costs the
// phase.
//
// This case is a PROBE, NOT A GATE. Every arm REQUIREs only that its figure is
// finite and strictly positive - a zero or a NaN means the MEASUREMENT is
// broken, which is the one thing that would make the table lie. NO ARM IS
// GATED HERE; the gates arrive with the engine arms (i)-(n) in T020, and the
// realisation decision is taken in T005 from the table this case prints.
//
//   *** STOP-AND-SURFACE RULE (FR-071, inherited verbatim from
//   *** resonance_drift_network_perf_test.cpp:57-64) - NON-NEGOTIABLE ***
//   NO IMPLEMENTING AGENT MAY lower kNumTones, raise kBudgetNs, relax a
//   threshold, shrink a workload or widen a tolerance to make a figure fit.
//   Reduce cost, never move the line. The table is emitted loudly via WARN
//   precisely so the decision is taken from the measured numbers rather than
//   from a guess.
//
// -----------------------------------------------------------------------------
// WHY ns/block AND NOT "% of one core"
// -----------------------------------------------------------------------------
// A percent-of-core figure is not reproducible across dev machines or CI
// runners. The measurement basis is NANOSECONDS PER 512-SAMPLE BLOCK AT 48 kHz
// (plan S12.1), the basis established by harmonic_cloud_perf_test.cpp and
// reused by continuous_body_perf_test.cpp, atmosphere_engine_perf_test.cpp and
// resonance_drift_network_perf_test.cpp:66-76. One block period is
// 10 666 667 ns, so the roadmap's 0.5 %/voice ceiling is 53 333 ns/block. The
// percent figure is REPORTED, never asserted.
//
// TRIAL SHAPE (tasks.md T004): best-of-25 x 500 blocks after 400 warm-up
// blocks, the atmosphere_engine_perf_test.cpp idiom. Many short trials, because
// the dev machine is a hybrid part and the dominant noise source is a whole
// trial migrating onto an E-core. Affinity pinning was tried and REJECTED in
// both reference perf TUs.
//
// RUN IT ALONE. Sustained benchmarking heats the CPU and boost clocks drop;
// figures drift ~14 % across a session. Nothing else may be executing:
//   node tools/run-cpu-tests.js dsp_systems_tests
//
// -----------------------------------------------------------------------------
// THE EIGHT ARMS, AND WHY EACH IS SHAPED THE WAY IT IS (tasks.md T004 table,
// plan S12.2)
// -----------------------------------------------------------------------------
//  (a) two PhaseAccumulators, advance() x2 per sample. The irreducible floor:
//      the FR-011 unison master at f and the 4f/3 master that carries
//      FifthBelow. Both `advance()` results are consumed - the method is
//      [[nodiscard]] and its wrap flag is what drives every SubOscillator.
//  (b) ONE SubOscillator at SubWaveform::Sine, driven by a 55 Hz master.
//      Isolates the std::sin that plan S12.4 predicts dominates. NOTE it
//      necessarily contains ONE PhaseAccumulator::advance as well - see the
//      double-counting note on the projection below.
//  (c) the same at SubWaveform::Square. Prices D-6's rejected default and is
//      the shape SC-013 (b) will gate through engine arm (j). Square runs the
//      minBLEP residual instead of the sin, so (c) - (b) is the honest price of
//      the waveform choice.
//  (d) three SubOscillators (Div2 OneOctave and Div4 TwoOctaves off the unison
//      master, FifthBelow OneOctave off the 4f/3 master) summed with three
//      LinearRamp level gains and a HELD per-tone breath scalar - the FR-022
//      three-factor product with the modulator's own cost deliberately absent
//      (no arm prices BreathingModulator; it advances once per 64 samples and
//      plan S12.4 does not list it as a candidate). Reported so a superlinear
//      surprise against 3 x (b) is visible.
//  (e) EnvelopeFollower::processSample in DetectionMode::RMS INCLUDING plan
//      S7.6's per-sample `detail::isFinite` guard on its input. The guard's
//      cost is MEASURED here, not assumed: it is the whole reason SC-009 (c2)
//      is reachable and it is paid on every sample of every render.
//  (f) TwoPoleLP::process per sample PLUS the S5.3 cutoff glide's per-control-
//      step term: one LinearRamp::process(), one std::exp2 and one relative
//      compare per 64 samples, with NO push - the steady state, where the glide
//      is parked and `|applied - pushed|` never crosses kCutoffPushRelative.
//      A push would recompute biquad coefficients, which the component does
//      only when the cutoff really moved.
//  (g) SaturationProcessor::processSample at SaturationType::Tape, input gain
//      +3 dB / output gain -3 dB (the FR-041 default drive). This is the arm
//      that carries A-2's three OnePoleSmoother advances per call (S14 C-6)
//      and the Pade tanh's DIVIDE.
//  (h) DCBlocker2::process prepared at kInfrasonicFilterHz = 18 Hz.
//
//  (r) SUPPLEMENTARY, not one of FR-071's lettered arms: the EIGHT per-sample
//      LinearRamp advances the component runs at steady state (per tone: level
//      + backstop gate = 6, plus trackGainRamp_ and wetGainRamp_ = 8). T005's
//      prediction formula names this term explicitly and no lettered arm
//      contains it, so it is measured here rather than guessed there. All eight
//      are PARKED on their targets, which is the shape plan S12.4 prices
//      ("each one compare when parked").
//
// THE PROJECTION this probe prints is T005's formula verbatim:
//     (a) + 3 x (b) + (e) + (f) + (g) + (h) + (r)
// against kBudgetNs = 53 333. It is REPORTED, NOT GATED. Two honesty notes
// travel with it and must travel with T005's transcription:
//   * it OVER-counts: arm (b) contains one PhaseAccumulator::advance, so
//     3 x (b) charges three master advances on top of arm (a)'s two. The
//     projection is therefore conservative by roughly two advances' worth.
//   * it UNDER-counts: it contains no BreathingModulator::processBlock(64), no
//     three-virtual-getCurrentValue refresh, no per-sample summing/multiplying
//     tail and no dormancy branch. Only the whole-engine arms (i)-(n) in T020
//     can settle SC-013; this projection exists to decide whether a lever is
//     needed NOW, which is exactly T005's question.
//
// -----------------------------------------------------------------------------
// THE SIX WHOLE-ENGINE ARMS AND THE FOUR SC-013 GATES (tasks.md T020,
// plan S12.2/S12.3)
// -----------------------------------------------------------------------------
// Added once `SubharmonicEngine` exists, on the SAME measurement basis:
//  (i) the whole engine at defaults (f = 55, all three tones awake) with a live
//      body                                        - SC-013 (a)'s GATED arm
//  (j) the whole engine, all three tones Square    - SC-013 (b)'s GATED arm
//  (k) the whole engine dormant, FR-025's skip     - SC-013 (c), and OQ-1's
//                                                    cheap-voice term
//  (n) the whole engine at setFundamentalHz(40) -
//      Div4 floored at 10 Hz, two tones awake      - SC-013 (d), REPORTED only
//  (l) ONE instance at defaults                    - OQ-1's global-placement arm
//  (m) EIGHT instances at defaults, all rendered
//      per block                                   - OQ-1's per-voice arm
//
// (l) and (m) are SEPARATE MEASUREMENTS, never (i) x 8: eight instances share
// L1/L2 and the scaling is the thing being measured. (l) is (i)'s workload
// re-measured on a fresh engine, deliberately - the spread between two
// identical arms in one run is the machine's own noise floor, printed beside
// the figures so a reader can see it rather than assume it.
//
// THE GATES, in SubharmonicEngine_CpuBudget:
//   (a) arm (i) <= kBudgetNs. The percent figure is reported, never asserted.
//   (b) arm (j) at the SAME ceiling. Square is a shipped configuration and
//       roadmap line 321's budget is unqualified; if this arm misses, the
//       stop-and-surface rule applies and the WAVEFORM OPTION is reconsidered -
//       never the budget.
//   (c) arm (k) at least kDormancyMinSaving (15 %) cheaper than arm (i), with
//       the absolute ns saving transcribed. If the saving is real but below
//       15 %: stop and surface, and reconsider what FR-025 skips. DO NOT LOWER
//       THE MARGIN.
//   (d) arm (n) measured and reported, not separately gated.
//
// THE CHECKED-IN BASELINES. Every [.perf] case is hidden from the per-push CI
// filter, so the runtime gates above run only when someone runs this suite by
// hand. The absolute ceiling is therefore ALSO carried at COMPILE time, as
// static_asserts over checked-in baseline constants (the
// resonance_drift_network_perf_test.cpp:2841-2856 idiom, itself from
// noise_organism_perf_test.cpp:1560-1575). Each gated baseline carries TWO
// different clauses:
//     static_assert(kBaseline <= kBudgetNs);        // the ceiling
//     static_assert(kBaseline >= kNoOpFloorNs);     // the anti-no-op floor
// The floor is not the ceiling restated: it catches a baseline transcribed from
// a run that did nothing - an unprepared engine takes processBlockTapped()'s
// passthrough branch and advances no state, and a baseline taken from one would
// satisfy the ceiling forever while measuring nothing at all.
//
// TRANSCRIBING A MEASURED FIGURE OVER A PROJECTION IS ALWAYS CORRECT; this case
// prints copy-pasteable lines for exactly that. RAISING one so a REQUIRE passes
// is forbidden. A measured figure that cannot satisfy both clauses is the
// stop-and-surface case, never a licence to weaken a clause.
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/minblep_table.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/two_pole_lp.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/processors/saturation_processor.h>
#include <krate/dsp/processors/sub_oscillator.h>
#include <krate/dsp/systems/subharmonic_engine.h>

// SubharmonicEngine_StageCostProbe (T004) STILL TOUCHES NOTHING IN THAT HEADER.
// It was written and first run when the header did not exist, and that is the
// point of tasks.md T004: arms (a)-(h) price the COMPOSED PRIMITIVES, so a
// change to the component cannot silently move them. The include arrived with
// T020's whole-engine arms (i)-(n) below, which is the first thing in this TU
// that needs the class.

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;

namespace {

// =============================================================================
// Measurement basis (SC-013, plan S12.1)
// =============================================================================

constexpr double kSr48 = 48000.0;
constexpr float kSr48f = 48000.0f;
constexpr std::size_t kBlockSize = 512;

/// The component's control chunk (FR-007, kControlChunkSamples). Arm (f)'s
/// glide term is pushed once per chunk, which is what makes it decidable.
constexpr std::size_t kControlChunk = 64;
static_assert(kBlockSize % kControlChunk == 0,
              "arm (f) pushes its glide term once per control chunk and the "
              "chunk must divide the measured block exactly");

/// Wall-clock period of one 512-sample block at 48 kHz, in nanoseconds.
constexpr double kBlockPeriodNs = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;

/// SC-013's per-voice budget: 0.5 % of one core (roadmap line 321, plan S12.1).
/// Written as the literal the spec names and TIED to its derivation by the
/// clause below rather than computed from it.
///
/// *** NO AGENT RAISES IT. *** The stop-and-surface rule in this file's banner
/// governs every response to a miss.
constexpr double kBudgetNs = 53333.0;
static_assert(kBudgetNs >= kBlockPeriodNs * 0.0049 && kBudgetNs <= kBlockPeriodNs * 0.0051,
              "SC-013's budget is 0.5 % of one 512-sample block at 48 kHz");

// Trial shape, pinned by tasks.md T004.
constexpr int kTrials = 25;
constexpr int kBlocksPerTrial = 500;
constexpr int kWarmupBlocks = 400;

// -----------------------------------------------------------------------------
// The component's normative defaults, spelled LOCALLY because the class that
// will own them does not exist yet - that is the whole point of this task.
// Every value below is the plan S1.2 / S2.2 default, transcribed, never guessed.
// -----------------------------------------------------------------------------
constexpr std::size_t kNumTones = 3;

constexpr float kDefaultFundamentalHz = 55.0f;  ///< A1 (plan S1.2)
constexpr double kFifthRatio = 4.0 / 3.0;       ///< the 4f/3 master (plan S2.3)

constexpr std::array<float, kNumTones> kDefaultToneLevelDb{-18.0f, -24.0f, -30.0f};
constexpr std::array<float, kNumTones> kDefaultToneBreathDepth{0.35f, 0.25f, 0.45f};
constexpr float kMinToneLevelDb = -60.0f;
constexpr float kBreathGainSpan = 0.45f;  ///< noise_organism.h:174

constexpr float kGainRampMs = 50.0f;         ///< noise_organism.h:178
constexpr float kGlideMs = 50.0f;            ///< plan S5.3
constexpr float kCutoffPushRelative = 1e-3f; ///< plan S5.3 (feedback_ecology.h:205)

constexpr std::size_t kBlepOversampling = 64;
constexpr std::size_t kBlepZeroCrossings = 8;
static_assert(kBlepZeroCrossings * 2 <= 64,
              "SubOscillator::prepare rejects a table with length() > 64 "
              "(sub_oscillator.h:143-147) and then returns 0.0f forever");

constexpr float kDefaultLowpassHz = 120.0f;   ///< FR-040
constexpr float kDefaultDriveDb = 3.0f;       ///< FR-041
constexpr float kInfrasonicFilterHz = 18.0f;  ///< FR-042
constexpr float kDefaultFollowerAttackMs = 120.0f;   ///< FR-031
constexpr float kDefaultFollowerReleaseMs = 800.0f;  ///< FR-031
constexpr float kDefaultTrackReferenceDb = -18.0f;   ///< FR-035

/// Arm (r): the eight per-sample LinearRamps the component advances
/// unconditionally - three level ramps, three backstop gates, trackGainRamp_
/// and wetGainRamp_ (plan S1.5, S6 "advance every per-sample ramp
/// unconditionally").
constexpr std::size_t kNumPerSampleRamps = 8;
static_assert(kNumPerSampleRamps == (2 * kNumTones) + 2,
              "the eight ramps are 2 per tone (level + gate) plus trackGain and "
              "wetGain; a different count would silently reprice T005's formula");

/// The body the level-dependent stages are driven with: a -12 dBFS 55 Hz sine,
/// the `makeDefaults` fixture's body (plan S10.3). None of the stages priced
/// here branches on its input value - Biquad, DCBlocker2 and the Pade tanh are
/// all branch-free - so the buffer exists to keep the arms off an all-zero
/// input, where FTZ/DAZ and the smoothers' early-outs would price a render the
/// component never performs.
constexpr float kBodyLevelDb = -12.0f;
constexpr float kBodyHz = 55.0f;

/// plan S2.1: the FR-020 fader bottom maps to EXACT zero, not dbToGain(-60).
[[nodiscard]] constexpr float toneLevelGain(float db) noexcept
{
    return (db <= kMinToneLevelDb) ? 0.0f : dbToGain(db);
}

// =============================================================================
// Best-of-N driver (resonance_drift_network_perf_test.cpp:286-308)
// =============================================================================

/// Pinned warm-up, then best-of-`kTrials` x `kBlocksPerTrial`; returns the
/// winning trial's ns per invocation of `runBlock`. Every arm here invokes one
/// 512-sample block per call, so every result is ns/block.
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

/// One block of the -12 dBFS 55 Hz body, generated once and reused by every
/// level-driven arm so all of them see identical excitation on every machine.
void fillBody(float* buffer, std::size_t numSamples) noexcept
{
    const float amplitude = dbToGain(kBodyLevelDb);
    const double inc = static_cast<double>(kBodyHz) / kSr48;
    double phase = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(kTwoPi * static_cast<float>(phase));
        phase += inc;
        if (phase >= 1.0) {
            phase -= 1.0;
        }
    }
}

/// A prepared master at the FR-013 default fundamental.
[[nodiscard]] PhaseAccumulator makeMaster(float hz) noexcept
{
    PhaseAccumulator acc{};
    acc.setFrequency(hz, kSr48f);
    return acc;
}

// =============================================================================
// Arm (a) - the two masters alone
// =============================================================================

[[nodiscard]] double measureMasters(double& sink)
{
    PhaseAccumulator unison = makeMaster(kDefaultFundamentalHz);
    PhaseAccumulator fifth = unison;
    // plan S2.3: the fifth master is built by SCALING THE INCREMENT in double,
    // never by setFrequency(4/3 * f) - the ratio must be exact over long
    // horizons. Priced in the shape the component will actually run.
    fifth.increment = unison.increment * kFifthRatio;

    return bestTrialNs([&]() noexcept {
        int wraps = 0;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            // advance() is [[nodiscard]] and its wrap flag is what drives every
            // SubOscillator, so both results are consumed here too.
            wraps += unison.advance() ? 1 : 0;
            wraps += fifth.advance() ? 1 : 0;
        }
        sink += static_cast<double>(wraps);
    });
}

// =============================================================================
// Arms (b) and (c) - ONE SubOscillator, Sine and Square
// =============================================================================

/// R-1 guard, and it is not ceremony: if `blepTable_` is not prepared before
/// `SubOscillator::prepare`, the oscillator sets prepared_ = false and
/// `process()` returns 0.0f FOREVER (sub_oscillator.h:143-147, :222-224). This
/// probe would then report the cost of an early return and the whole table
/// would lie. Every SubOscillator arm asserts the table is prepared AND that
/// the oscillator genuinely emits before it is timed.
void requireLiveOscillator(const MinBlepTable& table, SubOscillator& osc, PhaseAccumulator& master)
{
    REQUIRE(table.isPrepared());

    const float inc = static_cast<float>(master.increment);
    double energy = 0.0;
    for (std::size_t n = 0; n < 4096; ++n) {
        const bool wrapped = master.advance();
        energy += std::abs(static_cast<double>(osc.process(wrapped, inc)));
    }
    REQUIRE(energy > 0.0);
}

[[nodiscard]] double measureOneTone(SubWaveform waveform, SubOctave octave, double& sink)
{
    MinBlepTable table;
    table.prepare(kBlepOversampling, kBlepZeroCrossings);

    SubOscillator osc{&table};
    osc.prepare(kSr48);  // AFTER the table: the ordering is the correctness rule
    osc.setOctave(octave);
    osc.setWaveform(waveform);

    PhaseAccumulator master = makeMaster(kDefaultFundamentalHz);
    requireLiveOscillator(table, osc, master);

    const float inc = static_cast<float>(master.increment);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const bool wrapped = master.advance();
            acc += osc.process(wrapped, inc);
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Arm (d) - three tones summed with the FR-022 gain
// =============================================================================

[[nodiscard]] double measureThreeTones(double& sink)
{
    MinBlepTable table;
    table.prepare(kBlepOversampling, kBlepZeroCrossings);
    REQUIRE(table.isPrepared());

    // plan S4.1's structural octave assignment: Div2 and Div4 hang off the
    // unison master, FifthBelow off the 4f/3 master.
    std::array<SubOscillator, kNumTones> oscs{SubOscillator{&table}, SubOscillator{&table},
                                              SubOscillator{&table}};
    constexpr std::array<SubOctave, kNumTones> kOctaves{
        SubOctave::OneOctave, SubOctave::TwoOctaves, SubOctave::OneOctave};

    std::array<LinearRamp, kNumTones> levelRamps{};
    std::array<float, kNumTones> breathGains{};

    for (std::size_t i = 0; i < kNumTones; ++i) {
        oscs[i].prepare(kSr48);
        oscs[i].setOctave(kOctaves[i]);
        oscs[i].setWaveform(SubWaveform::Sine);  // plan S2.2's default, not the shipped Square

        levelRamps[i].configure(kGainRampMs, kSr48f);
        // PARKED on target, the steady state plan S12.4 prices: LinearRamp
        // early-outs at smoother.h:371-373 when current == target.
        levelRamps[i].snapTo(toneLevelGain(kDefaultToneLevelDb[i]));

        // The FR-022 second factor, HELD constant across a control chunk
        // (plan S5.2 step 2). A representative mid-swing breath value of 0.5
        // is used; the scalar's magnitude does not change the multiply's cost,
        // and no arm here prices BreathingModulator itself.
        breathGains[i] = 1.0f + (kBreathGainSpan * kDefaultToneBreathDepth[i] * 0.5f);
    }

    PhaseAccumulator unison = makeMaster(kDefaultFundamentalHz);
    PhaseAccumulator fifth = unison;
    fifth.increment = unison.increment * kFifthRatio;

    const float incUnison = static_cast<float>(unison.increment);
    const float incFifth = static_cast<float>(fifth.increment);

    // R-1 again, for the summed shape: a dead table would make all three tones
    // return 0.0f forever and this arm would price three early returns.
    {
        double energy = 0.0;
        for (std::size_t n = 0; n < 4096; ++n) {
            const bool wrapUnison = unison.advance();
            const bool wrapFifth = fifth.advance();
            energy += std::abs(static_cast<double>(oscs[0].process(wrapUnison, incUnison)));
            energy += std::abs(static_cast<double>(oscs[1].process(wrapUnison, incUnison)));
            energy += std::abs(static_cast<double>(oscs[2].process(wrapFifth, incFifth)));
        }
        REQUIRE(energy > 0.0);
    }

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const bool wrapUnison = unison.advance();
            const bool wrapFifth = fifth.advance();

            float sum = 0.0f;
            sum += oscs[0].process(wrapUnison, incUnison) * levelRamps[0].process()
                   * breathGains[0];
            sum += oscs[1].process(wrapUnison, incUnison) * levelRamps[1].process()
                   * breathGains[1];
            sum += oscs[2].process(wrapFifth, incFifth) * levelRamps[2].process()
                   * breathGains[2];

            acc += sum;
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Arm (e) - the follower, INCLUDING plan S7.6's per-sample isFinite guard
// =============================================================================

[[nodiscard]] double measureFollower(const float* body, double& sink)
{
    EnvelopeFollower follower;
    follower.prepare(kSr48, kBlockSize);
    // The shipped default is DetectionMode::Amplitude (envelope_follower.h:380);
    // FR-030 specifies RMS, so it must be written explicitly or this arm would
    // price the wrong detector.
    follower.setMode(DetectionMode::RMS);
    follower.setAttackTime(kDefaultFollowerAttackMs);
    follower.setReleaseTime(kDefaultFollowerReleaseMs);

    for (std::size_t n = 0; n < kBlockSize; ++n) {
        (void)follower.processSample(body[n]);
    }
    // Non-vacuity: a follower reading zero would mean the arm is pricing a
    // detector that never leaves its floor.
    REQUIRE(follower.getCurrentValue() > 0.0f);

    return bestTrialNs([&]() noexcept {
        float last = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            const float mono = body[n];
            // plan S7.6, C-3: EnvelopeFollower "Does NOT validate input"
            // (envelope_follower.h:162) and a NaN poisons squaredEnvelope_
            // forever. This ONE bit test is what makes SC-009 (c2) reachable,
            // it is paid on every sample, and so it is measured, not assumed.
            last = follower.processSample(detail::isFinite(mono) ? mono : 0.0f);
        }
        sink += static_cast<double>(last);
    });
}

// =============================================================================
// Arm (f) - TwoPoleLP per sample + the S5.3 glide's per-control-step term
// =============================================================================

[[nodiscard]] double measureLowpassStage(const float* body, double& sink)
{
    TwoPoleLP lowpass;
    lowpass.prepare(kSr48);
    lowpass.setCutoff(kDefaultLowpassHz);

    // The glide lives in log2-Hz and is advanced ONCE PER CONTROL CHUNK, so it
    // is configured at the CONTROL rate, not the audio rate (plan S2 step 7 -
    // the single most likely implementer trap in this phase).
    LinearRamp cutoffGlide;
    cutoffGlide.configure(kGlideMs, kSr48f / static_cast<float>(kControlChunk));
    cutoffGlide.snapTo(std::log2(kDefaultLowpassHz));

    float pushedCutoffHz = kDefaultLowpassHz;

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t off = 0; off < kBlockSize; off += kControlChunk) {
            // The control-step half: one parked ramp advance, one exp2, one
            // relative compare. NO push - at steady state the applied value
            // never moves by kCutoffPushRelative, and a push would recompute
            // biquad coefficients the component only recomputes on a real move.
            const float glide = cutoffGlide.process();
            const float applied = std::exp2(glide);
            if (std::fabs(applied - pushedCutoffHz) > kCutoffPushRelative * pushedCutoffHz) {
                lowpass.setCutoff(applied);
                pushedCutoffHz = applied;
            }

            for (std::size_t n = off; n < off + kControlChunk; ++n) {
                acc += lowpass.process(body[n]);
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Arm (g) - SaturationProcessor::processSample at the FR-041 default drive
// =============================================================================

[[nodiscard]] double measureSaturator(const float* body, double& sink)
{
    SaturationProcessor saturator;
    saturator.prepare(kSr48, kBlockSize);
    saturator.setType(SaturationType::Tape);
    // FR-041: drive is +driveDb in, -driveDb out, so the stage is unity at
    // small signals and the drive is a shaping control, not a level control.
    saturator.setInputGain(kDefaultDriveDb);
    saturator.setOutputGain(-kDefaultDriveDb);
    // mix_ defaults to 1.0 (saturation_processor.h:413), so the dry early-exit
    // at :238-240 is NOT taken. If it were, this arm would price a branch
    // instead of the Pade tanh and its divide.

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            acc += saturator.processSample(body[n]);
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Arm (h) - DCBlocker2 at the FR-042 corner
// =============================================================================

[[nodiscard]] double measureBlocker(const float* body, double& sink)
{
    DCBlocker2 blocker;
    blocker.prepare(kSr48, kInfrasonicFilterHz);

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            acc += blocker.process(body[n]);
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// Arm (r) - the eight per-sample LinearRamp advances (supplementary, T005)
// =============================================================================

[[nodiscard]] double measurePerSampleRamps(double& sink)
{
    std::array<LinearRamp, kNumPerSampleRamps> ramps{};
    // Parked on the steady-state targets the component holds at defaults:
    // three level gains, three open backstop gates, the tracking gain and the
    // wet gain. Parked is the honest shape - plan S12.4 prices these as "each
    // one compare when parked" - and a probe that kept them in flight would
    // price a component that is permanently fading.
    for (std::size_t i = 0; i < kNumTones; ++i) {
        ramps[i].configure(kGainRampMs, kSr48f);
        ramps[i].snapTo(toneLevelGain(kDefaultToneLevelDb[i]));

        ramps[kNumTones + i].configure(kGainRampMs, kSr48f);
        ramps[kNumTones + i].snapTo(1.0f);  // gateSteady at f = 55: no tone is floored
    }
    constexpr std::size_t kTrackGainIndex = 2 * kNumTones;
    constexpr std::size_t kWetGainIndex = kTrackGainIndex + 1;
    ramps[kTrackGainIndex].configure(kGainRampMs, kSr48f);
    ramps[kTrackGainIndex].snapTo(1.0f);  // trackGainRamp_ at trackingAmount = 1, envNorm = 1
    ramps[kWetGainIndex].configure(kGainRampMs, kSr48f);
    ramps[kWetGainIndex].snapTo(dbToGain(0.0f));  // wetGainRamp_ at the 0 dB default

    return bestTrialNs([&]() noexcept {
        float acc = 0.0f;
        for (std::size_t n = 0; n < kBlockSize; ++n) {
            for (std::size_t r = 0; r < kNumPerSampleRamps; ++r) {
                acc += ramps[r].process();
            }
        }
        sink += static_cast<double>(acc);
    });
}

// =============================================================================
// T020 - the whole-engine arms (i), (j), (k), (n), (l), (m)
// =============================================================================
// Same basis as arms (a)-(h): best-of-`kTrials` x `kBlocksPerTrial` 512-sample
// blocks at 48 kHz after `kWarmupBlocks` warm-up. These are the arms SC-013
// gates.
//
// EVERY ENGINE ARM RENDERS OUT OF PLACE. In-place (outL == inL) is supported by
// the component (SC-012 (b)) but is the WRONG shape to price here: the added sub
// would re-enter as the next block's dry input, the FR-030 follower would climb
// with every trial, and the arm would measure a body the test never chose.
//
// EVERY ENGINE ARM IS SETTLED AND THEN CHECKED FOR NON-VACUITY before it is
// timed. This is the engine analogue of requireLiveOscillator above and exists
// for the same reason: an unprepared engine takes processBlockTapped()'s
// passthrough branch (out = in, no state advanced), costs almost nothing, and
// would make every figure in the table a lie. The dormant arm is checked the
// other way round - it MUST be bit-identical passthrough, or it is not
// measuring FR-025's skip.

// -----------------------------------------------------------------------------
// The T004 locals above were spelled out LOCALLY because SubharmonicEngine did
// not exist when they were written (that is T004's whole point). It exists now,
// so they are TIED to it here. A probe that silently drifted off the
// component's real defaults would price a configuration the component never
// runs, and every figure in this TU would be about the wrong thing.
// -----------------------------------------------------------------------------
static_assert(kNumTones == SubharmonicEngine::kNumTones);
static_assert(kControlChunk == SubharmonicEngine::kControlChunkSamples);
static_assert(kDefaultFundamentalHz == SubharmonicEngine::kDefaultFundamentalHz);
static_assert(kMinToneLevelDb == SubharmonicEngine::kMinToneLevelDb);
static_assert(kBreathGainSpan == SubharmonicEngine::kBreathGainSpan);
static_assert(kGainRampMs == SubharmonicEngine::kGainRampMs);
static_assert(kGlideMs == SubharmonicEngine::kGlideMs);
static_assert(kCutoffPushRelative == SubharmonicEngine::kCutoffPushRelative);
static_assert(kBlepOversampling == SubharmonicEngine::kBlepOversampling);
static_assert(kBlepZeroCrossings == SubharmonicEngine::kBlepZeroCrossings);
static_assert(kDefaultLowpassHz == SubharmonicEngine::kDefaultLowpassHz);
static_assert(kDefaultDriveDb == SubharmonicEngine::kDefaultDriveDb);
static_assert(kInfrasonicFilterHz == SubharmonicEngine::kInfrasonicFilterHz);
static_assert(kDefaultFollowerAttackMs == SubharmonicEngine::kDefaultFollowerAttackMs);
static_assert(kDefaultFollowerReleaseMs == SubharmonicEngine::kDefaultFollowerReleaseMs);
static_assert(kDefaultTrackReferenceDb == SubharmonicEngine::kDefaultTrackReferenceDb);
static_assert(kDefaultToneLevelDb[0] == SubharmonicEngine::kDefaultToneLevelDb[0] &&
                  kDefaultToneLevelDb[1] == SubharmonicEngine::kDefaultToneLevelDb[1] &&
                  kDefaultToneLevelDb[2] == SubharmonicEngine::kDefaultToneLevelDb[2],
              "the probe's per-tone level defaults must be the component's");
static_assert(kDefaultToneBreathDepth[0] == SubharmonicEngine::kDefaultToneBreathDepth[0] &&
                  kDefaultToneBreathDepth[1] == SubharmonicEngine::kDefaultToneBreathDepth[1] &&
                  kDefaultToneBreathDepth[2] == SubharmonicEngine::kDefaultToneBreathDepth[2],
              "the probe's per-tone breath depths must be the component's");

// -----------------------------------------------------------------------------
// Arm parameters
// -----------------------------------------------------------------------------

/// Settling render before any engine arm is timed, and again after any setter
/// an arm applies. 200 blocks is 2.133 s at 48 kHz - long against the 50 ms
/// gain ramps, the 50 ms cutoff glide and the 800 ms follower release. The
/// FR-021 breathers (0.014-0.037 Hz) never settle BY DESIGN; they are the
/// component's slow modulation and no arm here depends on their phase.
constexpr int kSettleBlocks = 200;
static_assert(static_cast<double>(kSettleBlocks) * static_cast<double>(kBlockSize) / kSr48
                  > 2.0 * (kDefaultFollowerReleaseMs / 1000.0),
              "the settle must be long against the FR-031 release, or an arm is "
              "timed while the tracking gain is still charging");

/// Arm (n)'s fundamental: at 40 Hz the Div4 tone lands at 10 Hz, below
/// SubharmonicEngine::kMinToneHz (12 Hz), so the FR-016 backstop floors it and
/// two of the three tones stay awake.
constexpr float kFlooredFundamentalHz = 40.0f;
static_assert(kFlooredFundamentalHz * 0.25f < SubharmonicEngine::kMinToneHz,
              "arm (n) must actually floor the Div4 tone");
static_assert(kFlooredFundamentalHz * 0.5f > SubharmonicEngine::kMinToneHz,
              "arm (n) must leave the Div2 tone awake");
static_assert(kFlooredFundamentalHz * (4.0f / 3.0f) * 0.5f > SubharmonicEngine::kMinToneHz,
              "arm (n) must leave the FifthBelow tone awake");
static_assert(kFlooredFundamentalHz >= SubharmonicEngine::kMinFundamentalHz &&
                  kFlooredFundamentalHz <= SubharmonicEngine::kMaxFundamentalHz,
              "arm (n)'s fundamental must survive setFundamentalHz's clamp unchanged");

/// SC-013 (c): arm (k) must be at least this much cheaper than arm (i).
///
/// *** NO AGENT LOWERS IT. *** The margin is tied to the ~14 % session-to-
/// session drift the perf idiom records (resonance_drift_network_perf_test.cpp
/// :78-90): a saving smaller than the measurement's own noise floor is not a
/// measured saving. If the saving is real but below 15 %, the response is to
/// STOP AND SURFACE and reconsider what FR-025 skips - never to move this
/// number.
constexpr double kDormancyMinSaving = 0.15;
static_assert(kDormancyMinSaving >= 0.15,
              "SC-013 (c)'s dormancy margin is 15 % and no agent may lower it");

/// OQ-1's per-voice arm (m). Eight is the polyphony the FR-076 / SC-018 table's
/// last two rows are written against.
constexpr std::size_t kPolyInstances = 8;

// -----------------------------------------------------------------------------
// The checked-in baselines and their two compile-time clauses
//
// MEASURED 2026-09-13: MSVC Release, 48 kHz, 512-sample blocks, best-of-25 x
// 500 blocks after 400 warm-up, process pinned to the P-cores (affinity mask
// 0xFFFF), three isolated runs 20 s apart with nothing else executing; each
// constant is the LOWEST of the three. Run-to-run spread was 1.4 % on arm (i),
// 6.5 % on (m) and 22 % on (j).
//
// Arm (k) was re-measured on 2026-09-14 after the dormant path switched the
// three SubOscillators from process() to advance() (state without the
// waveform arithmetic; FR-080's one exception). Before: 15 415 / 16 617 /
// 18 864 ns, a 16-21 % saving on clean runs and 4 % on an interfered one -
// inside the machine's noise against SC-013 (c)'s 15 % line. After: 7 283 /
// 6 999 / 8 241 ns, a 66-71 % saving in the same protocol. The plan's
// projection, kept below for the record, priced the per-sample op ladder at
// nominal cost and multiplied by 512:
//
//     2 PhaseAccumulator::advance   (2 add + 2 cmp)              ~  1.0 ns
//     3 std::sin inside SubOscillator::process                   ~ 18.0 ns
//     3 SubOscillator flip-flop / sanitize overhead              ~  3.0 ns
//     8 parked LinearRamp::process  (one compare each)           ~  1.5 ns
//     1 EnvelopeFollower::processSample RMS + the isFinite bit   ~  5.0 ns
//     1 TwoPoleLP::process          (5 mul, 4 add)               ~  1.5 ns
//     1 SaturationProcessor::processSample (3 smoothers + the
//       Pade tanh's DIVIDE)                                      ~  6.0 ns
//     1 DCBlocker2::process         (5 mul, 4 add)               ~  1.5 ns
//     ~8 further multiplies and 2 compares                       ~  2.0 ns
//     = ~39.5 ns/sample x 512                                    ~ 20 200 ns
//
// Arm (k) drops the four gated terms (low-pass, saturator, blocker, the
// per-sample tail) - about 10.5 of those 39.5 ns, i.e. ~27 % - which is why the
// projection satisfies SC-013 (c)'s 15 % margin with room. Arm (j) is projected
// slightly ABOVE (i) deliberately: Square replaces the std::sin with the minBLEP
// residual path, and this file has no measurement saying which is cheaper, so
// the projection does not assume the favourable direction. Arm (m) is projected
// at 8 x (l) plus a cache-pressure allowance.
//
// The projection was within 3 % of the measurement on (i) and (l), 7 % on (m),
// and pessimistic on (j) (Square is cheaper than Sine, not dearer) and (k).
//
// The case prints copy-pasteable replacement lines BEFORE it evaluates a single
// gate, so the table survives a failure. When re-transcribing, take the LOWEST
// of several isolated runs: this repo has measured a 6.4 % run-to-run spread
// and ~14 % session drift on unchanged code.
// -----------------------------------------------------------------------------

/// The anti-no-op floor, spelled once so the clauses and the report agree.
constexpr double kNoOpFloorNs = kBudgetNs / 50.0;
static_assert(kNoOpFloorNs > 0.0 && kNoOpFloorNs < kBudgetNs,
              "the floor is a floor, not a second ceiling");

/// Reported alongside every arm as a regression readout. It is NOT a gate here:
/// the gates SC-013 names are absolute (kBudgetNs) and relative (arm (k) against
/// arm (i)), and adding a third, tighter runtime gate over a PROJECTED baseline
/// would fail on a machine that is merely slower than the projection - the
/// classic "measuring the machine, not the code" failure.
constexpr double kRegressionFactor = 1.5;

/// (i) the whole engine at defaults, all three tones awake, live body.
constexpr double kBaselineDefaultsNs = 19591.6;

/// (j) the whole engine with all three tones at SubWaveform::Square.
constexpr double kBaselineAllSquareNs = 13312.6;

/// (k) the whole engine dormant: FR-025 skips the low-pass, the saturator, the
/// blocker and the per-sample tail; both masters, all three oscillators, all
/// eight per-sample ramps and the follower still advance.
constexpr double kBaselineDormantNs = 6999.4;

/// (l) ONE instance at defaults - arm (i)'s workload, separately measured.
constexpr double kBaselineOneInstanceNs = 19925.6;

/// (m) EIGHT instances at defaults, all rendered per block.
constexpr double kBaselineEightInstancesNs = 163104.2;

// The two clauses, per arm.
static_assert(kBaselineDefaultsNs <= kBudgetNs,
              "SC-013 (a): the defaults baseline exceeds the 0.5 % per-voice budget");
static_assert(kBaselineDefaultsNs >= kNoOpFloorNs,
              "SC-013 (a): the defaults baseline looks like a no-op run");

static_assert(kBaselineAllSquareNs <= kBudgetNs,
              "SC-013 (b): the all-Square baseline exceeds the 0.5 % per-voice budget");
static_assert(kBaselineAllSquareNs >= kNoOpFloorNs,
              "SC-013 (b): the all-Square baseline looks like a no-op run");

static_assert(kBaselineDormantNs <= kBaselineDefaultsNs * (1.0 - kDormancyMinSaving),
              "SC-013 (c): the checked-in dormant baseline must itself clear the 15 % margin");
static_assert(kBaselineDormantNs >= kNoOpFloorNs,
              "SC-013 (c): the dormant baseline looks like a no-op run - a dormant engine "
              "still advances both masters, three oscillators, eight ramps and the follower");

static_assert(kBaselineOneInstanceNs <= kBudgetNs,
              "OQ-1: the one-instance baseline exceeds the 0.5 % per-voice budget");
static_assert(kBaselineOneInstanceNs >= kNoOpFloorNs,
              "OQ-1: the one-instance baseline looks like a no-op run");

// Arm (m) renders EIGHT voices, so its ceiling is eight per-voice lines, not
// one. It is OQ-1's scaling arm and is not gated at runtime.
static_assert(kBaselineEightInstancesNs <= kBudgetNs * static_cast<double>(kPolyInstances),
              "OQ-1: eight instances exceed eight per-voice budgets");
static_assert(kBaselineEightInstancesNs >= kNoOpFloorNs * static_cast<double>(kPolyInstances),
              "OQ-1: the eight-instance baseline looks like a no-op run");

// -----------------------------------------------------------------------------
// Engine fixtures
// -----------------------------------------------------------------------------

/// prepare() at the measured block size. maxBlockSamples sizes exactly one
/// thing - the SaturationProcessor::dryBuffer_ this component never reads
/// (FR-073) - but it is set to the rendered block anyway so the ledger and the
/// arm describe the same object. The designated initialiser is mandatory
/// (FR-003): a positional brace init would narrow under Clang.
void prepareEngine(SubharmonicEngine& engine)
{
    engine.prepare(kSr48, SubharmonicEngine::PrepareConfig{.maxBlockSamples = kBlockSize});
    REQUIRE(engine.isPrepared());
}

/// Renders `kSettleBlocks` blocks out of place, so every ramp, the glide and
/// the follower are parked before an arm is timed.
void settleEngine(SubharmonicEngine& engine, const float* body, float* outL, float* outR)
{
    for (int b = 0; b < kSettleBlocks; ++b) {
        engine.processBlock(body, body, outL, outR, kBlockSize);
    }
}

/// Sum |out - dry| over one block. Written as a magnitude sum rather than a
/// float equality so the same helper serves both the "must add audio" and the
/// "must be bit-identical passthrough" checks: the sum is EXACTLY 0.0 when every
/// sample was copied verbatim, and strictly positive the moment one was not.
[[nodiscard]] double sumAbsDelta(const float* out, const float* dry)
{
    double delta = 0.0;
    for (std::size_t n = 0; n < kBlockSize; ++n) {
        delta += std::abs(static_cast<double>(out[n]) - static_cast<double>(dry[n]));
    }
    return delta;
}

/// R-2, the engine analogue of requireLiveOscillator: settle, render one more
/// block, and REQUIRE that the engine really added a sub to BOTH channels. An
/// engine that rendered silence - unprepared, or with every tone at the fader
/// bottom - takes a branch that costs almost nothing, and the arm would price
/// that branch instead of the component.
void requireEngineAudible(SubharmonicEngine& engine, const float* body, float* outL, float* outR)
{
    settleEngine(engine, body, outL, outR);
    engine.processBlock(body, body, outL, outR, kBlockSize);
    REQUIRE(sumAbsDelta(outL, body) > 0.0);
    REQUIRE(sumAbsDelta(outR, body) > 0.0);
}

/// The dormant arm's non-vacuity check, and it is the OPPOSITE assertion: FR-025
/// makes the skip write `out = in` verbatim, so a genuinely dormant engine is
/// bit-identical passthrough on both channels AND reports all three tones
/// dormant. Without both halves this arm could be cheap for the wrong reason -
/// an unprepared engine is also cheap, and also passes audio through.
void requireEngineDormant(SubharmonicEngine& engine, const float* body, float* outL, float* outR)
{
    settleEngine(engine, body, outL, outR);
    engine.processBlock(body, body, outL, outR, kBlockSize);
    for (std::size_t i = 0; i < kNumTones; ++i) {
        REQUIRE(engine.isToneDormant(i));
    }
    REQUIRE(engine.isPrepared());
    REQUIRE_FALSE(sumAbsDelta(outL, body) > 0.0);
    REQUIRE_FALSE(sumAbsDelta(outR, body) > 0.0);
}

/// The shared timed body for every single-instance engine arm: one
/// processBlock() per invocation, so the figure is ns/block like every other arm
/// in this TU. The sink read is what stops the whole render being dead-coded.
[[nodiscard]] double timeEngine(SubharmonicEngine& engine, const float* body, float* outL,
                                float* outR, double& sink)
{
    return bestTrialNs([&]() noexcept {
        engine.processBlock(body, body, outL, outR, kBlockSize);
        sink += static_cast<double>(outL[kBlockSize - 1]) + static_cast<double>(outR[0]);
    });
}

// -----------------------------------------------------------------------------
// Arm (i) - the whole engine at defaults, SC-013 (a)'s gated arm
// -----------------------------------------------------------------------------

[[nodiscard]] double measureEngineDefaults(const float* body, double& sink)
{
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};

    SubharmonicEngine engine;
    prepareEngine(engine);
    // Nothing is written after prepare(): prepare() step (9) applies the
    // FR-020/FR-021/FR-031/FR-035/FR-040/FR-041/FR-051 defaults itself, so
    // "at defaults" is the prepared state and any setter here would be a
    // different arm.
    REQUIRE(engine.getFundamentalHz() == kDefaultFundamentalHz);
    requireEngineAudible(engine, body, outL.data(), outR.data());

    return timeEngine(engine, body, outL.data(), outR.data(), sink);
}

// -----------------------------------------------------------------------------
// Arm (j) - all three tones Square, SC-013 (b)'s gated arm
// -----------------------------------------------------------------------------

[[nodiscard]] double measureEngineAllSquare(const float* body, double& sink)
{
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};

    SubharmonicEngine engine;
    prepareEngine(engine);
    // AFTER prepare(), never before: prepare() applies the defaults and would
    // put every tone back to Sine.
    for (std::size_t i = 0; i < kNumTones; ++i) {
        engine.setToneWaveform(i, SubWaveform::Square);
        REQUIRE(engine.getToneWaveform(i) == SubWaveform::Square);
    }
    requireEngineAudible(engine, body, outL.data(), outR.data());

    return timeEngine(engine, body, outL.data(), outR.data(), sink);
}

// -----------------------------------------------------------------------------
// Arm (k) - the whole engine dormant, FR-025's skip. SC-013 (c) and OQ-1's
// cheap-voice term.
// -----------------------------------------------------------------------------

[[nodiscard]] double measureEngineDormant(const float* body, double& sink)
{
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};

    SubharmonicEngine engine;
    prepareEngine(engine);
    // The FR-020 fader bottom maps to a LITERAL 0.0f (plan S2.1), which is what
    // makes FR-023's dormancy predicate an exact `== 0.0f` on both the ramp's
    // target and its current value. The settle inside requireEngineDormant()
    // is what carries the 50 ms fade to that exact zero.
    for (std::size_t i = 0; i < kNumTones; ++i) {
        engine.setToneLevelDb(i, kMinToneLevelDb);
    }
    requireEngineDormant(engine, body, outL.data(), outR.data());

    return timeEngine(engine, body, outL.data(), outR.data(), sink);
}

// -----------------------------------------------------------------------------
// Arm (n) - setFundamentalHz(40): Div4 floored, two tones awake. SC-013 (d),
// reported and not gated.
// -----------------------------------------------------------------------------

[[nodiscard]] double measureEngineFlooredAt40(const float* body, double& sink)
{
    std::array<float, kBlockSize> outL{};
    std::array<float, kBlockSize> outR{};

    SubharmonicEngine engine;
    prepareEngine(engine);
    engine.setFundamentalHz(kFlooredFundamentalHz);
    requireEngineAudible(engine, body, outL.data(), outR.data());

    // The arm is only arm (n) if the backstop really engaged. infrasonicFloored
    // is written by updateControl(), so it is checked AFTER the settle render,
    // never straight after the setter.
    REQUIRE_FALSE(engine.isToneInfrasonicFloored(0));
    REQUIRE(engine.isToneInfrasonicFloored(1));
    REQUIRE_FALSE(engine.isToneInfrasonicFloored(2));
    // Floored is NOT dormant (FR-016): the tone is silenced through its gate and
    // its oscillator keeps advancing, so the chain stays active and this arm is
    // NOT a second dormancy arm.
    for (std::size_t i = 0; i < kNumTones; ++i) {
        REQUIRE_FALSE(engine.isToneDormant(i));
    }

    return timeEngine(engine, body, outL.data(), outR.data(), sink);
}

// -----------------------------------------------------------------------------
// Arm (l) - ONE instance at defaults, OQ-1's global-placement arm
// -----------------------------------------------------------------------------

/// Arm (i)'s workload on a FRESH engine and a fresh best-of-N, deliberately not
/// a reuse of arm (i)'s number: the FR-076 table needs a one-instance figure
/// taken in the same run as the eight-instance figure, and the spread between
/// two identical arms is this run's own noise floor, which the report prints.
[[nodiscard]] double measureOneInstance(const float* body, double& sink)
{
    return measureEngineDefaults(body, sink);
}

// -----------------------------------------------------------------------------
// Arm (m) - EIGHT instances at defaults, OQ-1's per-voice-placement arm
// -----------------------------------------------------------------------------

[[nodiscard]] double measureEightInstances(const float* body, double& sink)
{
    // SubharmonicEngine is neither copyable nor movable (the three
    // SubOscillators point at this object's own blepTable_), so it cannot live
    // in a reallocating container. A heap-allocated std::array is the shape
    // Phase 10 would use for a fixed voice pool, and it keeps ~8 engines plus
    // their buffers off the test's stack.
    auto engines = std::make_unique<std::array<SubharmonicEngine, kPolyInstances>>();

    // Per-instance output buffers: eight voices writing one shared buffer would
    // measure a cache-friendliness the real thing does not have.
    std::vector<float> outL(kPolyInstances * kBlockSize, 0.0f);
    std::vector<float> outR(kPolyInstances * kBlockSize, 0.0f);

    for (std::size_t v = 0; v < kPolyInstances; ++v) {
        SubharmonicEngine& engine = (*engines)[v];
        prepareEngine(engine);
        // At DEFAULTS, exactly as the arm is specified - same seed, same
        // fundamental, same levels. The eight are therefore identical by
        // construction; what this arm measures is eight instances' worth of
        // state sharing L1/L2, not eight different configurations.
        requireEngineAudible(engine, body, outL.data() + (v * kBlockSize),
                             outR.data() + (v * kBlockSize));
    }

    return bestTrialNs([&]() noexcept {
        for (std::size_t v = 0; v < kPolyInstances; ++v) {
            (*engines)[v].processBlock(body, body, outL.data() + (v * kBlockSize),
                                       outR.data() + (v * kBlockSize), kBlockSize);
        }
        sink += static_cast<double>(outL[0]) + static_cast<double>(outR[kBlockSize - 1]);
    });
}

// =============================================================================
// Reporting
// =============================================================================

[[nodiscard]] std::string row(const std::string& label, double nsPerBlock)
{
    std::ostringstream os;
    os << std::left << std::setw(56) << label << std::right << std::fixed << std::setprecision(1)
       << std::setw(12) << nsPerBlock << " ns/block   " << std::setprecision(4) << std::setw(9)
       << (100.0 * nsPerBlock / kBlockPeriodNs) << " % of one core";
    return os.str();
}

/// One row of plan S12.3's FR-076 / SC-018 placement table.
[[nodiscard]] std::string placementRow(const std::string& label, std::size_t instances,
                                       double nsPerBlock)
{
    std::ostringstream os;
    os << std::left << std::setw(34) << label << std::right << std::setw(4) << instances
       << std::fixed << std::setprecision(1) << std::setw(14) << nsPerBlock << " ns"
       << std::setprecision(3) << std::setw(10) << (100.0 * nsPerBlock / kBlockPeriodNs) << " %"
       << std::setw(10)
       << (100.0 * nsPerBlock / (static_cast<double>(instances) * kBlockPeriodNs)) << " %/voice";
    return os.str();
}

/// One copy-pasteable baseline line, so a measured figure replaces a projection
/// by transcription rather than by arithmetic done in someone's head.
[[nodiscard]] std::string baselineLine(const std::string& name, double measuredNs)
{
    std::ostringstream os;
    os << "    constexpr double " << std::left << std::setw(28) << name << " = " << std::fixed
       << std::setprecision(1) << measuredNs << ";";
    return os.str();
}

}  // namespace

// =============================================================================
// T004 - the FR-071 stage-cost probe, arms (a)-(h), with NO SubharmonicEngine
// =============================================================================

TEST_CASE("SubharmonicEngine_StageCostProbe", "[subharmonic_engine][.perf]")
{
    double sink = 0.0;

    std::array<float, kBlockSize> body{};
    fillBody(body.data(), kBlockSize);

    const double armA = measureMasters(sink);
    const double armB = measureOneTone(SubWaveform::Sine, SubOctave::OneOctave, sink);
    const double armC = measureOneTone(SubWaveform::Square, SubOctave::OneOctave, sink);
    const double armD = measureThreeTones(sink);
    const double armE = measureFollower(body.data(), sink);
    const double armF = measureLowpassStage(body.data(), sink);
    const double armG = measureSaturator(body.data(), sink);
    const double armH = measureBlocker(body.data(), sink);
    const double armR = measurePerSampleRamps(sink);

    // The sink is read so no arm can be dead-coded away. It is not a result.
    REQUIRE(detail::isFinite(sink));

    // -------------------------------------------------------------------------
    // The only assertions this case makes: a PROBE, not a gate (tasks.md T004).
    // A zero or a non-finite figure means the MEASUREMENT is broken, which is
    // the one thing that would make the table below lie.
    // -------------------------------------------------------------------------
    for (const double ns : {armA, armB, armC, armD, armE, armF, armG, armH, armR}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    // -------------------------------------------------------------------------
    // T005's prediction formula, verbatim. REPORTED, NOT GATED - the ruling on
    // it is T005's, taken from the table below, and the stop-and-surface rule
    // in this file's banner governs the response to a miss.
    // -------------------------------------------------------------------------
    const double projectedNs =
        armA + (3.0 * armB) + armE + armF + armG + armH + armR;
    const double marginNs = kBudgetNs - projectedNs;

    UNSCOPED_INFO(row("(a) two PhaseAccumulators, advance() x2", armA));
    UNSCOPED_INFO(row("(b) one SubOscillator, Sine", armB));
    UNSCOPED_INFO(row("(c) one SubOscillator, Square", armC));
    UNSCOPED_INFO(row("(d) three tones + FR-022 gains", armD));
    UNSCOPED_INFO(row("(e) EnvelopeFollower RMS + isFinite guard", armE));
    UNSCOPED_INFO(row("(f) TwoPoleLP + S5.3 glide term", armF));
    UNSCOPED_INFO(row("(g) SaturationProcessor Tape +3/-3 dB", armG));
    UNSCOPED_INFO(row("(h) DCBlocker2 at 18 Hz", armH));
    UNSCOPED_INFO(row("(r) eight parked LinearRamp advances", armR));
    UNSCOPED_INFO(row("== T005 PREDICTED ENGINE TOTAL (reported)", projectedNs));

    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  SubharmonicEngine T004 STAGE-COST PROBE - measured before the component\n"
       << "  exists (specs/vorago-phase6-subharmonic, plan S12.2/S12.4, FR-071 arms a-h)\n"
       << "  48 kHz, 512-sample blocks, best-of-" << kTrials << " x " << kBlocksPerTrial
       << " blocks after " << kWarmupBlocks << " warm-up\n"
       << "  RUN IN ISOLATION. A figure taken beside a build or another suite is not\n"
       << "  evidence.\n"
       << "=================================================================================\n"
       << "  THE GENERATORS\n"
       << row("      (a) two PhaseAccumulators, advance() x2/sample", armA) << "\n"
       << row("      (b) ONE SubOscillator, Sine (has 1 advance in it)", armB) << "\n"
       << row("      (c) ONE SubOscillator, Square (minBLEP, no sin)", armC) << "\n"
       << row("      (d) THREE tones + 3 LinearRamp + held breath", armD) << "\n"
       << "      (c) - (b) is the honest price of D-6's waveform choice; (d) against\n"
       << "      3 x (b) is where a superlinear surprise would show.\n"
       << "---------------------------------------------------------------------------------\n"
       << "  THE CHAIN, IN FR-040 ORDER\n"
       << row("      (e) EnvelopeFollower RMS + S7.6 isFinite guard", armE) << "\n"
       << row("      (f) TwoPoleLP/sample + glide exp2+compare/64", armF) << "\n"
       << row("      (g) SaturationProcessor::processSample, Tape", armG) << "\n"
       << row("      (h) DCBlocker2::process at 18 Hz", armH) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  THE TAIL\n"
       << row("      (r) 8 parked LinearRamp::process()/sample", armR) << "\n"
       << "      Supplementary, not an FR-071 letter: T005's formula names this term and\n"
       << "      no lettered arm contains it.\n"
       << "=================================================================================\n"
       << "  T005 PREDICTION: (a) + 3 x (b) + (e) + (f) + (g) + (h) + (r)\n"
       << "=================================================================================\n"
       << row("  predicted engine total at defaults", projectedNs) << "\n"
       << row("  SC-013 BUDGET (0.5 % of one core, per voice)", kBudgetNs) << "\n";

    if (marginNs > 0.0) {
        os << "  UNDER the ceiling by " << std::fixed << std::setprecision(1) << marginNs
           << " ns (" << std::setprecision(2) << (100.0 * projectedNs / kBudgetNs)
           << " % of the SC-013 line).\n"
           << "  T005 records the eight figures, this total and this margin, then Group D\n"
           << "  proceeds unchanged. NO LEVER IS TAKEN on a prediction that fits.\n";
    } else {
        os << "  *** AT OR OVER the ceiling by " << std::fixed << std::setprecision(1)
           << (-marginNs) << " ns (" << std::setprecision(2)
           << (projectedNs / kBudgetNs) << "x the line).\n"
           << "  *** >>> T005 STOPS AND SURFACES THIS TABLE TO THE USER. <<<\n"
           << "  *** The pre-authorised levers, IN ORDER (plan S12.4), and NEITHER may be\n"
           << "  *** applied before the measured table is on the record:\n"
           << "  ***   (1) replace the SaturationProcessor stage with a direct Sigmoid::tanh\n"
           << "  ***       plus this component's own two scalars - A DEPARTURE FROM FR-041\n"
           << "  ***       that must be raised as a spec question, never taken silently;\n"
           << "  ***   (2) if the three std::sin dominate - i.e. 3 x (b) is the largest\n"
           << "  ***       term - nothing in this phase can remove them without abandoning\n"
           << "  ***       D-4's reuse mandate: surface arms (b) and (d) and let the user\n"
           << "  ***       rule.\n";
    }

    os << "---------------------------------------------------------------------------------\n"
       << "  HONESTY NOTES that must travel with T005's transcription:\n"
       << "   * OVER-counts: arm (b) contains one PhaseAccumulator::advance, so 3 x (b)\n"
       << "     charges three master advances on top of arm (a)'s two.\n"
       << "   * UNDER-counts: no BreathingModulator::processBlock(64), no three-virtual\n"
       << "     getCurrentValue refresh, no per-sample summing/multiplying tail, no\n"
       << "     dormancy branch, no FR-026 sleep-edge memset. Only the whole-engine arms\n"
       << "     (i)-(n) in T020 can settle SC-013.\n"
       << "   * Arm (r) is a FLOOR: every ramp is parked, LinearRamp::process() early-outs\n"
       << "     at smoother.h:371-373, and the compiler may hoist the load out of the\n"
       << "     inner loop. It is measured parked anyway because parked is the shape the\n"
       << "     component holds at steady state (plan S12.4).\n"
       << "   * The FR-041 default drive is +" << std::setprecision(1) << kDefaultDriveDb
       << " dB in / -" << kDefaultDriveDb << " dB out, and the follower\n"
       << "     reference is " << kDefaultTrackReferenceDb << " dBFS; the body driving\n"
       << "     arms (e)-(h) is a " << kBodyHz << " Hz sine at " << kBodyLevelDb << " dBFS.\n"
       << "=================================================================================\n"
       << "  RECORD the full table above and the verdict in the scratchpad log and, at\n"
       << "  T005, in the phase's compliance notes.\n"
       << "=================================================================================\n";

    WARN(os.str());
}

// =============================================================================
// T020 - SC-013's gates: arms (i), (j), (k), (n) and OQ-1's (l) / (m)
// =============================================================================
// THE TABLE IS PRINTED BEFORE THE FIRST GATE IS EVALUATED. Catch2's REQUIRE
// aborts the case, and the one thing a stop-and-surface response needs is the
// measured table - so the report is emitted first and the gates come last.
// =============================================================================

TEST_CASE("SubharmonicEngine_CpuBudget", "[subharmonic_engine][.perf]")
{
    double sink = 0.0;

    std::array<float, kBlockSize> body{};
    fillBody(body.data(), kBlockSize);

    const double armI = measureEngineDefaults(body.data(), sink);
    const double armJ = measureEngineAllSquare(body.data(), sink);
    const double armK = measureEngineDormant(body.data(), sink);
    const double armN = measureEngineFlooredAt40(body.data(), sink);
    const double armL = measureOneInstance(body.data(), sink);
    const double armM = measureEightInstances(body.data(), sink);

    // The sink is read so no arm can be dead-coded away. It is not a result.
    REQUIRE(detail::isFinite(sink));

    // A zero or a non-finite figure means the MEASUREMENT is broken, which
    // would make every gate below meaningless rather than merely wrong.
    for (const double ns : {armI, armJ, armK, armN, armL, armM}) {
        REQUIRE(detail::isFinite(ns));
        REQUIRE(ns > 0.0);
    }

    // -------------------------------------------------------------------------
    // The SC-013 (c) arithmetic, computed here so the report and the gate below
    // cannot disagree about it.
    // -------------------------------------------------------------------------
    const double dormancySavingNs = armI - armK;
    const double dormancySavingFraction = dormancySavingNs / armI;

    // arm (i) and arm (l) are the SAME workload measured twice. Their spread is
    // this run's own noise floor and is printed so no reader has to assume it.
    const double identicalArmSpread = std::abs(armI - armL) / std::max(armI, armL);

    // -------------------------------------------------------------------------
    // The report. Emitted BEFORE the gates, deliberately.
    // -------------------------------------------------------------------------
    std::ostringstream os;
    os << "\n"
       << "=================================================================================\n"
       << "  SubharmonicEngine T020 CPU BUDGET - the whole-engine arms (i)-(n)\n"
       << "  specs/vorago-phase6-subharmonic: SC-013 (a)-(d), FR-076 / SC-018, plan S12.3\n"
       << "  48 kHz, 512-sample blocks, best-of-" << kTrials << " x " << kBlocksPerTrial
       << " blocks after " << kWarmupBlocks << " warm-up\n"
       << "  RUN IN ISOLATION - nothing else executing, and not back-to-back. A figure\n"
       << "  taken beside a build, a lint or another suite is not evidence.\n"
       << "=================================================================================\n"
       << "  THE GATED ARMS\n"
       << row("      (i) whole engine, defaults, live body", armI) << "\n"
       << row("      (j) whole engine, all three tones Square", armJ) << "\n"
       << row("      (k) whole engine dormant (FR-025 skip)", armK) << "\n"
       << row("      SC-013 BUDGET (0.5 % of one core, /voice)", kBudgetNs) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  SC-013 (c): the dormancy saving\n"
       << "      (i) - (k) = " << std::fixed << std::setprecision(1) << dormancySavingNs
       << " ns/block, i.e. " << std::setprecision(2) << (100.0 * dormancySavingFraction)
       << " % cheaper\n"
       << "      required: at least " << std::setprecision(1) << (100.0 * kDormancyMinSaving)
       << " % - NOT NEGOTIABLE, and never lowered to fit a figure.\n"
       << "---------------------------------------------------------------------------------\n"
       << "  THE REPORTED ARMS\n"
       << row("      (n) f = 40 Hz, Div4 floored, 2 tones awake", armN) << "\n"
       << row("      (l) ONE instance at defaults", armL) << "\n"
       << row("      (m) EIGHT instances at defaults", armM) << "\n"
       << "      (i) vs (l) - the same workload measured twice - differ by "
       << std::setprecision(2) << (100.0 * identicalArmSpread) << " %.\n"
       << "      That spread IS this run's noise floor: read every margin below against\n"
       << "      it before treating a difference as a property of the code.\n"
       << "=================================================================================\n"
       << "  FR-076 / SC-018 - THE PLACEMENT TABLE (plan S12.3). T021 transcribes this\n"
       << "  and PRESENTS IT TO THE USER; the roadmap Open Question 4 ruling is theirs.\n"
       << "  Placement                       inst      cost/block    % core   % per voice\n"
       << "---------------------------------------------------------------------------------\n"
       << placementRow("  global (post-voice-sum)", 1, armL) << "\n"
       << placementRow("  per-voice, 4 voices", 4, 4.0 * armL) << "\n"
       << placementRow("  per-voice, 6 voices", 6, 6.0 * armL) << "\n"
       << placementRow("  per-voice, 8 voices (measured)", kPolyInstances, armM) << "\n"
       << placementRow("  per-voice, 8, 6 dormant", kPolyInstances, (2.0 * armL) + (6.0 * armK))
       << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  Set the per-voice column against the roadmap's 4-5 % per-voice envelope\n"
       << "  (roadmap line 92) NET of what phases 2, 3 and 5 have already spent -\n"
       << "  1.75 % + 0.75 % + 1.5 % = 4.0 % - so the per-voice placement has about\n"
       << "  0.5-1.0 % of headroom left, and a 0.5 % component fits only at the top of\n"
       << "  that range. The global row is measured against the GLOBAL budget, not the\n"
       << "  per-voice line.\n"
       << "=================================================================================\n"
       << "  CHECKED-IN BASELINES - copy these over the constants in this file's\n"
       << "  baseline block, taking the LOWEST of several isolated runs. Transcribing a\n"
       << "  MEASUREMENT over a PROJECTION is always correct; RAISING one so a gate\n"
       << "  passes is forbidden.\n"
       << "---------------------------------------------------------------------------------\n"
       << baselineLine("kBaselineDefaultsNs", armI) << "\n"
       << baselineLine("kBaselineAllSquareNs", armJ) << "\n"
       << baselineLine("kBaselineDormantNs", armK) << "\n"
       << baselineLine("kBaselineOneInstanceNs", armL) << "\n"
       << baselineLine("kBaselineEightInstancesNs", armM) << "\n"
       << "---------------------------------------------------------------------------------\n"
       << "  The currently checked-in values (PROJECTIONS until transcribed):\n"
       << baselineLine("kBaselineDefaultsNs", kBaselineDefaultsNs) << "\n"
       << baselineLine("kBaselineAllSquareNs", kBaselineAllSquareNs) << "\n"
       << baselineLine("kBaselineDormantNs", kBaselineDormantNs) << "\n"
       << baselineLine("kBaselineOneInstanceNs", kBaselineOneInstanceNs) << "\n"
       << baselineLine("kBaselineEightInstancesNs", kBaselineEightInstancesNs) << "\n"
       << "  Regression readout (reported, NOT gated - see kRegressionFactor): measured\n"
       << "  against baseline x " << std::setprecision(1) << kRegressionFactor << " gives (i) "
       << (armI <= kBaselineDefaultsNs * kRegressionFactor ? "within" : "OVER")
       << ", (j) " << (armJ <= kBaselineAllSquareNs * kRegressionFactor ? "within" : "OVER")
       << ", (k) " << (armK <= kBaselineDormantNs * kRegressionFactor ? "within" : "OVER")
       << ", (l) " << (armL <= kBaselineOneInstanceNs * kRegressionFactor ? "within" : "OVER")
       << ", (m) " << (armM <= kBaselineEightInstancesNs * kRegressionFactor ? "within" : "OVER")
       << ".\n"
       << "=================================================================================\n"
       << "  IF A GATE BELOW FAILS: REDUCE COST, NEVER MOVE THE LINE.\n"
       << "   * (a)/(b) over kBudgetNs -> plan S12.4's levers, IN ORDER. Lever 1 replaces\n"
       << "     the SaturationProcessor stage with a direct Sigmoid::tanh and is A\n"
       << "     DEPARTURE FROM FR-041 that must be raised as a spec question, never taken\n"
       << "     silently. If (j) alone misses, the WAVEFORM OPTION is what is\n"
       << "     reconsidered - roadmap line 321's budget is unqualified.\n"
       << "   * (c) below 15 % -> stop and surface, and reconsider what FR-025 skips.\n"
       << "     DO NOT LOWER kDormancyMinSaving.\n"
       << "   * A verdict that flips between runs is measuring the machine. Confirm\n"
       << "     nothing else was running, let the machine idle, re-run this suite alone,\n"
       << "     and only then treat it as a defect.\n"
       << "=================================================================================\n";

    WARN(os.str());

    // -------------------------------------------------------------------------
    // SC-013 (a) - arm (i) at or under the 0.5 %/voice ceiling. The PERCENT
    // figure is reported above and is never asserted; kBudgetNs is the line.
    // -------------------------------------------------------------------------
    UNSCOPED_INFO(row("SC-013 (a) arm (i) whole engine at defaults", armI));
    UNSCOPED_INFO(row("SC-013 (a) ceiling", kBudgetNs));
    REQUIRE(armI <= kBudgetNs);

    // -------------------------------------------------------------------------
    // SC-013 (b) - arm (j) at the SAME ceiling. Square is a shipped
    // configuration; a miss reconsiders the waveform option, never the budget.
    // -------------------------------------------------------------------------
    UNSCOPED_INFO(row("SC-013 (b) arm (j) all three tones Square", armJ));
    REQUIRE(armJ <= kBudgetNs);

    // -------------------------------------------------------------------------
    // SC-013 (c) - arm (k) at least 15 % cheaper than arm (i).
    // -------------------------------------------------------------------------
    UNSCOPED_INFO(row("SC-013 (c) arm (k) dormant", armK));
    UNSCOPED_INFO(row("SC-013 (c) required ceiling for (k)",
                      armI * (1.0 - kDormancyMinSaving)));
    REQUIRE(armK <= armI * (1.0 - kDormancyMinSaving));

    // -------------------------------------------------------------------------
    // SC-013 (d) - arm (n) is MEASURED AND REPORTED, not separately gated. The
    // only assertion it carries is the shared non-vacuity check above, which is
    // what makes "reported" mean a real figure rather than an unchecked one.
    // -------------------------------------------------------------------------
    UNSCOPED_INFO(row("SC-013 (d) arm (n) f = 40 Hz, Div4 floored", armN));
}
