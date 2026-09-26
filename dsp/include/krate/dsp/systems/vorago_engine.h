// ==============================================================================
// Layer 3: System - VoragoEngine (the polyphonic dark-ambient drone engine)
// ==============================================================================
// Vorago Phase 10. Spec slug: vorago-phase10-voice-engine.
//   Spec:    specs/vorago-phase10-voice-engine/spec.md
//   Plan:    specs/vorago-phase10-voice-engine/plan.md
//   Tasks:   specs/vorago-phase10-voice-engine/tasks.md
//   Roadmap: specs/Vorago-roadmap.md, Part A -> Phase 10 (lines 446-474),
//            Open Questions 5 and 6 (lines 588-589)
//
// Owns the VoragoVoice pool, the shared identity sources (EcosystemEngine,
// SlowEventScheduler), note allocation and stealing, the voice-sum bus, the
// atmosphere return, the SubharmonicEngine / SpectralSmear tail and the
// output stage.
//
// Feature: vorago-phase10-voice-engine
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (no allocation, lock, exception or IO on the
//   audio thread; the voice pool is sized in prepare())
// - Principle III: Modern C++ (C++20, value semantics, no owning pointers)
// - Principle IX: Layer 3 - Layers 0-2 plus Layer 3 PEERS only
// - Principle X: DSP Constraints (reject-never-clamp setters, a non-finite
//   guard ladder)
// - Principle XI: Performance Budget (SC-001a, SC-001b, SC-002, SC-003)
//
// ------------------------------------------------------------------------------
// ARCHITECTURE RULINGS THAT SHAPE THIS FILE
//
// AR-1  THE CAVERN SPACE SITS OUTSIDE THIS ENGINE, at a documented seam. Layer 3
//       may not name a Layer 4 type, so NO effects/ HEADER IS INCLUDED HERE,
//       EVER (tools/lint-layers.js:74 flags a systems/ -> effects/ include).
//       The engine exposes TWO render entry points and the CALLER runs the
//       reverb between them - SeraphisEngine's shipped contract verbatim
//       (seraphis_engine.h:608-624):
//
//         engine.processStereoBlock(l, r, n);       // voices -> sum -> sub -> smear
//         cavern.processStereoBlock(l, r, l, r, n); // Layer 4, owned by the CALLER
//         engine.processOutputStage(l, r, n);       // TapeSaturator -> TruePeakLimiter
//
// AR-2  The identity layer is routed EXPLICITLY through concrete types. No
//       ModulationEngine, no VoiceModRouter, no virtual dispatch per sample.
// B-5   The atmosphere wet return is summed into the voice-sum bus BEFORE the
//       subharmonic stage.
// B-6   The CPU solve is N*V + (kMaxVoices - N)*L + G <= budget; kMaxVoices is a
//       BUDGET number, not a free ceiling.
// B-7   Stealing routes through the voice's RT-safe clearing paths, never
//       FeedbackEcology::reset() (control-thread only).
// ------------------------------------------------------------------------------
//
// BUILD STATE (tasks.md).
// T006 established this file as a compiling, lint-visible stub.
// T014 landed the constants, VoragoEngineConfig, prepare(), the four envelope
//   fan-out forwarders, the global-chain setter surface, setPolyphony() with its
//   musical-release semantics, the per-slot seeds, note handling and the
//   three-pass amnesty-aware steal policy.
// T015 (THIS PASS) lands the render loop (processStereoBlock), the two control
//   steps, the output stage, getLatencySamples() and the non-finite containment
//   - and with them the per-slice scratch buffers, the held sub-fundamental and
//   the FR-026 tidal fog fold that rewrites smear_ from smearBase_ every control
//   chunk.
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/db_utils.h>  // detail::isFinite - NEVER std::isnan (-ffast-math)
#include <krate/dsp/core/random.h>    // deriveStreamSeed

// Layer 1: Primitives
#include <krate/dsp/primitives/smoother.h>  // OnePoleSmoother (the FR-043 sum gain)

// Layer 2: Processors
#include <krate/dsp/processors/spectral_smear.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/true_peak_limiter.h>

// Layer 3: Systems (peers)
#include <krate/dsp/systems/atmosphere_engine.h>  // the GLOBAL ghost tap (FR-056)
#include <krate/dsp/systems/subharmonic_engine.h>
#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_voice.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Krate {
namespace DSP {

// =============================================================================
// VoragoEngineConfig (FR-042, FR-052; plan S6.3, S8.1)
// =============================================================================

/// @brief Prepare-time configuration for VoragoEngine.
///
/// Every field is CLAMPED by its owner, never rejected (FR-076). The three
/// values that deliberately diverge from their component defaults carry their
/// FR-091 justification inline.
struct VoragoEngineConfig {
    /// Clamped to [1, VoragoEngine::kMaxBlockSamples].
    std::size_t maxBlockSamples = 2048;

    /// Forwarded VERBATIM to every one of the kMaxVoices slots.
    VoragoVoiceConfig voice{};

    // --- the global ghost tap (FR-056; moved off VoragoVoiceConfig by OQ-1(b)) ---

    /// FR-091. NOT the component's 8.0: a ghost grain is 12 s long (FR-017), so
    /// a 4 s or 8 s capture ring could never hold one. 20 s is the smallest ring
    /// that holds a whole grain plus the 0.90 position spread the same table
    /// asks for. The cost is 2 x 20 x fs floats ONCE, globally (7.3 MB at
    /// 48 kHz); per voice at kMaxVoices it would have been 58 MB. Clamped to
    /// AtmosphereEngine's [1, 30].
    float atmosCaptureSeconds = 20.0f;
    bool atmosBlurEnabled = true;
    /// No Phase-10 consumer; off saves the freeze FFT and its ring.
    bool atmosFreezeEnabled = false;
    std::size_t atmosBlurFftSize = 1024;
    std::size_t atmosFreezeFftSize = 2048;

    /// Phase 10a. BOTH INERT BY DEFAULT (ADR-3, Clarifications 2026-09-22 Q5):
    /// Phase 10's SC-027 and its checked-in kEngineBaselineNsAtPoly4 were measured
    /// on the shipped ghost path, and a phase whose own criteria say the ceiling is
    /// unchanged may not move either. Phase 14 presets engage them.
    float atmosGhostReverseProbability = 0.0f;
    bool atmosGhostEventTriggers = false;

    // --- the global smear (FR-052) ---

    /// THE COMPONENT DEFAULTS TO false (spectral_smear.h:141-149). Vorago's Fog
    /// macro needs it, and SC-022 clause 2 needs BOTH states reachable on the
    /// real object, so the flag is carried here rather than assumed.
    bool smearEnabled = true;
    std::size_t smearFftSize = 2048;
};

// =============================================================================
// VoragoVoiceParams (Vorago Phase 12, FR-003)
// =============================================================================

/// @brief The run-time per-voice parameter set, broadcast by
///        VoragoEngine::applyVoiceParams to every slot.
///
/// @par Layer: 3 (systems/). Plain data; copying is trivial and allocation-free.
///
/// NO FIELD HERE MAY NAME A VoragoMacroTarget (the seraphis_engine.h:110-112
/// rule): those values reach the voices through VoragoMacroMatrix::setTargetBase,
/// and a second write path would double-apply them.
///
/// It is *not* VoragoVoiceConfig, which is prepare-time. Every default member
/// initializer is the voice's prepare() step-5 value, so broadcasting a
/// default-constructed instance is a no-op on the render.
struct VoragoVoiceParams {
    static constexpr std::size_t kNumNoiseSlots = NoiseOrganism::kMaxSources;  // 4
    static constexpr std::size_t kNumLoops = FeedbackEcology::kMaxLoops;       // 6

    float stereoSpread = 0.45f;          // vorago_voice.h prepare(): setStereoSpread(0.45f)
    float cloudSpectralGravity = 0.10f;  // vorago_voice.h prepare(): cloud_.setSpectralGravity(0.10f)
    ContinuousBody::BodyMaterial bodyMaterialA = ContinuousBody::BodyMaterial::StoneChamber;
    ContinuousBody::BodyMaterial bodyMaterialB = ContinuousBody::BodyMaterial::SteelTank;
    std::array<NoiseOrganismModel, kNumNoiseSlots> noiseModel{
        {NoiseOrganismModel::FilteredWind, NoiseOrganismModel::GranularDust,
         NoiseOrganismModel::Direct, NoiseOrganismModel::MetallicHiss}};
    /// The REQUESTED type; the organism's own default (noise_organism.h:1138).
    std::array<NoiseType, kNumNoiseSlots> noiseType{
        {NoiseType::Brown, NoiseType::Brown, NoiseType::Brown, NoiseType::Brown}};
    std::array<float, kNumNoiseSlots> noiseCombFundamentalHz{{60.0f, 60.0f, 60.0f, 60.0f}};
    std::array<float, kNumNoiseSlots> noiseCombSpread{{0.35f, 0.35f, 0.35f, 0.35f}};
    /// C-4: the per-model defaults the slots run at after prepare().
    std::array<float, kNumNoiseSlots> noiseCombFeedback{
        {NoiseOrganism::kDefaultCombFeedback, NoiseOrganism::kDefaultCombFeedback,
         NoiseOrganism::kDefaultCombFeedback, NoiseOrganism::kMetallicCombFeedback}};
    ResonanceDriftNetwork::AnchorMode resonanceAnchorMode =
        ResonanceDriftNetwork::AnchorMode::Hybrid;
    std::array<FeedbackEcology::FilterMode, kNumLoops> ecologyLoopFilterMode{
        {FeedbackEcology::FilterMode::Lowpass, FeedbackEcology::FilterMode::Lowpass,
         FeedbackEcology::FilterMode::Lowpass, FeedbackEcology::FilterMode::Lowpass,
         FeedbackEcology::FilterMode::Lowpass, FeedbackEcology::FilterMode::Lowpass}};

    /// 2 + 2 + 4 x 5 + 1 + 6 = 31 scalar values (FR-003).
    static constexpr std::size_t kFieldCount = 31;
};
static_assert(std::is_trivially_copyable_v<VoragoVoiceParams>);
static_assert(VoragoVoiceParams::kNumNoiseSlots == 4 && VoragoVoiceParams::kNumLoops == 6);

// =============================================================================
// VoragoEngine
// =============================================================================

/// @brief The polyphonic Vorago drone engine (Layer 3).
///
/// @par Ownership
/// kMaxVoices VoragoVoice slots, one VoiceAllocator, ONE global AtmosphereEngine
/// (the FR-056 ghost tap), one SubharmonicEngine, one SpectralSmear, the output
/// stage (two TapeSaturators + a TruePeakLimiter) and the FR-043 sum gain.
///
/// @par Heap
/// HUNDREDS OF KILOBYTES. `std::array<VoragoVoice, kMaxVoices>` alone is
/// ~1 MB, and MSVC's default main-thread stack is 1 MiB - so EVERY TEST AND
/// EVERY CALLER HEAP-ALLOCATES THIS OBJECT (seraphis_engine.h:201-204). A stack
/// local is a defect, not a style preference.
///
/// @par Thread safety
/// Audio thread only, except prepare(), reset() and silence(), which are
/// explicitly NOT audio-thread operations - see their warnings.
class VoragoEngine {
public:
    // =========================================================================
    // Constants (plan S6.2)
    // =========================================================================

    /// THE CEILING (S12 / OQ-1(a)) - AND NOT A FREE ONE. Every slot between
    /// kDefaultPolyphony and this value pays the life-only cost L on EVERY
    /// block, because the render loop bound is unconditional (B-6). It stays 8
    /// while SC-001a's polyphony survey runs, so the whole {1, 2, 4, 6, 8} curve
    /// is measurable; T031 lowers it if Q-A rules that way, in the same commit
    /// as the SC-001b baseline.
    static constexpr std::size_t kMaxVoices = 6;  // Q-A ruling 2026-09-19: 8 -> 6 (roadmap 4-6)

    /// The SHIPPED polyphony (S12's recommendation).
    static constexpr std::size_t kDefaultPolyphony = 4;

    /// The same absolute control grid the voice runs (FR-007).
    static constexpr std::size_t kControlChunkSamples = 64;
    static constexpr std::size_t kMaxBlockSamples = 2048;

    /// FR-045. Disjoint from EVERY VoragoVoice salt (which occupy
    /// 0x0100..0x0C01 - vorago_voice.h:382-395, whose own static_assert pins
    /// this value as a literal).
    static constexpr std::size_t kVoiceSaltBase = 0xA000;
    static constexpr std::size_t kAtmosSalt = 0xB000;

    /// FR-043. 100 ms, NOT 20 ms: the value is read once and HELD for a whole
    /// control chunk, so it is a staircase whose first stair is 28.35 % of the
    /// step at 20 ms - measured at 2.651x the click bound against 1.143x at
    /// 100 ms (seraphis_engine.h:219-246).
    static constexpr float kSumGainSmoothMs = 100.0f;

    /// FR-044. -30 dBFS long-release steal amnesty (seraphis_engine.h:247-248).
    static constexpr float kAmnestyLevelThreshold = 0.0316f;

    /// FR-053. Low drive (roadmap line 462): the saturator is a gluing stage,
    /// not an effect.
    static constexpr float kOutputSaturation = 0.12f;
    /// The shipped drive, and outputDriveDb_'s initial value. Since Phase 12
    /// (spec B-2) prepare() installs the STORED drive, not this constant.
    static constexpr float kOutputDriveDb = 0.0f;
    static constexpr float kOutputCeilingDb = -0.3f;

    /// At most one deferred non-finite voice CLEAR per control chunk. The
    /// serviced call is VoragoVoice::resetForRecovery() - the RT-SAFE path
    /// (B-7) - and NEVER reset(), one of whose six FeedbackEcology fills alone
    /// exceeds the 8 889 ns control-chunk budget (feedback_ecology.h:2401-2412).
    static constexpr std::size_t kResetsPerControlChunk = 1;

    /// FR-017's ghost gating: the level written when a burst is fully open. The
    /// BASE is 0.0 - a base of 0 is what makes a burst a burst.
    static constexpr float kGhostBurstPeak = 0.60f;

    /// Phase 10a FR-031's edge predicate, on the GATED level `ghostPeak_ * ghost`.
    /// Defined FROM kGhostBurstPeak so they cannot drift from Phase 10's SC-027
    /// detector thresholds (vorago_engine_test.cpp:2666-2667).
    static constexpr float kGhostTriggerRise = 0.5f * kGhostBurstPeak;
    static constexpr float kGhostTriggerFall = 0.05f * kGhostBurstPeak;

    /// FR-013's heap-free-giant guard, asserted just below the class.
    ///
    /// Expressed AGAINST VoragoVoice::kVoiceSizeBound rather than as a frozen
    /// byte count, for the seraphis_engine.h:264-287 reason: kVoiceSizeBound
    /// deliberately allows a 5 % per-toolchain padding difference, so a literal
    /// 1.05x engine bound recorded on one toolchain would turn into a hard build
    /// break on any compiler that used the headroom the voice guard explicitly
    /// permits. The additive 64 KiB covers everything the engine adds beyond the
    /// pool (allocator, atmosphere, subharmonic, smear, output stage, scalars),
    /// and the guard still fires on a ninth voice slot or on a forbidden member.
    static constexpr std::size_t kEngineSizeBound =
        kMaxVoices * VoragoVoice::kVoiceSizeBound + (64u * 1024u);

    // =========================================================================
    // Construction
    // =========================================================================

    VoragoEngine() noexcept = default;

    // NON-COPYABLE AND NON-MOVABLE, STATED rather than silently produced.
    // VoragoVoice deletes all four (vorago_voice.h:441-444) and
    // SubharmonicEngine deletes its move for a documented use-after-free reason
    // (subharmonic_engine.h:366-369), so a `= default`ed move here would be
    // DEFINED AS DELETED while reading as if the type were movable.
    VoragoEngine(const VoragoEngine&) = delete;
    VoragoEngine& operator=(const VoragoEngine&) = delete;
    VoragoEngine(VoragoEngine&&) = delete;
    VoragoEngine& operator=(VoragoEngine&&) = delete;

    // =========================================================================
    // Lifecycle (FR-042, FR-047)
    // =========================================================================

    /// @brief FR-042. The ONLY allocating path; NOT real-time safe.
    ///
    /// Prepares ALL kMaxVoices slots regardless of the current polyphony, which
    /// is exactly what makes setPolyphony() allocation-free: it only changes how
    /// many slots the allocator may hand out and how the sum gain is scaled.
    ///
    /// Order is load-bearing:
    ///   1. rate floor (FR-076: a NaN, zero or negative rate is SUBSTITUTED and
    ///      then floored - never rejected; isPrepared() is true afterwards);
    ///   2. clamp the config, never reject;
    ///   3. per-slot seed THEN voice prepare - VoragoVoice::setSeed only stores
    ///      while the voice is unprepared, and the voice's own prepare() calls
    ///      applySeeds() at its step 4, which is the one point at which
    ///      ContinuousBody::setSeed is still configure-time legal
    ///      (vorago_voice.h:918-924, continuous_body.h:1580-1589);
    ///   4. the allocator;
    ///   5. the global chain, with the S8.3 default table IN FULL - every row,
    ///      including the unchanged ones, so the table IS the code;
    ///   6. run state, then prepared_ = true.
    ///
    /// prepare() MAY BE CALLED REPEATEDLY (FR-077): polyphony, seed and every
    /// engine-owned setter value survive, because step 5 re-applies the table
    /// through the engine's own fields rather than through component defaults.
    void prepare(double sampleRate, const VoragoEngineConfig& cfg) noexcept {
        // --- 1. rate floor ---------------------------------------------------
        // NaN fails `> 1.0` and lands on 1.0; each owner floors AGAIN at its own
        // kMinUsableSampleRate = 8000.0. FR-076's "substituted, then floored".
        const double sr = (sampleRate > 1.0) ? sampleRate : 1.0;

        // --- 2. clamp the config --------------------------------------------
        const std::size_t maxBlock =
            std::clamp(cfg.maxBlockSamples, std::size_t{1}, kMaxBlockSamples);
        polyphony_ = std::clamp(polyphony_, std::size_t{1}, kMaxVoices);
        // Phase 10a FR-030's config shadow. It lands HERE - the step that already
        // turns cfg fields into engine state - and NOT in the FR-017 block, which
        // gains exactly one line (the setGrainReverseProbability forward).
        if (!ghostTriggersSet_) { ghostEventTriggers_ = cfg.atmosGhostEventTriggers; }  // FR-006
        ghostTriggerHigh_ = false;  // FR-031's latch starts disarmed

        // --- 3. voices: seed, then prepare, for ALL kMaxVoices (FR-042) ------
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setSeed(deriveStreamSeed(seed_, kVoiceSaltBase + v));  // FR-045
            voices_[v].prepare(sr, cfg.voice);
        }

        // --- 4. the allocator ------------------------------------------------
        allocator_.reset();
        allocator_.setAllocationMode(AllocationMode::Oldest);  // voice_allocator.h:311
        allocator_.setStealMode(StealMode::Hard);              // :317
        // setVoiceCount is [[nodiscard]] (:326); at prepare time there is nothing
        // to release, so discarding is correct. setPolyphony() is the one caller
        // that CONSUMES the span.
        static_cast<void>(allocator_.setVoiceCount(polyphony_));

        // --- 5a. the global ghost tap (FR-017's seven values, REPRODUCED) ----
        atmos_.setSeed(deriveStreamSeed(seed_, kAtmosSalt));
        atmos_.prepare(sr, AtmosphereEngine::PrepareConfig{
                               .captureSeconds = cfg.atmosCaptureSeconds,
                               .blurEnabled = cfg.atmosBlurEnabled,
                               .freezeEnabled = cfg.atmosFreezeEnabled,
                               .blurFftSize = cfg.atmosBlurFftSize,
                               .freezeFftSize = cfg.atmosFreezeFftSize,
                               .maxBlockSamples = maxBlock});
        atmos_.setDensity(0.30f);         // a ghost is an event, not a wash
        atmos_.setGrainSeconds(12.0f);    // a grain is a memory of the drone
        atmos_.setPitchSemitones(-12.0f); // ghosts sit an octave under
        atmos_.setPositionSpread(0.90f);  // read ages scatter across the capture
        atmos_.setBlur(atmosBlur_);       // roadmap line 114's darker blur default (0.85)
        atmos_.setDecorrelation(0.85f);   // wide, unlocalised
        if (!ghostReverseSet_) { ghostReverseProbability_ = cfg.atmosGhostReverseProbability; }  // FR-006
        atmos_.setGrainReverseProbability(ghostReverseProbability_);  // FR-034, dflt 0
        // Event-driven: the BASE is 0.0 and the control step writes
        // ghostPeak_ * max(getGhostRequest()) every chunk (T015).
        atmos_.setLevel(0.0f);

        // --- 5b. the subharmonic tail ---------------------------------------
        // UNLIKE 5c and 5d, these two setters run AFTER prepare(), and they MUST.
        // SubharmonicEngine::prepare() is the one component here whose prepare
        // RE-APPLIES ITS OWN DEFAULTS: step 9 calls applyDefaults()
        // (subharmonic_engine.h:408, :1355-1385), which rewrites every tone level
        // (setToneLevelDb) and the tracking amount (setTrackingAmount) from
        // kDefaultToneLevelDb / kDefaultTrackingAmount. Written beforehand, both
        // Vorago values were silently discarded while the engine's own
        // subToneOffsetDb_ / subTracking_ shadows kept them - so every getter,
        // and SC-009 clause 1 with it, reported a value the component was not
        // running on, and the first VoragoMacroMatrix::apply() at the NEUTRAL
        // changed the render (SC-009 clause 3).
        //
        // The snap the old ordering was reaching for is kept by calling reset()
        // after the writes: reset() is configuration-PRESERVING and re-snaps
        // every tone's levelRamp to toneLevelGain(levelDb) and trackGainRamp_ to
        // 1 - trackingAmount_ (subharmonic_engine.h:507-520) - it is the very
        // step 13 prepare() ends in. So a freshly prepared engine still STARTS on
        // these values instead of gliding into them, which is what SC-026
        // compares against a reset() one.
        sub_.prepare(sr, SubharmonicEngine::PrepareConfig{.maxBlockSamples = maxBlock});
        applySubToneLevels();            // subToneBaseDb_ + subToneOffsetDb_
        sub_.setTrackingAmount(subTracking_);
        sub_.reset();                    // re-snap both ramps to the values above
        // FR-051: the fundamental is HELD. prepare() does not invent one - the
        // control step writes it from the lowest SOUNDING voice, and when none
        // sounds the last value stands (T015).

        // --- 5c. the global smear (FR-052) ----------------------------------
        // The setters run BEFORE prepare() so its step 9 SNAPS the control
        // smoothers to them (spectral_smear.h:271-278) instead of ramping in.
        smear_.setSmearAmount(smearBase_);
        smear_.setDecoherence(smearDecoherence_);
        smear_.setSmearTilt(smearTilt_);
        smear_.prepare(sr, SpectralSmear::PrepareConfig{.fftSize = cfg.smearFftSize,
                                                        .enabled = cfg.smearEnabled});

        // --- 5d. the output stage (FR-053) ----------------------------------
        // The setters run BEFORE prepare() so the saturator's parameter
        // smoothers are SNAPPED to them (tape_saturator.h:164-168) instead of
        // ramping in from the ctor defaults.
        satL_.setDrive(outputDriveDb_);  // Phase 12 (spec B-2): the STORED drive
        satR_.setDrive(outputDriveDb_);
        satL_.setSaturation(outputSaturation_);
        satR_.setSaturation(outputSaturation_);
        satL_.setMix(1.0f);
        satR_.setMix(1.0f);
        // The block-size argument is IGNORED by TapeSaturator::prepare
        // (tape_saturator.h:141); kControlChunkSamples is passed because that is
        // the cadence processOutputStage drives it on, not because it is a limit.
        satL_.prepare(sr, kControlChunkSamples);
        satR_.prepare(sr, kControlChunkSamples);
        // spec B-2 makeup: ramped per sample over the SAME 5 ms the saturators
        // ramp their drive gain (tape_saturator.h:159-165), and SNAPPED here
        // for the same reason their smoothers are.
        makeupSm_.configure(TapeSaturator::kDefaultSmoothingMs, static_cast<float>(sr));
        makeupSm_.snapTo(dbToGain(-outputDriveDb_));

        limiter_.setCeilingDb(kOutputCeilingDb);  // true_peak_limiter.h:85
        limiter_.prepare(sr, kMaxBlockSamples);   // chunks internally (:104-118)

        // --- 6. run state ----------------------------------------------------
        // SNAPPED, not ramped: a first block that faded up from 0 over
        // kSumGainSmoothMs would be a level artefact on every prepare().
        sumGain_.configure(kSumGainSmoothMs, static_cast<float>(sr));  // smoother.h:160
        sumGain_.snapTo(sumGainForPolyphony(polyphony_));              // :263
        sumGainHeld_ = sumGain_.getCurrentValue();                     // :191

        sampleCounter_ = 0;
        nonFinitePending_ = 0u;
        // The FR-051 change detector is rewound with the component it shadows:
        // sub_ has just been re-prepared, so a shadow left holding a pre-prepare
        // frequency would make the first control step SKIP the write that
        // re-installs it. 0 is not a reachable `lowest` (the walk requires
        // hz > 0), so the next sounding chunk always writes.
        lastSubFundamentalHz_ = 0.0f;
        // FR-072's counter is a LIFETIME diagnostic, so reset()/silence()
        // deliberately leave it alone; only a full reconfiguration rewinds it.
        nonFiniteRecoveries_ = 0u;
        orphanTail_ = 0u;
        retriggerSlot_ = -1;
        stealTeardown_ = 0u;
        lastStolenVoice_ = -1;
        voiceSerial_.fill(std::uint64_t{0});
        nextSerial_ = 1u;
        prepared_ = true;
    }

    /// @brief FR-047. Per-voice reset() plus the global chain.
    ///
    /// @warning NOT AN AUDIO-THREAD OPERATION. VoragoVoice::reset() is B-7's
    ///          CONTROL-THREAD path: it reaches FeedbackEcology::reset(), a
    ///          std::fill over six power-of-two rings (~786 KB per voice at
    ///          48 kHz, ~3.1 MB at 192 kHz - feedback_ecology.h:2401-2412), on
    ///          all kMaxVoices slots. AtmosphereEngine::reset() then fills the
    ///          whole 20 s stereo capture ring (~7.3 MB at 48 kHz). Allocation-
    ///          free and lock-free, but nowhere near a bounded per-block cost.
    ///          The RT-safe trio is VoragoVoice::silence(), resetForSteal() and
    ///          resetForRecovery(), and those are the only clearing calls an
    ///          audio-thread path may reach.
    /// @note On an UNPREPARED engine this is a no-op, which is its documented
    ///       neutral (SC-023): there is no audio state to clear, every scalar is
    ///       still at its initial value, and the owned components have no
    ///       geometry for their own reset() paths to walk.
    void reset() noexcept {
        if (!prepared_) {
            return;
        }
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].reset();
        }
        clearRunState();
    }

    /// @brief FR-047. Per-voice silence() then reset() - the tail-CLEARING
    ///        pair - plus the global chain.
    ///
    /// The second call is reset() and NEVER resetForSteal(): silence() arms an
    /// anti-click decay from the voice's last emitted sample pair, and with the
    /// tail-PRESERVING entry point every slot would be left holding a live armed
    /// tail that the next note would sum in as a click.
    ///
    /// @warning NOT AN AUDIO-THREAD OPERATION - see reset().
    /// @note On an UNPREPARED engine this is a no-op - see reset().
    void silence() noexcept {
        if (!prepared_) {
            return;
        }
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].silence();
            voices_[v].reset();
        }
        clearRunState();
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief A PARTIAL accounting of the heap this engine holds.
    ///
    /// Sums `SpectralSmear` plus every voice's own figure, and each voice's
    /// figure IS complete (FR-006, vorago_voice.h getAllocatedBytes) - so the
    /// whole per-voice half of this number is exact. What is still missing is
    /// the GLOBAL half: `AtmosphereEngine`, `SubharmonicEngine` and
    /// `TruePeakLimiter` own heap and publish no figure this engine can reach
    /// uniformly (`VoiceAllocator` owns none). FR-006 words the prepare-time
    /// total clause on the VOICE, which is why the voice was completed and this
    /// getter was not; it is INCOMPLETE BY CONSTRUCTION and stays labelled as
    /// such. A getter that quietly claimed to be complete would be worse.
    ///
    /// Either way SC-004a / SC-004b / SC-014 are unaffected: they assert this
    /// number does not MOVE, and a partial figure that never moves still proves
    /// nothing under it was resized. The allocation gate proper is
    /// AllocationScope (SC-014), not this getter.
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept {
        std::size_t total = smear_.getAllocatedBytes();
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            total += voices_[v].getAllocatedBytes();
        }
        return total;
    }

    // =========================================================================
    // Polyphony and seeding (FR-043, FR-045, FR-048)
    // =========================================================================

    /// @brief FR-043. Clamped to [1, kMaxVoices]. ALLOCATES NOTHING.
    ///
    /// prepare() prepared all kMaxVoices slots regardless of polyphony, so this
    /// only changes how many slots the allocator may hand out and how the sum
    /// gain is scaled. It is also the ONLY place the FR-043 sum-gain TARGET
    /// moves - note events never touch it, so no caller partition can update it
    /// at a different time.
    ///
    /// setVoiceCount() pushes a NoteOff for every excess slot that was Active or
    /// Releasing AND force-idles it in the same loop (voice_allocator.h:340-352).
    /// The engine therefore treats each event as a MUSICAL RELEASE and NEVER as
    /// a retirement: voices_[i].noteOff() only, never allocator_.voiceFinished(i)
    /// (the allocator has already idled the slot), and the voice keeps rendering
    /// its tail because isRendering()'s second clause is !isFinished().
    ///
    /// A slot still sounding when the shrink idles it is an ORPHAN: the
    /// allocator may hand it out again at any moment, and FR-044's teardown is
    /// required when it does. orphanTail_ is the ONLY predicate that identifies
    /// those slots at dispatch time.
    void setPolyphony(std::size_t n) noexcept {
        polyphony_ = std::clamp(n, std::size_t{1}, kMaxVoices);
        for (const VoiceEvent& e : allocator_.setVoiceCount(polyphony_)) {  // :326
            const std::size_t i = static_cast<std::size_t>(e.voiceIndex);
            if (i >= kMaxVoices || e.type != VoiceEvent::Type::NoteOff) {
                continue;
            }
            voices_[i].noteOff();
            if (!voices_[i].isFinished()) {
                orphanTail_ |= voiceBit(i);
            }
        }
        // The TARGET moves; the VALUE ramps (FR-043).
        sumGain_.setTarget(sumGainForPolyphony(polyphony_));  // smoother.h:170
    }

    [[nodiscard]] std::size_t getPolyphony() const noexcept { return polyphony_; }

    /// @brief FR-045 / FR-048. Re-derives EVERY SLOT's seed from the new engine
    ///        seed, and the atmosphere's.
    ///
    /// THE SEED IS PER SLOT AND IS NEVER ADVANCED PER NOTE (FR-048). noteOn()
    /// does not consume, advance, reseed or perturb the slot seed - and it does
    /// NOT rewind the slot's run state either: FR-046 / SC-030 require an idle
    /// slot's ecosystem, schedulers and life modulators to keep advancing so a
    /// re-triggered voice never starts cold (ruled 2026-09-19, spec Q-M). The
    /// rewind that restores a slot's trajectory is reset() (or prepare()).
    /// Three consequences are normative:
    ///   - the same slot playing the same note twice, after a retire or a steal
    ///     FOLLOWED BY reset(), reproduces its first trajectory (SC-026);
    ///   - a different slot playing that same note does NOT (the salts are
    ///     disjoint);
    ///   - therefore the whole instrument's render is a pure function of
    ///     (engine seed, configuration, note sequence), which is what makes a
    ///     Phase 14 preset render reproducible.
    /// Advancing a seed per note would make every render of the same preset
    /// differ, and is FORBIDDEN.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setSeed(deriveStreamSeed(seed_, kVoiceSaltBase + v));
        }
        atmos_.setSeed(deriveStreamSeed(seed_, kAtmosSalt));
    }

    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }

    /// @brief Phase 12 (spec B-4 / FR-023). Rewind every slot that is NOT
    ///        rendering, so a seed change applied while silent reproduces a
    ///        fresh instance prepared at that seed (SC-014 (4)).
    ///
    /// setSeed() only re-seeds each component's stream (its documented
    /// contract, kept unchanged for every existing caller and golden): state a
    /// component drew from the OLD seed at prepare()/reset() time - modulator
    /// phases, scheduler positions, body pre-rolls - survives it. Rewinding a
    /// silent slot with resetForRecovery() (RT-safe, B-7) re-draws that state
    /// from the new streams. Sounding slots are left alone: their seed is part
    /// of the note they are playing (spec Q6).
    ///
    /// @note Real-time safe; allocation-free (resetForRecovery is). No-op
    ///       before prepare().
    void rewindIdleVoices() noexcept {
        if (!prepared_) {
            return;
        }
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (voices_[v].isConfigurable()) {
                voices_[v].resetForRecovery();
            }
        }
    }

    // =========================================================================
    // Notes (FR-044)
    // =========================================================================

    /// @brief FR-044. Allocate (or steal) a slot for `note`.
    ///
    /// With the selection below in place a saturated pool NEVER reaches the
    /// allocator's own `Oldest` steal: the victim slot is freed FIRST, so the
    /// allocator emits a plain NoteOn and the dispatch table's Steal row is the
    /// defensive path, not the live one.
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
        if (!prepared_) {
            return;
        }
        if (velocity == 0u) {  // the allocator maps it too (voice_allocator.h:230-233)
            noteOff(note);
            return;
        }
        // Provenance is established BEFORE the allocator call, with the
        // allocator's own public read surface. It CANNOT be recovered
        // afterwards: by the time the returned span is walked the slot already
        // carries the new note.
        retriggerSlot_ = -1;
        for (std::size_t i = 0; i < polyphony_; ++i) {
            if (allocator_.getVoiceState(i) != VoiceState::Idle
                && allocator_.getVoiceNote(i) == static_cast<int>(note)) {
                retriggerSlot_ = static_cast<int>(i);
                break;
            }
        }
        // A retrigger is excluded: that note already owns a slot, so nothing is
        // saturated from its point of view.
        if (retriggerSlot_ < 0 && noIdleVoice()) {
            freeChosenVictimSlot();
        }
        dispatch(allocator_.noteOn(note, velocity));  // voice_allocator.h:228
        // RA-4, asserted rather than assumed: the freed slot is the ONLY idle
        // slot the allocator can see, so the NoteOn it emitted must have named
        // it - which is what cleared the stealTeardown_ bit in dispatch(). A
        // leftover bit means the allocator allocated somewhere else and the
        // teardown never ran on the victim; that is a defect, not a fallback.
        assert(stealTeardown_ == 0u
               && "RA-4: allocator_.noteOn did not allocate the freed victim slot");
        stealTeardown_ = 0u;
        retriggerSlot_ = -1;
    }

    void noteOff(std::uint8_t note) noexcept {
        if (!prepared_) {
            return;
        }
        retriggerSlot_ = -1;
        dispatch(allocator_.noteOff(note));  // voice_allocator.h:257
    }

    // =========================================================================
    // FR-044's selection rule - STATED ONCE, EVALUATED NOWHERE ELSE
    // =========================================================================

    /// @brief The three-pass amnesty-aware steal selection.
    ///
    /// Public and static for the same reason VoragoVoice::combineWake is
    /// (vorago_voice.h:966-975): SC-012 enumerates it over a victim table with
    /// no render at all, and a rule that can only be observed through a render
    /// is a rule nobody can enumerate.
    ///
    /// The three passes:
    ///   pass 0  Releasing AND below kAmnestyLevelThreshold  -- the amnesty band
    ///   pass 1  Releasing, whatever the level
    ///   pass 2  Active
    /// The first pass that finds anything wins; within a pass the LOWEST level
    /// wins, and an exact tie goes to the LOWER serial, i.e. the older
    /// allocation.
    ///
    /// PASS 1 IS NOT REDUNDANT even though an argmin over a superset would give
    /// the same slot: it is the branch that makes "every candidate is at or
    /// above the threshold" still steal the quietest Releasing voice instead of
    /// falling through to pass 2 (which, with no Active voice in the pool, would
    /// steal nothing at all). This is FR-044's reading of the amnesty - IT
    /// PROTECTS THE LOUD, IT DOES NOT PREFER THEM.
    ///
    /// The pass-0 eligibility test is spelled `!(level < threshold)` and NOT
    /// `level >= threshold`, so a NON-FINITE level (which FR-072 contains but
    /// does not make impossible) is treated as NOT ELIGIBLE rather than as the
    /// quietest voice in the pool.
    ///
    /// AN IDLE SLOT MEANS NO VICTIM (SC-012 row (a)). The rule reports -1 for
    /// any pool that still holds an Idle slot, because the allocator's own idle
    /// search will TAKE that slot and a steal is not warranted - stealing while
    /// a free slot exists would silence a sounding voice for nothing. The same
    /// predicate is also stated at the call site as noIdleVoice(), which is a
    /// cheap early-out over the allocator's view; stating it HERE as well is
    /// what makes the rule total, so row (a) can be enumerated against the rule
    /// itself rather than against the branch that guards it. The two agree by
    /// construction: freeChosenVictimSlot() fills `states` from
    /// allocator_.getVoiceState(i) over exactly the slots noIdleVoice() scans,
    /// so the guard can never change what the engine does - it only makes the
    /// function answer row (a) the way SC-012 asks it.
    ///
    /// @param states  Per-slot allocator state.
    /// @param levels  Per-slot VoragoVoice::getCurrentLevel().
    /// @param serials Per-slot allocation order (the tie-break key). The
    ///                allocator's own timestamp is private (voice_allocator.h:
    ///                483) and "lower voice index" is NOT equivalent.
    /// @return The victim slot, or -1 when the pool holds an Idle slot or no
    ///         slot is a candidate in any pass. The three spans are walked over
    ///         their COMMON length.
    [[nodiscard]] static int selectStealVictim(std::span<const VoiceState> states,
                                               std::span<const float> levels,
                                               std::span<const std::uint64_t> serials) noexcept {
        const std::size_t count =
            std::min(states.size(), std::min(levels.size(), serials.size()));
        for (std::size_t i = 0; i < count; ++i) {
            if (states[i] == VoiceState::Idle) {
                return -1;
            }
        }
        std::size_t victim = 0;
        float bestLevel = 0.0f;
        bool found = false;
        for (int pass = 0; pass < 3 && !found; ++pass) {
            for (std::size_t i = 0; i < count; ++i) {
                const VoiceState state = states[i];
                const float level = levels[i];
                if (pass == 2) {
                    if (state != VoiceState::Active) {
                        continue;
                    }
                } else {
                    if (state != VoiceState::Releasing) {
                        continue;
                    }
                    if (pass == 0 && !(level < kAmnestyLevelThreshold)) {
                        continue;
                    }
                }
                const bool better = !found || level < bestLevel
                                    || (level == bestLevel && serials[i] < serials[victim]);
                if (better) {
                    victim = i;
                    bestLevel = level;
                    found = true;
                }
            }
        }
        return found ? static_cast<int>(victim) : -1;
    }

    // =========================================================================
    // Envelope fan-out (FR-014) - THE ONLY MUTABLE ROUTE FROM OUTSIDE TO THE
    // VOICES
    // =========================================================================
    //
    // getVoice(i) is CONST and the Voice-owned macro rows reach the voices
    // through `friend class VoragoMacroMatrix`, so without these four there is
    // no legal route to a voice envelope at all - and the applyFastAttack
    // fixture would have to const_cast.
    //
    // EACH FANS OUT OVER ALL kMaxVoices, NOT polyphony_ (deliberately unlike
    // VoragoMacroMatrix::apply): these are CONFIGURATION, prepare() installs
    // them on every slot, and a later setPolyphony() growth must not admit a
    // slot carrying a different envelope. THERE IS NO NON-CONST getVoice(i): a
    // caller able to desynchronise two slots would falsify exactly that
    // sentence, and SC-030's parity clause rests on it.

    void setEnvelopeMode(VoragoVoice::EnvelopeMode mode) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setEnvelopeMode(mode);
        }
    }

    void setEnvelopeStageTimeMs(int stage, float ms) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setEnvelopeStageTimeMs(stage, ms);
        }
    }

    void setEnvelopeReleaseMs(float ms) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setEnvelopeReleaseMs(ms);
        }
    }

    void setGrowthDurationSeconds(float seconds) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            voices_[v].setGrowthDurationSeconds(seconds);
        }
    }

    // The getters read SLOT 0, which the fan-out keeps identical to every other
    // slot (FR-071).
    [[nodiscard]] VoragoVoice::EnvelopeMode getEnvelopeMode() const noexcept {
        return voices_[0].getEnvelopeMode();
    }
    [[nodiscard]] float getEnvelopeStageTimeMs(int stage) const noexcept {
        return voices_[0].getEnvelopeStageTimeMs(stage);
    }
    [[nodiscard]] float getEnvelopeReleaseMs() const noexcept {
        return voices_[0].getEnvelopeReleaseMs();
    }
    [[nodiscard]] float getGrowthDurationSeconds() const noexcept {
        return voices_[0].getGrowthDurationSeconds();
    }

    // =========================================================================
    // Phase 12 parameter surface (FR-003)
    // =========================================================================

    /// @brief Broadcast the run-time voice parameter set to EVERY slot.
    ///
    /// THE BOUND IS kMaxVoices, NOT getPolyphony() (the seraphis_engine.h:704-713
    /// reason): setPolyphony() leaves an excess slot rendering its release as an
    /// orphan tail and processStereoBlock's loop bound is kMaxVoices
    /// unconditionally, so a polyphony bound would leave that tail - and any slot
    /// the allocator hands out after a polyphony increase - on stale values.
    ///
    /// Every forwarder early-outs an unchanged value (the T008 forwarders,
    /// HarmonicCloud::setStereoSpread, ContinuousBody::setMaterial), so a repeated
    /// identical broadcast is inert (SC-023 (1)). VoragoVoice::prepare()
    /// re-installs the defaults, so callers re-push after every prepare().
    ///
    /// @par Real-Time Safety: allocation-, lock-, exception- and I/O-free.
    void applyVoiceParams(const VoragoVoiceParams& p) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            VoragoVoice& voice = voices_[v];
            voice.setStereoSpread(p.stereoSpread);
            voice.setCloudSpectralGravity(p.cloudSpectralGravity);
            voice.setBodyMaterialA(p.bodyMaterialA);
            voice.setBodyMaterialB(p.bodyMaterialB);
            for (std::size_t s = 0; s < VoragoVoiceParams::kNumNoiseSlots; ++s) {
                voice.setNoiseSourceModel(s, p.noiseModel[s]);
                voice.setNoiseSourceType(s, p.noiseType[s]);
                voice.setNoiseCombTuning(s, p.noiseCombFundamentalHz[s], p.noiseCombSpread[s]);
                voice.setNoiseCombFeedback(s, p.noiseCombFeedback[s]);
            }
            voice.setResonanceAnchorMode(p.resonanceAnchorMode);
            for (std::size_t l = 0; l < VoragoVoiceParams::kNumLoops; ++l) {
                voice.setEcologyLoopFilterMode(l, p.ecologyLoopFilterMode[l]);
            }
        }
    }

    // =========================================================================
    // The global chain's own surface (the Engine-owned macro targets)
    // =========================================================================

    /// @brief FR-068's Weight row. A shared offset fanned out over the three
    ///        tones, on top of the per-tone base (setSubToneLevelDb, FR-005).
    void setSubToneLevelOffsetDb(float dB) noexcept {
        if (!detail::isFinite(dB)) {
            return;  // FR-071: rejected, the previous value stands
        }
        // FR-067's early-out: applySubToneLevels() ends in
        // SubharmonicEngine::setToneLevelDb, which RE-ARMS a per-tone LinearRamp
        // (subharmonic_engine.h:646, levelRamp at :882), and LinearRamp::setTarget
        // re-derives its increment from the CURRENT value on every call
        // (smoother.h:340-352). Re-writing the offset already in force while
        // those ramps are in flight would stretch the glide, which is exactly the
        // step an apply()-every-block must not produce.
        if (dB == subToneOffsetDb_) {
            return;
        }
        subToneOffsetDb_ = dB;
        applySubToneLevels();
    }
    [[nodiscard]] float getSubToneLevelOffsetDb() const noexcept { return subToneOffsetDb_; }

    /// @brief FR-005. Per-tone BASE under the shared macro offset:
    ///        level(t) = subToneBaseDb_[t] + subToneOffsetDb_.
    ///
    /// Out-of-range tone / non-finite dB: no-op (the previous value stands).
    /// An UNCHANGED value early-outs for the setSubToneLevelOffsetDb reason
    /// above (a re-armed LinearRamp stretches an in-flight glide), and only
    /// tone t is written, so the other two tones' in-flight ramps are never
    /// re-armed. The owner clamps the sum (SubharmonicEngine::setToneLevelDb).
    /// The base is an engine field that applySubToneLevels() reads, so it
    /// survives prepare().
    void setSubToneLevelDb(std::size_t tone, float dB) noexcept {
        if (tone >= SubharmonicEngine::kNumTones || !detail::isFinite(dB)) {
            return;
        }
        if (dB == subToneBaseDb_[tone]) {
            return;
        }
        subToneBaseDb_[tone] = dB;
        sub_.setToneLevelDb(tone, subToneBaseDb_[tone] + subToneOffsetDb_);
    }
    [[nodiscard]] float getSubToneLevelDb(std::size_t tone) const noexcept {
        return (tone < SubharmonicEngine::kNumTones) ? subToneBaseDb_[tone] : 0.0f;
    }

    void setSubTrackingAmount(float a) noexcept {
        if (!detail::isFinite(a)) {
            return;  // FR-071: rejected, the previous value stands
        }
        subTracking_ = std::clamp(a, 0.0f, 1.0f);
        sub_.setTrackingAmount(subTracking_);
    }
    [[nodiscard]] float getSubTrackingAmount() const noexcept { return subTracking_; }

    /// @brief WRITES `smearBase_`, NOT `smear_`.
    ///
    /// FR-026's tidal fold re-writes the COMPONENT every control chunk as
    /// `clamp(smearBase_ + maxOverRenderingVoices(getTidalFogDepth()), 0, 1)`
    /// (T015), so a setter that wrote the component directly would be silently
    /// overwritten on the next chunk and the Fog macro row would stop meaning
    /// anything. The component IS written here too, so the value is live before
    /// the first control step and so prepare() can snap the smoothers to it -
    /// but `smearBase_` is the field the fold reads and `getSmearAmount()`
    /// reports, because SC-009 clause 1 compares a row's base against the value
    /// read back from that target's getter, and a getter reading the component
    /// would compare against a number the matrix does not own.
    void setSmearAmount(float a) noexcept {
        if (!detail::isFinite(a)) {
            return;  // FR-071: rejected, the previous value stands
        }
        smearBase_ = std::clamp(a, 0.0f, 1.0f);
        smear_.setSmearAmount(smearBase_);
    }
    [[nodiscard]] float getSmearAmount() const noexcept { return smearBase_; }

    void setSmearDecoherence(float a) noexcept {
        if (!detail::isFinite(a)) {
            return;  // FR-071: rejected, the previous value stands
        }
        smearDecoherence_ = std::clamp(a, 0.0f, 1.0f);
        smear_.setDecoherence(smearDecoherence_);
    }
    [[nodiscard]] float getSmearDecoherence() const noexcept { return smearDecoherence_; }

    void setSmearTilt(float t) noexcept {
        if (!detail::isFinite(t)) {
            return;  // FR-071: rejected, the previous value stands
        }
        smearTilt_ = std::clamp(t, -1.0f, 1.0f);
        smear_.setSmearTilt(smearTilt_);
    }
    [[nodiscard]] float getSmearTilt() const noexcept { return smearTilt_; }

    /// @brief FR-017's burst peak. The BASE stays 0 - the control step writes
    ///        `ghostPeak_ * max(getGhostRequest())` every chunk (T015).
    void setGhostPeakLevel(float v) noexcept {
        if (!detail::isFinite(v)) {
            return;  // FR-071: rejected, the previous value stands
        }
        ghostPeak_ = std::clamp(v, 0.0f, 1.0f);
    }
    [[nodiscard]] float getGhostPeakLevel() const noexcept { return ghostPeak_; }

    /// Vorago Phase 12 FR-006: the ghost grain reverse probability. Survives a
    /// re-prepare - prepare() uses the config value only until this is called.
    /// @param p Clamped [0, 1]; non-finite is rejected (the previous value stands).
    void setGhostReverseProbability(float p) noexcept {
        if (!detail::isFinite(p)) {
            return;  // FR-071: rejected (the atmosphere itself would map NaN to 0)
        }
        ghostReverseProbability_ = std::clamp(p, 0.0f, 1.0f);
        ghostReverseSet_ = true;
        atmos_.setGrainReverseProbability(ghostReverseProbability_);  // birth-time read
    }
    [[nodiscard]] float getGhostReverseProbability() const noexcept {
        return atmos_.getGrainReverseProbability();
    }

    /// Vorago Phase 12 FR-006: Phase 10a FR-030's event-trigger switch as a
    /// parameter. on -> off disarms FR-031's latch (SC-022 (2)). Survives a
    /// re-prepare - prepare() uses the config value only until this is called.
    void setGhostEventTriggers(bool on) noexcept {
        if (ghostEventTriggers_ && !on) {
            ghostTriggerHigh_ = false;  // on -> off disarms
        }
        ghostEventTriggers_ = on;
        ghostTriggersSet_ = true;
    }
    [[nodiscard]] bool getGhostEventTriggers() const noexcept { return ghostEventTriggers_; }
    /// Test observable: FR-031's trigger latch.
    [[nodiscard]] bool isGhostTriggerLatchHigh() const noexcept { return ghostTriggerHigh_; }

    void setAtmosBlur(float a) noexcept {
        if (!detail::isFinite(a)) {
            return;  // FR-071: rejected, the previous value stands
        }
        atmosBlur_ = std::clamp(a, 0.0f, 1.0f);
        atmos_.setBlur(atmosBlur_);
    }
    [[nodiscard]] float getAtmosBlur() const noexcept { return atmosBlur_; }

    void setOutputSaturation(float a) noexcept {
        if (!detail::isFinite(a)) {
            return;  // FR-071: rejected, the previous value stands
        }
        outputSaturation_ = std::clamp(a, 0.0f, 1.0f);
        satL_.setSaturation(outputSaturation_);
        satR_.setSaturation(outputSaturation_);
    }
    [[nodiscard]] float getOutputSaturation() const noexcept { return outputSaturation_; }

    /// Vorago Phase 12, spec B-2: MAKEUP-COMPENSATED output drive (the
    /// Pressure macro's target). +d dB goes into both TapeSaturators and
    /// -d dB of linear gain is applied after them, before the limiter, so the
    /// loudness stays level while the tanh curvature lowers the crest factor.
    /// 0 dB (the default, == kOutputDriveDb) is bit-equal to the shipped stage:
    /// processOutputStage skips the makeup multiply entirely at 0 once the
    /// makeup ramp has settled there.
    /// The makeup gain is a per-sample OnePoleSmoother target (5 ms, the
    /// saturators' own drive smoothing time) - a plain per-chunk scalar stepped
    /// at block rate and failed SC-011 for the Pressure macro (ratio 2.19),
    /// measured 2026-09-25 - plus the saturators' own setTarget (FR-067).
    /// @param d Clamped [TapeSaturator::kMinDriveDb, kMaxDriveDb]; non-finite
    ///        is rejected (the previous value stands).
    void setOutputDriveDb(float d) noexcept {
        if (!detail::isFinite(d)) {
            return;  // FR-071: rejected, the previous value stands
        }
        outputDriveDb_ = std::clamp(d, TapeSaturator::kMinDriveDb, TapeSaturator::kMaxDriveDb);
        if (prepared_) {
            makeupSm_.setTarget(dbToGain(-outputDriveDb_));  // ramped, like the drive
        } else {
            makeupSm_.snapTo(dbToGain(-outputDriveDb_));  // prepare() snaps again
        }
        satL_.setDrive(outputDriveDb_);
        satR_.setDrive(outputDriveDb_);
    }
    [[nodiscard]] float getOutputDriveDb() const noexcept { return outputDriveDb_; }

    // =========================================================================
    // Render (FR-050, FR-053, FR-072, AR-1, B-5)
    // =========================================================================

    /// @brief FR-050. Render `n` samples of the Layer-3 chain into
    ///        `outL` / `outR`.
    ///
    /// OUTPUT ONLY - this engine has no audio input. The chain is
    ///
    ///     voices -> sum x sumGain -> + ghost return -> subharmonic -> smear
    ///
    /// which is the Layer-3 half of roadmap line 461. The caller then runs its
    /// own Layer-4 cavern and finally processOutputStage() (AR-1).
    ///
    /// @par Guard ladder (FR-008), in exactly this order
    ///   - a null channel pointer: NOTHING is written and NOTHING advances;
    ///   - `n == 0`: consumes NO control step. The FR-007 grid is ABSOLUTE, so a
    ///     zero-length call must not move it;
    ///   - `!prepared_`: `n` zeros on both channels, and nothing advances.
    ///
    /// @par Partition invariance (FR-007, SC-007)
    /// The loop walks the ABSOLUTE 64-sample grid held in sampleCounter_, not
    /// the caller's blocks: a control step lands once per 64 ELAPSED samples,
    /// never once per call. Every component in steps 2 and 3 carries its own
    /// absolute grid across calls (atmosphere_engine.h:699-707,
    /// subharmonic_engine.h:600-607) or is hop-based and block-size agnostic
    /// (SpectralSmear's STFT FIFO), and the voices absorb the partition through
    /// their own carry FIFOs, so handing them `slice` rather than a whole 64 is
    /// safe.
    ///
    /// @note Real-time safe: no allocation, lock, exception or IO.
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
            const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
            if (phase == 0u) {
                runPreRenderControlStep();
            }
            const std::size_t slice = std::min(n - done, kControlChunkSamples - phase);

            // --- 1. the voice sum --------------------------------------------
            std::fill_n(busL_.data(), slice, 0.0f);
            std::fill_n(busR_.data(), slice, 0.0f);
            // THE BOUND IS v < kMaxVoices UNCONDITIONALLY. A high-water bound
            // would leave the spare slots receiving neither processStereoBlock
            // nor advanceLifeOnly, and FR-046 / SC-030 are written against ALL
            // of them (seraphis_engine.h:523-527). This is also where B-6's L
            // term - the per-block cost of a non-rendering slot - comes from,
            // which is why kMaxVoices is a BUDGET number and not a free ceiling.
            for (std::size_t v = 0; v < kMaxVoices; ++v) {
                if (!isRendering(v)) {
                    voices_[v].advanceLifeOnly(slice);  // FR-046
                    continue;
                }
                voices_[v].processStereoBlock(vL_.data(), vR_.data(), slice);
                for (std::size_t s = 0; s < slice; ++s) {
                    const float a = vL_[s];
                    const float b = vR_[s];
                    // FR-072, AT the accumulation point and with BIT PATTERNS
                    // only (never std::isnan, which -ffast-math folds away). The
                    // poisoned voice contributes 0 for the rest of the slice;
                    // the reset is DEFERRED to the next pre-render control step,
                    // one slot per chunk, because the clearing call is O(1) but
                    // the alternative - reset() - is not (B-7).
                    if (!isFiniteBits(a) || !isFiniteBits(b)) {
                        nonFinitePending_ |= voiceBit(v);
                        break;
                    }
                    busL_[s] += a;
                    busR_[s] += b;
                }
            }
            // READ ONCE per control chunk, so the value is identical under any
            // caller partition (FR-043).
            const float g = sumGainHeld_;
            for (std::size_t s = 0; s < slice; ++s) {
                busL_[s] *= g;
                busR_[s] *= g;
            }

            // --- 2. the GLOBAL ghost tap (FR-056, B-5) ------------------------
            // Fed from the voice sum, BEFORE the subharmonic and the smear, so
            // the ghost hears the raw ensemble; its wet return is summed back
            // into THE SAME bus at THE SAME point, so the ghost shares the
            // instrument's sub-weight and fog rather than sitting outside them.
            //
            // A PLAIN SUM, no second gain: setLevel's trim is already applied
            // inside the component (atmosphere_engine.h:982) and multiplying by
            // getLevel() again would square it. The output is the WET TEXTURE
            // ONLY, and the component forbids aliasing in/out - which is why
            // atmosL_/atmosR_ exist as separate scratch.
            atmos_.processStereoBlock(busL_.data(), busR_.data(), atmosL_.data(), atmosR_.data(),
                                      slice);  // atmosphere_engine.h:674
            for (std::size_t s = 0; s < slice; ++s) {
                busL_[s] += atmosL_[s];
                busR_[s] += atmosR_[s];
            }

            // --- 3. the Layer-3 half of roadmap line 461's chain --------------
            sub_.processBlock(busL_.data(), busR_.data(), busL_.data(), busR_.data(),
                              slice);                              // :545, in place
            smear_.processBlock(busL_.data(), busR_.data(), slice);  // :341, in place

            std::copy_n(busL_.data(), slice, outL + done);
            std::copy_n(busR_.data(), slice, outR + done);

            sampleCounter_ += slice;
            done += slice;
            if (sampleCounter_ % kControlChunkSamples == 0u) {
                runPostRenderControlStep();
            }
        }
    }

    /// @brief FR-053. The output stage, IN PLACE. THE CALLER RUNS THIS AFTER ITS
    ///        REVERB (AR-1).
    ///
    /// In the composed chain the buffer is the CavernVerb return:
    ///
    ///     engine.processStereoBlock(l, r, n);
    ///     cavern.processStereoBlock(l, r, l, r, n);    // Layer 4, owned by the CALLER
    ///     engine.processOutputStage(l, r, n);
    ///
    /// Calling it on a buffer this engine did not produce is the INTENDED usage
    /// (seraphis_engine.h:610-618).
    ///
    /// The 64-sample saturator loop is a CADENCE CHOICE, NOT A SIZE CONSTRAINT:
    /// TapeSaturator::prepare ignores its block-size argument
    /// (tape_saturator.h:141) and process() is per-sample stateful and
    /// partition-invariant. The limiter is ALWAYS LAST and takes the WHOLE
    /// block, which is what makes FR-073's `|out| <= 1.0` a property of this
    /// stage rather than of the caller.
    ///
    /// @note Real-time safe.
    void processOutputStage(float* l, float* r, std::size_t n) noexcept {
        if (l == nullptr || r == nullptr || n == 0 || !prepared_) {  // FR-054
            return;
        }
        for (std::size_t done = 0; done < n; done += kControlChunkSamples) {
            const std::size_t slice = std::min(kControlChunkSamples, n - done);
            satL_.process(l + done, slice);  // tape_saturator.h:335 - mono, in place
            satR_.process(r + done, slice);
            // spec B-2 makeup, ramped per sample. The short-circuit keeps the
            // default stage bit-equal to the shipped one: at 0 dB with the ramp
            // settled on 1.0 no multiply runs at all.
            if (outputDriveDb_ != 0.0f || !makeupSm_.isComplete()) {
                for (std::size_t i = 0; i < slice; ++i) {
                    const float g = makeupSm_.process();
                    l[done + i] *= g;
                    r[done + i] *= g;
                }
            }
        }
        limiter_.processBlock(l, r, static_cast<int>(n));  // true_peak_limiter.h:104
    }

    /// @brief FR-052. The engine's reported latency: `SpectralSmear`'s, and
    ///        NOTHING ELSE.
    ///
    /// It is `fftSize` when the smear is prepared AND enabled and `0` otherwise
    /// (spectral_smear.h:472), and it is CONSTANT for a prepared instance.
    ///
    /// TWO THINGS ARE DELIBERATELY NOT ADDED, so a Phase 11 reader does not
    /// re-derive them:
    ///   - the ATMOSPHERE's own blur latency. The ghost is a PARALLEL WET PATH
    ///     summed into the bus, not a through-path delay: nothing the caller
    ///     feeds in is delayed by it, because nothing is fed in at all;
    ///   - the CAVERN's latency, which belongs to the caller - the reverb sits
    ///     outside this engine at AR-1's seam.
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return smear_.getLatencySamples();
    }

    // =========================================================================
    // Read access (FR-047, FR-071)
    // =========================================================================

    /// Slots below the current polyphony that the allocator does not report Idle.
    [[nodiscard]] std::size_t getActiveVoiceCount() const noexcept {
        std::size_t count = 0;
        for (std::size_t v = 0; v < polyphony_; ++v) {
            if (allocator_.getVoiceState(v) != VoiceState::Idle) {
                ++count;
            }
        }
        return count;
    }

    /// Slots taking the full audio path this block - exactly isRendering()'s
    /// predicate, so a post-shrink orphan tail is counted while it still rings.
    [[nodiscard]] std::size_t getRenderingVoiceCount() const noexcept {
        std::size_t count = 0;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (isRendering(v)) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] float getVoiceLevel(std::size_t index) const noexcept {
        return (index < kMaxVoices) ? voices_[index].getCurrentLevel() : 0.0f;
    }
    [[nodiscard]] VoiceState getVoiceState(std::size_t index) const noexcept {
        return (index < kMaxVoices) ? allocator_.getVoiceState(index) : VoiceState::Idle;
    }
    [[nodiscard]] const VoragoVoice& getVoice(std::size_t index) const noexcept {
        return voices_[index < kMaxVoices ? index : 0u];
    }
    /// The slot the LAST steal took, or -1 if the engine has never stolen.
    [[nodiscard]] int getLastStolenVoiceIndex() const noexcept { return lastStolenVoice_; }
    /// FR-044's tie-break key, tracked engine-side. Strictly increasing across
    /// note events; 0 means "never allocated".
    [[nodiscard]] std::uint64_t getVoiceAllocationSerial(std::size_t index) const noexcept {
        return (index < kMaxVoices) ? voiceSerial_[index] : std::uint64_t{0};
    }
    /// FR-072. One increment per voice actually cleared by the deferred
    /// recovery. A LIFETIME counter: prepare() rewinds it, reset()/silence()
    /// do not.
    [[nodiscard]] std::uint32_t getNonFiniteRecoveryCount() const noexcept {
        return nonFiniteRecoveries_;
    }

    [[nodiscard]] const AtmosphereEngine& atmosphere() const noexcept { return atmos_; }
    [[nodiscard]] const SubharmonicEngine& subharmonic() const noexcept { return sub_; }
    [[nodiscard]] const SpectralSmear& smear() const noexcept { return smear_; }

private:
    friend class VoragoMacroMatrix;                   // apply() needs non-const voice access
    friend struct detail::VoragoEngineNonFiniteProbe;  // SC-029 (B-4)

    // =========================================================================
    // Engine-owned defaults that are NOT component defaults (S8.3)
    // =========================================================================

    // EVERY engine-owned macro target reports its ENGINE FIELD, never the
    // component - the same rule setSmearAmount states for smearBase_ and for
    // exactly the same two reasons: a control step may rewrite the component
    // (FR-026 does, on smear_), and SC-009 clause 1 compares a row's base
    // against the value read back from that target's getter immediately after
    // prepare(), which must be a number the matrix owns. It is also what makes
    // FR-077 hold: a re-prepare() re-applies these fields rather than the
    // component defaults, so a caller's setting survives it.
    static constexpr float kDefaultSmearAmount = 0.20f;
    static constexpr float kDefaultSmearDecoherence = 0.20f;
    // Ruled 2026-09-19: 0.60 -> 1.0 (fully tracked, the component's own
    // default) - at 0.60 the static (1 - a) sub floor never went silent after
    // note-off.
    static constexpr float kDefaultSubTracking = 1.0f;
    static constexpr float kDefaultAtmosBlur = 0.85f;

    // =========================================================================
    // Helpers
    // =========================================================================

    /// One-hot mask for slot `v`. kMaxVoices is 8, so the shift is always in
    /// range for the 32-bit masks.
    [[nodiscard]] static constexpr std::uint32_t voiceBit(std::size_t v) noexcept {
        return static_cast<std::uint32_t>(1u) << static_cast<std::uint32_t>(v);
    }

    /// FR-043's law. At polyphony 1 this is exactly 1.
    [[nodiscard]] static float sumGainForPolyphony(std::size_t n) noexcept {
        return 1.0f / std::sqrt(static_cast<float>(n));
    }

    /// The second clause is what keeps a post-shrink orphan tail rendering; both
    /// clauses depend on VoragoVoice seeding quiescentChunks_ AT the retire
    /// value in clearRunState() (vorago_voice.h:1363-1365), without which every
    /// never-rendered slot would take the full audio path.
    [[nodiscard]] bool isRendering(std::size_t v) const noexcept {
        if (v < polyphony_) {
            return allocator_.getVoiceState(v) != VoiceState::Idle || !voices_[v].isFinished();
        }
        return !voices_[v].isFinished();
    }

    /// The saturation predicate, stated against the ALLOCATOR's view of the pool
    /// rather than the engine's: allocateNote searches findIdleVoice() over
    /// `i < voiceCount_` first and only steals when that search fails
    /// (voice_allocator.h:928-937), so an idle slot means the allocator will use
    /// it and no selection is needed. A slot that is Idle but still ringing (a
    /// post-shrink orphan) counts as idle HERE for exactly the same reason.
    [[nodiscard]] bool noIdleVoice() const noexcept {
        for (std::size_t i = 0; i < polyphony_; ++i) {
            if (allocator_.getVoiceState(i) == VoiceState::Idle) {
                return false;
            }
        }
        return true;
    }

    void applySubToneLevels() noexcept {
        for (std::size_t t = 0; t < SubharmonicEngine::kNumTones; ++t) {
            sub_.setToneLevelDb(t, subToneBaseDb_[t] + subToneOffsetDb_);
        }
    }

    /// Shared by reset() and silence(): the global chain plus the engine's own
    /// run state, so a reset engine renders exactly as a freshly prepared one AT
    /// THE SAME CONFIGURATION.
    ///
    /// THAT QUALIFIER IS LOAD-BEARING, and it is measured rather than assumed
    /// (SC-026). Every clearing path here and in VoragoVoice is
    /// configuration-PRESERVING by design (FR-005), and noteOn() is a
    /// configuration write: it pushes the note frequency into the cloud, both
    /// bodies and the resonance network, and ContinuousBody holds that frequency
    /// in a SMOOTHER (continuous_body.h:1442). So a voice that has been given a
    /// note and then reset starts its next note AT that pitch, while a voice
    /// fresh out of prepare() GLIDES to it from kDefaultNoteHz. Both are correct;
    /// they are simply not the same configuration, and a comparison that treats
    /// them as one is measuring the glide, not the rewind.
    ///
    /// Every sub-component reset() here clears STATE and leaves every parameter
    /// unchanged - TapeSaturator::reset() snaps its parameter smoothers to the
    /// CURRENT values rather than re-defaulting them (tape_saturator.h:180-198),
    /// and SpectralSmear::reset() is documented "Control values, tables and the
    /// seed survive" (spectral_smear.h:295-301).
    void clearRunState() noexcept {
        atmos_.reset();  // the 20 s capture ring - see the reset() warning
        // Phase 10a FR-031's latch is run STATE, so it rewinds here; the
        // ghostEventTriggers_ CONFIG shadow is a parameter and survives, exactly
        // as every sub-component reset() above leaves its parameters unchanged.
        ghostTriggerHigh_ = false;
        sub_.reset();
        smear_.reset();
        satL_.reset();
        satR_.reset();
        limiter_.reset();
        sumGain_.snapTo(sumGainForPolyphony(polyphony_));
        sumGainHeld_ = sumGain_.getCurrentValue();
        sampleCounter_ = 0;
        nonFinitePending_ = 0u;
        // Every voice has just been reset(), so no slot is carrying a tail a
        // teardown could still be needed for. voiceSerial_/nextSerial_ are
        // deliberately NOT rewound: they are allocation ORDER, and the steal
        // tie-break reads them for the life of the engine.
        orphanTail_ = 0u;
        retriggerSlot_ = -1;
        // No note-on is in flight, so no teardown can be owed. lastStolenVoice_
        // goes with it: after reset()/silence() no slot is carrying the stolen
        // voice's tail any more, so reporting one would outlive the state it
        // describes.
        stealTeardown_ = 0u;
        lastStolenVoice_ = -1;
    }

    // =========================================================================
    // The two control steps (T015) - and the state only they touch
    // =========================================================================
    //
    // The scratch buffers and the held sub fundamental are declared HERE rather
    // than in the state block below, so they sit with the only code that reads
    // them and no field reads as unused (clang's -Wunused-private-field is on
    // under -Wall).

    /// @brief FR-072's detector, routed through the ONE fast-math-immune
    ///        predicate (core/db_utils.h:118).
    ///
    /// NEVER std::isnan / std::isinf / std::isfinite: -ffast-math licenses the
    /// compiler to assume its operands are finite and fold the whole test away,
    /// which would delete the containment silently.
    [[nodiscard]] static bool isFiniteBits(float x) noexcept { return detail::isFinite(x); }

    /// The lowest set slot in `mask`, or -1 (seraphis_engine.h:1113-1120).
    [[nodiscard]] static int lowestSetVoice(std::uint32_t mask) noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if ((mask & voiceBit(v)) != 0u) {
                return static_cast<int>(v);
            }
        }
        return -1;
    }

    /// @brief At phase 0, BEFORE the voices render the chunk (plan S6.6).
    void runPreRenderControlStep() noexcept {
        // --- 1. the FR-043 sum gain, read once and HELD for the chunk --------
        // THE `- 1` IS LOAD-BEARING: OnePoleSmoother::process() itself advances
        // one sample (smoother.h:197), so advanceSamples(64) + process() would
        // advance 65 samples per 64-sample chunk and the gain would run ahead of
        // the audio clock (seraphis_engine.h:1153-1156).
        sumGain_.advanceSamples(kControlChunkSamples - 1u);  // smoother.h:243
        sumGainHeld_ = sumGain_.process();                   // :197

        // --- 2. FR-072's DEFERRED non-finite recovery ------------------------
        // resetForRecovery() and NOT resetForSteal(), which PRESERVES the fade
        // tail - a poisoned voice must not carry a poisoned tail into the next
        // note. And NOT reset() either: that is B-7's control-thread path, and
        // one of FeedbackEcology::reset()'s six fills alone exceeds the 8 889 ns
        // control-chunk budget (feedback_ecology.h:2401-2412), in the same block
        // as the non-finite event. resetForRecovery() is exactly reset() minus
        // that one call, which is why it satisfies both constraints.
        for (std::size_t serviced = 0; serviced < kResetsPerControlChunk; ++serviced) {
            const int slot = lowestSetVoice(nonFinitePending_);
            if (slot < 0) {
                break;
            }
            const auto v = static_cast<std::size_t>(slot);
            nonFinitePending_ &= ~voiceBit(v);
            voices_[v].resetForRecovery();  // vorago_voice.h:736
            ++nonFiniteRecoveries_;
        }

        // --- 3/4/5. one walk over the pool ----------------------------------
        // ghost  (FR-056, FR-020b): the MAXIMUM over RENDERING voices - the same
        //        non-silencing combine FR-023 establishes per voice.
        // fog    (FR-026): likewise a maximum over RENDERING voices.
        // lowest (FR-051): the lowest SOUNDING voice's frequency, read from the
        //        allocator, over every slot it does not report Idle.
        float ghost = 0.0f;
        float fog = 0.0f;
        float lowest = 0.0f;
        bool sounding = false;
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (isRendering(v)) {
                ghost = std::max(ghost, voices_[v].getGhostRequest());    // vorago_voice.h:942
                fog = std::max(fog, voices_[v].getTidalFogDepth());       // :951
            }
            if (allocator_.getVoiceState(v) == VoiceState::Idle) {        // voice_allocator.h:424
                continue;
            }
            const float hz = allocator_.getVoiceFrequency(v);             // :446
            if (!isFiniteBits(hz) || hz <= 0.0f) {
                continue;
            }
            if (!sounding || hz < lowest) {
                lowest = hz;
                sounding = true;
            }
        }

        // FR-017's gating. The BASE is 0.0 - a base of 0 is what makes a burst a
        // burst - and the burst peak is the engine-owned ghostPeak_.
        atmos_.setLevel(ghostPeak_ * ghost);  // atmosphere_engine.h:982

        // --- Phase 10a FR-031. The spawn path rides the SAME definition of "a
        //     ghost burst" Phase 10's SC-027 detector uses
        //     (vorago_engine_test.cpp:2666-2667, detector :2699-2704): a rising
        //     crossing of half the burst peak, re-armed only below 5 % of it. A
        //     hysteresis band and NOT "was exactly 0, now > 0", because the
        //     combined request is combineWake(0, eco, sched) = max
        //     (vorago_voice.h:1011-1014, :1846) whose ecosystem term
        //     ecosystemDepth_[k] * output[i] (:1734) is CONTINUOUS and prepare()
        //     installs depth 0.85 for every kind (:617) - an exact-zero predicate
        //     would fire at most once per render. THE LATCHED QUANTITY IS THE
        //     GATED VALUE - the very expression written above - so the spawn path
        //     closes with the level gate (SC-010 (c) is then literally SC-027
        //     clause 2's closed arm) and no grain is ever spawned for a ghost
        //     nobody can hear. FR-033: the setLevel write above is NEITHER moved
        //     NOR conditioned. FR-032: with the flag false this block is skipped
        //     entirely, triggerGrain() is never called and the latch does not
        //     advance. runPreRenderControlStep() runs inside processStereoBlock on
        //     the AUDIO THREAD, called only at phase == 0 (:887-889), so Vorago
        //     issues at most one trigger per control chunk.
        if (ghostEventTriggers_) {
            const float gated = ghostPeak_ * ghost;
            if (!ghostTriggerHigh_ && gated >= kGhostTriggerRise) {
                ghostTriggerHigh_ = true;
                atmos_.triggerGrain();  // FR-018
            } else if (ghostTriggerHigh_ && gated <= kGhostTriggerFall) {
                ghostTriggerHigh_ = false;
            }
        }

        // FR-051. When NO voice sounds the LAST VALUE IS HELD and
        // setFundamentalHz is not called at all - never reset to a default,
        // which would glissando the subs on every note. lastSubFundamentalHz_ is
        // the shadow SC-031 reads through subharmonic().getFundamentalHz().
        //
        // The `!= lastSubFundamentalHz_` guard is an equivalence, not a policy:
        // setFundamentalHz only rewrites two phase INCREMENTS and never touches
        // phase (FR-014 there), so re-writing the same value is a no-op - and
        // skipping it keeps two divides out of every control chunk on the
        // overwhelmingly common held-note path. The shadow is the pre-clamp
        // value, which is what makes it a change detector rather than a second
        // copy of the component's state.
        if (sounding && lowest != lastSubFundamentalHz_) {
            lastSubFundamentalHz_ = lowest;
            sub_.setFundamentalHz(lowest);  // subharmonic_engine.h:616
        }

        // FR-026's tidal fog fold. smearBase_ is the ENGINE's own field, written
        // only by setSmearAmount() and reported by getSmearAmount(); THE
        // COMPONENT IS WRITTEN ONLY HERE. That is the whole precedence rule, and
        // it is why the Fog macro row survives the next control chunk. At tidal
        // depth 0 the fold is the identity and the component reads exactly
        // smearBase_.
        smear_.setSmearAmount(std::clamp(smearBase_ + fog, 0.0f, 1.0f));  // spectral_smear.h:376
    }

    /// @brief AFTER the slice that completed a chunk (plan S6.6 steps 6-7).
    ///
    /// Running retirement on the ABSOLUTE grid rather than "once per block" is
    /// what makes retirement timing partition-invariant, which is exactly what
    /// SC-007's getActiveVoiceCount() clause reads.
    void runPostRenderControlStep() noexcept {
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            // FR-047's deferred retirement. voiceFinished() early-outs on
            // anything but Releasing (voice_allocator.h:288-292), so the state
            // test is stated rather than relied upon.
            if (allocator_.getVoiceState(v) == VoiceState::Releasing && voices_[v].isFinished()) {
                allocator_.voiceFinished(v);  // :288
            }
            // Orphan-tail bookkeeping: a post-shrink orphan that has rung itself
            // out is no longer a slot FR-044's teardown has anything to tear
            // down, so the bit is dropped and the next note-on onto it takes the
            // plain path (seraphis_engine.h:1236-1243).
            if ((orphanTail_ & voiceBit(v)) != 0u && voices_[v].isFinished()) {
                orphanTail_ &= ~voiceBit(v);
            }
        }
    }

    /// The per-slice scratch. kControlChunkSamples is the LARGEST slice the loop
    /// can ask for, because every slice is clipped to the remainder of the
    /// current chunk.
    std::array<float, kControlChunkSamples> busL_{};  ///< the voice-sum bus
    std::array<float, kControlChunkSamples> busR_{};
    std::array<float, kControlChunkSamples> vL_{};  ///< one voice's contribution
    std::array<float, kControlChunkSamples> vR_{};
    /// The ghost's WET return. Separate from the bus because AtmosphereEngine
    /// explicitly does not support in-place (atmosphere_engine.h:671-672).
    std::array<float, kControlChunkSamples> atmosL_{};
    std::array<float, kControlChunkSamples> atmosR_{};

    /// FR-051's held value. 0 means "no voice has ever sounded", in which case
    /// SubharmonicEngine still holds its own post-prepare fundamental.
    float lastSubFundamentalHz_ = 0.0f;

    // =========================================================================
    // Stealing (FR-044)
    // =========================================================================

    /// @brief Select the steal victim and FREE ITS SLOT with the allocator's own
    ///        public surface.
    ///
    /// VoiceAllocator has NO `Quietest` mode (voice_allocator.h:55-60) and
    /// "[d]oes NOT own or process any DSP" (:124-125), so it cannot see a level
    /// and there is no pre-emption hook on noteOn. The engine therefore selects
    /// (selectStealVictim, above), then frees the slot so that the allocator's
    /// own idle search has EXACTLY ONE candidate.
    ///
    /// @return true when a victim was found and freed.
    bool freeChosenVictimSlot() noexcept {
        // The bound is RE-STATED from polyphony_ (which prepare() and
        // setPolyphony() already clamp) purely so it is provable AT THE POINT OF
        // USE: GCC unrolls the selection loop and, unable to carry that clamp
        // this far, reports -Warray-bounds otherwise.
        const std::size_t voiceCount = std::min(polyphony_, kMaxVoices);
        std::array<VoiceState, kMaxVoices> states{};
        std::array<float, kMaxVoices> levels{};
        for (std::size_t i = 0; i < voiceCount; ++i) {
            states[i] = allocator_.getVoiceState(i);   // voice_allocator.h:424
            levels[i] = voices_[i].getCurrentLevel();  // vorago_voice.h:906
        }
        const int chosen = selectStealVictim(
            std::span<const VoiceState>{states.data(), voiceCount},
            std::span<const float>{levels.data(), voiceCount},
            std::span<const std::uint64_t>{voiceSerial_.data(), voiceCount});
        if (chosen < 0) {
            // Unreachable behind noIdleVoice(): a slot that is neither Idle nor
            // Releasing nor Active does not exist. Stated as a guard rather than
            // an assert so a future VoiceState addition degrades to "let the
            // allocator steal by itself" instead of freeing slot 0 by accident.
            return false;
        }
        const std::size_t victim = static_cast<std::size_t>(chosen);

        // An Active victim has to reach Releasing before voiceFinished() will
        // touch it (voice_allocator.h:288-292 early-outs on anything else). The
        // events are DISCARDED: this is bookkeeping, not a musical release - the
        // voice is about to be silenced by the teardown, and dispatching a
        // NoteOff to it would call voices_[victim].noteOff() one step before that.
        if (allocator_.getVoiceState(victim) == VoiceState::Active) {
            const int victimNote = allocator_.getVoiceNote(victim);  // :406
            if (victimNote >= 0) {
                static_cast<void>(
                    allocator_.noteOff(static_cast<std::uint8_t>(victimNote)));  // :257
            }
        }
        // Now legal, and it is what returns the slot to Idle.
        allocator_.voiceFinished(victim);  // :288
        stealTeardown_ |= voiceBit(victim);
        lastStolenVoice_ = static_cast<int>(victim);
        return true;
    }

    /// @brief FR-044's three-step teardown, in the one order the criteria pin.
    ///
    /// silence() arms the kSilenceRampMs anti-click decay from the last sample
    /// the voice actually emitted and hard-clears every sub-component
    /// (vorago_voice.h:755-768); resetForSteal() - NEVER reset() - then restores
    /// run state while PRESERVING that armed tail (:747), which is the whole
    /// reason the voice carries four clearing entry points; noteOn() starts the
    /// incoming note.
    ///
    /// BOTH CLEARING CALLS ARE ON B-7's RT-SAFE SIDE. With a 4-voice pool and
    /// minutes-long tails STEALING IS THE NORMAL ALLOCATION PATH, so the
    /// pre-B-7 shape would have run ~1.6 MB of std::fill per steal at 48 kHz
    /// inside this function, on the audio thread.
    void teardownAndStart(std::size_t i, float frequencyHz, float velocity) noexcept {
        voices_[i].silence();
        voices_[i].resetForSteal();
        voices_[i].noteOn(frequencyHz, velocity);
    }

    /// The dispatch table, following PolySynthEngine::dispatchPolyNoteOn
    /// (poly_synth_engine.h:597-620) with the Steal row SPLIT BY PROVENANCE.
    ///
    /// `served` is what keeps the serial bumped exactly once per dispatched
    /// span: the allocator's steal path pushes Steal and NoteOn for the SAME
    /// slot in one span (voice_allocator.h:1025-1066), and two bumps would
    /// corrupt the tie-break key selectStealVictim reads.
    void dispatch(std::span<const VoiceEvent> events) noexcept {
        std::uint32_t served = 0u;
        for (const VoiceEvent& e : events) {
            const std::size_t i = static_cast<std::size_t>(e.voiceIndex);
            if (i >= kMaxVoices) {
                continue;
            }
            const std::uint32_t bit = voiceBit(i);
            const float velocity = static_cast<float>(e.velocity) / 127.0f;
            switch (e.type) {
                case VoiceEvent::Type::NoteOn:
                    if (((orphanTail_ | stealTeardown_) & bit) != 0u) {
                        // TWO provenances, one teardown.
                        //
                        // orphanTail_: the slot a polyphony shrink force-idled
                        // while it was still sounding. `!isFinished()` alone
                        // would also match every live retrigger target and wipe
                        // a sounding voice.
                        //
                        // stealTeardown_: the slot freeChosenVictimSlot() just
                        // freed. Because that happens BEFORE allocator_.noteOn,
                        // the allocator sees an idle slot and emits a plain
                        // NoteOn, so the Steal row below never fires on a real
                        // steal - and FR-044 still demands
                        // silence() -> resetForSteal() -> noteOn() on the
                        // victim, inside this same block.
                        teardownAndStart(i, e.frequency, velocity);
                        orphanTail_ &= ~bit;
                        stealTeardown_ &= ~bit;
                    } else {
                        voices_[i].noteOn(e.frequency, velocity);
                    }
                    if ((served & bit) == 0u) {
                        voiceSerial_[i] = nextSerial_++;
                        served |= bit;
                    }
                    break;
                case VoiceEvent::Type::NoteOff:
                    voices_[i].noteOff();
                    break;
                case VoiceEvent::Type::Steal:
                    // The allocator emits Steal for the OUTGOING note of an
                    // ordinary same-note retrigger too (voice_allocator.h:
                    // 239-242, :846-853), with no pool saturation involved. On
                    // that provenance the event is pure bookkeeping and the
                    // NoteOn that follows it in the same span does the work;
                    // running the teardown here would wipe a live, sounding
                    // voice, call noteOn() twice and bump the serial twice.
                    if (static_cast<int>(i) == retriggerSlot_) {
                        break;  // ignore the event ENTIRELY
                    }
                    // Engine-initiated steal. With the free-before-allocate
                    // selection in place this branch is unreachable by
                    // construction; retained as the defensive path.
                    teardownAndStart(i, e.frequency, velocity);
                    stealTeardown_ &= ~bit;
                    if ((served & bit) == 0u) {
                        voiceSerial_[i] = nextSerial_++;
                        served |= bit;
                    }
                    break;
            }
        }
    }

    // =========================================================================
    // State
    // =========================================================================
    // Members T015 owns (the per-slice scratch buffers and the held sub
    // fundamental) are declared WITH it, not here, so no field sits unused
    // (clang's -Wunused-private-field is on under -Wall).

    std::array<VoragoVoice, kMaxVoices> voices_;
    VoiceAllocator allocator_;

    AtmosphereEngine atmos_;   ///< the GLOBAL ghost tap (FR-056, OQ-1(b))
    SubharmonicEngine sub_;    ///< the held-fundamental sub tail (FR-051)
    SpectralSmear smear_;      ///< the global fog (FR-052)
    TapeSaturator satL_;       ///< mono, in place (tape_saturator.h:335)
    TapeSaturator satR_;
    TruePeakLimiter limiter_;  ///< stereo, in place (true_peak_limiter.h:104)

    OnePoleSmoother sumGain_;   ///< FR-043, target 1/sqrt(polyphony)
    float sumGainHeld_ = 1.0f;  ///< read ONCE per control chunk (T015)

    std::uint64_t sampleCounter_ = 0;  ///< the FR-007 absolute grid anchor

    /// FR-026's fold BASE. Written only by setSmearAmount and reported by
    /// getSmearAmount(); the COMPONENT is written by the control step.
    float smearBase_ = kDefaultSmearAmount;
    float smearDecoherence_ = kDefaultSmearDecoherence;
    float smearTilt_ = 0.0f;
    float ghostPeak_ = kGhostBurstPeak;
    bool ghostEventTriggers_ = false;  ///< FR-030's config shadow
    bool ghostTriggerHigh_ = false;    ///< FR-031's latch, the VoragoVoice::eventWasActive_
                                       ///< shape (vorago_voice.h:1775-1784)
    float ghostReverseProbability_ = 0.0f;  ///< FR-006 (Phase 12)
    bool ghostReverseSet_ = false;          ///< FR-006: the setter overrides the config
    bool ghostTriggersSet_ = false;         ///< FR-006: the setter overrides the config
    float subToneOffsetDb_ = 0.0f;
    /// FR-005's per-tone base, initialised to the shipped constants so
    /// applySubToneLevels() computes the identical float sum until a set.
    std::array<float, SubharmonicEngine::kNumTones> subToneBaseDb_ =
        SubharmonicEngine::kDefaultToneLevelDb;
    float subTracking_ = kDefaultSubTracking;
    float atmosBlur_ = kDefaultAtmosBlur;
    float outputSaturation_ = kOutputSaturation;
    float outputDriveDb_ = kOutputDriveDb;  ///< spec B-2 (Phase 12)
    OnePoleSmoother makeupSm_;              ///< -> dbToGain(-outputDriveDb_), 5 ms per-sample ramp

    std::uint32_t nonFinitePending_ = 0u;      ///< FR-072's deferred-reset bitmask (T015)
    std::uint32_t nonFiniteRecoveries_ = 0u;   ///< FR-072's lifetime counter

    /// Slots a polyphony shrink force-idled while they were still sounding. THE
    /// teardown predicate - written only by setPolyphony(), cleared by the
    /// NoteOn row and by T015's post-render control step.
    std::uint32_t orphanTail_ = 0u;

    /// The slot the CURRENT noteOn() is a same-note retrigger of, or -1. Live
    /// only for the duration of one dispatch; it is what splits the allocator's
    /// two Steal provenances.
    int retriggerSlot_ = -1;

    /// THE RA-4 ASSERTION FLAG, NOT A DEFERRAL MECHANISM - the name is inherited
    /// from seraphis_engine.h:516-519 and is the one thing about it a reader
    /// mis-reads. It is raised when freeChosenVictimSlot() frees a victim slot
    /// and cleared in dispatch()'s NoteOn row; noteOn() then asserts it is back
    /// to 0. A bit that survives one noteOn() is the RA-4 defect.
    std::uint32_t stealTeardown_ = 0u;

    /// FR-044's tie-break key. Bumped exactly once per dispatched span that
    /// lands a note on the slot.
    std::array<std::uint64_t, kMaxVoices> voiceSerial_{};
    std::uint64_t nextSerial_ = 1u;

    /// The slot the LAST steal took, or -1 if the engine has never stolen.
    int lastStolenVoice_ = -1;

    std::size_t polyphony_ = kDefaultPolyphony;
    std::uint32_t seed_ = 1u;
    bool prepared_ = false;
};

static_assert(sizeof(VoragoEngine) <= VoragoEngine::kEngineSizeBound,
              "FR-013: VoragoEngine has grown past kMaxVoices voice slots plus 64 KiB - "
              "check for a forbidden member before raising the bound");

}  // namespace DSP
}  // namespace Krate
