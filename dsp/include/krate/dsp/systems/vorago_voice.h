// ==============================================================================
// Layer 3: System - VoragoVoice (one dark-ambient drone voice)
// ==============================================================================
// Vorago Phase 10. Spec slug: vorago-phase10-voice-engine.
//   Spec:    specs/vorago-phase10-voice-engine/spec.md
//   Plan:    specs/vorago-phase10-voice-engine/plan.md
//   Tasks:   specs/vorago-phase10-voice-engine/tasks.md
//   Roadmap: specs/Vorago-roadmap.md, Part A -> Phase 10 (lines 446-474),
//            Open Questions 5 and 6 (lines 588-589)
//
// One voice composes the Vorago substrate: BloomEngine -> HarmonicCloud
// spectrum handoff, NoiseOrganism, ResonanceDriftNetwork, FeedbackEcology,
// two ContinuousBody slots, and the identity layer that reads
// EcosystemEngine / SlowEventScheduler.
//
// Feature: vorago-phase10-voice-engine
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (no allocation, lock, exception or IO on the
//   audio thread; every pool is sized in prepare())
// - Principle III: Modern C++ (C++20, value semantics, no owning pointers)
// - Principle IX: Layer 3 - Layers 0-2 plus Layer 3 PEERS only
// - Principle X: DSP Constraints (reject-never-clamp setters, a non-finite
//   guard ladder)
// - Principle XI: Performance Budget (SC-001a, SC-001b, SC-002, SC-003)
//
// ------------------------------------------------------------------------------
// ARCHITECTURE RULINGS THAT SHAPE THIS FILE
//
// AR-1  NO effects/ HEADER IS INCLUDED HERE, EVER. The cavern space is Layer 4
//       and sits OUTSIDE the engine at a documented seam; tools/lint-layers.js
//       flags a systems/ -> effects/ include. atmosphere_engine.h is likewise
//       NOT included: the ghost tap is engine-owned (plan OQ-1 (b)).
// AR-2  The identity layer is routed EXPLICITLY - EcosystemEngine and
//       SlowEventScheduler outputs are read through their concrete types and
//       pushed into shipped setters. No ModulationEngine, no VoiceModRouter, no
//       ModulationSource virtual dispatch on the audio thread.
// AR-5  The voice envelope gates the EXCITATION bus (cloud + noise), not the
//       voice output. Nothing downstream of that point is gated; the voice
//       retires on a level detector (FR-013), exactly as SeraphisVoice does
//       (seraphis_voice.h:1066).
// B-7   The RT-safe clearing paths (resetForRecovery / resetForSteal / silence)
//       route through FeedbackEcology::silenceAudio() (feedback_ecology.h:935),
//       never its control-thread-only reset() (:854, cost quantified at
//       :2401-2412: ~786 KB of std::fill per voice at 48 kHz, ~3.1 MB at
//       192 kHz). reset() is the ONE entry point that reaches the wipe, and it
//       is not an audio-thread call.
//
// ------------------------------------------------------------------------------
// THE THREE INVARIANTS OF THE CONTROL CLOCK
//
// D1  THE VOICE NEVER RENDERS A PARTIAL CHUNK. HarmonicCloud (:908-912),
//     ResonanceDriftNetwork, FeedbackEcology, BloomEngine, EcosystemEngine,
//     ContinuousBody and NoiseOrganism all run a 64-sample control grid, and
//     several take exactly ONE control step per call regardless of the length
//     passed. Whole chunks are rendered into a 64-sample stereo carry FIFO and
//     the caller is served out of it, which costs zero added latency and makes
//     FR-007's partition invariance exact.
// D3  silence() cannot fade by RENDERING: a steal is issued BETWEEN blocks, so
//     there are no samples for a fade to occupy. The last served sample pair is
//     captured and added, decaying, to the first silenceRampSamples_ samples
//     rendered afterwards.
// D4  lastOut* is captured at SERVE time, not at render time: on a mid-chunk
//     steal carryL_[63] is up to 63 samples of program material away from the
//     amplitude the output actually reached.
//
// ------------------------------------------------------------------------------
// BUILD STATE (tasks.md).
// T010 (this pass) lands the constants, VoragoVoiceConfig, prepare(), the four
//   clearing paths, the seeds, the six-stage envelope and the note surface. It
//   also lands processStereoBlock()/advanceLifeOnly() and renderOneChunk(),
//   because T010's own authoritative test list (tasks.md:541-600) measures
//   control-step accounting and a silenced ecology tail, neither of which is
//   observable without a render path. T011/T012 refine the two hooks named
//   below and the render chain's spectral and blend detail.
// T011 (landed) fills updateSpectrumTarget() - the bloom -> cloud spectrum
//   handoff (B-1, B-2, FR-011, FR-012), called from step 2 of renderOneChunk().
// T012 (landed) pins the render chain that T010 wrote: partition invariance over
//   the carry FIFO (SC-007), the FR-015 decorrelation pair (B-3, measured figure
//   recorded at kNoiseApCoeffR), the two-body blend endpoints and its per-sample
//   ramp (SC-017, SC-017a), and FR-024's dormancy boundaries. It changed no
//   step of renderOneChunk(): steps 3-10 already matched the plan S3.4 order.
// T013 (landed) fills publishIdentity() and assignAgentSlots() - the identity
//   layer, FR-020..FR-026, called from step 1 of renderOneChunk() and from
//   prepare() step 6 respectively. publishIdentity() is split into a GATHER half
//   (gatherEcosystemLanes / gatherSchedulerLanes, which read the ecosystem and
//   the two schedulers) and an APPLY half (applyIdentityLanes, which writes the
//   shipped setters and nothing else), so SC-019a and SC-019b can drive the
//   apply half over an ENUMERATED table that the live components would never
//   produce on demand - the B-4 probe rule, with the friend struct defined in
//   the test TU.
//
// ------------------------------------------------------------------------------
// TWO COMPONENT CEILINGS THIS CLASS RAISES PER INSTANCE (ruled 2026-09-18)
//
// 1. MultiStageEnvelope clamps every stage and the release to a ceiling that
//    ships as kMaxStageTimeMs = 10 000 ms. FR-014's shape (a 20 s attack, a
//    60 s third body stage, a 45 s release) needs more, so prepare() raises
//    THIS instance's ceiling through the append-only
//    MultiStageEnvelope::setMaxStageTimeMs(kEnvelopeMaxStageTimeMs) before it
//    authors a single stage. The constant itself is untouched: Seraphis and
//    Ruinae derive parameter ranges from it and never call the setter.
//    envelope().getStageTime(st) therefore reads back exactly what the shadow
//    arrays hold (VoragoVoice_EnvelopeShapeIsShipped pins both).
// 2. GrowthEnvelope clamps its duration to kMaxDuration = 60 s the same way;
//    prepare() raises it to kGrowthMaxDurationSeconds through
//    GrowthEnvelope::setMaxDuration before FR-090's 120 s duration is set, so
//    growth().getDuration() reports 120.
// Both setters are default-inert appends under the Seraphis-green gate
// (SC-016; tools/check-seraphis-green.js checks the zero-deletion bar).
// ==============================================================================

#pragma once

// --- Layer 0 (core) ---------------------------------------------------------
#include <krate/dsp/core/db_utils.h>   // detail::isFinite - fast-math-immune
#include <krate/dsp/core/env_curve.h>  // EnvCurve
#include <krate/dsp/core/random.h>     // deriveStreamSeed, Xorshift32

// --- Layer 1 (primitives) ---------------------------------------------------
#include <krate/dsp/primitives/biquad.h>    // FR-015 decorrelation pair (B-3)
#include <krate/dsp/primitives/smoother.h>  // LinearRamp (blend + noise gain)

// --- Layer 2 (processors) ---------------------------------------------------
// multi_stage_envelope.h transitively provides primitives/envelope_utils.h,
// which is where RetriggerMode lives (:64) - there is no curve enum there, and
// EnvCurve comes from core/env_curve.h:24 instead.
#include <krate/dsp/processors/breathing_modulator.h>
#include <krate/dsp/processors/growth_envelope.h>
#include <krate/dsp/processors/multi_stage_envelope.h>
#include <krate/dsp/processors/slow_event_scheduler.h>
#include <krate/dsp/processors/tidal_modulator.h>

// --- Layer 3 (systems) - PEERS ONLY -----------------------------------------
#include <krate/dsp/systems/bloom_engine.h>
#include <krate/dsp/systems/continuous_body.h>
#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/feedback_ecology.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/noise_organism.h>
#include <krate/dsp/systems/resonance_drift_network.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

class VoragoEngine;
class VoragoMacroMatrix;

namespace detail {
/// SC-011's silence-ramp probe. B-4: DEFINED IN THE TEST TU, never here, so no
/// KRATE_DSP_VORAGO_TEST_HOOKS define and no target_compile_definitions line
/// exists anywhere in the build.
struct VoragoVoiceSilenceRampProbe;
/// SC-029's non-finite containment probe. Same rule (B-4).
struct VoragoEngineNonFiniteProbe;
/// SC-019a / SC-019b's identity-layer probe. Same rule (B-4): DEFINED IN THE
/// TEST TU. It exists because the two contributions FR-023 combines are not
/// settable from outside - a SlowEventScheduler's running event value and an
/// EcosystemEngine agent's energy are the components' own business - so an
/// ENUMERATED pair table can only reach VoragoVoice::applyIdentityLanes() and
/// VoragoVoice::reduceAgentLanes() through a friend.
struct VoragoVoiceIdentityProbe;
}  // namespace detail

// =============================================================================
// VoragoVoiceConfig (FR-004)
// =============================================================================

/// @brief Designated-initialiser-only prepare() configuration for VoragoVoice.
///
/// EVERY FIELD IS CLAMPED, NEVER REJECTED (FR-004). Each owner clamps again and
/// the getter that reports the realised value is the OWNER's
/// (`noise().getNumSources()`, `resonance().getNumPeaks()`,
/// `ecology().getNumLoops()`, `ecosystem().getAgentCount()`,
/// `bloom().numChildSlots()`), so the voice stores no shadow of a capacity it
/// does not own.
///
/// There is deliberately NO atmosphere field: AtmosphereEngine is engine-owned
/// (FR-004, FR-042, plan OQ-1 (b)).
struct VoragoVoiceConfig {
    /// Clamped [1, VoragoVoice::kMaxBlockSamples]. The sub-components floor it
    /// again at 64 (noise_organism.h:229, resonance_drift_network.h PrepareConfig,
    /// feedback_ecology.h PrepareConfig), and in FeedbackEcology and
    /// ResonanceDriftNetwork it sizes nothing at all.
    std::size_t maxBlockSamples = 2048;
    /// Clamped [1, NoiseOrganism::kMaxSources] (4).
    std::size_t numNoiseSources = 4;
    /// Clamped [1, ResonanceDriftNetwork::kMaxPeaks] (12).
    std::size_t numResonancePeaks = 12;
    /// Clamped [1, FeedbackEcology::kMaxLoops] (6).
    std::size_t numEcologyLoops = 6;
    /// Clamped [EcosystemEngine::kMinAgents, EcosystemEngine::kMaxAgents].
    std::size_t ecosystemAgents = 32;
    /// Clamped [1, EcosystemEngine::kMaxResourceCells].
    std::size_t ecosystemCells = 64;
    /// Clamped [EcosystemEngine::kMinStepIntervalChunks,
    ///          EcosystemEngine::kMaxStepIntervalChunks] = [8, 64].
    std::size_t ecosystemStepChunks = 8;
    /// Clamped [0, BloomEngine::kMaxChildren]. SIX, deliberately NOT the
    /// component's own default of 8 (bloom_engine.h:215): B-1's cloud-capacity
    /// floor is kBloomChildSlots + kMinParentSlots, and 8 would raise it to 16.
    std::size_t bloomChildSlots = 6;
    /// Forwarded to NoiseOrganism, clamped [5, 200] there (:231).
    float maxCombDelayMs = 50.0f;
};

// =============================================================================
// VoragoVoice (Layer 3)
// =============================================================================

/// @brief One Vorago drone voice.
///
/// REAL-TIME CONTRACT. prepare() is the ONLY allocating path and is not RT-safe.
/// reset() is likewise NOT an audio-thread call (B-7). Everything else -
/// processStereoBlock(), advanceLifeOnly(), noteOn(), noteOff(),
/// resetForRecovery(), resetForSteal(), silence() and every setter - is
/// allocation-free, lock-free, exception-free and IO-free.
class VoragoVoice {
public:
    // =========================================================================
    // Public constants - all class-scoped, kPascalCase (plan S2.2)
    // =========================================================================

    /// FR-007. The shared control grid: harmonic_cloud.h:144,
    /// noise_organism.h:150, resonance_drift_network.h:135,
    /// feedback_ecology.h:195, bloom_engine.h:221, ecosystem_engine.h:182,
    /// continuous_body.h:117 all publish the same 64.
    static constexpr std::size_t kControlChunkSamples = 64;
    /// FR-004 clamp ceiling for VoragoVoiceConfig::maxBlockSamples.
    static constexpr std::size_t kMaxBlockSamples = 2048;
    /// FR-036. Two ContinuousBody slots, blended per sample.
    static constexpr std::size_t kNumBodies = 2;
    /// FR-022 (Q7). A fast scheduler and a slow one.
    static constexpr std::size_t kNumEventSchedulers = 2;

    /// FR-022. The five destination FAMILIES a SlowEventScheduler event can
    /// address. `setTargetCount(kNumEventFamilies)` is written at prepare() and
    /// `getActiveTarget()` (slow_event_scheduler.h:356) returns an index into
    /// THIS roster, or `SlowEventScheduler::kNoTarget` (0xFF) while idle.
    ///
    /// THE ORDER IS NOT `EcosystemEngine::Kind`'s ORDER and must not be assumed
    /// to be: Kind is {Partial, Resonator, Noise, Feedback, Ghost}
    /// (ecosystem_engine.h:282-288) and the families below are in the order
    /// FR-022 lists them. kindForFamily() is the ONE mapping between the two
    /// rosters, which is why it is written out rather than left to an implicit
    /// cast that would quietly route a peak wake into the noise organism.
    enum class EventFamily : std::uint8_t {
        BloomTrigger = 0,  ///< BloomEngine::triggerBloom(), ONSET EDGE ONLY
        NoiseWake = 1,     ///< NoiseOrganism::setSourceWake
        PeakWake = 2,      ///< ResonanceDriftNetwork::setPeakWake
        LoopWake = 3,      ///< FeedbackEcology::setLoopWake
        GhostBurst = 4     ///< the voice's ghostRequest_ accumulator (FR-020b)
    };
    static constexpr std::size_t kNumEventFamilies = 5;
    static_assert(static_cast<std::size_t>(EventFamily::GhostBurst) + 1u == kNumEventFamilies,
                  "FR-022: the destination-family roster size");

    /// FR-020a. The widest destination family, and the second dimension of the
    /// identity layer's 5 x 12 reduction scratch: ResonanceDriftNetwork's 12
    /// peaks against Noise 4, Feedback 6, Partial 1 and Ghost 1.
    static constexpr std::size_t kMaxSlotsPerKind = ResonanceDriftNetwork::kMaxPeaks;
    static_assert(kMaxSlotsPerKind >= NoiseOrganism::kMaxSources
                      && kMaxSlotsPerKind >= FeedbackEcology::kMaxLoops,
                  "FR-020a: every destination family must fit the reduction scratch");

    /// FR-022's two shipped interval ranges, BEFORE the event-rate scale.
    /// prepare() writes them at scale 1.0 and publishIdentity() re-writes them
    /// divided by getEventRateScale(), so a HIGHER scale means SHORTER intervals
    /// - more events, which is what plan S7.3's `Life` row asks for.
    static constexpr float kFastEventIntervalMinSeconds = 20.0f;
    static constexpr float kFastEventIntervalMaxSeconds = 90.0f;
    static constexpr float kSlowEventIntervalMinSeconds = 180.0f;
    static constexpr float kSlowEventIntervalMaxSeconds = 600.0f;

    // --- retirement (FR-013, Q6) --------------------------------------------
    /// -90 dBFS. Deliberately NOT Seraphis's -100 (seraphis_voice.h:148): a
    /// Vorago tail is minutes long and the level detector must be able to
    /// declare it over.
    static constexpr float kTailSilenceThreshold = 3.1623e-5f;
    /// Level-detector release TAU. NOT a time-to-99 %, which is why
    /// calculateOnePolCoefficient must not be used here: that helper treats its
    /// argument as 5*tau (smoother.h:86-93) and would give tau = 20 ms.
    static constexpr float kLevelReleaseMs = 100.0f;
    /// silence() ramp. Shorter than one control chunk at every supported rate,
    /// so a steal completes inside one chunk (seraphis_voice.h:155).
    static constexpr float kSilenceRampMs = 1.0f;
    /// FR-013's TEN SECONDS, expressed as a DURATION and derived at prepare()
    /// into quiescentChunksToRetire_. A literal chunk count would silently mean
    /// 2.5 s at 192 kHz.
    static constexpr float kQuiescentSeconds = 10.0f;

    // --- envelope (FR-014, Q5) ----------------------------------------------
    /// The ONE curve every setStage() call uses.
    static constexpr EnvCurve kStageCurve = EnvCurve::Exponential;
    /// Six stages; <= MultiStageEnvelope::kMaxStages (8).
    ///
    /// FR-014 says "the 4-stage MultiStageEnvelope" and this class ships six.
    /// That is a CARRIED deviation, not an absorbed one: FR-014's "4-stage" is a
    /// statement about the PRE-SUSTAIN WALK - attack plus three body stages,
    /// exactly stages 0..3 and exactly the three numbers FR-014 pins. Stages 4
    /// and 5 exist because advanceToNextStage() only enters Sustaining when
    /// currentStage_ == sustainPoint_ (multi_stage_envelope.h:384-387), so a
    /// sustain-hold stage at kEnvelopeSustainPoint and a post-sustain stage
    /// above it are required by the component's own contract. Both are 0 ms and
    /// neither adds a millisecond of envelope.
    static constexpr int kEnvelopeStages = 6;
    /// Stages 0..3 are the pre-sustain walk.
    static constexpr int kEnvelopeSustainPoint = 4;

    /// FR-014's shipped stage levels, reproduced not re-derived (plan S3.8).
    static constexpr std::array<float, static_cast<std::size_t>(kEnvelopeStages)>
        kDefaultStageLevels{1.00f, 0.80f, 0.92f, 0.85f, 0.85f, 0.00f};
    /// FR-014's shipped stage times in ms, reproduced not re-derived.
    static constexpr std::array<float, static_cast<std::size_t>(kEnvelopeStages)>
        kDefaultStageTimesMs{20000.0f, 30000.0f, 45000.0f, 60000.0f, 0.0f, 0.0f};
    /// FR-014's shipped release, in ms.
    static constexpr float kDefaultReleaseMs = 45000.0f;
    /// The per-instance ceiling prepare() gives mse_ (MultiStageEnvelope ships
    /// 10 000 ms; see the header banner). Twice the longest shipped stage, so
    /// the setter surface has room without another ceiling change.
    static constexpr float kEnvelopeMaxStageTimeMs = 120000.0f;
    /// FR-090's Growth-mode rise duration, in seconds, and the per-instance
    /// ceiling prepare() gives growth_ so that it is not clamped to the
    /// component's shipped 60 s.
    static constexpr float kDefaultGrowthDurationSeconds = 120.0f;
    static constexpr float kGrowthMaxDurationSeconds = kDefaultGrowthDurationSeconds;

    /// FR-014a / Q5. Standard walks the six stages; Growth forces every
    /// pre-sustain stage time to 0 ms and multiplies the composite by
    /// GrowthEnvelope's logistic swell instead.
    enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 };

    // --- bloom slot budget (B-1) --------------------------------------------
    /// The child-slot count this voice reserves in the BloomEngine.
    static constexpr std::size_t kBloomChildSlots = 6;
    /// The parent region B-1 refuses to let the bloom displace.
    static constexpr std::size_t kMinParentSlots = 8;
    /// B-1's cloud-capacity floor. Below it the FLOOR wins: the top
    /// numChildSlots() slots sit above HarmonicCloud::getActivePartialCount()
    /// and the bloom goes inaudible, while the parent spectrum is never
    /// displaced. That direction is deliberate - capacity == activeCount with no
    /// floor drives reserveBase() to 0 at activeCount <= 6 and DELETES the
    /// parents.
    static constexpr std::size_t kMinCloudCapacity = kBloomChildSlots + kMinParentSlots;

    // --- noise decorrelation (FR-015, B-3) ----------------------------------
    /// Second-order all-pass coefficient for the left branch.
    static constexpr float kNoiseApCoeffL = 0.6923878f;
    /// Second-order all-pass coefficient for the right branch. The pair is flat
    /// PER CHANNEL by construction; the MONO SUM is bounded, not flat (B-3).
    ///
    /// THE MEASURED FIGURE, evaluated from these two coefficients and the
    /// one-sample R-branch delay over [20 Hz, 8 kHz] at 48 kHz: the mono FOLD,
    /// (L + R) / 2, falls monotonically from 0.00 dB at DC to -3.66 dB at 8 kHz.
    /// The dip IS the decorrelation - at 8 kHz the branch phase difference has
    /// reached about 98 degrees, i.e. near quadrature, and 2|cos(dphi/2)| is 0.66
    /// there. `VoragoVoice_NoiseDecorrelationMonoSum` prints the measured worst
    /// deviation on every run.
    ///
    /// A BARE ONE-SAMPLE-DELAY PAIR (no all-passes) deviates only 1.25 dB over
    /// the same band, because inside it that pair barely decorrelates at all: its
    /// comb null sits at Nyquist, OUTSIDE [20 Hz, 8 kHz]. The comb-filtering a
    /// delay decorrelator is criticised for belongs to a FRACTIONAL / multi-
    /// millisecond delay, not to a unit delay - so a one-sample delay is not a
    /// comparator this pair can beat on mono-fold flatness, and "flatter mono
    /// fold" and "more decorrelation" pull in opposite directions by
    /// construction.
    static constexpr float kNoiseApCoeffR = 0.4021921f;

    // --- blend (FR-036, FR-037) ---------------------------------------------
    /// The library-wide gain-ramp time (noise_organism.h:178).
    static constexpr float kBlendRampMs = 50.0f;

    // --- seed salts (FR-025); pairwise distinct, asserted just below ---------
    static constexpr std::size_t kCloudSalt = 0x0100;
    static constexpr std::size_t kNoiseSalt = 0x0200;
    static constexpr std::size_t kResonanceSalt = 0x0300;
    static constexpr std::size_t kEcologySalt = 0x0400;
    static constexpr std::size_t kBloomSalt = 0x0500;
    static constexpr std::size_t kEcosystemSalt = 0x0600;
    static constexpr std::size_t kBodyASalt = 0x0700;
    static constexpr std::size_t kBodyBSalt = 0x0800;
    static constexpr std::size_t kBreathSalt = 0x0900;
    static constexpr std::size_t kTideSalt = 0x0A00;
    /// + scheduler index.
    static constexpr std::size_t kSchedSaltBase = 0x0B00;
    /// + scheduler index. FR-022's seeded slot draw.
    static constexpr std::size_t kSlotDrawSaltBase = 0x0C00;

    static_assert(kCloudSalt != kNoiseSalt && kNoiseSalt != kResonanceSalt
                      && kResonanceSalt != kEcologySalt && kEcologySalt != kBloomSalt
                      && kBloomSalt != kEcosystemSalt && kEcosystemSalt != kBodyASalt
                      && kBodyASalt != kBodyBSalt && kBodyBSalt != kBreathSalt
                      && kBreathSalt != kTideSalt && kTideSalt != kSchedSaltBase
                      && kSchedSaltBase != kSlotDrawSaltBase,
                  "FR-025: the twelve salts must be pairwise distinct");
    static_assert(kSchedSaltBase + kNumEventSchedulers <= kSlotDrawSaltBase,
                  "FR-025: the scheduler salt RANGE must not overlap the slot-draw range");
    /// FR-025 / FR-045. VoragoEngine::kVoiceSaltBase is 0xA000 (plan S6.2,
    /// tasks.md T014) and is written here as a literal because vorago_engine.h
    /// includes THIS header, not the other way round. A T014 that changes the
    /// engine constant must change this literal in the same commit.
    static_assert(kSlotDrawSaltBase + kNumEventSchedulers < 0xA000u,
                  "FR-045: the voice salt range must sit below VoragoEngine::kVoiceSaltBase");

    /// FR-002's ownership guard, asserted just below the class.
    ///
    /// MEASURED, not guessed: `sizeof(VoragoVoice)` is **117 920 B** (clang 20,
    /// -std=c++20 -O1, x86_64-pc-windows-msvc, repo headers, this class as
    /// shipped; alignof is 32). The bound is ceil(117 920 x 1.05) = 123 816
    /// rounded up to the next 64 B = 123 840 - the
    /// seraphis_voice.h:186-204 rule. A padding difference between
    /// toolchains therefore does not turn a size guard into a build break, while
    /// any of the members FR-002 forbids - ModulationEngine, VoiceModRouter,
    /// PolySynthEngine, SynthVoice, CavernVerb, SubharmonicEngine, SpectralSmear,
    /// AtmosphereEngine - still blows it.
    static constexpr std::size_t kVoiceSizeBound = 123840;

    // =========================================================================
    // Construction
    // =========================================================================

    VoragoVoice() noexcept = default;

    // NON-COPYABLE AND NON-MOVABLE, STATED rather than silently produced.
    // ContinuousBody user-declares a deleted copy constructor and NO move
    // members (continuous_body.h:1100-1101), which suppresses its implicit move;
    // overload resolution then selects the deleted copy ctor. `= default`ed move
    // members here would therefore be DEFINED AS DELETED while reading as if the
    // type were movable. Nothing in Phase 10 moves a voice -
    // std::array<VoragoVoice, kMaxVoices> does not require it.
    VoragoVoice(const VoragoVoice&) = delete;
    VoragoVoice& operator=(const VoragoVoice&) = delete;
    VoragoVoice(VoragoVoice&&) = delete;
    VoragoVoice& operator=(VoragoVoice&&) = delete;

    // =========================================================================
    // Lifecycle (FR-003, FR-005, FR-006)
    // =========================================================================

    /// @brief FR-003. The ONLY allocating path; not real-time safe.
    ///
    /// The numbered order below is plan S3.1 and is LOAD-BEARING:
    ///   1. rate floor;
    ///   2. clamp the config, never reject;
    ///   3. sub-component prepare, designated initialisers throughout;
    ///   4. applySeeds() BEFORE the first note - ContinuousBody::setSeed is
    ///      configure-time only and deliberately not retro-deterministic
    ///      (continuous_body.h:1580-1589);
    ///   5. the FR-090 default table in full, every row including the unchanged
    ///      ones, so the table IS the code;
    ///   6. the agent -> destination deal (T013's assignAgentSlots());
    ///   7. derived constants;
    ///   8. prepared_ = true; reset().
    ///
    /// prepare() MAY BE CALLED REPEATEDLY (FR-077). Configuration - seeds, every
    /// setter value, the macro bases - survives, because every owner re-derives
    /// STATE, NEVER CONFIGURATION (ecosystem_engine.h:330-335). The one exception
    /// is NoiseOrganism, whose prepare() is documented to restore its own
    /// defaults (noise_organism.h:219-221); step 5 therefore runs on EVERY
    /// prepare(), which is what makes this class's contract uniform.
    ///
    /// FR-076: a NaN, zero or negative sample rate is SUBSTITUTED and then
    /// FLOORED - never rejected. isPrepared() is true afterwards in all three
    /// cases and a subsequent render is finite and bounded.
    void prepare(double sampleRate, const VoragoVoiceConfig& cfg) noexcept {
        // --- 1. rate floor (the seraphis_voice.h:238 idiom) ------------------
        // NaN fails `> 1.0` and lands on 1.0. Each owner floors AGAIN at its own
        // kMinUsableSampleRate = 8000.0 (ecosystem_engine.h:201,
        // bloom_engine.h:229, feedback_ecology.h:434,
        // resonance_drift_network.h:281) - FR-076's "substituted, then floored".
        sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0;

        // --- 2. clamp the config, never reject (FR-004) ----------------------
        const std::size_t maxBlock =
            std::clamp(cfg.maxBlockSamples, std::size_t{1}, kMaxBlockSamples);
        const std::size_t numNoise =
            std::clamp(cfg.numNoiseSources, std::size_t{1}, NoiseOrganism::kMaxSources);
        const std::size_t numPeaks =
            std::clamp(cfg.numResonancePeaks, std::size_t{1}, ResonanceDriftNetwork::kMaxPeaks);
        const std::size_t numLoops =
            std::clamp(cfg.numEcologyLoops, std::size_t{1}, FeedbackEcology::kMaxLoops);
        const std::size_t agents = std::clamp(cfg.ecosystemAgents, EcosystemEngine::kMinAgents,
                                              EcosystemEngine::kMaxAgents);
        const std::size_t cells =
            std::clamp(cfg.ecosystemCells, std::size_t{1}, EcosystemEngine::kMaxResourceCells);
        const std::size_t stepChunks =
            std::clamp(cfg.ecosystemStepChunks, EcosystemEngine::kMinStepIntervalChunks,
                       EcosystemEngine::kMaxStepIntervalChunks);
        const std::size_t childSlots =
            std::clamp(cfg.bloomChildSlots, std::size_t{0}, BloomEngine::kMaxChildren);
        const float combMs =
            std::clamp(detail::isFinite(cfg.maxCombDelayMs) ? cfg.maxCombDelayMs : 50.0f, 5.0f,
                       200.0f);

        // --- 3. sub-component prepare, designated initialisers throughout -----
        // No positional brace init anywhere: Clang errors on narrowing where
        // MSVC does not (ecosystem_engine.h:290-301).
        cloud_.prepare(sampleRate_);  // harmonic_cloud.h:282
        noise_.prepare(sampleRate_, NoiseOrganism::PrepareConfig{.maxBlockSamples = maxBlock,
                                                                 .maxCombDelayMs = combMs,
                                                                 .numSources = numNoise});
        resonance_.prepare(sampleRate_, ResonanceDriftNetwork::PrepareConfig{
                                            .maxBlockSamples = maxBlock, .numPeaks = numPeaks});
        ecology_.prepare(sampleRate_, FeedbackEcology::PrepareConfig{.maxBlockSamples = maxBlock,
                                                                     .numLoops = numLoops});
        // B-1 raises the bloom's capacity per control chunk; this is only the
        // initial value (bloom_engine.h:338-356).
        bloom_.prepare(sampleRate_, BloomEngine::PrepareConfig{.capacity = kMinCloudCapacity,
                                                               .numChildSlots = childSlots});
        ecosystem_.prepare(sampleRate_, EcosystemEngine::PrepareConfig{
                                            .agentCount = agents,
                                            .resourceCells = cells,
                                            .energyBudget = 1.0,
                                            .initialPoolFraction = 0.5,
                                            .stepIntervalChunks = stepChunks});
        for (auto& b : bodies_) {
            b.prepare(sampleRate_);  // continuous_body.h:1113
        }
        mse_.prepare(static_cast<float>(sampleRate_));  // :73 takes FLOAT, not double
        growth_.prepare(sampleRate_);                   // growth_envelope.h:117
        for (auto& s : sched_) {
            s.prepare(sampleRate_);  // slow_event_scheduler.h:207
        }
        breath_.prepare(sampleRate_);  // breathing_modulator.h:144
        tide_.prepare(sampleRate_);    // tidal_modulator.h:169

        // --- 4. seeds, BEFORE the first note ---------------------------------
        applySeeds();

        // --- 5. the FR-090 default table (plan S8.2), IN FULL ------------------
        // Every row, including the ones that adopt the component default - the
        // table IS the code (the seraphis_voice.h:263-333 shape).

        // Harmonic cloud. setRichness goes through the voice's own setter so the
        // B-2 shadow cloudRichness_ (the ONE source of the amplitude exponent
        // p(r)) can never disagree with the component.
        setRichness(0.70f);          // component 1.0 (its clamp max): N(0.70) = 18 partials
        setSpectralTiltDb(-4.0f);    // component 0.0; also mirrors onto bloom_ (FR-012)
        setMutation(0.15f);          // component 0.0 - the zero-travel trap
        setInharmonicity(0.015f);    // component 0.0 (its floor)
        cloud_.setSpectralGravity(0.10f);   // component 0.0; NOT a Gravity-macro target (Q4)
        setDriftDepthCents(8.0f);    // component 0.0
        setStereoSpread(0.45f);      // component 0.0
        cloud_.setAttackTimeSec(0.05f);     // (unchanged) - the component floor
        cloud_.setDecayTimeSec(8.0f);       // component 0.5

        // Noise organism. NoiseOrganism::prepare() restores its OWN defaults
        // (:219-221), which is exactly why this block runs on every prepare().
        noise_.setNumSources(numNoise);
        // Phase 12 (plan S2.3): the forwarder shadows go back with the organism.
        combTuningSet_.fill(false);
        combHzReq_.fill(0.0f);
        combSpreadReq_.fill(0.0f);
        combFeedbackLatched_.fill(false);
        noise_.setSourceModel(0, NoiseOrganismModel::FilteredWind);
        noise_.setSourceModel(1, NoiseOrganismModel::GranularDust);
        noise_.setSourceModel(2, NoiseOrganismModel::Direct);
        noise_.setSourceModel(3, NoiseOrganismModel::MetallicHiss);
        setNoiseLevelDb(-18.0f);     // component -12.0: the noise is a bed, not a layer
        setNoiseWanderRate(0.03f);   // (unchanged) kDefaultWanderRateHz
        setNoiseWakeBase(0.35f);     // FR-021's per-destination base (voice-owned)

        // Resonance drift network.
        resonance_.setAnchorMode(ResonanceDriftNetwork::AnchorMode::Hybrid);  // Free -> Hybrid:
                                     // the only mode that consumes setGravity (FR-016 / Q4)
        resonance_.setNumPeaks(numPeaks);
        setResonanceGravity(0.0f);   // (unchanged) - the base FR-016's two lanes sum onto
        setResonanceMix(0.45f);
        resonance_.setWetGain(ResonanceDriftNetwork::kDefaultWetGainDb);  // (unchanged) 34.5 dB
        for (std::size_t p = 0; p < ResonanceDriftNetwork::kMaxPeaks; ++p) {
            resonance_.setPeakLevel(p, -9.0f);
            resonance_.setFreqWander(p, 1.5f);
        }
        setResonanceWanderRate(0.03f);  // (unchanged); Movement's row moves it
        peakWakeBase_.fill(0.50f);      // half the peaks awake at the neutral

        // Feedback ecology.
        ecology_.setNumLoops(numLoops);
        setEcologyMix(FeedbackEcology::kDefaultMix);            // (unchanged) 0.15
        setEcologyLoopGain(FeedbackEcology::kDefaultLoopGain);  // (unchanged) 0.72
        // 3x the component's kDefaultCoupling (0.04) on the neighbour ring.
        // Phase 5 measured cross-loop interaction at about -84 dB at the default
        // voicing (roadmap lines 276-278); 0.12 is Phase 10's answer, recorded
        // here rather than pretended about.
        for (std::size_t l = 0; l < numLoops; ++l) {
            ecology_.setCoupling(l, (l + 1u) % numLoops, 0.12f);
        }
        loopWakeBase_.fill(0.50f);

        // Bloom engine.
        setBloomDepth(0.60f);        // component 1.0; Density's row moves it up
        setBloomSpawnRateHz(BloomEngine::kDefaultSpawnRateHz);  // (unchanged) 1/240
        bloom_.setFadeInSeconds(45.0f);    // (unchanged) kDefaultFadeInSeconds
        bloom_.setHoldSeconds(120.0f);     // (unchanged) kDefaultHoldSeconds
        bloom_.setFadeOutSeconds(180.0f);  // (unchanged) kDefaultFadeOutSeconds
        // FR-012: the consumer tilt mirrors the cloud's tilt on every step
        // either changes. setSpectralTiltDb above already pushed it; restated
        // here so the table is complete.
        bloom_.setConsumerTiltDb(cloud_.getSpectralTiltDb());

        // Ecosystem routing depth (voice-side; FR-021).
        // Ruled 2026-09-19 (spec Q-J): 0.50f -> 0.85f. SC-005 read a centroid CV
        // of 0.031 over the 8 h soak against the 0.05 bar, and of every FR-090
        // travel lever probed this is the one that moves it (8 h unaccelerated
        // harness renders on this tree: 0.044 at 0.50 (seed 1), 0.050 at 0.85
        // and 0.053 at 1.0 (three-seed means); drift, breathing, tidal, wander
        // rates, bloom depth/rate, blur and ghost each moved a 2.5 h render by
        // < 0.004).
        // 0.85 is the CEILING that keeps the Partial lane honest: the cloud
        // mutation is clamp(mutationBase_ + depth * agentOutput) with the base
        // at 0.15 and the output in [0, 1], so at 1.0 the neutral drone pinned
        // mutation at the clamp (1.0) and SC-008's Entropy clause read 1 -> 1.
        // Life's row keeps 0.15 of travel to the same endpoint.
        setEcosystemDepth(0.85f);    // was 0.50f; Life's row moves it to 1.0

        // The two bodies. A is the room, B is the object; the pair spans the
        // darkness axis without being the two extremes.
        setBodyMaterialA(ContinuousBody::BodyMaterial::StoneChamber);  // component Glass
        setBodyMaterialB(ContinuousBody::BodyMaterial::SteelTank);     // component Glass
        setBodyResonance(ContinuousBody::kDefaultResonance);  // (unchanged) 0.70
        setBodyDamping(0.25f);       // component 0.0 (its floor) - the zero-travel fix
        setBodyMix(ContinuousBody::kDefaultMix);  // (unchanged) 1.00 - fully wet
        for (auto& b : bodies_) {
            b.setCloudMix(ContinuousBody::kDefaultCloudMix);  // (unchanged) 0.25
            b.setCloudDecaySec(20.0f);                        // component 4.0
            b.setWidth(ContinuousBody::kDefaultWidth);        // (unchanged) 1.00
        }
        setBodyBlend(0.35f);         // biased toward body A (the room)
        blend_.snapTo(0.35f);        // no 50 ms window at t = 0

        // The two event schedulers (FR-022, Q7).
        // The two ranges at scale 1.0. publishIdentity() re-writes them scaled by
        // eventRateScale_ every control step (applyEventRateScale), which is what
        // makes the voice the single owner of both ranges.
        setEventRateScale(1.0f);
        applyEventRateScale();  // fast 20-90 s, slow 180-600 s (600 s is kMaxIntervalSeconds)
        for (auto& s : sched_) {
            s.setEnvelopeTimes(8.0f, 20.0f, 30.0f);   // a wake is a swell, not a trigger
            s.setDepthRange(0.4f, 1.0f);
            s.setBipolarProbability(0.0f);            // every event is a POSITIVE wake
            s.setTargetCount(static_cast<std::uint8_t>(kNumEventFamilies));
        }

        // The two life modulators (FR-026).
        breath_.setRate(0.017f);     // about one breath per minute
        setBreathingDepth(0.30f);    // the Gravity lane's swing
        setBreathingIrregularity(0.30f);
        tide_.setRate(0.25f);        // a tide inside the 30 s - 10 min range
        setTidalDepth(0.40f);

        // --- 5b. the FR-014 envelope, through the single write path -----------
        // Mode first: applyStage's Growth branch zeroes pre-sustain times, and a
        // prepare() issued while the voice was in Growth mode must still author
        // the Standard shape.
        envMode_ = EnvelopeMode::Standard;
        // Ceilings first: every setStage/setReleaseTime below clamps to them.
        mse_.setMaxStageTimeMs(kEnvelopeMaxStageTimeMs);
        growth_.setMaxDuration(kGrowthMaxDurationSeconds);
        mse_.setNumStages(kEnvelopeStages);           // multi_stage_envelope.h:135
        mse_.setSustainPoint(kEnvelopeSustainPoint);  // :178 - read by applyStage
        // applyStage's idempotence guard compares against the shadows, so the
        // six stages this prepare authors are invalidated first. Without this,
        // (a) stage 5's {0, 0} would match the zero-initialised shadow and never
        // reach mse_, and (b) a re-prepare after a Growth-mode session would find
        // the shadows already equal and silently leave mse_ holding the 0 ms
        // pre-sustain times.
        for (int st = 0; st < MultiStageEnvelope::kMaxStages; ++st) {
            const auto i = static_cast<std::size_t>(st);
            stageLevel_[i] = -1.0f;
            stageTimeMs_[i] = -1.0f;
        }
        for (int st = 0; st < kEnvelopeStages; ++st) {
            const auto i = static_cast<std::size_t>(st);
            applyStage(st, kDefaultStageLevels[i], kDefaultStageTimesMs[i]);
        }
        setEnvelopeReleaseMs(kDefaultReleaseMs);
        // EXPLICIT: the component default is RetriggerMode::Hard (:463), which
        // would restart a 20 s attack on every re-articulation.
        mse_.setRetriggerMode(RetriggerMode::Legato);  // :215
        setGrowthDurationSeconds(kDefaultGrowthDurationSeconds);  // ceiling raised above

        // --- 6. the agent -> destination deal (FR-020a; T013 fills the body) --
        assignAgentSlots();

        // --- 7. derived constants ---------------------------------------------
        levelReleaseCoeff_ =
            std::exp(-static_cast<float>(kControlChunkSamples)
                     / (0.001f * kLevelReleaseMs * static_cast<float>(sampleRate_)));
        silenceRampSamples_ = std::max(
            1, static_cast<int>(
                   std::lround(0.001f * kSilenceRampMs * static_cast<float>(sampleRate_))));
        quiescentChunksToRetire_ = std::max(
            1, static_cast<int>(std::lround(kQuiescentSeconds * sampleRate_
                                            / static_cast<double>(kControlChunkSamples))));
        noiseGain_.configure(NoiseOrganism::kGainRampMs, static_cast<float>(sampleRate_));
        blend_.configure(kBlendRampMs, static_cast<float>(sampleRate_));
        // FR-015 / B-3: two SECOND-ORDER all-passes, {b0 = a, b1 = 0, b2 = 1,
        // a1 = 0, a2 = a}. Unity magnitude per channel at every frequency; the
        // one-sample delay on the R branch (renderOneChunk step 4) is what makes
        // the pair a phase-DIFFERENCE network rather than two unrelated filters.
        noiseApL_.setCoefficients(BiquadCoefficients{.b0 = kNoiseApCoeffL,
                                                     .b1 = 0.0f,
                                                     .b2 = 1.0f,
                                                     .a1 = 0.0f,
                                                     .a2 = kNoiseApCoeffL});
        noiseApR_.setCoefficients(BiquadCoefficients{.b0 = kNoiseApCoeffR,
                                                     .b1 = 0.0f,
                                                     .b2 = 1.0f,
                                                     .a1 = 0.0f,
                                                     .a2 = kNoiseApCoeffR});

        // --- 8. -----------------------------------------------------------------
        prepared_ = true;
        reset();
    }

    /// @brief FR-005. Full reset; CLEARS the armed fade tail.
    ///
    /// **CONTROL THREAD ONLY (B-7).** This is the one entry point that reaches
    /// FeedbackEcology::reset() (:854), whose own body states "reset() is a
    /// control-thread call" (:861-862) and whose cost is quantified at
    /// :2401-2412 - a std::fill over the whole power-of-two ring per loop,
    /// ~786 KB for six loops at 48 kHz and ~3.1 MB at 192 kHz, where "a single
    /// 131 KB fill already exceeds" the control-chunk budget.
    ///
    /// Sole callers: prepare() step 8, VoragoEngine::reset() and
    /// VoragoEngine::silence(), all three of which are declared not-audio-thread.
    void reset() noexcept {
        clearRunState(/*rtSafe=*/false);
        fadeTailL_ = 0.0f;
        fadeTailR_ = 0.0f;
        fadeRemaining_ = 0;
    }

    /// @brief RT-SAFE (B-7). reset()'s twin with ecology_.silenceAudio() in
    ///        place of ecology_.reset(); CLEARS the armed fade tail.
    ///
    /// The tail is cleared because a poisoned voice must not carry a poisoned
    /// fade tail into the next note. The WIPE is dropped because
    /// clearLoopAudio()'s read-mute window makes the ring unobservable for
    /// exactly one delay length (feedback_ecology.h:2412-2425), so a non-finite
    /// sample sitting in it is never read.
    ///
    /// Sole caller: FR-072's deferred non-finite recovery.
    void resetForRecovery() noexcept {
        clearRunState(/*rtSafe=*/true);
        fadeTailL_ = 0.0f;
        fadeTailR_ = 0.0f;
        fadeRemaining_ = 0;
    }

    /// @brief RT-SAFE (B-7). Same body; PRESERVES the armed fade tail
    ///        (seraphis_voice.h:399-405).
    ///
    /// Sole caller: the engine's steal teardown, which runs silence() first.
    void resetForSteal() noexcept { clearRunState(/*rtSafe=*/true); }

    /// @brief RT-SAFE (B-7). Arm the D3 anti-click ramp, then clear run state.
    ///
    /// A steal runs silence() then resetForSteal(), so clearRunState(true)
    /// executes TWICE. At clearLoopAudio()'s O(1) that is a few hundred
    /// nanoseconds twice; routed through ecology_.reset() it would have been
    /// ~1.6 MB of std::fill per steal at 48 kHz, on the audio thread.
    void silence() noexcept {
        fadeTailL_ = lastOutL_;
        fadeTailR_ = lastOutR_;
        fadeRemaining_ = silenceRampSamples_;
        clearRunState(/*rtSafe=*/true);
        // Discard any un-served rendered audio.
        carryAvail_ = 0;
        carryRead_ = 0;
        carryIsLifeOnly_ = true;
    }

    /// @brief FR-006. THE PREPARE-TIME TOTAL of the heap this voice holds.
    ///
    /// Complete, and complete by enumeration rather than by hope: the sum below
    /// covers EVERY member declared in the State block at the bottom of this
    /// class, in two groups.
    ///
    /// OWNS HEAP, publishes its own figure:
    ///   * `noise_`      NoiseOrganism           (noise_organism.h:999)
    ///   * `ecology_`    FeedbackEcology         (feedback_ecology.h:1362)
    ///   * `bodies_[2]`  ContinuousBody          - waveguide + comb bank +
    ///                                              decay-cloud delays and
    ///                                              diffusion (continuous_body.h)
    ///   * `resonance_`  ResonanceDriftNetwork   (:912, structurally 0)
    ///   * `bloom_`      BloomEngine             (:817, structurally 0)
    ///   * `ecosystem_`  EcosystemEngine         (:1040, structurally 0)
    ///
    /// REACHES NO ALLOCATOR AT ALL, so contributes a provable zero: `cloud_`
    /// (HarmonicCloud), `mse_` (MultiStageEnvelope), `growth_` (GrowthEnvelope),
    /// `sched_` (SlowEventScheduler x2), `breath_` (BreathingModulator),
    /// `tide_` (TidalModulator), and this class's own arrays, ramps and
    /// scalars - every one of those headers declares only `std::array` and
    /// scalar members and names no `std::vector`, `new` or allocator.
    ///
    /// `ContinuousBody` was the one real gap: it holds four delay-line groups
    /// and published no figure, which is why this getter used to describe
    /// itself as incomplete. It now publishes one, so the claim above is the
    /// literal total and not a share of it.
    ///
    /// This does NOT displace SC-014's AllocationScope gate: an invariant total
    /// proves nothing was RESIZED, while AllocationScope proves nothing was
    /// allocated. Both still run.
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept {
        return noise_.getAllocatedBytes() + resonance_.getAllocatedBytes()
               + ecology_.getAllocatedBytes() + bloom_.getAllocatedBytes()
               + ecosystem_.getAllocatedBytes() + bodies_[0].getAllocatedBytes()
               + bodies_[1].getAllocatedBytes();
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Render (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Render `n` samples into the caller's stereo buffers.
    ///
    /// Guard order, and each guard's documented neutral:
    ///   - a NULL pointer on either channel: NOTHING is written and NOTHING is
    ///     advanced;
    ///   - `n == 0`: consumes NO control step;
    ///   - `!prepared_`: `n` zeros on both channels, and nothing advances.
    ///
    /// D1: the voice never renders a partial chunk. Whole 64-sample chunks are
    /// rendered on demand into carryL_/carryR_ and the caller is served out of
    /// them, so a control step lands once per 64 ELAPSED SAMPLES rather than once
    /// per call and any partition of the same total is bit-identical (FR-007).
    void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept {
        if (outL == nullptr || outR == nullptr) {
            return;
        }
        if (n == 0) {
            return;
        }
        if (!prepared_) {
            std::fill_n(outL, n, 0.0f);
            std::fill_n(outR, n, 0.0f);
            return;
        }

        std::size_t done = 0;
        while (done < n) {
            if (carryAvail_ == 0) {
                renderOneChunk();  // always exactly kControlChunkSamples
            }
            const std::size_t take = std::min(n - done, carryAvail_);
            std::copy_n(carryL_.data() + carryRead_, take, outL + done);
            std::copy_n(carryR_.data() + carryRead_, take, outR + done);
            // D4: captured at SERVE time.
            lastOutL_ = outL[done + take - 1];
            lastOutR_ = outR[done + take - 1];
            carryRead_ += take;
            carryAvail_ -= take;
            done += take;
        }
    }

    /// @brief Advance the identity layer and the life modulators for `n` samples
    ///        WITHOUT rendering, on the SAME carry clock processStereoBlock uses.
    ///
    /// SC-030's invariant - equal EcosystemEngine::getControlStepCount() and
    /// equal scheduler event counts across a rendering and a life-only advance of
    /// the same length - is a property of step 1 of renderOneChunk(), and step 1
    /// is exactly what the two paths share.
    void advanceLifeOnly(std::size_t n) noexcept {
        if (n == 0 || !prepared_) {
            return;
        }
        std::size_t done = 0;
        while (done < n) {
            if (carryAvail_ == 0) {
                advanceOneChunkLifeOnly();
            }
            const std::size_t take = std::min(n - done, carryAvail_);
            carryRead_ += take;
            carryAvail_ -= take;
            done += take;
        }
    }

    // =========================================================================
    // Notes (FR-013)
    // =========================================================================

    /// @brief Retune the cloud, both bodies and the resonance network, and gate
    ///        the envelope.
    ///
    /// THE THREE FREQUENCY CLAMPS DIFFER DELIBERATELY, and are documented rather
    /// than repaired: ContinuousBody clamps to [20, 8000]
    /// (continuous_body.h:138-139), HarmonicCloud to [20, 4000]
    /// (harmonic_cloud.h:184-185) and ResonanceDriftNetwork floors at 8 Hz
    /// (resonance_drift_network.h:243), so MIDI 127 lands at different places in
    /// the three engines.
    void noteOn(float frequencyHz, float velocity) noexcept {
        hasSounded_ = true;
        renderedSinceNoteOn_ = false;
        if (carryIsLifeOnly_) {
            // Drop the zero-filled IDLE carry so the onset is sample-accurate. A
            // LIVE retrigger's carry is real program material and is KEPT:
            // dropping it would skip up to 63 rendered samples and create exactly
            // the click SC-011 measures.
            carryAvail_ = 0;
            carryRead_ = 0;
        }
        velocity_ = std::clamp(detail::isFinite(velocity) ? velocity : 0.0f, 0.0f, 1.0f);
        cloud_.setFundamentalHz(frequencyHz);  // harmonic_cloud.h:383
        cloud_.noteOn();                       // :635 - redraws phases only when quiescent
        bodies_[0].setNoteFrequencyHz(frequencyHz);  // continuous_body.h:1436
        bodies_[1].setNoteFrequencyHz(frequencyHz);
        resonance_.setNoteFrequency(frequencyHz);  // resonance_drift_network.h:624
        mse_.gate(true);                           // multi_stage_envelope.h:99
        if (envMode_ == EnvelopeMode::Growth) {
            growth_.trigger();  // growth_envelope.h:161 - no-op while Rising
        }
    }

    /// @brief Release the excitation only (AR-5, D3).
    ///
    /// The bodies, the ecology, the resonance network and the identity layer keep
    /// running - they ARE the tail. This is `cloud_.noteOff(); mse_.gate(false);`
    /// and NOTHING else, deliberately.
    void noteOff() noexcept {
        cloud_.noteOff();  // harmonic_cloud.h:663
        mse_.gate(false);
    }

    /// FR-013. The voice has been quiescent for kQuiescentSeconds.
    ///
    /// At -90 dBFS with a ten-second counter the 100 ms detector release is NOT
    /// the hysteresis - the COUNTER is. Nobody later should "tune"
    /// kLevelReleaseMs expecting it to matter.
    [[nodiscard]] bool isFinished() const noexcept {
        return quiescentChunks_ >= quiescentChunksToRetire_;
    }
    [[nodiscard]] float getCurrentLevel() const noexcept { return level_; }
    [[nodiscard]] bool hasRenderedSinceNoteOn() const noexcept { return renderedSinceNoteOn_; }
    /// "Prepared but not currently sounding" - the configure-time predicate.
    [[nodiscard]] bool isConfigurable() const noexcept { return !hasSounded_ || isFinished(); }

    // =========================================================================
    // Seeding (FR-025)
    // =========================================================================

    /// @brief Set the voice seed and re-derive every sub-stream immediately.
    ///
    /// Seed 0 is LEGAL: deriveStreamSeed substitutes 0x2545F491 when the hash
    /// lands on 0 (core/random.h:110). ContinuousBody::setSeed is configure-time
    /// only and deliberately NOT retro-deterministic
    /// (continuous_body.h:1580-1589), so callers seed BEFORE the first note.
    /// The agent -> slot deal is RE-COMPUTED here, not only at prepare():
    /// EcosystemEngine::setSeed() calls its own reset() (ecosystem_engine.h:404-407)
    /// and reset() re-runs the stratified deal plus the Fisher-Yates shuffle
    /// (:2167-2212), so a new seed gives every agent a NEW kind. A deal left
    /// standing would route Noise-kind agents into peak slots.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        if (prepared_) {
            applySeeds();
            assignAgentSlots();
        }
    }
    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }

    // =========================================================================
    // The identity layer's published quantities (FR-020b, FR-026)
    // =========================================================================

    /// FR-020b. The ghost-burst request this voice publishes for the ENGINE to
    /// fold into AtmosphereEngine::setLevel. Held between control steps.
    /// publishIdentity() (T013) is the only writer.
    [[nodiscard]] float getGhostRequest() const noexcept { return ghostRequest_; }

    /// FR-026 lane 2. The fog depth this voice publishes for the engine to fold
    /// onto its own smear base. EXACTLY `max(0, tide_.getCurrentValue())` - a
    /// NET, not a fold, so a tide trough cannot pull fog BELOW the engine's base,
    /// and exactly 0.0f at tidal depth 0. There is NO second depth factor:
    /// TidalModulator already scales by its own depth inside the component
    /// (tidal_modulator.h:310-322). The voice never touches SpectralSmear - it
    /// does not own one (FR-002, FR-052).
    [[nodiscard]] float getTidalFogDepth() const noexcept { return tidalFogDepth_; }

    /// FR-026 lane 1. EXACTLY `breath_.getCurrentValue()`, the SIGNED term the
    /// voice adds to gravityBase_ before writing
    /// ResonanceDriftNetwork::setGravity each control step. Again no second depth
    /// factor - BreathingModulator returns `clamp(depth_ * bipolar, -1, 1)`
    /// (breathing_modulator.h:272-290, read back at :222). NOT
    /// getBreathingDepth(), which is FR-071's read-back of the CONFIGURED depth:
    /// deliberately different names for deliberately different things.
    [[nodiscard]] float getBreathingGravityLane() const noexcept { return breathGravityLane_; }

    /// @brief FR-023, THE COMBINE RULE - stated once, evaluated nowhere else.
    ///
    /// Where a `SlowEventScheduler` and the `EcosystemEngine` address the SAME
    /// destination, the two contributions combine by the MAXIMUM, so neither can
    /// SILENCE a slot the other woke. Two consequences fall straight out of the
    /// expression rather than out of five call sites:
    ///   - with both contributions at 0 the destination reads EXACTLY its
    ///     configured base, which is FR-021's "at depth 0 the ecosystem changes
    ///     nothing";
    ///   - the result can never sit BELOW the base, so the identity layer is a
    ///     wake-only lane and FR-024's dormancy stays the components' own.
    ///
    /// Public and static because SC-019a enumerates it over a (scheduler,
    /// ecosystem) table with no render at all.
    [[nodiscard]] static constexpr float combineWake(float base, float eco,
                                                     float sched) noexcept {
        return std::max(base, std::max(eco, sched));
    }

    // =========================================================================
    // Envelope (FR-014, FR-014a)
    // =========================================================================

    /// Growth mode forces EVERY stage from 0 up to sustainPoint-1 to 0 ms,
    /// preserving level and curve. Zeroing stage 0 alone is not enough:
    /// advanceToNextStage() only enters Sustaining when
    /// currentStage_ == sustainPoint_ (multi_stage_envelope.h:384-387), so the
    /// three body stages would still shape the composite.
    void setEnvelopeMode(EnvelopeMode mode) noexcept {
        if (mode == envMode_) {
            return;
        }
        envMode_ = mode;
        const int sustain = mse_.getSustainPoint();
        for (int st = 0; st < sustain; ++st) {
            const auto i = static_cast<std::size_t>(st);
            const float ms = (mode == EnvelopeMode::Growth) ? 0.0f : stageTimeMs_[i];
            mse_.setStage(st, stageLevel_[i], ms, kStageCurve);
        }
    }

    /// The ONE write path into mse_'s stage configuration. The shadow always
    /// takes the caller's value, so the getter reads back what was set even in
    /// Growth mode, where the pre-sustain time is stored but not applied.
    void setEnvelopeStageTimeMs(int stage, float ms) noexcept {
        if (stage < 0 || stage >= MultiStageEnvelope::kMaxStages) {
            return;
        }
        if (!detail::isFinite(ms)) {
            return;  // FR-071: rejected, the previous value stands
        }
        applyStage(stage, stageLevel_[static_cast<std::size_t>(stage)], ms);
    }

    void setEnvelopeReleaseMs(float ms) noexcept {
        if (!detail::isFinite(ms)) {
            return;  // FR-071: rejected, the previous value stands
        }
        releaseMs_ = ms;
        mse_.setReleaseTime(ms);  // clamps at the ceiling prepare() raised
    }

    void setGrowthDurationSeconds(float seconds) noexcept {
        if (!detail::isFinite(seconds)) {
            return;  // FR-071: rejected, the previous value stands
        }
        growth_.setDuration(seconds);  // clamps [1, kGrowthMaxDurationSeconds]
    }

    [[nodiscard]] EnvelopeMode getEnvelopeMode() const noexcept { return envMode_; }
    [[nodiscard]] float getEnvelopeStageTimeMs(int stage) const noexcept {
        if (stage < 0 || stage >= MultiStageEnvelope::kMaxStages) {
            return 0.0f;
        }
        return stageTimeMs_[static_cast<std::size_t>(stage)];
    }
    [[nodiscard]] float getEnvelopeStageLevel(int stage) const noexcept {
        if (stage < 0 || stage >= MultiStageEnvelope::kMaxStages) {
            return 0.0f;
        }
        return stageLevel_[static_cast<std::size_t>(stage)];
    }
    [[nodiscard]] float getEnvelopeReleaseMs() const noexcept { return releaseMs_; }
    [[nodiscard]] float getGrowthDurationSeconds() const noexcept { return growth_.getDuration(); }
    /// The composite envelope gain actually applied to the EXCITATION bus.
    [[nodiscard]] float getEnvelopeOutput() const noexcept { return envOutput_; }

    // =========================================================================
    // The macro-writable surface (S7's Voice-owned targets)
    // =========================================================================
    // Each is a one-to-one forwarder onto a shipped setter EXCEPT where the
    // comment says it fans out. NONE adds clamping of its own: the owner already
    // clamps and a second guard would only let the two surfaces disagree
    // (seraphis_voice.h:641-647). Every setter has a matching getter (FR-071),
    // and the getter reads the OWNER wherever the owner owns the value.
    //
    // FR-071's NON-FINITE RULE IS NOT CLAMPING, and is therefore not covered by
    // the paragraph above: "a non-finite argument is rejected, the previous
    // value stands" (spec FR-071, the rule bloom_engine.h:524-529 states
    // verbatim). A guard is written HERE only where the owner does not already
    // reject. MEASURED, not assumed (the SC-028 table, one run over every row):
    // BreathingModulator, TidalModulator and GrowthEnvelope clamp a NaN straight
    // through, and ContinuousBody::setResonance / setDamping / setMix
    // (continuous_body.h:1409, :1418, :1457) plus NoiseOrganism::setSourceLevel /
    // setWanderRate (noise_organism.h:493, :782) SUBSTITUTE THEIR OWN DEFAULT -
    // which moves the value rather than holding it, so a guard is needed there
    // too. It is written here and not in those components because both are
    // Seraphis-shipped and AR-4 / SC-016 bind this phase to an append-only
    // ContinuousBody change. Where the owner really does reject (HarmonicCloud,
    // ResonanceDriftNetwork::setMix / setWanderRate, FeedbackEcology, BloomEngine)
    // the forwarder stays a bare forward and the two surfaces cannot disagree. Rejecting NEVER substitutes a default: a setter that
    // wrote its default on a NaN would silently move the value, which is the
    // defect SC-028 exists to catch.

    /// ALSO updates cloudRichness_, B-2's shadow and the ONE source of the
    /// amplitude exponent p(r) that updateSpectrumTarget() evaluates. The shadow
    /// takes the COMPONENT's clamped value, never the caller's raw one.
    void setRichness(float r) noexcept {
        cloud_.setRichness(r);
        cloudRichness_ = cloud_.getRichness();
    }
    [[nodiscard]] float getRichness() const noexcept { return cloud_.getRichness(); }

    /// ALSO pushes BloomEngine::setConsumerTiltDb (FR-012) - the two must track
    /// on every step either changes.
    void setSpectralTiltDb(float dbPerOct) noexcept {
        cloud_.setSpectralTiltDb(dbPerOct);
        cloudTiltDb_ = cloud_.getSpectralTiltDb();
        bloom_.setConsumerTiltDb(cloudTiltDb_);
    }
    [[nodiscard]] float getSpectralTiltDb() const noexcept { return cloud_.getSpectralTiltDb(); }

    /// ALSO updates mutationBase_, the base FR-020's identity lane sums onto.
    void setMutation(float m) noexcept {
        cloud_.setMutation(m);
        mutationBase_ = cloud_.getMutation();
    }
    [[nodiscard]] float getMutation() const noexcept { return mutationBase_; }

    void setInharmonicity(float B) noexcept { cloud_.setInharmonicity(B); }
    [[nodiscard]] float getInharmonicity() const noexcept { return cloud_.getInharmonicity(); }

    void setDriftDepthCents(float cents) noexcept { cloud_.setDriftDepthCents(cents); }
    [[nodiscard]] float getDriftDepthCents() const noexcept {
        return cloud_.getDriftDepthCents();
    }

    void setStereoSpread(float s) noexcept { cloud_.setStereoSpread(s); }
    [[nodiscard]] float getStereoSpread() const noexcept { return cloud_.getStereoSpread(); }

    // --- Vorago Phase 12 (FR-004): seven parameter forwarders ----------------
    // A repeated identical broadcast must arm no duck, restart no ramp and not
    // re-mark anchors dirty (SC-023 (1)). Where the owner already rejects
    // non-finite input and early-outs an unchanged write the forward is bare;
    // elsewhere the guard lives here (plan S2.3).

    /// Owner rejects NaN/Inf and early-outs unchanged (harmonic_cloud.h:478-488).
    void setCloudSpectralGravity(float g) noexcept { cloud_.setSpectralGravity(g); }

    /// Owner: invalid slot is a no-op; an unchanged goal is a full no-op with no
    /// duck (noise_organism.h:463-471, :1703-1735).
    void setNoiseSourceModel(std::size_t slot, NoiseOrganismModel m) noexcept {
        noise_.setSourceModel(slot, m);
    }

    /// Same owner rule as setNoiseSourceModel (noise_organism.h:478-484).
    void setNoiseSourceType(std::size_t slot, NoiseType t) noexcept {
        noise_.setSourceNoiseType(slot, t);
    }

    /// The owner SUBSTITUTES its defaults for a non-finite argument
    /// (noise_organism.h:573-575), so the voice rejects the whole call instead.
    /// The early-out compares against the last REQUEST, because the owner's
    /// getter reports a sample-rate-clamped value.
    void setNoiseCombTuning(std::size_t slot, float hz, float spread) noexcept {
        if (slot >= NoiseOrganism::kMaxSources) {
            return;
        }
        if (!detail::isFinite(hz) || !detail::isFinite(spread)) {
            return;  // FR-071: rejected, the previous value stands
        }
        if (combTuningSet_[slot] && hz == combHzReq_[slot] && spread == combSpreadReq_[slot]) {
            return;
        }
        combTuningSet_[slot] = true;
        combHzReq_[slot] = hz;
        combSpreadReq_[slot] = spread;
        noise_.setCombTuning(slot, hz, spread);
    }

    /// LATCH-AWARE early-out (C-4): the FIRST push always reaches the owner, even
    /// at the value already running, because only the owner's write latches the
    /// slot against a later model change re-deriving its feedback
    /// (noise_organism.h:579-598).
    void setNoiseCombFeedback(std::size_t slot, float fb) noexcept {
        if (slot >= NoiseOrganism::kMaxSources) {
            return;
        }
        if (!detail::isFinite(fb)) {
            return;  // FR-071: the owner would substitute its default (:594)
        }
        if (combFeedbackLatched_[slot]
            && std::clamp(fb, 0.0f, NoiseOrganism::kCombFeedbackCap)
                   == noise_.getCombFeedback(slot)) {
            return;
        }
        noise_.setCombFeedback(slot, fb);
        combFeedbackLatched_[slot] = true;
    }

    /// The owner has no early-out and re-marks anchors dirty on every write
    /// (resonance_drift_network.h:587-590).
    void setResonanceAnchorMode(ResonanceDriftNetwork::AnchorMode m) noexcept {
        if (m == resonance_.getAnchorMode()) {
            return;
        }
        resonance_.setAnchorMode(m);
    }

    /// The owner validates the enumerator but has no early-out
    /// (feedback_ecology.h:1035-1044).
    void setEcologyLoopFilterMode(std::size_t loop, FeedbackEcology::FilterMode m) noexcept {
        if (loop >= FeedbackEcology::kMaxLoops) {
            return;
        }
        if (m == ecology_.getLoopFilterMode(loop)) {
            return;
        }
        ecology_.setLoopFilterMode(loop, m);
    }

    /// FAN-OUT: every CONFIGURED source slot.
    void setNoiseLevelDb(float dB) noexcept {
        if (!detail::isFinite(dB)) {
            return;  // FR-071: rejected, the previous value stands
        }
        // FR-067: EARLY-OUT ON AN UNCHANGED VALUE. NoiseOrganism::setSourceLevel
        // RE-ARMS a LinearRamp (noise_organism.h:508-509, and LinearRamp::setTarget
        // recomputes increment = (target - current) / N on EVERY call,
        // smoother.h:340-352), so re-writing the level the component already holds
        // while that ramp is in flight stretches the glide - which is exactly the
        // step apply()-every-block must not produce. The clamp mirrors the
        // component's own (noise_organism.h:493) so the comparison is against the
        // value the component would store; a non-finite dB never reaches it.
        const float clamped =
            std::clamp(dB, NoiseGenerator::kMinLevelDb, NoiseGenerator::kMaxLevelDb);
        const std::size_t n = noise_.getNumSources();
        if (n > 0 && clamped == noise_.getSourceLevel(0)) {
            return;
        }
        for (std::size_t s = 0; s < n; ++s) {
            noise_.setSourceLevel(s, dB);
        }
    }
    [[nodiscard]] float getNoiseLevelDb() const noexcept { return noise_.getSourceLevel(0); }

    /// FR-021's per-destination wake BASE. Voice-owned: NoiseOrganism's own
    /// setSourceWake is written by publishIdentity() each control step from this
    /// base plus the ecosystem's contribution, so the base cannot be read back
    /// from the component.
    void setNoiseWakeBase(float w) noexcept {
        if (!detail::isFinite(w)) {
            return;  // FR-071: rejected, the previous value stands
        }
        noiseWakeBase_.fill(std::clamp(w, 0.0f, 1.0f));
    }
    [[nodiscard]] float getNoiseWakeBase() const noexcept { return noiseWakeBase_[0]; }

    void setNoiseWanderRate(float hz) noexcept {
        if (!detail::isFinite(hz)) {
            return;  // FR-071: rejected, the previous value stands
        }
        noise_.setWanderRate(hz);  // noise_organism.h:782 sanitise()s to a DEFAULT
    }
    [[nodiscard]] float getNoiseWanderRate() const noexcept { return noise_.getWanderRate(); }

    /// FR-016's BASE. The two summed lanes (the breathing lane and the
    /// ecosystem's) are added onto it by publishIdentity() each control step, so
    /// ResonanceDriftNetwork::getGravity() reports the SUM, not this base.
    void setResonanceGravity(float g) noexcept {
        if (!detail::isFinite(g)) {
            return;  // FR-071: rejected, the previous value stands
        }
        gravityBase_ = std::clamp(g, -1.0f, 1.0f);
        resonance_.setGravity(gravityBase_);
    }
    [[nodiscard]] float getResonanceGravity() const noexcept { return gravityBase_; }

    /// Vorago Phase 12, spec B-1: the Gravity macro's octave-lock target.
    /// A plain forward - ResonanceDriftNetwork::setOctaveLock rejects
    /// non-finite, clamps [0, 1] and early-outs an unchanged write (FR-067).
    void setResonanceOctaveLock(float lock) noexcept { resonance_.setOctaveLock(lock); }
    [[nodiscard]] float getResonanceOctaveLock() const noexcept {
        return resonance_.getOctaveLock();
    }

    void setResonanceMix(float m) noexcept { resonance_.setMix(m); }
    [[nodiscard]] float getResonanceMix() const noexcept { return resonance_.getMix(); }

    void setResonanceWanderRate(float hz) noexcept { resonance_.setWanderRate(hz); }
    [[nodiscard]] float getResonanceWanderRate() const noexcept {
        return resonance_.getWanderRate();
    }

    /// FR-067's early-out: FeedbackEcology::setMix re-arms a LinearRamp
    /// (feedback_ecology.h:1316-1318, mixRamp_ at :1683), so an unchanged write
    /// must not reach it. The clamp mirrors the owner's own (:1317); a
    /// non-finite `m` fails the finite test and is left to the owner to reject.
    void setEcologyMix(float m) noexcept {
        if (detail::isFinite(m) && std::clamp(m, 0.0f, 1.0f) == ecology_.getMix()) {
            return;
        }
        ecology_.setMix(m);
    }
    [[nodiscard]] float getEcologyMix() const noexcept { return ecology_.getMix(); }

    /// FAN-OUT: every CONFIGURED loop.
    void setEcologyLoopGain(float g) noexcept {
        const std::size_t n = ecology_.getNumLoops();
        for (std::size_t l = 0; l < n; ++l) {
            ecology_.setLoopGain(l, g);
        }
    }
    [[nodiscard]] float getEcologyLoopGain() const noexcept { return ecology_.getLoopGain(0); }

    /// FR-036 / FR-037. Advanced PER SAMPLE inside the chunk, never stepped at
    /// the chunk boundary.
    void setBodyBlend(float b) noexcept {
        if (!detail::isFinite(b)) {
            return;  // FR-071: rejected, the previous value stands
        }
        // FR-067's early-out: blend_ is a LinearRamp advanced per sample, and
        // LinearRamp::setTarget re-derives its increment from the CURRENT value
        // every call (smoother.h:340-352), so re-writing the target it already
        // carries mid-glide would stretch the glide.
        const float v = std::clamp(b, 0.0f, 1.0f);
        if (v == blend_.getTarget()) {
            return;
        }
        blend_.setTarget(v);
    }
    [[nodiscard]] float getBodyBlend() const noexcept { return blend_.getTarget(); }

    /// FAN-OUT: BOTH bodies.
    void setBodyDamping(float d) noexcept {
        if (!detail::isFinite(d)) {
            return;  // FR-071: rejected, the previous value stands
        }
        for (auto& b : bodies_) {
            b.setDamping(d);  // continuous_body.h:1418 substitutes kDefaultDamping
        }
    }
    [[nodiscard]] float getBodyDamping() const noexcept { return bodies_[0].getDamping(); }

    /// FAN-OUT: BOTH bodies.
    void setBodyResonance(float r) noexcept {
        if (!detail::isFinite(r)) {
            return;  // FR-071: rejected, the previous value stands
        }
        for (auto& b : bodies_) {
            b.setResonance(r);  // :1409 substitutes kDefaultResonance
        }
    }
    [[nodiscard]] float getBodyResonance() const noexcept { return bodies_[0].getResonance(); }

    /// FAN-OUT: BOTH bodies.
    void setBodyMix(float m) noexcept {
        if (!detail::isFinite(m)) {
            return;  // FR-071: rejected, the previous value stands
        }
        for (auto& b : bodies_) {
            b.setMix(m);  // :1457 substitutes kDefaultMix
        }
    }
    [[nodiscard]] float getBodyMix() const noexcept { return bodies_[0].getMix(); }

    void setBodyMaterialA(ContinuousBody::BodyMaterial m) noexcept { bodies_[0].setMaterial(m); }
    void setBodyMaterialB(ContinuousBody::BodyMaterial m) noexcept { bodies_[1].setMaterial(m); }
    [[nodiscard]] ContinuousBody::BodyMaterial getBodyMaterialA() const noexcept {
        return bodies_[0].getMaterial();
    }
    [[nodiscard]] ContinuousBody::BodyMaterial getBodyMaterialB() const noexcept {
        return bodies_[1].getMaterial();
    }

    /// FR-021's routing depth: how far an agent's output moves its destination's
    /// wake away from that destination's base. Voice-owned; read by
    /// publishIdentity() (T013).
    ///
    /// FR-021 WORDS IT PER DESTINATION, so the depth IS per destination: one
    /// scalar per `EcosystemEngine::Kind`, which is exactly the destination
    /// roster the routing uses (slotCountForKind(), :1560-1577 - Partial ->
    /// cloud mutation, Resonator -> peak wakes, Noise -> source wakes, Feedback
    /// -> loop wakes, Ghost -> the FR-020b ghost request). Nothing else in this
    /// class indexes a "destination".
    ///
    /// The scalar setter below writes ALL FIVE, which is how every shipped
    /// caller uses it: prepare() installs the FR-090 default (:617) and the Life
    /// macro row's `EcosystemDepth` target (vorago_macro_matrix.h:1003) moves the
    /// whole lane together. Per-destination control is the additional surface,
    /// not a replacement.
    void setEcosystemDepth(float d) noexcept {
        if (!detail::isFinite(d)) {
            return;  // FR-071: rejected, the previous value stands
        }
        ecosystemDepth_.fill(std::clamp(d, 0.0f, 1.0f));
    }

    /// @brief The depth shared by every destination.
    ///
    /// Defined as the MAXIMUM over the five destinations, so that (a) a caller
    /// that only ever uses the scalar setter reads back exactly what it wrote -
    /// which is what SC-009's `base == getter-after-prepare()` row compares - and
    /// (b) "is any destination routed at all" stays answerable from one call:
    /// this returns 0 if and only if the ecosystem changes nothing anywhere,
    /// which is FR-021's depth-0 clause.
    [[nodiscard]] float getEcosystemDepth() const noexcept {
        float m = ecosystemDepth_[0];
        for (std::size_t k = 1; k < EcosystemEngine::kNumKinds; ++k) {
            m = std::max(m, ecosystemDepth_[k]);
        }
        return m;
    }

    /// @brief FR-021. ONE destination family's routing depth.
    ///
    /// An out-of-range `dest` is a silent no-op and a non-finite `d` is rejected
    /// with the previous value standing - FR-071's uniform index/value rules.
    void setEcosystemDepthFor(EcosystemEngine::Kind dest, float d) noexcept {
        const auto k = static_cast<std::size_t>(dest);
        if (k >= EcosystemEngine::kNumKinds || !detail::isFinite(d)) {
            return;
        }
        ecosystemDepth_[k] = std::clamp(d, 0.0f, 1.0f);
    }

    /// @brief FR-021. Reads the clamp; an out-of-range `dest` reads 0 (inert).
    [[nodiscard]] float getEcosystemDepthFor(EcosystemEngine::Kind dest) const noexcept {
        const auto k = static_cast<std::size_t>(dest);
        return (k < EcosystemEngine::kNumKinds) ? ecosystemDepth_[k] : 0.0f;
    }

    /// FR-022's interval scale - Life's target. Voice-owned; APPLIED to the two
    /// SlowEventSchedulers by publishIdentity() (T013), which is the only place
    /// that owns the scheduler interval ranges.
    void setEventRateScale(float s) noexcept {
        if (!detail::isFinite(s)) {
            return;  // FR-071: rejected, the previous value stands
        }
        eventRateScale_ = std::clamp(s, 0.1f, 10.0f);
    }
    [[nodiscard]] float getEventRateScale() const noexcept { return eventRateScale_; }

    /// ALSO updates bloomDepthBase_, the base the ecosystem's Partial agents add
    /// onto each control step (FR-020).
    void setBloomDepth(float d) noexcept {
        if (!detail::isFinite(d)) {
            return;  // FR-071: rejected, the previous value stands
        }
        // FR-067's early-out: BloomEngine::setDepth re-arms a LinearRamp
        // (bloom_engine.h:537, depthRamp_ at :1614). publishIdentity() rewrites
        // that component value every control step anyway, so skipping an
        // unchanged write here changes nothing but the redundant re-arm.
        const float v = std::clamp(d, 0.0f, 1.0f);
        if (v == bloomDepthBase_) {
            return;
        }
        bloomDepthBase_ = v;
        bloom_.setDepth(bloomDepthBase_);
    }
    [[nodiscard]] float getBloomDepth() const noexcept { return bloomDepthBase_; }

    void setBloomSpawnRateHz(float hz) noexcept { bloom_.setSpawnRateHz(hz); }
    [[nodiscard]] float getBloomSpawnRateHz() const noexcept { return bloom_.getSpawnRateHz(); }

    void setBreathingDepth(float d) noexcept {
        if (!detail::isFinite(d)) {
            return;  // FR-071: rejected, the previous value stands
        }
        breath_.setDepth(d);  // breathing_modulator.h:177 clamps, it does not reject
    }
    /// FR-071's read-back of the CONFIGURED depth - NOT the lane value, which is
    /// getBreathingGravityLane().
    [[nodiscard]] float getBreathingDepth() const noexcept { return breath_.getDepth(); }

    void setBreathingIrregularity(float i) noexcept {
        if (!detail::isFinite(i)) {
            return;  // FR-071: rejected, the previous value stands
        }
        breath_.setIrregularity(i);  // same: the owner clamps a NaN through
    }
    [[nodiscard]] float getBreathingIrregularity() const noexcept {
        return breath_.getIrregularity();
    }

    void setTidalDepth(float d) noexcept {
        if (!detail::isFinite(d)) {
            return;  // FR-071: rejected, the previous value stands
        }
        tide_.setDepth(d);  // tidal_modulator.h:209 clamps, it does not reject
    }
    [[nodiscard]] float getTidalDepth() const noexcept { return tide_.getDepth(); }

    // =========================================================================
    // Sub-component read access, for tests and for the engine
    // =========================================================================

    [[nodiscard]] const HarmonicCloud& cloud() const noexcept { return cloud_; }
    [[nodiscard]] const NoiseOrganism& noise() const noexcept { return noise_; }
    [[nodiscard]] const ResonanceDriftNetwork& resonance() const noexcept { return resonance_; }
    [[nodiscard]] const FeedbackEcology& ecology() const noexcept { return ecology_; }
    [[nodiscard]] const BloomEngine& bloom() const noexcept { return bloom_; }
    [[nodiscard]] const EcosystemEngine& ecosystem() const noexcept { return ecosystem_; }
    [[nodiscard]] const ContinuousBody& bodyA() const noexcept { return bodies_[0]; }
    [[nodiscard]] const ContinuousBody& bodyB() const noexcept { return bodies_[1]; }
    [[nodiscard]] const SlowEventScheduler& scheduler(std::size_t i) const noexcept {
        return sched_[std::min(i, kNumEventSchedulers - 1u)];
    }
    [[nodiscard]] const BreathingModulator& breathing() const noexcept { return breath_; }
    [[nodiscard]] const TidalModulator& tide() const noexcept { return tide_; }
    [[nodiscard]] const GrowthEnvelope& growth() const noexcept { return growth_; }
    /// The envelope generator itself. MultiStageEnvelope publishes no stage-time
    /// getter, so `getState()` / `getCurrentStage()` are the ONLY way a test can
    /// observe that Growth mode really zeroed the pre-sustain walk rather than
    /// merely storing zeros in the voice's shadows.
    [[nodiscard]] const MultiStageEnvelope& envelope() const noexcept { return mse_; }

private:
    friend struct detail::VoragoVoiceSilenceRampProbe;  // SC-011
    friend struct detail::VoragoEngineNonFiniteProbe;   // SC-029 (B-4)
    friend struct detail::VoragoVoiceIdentityProbe;     // SC-019a / SC-019b (B-4)
    friend class VoragoEngine;                          // the engine owns its voices
    friend class VoragoMacroMatrix;                     // S7's apply() needs non-const access

    // =========================================================================
    // Seeding
    // =========================================================================

    void applySeeds() noexcept {
        cloud_.setSeed(deriveStreamSeed(seed_, kCloudSalt));  // core/random.h:102
        noise_.setSeed(deriveStreamSeed(seed_, kNoiseSalt));
        resonance_.setSeed(deriveStreamSeed(seed_, kResonanceSalt));
        ecology_.setSeed(deriveStreamSeed(seed_, kEcologySalt));
        bloom_.setSeed(deriveStreamSeed(seed_, kBloomSalt));
        ecosystem_.setSeed(deriveStreamSeed(seed_, kEcosystemSalt));
        bodies_[0].setSeed(deriveStreamSeed(seed_, kBodyASalt));
        bodies_[1].setSeed(deriveStreamSeed(seed_, kBodyBSalt));
        breath_.setSeed(deriveStreamSeed(seed_, kBreathSalt));
        tide_.setSeed(deriveStreamSeed(seed_, kTideSalt));
        for (std::size_t k = 0; k < kNumEventSchedulers; ++k) {
            sched_[k].setSeed(deriveStreamSeed(seed_, kSchedSaltBase + k));
            slotDrawRng_[k].seed(deriveStreamSeed(seed_, kSlotDrawSaltBase + k));
        }
        // GrowthEnvelope::setSeed is a documented NO-OP (growth_envelope.h:140)
        // and is deliberately not called, so it carries no salt.
    }

    // =========================================================================
    // The four clearing paths - ONE shared body, two threads (B-7)
    // =========================================================================

    /// @brief reset()'s body. `rtSafe` selects the ONE call that differs.
    ///
    /// The three RT-safe entry points differ from reset() by exactly this one
    /// call, in exactly this one place, so they cannot drift apart.
    void clearRunState(bool rtSafe) noexcept {
        // FIRST, BEFORE the component resets below, and that order is the same
        // one prepare() states for SpectralSmear and TapeSaturator: each of these
        // six destinations is a ramp TARGET in the component that owns it, and
        // each of those components snaps its ramps to the stored target inside
        // its own reset(). Written afterwards they would be targets to glide
        // toward from wherever the reset snapped; written here they are what the
        // reset snaps TO, which is the only spelling that is idempotent - and a
        // clearing path that is not idempotent is not a rewind.
        installIdentityNeutral();

        // FR-011's latch is state in TWO objects, and clearing only ours strands
        // the other. HarmonicCloud::reset() deliberately does NOT drop a spectral
        // target - it is documented as "silence all partial state WITHOUT changing
        // configuration" (harmonic_cloud.h:312-313) and treats the supplied target
        // as configuration, which is why its own recomputes are target-aware
        // (:338-352). So without this call the cloud walks out of every clearing
        // path still holding the pre-clear parent+child spectrum, while
        // `targetActive_ = false` below tells updateSpectrumTarget() it has nothing
        // to clear: the `else if (targetActive_)` arm can then NEVER fire and the
        // stale target stands for the rest of the voice's life - the top
        // numChildSlots() partials deleted, the rest frozen at their pre-clear
        // amplitudes - until the next bloom child happens to spawn. Issued BEFORE
        // cloud_.reset() so that reset()'s own recompute runs against the
        // PARAMETRIC law and its normGain_.snapTo() snaps to that level; clearing
        // afterwards would snap the normalizer to the stale spectrum's gain and
        // then slide off it over kNormGainSmoothMs. RT-safe: one bool and two
        // dirty masks (harmonic_cloud.h:862-866).
        cloud_.clearSpectralTarget();
        cloud_.reset();
        noise_.reset();
        resonance_.reset();
        if (rtSafe) {
            ecology_.silenceAudio();  // feedback_ecology.h:935 - O(1) per owned loop
        } else {
            ecology_.reset();  // :854 - the O(buffer) wipe, CONTROL THREAD ONLY
        }
        bloom_.reset();
        ecosystem_.reset();
        for (auto& b : bodies_) {
            b.reset();
        }
        mse_.reset();
        growth_.reset();
        for (auto& s : sched_) {
            s.reset();
        }
        breath_.reset();
        tide_.reset();

        excL_.fill(0.0f);
        excR_.fill(0.0f);
        noiseMono_.fill(0.0f);
        bodyAL_.fill(0.0f);
        bodyAR_.fill(0.0f);
        bodyBL_.fill(0.0f);
        bodyBR_.fill(0.0f);
        carryL_.fill(0.0f);
        carryR_.fill(0.0f);
        ratios_.fill(0.0f);
        amplitudes_.fill(0.0f);
        parentCount_ = 0;
        targetActive_ = false;

        carryAvail_ = 0;
        carryRead_ = 0;
        carryIsLifeOnly_ = true;

        noiseGain_.snapTo(1.0f);
        blend_.snapToTarget();
        noiseApL_.reset();
        noiseApR_.reset();
        noiseApDelayR_ = 0.0f;

        level_ = 0.0f;
        ghostRequest_ = 0.0f;
        breathGravityLane_ = 0.0f;
        tidalFogDepth_ = 0.0f;
        envOutput_ = 0.0f;
        lastOutL_ = 0.0f;
        lastOutR_ = 0.0f;
        hasSounded_ = false;
        renderedSinceNoteOn_ = false;
        velocity_ = 1.0f;

        for (std::size_t k = 0; k < kNumEventSchedulers; ++k) {
            slotDrawRng_[k].seed(deriveStreamSeed(seed_, kSlotDrawSaltBase + k));
            lastEventTarget_[k] = SlowEventScheduler::kNoTarget;
            drawnSlot_[k] = 0u;
            eventWasActive_[k] = false;
        }

        // SEEDED AT THE RETIRE VALUE, NOT 0 (seraphis_voice.h:1022-1027). The
        // counter only advances inside renderOneChunk/advanceOneChunkLifeOnly, so
        // with a 0 seed every never-rendered slot would report
        // isFinished() == false and the engine would take the full render path on
        // all kMaxVoices slots for the first quiescentChunksToRetire_ chunks -
        // ten seconds of it.
        quiescentChunks_ = quiescentChunksToRetire_;
    }

    // =========================================================================
    // Envelope - the single write path
    // =========================================================================

    /// THE single write path into mse_'s stage configuration.
    ///
    /// The shadow arrays are the single source of truth: setEnvelopeMode's
    /// Standard branch restores from them, so a direct mse_.setStage anywhere
    /// else would leave them stale and a Standard -> Growth -> Standard round trip
    /// would silently install a 0 ms attack.
    void applyStage(int st, float level, float ms) noexcept {
        if (st < 0 || st >= MultiStageEnvelope::kMaxStages) {
            return;
        }
        const auto i = static_cast<std::size_t>(st);
        // Idempotence guard. MultiStageEnvelope::setStage has NO equality
        // early-out of its own, so without this an unchanged-knob apply() every
        // block would keep rewriting the stage.
        if (stageLevel_[i] == level && stageTimeMs_[i] == ms) {
            return;
        }
        stageLevel_[i] = level;
        stageTimeMs_[i] = ms;
        if (envMode_ == EnvelopeMode::Standard || st >= mse_.getSustainPoint()) {
            mse_.setStage(st, level, ms, kStageCurve);  // multi_stage_envelope.h:166
        } else {
            mse_.setStage(st, level, 0.0f, kStageCurve);  // Growth zeroes pre-sustain times
        }
    }

    // =========================================================================
    // The identity layer (FR-020 .. FR-026) - plan S3.6, tasks.md T013
    // =========================================================================

    /// FR-020 / FR-022. The `EcosystemEngine::Kind` a scheduler destination
    /// FAMILY addresses. The two rosters are deliberately in different orders
    /// (see EventFamily), so this is the one place the mapping exists.
    [[nodiscard]] static constexpr std::size_t kindForFamily(std::size_t family) noexcept {
        constexpr std::array<std::size_t, kNumEventFamilies> kMap{
            static_cast<std::size_t>(EcosystemEngine::Kind::Partial),    // BloomTrigger
            static_cast<std::size_t>(EcosystemEngine::Kind::Noise),      // NoiseWake
            static_cast<std::size_t>(EcosystemEngine::Kind::Resonator),  // PeakWake
            static_cast<std::size_t>(EcosystemEngine::Kind::Feedback),   // LoopWake
            static_cast<std::size_t>(EcosystemEngine::Kind::Ghost)};     // GhostBurst
        return kMap[(family < kNumEventFamilies) ? family : (kNumEventFamilies - 1u)];
    }

    /// FR-020a. How many destination slots kind @p k addresses.
    ///
    /// Read from the OWNERS (`getNumPeaks`, `getNumSources`, `getNumLoops`),
    /// never from VoragoVoiceConfig: each owner clamps the requested count again
    /// and the realised figure is the owner's, which is the S2.3 rule that the
    /// voice stores no shadow of a capacity it does not own. `Partial` and
    /// `Ghost` are single-destination families - the cloud/bloom pair and the
    /// FR-020b accumulator - so both report 1.
    [[nodiscard]] std::size_t slotCountForKind(std::size_t k) const noexcept {
        if (k == static_cast<std::size_t>(EcosystemEngine::Kind::Partial)) {
            return 1u;
        }
        if (k == static_cast<std::size_t>(EcosystemEngine::Kind::Resonator)) {
            return std::min(resonance_.getNumPeaks(), ResonanceDriftNetwork::kMaxPeaks);
        }
        if (k == static_cast<std::size_t>(EcosystemEngine::Kind::Noise)) {
            return std::min(noise_.getNumSources(), NoiseOrganism::kMaxSources);
        }
        if (k == static_cast<std::size_t>(EcosystemEngine::Kind::Feedback)) {
            return std::min(ecology_.getNumLoops(), FeedbackEcology::kMaxLoops);
        }
        if (k == static_cast<std::size_t>(EcosystemEngine::Kind::Ghost)) {
            return 1u;
        }
        return 0u;
    }

    /// ONE control step's routed contributions, indexed `[Kind][slot]`.
    ///
    /// 2 x 5 x 12 floats = 480 B of STACK, built fresh in publishIdentity() and
    /// gone on the way out - no member, so two voices can never share it and a
    /// stale lane cannot survive a reset. It is a NAMED type rather than four
    /// local arrays so the identity layer splits cleanly into a GATHER half,
    /// which reads the ecosystem and the schedulers, and an APPLY half, which
    /// writes the shipped setters and nothing else. SC-019a and SC-019b drive
    /// the apply half directly over an enumerated table: a running
    /// SlowEventScheduler's event value and an EcosystemEngine agent's energy
    /// are the components' own business and are not settable from outside, so
    /// "both zeros, both ones, the equal case" is otherwise unreachable.
    struct IdentityLanes {
        std::array<std::array<float, kMaxSlotsPerKind>, EcosystemEngine::kNumKinds> eco{};
        std::array<std::array<float, kMaxSlotsPerKind>, EcosystemEngine::kNumKinds> sched{};
    };

    /// FR-020a. The agent -> destination deal, computed ONCE per prepare() /
    /// setSeed() from `ecosystem_.getAgentKind(i)`.
    ///
    /// KIND IS NOT `i % kNumKinds`. EcosystemEngine deals kinds by a STRATIFIED
    /// deal followed by a Fisher-Yates shuffle (ecosystem_engine.h:2167-2212)
    /// precisely so that kind is not a function of agent index, so the deal must
    /// be READ. Agents of a kind are then dealt ROUND-ROBIN over that kind's
    /// slots, which is what makes the many-to-one case FR-020a reduces the
    /// normal case rather than an edge one: at the shipped 32 agents each kind
    /// holds 6 or 7, so `Ghost` and `Partial` (1 slot each) are addressed six or
    /// seven deep while `Resonator` (12 slots) leaves five slots unaddressed.
    void assignAgentSlots() noexcept {
        agentSlot_.fill(0u);
        agentValid_.fill(0u);
        std::array<std::size_t, EcosystemEngine::kNumKinds> cursor{};
        const std::size_t agents =
            std::min(ecosystem_.getAgentCount(), EcosystemEngine::kMaxAgents);
        for (std::size_t i = 0; i < agents; ++i) {
            const auto k = static_cast<std::size_t>(ecosystem_.getAgentKind(i));  // :893
            if (k >= EcosystemEngine::kNumKinds) {
                continue;
            }
            const std::size_t slots = slotCountForKind(k);
            if (slots == 0u) {
                continue;  // a family with no realised destination is not routed
            }
            agentSlot_[i] = static_cast<std::uint8_t>(cursor[k] % slots);
            agentValid_[i] = 1u;
            ++cursor[k];
        }
    }

    /// FR-020a. The MANY-TO-ONE reduction: a slot reads ONLY the strongest
    /// addressing agent, `argmax` over energy - NEVER a blend and never a mean.
    ///
    /// One pass, no sort, no allocation, `kNumKinds x kMaxSlotsPerKind` (5 x 12)
    /// doubles of stack. `bestE` starts at -1.0 and every energy is >= 0, so the
    /// first addressing agent always wins its slot; the comparison is a STRICT
    /// `>`, which keeps the LOWER agent index on a tie and makes the outcome a
    /// function of the seed alone. A non-finite energy fails `e > bestE` and is
    /// therefore treated as NOT the strongest, rather than as the strongest.
    ///
    /// The per-agent arrays are parameters rather than direct
    /// `ecosystem_.getAgentEnergy(i)` reads so that SC-019b can enumerate energy
    /// tuples - a clear winner, a tie, a three-way - that a live
    /// `EcosystemEngine` will not produce on demand. gatherEcosystemLanes() is
    /// the only production caller and feeds it the component's own numbers, so
    /// the enumerated case and the shipped path run the SAME scan.
    ///
    /// FR-021's routing depth is applied HERE, at store time, which is identical
    /// to scaling the winner afterwards and saves a second pass.
    void reduceAgentLanes(const double* energy, const float* output, std::size_t count,
                          IdentityLanes& lanes) const noexcept {
        std::array<std::array<double, kMaxSlotsPerKind>, EcosystemEngine::kNumKinds> bestE{};
        for (auto& row : bestE) {
            row.fill(-1.0);
        }
        const std::size_t n = std::min(count, EcosystemEngine::kMaxAgents);
        for (std::size_t i = 0; i < n; ++i) {
            if (agentValid_[i] == 0u) {
                continue;
            }
            const auto k = static_cast<std::size_t>(ecosystem_.getAgentKind(i));
            const auto s = static_cast<std::size_t>(agentSlot_[i]);
            if (k >= EcosystemEngine::kNumKinds || s >= kMaxSlotsPerKind) {
                continue;
            }
            if (energy[i] > bestE[k][s]) {
                bestE[k][s] = energy[i];
                lanes.eco[k][s] = ecosystemDepth_[k] * output[i];
            }
        }
    }

    /// FR-020 / FR-020a. Copy the ecosystem's per-agent state into the scan's
    /// two arrays and reduce. 48 doubles + 48 floats of stack, once per control
    /// step - the accepted cost of Q2's per-step argmax.
    void gatherEcosystemLanes(IdentityLanes& lanes) const noexcept {
        std::array<double, EcosystemEngine::kMaxAgents> energy{};
        std::array<float, EcosystemEngine::kMaxAgents> output{};
        const std::size_t agents =
            std::min(ecosystem_.getAgentCount(), EcosystemEngine::kMaxAgents);
        for (std::size_t i = 0; i < agents; ++i) {
            energy[i] = ecosystem_.getAgentEnergy(i);  // :890, a double
            output[i] = ecosystem_.getAgentOutput(i);  // :886, [0, 1]
        }
        reduceAgentLanes(energy.data(), output.data(), agents, lanes);
    }

    /// FR-022. The scheduler contribution, and the ONLY mutating half of the
    /// gather: it advances the per-scheduler slot draw and arms the bloom.
    ///
    /// `value` is a NET, not a fold: `setBipolarProbability(0)` makes every event
    /// positive (prepare() step 5), so `max(0, getCurrentValue())` only removes a
    /// sign this configuration never produces and can never SUBTRACT from a base.
    ///
    /// THE SLOT WITHIN A MULTI-SLOT FAMILY IS DRAWN ONCE PER EVENT, on the RISING
    /// EDGE of isEventActive() (slow_event_scheduler.h:361) latched in
    /// eventWasActive_, and held for the whole event - a per-step draw would
    /// spray one swell across every slot of its family. `BloomTrigger` likewise
    /// calls triggerBloom() on the ONSET EDGE ONLY: `armed_` is edge-like and a
    /// level-polling caller is the natural bug the component guards against
    /// (bloom_engine.h:654-663).
    ///
    /// Two schedulers can land on one destination in the same step; they combine
    /// by the same maximum FR-023 uses, for the same reason.
    void gatherSchedulerLanes(IdentityLanes& lanes) noexcept {
        for (std::size_t k = 0; k < kNumEventSchedulers; ++k) {
            const bool active = sched_[k].isEventActive();
            const std::uint8_t family = sched_[k].getActiveTarget();  // kNoTarget while idle
            if (active && !eventWasActive_[k] && family < kNumEventFamilies) {
                const std::size_t kind = kindForFamily(family);
                const std::size_t slots = std::max(slotCountForKind(kind), std::size_t{1});
                drawnSlot_[k] = static_cast<std::uint8_t>(slotDrawRng_[k].next() % slots);
                lastEventTarget_[k] = family;
                if (family == static_cast<std::uint8_t>(EventFamily::BloomTrigger)) {
                    bloom_.triggerBloom();  // :663 - the ONSET EDGE, never the level
                }
            }
            eventWasActive_[k] = active;
            if (!active || family >= kNumEventFamilies
                || family == static_cast<std::uint8_t>(EventFamily::BloomTrigger)) {
                continue;  // BloomTrigger is a TRIGGER: it carries no routed level
            }
            const std::size_t kind = kindForFamily(family);
            const std::size_t slot =
                std::min(static_cast<std::size_t>(drawnSlot_[k]), kMaxSlotsPerKind - 1u);
            const float value = std::max(0.0f, sched_[k].getCurrentValue());  // :331
            lanes.sched[kind][slot] = std::max(lanes.sched[kind][slot], value);
        }
    }

    /// FR-020 / FR-020b / FR-023 / FR-024. The APPLY half: the only writer of
    /// setSourceWake / setPeakWake / setLoopWake / setMutation / setDepth /
    /// ghostRequest_ anywhere in this class.
    ///
    /// The three WAKE surfaces take FR-023's maximum against their voice-owned
    /// base. The `Partial` pair does NOT: the scheduler's only `Partial`-family
    /// destination is BloomTrigger, which is a trigger and carries no level, so
    /// there is no second contribution to combine with and FR-020's table routes
    /// the agent value onto the base as a SUM (plan S3.6 (d)) - a mutation or a
    /// bloom depth is a quantity the ecosystem ADDS to, not a gate it opens.
    /// `Ghost` has no base at all (FR-020b): the request IS the maximum of the
    /// two contributions, and the voice never touches AtmosphereEngine - it does
    /// not own one (FR-002, FR-056).
    ///
    /// Everything goes through the shipped setters and nothing else, so dormancy
    /// semantics stay the components' own and the voice adds no second gate
    /// (FR-024).
    void applyIdentityLanes(const IdentityLanes& lanes) noexcept {
        constexpr auto kPartial = static_cast<std::size_t>(EcosystemEngine::Kind::Partial);
        constexpr auto kResonator = static_cast<std::size_t>(EcosystemEngine::Kind::Resonator);
        constexpr auto kNoise = static_cast<std::size_t>(EcosystemEngine::Kind::Noise);
        constexpr auto kFeedback = static_cast<std::size_t>(EcosystemEngine::Kind::Feedback);
        constexpr auto kGhost = static_cast<std::size_t>(EcosystemEngine::Kind::Ghost);

        const std::size_t sources = std::min(noise_.getNumSources(), NoiseOrganism::kMaxSources);
        for (std::size_t s = 0; s < sources; ++s) {
            noise_.setSourceWake(  // noise_organism.h:844
                s, combineWake(noiseWakeBase_[s], lanes.eco[kNoise][s], lanes.sched[kNoise][s]));
        }

        const std::size_t peaks =
            std::min(resonance_.getNumPeaks(), ResonanceDriftNetwork::kMaxPeaks);
        for (std::size_t p = 0; p < peaks; ++p) {
            resonance_.setPeakWake(  // resonance_drift_network.h:771
                p, combineWake(peakWakeBase_[p], lanes.eco[kResonator][p],
                               lanes.sched[kResonator][p]));
        }

        const std::size_t loops = std::min(ecology_.getNumLoops(), FeedbackEcology::kMaxLoops);
        for (std::size_t l = 0; l < loops; ++l) {
            ecology_.setLoopWake(  // feedback_ecology.h:1293
                l, combineWake(loopWakeBase_[l], lanes.eco[kFeedback][l],
                               lanes.sched[kFeedback][l]));
        }

        const float partialEco = lanes.eco[kPartial][0];
        cloud_.setMutation(std::clamp(mutationBase_ + partialEco, 0.0f, 1.0f));
        bloom_.setDepth(std::clamp(bloomDepthBase_ + partialEco, 0.0f, 1.0f));

        ghostRequest_ = combineWake(0.0f, lanes.eco[kGhost][0], lanes.sched[kGhost][0]);
    }

    /// The identity layer's SIX destinations, written at their bases with no
    /// lane contribution at all - the state a voice that has never run a control
    /// step is in.
    ///
    /// A CLEARING PATH HAS TO MAKE THESE WRITES, and the reason is that all six
    /// destinations are CONFIGURATION in the components that own them, not audio
    /// state: NoiseOrganism, ResonanceDriftNetwork, FeedbackEcology,
    /// HarmonicCloud and BloomEngine all document reset() as configuration-
    /// PRESERVING, so a clear would otherwise leave every one of them holding the
    /// last modulated value publishIdentity() wrote - a mutation of 0.58 where
    /// the base is 0.15, a bloom depth of 1.00 where the base is 0.60, and six
    /// wake surfaces sitting wherever their last agent left them. The first
    /// control step after the clear does recompute all six from the bases, but it
    /// recomputes them as RAMP TARGETS, so the stale value is where the ramp
    /// starts and the difference is audible for as long as the ramp runs.
    ///
    /// This is what makes VoragoEngine::reset()'s documented contract - a reset
    /// engine renders exactly as a freshly prepared one - true rather than merely
    /// intended: prepare() ends in reset() (step 8), so prepare() and every
    /// clearing path install these six through this ONE body and cannot drift
    /// apart.
    ///
    /// RT-safe: six scalar setters and nothing else, which is what lets
    /// resetForSteal() / resetForRecovery() reach it.
    void installIdentityNeutral() noexcept {
        // static constexpr, not a temporary: IdentityLanes is 480 bytes and a
        // steal reaches this twice (silence() then resetForSteal()), so a
        // function-local constant costs a zero-fill of nothing at all.
        static constexpr IdentityLanes kNoLanes{};
        applyIdentityLanes(kNoLanes);  // every lane zero: the bases alone
        resonance_.setGravity(std::clamp(gravityBase_, -1.0f, 1.0f));
    }

    /// FR-026. The two life-modulator lanes, summed onto their bases and
    /// PUBLISHED so they are observable rather than inferred.
    ///
    /// NO SECOND DEPTH FACTOR ON EITHER LANE, and that is a reading of the
    /// components rather than an omission: BreathingModulator::getCurrentValue()
    /// is already `clamp(depth_ * bipolar, -1, 1)` (breathing_modulator.h:286,
    /// surfaced at :222) and TidalModulator scales by its own depth inside
    /// rawOutput() (tidal_modulator.h:317), so a voice-side multiply by a shadow
    /// of the same depth would make the `Movement -> BreathingDepth` row
    /// QUADRATIC in its own parameter.
    ///
    /// `HarmonicCloud::setSpectralGravity` is driven by NEITHER lane and keeps
    /// its FR-090 value (Q4). The tidal lane is published as a NET,
    /// `max(0, tide)`, so a tide TROUGH cannot pull the engine's fog BELOW its
    /// own smear base; the engine folds it on at its own control step, because
    /// the voice never reaches SpectralSmear - it does not own one.
    void publishLifeLanes() noexcept {
        breathGravityLane_ = breath_.getCurrentValue();  // breathing_modulator.h:222
        resonance_.setGravity(std::clamp(gravityBase_ + breathGravityLane_, -1.0f, 1.0f));
        tidalFogDepth_ = std::max(0.0f, tide_.getCurrentValue());  // tidal_modulator.h:263
    }

    /// FR-022's interval scale, applied where the voice owns the two ranges.
    ///
    /// `setIntervalRange` stores two clamped floats and nothing else
    /// (slow_event_scheduler.h:238-242) - no ramp, no phase, no redraw - so
    /// re-writing the same numbers every control step is exactly idempotent, and
    /// a change takes effect at the NEXT drawn period rather than truncating the
    /// running one. eventRateScale_ is clamped to [0.1, 10] by its setter, so the
    /// division is always by a strictly positive number.
    void applyEventRateScale() noexcept {
        sched_[0].setIntervalRange(kFastEventIntervalMinSeconds / eventRateScale_,
                                   kFastEventIntervalMaxSeconds / eventRateScale_);
        if constexpr (kNumEventSchedulers > 1u) {
            sched_[1].setIntervalRange(kSlowEventIntervalMinSeconds / eventRateScale_,
                                       kSlowEventIntervalMaxSeconds / eventRateScale_);
        }
    }

    /// FR-020 .. FR-026. The ONLY writer of the wake / depth surfaces and of the
    /// two published lanes. Called from step 1 of renderOneChunk() AND from
    /// advanceOneChunkLifeOnly(), which is what makes SC-030's rendering and
    /// life-only advances indistinguishable to the identity layer.
    void publishIdentity() noexcept {
        IdentityLanes lanes{};
        gatherEcosystemLanes(lanes);
        gatherSchedulerLanes(lanes);
        applyIdentityLanes(lanes);
        publishLifeLanes();
        applyEventRateScale();
    }

    /// @brief FR-011 / FR-012 / B-1 / B-2. The bloom -> cloud spectrum handoff.
    ///
    /// Called ONCE per control chunk, from step 2 of renderOneChunk(), BEFORE the
    /// cloud renders - `setSpectralTarget` only raises dirty flags, and they are
    /// consumed at the head of the FIRST updateControl of the NEXT
    /// `processStereoBlock` call (`harmonic_cloud.h:748-752`). Called any later in
    /// the step order and the target would be one chunk stale.
    ///
    /// --- THE ONE WRITE PATH FOR RICHNESS -------------------------------------
    /// `cloudRichness_` is the voice's shadow of the cloud's richness and the ONE
    /// source of the amplitude exponent p(r) evaluated below. EVERY path that
    /// writes richness goes through VoragoVoice::setRichness (:936), which updates
    /// the shadow AND the cloud from the COMPONENT's clamped value. There is no
    /// second writer. `HarmonicCloud::getRichness()` does exist
    /// (`harmonic_cloud.h:490`) and reading it here instead of the shadow is the
    /// equivalent implementation - take either, but NEVER both, because two
    /// sources of p(r) can disagree for exactly one chunk after a richness change
    /// and that is an audible spectral step nothing else would explain.
    ///
    /// --- B-2: WHY std::exp2 AND NEVER std::pow --------------------------------
    /// The cloud's own rolloff is `std::exp2(-exponent * kHarmonicCloudLog2N[i])`
    /// (`harmonic_cloud.h:1492-1494`) off the shared 64-entry log2 table
    /// (`:57-62`). Supplying `std::pow(n, -p)` instead would be the SAME NUMBER
    /// MATHEMATICALLY and a different float: the table carries its own ~2e-8
    /// relative rounding, so the parent region would no longer be bit-identical to
    /// the untargeted render and every bloom spawn would step the whole parent
    /// spectrum by that much. The expression below is the cloud's, character for
    /// character, so the two agree by construction rather than by tolerance.
    /// `ratios_[i] = float(i + 1)` is exact for the same reason: it takes the
    /// FR-082 identity branch (`harmonic_cloud.h:1327-1337`), which falls back to
    /// the unmodified parametric frequency law INCLUDING its own `gravityIsZero`
    /// arm, so gravity and inharmonicity keep acting exactly as they do untargeted.
    ///
    /// --- NEUTRALITY IS A PARENT-REGION CLAIM, NOT A WHOLE-SPECTRUM ONE --------
    /// The voice supplies `parentCount_ == reserveBase()` slots. Slots
    /// `[reserveBase(), capacity())` are the bloom's reserved region and carry
    /// amplitude 0 while no child is live, where the untargeted cloud would carry
    /// `n^-p`. A target left standing there would therefore silently DELETE the
    /// top `numChildSlots()` partials of the shipped spectrum. That is exactly why
    /// `clearSpectralTarget()` is called the moment `getLiveChildCount() == 0`,
    /// and why `targetActive_` latches: the clear is issued once, on the edge, not
    /// every chunk.
    ///
    /// --- B-1: WHICH WAY THE CAPACITY FLOOR DEGRADES ---------------------------
    /// Below `activeCount_ < kMinCloudCapacity` (14) the FLOOR wins: the top
    /// `numChildSlots()` slots sit ABOVE the cloud's active count, so the bloom
    /// becomes INAUDIBLE at very low richness while the parents are never
    /// displaced. That direction is deliberate. The alternative - `capacity ==
    /// activeCount` with no floor - drives `reserveBase()` to 0 at
    /// `activeCount <= kBloomChildSlots` (6) and DELETES the parent spectrum
    /// outright, which is the loud failure rather than the quiet one.
    ///
    /// --- setSpectralTarget CAN NEVER BE REJECTED HERE -------------------------
    /// The four wholesale-rejection conditions (`harmonic_cloud.h:801-803`),
    /// checked off one by one:
    ///   1. null pointers - both arguments are `std::array` members of this
    ///      object, so neither can be null;
    ///   2. `count == 0` - `count` is either `parentCount_` (the disengaged
    ///      pass-through return, `bloom_engine.h:1534-1537`) or `capacity_` (the
    ///      engaged return, `:1584`). `capacity_ >= kMinCloudCapacity` = 14 and
    ///      `parentCount_ = capacity_ - numChildSlots() >= kMinParentSlots` = 8,
    ///      so `count >= 8`;
    ///   3. `count > kMaxPartials` - `capacity_ <= HarmonicCloud::kMaxPartials`
    ///      by the clamp below and `parentCount_ < capacity_`, so `count <= 64`;
    ///   4. any NaN/Inf, any `ratios[i] <= 0` or any `amplitudes[i] < 0` - every
    ///      slot below `parentCount_` is written by the loop below with
    ///      `float(i + 1) >= 1` and a finite `exp2` of a finite product; every
    ///      slot in `[parentCount_, capacity_)` is repadded UNCONDITIONALLY by
    ///      `BloomEngine::applyOutput` with `float(i + 1)` and either 0 or a live
    ///      child amplitude that is finite and in `[0, target]`
    ///      (`bloom_engine.h:1553-1584`). No slot below `count` is ever stale.
    void updateSpectrumTarget() noexcept {
        // FR-012 / B-1. The bloom's slot budget tracks what the cloud will
        // actually SOUND, floored so the parent region survives (see above).
        const std::size_t active = cloud_.getActivePartialCount();  // harmonic_cloud.h:950
        const std::size_t capacity =
            std::clamp(active, kMinCloudCapacity, HarmonicCloud::kMaxPartials);
        if (capacity != bloom_.capacity()) {
            bloom_.setCapacity(capacity);  // bloom_engine.h:628
        }

        // B-1 (Q-D): the parent count handed to processChunk is reserveBase(),
        // NEVER getActivePartialCount(). reserveBase() is the top of the region
        // the caller owns; anything larger overruns the bloom's reserved region
        // and turns getOverlapEngagementCount() (bloom_engine.h:748) from a real
        // fault signal into a counter that is always saturated.
        parentCount_ = bloom_.reserveBase();  // bloom_engine.h:710

        // FR-041(b)'s p(r), evaluated exactly as harmonic_cloud.h:1467-1468 does.
        const float p = HarmonicCloud::kRichnessMinExponent
                        + (HarmonicCloud::kRichnessMaxExponent
                           - HarmonicCloud::kRichnessMinExponent)
                              * cloudRichness_;
        for (std::size_t i = 0; i < parentCount_; ++i) {
            ratios_[i] = static_cast<float>(i + 1);  // FR-082 identity branch
            // NOT std::pow - see B-2 above. harmonic_cloud.h:57-62, :1492-1494.
            amplitudes_[i] = std::exp2(-p * detail::kHarmonicCloudLog2N[i]);
        }

        // bloom_engine.h:494. parentCount_ is an ANALYSIS length; the arrays are
        // kMaxSlots (64) entries each, which is the normative precondition
        // (bloom_engine.h:466-481) and is what the two std::array declarations
        // below the state section guarantee.
        const std::size_t count = bloom_.processChunk(ratios_.data(), amplitudes_.data(),
                                                     parentCount_, kControlChunkSamples);

        // FR-011's two edges. Both go through the cloud's own dirty-flag path and
        // its FR-014 amplitude smoother, which is what makes them click-free
        // (harmonic_cloud.h:861-866); SC-018a measures both.
        const bool wantTarget = (bloom_.getLiveChildCount() > 0u);  // bloom_engine.h:712
        if (wantTarget) {
            cloud_.setSpectralTarget(ratios_.data(), amplitudes_.data(), count);  // :769
        } else if (targetActive_) {
            cloud_.clearSpectralTarget();  // harmonic_cloud.h:862
        }
        targetActive_ = wantTarget;
    }

    // =========================================================================
    // The control chunk
    // =========================================================================

    /// FR-013's level detector, verbatim from seraphis_voice.h:1178-1183.
    void updateLevel(float chunkPeak) noexcept {
        level_ = (chunkPeak > level_) ? chunkPeak
                                      : chunkPeak + (level_ - chunkPeak) * levelReleaseCoeff_;
        quiescentChunks_ = (level_ < kTailSilenceThreshold) ? (quiescentChunks_ + 1) : 0;
    }

    /// @brief Render EXACTLY kControlChunkSamples into the carry FIFO (FR-010).
    ///
    /// Step order is the contract (plan S3.4). Steps 1 and 2 are the two hooks
    /// T013 and T011 fill; steps 3-10 are the render chain T012 refines.
    void renderOneChunk() noexcept {
        constexpr std::size_t n = kControlChunkSamples;
        renderedSinceNoteOn_ = true;

        // 1. IDENTITY LAYER. Advanced BEFORE anything reads a wake value, so the
        //    whole chunk renders against one consistent identity state.
        ecosystem_.processChunk(n);  // ecosystem_engine.h:431
        for (std::size_t k = 0; k < kNumEventSchedulers; ++k) {
            sched_[k].processBlock(n);  // slow_event_scheduler.h:304
        }
        breath_.processBlock(n);  // breathing_modulator.h:209
        tide_.processBlock(n);    // tidal_modulator.h:250
        publishIdentity();

        // 2. BLOOM -> CLOUD SPECTRUM HANDOFF.
        updateSpectrumTarget();

        // 3. EXCITATION: the cloud.
        cloud_.processStereoBlock(excL_.data(), excR_.data(), n);  // harmonic_cloud.h:878

        // 4. EXCITATION: the noise organism, decorrelated and summed in
        //    (FR-015, B-3). noise_.processBlock is MONO out.
        noise_.processBlock(noiseMono_.data(), n);  // noise_organism.h:414
        for (std::size_t s = 0; s < n; ++s) {
            const float g = noiseGain_.process();
            const float m = noiseMono_[s] * g;
            const float aL = noiseApL_.process(m);              // biquad.h:352
            const float aR = noiseApR_.process(noiseApDelayR_);  // the one-sample branch delay
            noiseApDelayR_ = m;
            excL_[s] += aL;
            excR_[s] += aR;
        }

        // 5. THE VOICE ENVELOPE, IN PLACE ON THE EXCITATION BUS (AR-5).
        //    NOTHING DOWNSTREAM OF THIS POINT IS GATED.
        if (envMode_ == EnvelopeMode::Growth) {
            growth_.processBlock(n);                          // growth_envelope.h:185
            const float gGrowth = growth_.getCurrentValue();  // held across the chunk
            for (std::size_t s = 0; s < n; ++s) {
                const float g = velocity_ * gGrowth * mse_.process();
                excL_[s] *= g;
                excR_[s] *= g;
                envOutput_ = g;
            }
        } else {
            for (std::size_t s = 0; s < n; ++s) {
                const float g = velocity_ * mse_.process();  // multi_stage_envelope.h:223
                excL_[s] *= g;
                excR_[s] *= g;
                envOutput_ = g;
            }
        }

        // 6. RESONANCE - IN PLACE. "the inputs may alias the outputs, in either
        //    pairing" (resonance_drift_network.h:506).
        resonance_.processBlock(excL_.data(), excR_.data(), excL_.data(), excR_.data(), n);

        // 7. ECOLOGY - IN PLACE, same documented licence (feedback_ecology.h:908).
        ecology_.processBlock(excL_.data(), excR_.data(), excL_.data(), excR_.data(), n);

        // 8. THE TWO-BODY BLEND (FR-036, FR-037). NOT in place - ContinuousBody
        //    forbids aliasing (continuous_body.h:1609-1610). BOTH bodies run at
        //    EVERY blend value, so the composed render never depends on blend
        //    history. At b = 0 the expression is 1.0f * A + 0.0f * B and IEEE-754
        //    multiplication of a finite B by exactly 0.0f is +/-0.0f, so body B
        //    contributes BIT-NOTHING - contribution nullity, not "a single-body
        //    render", which is not a configuration this product has.
        bodies_[0].processStereoBlock(excL_.data(), excR_.data(), bodyAL_.data(), bodyAR_.data(),
                                      n);
        bodies_[1].processStereoBlock(excL_.data(), excR_.data(), bodyBL_.data(), bodyBR_.data(),
                                      n);
        for (std::size_t s = 0; s < n; ++s) {
            const float b = blend_.process();  // PER SAMPLE (FR-037)
            const float a = 1.0f - b;
            carryL_[s] = a * bodyAL_[s] + b * bodyBL_[s];
            carryR_[s] = a * bodyAR_[s] + b * bodyBR_[s];
        }

        // 9. THE D3 SILENCE FADE TAIL. Guarded so fadeRemaining_ can never run
        //    negative, which would add an inverted, magnitude-GROWING tail
        //    forever (seraphis_voice.h:1152-1162).
        for (std::size_t s = 0; s < n && fadeRemaining_ > 0; ++s) {
            const float w =
                static_cast<float>(fadeRemaining_) / static_cast<float>(silenceRampSamples_);
            carryL_[s] += fadeTailL_ * w;
            carryR_[s] += fadeTailR_ * w;
            --fadeRemaining_;
        }

        // 10. LEVEL DETECTOR + RETIREMENT, on the POST-blend buffer - exactly what
        //     this voice contributes.
        float chunkPeak = 0.0f;
        for (std::size_t s = 0; s < n; ++s) {
            chunkPeak = std::max(chunkPeak, std::max(std::fabs(carryL_[s]), std::fabs(carryR_[s])));
        }
        updateLevel(chunkPeak);

        carryAvail_ = n;
        carryRead_ = 0;
        carryIsLifeOnly_ = false;
        // lastOut* is NOT assigned here - it is captured at SERVE time (D4).
    }

    /// @brief Step 1 only, plus updateLevel(0), a zero-filled carry and a cleared
    ///        lastOut pair.
    ///
    /// It deliberately does NOT run step 2: BloomEngine's clock draw is a pure
    /// function of elapsed control steps, so advancing it in both paths would run
    /// the bloom clock twice per chunk for a voice that alternates idle and
    /// rendering.
    void advanceOneChunkLifeOnly() noexcept {
        constexpr std::size_t n = kControlChunkSamples;
        ecosystem_.processChunk(n);
        for (std::size_t k = 0; k < kNumEventSchedulers; ++k) {
            sched_[k].processBlock(n);
        }
        breath_.processBlock(n);
        tide_.processBlock(n);
        publishIdentity();

        updateLevel(0.0f);
        carryL_.fill(0.0f);
        carryR_.fill(0.0f);
        carryAvail_ = n;
        carryRead_ = 0;
        carryIsLifeOnly_ = true;
        lastOutL_ = 0.0f;
        lastOutR_ = 0.0f;
    }

    // =========================================================================
    // Private state (plan S2.5)
    // =========================================================================

    // --- owned sub-components (FR-002; exactly these, by value) --------------
    // FORBIDDEN here, and the size guard is what enforces it: ModulationEngine,
    // VoiceModRouter, PolySynthEngine, SynthVoice, CavernVerb, SubharmonicEngine,
    // SpectralSmear, AtmosphereEngine.
    HarmonicCloud cloud_;
    NoiseOrganism noise_;
    ResonanceDriftNetwork resonance_;
    FeedbackEcology ecology_;
    BloomEngine bloom_;
    std::array<ContinuousBody, kNumBodies> bodies_;  // A = 0, B = 1
    EcosystemEngine ecosystem_;
    MultiStageEnvelope mse_;
    GrowthEnvelope growth_;
    std::array<SlowEventScheduler, kNumEventSchedulers> sched_;
    BreathingModulator breath_;
    TidalModulator tide_;

    // --- scratch: ONE control chunk each (D1). Nine buffers, 2 304 B/voice ----
    // excL_/excR_ carry the signal from the cloud all the way to the body inputs:
    // ResonanceDriftNetwork (:506) and FeedbackEcology (:908) BOTH document that
    // the inputs may alias the outputs, so those two stages run IN PLACE and no
    // extra pair is needed. ContinuousBody does NOT support in place
    // (:1609-1610), hence the four body buffers.
    std::array<float, kControlChunkSamples> excL_{};
    std::array<float, kControlChunkSamples> excR_{};
    std::array<float, kControlChunkSamples> noiseMono_{};
    std::array<float, kControlChunkSamples> bodyAL_{};
    std::array<float, kControlChunkSamples> bodyAR_{};
    std::array<float, kControlChunkSamples> bodyBL_{};
    std::array<float, kControlChunkSamples> bodyBR_{};
    std::array<float, kControlChunkSamples> carryL_{};
    std::array<float, kControlChunkSamples> carryR_{};
    std::size_t carryAvail_ = 0;
    std::size_t carryRead_ = 0;
    bool carryIsLifeOnly_ = true;

    // --- the bloom/cloud spectrum handoff (FR-011) ---------------------------
    // EXACTLY BloomEngine::kMaxSlots entries each: processChunk requires at least
    // 64 writable floats whatever the configured capacity is, and says so as a
    // normative precondition (bloom_engine.h:466-481, :348-356).
    std::array<float, BloomEngine::kMaxSlots> ratios_{};
    std::array<float, BloomEngine::kMaxSlots> amplitudes_{};
    std::size_t parentCount_ = 0;
    bool targetActive_ = false;  // the FR-011 edge latch

    // --- noise decorrelation (FR-015, B-3) -----------------------------------
    Biquad noiseApL_;
    Biquad noiseApR_;
    float noiseApDelayR_ = 0.0f;
    LinearRamp noiseGain_;  // kGainRampMs

    // --- Phase 12 forwarder shadows (FR-004, plan S2.3) ----------------------
    // Cleared in prepare() step 5, because NoiseOrganism::prepare() clears the
    // organism's own feedback latch: a stale voice-side flag would suppress the
    // first post-prepare push.
    std::array<bool, NoiseOrganism::kMaxSources> combTuningSet_{};
    std::array<float, NoiseOrganism::kMaxSources> combHzReq_{};
    std::array<float, NoiseOrganism::kMaxSources> combSpreadReq_{};
    std::array<bool, NoiseOrganism::kMaxSources> combFeedbackLatched_{};

    // --- the two-body blend (FR-036, FR-037) ---------------------------------
    LinearRamp blend_;  // advanced PER SAMPLE inside the chunk

    // --- identity routing (FR-020 .. FR-023) ---------------------------------
    std::array<std::uint8_t, EcosystemEngine::kMaxAgents> agentSlot_{};   // fixed at prepare()
    std::array<std::uint8_t, EcosystemEngine::kMaxAgents> agentValid_{};
    std::array<float, NoiseOrganism::kMaxSources> noiseWakeBase_{};
    std::array<float, ResonanceDriftNetwork::kMaxPeaks> peakWakeBase_{};
    std::array<float, FeedbackEcology::kMaxLoops> loopWakeBase_{};
    /// FR-021: ONE routing depth PER DESTINATION FAMILY, indexed by
    /// `EcosystemEngine::Kind`. Zero-initialised, i.e. FR-021's neutral - the
    /// shipped 0.85 is installed by prepare() (:617), never by the member.
    std::array<float, EcosystemEngine::kNumKinds> ecosystemDepth_{};
    float ghostRequest_ = 0.0f;  // FR-020b, held between control steps
    // The FR-021/FR-026 bases the identity layer sums onto, and the two lanes it
    // PUBLISHES. publishIdentity() is the only writer of the last two.
    float gravityBase_ = 0.0f;
    float mutationBase_ = 0.15f;
    float bloomDepthBase_ = 0.60f;
    float breathGravityLane_ = 0.0f;
    float tidalFogDepth_ = 0.0f;
    // The two modulator DEPTHS are deliberately NOT shadowed: setBreathingDepth /
    // setTidalDepth forward to breath_.setDepth / tide_.setDepth and the FR-071
    // getters read the components' own breathing_modulator.h:189 /
    // tidal_modulator.h:214 - the S2.3 rule, that the voice stores no shadow of a
    // value it does not own.
    // No `{}` here: Xorshift32's only constructor is `explicit` with a default
    // argument (core/random.h:45), so brace-initialising the array is
    // copy-initialisation from `{}` and is ill-formed. Default-initialisation
    // runs that constructor per element, which is what is wanted; clearRunState()
    // re-seeds both lanes from seed_ before anything reads them.
    std::array<Xorshift32, kNumEventSchedulers> slotDrawRng_;  // FR-022's seeded slot draw
    std::array<std::uint8_t, kNumEventSchedulers> lastEventTarget_{};
    std::array<std::uint8_t, kNumEventSchedulers> drawnSlot_{};
    std::array<bool, kNumEventSchedulers> eventWasActive_{};

    // --- level detector / retirement (FR-013) --------------------------------
    float level_ = 0.0f;
    float levelReleaseCoeff_ = 0.0f;
    int quiescentChunks_ = 0;             // seeded AT quiescentChunksToRetire_ in clearRunState()
    int quiescentChunksToRetire_ = 7500;  // derived at prepare(): 10 s at 48 kHz

    // --- envelope shadows (the single write path) ----------------------------
    EnvelopeMode envMode_ = EnvelopeMode::Standard;
    std::array<float, static_cast<std::size_t>(MultiStageEnvelope::kMaxStages)> stageTimeMs_{};
    std::array<float, static_cast<std::size_t>(MultiStageEnvelope::kMaxStages)> stageLevel_{};
    float releaseMs_ = kDefaultReleaseMs;
    float velocity_ = 1.0f;
    float envOutput_ = 0.0f;

    // --- silence carry (the D3 anti-click tail) ------------------------------
    float fadeTailL_ = 0.0f;
    float fadeTailR_ = 0.0f;
    float lastOutL_ = 0.0f;
    float lastOutR_ = 0.0f;
    int fadeRemaining_ = 0;
    int silenceRampSamples_ = 48;

    // --- bookkeeping ---------------------------------------------------------
    double sampleRate_ = 48000.0;
    bool prepared_ = false;
    bool hasSounded_ = false;
    bool renderedSinceNoteOn_ = false;
    std::uint32_t seed_ = 1u;
    float cloudRichness_ = 0.70f;  // B-2's shadow; the ONE source of p(r)
    float cloudTiltDb_ = -4.0f;    // FR-012's consumer-tilt mirror
    float eventRateScale_ = 1.0f;
};

/// FR-002. The ownership guard, asserted where a reader will see it.
static_assert(sizeof(VoragoVoice) <= VoragoVoice::kVoiceSizeBound,
              "FR-002: VoragoVoice has grown past its measured ownership bound - a forbidden "
              "sub-component (ModulationEngine, VoiceModRouter, PolySynthEngine, SynthVoice, "
              "CavernVerb, SubharmonicEngine, SpectralSmear, AtmosphereEngine) is the first "
              "thing to look for");

/// std::array<VoragoVoice, kMaxVoices> is several hundred kilobytes. NEVER a
/// test local, always heap-allocated (the seraphis_engine.h:201-204 warning).
static_assert(VoragoVoice::kControlChunkSamples == 64,
              "FR-007: the shared Phase 2-9 control grid");

}  // namespace DSP
}  // namespace Krate
