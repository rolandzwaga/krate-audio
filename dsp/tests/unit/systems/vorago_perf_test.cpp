// ==============================================================================
// Layer 3: System Tests - Vorago CPU budgets
//                                    (specs/vorago-phase10-voice-engine)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase10-voice-engine/spec.md   (SC-001a, SC-001b, SC-002,
//                                                         SC-003)
//            specs/vorago-phase10-voice-engine/plan.md   (S12.1 - S12.4, B-6, B-7)
//            specs/vorago-phase10-voice-engine/tasks.md  (T006 creates and wires
//                                                         this TU; T021 fills it)
//
// SCOPE OF THIS TU: SC-001a, SC-001b, SC-002 (including the advanceLifeOnly / L
//   arm of ruling B-6's N*V + (kMaxVoices - N)*L + G <= budget solve), SC-003
//   and VoragoVoice_ClearingPathCost. Every case here is [.perf].
//
// SC-001b - THE GATE - WAS WRITTEN LAST, AND THAT ORDER IS FR-083, NOT AN
//   ACCIDENT. Its threshold is the (polyphony, lever set) pair the OQ-1(a) / Q-A
//   ruling fixes, and FR-083 forbids checking in a baseline before that ruling
//   exists as a spec amendment: "a baseline transcribed against an unruled
//   configuration pins the wrong workload and is the one way the ladder gets
//   skipped in practice" (plan S12.3). The order this file was built in:
//     1. VoragoEngine_CpuSurvey (SC-001a) measured V, L, G and printed the
//        ladder - the ARTEFACT the ruling was taken from;
//     2. the user ruled (spec Clarifications 2026-09-19, Q-H): shipped polyphony
//        4, kMaxVoices 6, no voicing levers, and clause (ii) of SC-001b
//        REFORMULATED (see kEngineBaselineNsAtPoly4 below);
//     3. the engine at the ruled configuration was re-measured after a restart
//        and an idle, and THAT figure - not the warm-machine survey - became the
//        checked-in baseline (VoragoEngine_CpuBudget, at the end of the SC-001
//        section).
//
// RUN THESE ALONE. They assert wall-clock against audio time, so any competing
//   load - another suite, a build, clang-tidy, a parallel agent - inflates the
//   number and produces a false red on untouched code:
//       node tools/run-cpu-tests.js dsp_systems_tests
//   A CPU budget is a functional requirement: NEVER relax one and NEVER shrink
//   a workload to make it pass. Re-voice, reduce cost, or stop and surface the
//   measurement. The lever ladder is plan S12.4, in order: L-7 (lower
//   kMaxVoices), L-4 (per-voice counts), cheaper material voicing, L-3 (one
//   body, USER RULING ONLY), L-5 (lower shipped polyphony, floored at 4),
//   L-6 (stop and surface).
//
// ALLOCATION DETECTION: this TU includes neither <allocation_detector.h> nor
//   <allocation_operator_overrides.h>. The single owner of the global
//   operator new/delete replacements in dsp_systems_tests is
//   unit/systems/selectable_oscillator_test.cpp:388; a second include of
//   <allocation_operator_overrides.h> is a duplicate-symbol link error.
//
// PORTABILITY: no std::isnan / std::isinf / std::isfinite anywhere in this TU,
//   so it stays correct under -ffast-math. Finiteness is read off the IEEE-754
//   exponent field instead (isFiniteValue below).
//
// COMPILE FLAGS: none, deliberately. This TU is absent from BOTH the
//   -fno-fast-math list and the -O2 cap list in dsp/tests/CMakeLists.txt (:516
//   lists it in the plain enumerated source list) - either flag would change the
//   figures the whole ruling is taken from.
//
// FTZ/DAZ: dsp/tests/dsp_test_main.cpp calls enableFTZDAZ() before any case
//   runs, so every figure below is measured with denormals flushed BY THE
//   PROCESS - the environment the audio thread runs in.
//
// WHY THE ENGINE AND VOICE ARMS USE kFastAttackEnvelopeConfig. The shipped
//   envelope is a 20 s attack followed by 30 / 45 / 60 s body stages
//   (vorago_voice.h:321-323). A best-of-25 x 500-block trial after 400 warm-up
//   blocks spans ~4.3 s of audio, so with the shipped envelope every figure
//   would be measured while the excitation bus is still ramping in and the
//   bodies, the ecology and the resonance network have not reached their steady
//   state. The fixture changes stage TIMES only and leaves FR-014's stage LEVELS
//   identical (vorago_fixtures.h:611-619 pins that at compile time), so it moves
//   no per-sample arithmetic - it only gets the chain to the steady state that a
//   CPU figure is supposed to describe.
// ==============================================================================

#include <catch2/catch_all.hpp>

// --- the subjects ------------------------------------------------------------
#include <krate/dsp/systems/vorago_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>
#include <krate/dsp/systems/vorago_voice.h>

// --- the SC-002 standalone stages, named explicitly rather than relied on
//     transitively through vorago_voice.h / vorago_engine.h -------------------
#include <krate/dsp/core/env_curve.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/multi_stage_envelope.h>
#include <krate/dsp/processors/spectral_smear.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/atmosphere_engine.h>
#include <krate/dsp/systems/bloom_engine.h>
#include <krate/dsp/systems/continuous_body.h>
#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/feedback_ecology.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/noise_organism.h>
#include <krate/dsp/systems/resonance_drift_network.h>
#include <krate/dsp/systems/subharmonic_engine.h>

#include <vorago_fixtures.h>

#include "vorago_perf_budget.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using Krate::DSP::AtmosphereEngine;
using Krate::DSP::Biquad;
using Krate::DSP::BiquadCoefficients;
using Krate::DSP::BloomEngine;
using Krate::DSP::ContinuousBody;
using Krate::DSP::deriveStreamSeed;
using Krate::DSP::EcosystemEngine;
using Krate::DSP::FeedbackEcology;
using Krate::DSP::HarmonicCloud;
using Krate::DSP::LinearRamp;
using Krate::DSP::MultiStageEnvelope;
using Krate::DSP::NoiseOrganism;
using Krate::DSP::NoiseOrganismModel;
using Krate::DSP::ResonanceDriftNetwork;
using Krate::DSP::RetriggerMode;
using Krate::DSP::SpectralSmear;
using Krate::DSP::SubharmonicEngine;
using Krate::DSP::TapeSaturator;
using Krate::DSP::TruePeakLimiter;
using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoEngineConfig;
using Krate::DSP::VoragoMacroMatrix;
using Krate::DSP::VoragoVoice;
using Krate::DSP::VoragoVoiceConfig;

using Krate::DSP::TestUtils::Vorago::applyFastAttack;
using Krate::DSP::TestUtils::Vorago::kFastAttackEnvelopeConfig;
using Krate::DSP::TestUtils::Vorago::makeEngine;

namespace {

// =============================================================================
// Measurement basis (plan S12.1), inherited VERBATIM from
// dsp/tests/unit/effects/cavern_verb_perf_test.cpp:105-125 - with ONE constant
// changed, and deliberately: kReferenceNs is roadmap line 470's THIRTY percent
// global instrument budget, not Phase 9's five percent for one reverb.
// =============================================================================

using Krate::DSP::TestUtils::Vorago::kSr48;
constexpr double kSr192 = 192000.0;
using Krate::DSP::TestUtils::Vorago::kBlockSize;

/// Wall-clock budget of one 512-sample block at 48 kHz, in nanoseconds.
using Krate::DSP::TestUtils::Vorago::kBlockBudgetNs;

/// The relative regression bound the checked-in baseline carries (SC-001b's
/// clause (ii)). The LADDER is read against the gated line, not only the
/// reference, which is why it is stated before the reference.
constexpr double kRegressionFactor = 1.5;

/// roadmap line 470's global ceiling: 30 % of one core, 3 200 000 ns/block.
using Krate::DSP::TestUtils::Vorago::kReferenceNs;

/// The baseline at which the 1.5x regression line and the ceiling coincide:
/// 2 133 333.3 ns/block. SC-001b's ORIGINAL clause (ii) required the baseline
/// to sit at or below this; the ruled configuration does not (see
/// kEngineBaselineNsAtPoly4) and the clause was reformulated by ruling. Kept
/// because the survey prints every figure against it: it is the line a
/// baseline would have to reach for the full 1.5x headroom to exist.
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;

// --- Structural clauses: what these numbers describe -------------------------
// If any of these moves, the measurement no longer describes the configuration
// SC-001a / SC-002 specified, and the TU stops compiling rather than silently
// reporting a figure for a different one.

static_assert(VoragoVoice::kControlChunkSamples == 64,
              "FR-007: every arm here renders on the 64-sample absolute control grid");
static_assert(VoragoEngine::kControlChunkSamples == VoragoVoice::kControlChunkSamples,
              "the engine and the voice must share one control grid or the per-stage arms are "
              "not measuring the cadence the voice runs");
static_assert(kBlockSize % VoragoVoice::kControlChunkSamples == 0u,
              "the measured block must be a whole number of control chunks");
static_assert(VoragoEngine::kMaxVoices == 6u,
              "Q-A ruling 2026-09-19 (spec Clarifications): kMaxVoices lowered 8 -> 6 after the "
              "SC-001a survey measured the idle-slot cost L at ~12 700 ns (0.4 % of the "
              "reference per slot). The survey now sweeps {1, 2, 4, 6}; polyphony 8 is above "
              "the ceiling and setPolyphony clamps it");
static_assert(VoragoEngine::kDefaultPolyphony == 4u,
              "S12.3's shipped recommendation; L-5's floor is roadmap line 460's 4");

constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;
constexpr std::size_t kChunksPerBlock = kBlockSize / kChunk;

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
using Krate::DSP::TestUtils::Vorago::kCavernMeasuredNsPerBlock;

/// The CHECKED-IN Phase 9 baseline for the same arm, ceil(measured x 1.05)
/// (cavern_verb_perf_test.cpp:255, compliance.md:85). Printed alongside the
/// measured figure so the ladder can be read either way: the measured number is
/// what the machine does, the baseline is what Phase 9 gates at.
constexpr double kCavernBaselineNsPerBlock = 129150.0;

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
using Krate::DSP::TestUtils::Vorago::kEngineMeasuredNsAtPoly4;
using Krate::DSP::TestUtils::Vorago::kEngineBaselineNsAtPoly4;
constexpr double kBaselineWithCavernNs = kEngineBaselineNsAtPoly4 + kCavernMeasuredNsPerBlock;
constexpr double kAvailableRegressionHeadroom =
    (kReferenceNs - kCavernMeasuredNsPerBlock) / kEngineBaselineNsAtPoly4;

// FR-083: the baseline IS ceil(measured x 1.05), and nothing else.
static_assert(kEngineBaselineNsAtPoly4 >= kEngineMeasuredNsAtPoly4 * 1.05 &&
                  kEngineBaselineNsAtPoly4 < (kEngineMeasuredNsAtPoly4 * 1.05) + 1.0,
              "FR-083: the checked-in baseline must be ceil(measured x 1.05) of the transcribed "
              "measurement - re-measure, do not hand-edit either number");
// FR-083: a baseline above the ceiling means the phase is over budget and
// FR-081's ladder applies - it may not be checked in.
static_assert(kBaselineWithCavernNs <= kReferenceNs,
              "SC-001b / FR-083: the checked-in baseline plus the Cavern term is above roadmap "
              "line 470's 30 % ceiling; the phase is over budget and FR-081's ladder applies");
// The reformulated clause (ii) is only meaningful while the ceiling is the
// tighter of the two lines. If the available headroom ever reaches 1.5x the
// original clause is met again and this note - not the gate - is what changes.
static_assert(kAvailableRegressionHeadroom > 1.0,
              "the baseline must sit strictly below the ceiling for any regression headroom to "
              "exist at all");

// =============================================================================
// Trial shape (plan S12.1)
// =============================================================================
// Best-of-25 x 500 blocks after 400 warm-up blocks, P-core-pinned.
//
// MANY SHORT TRIALS, for the reason cavern_verb_perf_test.cpp:315-327 states:
// the dominant noise source on a hybrid CPU is not jitter smeared across a
// trial, it is the WHOLE TRIAL being migrated onto an E-core - a ~20 % step that
// best-of-N cannot reject when N is small and each trial is long enough to be
// migrated. The minimum of 25 short trials is the least contaminated estimate of
// the real cost, which is what a budget wants.

constexpr int kTrials = 25;
constexpr int kBlocksPerTrial = 500;   ///< ~5.3 s of audio per trial
constexpr int kWarmupBlocks = 400;     ///< ~4.3 s: past every smoother in the chain

/// The stage probe measures fifteen arms; 25 x 500 on each would put the case
/// well past the lane's wall clock for no extra resolution on a BREAKDOWN that
/// gates nothing. The arms that FEED A GATE - V (the voice) and the nine sum
/// terms SC-003 reads - are measured at the full shape in
/// VoragoVoice_CompositionOverhead, which is the case that gates.
constexpr int kStageTrials = 12;
constexpr int kStageBlocksPerTrial = 200;
constexpr int kStageWarmupBlocks = 300;

/// The clearing paths are single CALLS, not blocks. One call is far too short to
/// time directly (a steady_clock tick is tens of nanoseconds), so each trial
/// times a run of calls and divides. The paths have no early-out - every one of
/// them is an unconditional walk over the same fills - so a repeated call costs
/// what the first one costs.
constexpr int kClearTrials = 25;
constexpr int kClearCallsPerTrial = 100;

/// One 64-sample control chunk, in nanoseconds, at the rate named.
/// 1 333 333.3 ns at 48 kHz and 333 333.3 ns at 192 kHz (B-7's clause (b)).
[[nodiscard]] constexpr double controlChunkNs(double sampleRate) noexcept {
    return (static_cast<double>(kChunk) / sampleRate) * 1.0e9;
}

/// Finite check WITHOUT std::isnan (FR-071): the macOS leg builds with
/// -ffast-math, which folds the classifiers. Inspect the IEEE-754 exponent
/// field instead.
[[nodiscard]] bool isFiniteValue(float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

/// Best-of-N driver. `runBlock` performs exactly one 512-sample block of work.
/// Taken by const reference, not by forwarding reference: it is INVOKED, many
/// times, never consumed, so there is nothing to forward.
template <typename BlockFn>
[[nodiscard]] double bestNsPerBlock(int trials, int blocksPerTrial, const BlockFn& runBlock) {
    double best = std::numeric_limits<double>::max();
    for (int trial = 0; trial < trials; ++trial) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocksPerTrial; ++i) {
            runBlock();
        }
        const auto end = std::chrono::steady_clock::now();
        const double elapsedNs = std::chrono::duration<double, std::nano>(end - start).count();
        best = std::min(best, elapsedNs / static_cast<double>(blocksPerTrial));
    }
    return best;
}

/// Warm-up then best-of-N, the shape every block-measured arm uses.
template <typename BlockFn>
[[nodiscard]] double warmThenMeasure(int warmupBlocks, int trials, int blocksPerTrial,
                                     const BlockFn& runBlock) {
    for (int i = 0; i < warmupBlocks; ++i) {
        runBlock();
    }
    return bestNsPerBlock(trials, blocksPerTrial, runBlock);
}

// =============================================================================
// Buffers and deterministic excitation
// =============================================================================

/// One block of stereo scratch plus a mono lane. Separate arrays: the perf lane
/// never relies on in-place support (ContinuousBody explicitly forbids it,
/// continuous_body.h:1609-1610).
struct Buffers {
    std::array<float, kBlockSize> inLeft{};
    std::array<float, kBlockSize> inRight{};
    std::array<float, kBlockSize> outLeft{};
    std::array<float, kBlockSize> outRight{};
    std::array<float, kBlockSize> scratchLeft{};
    std::array<float, kBlockSize> scratchRight{};
    std::array<float, kBlockSize> mono{};
};

/// Deterministic decorrelated stereo noise at ~-12 dBFS, built once and replayed
/// every block. Only the stages that TAKE audio use it (the bodies, the
/// resonance network, the ecology, the atmosphere, the subharmonic, the smear
/// and the output stage); the cloud, the noise organism, the bloom, the
/// ecosystem, the voice and the engine are generators.
///
/// Xorshift32 rather than <random> so the sequence is identical on every
/// toolchain (std::uniform_real_distribution is not portable).
void fillExcitation(Buffers& buf) noexcept {
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
// The SC-001a / SC-002 subject configuration
// =============================================================================
// EVERY ARM BELOW IS PINNED TO THE SHIPPED FR-090 TABLE, reproduced from
// VoragoVoice::prepare() step 5 (vorago_voice.h:547-650) rather than inherited,
// because the sub-components are built STANDALONE here and their own prepare()
// restores THEIR defaults, not Vorago's. A stage measured at its component
// default is a figure for a configuration nobody ships.
//
// The seed of every standalone stage is derived with the SAME salt the voice
// uses (vorago_voice.h:382-395, via core/random.h's deriveStreamSeed), so a
// standalone arm and the corresponding stage inside the voice run the same
// stream and therefore the same amount of work.

/// The one engine seed every arm in this TU derives from.
constexpr std::uint32_t kSeed = 1u;

/// A low drone note. 55 Hz is inside all three of the FR-013 frequency clamps
/// (ContinuousBody [20, 8000], HarmonicCloud [20, 4000], ResonanceDriftNetwork
/// floors at 8 - vorago_voice.h:868-873), so no stage is silently retuned.
constexpr float kNoteHz = 55.0f;

/// MIDI A1 = 33, the note number whose pitch is kNoteHz.
constexpr std::uint8_t kMidiNote = 33u;

/// The shipped voice configuration. Defaults throughout: VoragoVoiceConfig's own
/// field initialisers ARE FR-090's per-voice counts (vorago_voice.h:187-212),
/// and L-4 - which would lower them - is a lever the ladder has not reached.
[[nodiscard]] VoragoVoiceConfig shippedVoiceConfig() noexcept {
    return VoragoVoiceConfig{};
}

/// The shipped engine configuration.
[[nodiscard]] VoragoEngineConfig shippedEngineConfig() noexcept {
    VoragoEngineConfig cfg{};
    cfg.voice = shippedVoiceConfig();
    return cfg;
}

// -----------------------------------------------------------------------------
// The standalone voice-column stages
// -----------------------------------------------------------------------------

/// FR-090's harmonic cloud (vorago_voice.h:552-561), noted on at kNoteHz.
[[nodiscard]] std::unique_ptr<HarmonicCloud> buildCloud() {
    auto cloud = std::make_unique<HarmonicCloud>();
    cloud->prepare(kSr48);
    cloud->setSeed(deriveStreamSeed(kSeed, VoragoVoice::kCloudSalt));
    cloud->setRichness(0.70f);
    cloud->setSpectralTiltDb(-4.0f);
    cloud->setMutation(0.15f);
    cloud->setInharmonicity(0.015f);
    cloud->setSpectralGravity(0.10f);
    cloud->setDriftDepthCents(8.0f);
    cloud->setStereoSpread(0.45f);
    cloud->setAttackTimeSec(0.05f);
    cloud->setDecayTimeSec(8.0f);
    cloud->setFundamentalHz(kNoteHz);
    cloud->noteOn();
    return cloud;
}

/// FR-090's noise organism (vorago_voice.h:563-573). The wakes are written once
/// at their FR-021 BASE: publishIdentity() writes
/// combineWake(base, eco, sched) every control step and combineWake(b, 0, 0) is
/// exactly `b` (vorago_voice.h:976-979), so a standalone arm at the base is the
/// arm the voice runs whenever the ecosystem is not addressing that slot.
[[nodiscard]] std::unique_ptr<NoiseOrganism> buildNoise() {
    auto noise = std::make_unique<NoiseOrganism>();
    const VoragoVoiceConfig cfg = shippedVoiceConfig();
    noise->prepare(kSr48, NoiseOrganism::PrepareConfig{.maxBlockSamples = kBlockSize,
                                                       .maxCombDelayMs = cfg.maxCombDelayMs,
                                                       .numSources = cfg.numNoiseSources});
    noise->setSeed(deriveStreamSeed(kSeed, VoragoVoice::kNoiseSalt));
    noise->setNumSources(cfg.numNoiseSources);
    noise->setSourceModel(0, NoiseOrganismModel::FilteredWind);
    noise->setSourceModel(1, NoiseOrganismModel::GranularDust);
    noise->setSourceModel(2, NoiseOrganismModel::Direct);
    noise->setSourceModel(3, NoiseOrganismModel::MetallicHiss);
    for (std::size_t s = 0; s < cfg.numNoiseSources; ++s) {
        noise->setSourceLevel(s, -18.0f);
        noise->setSourceWake(s, 0.35f);
    }
    noise->setWanderRate(0.03f);
    return noise;
}

/// FR-090's resonance network (vorago_voice.h:575-589).
[[nodiscard]] std::unique_ptr<ResonanceDriftNetwork> buildResonance() {
    auto res = std::make_unique<ResonanceDriftNetwork>();
    const VoragoVoiceConfig cfg = shippedVoiceConfig();
    res->prepare(kSr48, ResonanceDriftNetwork::PrepareConfig{.maxBlockSamples = kBlockSize,
                                                             .numPeaks = cfg.numResonancePeaks});
    res->setSeed(deriveStreamSeed(kSeed, VoragoVoice::kResonanceSalt));
    res->setAnchorMode(ResonanceDriftNetwork::AnchorMode::Hybrid);
    res->setNumPeaks(cfg.numResonancePeaks);
    res->setGravity(0.0f);
    res->setMix(0.45f);
    res->setWetGain(ResonanceDriftNetwork::kDefaultWetGainDb);
    for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
        res->setPeakLevel(p, -9.0f);
        res->setFreqWander(p, 1.5f);
    }
    res->setWanderRate(0.03f);
    for (std::size_t p = 0; p < cfg.numResonancePeaks; ++p) {
        res->setPeakWake(p, 0.50f);
    }
    res->setNoteFrequency(kNoteHz);
    return res;
}

/// FR-090's feedback ecology (vorago_voice.h:591-603).
[[nodiscard]] std::unique_ptr<FeedbackEcology> buildEcology(double sampleRate) {
    auto eco = std::make_unique<FeedbackEcology>();
    const VoragoVoiceConfig cfg = shippedVoiceConfig();
    eco->prepare(sampleRate, FeedbackEcology::PrepareConfig{.maxBlockSamples = kBlockSize,
                                                            .numLoops = cfg.numEcologyLoops});
    eco->setSeed(deriveStreamSeed(kSeed, VoragoVoice::kEcologySalt));
    eco->setNumLoops(cfg.numEcologyLoops);
    eco->setMix(FeedbackEcology::kDefaultMix);
    for (std::size_t l = 0; l < cfg.numEcologyLoops; ++l) {
        eco->setLoopGain(l, FeedbackEcology::kDefaultLoopGain);
        eco->setCoupling(l, (l + 1u) % cfg.numEcologyLoops, 0.12f);
        eco->setLoopWake(l, 0.50f);
    }
    return eco;
}

/// FR-090's bloom engine (vorago_voice.h:605-616).
[[nodiscard]] std::unique_ptr<BloomEngine> buildBloom() {
    auto bloom = std::make_unique<BloomEngine>();
    const VoragoVoiceConfig cfg = shippedVoiceConfig();
    bloom->prepare(kSr48, BloomEngine::PrepareConfig{.capacity = VoragoVoice::kMinCloudCapacity,
                                                     .numChildSlots = cfg.bloomChildSlots});
    bloom->setSeed(deriveStreamSeed(kSeed, VoragoVoice::kBloomSalt));
    bloom->setDepth(0.60f);
    bloom->setSpawnRateHz(BloomEngine::kDefaultSpawnRateHz);
    bloom->setFadeInSeconds(45.0f);
    bloom->setHoldSeconds(120.0f);
    bloom->setFadeOutSeconds(180.0f);
    bloom->setConsumerTiltDb(-4.0f);
    return bloom;
}

/// FR-090's ecosystem (vorago_voice.h:520-526).
[[nodiscard]] std::unique_ptr<EcosystemEngine> buildEcosystem() {
    auto eco = std::make_unique<EcosystemEngine>();
    const VoragoVoiceConfig cfg = shippedVoiceConfig();
    eco->prepare(kSr48, EcosystemEngine::PrepareConfig{.agentCount = cfg.ecosystemAgents,
                                                       .resourceCells = cfg.ecosystemCells,
                                                       .energyBudget = 1.0,
                                                       .initialPoolFraction = 0.5,
                                                       .stepIntervalChunks
                                                       = cfg.ecosystemStepChunks});
    eco->setSeed(deriveStreamSeed(kSeed, VoragoVoice::kEcosystemSalt));
    return eco;
}

/// FR-090's body A (StoneChamber) or body B (SteelTank), vorago_voice.h:631-643.
/// setMaterial precedes prepare() for the reason continuous_body_perf_test.cpp
/// states: the material selects the mode set the geometry is built from.
[[nodiscard]] std::unique_ptr<ContinuousBody> buildBody(ContinuousBody::BodyMaterial material,
                                                        std::size_t salt) {
    auto body = std::make_unique<ContinuousBody>();
    body->setMaterial(material);
    body->prepare(kSr48);
    body->setSeed(deriveStreamSeed(kSeed, salt));
    body->setMaterial(material);
    body->setResonance(ContinuousBody::kDefaultResonance);
    body->setDamping(0.25f);
    body->setMix(ContinuousBody::kDefaultMix);
    body->setCloudMix(ContinuousBody::kDefaultCloudMix);
    body->setCloudDecaySec(20.0f);
    body->setWidth(ContinuousBody::kDefaultWidth);
    body->setNoteFrequencyHz(kNoteHz);
    return body;
}

/// The voice's envelope, configured through the SAME ceilings, stage count and
/// retrigger mode prepare() installs (vorago_voice.h:652-680) and then given
/// kFastAttackEnvelopeConfig's TIMES - the substitution the voice and engine
/// arms make, so the sum and the whole describe the same envelope.
[[nodiscard]] std::unique_ptr<MultiStageEnvelope> buildEnvelope() {
    auto mse = std::make_unique<MultiStageEnvelope>();
    mse->prepare(static_cast<float>(kSr48));
    mse->setMaxStageTimeMs(VoragoVoice::kEnvelopeMaxStageTimeMs);
    mse->setNumStages(VoragoVoice::kEnvelopeStages);
    mse->setSustainPoint(VoragoVoice::kEnvelopeSustainPoint);
    for (int st = 0; st < VoragoVoice::kEnvelopeStages; ++st) {
        const auto i = static_cast<std::size_t>(st);
        mse->setStage(st, kFastAttackEnvelopeConfig.stages[i].level,
                      kFastAttackEnvelopeConfig.stages[i].ms, VoragoVoice::kStageCurve);
    }
    mse->setReleaseTime(kFastAttackEnvelopeConfig.releaseMs);
    mse->setRetriggerMode(RetriggerMode::Legato);
    mse->gate(true);
    return mse;
}

// -----------------------------------------------------------------------------
// The standalone engine-column stages
// -----------------------------------------------------------------------------

/// The GLOBAL ghost tap at the seven FR-017 values prepare() installs
/// (vorago_engine.h:290-303).
///
/// ONE DELIBERATE DIVERGENCE: setLevel(kGhostBurstPeak) instead of the
/// prepare-time 0.0. The engine's control step writes
/// `ghostPeak_ * max(getGhostRequest())` every chunk and a ghost is an EVENT on a
/// 20-90 s scheduler, so a level-0 arm would measure the idle path and report a
/// global cost the instrument does not pay when a ghost fires. This arm is
/// therefore the ghost's ACTIVE cost - the worst case, which is the one a budget
/// wants - and VoragoEngine_CpuSurvey's measured totals see the same component
/// mostly idle. Both numbers are printed and both are labelled.
[[nodiscard]] std::unique_ptr<AtmosphereEngine> buildAtmosphere() {
    auto atmos = std::make_unique<AtmosphereEngine>();
    const VoragoEngineConfig cfg = shippedEngineConfig();
    atmos->prepare(kSr48, AtmosphereEngine::PrepareConfig{
                              .captureSeconds = cfg.atmosCaptureSeconds,
                              .blurEnabled = cfg.atmosBlurEnabled,
                              .freezeEnabled = cfg.atmosFreezeEnabled,
                              .blurFftSize = cfg.atmosBlurFftSize,
                              .freezeFftSize = cfg.atmosFreezeFftSize,
                              .maxBlockSamples = cfg.maxBlockSamples});
    atmos->setSeed(deriveStreamSeed(kSeed, VoragoEngine::kAtmosSalt));
    atmos->setDensity(0.30f);
    atmos->setGrainSeconds(12.0f);
    atmos->setPitchSemitones(-12.0f);
    atmos->setPositionSpread(0.90f);
    // 0.85f, roadmap line 114's darker blur default. The literal is reproduced
    // rather than referenced because VoragoEngine::kDefaultAtmosBlur is PRIVATE
    // (vorago_engine.h:1005 opens the private section at :1021-1024) - it is an
    // engine-owned field, not part of the component contract.
    atmos->setBlur(0.85f);
    atmos->setDecorrelation(0.85f);
    atmos->setLevel(VoragoEngine::kGhostBurstPeak);
    return atmos;
}

/// The held-fundamental sub tail (vorago_engine.h:307-313). prepare() installs
/// the component's own FR-020 tone table; the engine then writes the tracking
/// amount, and the control step writes the fundamental from the lowest sounding
/// voice - which is what kNoteHz stands in for here.
[[nodiscard]] std::unique_ptr<SubharmonicEngine> buildSubharmonic() {
    auto sub = std::make_unique<SubharmonicEngine>();
    const VoragoEngineConfig cfg = shippedEngineConfig();
    sub->prepare(kSr48, SubharmonicEngine::PrepareConfig{.maxBlockSamples = cfg.maxBlockSamples});
    // 1.0f - vorago_engine.h's kDefaultSubTracking (ruled 2026-09-19: 0.60 ->
    // 1.0), reproduced because that constant is private (the engine-owned
    // default table).
    sub->setTrackingAmount(1.0f);
    sub->setFundamentalHz(kNoteHz);
    return sub;
}

/// The global fog (vorago_engine.h:315-323). The setters precede prepare()
/// exactly as the engine's do, so its step 9 SNAPS the control smoothers to them
/// instead of ramping in.
[[nodiscard]] std::unique_ptr<SpectralSmear> buildSmear() {
    auto smear = std::make_unique<SpectralSmear>();
    const VoragoEngineConfig cfg = shippedEngineConfig();
    // 0.20f / 0.20f - vorago_engine.h:1021-1022, reproduced for the same reason:
    // the engine-owned default table is private.
    smear->setSmearAmount(0.20f);
    smear->setDecoherence(0.20f);
    smear->setSmearTilt(0.0f);
    smear->prepare(kSr48, SpectralSmear::PrepareConfig{.fftSize = cfg.smearFftSize,
                                                       .enabled = cfg.smearEnabled});
    return smear;
}

/// The FR-053 output stage: two mono saturators on the 64-sample cadence and the
/// true-peak limiter over the whole block (vorago_engine.h:922-932).
struct OutputStage {
    TapeSaturator satL;
    TapeSaturator satR;
    TruePeakLimiter limiter;
};

[[nodiscard]] std::unique_ptr<OutputStage> buildOutputStage() {
    auto out = std::make_unique<OutputStage>();
    out->satL.setDrive(VoragoEngine::kOutputDriveDb);
    out->satR.setDrive(VoragoEngine::kOutputDriveDb);
    out->satL.setSaturation(VoragoEngine::kOutputSaturation);
    out->satR.setSaturation(VoragoEngine::kOutputSaturation);
    out->satL.setMix(1.0f);
    out->satR.setMix(1.0f);
    out->satL.prepare(kSr48, kChunk);
    out->satR.prepare(kSr48, kChunk);
    out->limiter.setCeilingDb(VoragoEngine::kOutputCeilingDb);
    out->limiter.prepare(kSr48, VoragoEngine::kMaxBlockSamples);
    return out;
}

// -----------------------------------------------------------------------------
// The composed subjects
// -----------------------------------------------------------------------------

/// One prepared, noted-on VoragoVoice at the shipped configuration with the
/// FR-014a fast attack. HEAP-HELD: VoragoVoice is ~124 KB (kVoiceSizeBound) and
/// a stack local is a defect, not a style preference (vorago_voice.h:2085-2087).
[[nodiscard]] std::unique_ptr<VoragoVoice> buildVoice(double sampleRate, bool sounding) {
    auto voice = std::make_unique<VoragoVoice>();
    voice->setSeed(deriveStreamSeed(kSeed, VoragoEngine::kVoiceSaltBase));
    voice->prepare(sampleRate, shippedVoiceConfig());
    applyFastAttack(*voice);
    if (sounding) {
        voice->noteOn(kNoteHz, 1.0f);
    }
    return voice;
}

/// A prepared engine at polyphony `n` with `n` notes sounding, macros at their
/// FR-061 neutral and the FR-014a fast attack installed on every slot.
///
/// ORDER MATTERS. setPolyphony() first, because VoragoMacroMatrix::apply()
/// iterates `i < getPolyphony()` (vorago_macro_matrix.h:923). The matrix next,
/// because its neutral row bases are the FR-090 table. applyFastAttack() LAST,
/// because it is the one substitution this TU makes and nothing may overwrite
/// it. Notes last of all.
[[nodiscard]] std::unique_ptr<VoragoEngine> buildEngineAtPolyphony(std::size_t n) {
    auto engine = makeEngine(kSr48, shippedEngineConfig());
    engine->setSeed(kSeed);
    engine->setPolyphony(n);

    // A DEFAULT-CONSTRUCTED VoragoMacroValues IS the FR-061 neutral - eleven
    // zeros and Gravity at its bipolar 0.5 (vorago_macro_matrix.h:199-210) - so
    // the matrix is applied as constructed rather than with a hand-written
    // neutral literal that could drift from it.
    const VoragoMacroMatrix matrix;
    matrix.apply(*engine);

    applyFastAttack(*engine);
    for (std::size_t v = 0; v < n; ++v) {
        engine->noteOn(static_cast<std::uint8_t>(kMidiNote + v), static_cast<std::uint8_t>(100));
    }
    return engine;
}

// =============================================================================
// Reporting
// =============================================================================

/// One measured figure and what it is.
struct StageFigure {
    std::string name;
    double nsPerBlock = 0.0;
    /// True when this figure is one of the terms SC-003's sum is built from.
    /// The informational rows (the ecosystem, which is already inside L; the
    /// engine column, which is G and not part of a VOICE) are false.
    bool inVoiceSum = false;
};

[[nodiscard]] std::string pct(double value, double whole) {
    std::ostringstream os;
    os << ((whole > 0.0) ? ((value / whole) * 100.0) : 0.0) << " %";
    return os.str();
}

/// The FR-081 ladder for one (kMaxVoices, N) pair, against B-6's CORRECTED solve
///     N*V + (kMaxVoices - N)*L + G <= budget
/// Every spare slot costs L on every block because the render loop bound is
/// unconditional (vorago_engine.h:833-838), which is what makes kMaxVoices a
/// BUDGET number and not a free ceiling.
[[nodiscard]] double ladderCost(std::size_t maxVoices, std::size_t n, double v, double l,
                                double g) noexcept {
    const std::size_t sounding = std::min(n, maxVoices);
    const double spare = static_cast<double>(maxVoices - sounding);
    return (static_cast<double>(sounding) * v) + (spare * l) + g;
}

/// The largest N in [1, maxVoices] whose ladder cost fits `budget`; 0 when even
/// N = 1 does not fit.
[[nodiscard]] std::size_t largestFittingN(std::size_t maxVoices, double v, double l, double g,
                                          double budget) noexcept {
    std::size_t best = 0;
    for (std::size_t n = 1; n <= maxVoices; ++n) {
        if (ladderCost(maxVoices, n, v, l, g) <= budget) {
            best = n;
        }
    }
    return best;
}

}  // namespace

// =============================================================================
// SC-001a - the polyphony survey. MEASURES AND REPORTS; GATES NOTHING.
// =============================================================================
// This case is the ARTEFACT the OQ-1(a) / Q-A ruling is taken from: the shipped
// polyphony AND the value of kMaxVoices. It asserts finite-and-positive and
// nothing else (spec SC-001a, FR-082).
//
// WHAT IS MEASURED DIRECTLY: the engine at polyphony {1, 2, 4, 6}, one
// standalone VoragoVoice (V) and one standalone life-only advance (L). G - the
// global chain - is DERIVED from those three, because no engine configuration
// can be rendered with the global chain removed:
//
//     total(N) = N*V' + (kMaxVoices - N)*L' + G
//   =>       G = total(N) - N*V - (kMaxVoices - N)*L
//
// where V' and L' are the IN-SITU costs and V, L are the standalone ones. The
// two differ by exactly SC-003's composition overhead, so the derived G carries
// that overhead with the opposite sign and is a LOWER bound on the true global
// cost. Every swept N yields its own estimate and all five are printed: if they
// disagree badly, the solve's shape - not the arithmetic - is what is wrong, and
// that is worth seeing. T022 cross-checks the whole sum once against the real
// composed chain.
//
// THE PER-STAGE BREAKDOWN IS NOT REPRINTED HERE. VoragoVoice_StageCostProbe is
// in THIS TU and runs in THE SAME invocation, so its fifteen-row table is
// already in the same log; measuring the fifteen arms twice would double an
// ~11-minute lane for no new number. What this case prints is the breakdown the
// LADDER consumes - V, L, G and the per-polyphony split - which is the part
// SC-001a's own text names.
TEST_CASE("VoragoEngine_CpuSurvey", "[systems][vorago][.perf]") {
    // -------------------------------------------------------------------------
    // V: one VoragoVoice, standalone, sounding.
    // -------------------------------------------------------------------------
    auto voice = buildVoice(kSr48, /*sounding=*/true);
    Buffers voiceBuf{};
    double voiceSink = 0.0;
    const double v = warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
        voice->processStereoBlock(voiceBuf.outLeft.data(), voiceBuf.outRight.data(), kBlockSize);
        voiceSink += static_cast<double>(voiceBuf.outLeft[0])
                     + static_cast<double>(voiceBuf.outRight[kBlockSize - 1]);
    });

    // -------------------------------------------------------------------------
    // L: one VoragoVoice advanced life-only. B-6's spare-slot term.
    // -------------------------------------------------------------------------
    auto lifeVoice = buildVoice(kSr48, /*sounding=*/false);
    const double l = warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial,
                                     [&]() noexcept { lifeVoice->advanceLifeOnly(kBlockSize); });

    // -------------------------------------------------------------------------
    // The polyphony sweep.
    // -------------------------------------------------------------------------
    constexpr std::array<std::size_t, 4> kPolyphonies{1u, 2u, 4u, 6u};  // Q-A: kMaxVoices = 6
    std::array<double, kPolyphonies.size()> measured{};
    std::array<double, kPolyphonies.size()> derivedG{};
    double engineSink = 0.0;

    for (std::size_t k = 0; k < kPolyphonies.size(); ++k) {
        const std::size_t n = kPolyphonies[k];
        auto engine = buildEngineAtPolyphony(n);
        Buffers buf{};
        measured[k] = warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            engine->processStereoBlock(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
            engine->processOutputStage(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
            engineSink += static_cast<double>(buf.outLeft[0])
                          + static_cast<double>(buf.outRight[kBlockSize - 1]);
        });
        const double spare = static_cast<double>(VoragoEngine::kMaxVoices - n);
        derivedG[k] = measured[k] - (static_cast<double>(n) * v) - (spare * l);
    }

    // -------------------------------------------------------------------------
    // The report. EVERYTHING IS PRINTED BEFORE ANY REQUIRE: a REQUIRE aborts the
    // case, and the ruling needs the whole table including any row that failed.
    // -------------------------------------------------------------------------
    {
        std::ostringstream os;
        os << "SC-001a - VoragoEngine polyphony survey, ns per 512-sample block @ 48 kHz\n"
           << "  block budget         : " << kBlockBudgetNs << " ns\n"
           << "  reference (30 %)     : " << kReferenceNs
           << " ns/block   (roadmap line 470, GLOBAL)\n"
           << "  regression-gated line: " << kMaxAdmissibleNs << " ns/block   (reference / "
           << kRegressionFactor << ")\n"
           << "  kMaxVoices (compiled): " << VoragoEngine::kMaxVoices
           << "   - a COMPILE-TIME constant, so its cost is evaluated arithmetically below\n"
           << "  V (voice, standalone): " << v << " ns/block\n"
           << "  L (advanceLifeOnly)  : " << l << " ns/block   (B-6's spare-slot term)\n"
           << "  Cavern (arithmetic)  : " << kCavernMeasuredNsPerBlock
           << " ns/block   (measured, phase9 compliance.md:123; checked-in baseline "
           << kCavernBaselineNsPerBlock << ")";
        WARN(os.str());
    }

    for (std::size_t k = 0; k < kPolyphonies.size(); ++k) {
        const std::size_t n = kPolyphonies[k];
        const double withCavern = measured[k] + kCavernMeasuredNsPerBlock;
        std::ostringstream os;
        os << "SC-001a  polyphony " << n << "  (kMaxVoices = " << VoragoEngine::kMaxVoices << ")\n"
           << "    engine measured    : " << measured[k] << " ns/block  ("
           << pct(measured[k], kBlockBudgetNs) << " of one core)\n"
           << "    + Cavern           : " << withCavern << " ns/block  ("
           << pct(withCavern, kBlockBudgetNs) << " of one core)\n"
           << "    vs reference       : " << pct(withCavern, kReferenceNs) << " of "
           << kReferenceNs << "\n"
           << "    vs gated line      : " << pct(withCavern, kMaxAdmissibleNs) << " of "
           << kMaxAdmissibleNs << "\n"
           << "    split  N*V         : " << (static_cast<double>(n) * v) << "\n"
           << "    split  spare*L     : "
           << (static_cast<double>(VoragoEngine::kMaxVoices - n) * l) << "  ("
           << (VoragoEngine::kMaxVoices - n) << " spare slots)\n"
           << "    G derived          : " << derivedG[k]
           << "  (measured - N*V - spare*L; a LOWER bound, see the case banner)";
        WARN(os.str());
    }

    // The G the ladder is computed from: the MEDIAN of the five estimates, which
    // rejects a single contaminated sweep point without assuming any one of them
    // is canonical. All five are printed above.
    std::array<double, kPolyphonies.size()> sortedG = derivedG;
    std::sort(sortedG.begin(), sortedG.end());
    const double g = sortedG[sortedG.size() / 2u];

    // -------------------------------------------------------------------------
    // FR-081's ladder, recomputed from the MEASURED V, L and G against B-6's
    // corrected solve, for kMaxVoices in {4, 6, 8}. THIS TABLE IS THE ARTEFACT
    // Q-A's kMaxVoices half is ruled from (plan S12.2, S12.3, L-7).
    // -------------------------------------------------------------------------
    constexpr std::array<std::size_t, 3> kCeilings{4u, 6u, 8u};
    for (const std::size_t ceiling : kCeilings) {
        std::ostringstream os;
        os << "SC-001a / FR-081 ladder  -  kMaxVoices = " << ceiling
           << "   (solve: N*V + (kMaxVoices - N)*L + G + Cavern <= budget)\n"
           << "    using V = " << v << ", L = " << l << ", G = " << g
           << " (median of the five estimates), Cavern = " << kCavernMeasuredNsPerBlock << "\n";
        for (const std::size_t n : kPolyphonies) {
            if (n > ceiling) {
                continue;
            }
            const double bare = ladderCost(ceiling, n, v, l, g) + kCavernMeasuredNsPerBlock;
            // SC-003's composition overhead applied to the PER-VOICE term only,
            // which is the column FR-081's option table is stated in.
            const double gated = ladderCost(ceiling, n, v * 1.15, l, g) + kCavernMeasuredNsPerBlock;
            os << "      N = " << n << "  bare " << bare << " ns (" << pct(bare, kReferenceNs)
               << " of ref, " << pct(bare, kMaxAdmissibleNs) << " of gated)"
               << "   x1.15 " << gated << " ns (" << pct(gated, kReferenceNs) << " of ref, "
               << pct(gated, kMaxAdmissibleNs) << " of gated)\n";
        }
        const double cav = kCavernMeasuredNsPerBlock;
        os << "      N_ref  bare = " << largestFittingN(ceiling, v, l, g, kReferenceNs - cav)
           << " / x1.15 = " << largestFittingN(ceiling, v * 1.15, l, g, kReferenceNs - cav) << "\n"
           << "      N_gate bare = " << largestFittingN(ceiling, v, l, g, kMaxAdmissibleNs - cav)
           << " / x1.15 = " << largestFittingN(ceiling, v * 1.15, l, g, kMaxAdmissibleNs - cav)
           << "\n"
           << "      (0 means not even one voice fits that budget at this ceiling)";
        WARN(os.str());
    }

    // -------------------------------------------------------------------------
    // Assertions: FINITE AND POSITIVE ONLY (spec SC-001a). Nothing here gates a
    // CPU figure - VoragoEngine_CpuBudget (SC-001b) does.
    // -------------------------------------------------------------------------
    REQUIRE(isFiniteValue(static_cast<float>(voiceSink)));
    REQUIRE(isFiniteValue(static_cast<float>(engineSink)));
    REQUIRE(v > 0.0);
    REQUIRE(l > 0.0);
    for (std::size_t k = 0; k < kPolyphonies.size(); ++k) {
        REQUIRE(measured[k] > 0.0);
    }
}

// =============================================================================
// SC-001b - THE GATE. The one case in this TU that REQUIREs a CPU figure.
// =============================================================================
// Written after the Q-H ruling and the cooled re-measurement (FR-083; the
// provenance block above kEngineBaselineNsAtPoly4 has the numbers and the
// protocol). It renders the engine at the ruled shipped configuration through
// the SAME arm the survey sweeps, so the gated figure and the survey's
// polyphony-4 row are the same measurement, and gates it twice:
//   (i)  against roadmap line 470's ceiling, with the Cavern term added
//        arithmetically (AR-1: the reverb is outside this engine);
//   (ii) against the checked-in baseline x kRegressionFactor.
// EVERYTHING IS PRINTED BEFORE ANY REQUIRE so a red run still leaves the
// figure, the baseline and both lines in the log.
//
// A RED HERE IS NOT A THRESHOLD TO MOVE. Confirm nothing else was running,
// re-run alone after the machine has idled, and only then treat it as a
// regression: the lever ladder is plan S12.4 (banner above), and lowering the
// baseline is a re-measurement under FR-083's protocol, never an edit.
TEST_CASE("VoragoEngine_CpuBudget", "[systems][vorago][.perf]") {
    static_assert(VoragoEngine::kDefaultPolyphony == 4u,
                  "SC-001b is ruled at shipped polyphony 4 (Q-H); a different shipped polyphony "
                  "is a new ruling and a new baseline, not a silent re-pin");

    auto engine = buildEngineAtPolyphony(VoragoEngine::kDefaultPolyphony);
    Buffers buf{};
    double sink = 0.0;
    const double engineNs = warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
        engine->processStereoBlock(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
        engine->processOutputStage(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
        sink += static_cast<double>(buf.outLeft[0]) + static_cast<double>(buf.outRight[kBlockSize - 1]);
    });

    const double withCavern = engineNs + kCavernMeasuredNsPerBlock;
    const double regressionLineNs = kEngineBaselineNsAtPoly4 * kRegressionFactor;

    {
        std::ostringstream os;
        os << "SC-001b - VoragoEngine CPU gate, ns per 512-sample block @ 48 kHz\n"
           << "  configuration        : polyphony " << VoragoEngine::kDefaultPolyphony
           << " of kMaxVoices " << VoragoEngine::kMaxVoices
           << ", FR-090 defaults, macros neutral, no voicing levers (Q-H)\n"
           << "  engine measured      : " << engineNs << " ns/block  ("
           << pct(engineNs, kBlockBudgetNs) << " of one core)\n"
           << "  + Cavern (arithmetic): " << withCavern << " ns/block  ("
           << pct(withCavern, kBlockBudgetNs) << " of one core)\n"
           << "  clause (i)  ceiling  : " << withCavern << " <= " << kReferenceNs << "  ("
           << pct(withCavern, kReferenceNs) << " of the reference)\n"
           << "  clause (ii) baseline : " << engineNs << " <= " << regressionLineNs << "  (baseline "
           << kEngineBaselineNsAtPoly4 << " x " << kRegressionFactor << "; "
           << pct(engineNs, kEngineBaselineNsAtPoly4) << " of the baseline)\n"
           << "  headroom available   : " << kAvailableRegressionHeadroom
           << "x of the baseline before clause (i) trips (the ceiling binds first; "
           << "kMaxAdmissibleNs " << kMaxAdmissibleNs << " is not met by ruling)";
        WARN(os.str());
    }

    REQUIRE(isFiniteValue(static_cast<float>(sink)));
    REQUIRE(engineNs > 0.0);
    REQUIRE(withCavern <= kReferenceNs);
    REQUIRE(engineNs <= regressionLineNs);
}

// =============================================================================
// SC-002 - the per-stage cost breakdown. MEASUREMENT, NOT A CLOSURE GATE.
// =============================================================================
// Every stage is measured STANDALONE in this TU, because this spec declares no
// in-situ per-stage timing hook. The old "sum within 5 % of the whole" clause is
// deleted (spec SC-002): 5 % would force whole <= 1.053 x sum while SC-003
// legitimately allows 1.15, so the two would contradict each other. Composition
// is gated ONCE, by SC-003.
//
// A-6: `atmosphere` is in the ENGINE column. Spec SC-002's own list places it in
// the voice column; that list predates OQ-1 ruling (b), which moved
// AtmosphereEngine onto VoragoEngine (FR-002, FR-041, FR-056). Its cost is a
// GLOBAL stage paid once, not a per-voice stage paid N times.
//
// THE ARM THAT IS NOT A STAGE: VoragoVoice::advanceLifeOnly(512), standalone.
// That is B-6's L - the per-block cost of a slot that is NOT rendering - and it
// is what SC-001a's ladder consumes. It is measured here because SC-002 is where
// the standalone arms live.
TEST_CASE("VoragoVoice_StageCostProbe", "[systems][vorago][.perf]") {
    Buffers buf{};
    fillExcitation(buf);
    double sink = 0.0;

    std::vector<StageFigure> voiceStages;
    std::vector<StageFigure> engineStages;

    /// Keeps BloomEngine::processChunk's [[nodiscard]] return observable so the
    /// call cannot be dead-coded. Asserted at the BOTTOM of the case, with every
    /// other assertion, because a REQUIRE where a figure is measured would abort
    /// before the table is printed.
    std::size_t bloomCounts = 0;

    // --- cloud ---------------------------------------------------------------
    {
        auto cloud = buildCloud();
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    cloud->processStereoBlock(buf.outLeft.data() + (c * kChunk),
                                              buf.outRight.data() + (c * kChunk), kChunk);
                }
                sink += static_cast<double>(buf.outLeft[0]);
            });
        voiceStages.push_back(StageFigure{"cloud", ns, true});
    }

    // --- noise organism + the FR-015 / B-3 decorrelation ----------------------
    // The decorrelation loop is measured WITH the organism because it is not a
    // component: it is renderOneChunk step 4's body (vorago_voice.h:1851-1860),
    // two second-order all-passes, a one-sample branch delay and the gain ramp.
    {
        auto noise = buildNoise();
        Biquad apL;
        Biquad apR;
        apL.setCoefficients(BiquadCoefficients{.b0 = VoragoVoice::kNoiseApCoeffL,
                                               .b1 = 0.0f,
                                               .b2 = 1.0f,
                                               .a1 = 0.0f,
                                               .a2 = VoragoVoice::kNoiseApCoeffL});
        apR.setCoefficients(BiquadCoefficients{.b0 = VoragoVoice::kNoiseApCoeffR,
                                               .b1 = 0.0f,
                                               .b2 = 1.0f,
                                               .a1 = 0.0f,
                                               .a2 = VoragoVoice::kNoiseApCoeffR});
        LinearRamp gain;
        gain.configure(NoiseOrganism::kGainRampMs, static_cast<float>(kSr48));
        gain.snapTo(1.0f);
        float apDelayR = 0.0f;

        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    noise->processBlock(buf.mono.data() + off, kChunk);
                    for (std::size_t s = 0; s < kChunk; ++s) {
                        const float g = gain.process();
                        const float m = buf.mono[off + s] * g;
                        const float aL = apL.process(m);
                        const float aR = apR.process(apDelayR);
                        apDelayR = m;
                        buf.outLeft[off + s] += aL;
                        buf.outRight[off + s] += aR;
                    }
                }
                sink += static_cast<double>(buf.outLeft[0]);
            });
        voiceStages.push_back(StageFigure{"noise + decorrelation", ns, true});
    }

    // --- resonance network (IN PLACE, resonance_drift_network.h:506) ----------
    {
        auto res = buildResonance();
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                std::copy(buf.inLeft.begin(), buf.inLeft.end(), buf.scratchLeft.begin());
                std::copy(buf.inRight.begin(), buf.inRight.end(), buf.scratchRight.begin());
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    res->processBlock(buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      kChunk);
                }
                sink += static_cast<double>(buf.scratchLeft[0]);
            });
        voiceStages.push_back(StageFigure{"resonance", ns, true});
    }

    // --- feedback ecology (IN PLACE, feedback_ecology.h:908) ------------------
    {
        auto eco = buildEcology(kSr48);
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                std::copy(buf.inLeft.begin(), buf.inLeft.end(), buf.scratchLeft.begin());
                std::copy(buf.inRight.begin(), buf.inRight.end(), buf.scratchRight.begin());
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    eco->processBlock(buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      kChunk);
                }
                sink += static_cast<double>(buf.scratchLeft[0]);
            });
        voiceStages.push_back(StageFigure{"ecology", ns, true});
    }

    // --- bloom (renderOneChunk step 2) ---------------------------------------
    // The exp2 parent fill is measured WITH BloomEngine::processChunk because
    // the two together ARE step 2 (vorago_voice.h:1783-1812). The arrays are
    // kMaxSlots long, which is processChunk's normative precondition
    // (bloom_engine.h:188-190) - NOT parentCount long.
    {
        auto bloom = buildBloom();
        std::array<float, BloomEngine::kMaxSlots> ratios{};
        std::array<float, BloomEngine::kMaxSlots> amplitudes{};
        const float p = HarmonicCloud::kRichnessMinExponent
                        + ((HarmonicCloud::kRichnessMaxExponent
                            - HarmonicCloud::kRichnessMinExponent)
                           * 0.70f);
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t parents = bloom->reserveBase();
                    for (std::size_t i = 0; i < parents; ++i) {
                        ratios[i] = static_cast<float>(i + 1);
                        amplitudes[i] =
                            std::exp2(-p * Krate::DSP::detail::kHarmonicCloudLog2N[i]);
                    }
                    bloomCounts += bloom->processChunk(ratios.data(), amplitudes.data(), parents,
                                                       kChunk);
                }
                sink += static_cast<double>(amplitudes[0]);
            });
        voiceStages.push_back(StageFigure{"bloom (step 2)", ns, true});
    }

    // --- ecosystem -----------------------------------------------------------
    // INFORMATIONAL, NOT A SUM TERM. The ecosystem is advanced inside step 1,
    // and step 1 in full is what the L arm measures - adding both would count it
    // twice and make SC-003's bound artificially easy to pass.
    {
        auto eco = buildEcosystem();
        const double ns = warmThenMeasure(kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial,
                                          [&]() noexcept {
                                              for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                                                  eco->processChunk(kChunk);
                                              }
                                          });
        voiceStages.push_back(StageFigure{"ecosystem (inside L, NOT summed)", ns, false});
    }

    // --- body A / body B -----------------------------------------------------
    {
        auto bodyA = buildBody(ContinuousBody::BodyMaterial::StoneChamber,
                               VoragoVoice::kBodyASalt);
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    bodyA->processStereoBlock(buf.inLeft.data() + off, buf.inRight.data() + off,
                                              buf.outLeft.data() + off, buf.outRight.data() + off,
                                              kChunk);
                }
                sink += static_cast<double>(buf.outLeft[0]);
            });
        voiceStages.push_back(StageFigure{"body A (StoneChamber)", ns, true});
    }
    {
        auto bodyB = buildBody(ContinuousBody::BodyMaterial::SteelTank, VoragoVoice::kBodyBSalt);
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    bodyB->processStereoBlock(buf.inLeft.data() + off, buf.inRight.data() + off,
                                              buf.outLeft.data() + off, buf.outRight.data() + off,
                                              kChunk);
                }
                sink += static_cast<double>(buf.outLeft[0]);
            });
        voiceStages.push_back(StageFigure{"body B (SteelTank)", ns, true});
    }

    // --- envelope + blend ----------------------------------------------------
    // renderOneChunk steps 5, 8's per-sample mix and 10's peak scan: the
    // per-sample arithmetic the voice does that belongs to no component.
    {
        auto mse = buildEnvelope();
        LinearRamp blend;
        blend.configure(VoragoVoice::kBlendRampMs, static_cast<float>(kSr48));
        blend.snapTo(0.35f);
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                float peak = 0.0f;
                for (std::size_t s = 0; s < kBlockSize; ++s) {
                    const float g = mse->process();
                    buf.scratchLeft[s] = buf.inLeft[s] * g;
                    buf.scratchRight[s] = buf.inRight[s] * g;
                }
                for (std::size_t s = 0; s < kBlockSize; ++s) {
                    const float b = blend.process();
                    const float a = 1.0f - b;
                    buf.outLeft[s] = (a * buf.scratchLeft[s]) + (b * buf.inLeft[s]);
                    buf.outRight[s] = (a * buf.scratchRight[s]) + (b * buf.inRight[s]);
                    peak = std::max(peak,
                                    std::max(std::fabs(buf.outLeft[s]), std::fabs(buf.outRight[s])));
                }
                sink += static_cast<double>(peak);
            });
        voiceStages.push_back(StageFigure{"envelope + blend", ns, true});
    }

    // --- L: advanceLifeOnly, standalone. NOT A STAGE, and a SUM TERM ----------
    // It is the sum term that stands for renderOneChunk step 1 in full - the
    // ecosystem, both schedulers, both life modulators and publishIdentity's
    // reduction scan - which is work the voice does on EVERY chunk.
    {
        auto lifeVoice = buildVoice(kSr48, /*sounding=*/false);
        const double ns = warmThenMeasure(kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial,
                                          [&]() noexcept { lifeVoice->advanceLifeOnly(kBlockSize); });
        voiceStages.push_back(StageFigure{"L = advanceLifeOnly (step 1 in full)", ns, true});
    }

    // --- the engine column ---------------------------------------------------
    {
        auto voice = buildVoice(kSr48, /*sounding=*/true);
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                voice->processStereoBlock(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
                sink += static_cast<double>(buf.outLeft[0]);
            });
        engineStages.push_back(StageFigure{"voice sum (V, per sounding slot)", ns, false});
    }
    {
        auto atmos = buildAtmosphere();
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    atmos->processStereoBlock(buf.inLeft.data() + off, buf.inRight.data() + off,
                                              buf.outLeft.data() + off, buf.outRight.data() + off,
                                              kChunk);
                }
                sink += static_cast<double>(buf.outLeft[0]);
            });
        engineStages.push_back(StageFigure{"atmosphere (A-6: ENGINE column, ACTIVE ghost)", ns,
                                           false});
    }
    {
        auto sub = buildSubharmonic();
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                std::copy(buf.inLeft.begin(), buf.inLeft.end(), buf.scratchLeft.begin());
                std::copy(buf.inRight.begin(), buf.inRight.end(), buf.scratchRight.begin());
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    sub->processBlock(buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      kChunk);
                }
                sink += static_cast<double>(buf.scratchLeft[0]);
            });
        engineStages.push_back(StageFigure{"subharmonic", ns, false});
    }
    {
        auto smear = buildSmear();
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                std::copy(buf.inLeft.begin(), buf.inLeft.end(), buf.scratchLeft.begin());
                std::copy(buf.inRight.begin(), buf.inRight.end(), buf.scratchRight.begin());
                for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                    const std::size_t off = c * kChunk;
                    smear->processBlock(buf.scratchLeft.data() + off,
                                        buf.scratchRight.data() + off, kChunk);
                }
                sink += static_cast<double>(buf.scratchLeft[0]);
            });
        engineStages.push_back(StageFigure{"smear", ns, false});
    }
    {
        auto out = buildOutputStage();
        const double ns = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                std::copy(buf.inLeft.begin(), buf.inLeft.end(), buf.scratchLeft.begin());
                std::copy(buf.inRight.begin(), buf.inRight.end(), buf.scratchRight.begin());
                for (std::size_t done = 0; done < kBlockSize; done += kChunk) {
                    out->satL.process(buf.scratchLeft.data() + done, kChunk);
                    out->satR.process(buf.scratchRight.data() + done, kChunk);
                }
                out->limiter.processBlock(buf.scratchLeft.data(), buf.scratchRight.data(),
                                          static_cast<int>(kBlockSize));
                sink += static_cast<double>(buf.scratchLeft[0]);
            });
        engineStages.push_back(StageFigure{"output stage (2x saturator + limiter)", ns, false});
    }

    // -------------------------------------------------------------------------
    // The whole, measured DIRECTLY in the same run - the denominator each share
    // is expressed against (spec SC-002, "each figure's share of the directly
    // measured whole").
    // -------------------------------------------------------------------------
    double voiceWhole = 0.0;
    {
        auto voice = buildVoice(kSr48, /*sounding=*/true);
        voiceWhole = warmThenMeasure(
            kStageWarmupBlocks, kStageTrials, kStageBlocksPerTrial, [&]() noexcept {
                voice->processStereoBlock(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
                sink += static_cast<double>(buf.outLeft[0]);
            });
    }

    double voiceSum = 0.0;
    for (const StageFigure& f : voiceStages) {
        if (f.inVoiceSum) {
            voiceSum += f.nsPerBlock;
        }
    }
    double engineSum = 0.0;
    for (const StageFigure& f : engineStages) {
        engineSum += f.nsPerBlock;
    }

    // --- report, before any REQUIRE ------------------------------------------
    {
        std::ostringstream os;
        os << "SC-002 - Vorago per-stage breakdown, ns per 512-sample block @ 48 kHz\n"
           << "  measured whole (VoragoVoice): " << voiceWhole << " ns/block\n"
           << "  VOICE COLUMN (rows marked [sum] are the terms SC-003 adds up)\n";
        for (const StageFigure& f : voiceStages) {
            os << "    " << (f.inVoiceSum ? "[sum] " : "[----] ") << f.name << " : " << f.nsPerBlock
               << " ns  (" << pct(f.nsPerBlock, voiceWhole) << " of the measured whole)\n";
        }
        os << "    sum of [sum] rows : " << voiceSum << " ns  ("
           << pct(voiceSum, voiceWhole) << " of the measured whole)\n"
           << "  ENGINE COLUMN (G's stages; each is paid ONCE, not N times)\n";
        for (const StageFigure& f : engineStages) {
            os << "    " << f.name << " : " << f.nsPerBlock << " ns\n";
        }
        os << "    engine column sum : " << engineSum << " ns\n"
           << "  NOTE: the atmosphere row is the ACTIVE ghost (setLevel = kGhostBurstPeak), the "
              "worst case. VoragoEngine_CpuSurvey's measured totals see it mostly idle, because a "
              "ghost is an event on a 20-90 s scheduler.\n"
           << "  NOTE: there is NO closure bound here (spec SC-002). Composition is gated once, "
              "by SC-003, at 1.15.";
        WARN(os.str());
    }

    // --- assertions: finite and positive only (spec SC-002, FR-082) ----------
    REQUIRE(isFiniteValue(static_cast<float>(sink)));
    REQUIRE(bloomCounts > 0u);
    REQUIRE(voiceWhole > 0.0);
    REQUIRE(voiceSum > 0.0);
    REQUIRE(engineSum > 0.0);
    for (const StageFigure& f : voiceStages) {
        INFO("voice stage: " << f.name);
        REQUIRE(f.nsPerBlock > 0.0);
    }
    for (const StageFigure& f : engineStages) {
        INFO("engine stage: " << f.name);
        REQUIRE(f.nsPerBlock > 0.0);
    }
}

// =============================================================================
// SC-003 - composition overhead. THIS ONE GATES.
// =============================================================================
// measured(VoragoVoice) <= 1.15 x sum(standalone sub-components), both measured
// IN THIS TU IN THE SAME RUN so the machine state is shared - the Phase 9
// DATASET 1 lesson (cavern_verb_perf_test.cpp:216-231): the same binary, one
// minute apart, moved by 16-20 % on arms that were compared against each other
// while the code did not change.
//
// Seraphis's bound is 1.1 and it passed with 1.5 % margin
// (specs/seraphis-phase7-voice-engine/compliance.md:144); Vorago's voice has
// more inter-stage buffer traffic, hence 1.15 (spec SC-003).
//
// THE SUM'S TERMS ARE STATED HERE AND NOWHERE ELSE, because which terms are in
// it is the whole content of the criterion:
//     cloud, noise + decorrelation, resonance, ecology, bloom (step 2),
//     body A, body B, envelope + blend, and L (= step 1 in full).
// The ecosystem is NOT a term: it runs inside step 1 and L already carries it.
// Adding it would inflate the denominator and make the bound vacuous.
//
// IF THIS FAILS: the fix is the composition, never the 1.15. Every stage above
// is measured at the same configuration the voice runs it at, so a ratio above
// 1.15 means the voice is paying for buffer traffic the stages do not - which is
// a defect in the composition, not a budget item (FR-082).
TEST_CASE("VoragoVoice_CompositionOverhead", "[systems][vorago][.perf]") {
    Buffers buf{};
    fillExcitation(buf);
    double sink = 0.0;

    // --- the nine standalone terms -------------------------------------------
    auto cloud = buildCloud();
    const double nsCloud =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                cloud->processStereoBlock(buf.outLeft.data() + (c * kChunk),
                                          buf.outRight.data() + (c * kChunk), kChunk);
            }
            sink += static_cast<double>(buf.outLeft[0]);
        });

    auto noise = buildNoise();
    Biquad apL;
    Biquad apR;
    apL.setCoefficients(BiquadCoefficients{.b0 = VoragoVoice::kNoiseApCoeffL,
                                           .b1 = 0.0f,
                                           .b2 = 1.0f,
                                           .a1 = 0.0f,
                                           .a2 = VoragoVoice::kNoiseApCoeffL});
    apR.setCoefficients(BiquadCoefficients{.b0 = VoragoVoice::kNoiseApCoeffR,
                                           .b1 = 0.0f,
                                           .b2 = 1.0f,
                                           .a1 = 0.0f,
                                           .a2 = VoragoVoice::kNoiseApCoeffR});
    LinearRamp noiseGain;
    noiseGain.configure(NoiseOrganism::kGainRampMs, static_cast<float>(kSr48));
    noiseGain.snapTo(1.0f);
    float apDelayR = 0.0f;
    const double nsNoise =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                const std::size_t off = c * kChunk;
                noise->processBlock(buf.mono.data() + off, kChunk);
                for (std::size_t s = 0; s < kChunk; ++s) {
                    const float g = noiseGain.process();
                    const float m = buf.mono[off + s] * g;
                    const float aL = apL.process(m);
                    const float aR = apR.process(apDelayR);
                    apDelayR = m;
                    buf.outLeft[off + s] += aL;
                    buf.outRight[off + s] += aR;
                }
            }
            sink += static_cast<double>(buf.outLeft[0]);
        });

    auto res = buildResonance();
    const double nsResonance =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            std::copy(buf.inLeft.begin(), buf.inLeft.end(), buf.scratchLeft.begin());
            std::copy(buf.inRight.begin(), buf.inRight.end(), buf.scratchRight.begin());
            for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                const std::size_t off = c * kChunk;
                res->processBlock(buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                  buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                  kChunk);
            }
            sink += static_cast<double>(buf.scratchLeft[0]);
        });

    auto ecology = buildEcology(kSr48);
    const double nsEcology =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            std::copy(buf.inLeft.begin(), buf.inLeft.end(), buf.scratchLeft.begin());
            std::copy(buf.inRight.begin(), buf.inRight.end(), buf.scratchRight.begin());
            for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                const std::size_t off = c * kChunk;
                ecology->processBlock(buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      buf.scratchLeft.data() + off, buf.scratchRight.data() + off,
                                      kChunk);
            }
            sink += static_cast<double>(buf.scratchLeft[0]);
        });

    auto bloom = buildBloom();
    std::array<float, BloomEngine::kMaxSlots> ratios{};
    std::array<float, BloomEngine::kMaxSlots> amplitudes{};
    const float pExponent = HarmonicCloud::kRichnessMinExponent
                            + ((HarmonicCloud::kRichnessMaxExponent
                                - HarmonicCloud::kRichnessMinExponent)
                               * 0.70f);
    std::size_t bloomCounts = 0;
    const double nsBloom =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                const std::size_t parents = bloom->reserveBase();
                for (std::size_t i = 0; i < parents; ++i) {
                    ratios[i] = static_cast<float>(i + 1);
                    amplitudes[i] =
                        std::exp2(-pExponent * Krate::DSP::detail::kHarmonicCloudLog2N[i]);
                }
                bloomCounts += bloom->processChunk(ratios.data(), amplitudes.data(), parents,
                                                   kChunk);
            }
            sink += static_cast<double>(amplitudes[0]);
        });

    auto bodyA = buildBody(ContinuousBody::BodyMaterial::StoneChamber, VoragoVoice::kBodyASalt);
    const double nsBodyA =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                const std::size_t off = c * kChunk;
                bodyA->processStereoBlock(buf.inLeft.data() + off, buf.inRight.data() + off,
                                          buf.outLeft.data() + off, buf.outRight.data() + off,
                                          kChunk);
            }
            sink += static_cast<double>(buf.outLeft[0]);
        });

    auto bodyB = buildBody(ContinuousBody::BodyMaterial::SteelTank, VoragoVoice::kBodyBSalt);
    const double nsBodyB =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            for (std::size_t c = 0; c < kChunksPerBlock; ++c) {
                const std::size_t off = c * kChunk;
                bodyB->processStereoBlock(buf.inLeft.data() + off, buf.inRight.data() + off,
                                          buf.outLeft.data() + off, buf.outRight.data() + off,
                                          kChunk);
            }
            sink += static_cast<double>(buf.outLeft[0]);
        });

    auto mse = buildEnvelope();
    LinearRamp blend;
    blend.configure(VoragoVoice::kBlendRampMs, static_cast<float>(kSr48));
    blend.snapTo(0.35f);
    const double nsEnvelope =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            float peak = 0.0f;
            for (std::size_t s = 0; s < kBlockSize; ++s) {
                const float g = mse->process();
                buf.scratchLeft[s] = buf.inLeft[s] * g;
                buf.scratchRight[s] = buf.inRight[s] * g;
            }
            for (std::size_t s = 0; s < kBlockSize; ++s) {
                const float b = blend.process();
                const float a = 1.0f - b;
                buf.outLeft[s] = (a * buf.scratchLeft[s]) + (b * buf.inLeft[s]);
                buf.outRight[s] = (a * buf.scratchRight[s]) + (b * buf.inRight[s]);
                peak = std::max(peak,
                                std::max(std::fabs(buf.outLeft[s]), std::fabs(buf.outRight[s])));
            }
            sink += static_cast<double>(peak);
        });

    auto lifeVoice = buildVoice(kSr48, /*sounding=*/false);
    const double nsLife = warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial,
                                          [&]() noexcept { lifeVoice->advanceLifeOnly(kBlockSize); });

    // --- the whole -----------------------------------------------------------
    auto voice = buildVoice(kSr48, /*sounding=*/true);
    const double nsVoice =
        warmThenMeasure(kWarmupBlocks, kTrials, kBlocksPerTrial, [&]() noexcept {
            voice->processStereoBlock(buf.outLeft.data(), buf.outRight.data(), kBlockSize);
            sink += static_cast<double>(buf.outLeft[0]);
        });

    const double sum = nsCloud + nsNoise + nsResonance + nsEcology + nsBloom + nsBodyA + nsBodyB
                       + nsEnvelope + nsLife;
    constexpr double kOverheadBound = 1.15;
    const double ratio = (sum > 0.0) ? (nsVoice / sum) : 0.0;

    // --- report, before any REQUIRE ------------------------------------------
    {
        std::ostringstream os;
        os << "SC-003 - VoragoVoice composition overhead, ns per 512-sample block @ 48 kHz\n"
           << "    cloud                 : " << nsCloud << "\n"
           << "    noise + decorrelation : " << nsNoise << "\n"
           << "    resonance             : " << nsResonance << "\n"
           << "    ecology               : " << nsEcology << "\n"
           << "    bloom (step 2)        : " << nsBloom << "\n"
           << "    body A                : " << nsBodyA << "\n"
           << "    body B                : " << nsBodyB << "\n"
           << "    envelope + blend      : " << nsEnvelope << "\n"
           << "    L (step 1 in full)    : " << nsLife << "\n"
           << "    ------------------------------------------\n"
           << "    sum of standalone     : " << sum << "\n"
           << "    measured VoragoVoice  : " << nsVoice << "\n"
           << "    ratio whole / sum     : " << ratio << "   (bound " << kOverheadBound << ")\n"
           << "    bound in ns           : " << (sum * kOverheadBound)
           << "   - if this is exceeded, fix the COMPOSITION, never the 1.15 (FR-082)";
        WARN(os.str());
    }

    REQUIRE(isFiniteValue(static_cast<float>(sink)));
    REQUIRE(bloomCounts > 0u);
    REQUIRE(sum > 0.0);
    REQUIRE(nsVoice > 0.0);
    REQUIRE(nsVoice <= (sum * kOverheadBound));
}

// =============================================================================
// B-7's cost half - VoragoVoice_ClearingPathCost
// =============================================================================
// B-7 split VoragoVoice's clearing into one control-thread path and three
// RT-safe ones, on the strength of an ARITHMETIC claim: reset() reaches
// FeedbackEcology::reset(), a std::fill over six power-of-two rings - ~786 KB at
// 48 kHz and ~3.1 MB at 192 kHz (feedback_ecology.h:2401-2412) - while the three
// RT-safe paths take ecology_.silenceAudio()'s O(1) read-mute instead
// (vorago_voice.h:1296-1300). THIS CASE IS THAT CLAIM, MEASURED.
//
// Clause (a) is the non-vacuous one: each RT-safe path must be at least 10x
// cheaper than reset() AT BOTH RATES. It is the clause that regresses the day
// someone puts ecology_.reset() back on the steal path - which is exactly the
// defect B-7 was raised against.
//
// Clause (b) is what kResetsPerControlChunk's rationale is sized against: a
// steal costs silence() + resetForSteal() (plan S3.2, "clearRunState(true)
// executes TWICE"), and that pair must fit inside ONE 64-sample control chunk at
// the measured rate - 1 333 333 ns at 48 kHz, 333 333 ns at 192 kHz. Record the
// figure in compliance.md.
//
// WHY A RUN OF CALLS RATHER THAN ONE. A single call is far too short to time
// against steady_clock's tick. None of the four paths has an early-out - each is
// an unconditional walk over the same buffers and sub-component resets - so the
// second call costs what the first one costs and a run of kClearCallsPerTrial
// divided by that count is the per-call figure.
TEST_CASE("VoragoVoice_ClearingPathCost", "[systems][vorago][.perf]") {
    struct RateResult {
        double sampleRate = 0.0;
        double reset = 0.0;
        double recovery = 0.0;
        double steal = 0.0;
        double silence = 0.0;
        double stealPair = 0.0;  ///< silence() + resetForSteal(), what one steal costs
    };

    const auto measureRate = [](double sampleRate) {
        RateResult r{};
        r.sampleRate = sampleRate;

        // The voice is driven into a REAL state first: a clearing path measured
        // on a never-rendered voice would walk cold buffers and report a figure
        // no steal ever pays.
        auto voice = buildVoice(sampleRate, /*sounding=*/true);
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        for (int i = 0; i < 200; ++i) {
            voice->processStereoBlock(left.data(), right.data(), kBlockSize);
        }

        const auto bestNsPerCall = [](int trials, int callsPerTrial, const auto& runCall) {
            double best = std::numeric_limits<double>::max();
            for (int trial = 0; trial < trials; ++trial) {
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < callsPerTrial; ++i) {
                    runCall();
                }
                const auto end = std::chrono::steady_clock::now();
                const double ns = std::chrono::duration<double, std::nano>(end - start).count();
                best = std::min(best, ns / static_cast<double>(callsPerTrial));
            }
            return best;
        };

        r.reset = bestNsPerCall(kClearTrials, kClearCallsPerTrial,
                                [&]() noexcept { (*voice).reset(); });
        r.recovery = bestNsPerCall(kClearTrials, kClearCallsPerTrial,
                                   [&]() noexcept { voice->resetForRecovery(); });
        r.steal = bestNsPerCall(kClearTrials, kClearCallsPerTrial,
                                [&]() noexcept { voice->resetForSteal(); });
        r.silence = bestNsPerCall(kClearTrials, kClearCallsPerTrial,
                                  [&]() noexcept { voice->silence(); });
        r.stealPair = bestNsPerCall(kClearTrials, kClearCallsPerTrial, [&]() noexcept {
            voice->silence();
            voice->resetForSteal();
        });
        return r;
    };

    const RateResult at48 = measureRate(kSr48);
    const RateResult at192 = measureRate(kSr192);

    // --- ALL FOUR FIGURES PRINTED BEFORE ANY REQUIRE -------------------------
    // A REQUIRE aborts the case, so gating where a figure is measured would let
    // the first failing rate hide the other one - and compliance.md needs both.
    const auto report = [](const RateResult& r) {
        const double chunkNs = controlChunkNs(r.sampleRate);
        std::ostringstream os;
        os << "B-7 clearing-path cost @ " << r.sampleRate << " Hz, ns per call\n"
           << "    reset()            : " << r.reset
           << "   (CONTROL THREAD ONLY - reaches FeedbackEcology::reset()'s O(buffer) wipe)\n"
           << "    resetForRecovery() : " << r.recovery << "   (RT-safe; x"
           << ((r.recovery > 0.0) ? (r.reset / r.recovery) : 0.0) << " cheaper than reset())\n"
           << "    resetForSteal()    : " << r.steal << "   (RT-safe; x"
           << ((r.steal > 0.0) ? (r.reset / r.steal) : 0.0) << " cheaper)\n"
           << "    silence()          : " << r.silence << "   (RT-safe; x"
           << ((r.silence > 0.0) ? (r.reset / r.silence) : 0.0) << " cheaper)\n"
           << "    silence() + resetForSteal() = ONE STEAL : " << r.stealPair << " ns\n"
           << "    one 64-sample control chunk             : " << chunkNs << " ns  ("
           << ((chunkNs > 0.0) ? ((r.stealPair / chunkNs) * 100.0) : 0.0)
           << " % of it)  <- kResetsPerControlChunk (= " << VoragoEngine::kResetsPerControlChunk
           << ") is sized against this";
        return os.str();
    };
    WARN(report(at48));
    WARN(report(at192));

    // --- clause (a): each RT-safe path is no dearer than reset() and fits one
    // control chunk at that rate. RULED 2026-09-19 (spec Q-L): the original
    // ">= 10x cheaper than reset()" rested on a false premise - silenceAudio()
    // wipes the same delay memory reset() does (feedback_ecology.h, B-7), so
    // the two paths differ by one small call (measured 37.3 us vs 23.4 us at
    // 48 kHz, 1.6x). The regression the ratio was meant to catch (a component
    // reset() creeping back onto an audio-thread path) is caught by the chunk
    // bound below and by the clearing-path tests that pin which call each
    // path makes. No ratio is asserted.
    const std::array<RateResult, 2> rates{at48, at192};
    for (const RateResult& r : rates) {
        INFO("sample rate " << r.sampleRate);
        const double chunkNs = controlChunkNs(r.sampleRate);
        REQUIRE(r.reset > 0.0);
        REQUIRE(r.recovery > 0.0);
        REQUIRE(r.steal > 0.0);
        REQUIRE(r.silence > 0.0);
        REQUIRE(r.recovery <= r.reset);
        REQUIRE(r.steal <= r.reset);
        REQUIRE(r.silence <= r.reset);
        REQUIRE(r.recovery <= chunkNs);
        REQUIRE(r.steal <= chunkNs);
        REQUIRE(r.silence <= chunkNs);
    }

    // --- clause (b): one steal fits inside one control chunk at that rate ----
    REQUIRE(at48.stealPair > 0.0);
    REQUIRE(at192.stealPair > 0.0);
    REQUIRE(at48.stealPair <= controlChunkNs(kSr48));
    REQUIRE(at192.stealPair <= controlChunkNs(kSr192));
}
