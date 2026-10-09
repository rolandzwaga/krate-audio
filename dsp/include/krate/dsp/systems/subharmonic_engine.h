// ==============================================================================
// Layer 3: System Component - SubharmonicEngine
// ==============================================================================
// Three shipped SubOscillator dividers hung off two synthetic PhaseAccumulator
// masters (f for Div2/Div4, 4f/3 for FifthBelow), summed with the house
// three-factor gain (level ramp x breath gain x backstop gate), pushed through
// TwoPoleLP -> SaturationProcessor -> xtrackGain -> DCBlocker2, and ADDED to an
// untouched dry path. Vorago's subharmonic reinforcement layer: it is a
// generator that follows the body's level, not a bass enhancer inserted in
// series - the dry path is never filtered, never sanitised and never clamped.
//
// NO NEW DSP MATHEMATICS ANYWHERE and NO AMENDMENT TO ANY SHIPPED HEADER: every
// stage is a composed Layer 0-2 component, and the ten headers this file
// composes are byte-unchanged by this phase (FR-080).
//
// Feature: vorago-phase6-subharmonic
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept throughout; prepare() is the ONLY
//   allocating method - see getAllocatedBytes below)
// - Principle III: Modern C++ (C++20, RAII, value semantics, no owning ptrs)
// - Principle IX: Layer 3 (composes Layer 0-2 components only)
// - Principle X: DSP Constraints (de-zippered gains, glided cutoff, DC-blocked
//   output, a four-rung boundedness ladder)
// - Principle XI: Performance Budget (< 0.5 % of a 512-sample block at 48 kHz,
//   SC-013)
//
// Reference: specs/vorago-phase6-subharmonic/spec.md
//            specs/vorago-phase6-subharmonic/plan.md
//
// BUILD STATE (tasks.md T010 - the render path).
// T007's skeleton pass established:
//   * the plan S1.1 include list and the FOUR deleted copy/move operations
//     (S14 C-1 - MinBlepTable is non-copyable but MOVABLE, so an implicit move
//     is a use-after-free waiting for Phase 10);
//   * every plan S1.2 constant with EVERY static_assert live - each one is a
//     real compile-time assertion about a shipped constant, so this file failing
//     to compile IS the falsification for the skeleton;
//   * the nested Tone / PrepareConfig types and index();
//   * the complete plan S1.4 public surface, DECLARED, with the read surface
//     returning the member (or the composed object) it names and everything
//     behavioural a documented-neutral stub carrying its owning task number;
//   * the complete plan S1.5 private state, in declaration order;
//   * the plan S1.6 salt table with its overlap static_assert;
//   * the plan S7.5 fault-injection probe forward declaration and friend.
// T008 added, in plan order: the S2.1 clamp helpers, S2's thirteen prepare()
//   steps, S2.2's applyDefaults() table, S2.3's setFundamentalHz() and
//   getToneFrequencyHz(), S3.1's reset(), S3.2's setSeed(), refreshBreath(), the
//   S9 allocation ledger, and every FR-060 setter except setSubToMainEnabled.
// T009 landed the S5.2 backstop law inside gateSteady() - the ONE owner of the
//   FR-016 thresholds - which turns the already-final prepare() step (10) and
//   reset() step (5) snaps into real latches for isToneInfrasonicFloored
//   (SC-020 (d)). setFundamentalHz()/getToneFrequencyHz() (plan S2.3) and the
//   structural octave assignment (plan S4.1, prepare() step 5) landed with T008
//   and are covered from here by SubharmonicEngine_ToneMapping.
// T010 landed the render path: processBlock() delegating to
//   processBlockTapped() (so FR-063's bit-identity is STRUCTURAL), the FR-050
//   guard ladder (null channel pointer -> numSamples == 0 -> the pre-prepare
//   PASSTHROUGH, which deliberately differs from FeedbackEcology's zero-fill),
//   the FR-007 chunk loop over an ABSOLUTE control residue, plan S5.2's
//   normatively-ordered updateControl() and plan S6's renderChunk() - the
//   FR-040 chain, the S7.6 sensor guard, the FR-055 trap, the FR-062 tap and
//   the FR-054 clamp on the sub contribution only.
// T011 landed dormancy: isToneDormant()'s exact `== 0.0f` predicate (plan
//   S5.4), which allTonesDormant() already composed, updateControl() step
//   (6)'s chainActive_ EDGE LATCH with FR-026's three ordered resets, and
//   renderChunk() step (4)'s skip. The follower is deliberately NOT reset at
//   the sleep edge - it is the sensor.
// T012 landed setSubToMainEnabled() - the one line that makes the FR-064 gate
//   already sitting in renderChunk() step (9) reachable. Nothing else moved:
//   the tap (step (7)) was already independent of the flag and already
//   post-tracking/pre-wet-gain, and the FR-054 clamp (step (8)) was already
//   scoped to the WET SUB alone rather than to (in + sub). T012's two cases -
//   SubharmonicEngine_SubToMainRouting and SubharmonicEngine_ClampScope - are
//   what turn both of those from "written that way" into asserted behaviour.
// THE HEADER IS FEATURE-COMPLETE from here: T013 onward are test-only tasks.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>          // L0: dbToGain, detail::isFinite/isNaN/isInf
#include <krate/dsp/core/math_constants.h>    // L0: FR-001's normative list (see the note below)
#include <krate/dsp/core/phase_utils.h>       // L0: PhaseAccumulator, calculatePhaseIncrement
#include <krate/dsp/core/random.h>            // L0: deriveStreamSeed
#include <krate/dsp/primitives/smoother.h>    // L1: LinearRamp
#include <krate/dsp/primitives/minblep_table.h>  // L1: the ONE shared table (D-8)
#include <krate/dsp/primitives/two_pole_lp.h>    // L1: chain stage 1 (FR-040)
#include <krate/dsp/primitives/dc_blocker.h>     // L1: DCBlocker2, chain stage 3 (FR-042)
#include <krate/dsp/processors/sub_oscillator.h>       // L2: SubOscillator, SubOctave, SubWaveform
#include <krate/dsp/processors/breathing_modulator.h>  // L2: FR-021's per-tone breath
#include <krate/dsp/processors/envelope_follower.h>    // L2: the FR-030 sensor, DetectionMode
#include <krate/dsp/processors/saturation_processor.h> // L2: chain stage 2 (FR-041)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// WHY THERE IS NO LAYER 3 AND NO LAYER 4 INCLUDE: this component composes Layer
// 0-2 only, so tools/lint-layers.js passes structurally rather than by luck.
//
// WHY core/math_constants.h IS LISTED: it is in FR-001's normative dependency
// list. The header's own arithmetic needs only std::exp2 / std::log2 / std::abs
// from <cmath>, so the include documents the dependency FR-001 declares rather
// than a live use. If a later task introduces a use, the include is already
// where that use expects it.

namespace Krate::DSP {

namespace detail {
/// @brief FR-055 fault-injection probe. DECLARED HERE, DEFINED ONLY BY A TEST TU.
///
/// FR-055's per-sample non-finite trap inside renderChunk() is UNREACHABLE
/// through the public API: every setter rejects a non-finite argument (FR-009),
/// SubOscillator::sanitize maps NaN to 0.0f and clamps to [-2, +2]
/// (sub_oscillator.h:356-364), Biquad::process resets and returns 0.0f on a
/// non-finite input, and the S7.6 guard keeps the follower's input clean. The
/// two stages that CANNOT self-heal - DCBlocker2::process, which has no
/// finiteness branch at all (dc_blocker.h:328-341), and
/// EnvelopeFollower::processSample, which documents "Does NOT validate input"
/// (envelope_follower.h:162) - are only reachable from inside the class. This
/// friend is therefore the only way rung 4 is testable at all.
///
/// It costs nothing at run time and adds no public surface: the library never
/// defines it, so a shipping build has no way to call it. Pattern quoted from
/// systems/feedback_ecology.h:166-172 (declaration) and :1525 (friend).
///
/// ODR: swept this session - `SubharmonicEngineNonFiniteProbe` has zero matches
/// in dsp/, plugins/ or tools/ outside this phase's own artefacts.
struct SubharmonicEngineNonFiniteProbe;
}  // namespace detail

/// @brief Three frequency-divided sub tones added to an untouched dry path.
///
/// @par Layer: 3 (systems/). Dependencies: Layers 0-2 only. NO Layer 3 peer, no
///      Layer 4.
///
/// @par Real-Time Safety: every method is noexcept and allocation-free except
///      prepare(), which is the ONLY allocating method (FR-073,
///      getAllocatedBytes()).
///
/// @par Structure (FR-010, plan S4.1)
/// | index | Tone         | master               | SubOctave  | rendered |
/// |-------|--------------|----------------------|------------|----------|
/// | 0     | `Div2`       | `masterUnison_` (f)  | OneOctave  | f/2      |
/// | 1     | `Div4`       | `masterUnison_` (f)  | TwoOctaves | f/4      |
/// | 2     | `FifthBelow` | `masterFifth_` (4f/3)| OneOctave  | 2f/3     |
///
/// @par The dry path is sacred (FR-050): the component ADDS a mono sub scalar to
///      both channels and touches the input in no other way. It does not
///      sanitise it, does not filter it and does not clamp it - FR-054's clamp
///      binds the SUB CONTRIBUTION ONLY (Q6, D-12).
class SubharmonicEngine {
public:
    // =========================================================================
    // Constants (plan S1.2) - every static_assert below is live
    // =========================================================================

    // ---- structure ----------------------------------------------------------
    static constexpr std::size_t kNumTones = 3;                    // FR-010

    /// The shared library-wide control clock (harmonic_cloud.h:144,
    /// continuous_body.h:97, noise_organism.h:150,
    /// resonance_drift_network.h:135). A component that drifted off it would
    /// decorrelate the per-voice modulation grid at Phase 10.
    static constexpr std::size_t kControlChunkSamples = 64;        // FR-007
    static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

    /// prepare()'s sample-rate floor (FR-006). NOT 1 Hz: kMasterNyquistRatio * 1
    /// Hz and kLowpassNyquistRatio * 1 Hz both fall BELOW their paired floors,
    /// inverting the std::clamp bounds - UB, and MSVC's <algorithm> fires
    /// _STL_VERIFY.
    static constexpr double kMinUsableSampleRate = 8000.0;         // FR-006

    static constexpr float kGainRampMs  = 50.0f;                   // noise_organism.h:178
    static constexpr float kGlideMs     = 50.0f;                   // S5.3, the cutoff glide
    static constexpr float kOutputClamp = 4.0f;                    // noise_organism.h:180
    static constexpr float kCutoffPushRelative = 1e-3f;            // S5.3 (feedback_ecology.h:205)

    static constexpr std::uint32_t kDefaultSeed = 0x5E6BA51Cu;

    // ---- minBLEP table (FR-004, D-8) ----------------------------------------
    static constexpr std::size_t kBlepOversampling  = 64;
    static constexpr std::size_t kBlepZeroCrossings = 8;
    static_assert(kBlepZeroCrossings * 2 <= 64,
                  "SubOscillator::prepare rejects a table with length() > 64 "
                  "(sub_oscillator.h:143-147) and then returns 0.0f forever");

    // ---- pitch (FR-013) ------------------------------------------------------
    static constexpr float kMinFundamentalHz     = 8.0f;
    static constexpr float kMaxFundamentalHz     = 4186.0f;        // C8, A-5
    static constexpr float kDefaultFundamentalHz = 55.0f;          // A1
    /// Applied to the 4f/3 master, NOT to f: the largest master increment is 0.4.
    static constexpr float kMasterNyquistRatio   = 0.3f;
    static_assert(static_cast<double>(kMasterNyquistRatio) * kMinUsableSampleRate
                      > static_cast<double>(kMinFundamentalHz),
                  "the FR-013 clamp range must stay ordered at the lowest accepted rate");
    static_assert(kMasterNyquistRatio * (4.0f / 3.0f) < 1.0f,
                  "PhaseAccumulator::advance subtracts 1.0 exactly once (phase_utils.h:161-168)");

    // ---- infrasonic handling (FR-016, FR-042, Q2) ---------------------------
    static constexpr float kMinToneHz          = 12.0f;            // far-below backstop gate
    static constexpr float kInfrasonicFilterHz = 18.0f;            // DCBlocker2 corner
    static_assert(kMinToneHz < kInfrasonicFilterHz,
                  "the backstop must sit below the filter it backs up");

    // ---- per-tone level and breathing (FR-020, FR-021, FR-022) --------------
    static constexpr float kMinToneLevelDb = -60.0f;               // exact fader bottom
    static constexpr float kMaxToneLevelDb = +6.0f;
    static constexpr float kBreathGainSpan = 0.45f;                // noise_organism.h:174

    /// FR-003's per-tone defaults, declared HERE rather than only in
    /// applyDefaults()'s prose: they differ per tone, so they cannot be
    /// `ToneState` member initialisers, and the constructor (S1.1) needs them as
    /// much as prepare() step (9) does.
    static constexpr std::array<float, kNumTones> kDefaultToneLevelDb{-18.0f, -24.0f, -30.0f};
    static constexpr std::array<float, kNumTones> kDefaultToneBreathRateHz{0.037f, 0.023f, 0.014f};
    static constexpr std::array<float, kNumTones> kDefaultToneBreathDepth{0.35f, 0.25f, 0.45f};

    /// FR-021's rate range. The engine clamps with THESE and then pushes; the
    /// modulator clamps again with its own (breathing_modulator.h:170-172) and
    /// `getToneBreathRate` forwards to `breath.getRate()` (S8), so the two
    /// clamps must agree or the getter would contradict the engine's declared
    /// range. The static_assert is what keeps them agreeing.
    static constexpr float kMinBreathRateHz = 0.01f;
    static constexpr float kMaxBreathRateHz = 0.5f;
    static_assert(kMinBreathRateHz == BreathingModulator::kMinRate &&
                  kMaxBreathRateHz == BreathingModulator::kMaxRate,
                  "FR-021's range must mirror the modulator's own (breathing_modulator.h:108-110)");
    /// S14 C-5. WITHOUT a non-zero irregularity the modulator never touches its
    /// RNG (breathing_modulator.h:262-271), the FR-070 seed is inert, and
    /// SC-011 (b) is unreachable. This is the smallest value that makes the seed
    /// observable. Not exposed as a setter; FR-060's list stays closed.
    static constexpr float kDefaultBreathIrregularity = 0.25f;

    // ---- tracking (FR-030 - FR-035) -----------------------------------------
    static constexpr float kDefaultTrackingAmount    = 1.0f;
    static constexpr float kMinTrackReferenceDb      = -48.0f;
    static constexpr float kMaxTrackReferenceDb      = 0.0f;
    static constexpr float kDefaultTrackReferenceDb  = -18.0f;
    static constexpr float kDefaultFollowerAttackMs  = 120.0f;
    static constexpr float kDefaultFollowerReleaseMs = 800.0f;
    /// FR-031's ranges, named for the same reason as kMin/kMaxBreathRateHz: the
    /// engine clamps with these, the follower clamps again with its own
    /// (envelope_follower.h:219-229), and getFollowerAttackMs /
    /// getFollowerReleaseMs forward to follower_.getAttackTime() /
    /// getReleaseTime() (S8). Without the named bounds FR-061's "the getter
    /// reports the applied (clamped) value" has no stated range at this level.
    static constexpr float kMinFollowerAttackMs  = 0.1f;
    static constexpr float kMaxFollowerAttackMs  = 500.0f;
    static constexpr float kMinFollowerReleaseMs = 1.0f;
    static constexpr float kMaxFollowerReleaseMs = 5000.0f;
    static_assert(kMinFollowerAttackMs == EnvelopeFollower::kMinAttackMs &&
                  kMaxFollowerAttackMs == EnvelopeFollower::kMaxAttackMs &&
                  kMinFollowerReleaseMs == EnvelopeFollower::kMinReleaseMs &&
                  kMaxFollowerReleaseMs == EnvelopeFollower::kMaxReleaseMs,
                  "FR-031's ranges must mirror the follower's own (envelope_follower.h:88-91)");
    static_assert(kDefaultFollowerAttackMs != EnvelopeFollower::kDefaultAttackMs &&
                  kDefaultFollowerReleaseMs != EnvelopeFollower::kDefaultReleaseMs,
                  "FR-031's defaults differ from the shipped 10/100 ms, so they MUST be pushed; "
                  "S8's forwarding getters are what make an omitted push detectable");

    // ---- chain (FR-040 - FR-042) --------------------------------------------
    static constexpr float kMinLowpassHz        = 40.0f;
    static constexpr float kMaxLowpassHz        = 2000.0f;
    static constexpr float kDefaultLowpassHz    = 120.0f;
    /// Inert at every accepted rate (0.45 * 8000 = 3600 > kMaxLowpassHz).
    /// Written so the ordering std::clamp requires is visible HERE rather than
    /// inferred.
    static constexpr float kLowpassNyquistRatio = 0.45f;
    static_assert(kLowpassNyquistRatio * static_cast<float>(kMinUsableSampleRate) > kMinLowpassHz,
                  "the FR-040 clamp range must stay ordered at the lowest accepted rate");

    static constexpr float kMinDriveDb     = 0.0f;
    static constexpr float kMaxDriveDb     = 12.0f;
    static constexpr float kDefaultDriveDb = 3.0f;
    static_assert(kMaxDriveDb <= 24.0f && kMinDriveDb >= -24.0f,
                  "drive must stay inside SaturationProcessor::kMin/kMaxGainDb (:104-105)");

    // ---- output (FR-050 - FR-054) -------------------------------------------
    static constexpr float kMinWetGainDb     = -60.0f;             // exact fader bottom
    static constexpr float kMaxWetGainDb     = +6.0f;
    static constexpr float kDefaultWetGainDb = 0.0f;

    // ---- the FR-052 structural bounds, as real static_asserts ---------------
    /// SubOscillator::sanitize clamps EVERY tone's output to [-2, +2]
    /// (sub_oscillator.h:356-364) - the Square path's minBLEP residual can and
    /// does exceed 1.0. The spec's original FR-052 formula omitted this factor
    /// (S14 C-4).
    static constexpr float kSubOscillatorOutputBound = 2.0f;
    static constexpr float kMaxPreSaturationMagnitude =
        static_cast<float>(kNumTones) * kSubOscillatorOutputBound *
        dbToGain(kMaxToneLevelDb) * (1.0f + kBreathGainSpan);          // ~= 17.36
    /// FastMath::fastTanh is exactly +/-1 beyond +/-3.5 and 0.99924 at the
    /// domain edge; outputGain = dbToGain(-driveDb) <= 1 for driveDb >= 0.
    static constexpr float kSaturatorOutputBound = 1.0f;
    /// DCBlocker2 at Q = 1/sqrt(3) <= 1/sqrt(2): magnitude rises monotonically
    /// from 0 at DC toward 1 and never exceeds it (verified numerically at
    /// 18 Hz / 48 kHz: 0.999998).
    static constexpr float kInfrasonicFilterPeakGain = 1.0f;
    static constexpr float kMaxPreClampMagnitude =
        kSaturatorOutputBound * kInfrasonicFilterPeakGain * dbToGain(kMaxWetGainDb);  // ~= 2.00

    static_assert(kMaxPreSaturationMagnitude > kSaturatorOutputBound,
                  "the saturator, not the clamp, must be the stage that catches the peak");
    static_assert(kMaxPreClampMagnitude < kOutputClamp,
                  "FR-054's clamp is a backstop, not a shaping stage");

    // =========================================================================
    // Nested types (plan S1.3)
    // =========================================================================

    /// FR-010. APPEND ONLY - this becomes a persisted plugin parameter at Phase
    /// 12. Nested deliberately: SubOctave and SubWaveform are already at
    /// namespace scope in a header this one includes (sub_oscillator.h:48, :60),
    /// and a third Sub* enum out there is a future ODR liability for no gain.
    /// Precedent: EnvelopeFilter::FilterType (processors/envelope_filter.h:89).
    enum class Tone : std::uint8_t { Div2 = 0, Div4 = 1, FifthBelow = 2 };

    /// FR-003. Callers MUST use designated initialisers -
    /// PrepareConfig{.maxBlockSamples = 512} - so no narrowing conversion hides
    /// in a positional brace init (Clang errors where MSVC does not). Nested,
    /// following NoiseOrganism::PrepareConfig (noise_organism.h:190) and
    /// ResonanceDriftNetwork::PrepareConfig (:299).
    struct PrepareConfig {
        /// Clamped [64, 8192], retained and reported. It sizes exactly ONE
        /// thing: the SaturationProcessor::dryBuffer_ this component never reads
        /// (FR-073).
        std::size_t maxBlockSamples = 2048;
    };

    [[nodiscard]] static constexpr std::size_t index(Tone t) noexcept {
        return static_cast<std::size_t>(t);
    }

    // =========================================================================
    // Construction (plan S1.1)
    // =========================================================================

    /// FR-003: construction is allocation-free but NOT `= default`. The three
    /// per-tone configuration scalars (level, breath rate, breath depth) differ
    /// per tone, so they cannot be expressed as in-class member initialisers on
    /// `ToneState`; the constructor runs the same `applyDefaults()` that
    /// prepare() step (9) runs, so every getter reports the
    /// FR-020/FR-021/FR-031/FR-035/FR-040/FR-041/FR-051 default on a
    /// default-constructed object, exactly as FR-003 requires. `applyDefaults()`
    /// allocates nothing and must be safe on an unprepared object (S2.2) - in
    /// particular the pushes the forwarding getters of S8 read back (follower
    /// attack/release, breath rate, waveform) are NOT gated on `prepared_`.
    /// Audio state is still unusable until prepare() (FR-050's passthrough).
    SubharmonicEngine() noexcept { applyDefaults(); }
    ~SubharmonicEngine() = default;

    // The three SubOscillators hold `const MinBlepTable*` pointing at THIS
    // object's blepTable_ member (sub_oscillator.h:118, :370). MinBlepTable is
    // non-copyable (minblep_table.h:56-57), so the copy operations are already
    // implicitly deleted - but it IS movable (:58-59), so a MOVE would be
    // implicitly generated, would move the table's vectors out, and would leave
    // every SubOscillator pointing at the MOVED-FROM object. That object then
    // destructs and all three tones read freed memory. Deleting the move is not
    // defensive: it is the only thing standing between this composition and a
    // use-after-free no test would provoke. Phase 10 therefore holds a
    // SubharmonicEngine by value as a member, never in a reallocating container.
    SubharmonicEngine(const SubharmonicEngine&) = delete;
    SubharmonicEngine& operator=(const SubharmonicEngine&) = delete;
    SubharmonicEngine(SubharmonicEngine&&) = delete;
    SubharmonicEngine& operator=(SubharmonicEngine&&) = delete;

    // =========================================================================
    // Lifecycle (plan S1.4, S2, S3.1)
    // =========================================================================

    /// @brief The ONLY allocating method (FR-004, FR-073). NOT real-time safe.
    /// @param sampleRate Non-finite or below kMinUsableSampleRate is raised to
    ///        the floor; the applied value is reported by getSampleRate().
    /// @param config maxBlockSamples is clamped to [64, 8192] and reported by
    ///        getMaxBlockSamples().
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // --- (1) sample rate -------------------------------------------------
        sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));
        const auto fs = static_cast<float>(sampleRate_);

        // --- (2) clamp the caller's request ONCE, here ------------------------
        // applyDefaults() never touches a PrepareConfig field (the
        // noise_organism.h rule), so this is the single owner of the value.
        config_.maxBlockSamples =
            std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});

        // --- (3) cache the rate-derived ceilings ------------------------------
        // Built ONCE, so no clamp pair is ever assembled inline; the two S1.2
        // static_asserts prove both pairs stay ordered at the 8 kHz floor.
        maxFundamentalHz_ = std::min(kMaxFundamentalHz, kMasterNyquistRatio * fs);
        maxLowpassHz_     = std::min(kMaxLowpassHz, kLowpassNyquistRatio * fs);

        // --- (4) the shared minBLEP table, BEFORE any sub-oscillator ----------
        // Correctness, not tidiness: SubOscillator::prepare sets prepared_ =
        // false on a null or unprepared table (sub_oscillator.h:143-147) and
        // process() then returns 0.0f FOREVER (:222-224). A Sine-only
        // configuration on an unprepared table renders SILENCE.
        blepTable_.prepare(kBlepOversampling, kBlepZeroCrossings);

        // --- (5) the three tone generators ------------------------------------
        // The octave is STRUCTURAL (plan S4.1): no setter exposes it. Assigning
        // a fresh SubOscillator re-points a live object at this object's table
        // on a re-prepare; Residual's default constructor (minblep_table.h:410)
        // makes the assigned-in temporary allocation-free, and osc.prepare()
        // then allocates the real 16-float residual.
        constexpr std::array<SubOctave, kNumTones> kToneOctave{
            SubOctave::OneOctave,    // Div2       -> f/2
            SubOctave::TwoOctaves,   // Div4       -> f/4
            SubOctave::OneOctave};   // FifthBelow -> (4f/3)/2
        for (std::size_t i = 0; i < kNumTones; ++i) {
            tones_[i].osc = SubOscillator(&blepTable_);
            tones_[i].osc.prepare(sampleRate_);
            tones_[i].osc.setOctave(kToneOctave[i]);
        }

        // --- (6) the rest of the composition ----------------------------------
        for (std::size_t i = 0; i < kNumTones; ++i) {
            tones_[i].breath.prepare(sampleRate_);
        }
        follower_.prepare(sampleRate_, config_.maxBlockSamples);
        lowpass_.prepare(sampleRate_);
        saturator_.prepare(sampleRate_, config_.maxBlockSamples);
        blocker_.prepare(sampleRate_, kInfrasonicFilterHz);

        // --- (7) every smoother ------------------------------------------------
        for (std::size_t i = 0; i < kNumTones; ++i) {
            tones_[i].levelRamp.configure(kGainRampMs, fs);
            tones_[i].gate.configure(kGainRampMs, fs);
        }
        trackGainRamp_.configure(kGainRampMs, fs);
        wetGainRamp_.configure(kGainRampMs, fs);
        // THE CONTROL RATE, NOT THE AUDIO RATE. calculateLinearIncrement divides
        // by rampTimeMs * 0.001 * sampleRate (smoother.h:100-108), so a ramp
        // advanced once per 64 samples must be told fs / 64. Handing it fs makes
        // the glide 64x too slow (3.2 s) and SC-020 (c)'s 52 ms window
        // unreachable.
        cutoffGlide_.configure(kGlideMs, fs / static_cast<float>(kControlChunkSamples));

        // --- (8) live BEFORE the defaults are pushed ---------------------------
        prepared_ = true;

        // --- (9) the defaults --------------------------------------------------
        applyDefaults();

        // --- (10) snap everything to steady state, no ramp in flight -----------
        for (std::size_t i = 0; i < kNumTones; ++i) {
            ToneState& t = tones_[i];
            t.levelRamp.snapTo(toneLevelGain(t.levelDb));
            const float steady = gateSteady(i);
            t.gate.snapTo(steady);
            t.lastGateTarget    = steady;
            t.infrasonicFloored = (steady == 0.0f);
            refreshBreath(i);
        }
        // The follower reads 0 at prepare, so envNorm = 0 and the FR-032 law
        // lands on (1 - amount).
        trackGainRamp_.snapTo(1.0f - trackingAmount_);
        wetGainRamp_.snapTo(wetGain(wetGainDb_));
        cutoffGlide_.snapTo(std::log2(lowpassHz_));
        lowpass_.setCutoff(lowpassHz_);
        pushedCutoffHz_ = lowpassHz_;

        // --- (11) counters and grid phase --------------------------------------
        controlPhase_    = 0;
        clampEngagements_ = 0;
        trackedEnvNorm_  = 0.0f;
        chainActive_     = !allTonesDormant();
        allocatedBytes_  = computeAllocatedBytes();

        // --- (12) LAST: the seed -----------------------------------------------
        // BreathingModulator::prepare calls initState(), which re-seeds from
        // configuredSeed_; a seed distributed before step (6) would be discarded
        // by that prepare. Same ordering as NoiseOrganism and
        // ResonanceDriftNetwork.
        setSeed(seed_);

        // --- (13) rewind --------------------------------------------------------
        reset();
    }

    /// @brief FR-005: rewinds audio and modulation state, preserving every
    ///        configuration scalar. Real-time safe.
    void reset() noexcept {
        // (1) both phases together (FR-014).
        masterUnison_.reset();
        masterFifth_.reset();

        // (2) the generators. breath.reset() re-seeds from configuredSeed_, so
        //     the same seed re-renders identically from the top.
        for (std::size_t i = 0; i < kNumTones; ++i) {
            tones_[i].osc.reset();
            tones_[i].breath.reset();
        }

        // (3) the chain. saturator_.reset() re-snaps its own three
        //     OnePoleSmoothers to dbToGain(inputGainDb_) etc.
        //     (saturation_processor.h:149-165), so the drive survives -
        //     configuration-preserving, as FR-005 requires. lowpass_.reset() and
        //     blocker_.reset() keep their coefficients.
        follower_.reset();
        lowpass_.reset();
        saturator_.reset();
        blocker_.reset();

        // (4)/(5) the just-rewound breathers, then every ramp snapped to its
        //         steady target.
        for (std::size_t i = 0; i < kNumTones; ++i) {
            refreshBreath(i);
            ToneState& t = tones_[i];
            t.levelRamp.snapTo(toneLevelGain(t.levelDb));
            const float steady = gateSteady(i);
            t.gate.snapTo(steady);
            t.lastGateTarget    = steady;
            t.infrasonicFloored = (steady == 0.0f);
        }
        trackGainRamp_.snapTo(1.0f - trackingAmount_);
        wetGainRamp_.snapTo(wetGain(wetGainDb_));
        cutoffGlide_.snapTo(std::log2(lowpassHz_));
        lowpass_.setCutoff(lowpassHz_);
        pushedCutoffHz_ = lowpassHz_;

        // (6) counters. allocatedBytes_ is deliberately NOT touched - SC-010
        //     asserts the ledger is unchanged by reset().
        controlPhase_     = 0;
        clampEngagements_ = 0;
        trackedEnvNorm_   = 0.0f;
        chainActive_      = !allTonesDormant();
    }

    // =========================================================================
    // Render (plan S1.4, S5.1, S6)
    // =========================================================================

    /// @brief FR-050. Adds the mono sub contribution to both channels; the dry
    ///        path is otherwise untouched. In-place (outL == inL) is supported.
    ///
    /// Delegates to processBlockTapped() with a null tap so FR-063's
    /// "the tapped main output is bit-identical to the untapped one" is
    /// STRUCTURAL - there is exactly one render body - rather than an identity
    /// two parallel bodies would have to maintain.
    void processBlock(const float* inL, const float* inR, float* outL, float* outR,
                      std::size_t numSamples) noexcept {
        processBlockTapped(inL, inR, outL, outR, nullptr, numSamples);
    }

    /// @brief FR-062. As processBlock(), plus the post-chain, post-tracking,
    ///        PRE-wet-gain, PRE-clamp sub signal written to `subTap`. The tap is
    ///        independent of setSubToMainEnabled() (FR-064). `subTap` may be
    ///        null.
    void processBlockTapped(const float* inL, const float* inR, float* outL, float* outR,
                            float* subTap, std::size_t numSamples) noexcept {
        // ---- THE FR-050 GUARD LADDER, in exactly this order -------------------
        //  1. any null CHANNEL pointer -> return, writing NOTHING and advancing
        //     NOTHING. (`subTap` is exempt: a null tap is the untapped call.)
        //  2. numSamples == 0 -> return, consuming NO control step. The FR-007
        //     residue is ABSOLUTE, so a zero-length call must not move it.
        //  3. !prepared_ -> PASSTHROUGH (in -> out) plus a zeroed subTap, and no
        //     state advance. NOTE: this DIFFERS from FeedbackEcology, which
        //     zero-fills (:951-955). FR-050 makes passthrough a promise here
        //     (spec.md:672-675), not a coincidence of unprepared oscillators.
        if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) {
            return;
        }
        if (numSamples == 0) {
            return;
        }
        if (!prepared_) {
            // The self-copy MUST be SKIPPED, not relied on: [alg.copy] requires
            // that `result` not lie in [first, first + n), and in-place
            // rendering (outL == inL, SC-012 (b)) puts it exactly there. It
            // happens to work today only because MSVC and libstdc++ both route
            // trivially-copyable contiguous ranges to memmove - an
            // implementation detail, not a contract, and no sanitizer in this
            // repo's CI flags it. The two reachable preconditions meet whenever
            // a host renders in place before prepare().
            if (outL != inL) {
                std::copy_n(inL, numSamples, outL);
            }
            if (outR != inR) {
                std::copy_n(inR, numSamples, outR);
            }
            if (subTap != nullptr) {
                std::fill_n(subTap, numSamples, 0.0f);
            }
            return;
        }

        // ---- the FR-007 chunk loop --------------------------------------------
        // controlPhase_ lives ACROSS calls, so a 36 + 28 split runs exactly the
        // one control step an unsplit 64 runs. SC-012 (a) and SC-019 are the
        // criteria that catch a block-relative grid.
        std::size_t done = 0;
        while (done < numSamples) {
            if (controlPhase_ == 0) {
                updateControl();
            }
            const std::size_t chunk =
                std::min(numSamples - done, kControlChunkSamples - controlPhase_);
            renderChunk(inL + done, inR + done, outL + done, outR + done,
                        (subTap != nullptr) ? subTap + done : nullptr, chunk);
            controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples;
            done += chunk;
        }
    }

    // =========================================================================
    // Setters (FR-060) - every float setter REJECTS a non-finite argument
    // (FR-009, plan S7.7); every tone setter silently no-ops out of range
    // =========================================================================

    /// Plan S2.3: the ONE owner of both master increments.
    void setFundamentalHz(float hz) noexcept {
        if (!detail::isFinite(hz)) {
            return;  // FR-009: reject, the previous value stands
        }
        fundamentalHz_ = std::clamp(hz, kMinFundamentalHz, maxFundamentalHz_);
        // PHASE IS NOT TOUCHED (FR-014): only `increment` is written, exactly as
        // PhaseAccumulator::setFrequency does (phase_utils.h:177-179), so a
        // pitch change is phase-continuous.
        masterUnison_.increment =
            calculatePhaseIncrement(fundamentalHz_, static_cast<float>(sampleRate_));
        // *** THE FIFTH-BELOW CONSTRUCTION (FR-012) ***
        // NOT setFrequency(4.0f/3.0f * f, fs): that rounds 4f/3 to float BEFORE
        // the divide and makes the two increments' ratio only float-accurate
        // (~1e-7 relative), which shows up as a slow relative phase creep
        // between the octave and the fifth. ONE double multiply makes the ratio
        // the exactly-double-rounded 4/3 (~1e-16), so FR-014's "the two
        // accumulators realign every three cycles of f" is true to double
        // precision and SC-003 (b)'s +/-2 cents has four orders of margin.
        masterFifth_.increment = masterUnison_.increment * (4.0 / 3.0);
        incUnison_ = static_cast<float>(masterUnison_.increment);
        incFifth_  = static_cast<float>(masterFifth_.increment);
    }
    void setToneLevelDb(std::size_t tone, float db) noexcept {
        if (tone >= kNumTones || !detail::isFinite(db)) {
            return;
        }
        ToneState& t = tones_[tone];
        t.levelDb = std::clamp(db, kMinToneLevelDb, kMaxToneLevelDb);
        // toneLevelGain maps the exact fader bottom to a literal 0.0f, which is
        // what makes FR-023's dormancy predicate an `== 0.0f` comparison.
        t.levelRamp.setTarget(toneLevelGain(t.levelDb));
    }
    void setToneWaveform(std::size_t tone, SubWaveform waveform) noexcept {
        if (tone >= kNumTones) {
            return;
        }
        tones_[tone].waveform = waveform;
        tones_[tone].osc.setWaveform(waveform);
    }
    void setToneBreathRate(std::size_t tone, float hz) noexcept {
        // Clamps to [kMinBreathRateHz, kMaxBreathRateHz], then pushes into the
        // modulator, which clamps again with the bounds this file
        // static_asserts against - so the second clamp is idempotent and
        // getToneBreathRate()'s forward cannot contradict this range.
        if (tone >= kNumTones || !detail::isFinite(hz)) {
            return;
        }
        tones_[tone].breath.setRate(std::clamp(hz, kMinBreathRateHz, kMaxBreathRateHz));
    }
    void setToneBreathDepth(std::size_t tone, float normalized) noexcept {
        // The depth lives HERE, in breathGain; the modulator stays at its
        // library depth of 1.0 (Q7). Never call breath.setDepth() - that would
        // square the depth.
        if (tone >= kNumTones || !detail::isFinite(normalized)) {
            return;
        }
        tones_[tone].breathDepth = std::clamp(normalized, 0.0f, 1.0f);
    }
    void setTrackingAmount(float normalized) noexcept {
        if (!detail::isFinite(normalized)) {
            return;
        }
        trackingAmount_ = std::clamp(normalized, 0.0f, 1.0f);
    }
    void setTrackReferenceDb(float db) noexcept {
        if (!detail::isFinite(db)) {
            return;
        }
        trackReferenceDb_  = std::clamp(db, kMinTrackReferenceDb, kMaxTrackReferenceDb);
        trackReferenceRms_ = dbToGain(trackReferenceDb_);
    }
    void setFollowerAttackMs(float ms) noexcept {
        if (!detail::isFinite(ms)) {
            return;
        }
        // Not stored here: the follower owns it and getFollowerAttackMs()
        // forwards to it (plan S8), so a stored-but-never-pushed value is not
        // representable.
        follower_.setAttackTime(std::clamp(ms, kMinFollowerAttackMs, kMaxFollowerAttackMs));
    }
    void setFollowerReleaseMs(float ms) noexcept {
        if (!detail::isFinite(ms)) {
            return;
        }
        follower_.setReleaseTime(std::clamp(ms, kMinFollowerReleaseMs, kMaxFollowerReleaseMs));
    }
    void setLowpassCutoffHz(float hz) noexcept {
        // It writes cutoffGlide_.setTarget(std::log2(clamped)) and NEVER touches
        // the biquad directly (plan S5.3) - a direct configure() swaps b0 by
        // three orders while z1_/z2_ hold old-pole state, which is exactly the
        // step ClickDetector's 5-sigma test finds (SC-008).
        if (!detail::isFinite(hz)) {
            return;
        }
        lowpassHz_ = std::clamp(hz, kMinLowpassHz, maxLowpassHz_);
        cutoffGlide_.setTarget(std::log2(lowpassHz_));
    }
    void setDriveDb(float db) noexcept {
        if (!detail::isFinite(db)) {
            return;
        }
        driveDb_ = std::clamp(db, kMinDriveDb, kMaxDriveDb);
        // Unity-through drive: the saturator's input gain pushes into the tanh
        // and the output gain takes the same number back out, so drive changes
        // the SHAPE, not the level (FR-041). Both sit inside
        // SaturationProcessor::kMin/kMaxGainDb, which S1.2 static_asserts.
        saturator_.setInputGain(driveDb_);
        saturator_.setOutputGain(-driveDb_);
    }
    void setWetGainDb(float db) noexcept {
        if (!detail::isFinite(db)) {
            return;
        }
        wetGainDb_ = std::clamp(db, kMinWetGainDb, kMaxWetGainDb);
        wetGainRamp_.setTarget(wetGain(wetGainDb_));
    }
    /// FR-064 (Q8). The flag gates renderChunk() step (9)'s ADD and nothing
    /// else. It does not silence the engine, does not touch the FR-062 tap and
    /// does not enter FR-025's dormancy bookkeeping or FR-054's counter - all
    /// three are defined on `subChain`, strictly upstream of the flag.
    ///
    /// DELIBERATELY NOT DE-ZIPPERED. FR-064 promises the main output is
    /// BIT-IDENTICAL to the dry input while the flag is `false`; a 50 ms fade
    /// would violate that on every sample of the fade, so there is no ramp here
    /// and the transition is a hard step. The caller that wants a smooth
    /// transition owns it - FR-051's wet fader is the de-zippered control.
    ///
    /// No finiteness guard: the argument is a `bool` (FR-009 binds float
    /// arguments only), and no clamp: `true`/`false` are the whole range.
    void setSubToMainEnabled(bool enabled) noexcept { subToMainEnabled_ = enabled; }
    /// Plan S3.2. Seed 0 is legal (deriveStreamSeed substitutes a non-zero
    /// stream, core/random.h:102-113).
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        for (std::size_t i = 0; i < kNumTones; ++i) {
            tones_[i].breath.setSeed(deriveStreamSeed(seed_, kSaltBreath + i));
            // MANDATORY: setSeed reseeds rng_ but leaves phase_ and the output
            // smoother where they were; reset() -> initState() is what makes the
            // same seed re-render identically from the top.
            tones_[i].breath.reset();
            refreshBreath(i);
        }
    }

    // =========================================================================
    // Getters (FR-061) - every one reports the APPLIED value (plan S8)
    // =========================================================================

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::size_t getMaxBlockSamples() const noexcept { return config_.maxBlockSamples; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    [[nodiscard]] float getFundamentalHz() const noexcept { return fundamentalHz_; }

    /// FR-012. Derived FROM the accumulator increments (plan S2.3), never
    /// recomputed from fundamentalHz_, so the read surface and the rendered
    /// pitch cannot disagree. Out-of-range tone index returns 0.0f.
    [[nodiscard]] float getToneFrequencyHz(std::size_t tone) const noexcept {
        switch (tone) {
            case 0:  return static_cast<float>(masterUnison_.increment * sampleRate_ * 0.5);
            case 1:  return static_cast<float>(masterUnison_.increment * sampleRate_ * 0.25);
            case 2:  return static_cast<float>(masterFifth_.increment * sampleRate_ * 0.5);
            default: return 0.0f;
        }
    }

    [[nodiscard]] float getToneLevelDb(std::size_t tone) const noexcept {
        return (tone < kNumTones) ? tones_[tone].levelDb : 0.0f;
    }
    [[nodiscard]] SubWaveform getToneWaveform(std::size_t tone) const noexcept {
        return (tone < kNumTones) ? tones_[tone].waveform : SubWaveform::Sine;
    }
    /// Forwards to the modulator, the one object that stores the rate and clamps
    /// it (plan S8) - a stored-but-never-pushed rate is not representable.
    [[nodiscard]] float getToneBreathRate(std::size_t tone) const noexcept {
        return (tone < kNumTones) ? tones_[tone].breath.getRate() : 0.0f;
    }
    [[nodiscard]] float getToneBreathDepth(std::size_t tone) const noexcept {
        return (tone < kNumTones) ? tones_[tone].breathDepth : 0.0f;
    }
    /// The raw bipolar b_i in [-1, +1], NOT depth-scaled (Q7).
    [[nodiscard]] float getToneBreathValue(std::size_t tone) const noexcept {
        return (tone < kNumTones) ? tones_[tone].breathValue : 0.0f;
    }
    /// FR-022's three-factor product, exactly noise_organism.h:932-938's form.
    [[nodiscard]] float getToneCurrentGain(std::size_t tone) const noexcept {
        if (tone >= kNumTones) {
            return 0.0f;
        }
        const ToneState& t = tones_[tone];
        return t.levelRamp.getCurrentValue() * t.breathGain * t.gate.getCurrentValue();
    }
    /// FR-023: the FR-020 fader bottom, NOT the FR-016 backstop.
    ///
    /// The `== 0.0f` is EXACT and correct BY CONSTRUCTION, not by tolerance
    /// (plan S5.4): toneLevelGain(kMinToneLevelDb) returns a LITERAL 0.0f
    /// (see the helper above - dbToGain(-60) is 1e-3, not zero), and
    /// LinearRamp::process() lands exactly ON its target rather than
    /// approaching it (smoother.h:379-383 clamps the overshoot to target_).
    /// So a tone parked at the fader bottom reports target_ == current_ ==
    /// 0.0f bit-exactly, which is what makes SC-014 (a)'s bit-identical
    /// passthrough reachable at all.
    ///
    /// BOTH halves are load-bearing. `getTarget() == 0` alone would call a
    /// tone dormant while its 50 ms fade is still in flight and still adding
    /// audio; `getCurrentValue() == 0` alone would call a tone dormant on the
    /// first sample of a fade UP from the floor, whose ramp has not yet moved.
    ///
    /// An out-of-range index is NOT dormant: allTonesDormant() folds this
    /// predicate with `&&`, and returning true for a bad index would be a
    /// silent bias toward sleeping.
    [[nodiscard]] bool isToneDormant(std::size_t tone) const noexcept {
        if (tone >= kNumTones) {
            return false;
        }
        const LinearRamp& ramp = tones_[tone].levelRamp;
        return ramp.getTarget() == 0.0f && ramp.getCurrentValue() == 0.0f;
    }
    /// FR-016: a backstop-gated tone is silenced via `gate` but is NOT dormant.
    [[nodiscard]] bool isToneInfrasonicFloored(std::size_t tone) const noexcept {
        return (tone < kNumTones) && tones_[tone].infrasonicFloored;
    }

    [[nodiscard]] float getTrackingAmount() const noexcept { return trackingAmount_; }
    /// FR-034: the last computed envNorm, clamped [0, 1].
    [[nodiscard]] float getTrackedEnvelope() const noexcept { return trackedEnvNorm_; }
    [[nodiscard]] float getTrackingGain() const noexcept {
        return trackGainRamp_.getCurrentValue();
    }
    [[nodiscard]] float getTrackReferenceDb() const noexcept { return trackReferenceDb_; }
    /// Forwards to the follower (plan S8): the shipped defaults are 10 / 100 ms,
    /// so a build that stores FR-031's 120 / 800 and never pushes reports the
    /// shipped values here and fails SubharmonicEngine_ControlSurfaceContract.
    [[nodiscard]] float getFollowerAttackMs() const noexcept { return follower_.getAttackTime(); }
    [[nodiscard]] float getFollowerReleaseMs() const noexcept { return follower_.getReleaseTime(); }

    /// The clamped REQUEST, not the glide's instantaneous value (plan S5.3).
    [[nodiscard]] float getLowpassCutoffHz() const noexcept { return lowpassHz_; }
    [[nodiscard]] float getDriveDb() const noexcept { return driveDb_; }
    [[nodiscard]] float getWetGainDb() const noexcept { return wetGainDb_; }
    [[nodiscard]] bool getSubToMainEnabled() const noexcept { return subToMainEnabled_; }

    [[nodiscard]] std::uint32_t getSeed() const noexcept { return seed_; }
    /// FR-054: saturating, finite excursions only, cleared by prepare()/reset().
    [[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept {
        return clampEngagements_;
    }
    /// FR-073: the computed ledger of plan S9 - 8384 + 4 * maxBlockSamples.
    /// Self-reported: AllocationDetector has no byte accounting.
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return allocatedBytes_; }

private:
    /// FR-055 fault injection - see plan S7.5 and the forward declaration above.
    friend struct detail::SubharmonicEngineNonFiniteProbe;

    // =========================================================================
    // Private state (plan S1.5) - EXACT members, in declaration order
    // =========================================================================

    struct ToneState {
        // The {} here is REQUIRED, not redundant: SubOscillator::SubOscillator is
        // explicit, and the tones_{} aggregate initialisation below copy-initialises
        // every member whose initialiser is omitted. Re-pointed at &blepTable_ in
        // prepare() step 5.
        SubOscillator      osc{};  // NOLINT(readability-redundant-member-init)
        BreathingModulator breath;
        LinearRamp         levelRamp;    // FR-022 factor 1 - retargeted only by setToneLevelDb
        LinearRamp         gate;         // FR-022 factor 3 - retargeted only at a backstop edge

        // Configuration. These two differ per tone, so they CANNOT carry the
        // FR-020/FR-021 defaults as in-class initialisers; the constructor's
        // applyDefaults() call writes kDefaultToneLevelDb[i] /
        // kDefaultToneBreathDepth[i] before any caller can observe them, which
        // is how FR-003 is satisfied. The zeroes below are never read.
        float levelDb        = 0.0f;
        float breathDepth    = 0.0f;
        // NO breathRateHz member: the rate lives in the modulator, which clamps
        // it, and getToneBreathRate() forwards to breath.getRate() (S8) so a
        // stored-but-never-pushed rate is not representable.
        float breathValue    = 0.0f;     // last raw b_i, FR-061
        float breathGain     = 1.0f;     // FR-022 factor 2, held constant across a control chunk
        float lastGateTarget = 1.0f;     // edge detection for the FR-016 backstop
        SubWaveform waveform = SubWaveform::Sine;
        bool  infrasonicFloored = false;
    };

    MinBlepTable                     blepTable_;        // D-8: ONE table, shared by all three
    std::array<ToneState, kNumTones> tones_{};
    PhaseAccumulator                 masterUnison_{};   // f    -> Div2, Div4
    PhaseAccumulator                 masterFifth_{};    // 4f/3 -> FifthBelow
    float                            incUnison_ = 0.0f; // float cache of masterUnison_.increment
    float                            incFifth_  = 0.0f;

    EnvelopeFollower    follower_{};
    TwoPoleLP           lowpass_;
    SaturationProcessor saturator_{};
    DCBlocker2          blocker_;

    LinearRamp trackGainRamp_;     // FR-032, per-sample, advances even while dormant
    LinearRamp wetGainRamp_;       // FR-051, per-sample, advances even while dormant
    LinearRamp cutoffGlide_;       // S5.3, log2-Hz, advanced ONCE PER CONTROL CHUNK

    double        sampleRate_ = 48000.0;
    PrepareConfig config_{};
    float maxFundamentalHz_ = kMaxFundamentalHz;   // cached rate-derived ceiling
    float maxLowpassHz_     = kMaxLowpassHz;       // cached rate-derived ceiling

    float fundamentalHz_     = kDefaultFundamentalHz;
    float trackingAmount_    = kDefaultTrackingAmount;
    float trackReferenceDb_  = kDefaultTrackReferenceDb;
    float trackReferenceRms_ = dbToGain(kDefaultTrackReferenceDb);  // cached denominator
    // NO followerAttackMs_ / followerReleaseMs_ members: both live in the
    // follower, which clamps them, and the two getters forward to it (S8). A
    // build that stored them and never called setAttackTime/setReleaseTime was
    // green on every criterion in an earlier draft of the plan; with the value
    // held in exactly one place that build cannot exist.
    float lowpassHz_         = kDefaultLowpassHz;  // the CLAMPED REQUEST - what the getter reports
    float pushedCutoffHz_    = 0.0f;               // last value handed to lowpass_.setCutoff
    float driveDb_           = kDefaultDriveDb;
    float wetGainDb_         = kDefaultWetGainDb;
    float trackedEnvNorm_    = 0.0f;               // FR-034

    std::uint32_t seed_             = kDefaultSeed;
    std::uint32_t clampEngagements_ = 0;
    std::size_t   controlPhase_     = 0;           // FR-007 ABSOLUTE residue, carried across calls
    std::size_t   allocatedBytes_   = 0;

    bool subToMainEnabled_ = true;   // FR-064
    // chainActive_ is a MEMBER, not a predicate recomputed per chunk: FR-026's
    // clear must fire exactly once, on the EDGE. A recomputed predicate cannot
    // tell the first dormant chunk from the thousandth, and re-running three
    // reset() calls every chunk would erase the FR-025 CPU saving SC-013 (c)
    // gates. Same shape as Loop::engineActive (feedback_ecology.h:1901-1904).
    bool chainActive_      = false;  // FR-025/FR-026
    bool prepared_         = false;

    // =========================================================================
    // Salt table (FR-072, plan S1.6) - APPEND ONLY
    // =========================================================================
    // Renumbering a base silently changes every Phase-6 render. A later phase
    // adding a lane takes kSaltNextFree and moves that constant up.
    static constexpr std::size_t kSaltBreath   = 0;   // + tone index, three entries
    static constexpr std::size_t kSaltNextFree = 8;
    static_assert(kSaltBreath + kNumTones <= kSaltNextFree, "salt table overflow");

    // =========================================================================
    // Internals
    // =========================================================================

    // ---- the shared clamp helpers (plan S2.1) --------------------------------

    [[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }
    [[nodiscard]] static constexpr double sanitise(double v, double neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }
    /// FR-020's fader bottom. dbToGain(-60) is 1e-3, NOT zero - the exact-zero
    /// mapping is what makes FR-023's dormancy test an `== 0.0f` comparison
    /// rather than an epsilon, and what makes SC-014 (a)'s bit-identity
    /// reachable at all.
    [[nodiscard]] static constexpr float toneLevelGain(float db) noexcept {
        return (db <= kMinToneLevelDb) ? 0.0f : dbToGain(db);
    }
    [[nodiscard]] static constexpr float wetGain(float db) noexcept {
        return (db <= kMinWetGainDb) ? 0.0f : dbToGain(db);
    }

    // ---- the FR-073 ledger (plan S9) -----------------------------------------
    // The RESIDENT heap term, derived rather than asserted: MinBlepTable's
    // table_ and blampTable_ are length_ * oversamplingFactor floats each
    // (minblep_table.h:216-217, :243) with length_ = 2 * zeroCrossings
    // (:88), and each SubOscillator's Residual::buffer_ is table.length()
    // floats (:405). SaturationProcessor::dryBuffer_ (:136) is the only
    // block-sized term - dead weight this component never reads, declared here
    // rather than hidden. Everything else in the composition is heap-free.
    static constexpr std::size_t kBlepTableLength = kBlepZeroCrossings * 2;
    static constexpr std::size_t kBlepTableBytes =
        sizeof(float) * kBlepTableLength * kBlepOversampling;
    static constexpr std::size_t kBlepResidualBytes =
        sizeof(float) * kBlepTableLength * kNumTones;
    static constexpr std::size_t kFixedHeapBytes = 2 * kBlepTableBytes + kBlepResidualBytes;
    static_assert(kFixedHeapBytes == std::size_t{8384},
                  "plan S9's ledger: 4096 (blep) + 4096 (blamp) + 192 (three residuals)");

    [[nodiscard]] std::size_t computeAllocatedBytes() const noexcept {
        return kFixedHeapBytes + sizeof(float) * config_.maxBlockSamples;
    }

    // ---- per-tone control-rate helpers ----------------------------------------

    /// Plan S5.2 step (2). getCurrentValue() is VIRTUAL
    /// (breathing_modulator.h:221), so this runs three virtual calls per 64
    /// samples and never per sample (FR-024, A-1).
    void refreshBreath(std::size_t tone) noexcept {
        ToneState& t = tones_[tone];
        const float b = std::clamp(sanitise(t.breath.getCurrentValue(), 0.0f), -1.0f, 1.0f);
        t.breathValue = b;                                          // FR-061 reports THIS
        t.breathGain  = 1.0f + kBreathGainSpan * t.breathDepth * b;  // FR-022, in [0.55, 1.45]
    }

    /// Plan S5.2: the SINGLE owner of the FR-016 backstop law. The three
    /// call sites - prepare() step (10), reset() step (5) and (from T010)
    /// updateControl() step (3) - each compare the result against an exact
    /// `0.0f`, so the LITERAL 0.0f / 1.0f here is load-bearing rather than
    /// stylistic: a computed near-zero would make `infrasonicFloored` and the
    /// gate's edge detection a coin toss. Precedent:
    /// resonance_drift_network.h:1680.
    ///
    /// The threshold is read off getToneFrequencyHz(), which is itself derived
    /// from the accumulator increments (S2.3), so the gate and the read surface
    /// are structurally incapable of disagreeing. Solving
    /// `toneHz >= kMinToneHz` per tone gives FR-016's thresholds on `f`: Div4
    /// (f/4) floors below f = 48 Hz, Div2 (f/2) below 24 Hz and FifthBelow
    /// (2f/3) below 18 Hz - SC-020 (d), and the reason the FR-013 default of
    /// 55 Hz (27.5 / 13.75 / 36.67 Hz) leaves all three awake.
    ///
    /// An out-of-range index returns 1.0f: getToneFrequencyHz() reports the
    /// documented neutral 0.0f there, which would otherwise read as "floored"
    /// and make isToneInfrasonicFloored(bad) true. The public getter's own
    /// `tone < kNumTones` guard already covers this; the explicit branch keeps
    /// the helper correct on its own terms.
    [[nodiscard]] float gateSteady(std::size_t tone) const noexcept {
        if (tone >= kNumTones) {
            return 1.0f;
        }
        return (getToneFrequencyHz(tone) < kMinToneHz) ? 0.0f : 1.0f;
    }

    /// Composed from the public predicate and its exact `== 0.0f` form (plan
    /// S5.4). Used by prepare() step (11) and reset() step (6) to seed the
    /// chainActive_ edge latch, and by updateControl() step (6) to drive it.
    [[nodiscard]] bool allTonesDormant() const noexcept {
        for (std::size_t i = 0; i < kNumTones; ++i) {
            if (!isToneDormant(i)) {
                return false;
            }
        }
        return true;
    }

    // ---- the control step (plan S5.2) ----------------------------------------

    /// Plan S5.2. Called from processBlockTapped() exactly when
    /// `controlPhase_ == 0`, i.e. once every 64 RENDERED samples whatever the
    /// host's partition. THE ORDER BELOW IS NORMATIVE - every line has a reason
    /// recorded in plan S5.2, and three of them are the difference between a
    /// correct component and one that passes by accident.
    void updateControl() noexcept {
        for (std::size_t i = 0; i < kNumTones; ++i) {
            ToneState& t = tones_[i];

            // (1) ALWAYS kControlChunkSamples, NEVER the rendered `chunk`. 64
            //     samples always elapse between two updateControl() calls,
            //     whatever the partition; advancing by `chunk` would make the
            //     breath rate a function of the host's block size
            //     (breathing_modulator.h:208-215 advances phase by numSamples)
            //     and SC-012 (a) would fail.
            t.breath.processBlock(kControlChunkSamples);

            // (2) three VIRTUAL getCurrentValue() calls per 64 samples, never
            //     per sample (FR-024, A-1).
            refreshBreath(i);

            // (3) the FR-016 backstop, EDGE-DETECTED. Edge-only is
            //     load-bearing (Q1): LinearRamp::setTarget recomputes the
            //     increment from the REMAINING distance (smoother.h:342-355),
            //     so a ramp retargeted every control step never arrives and
            //     SC-020 (c)'s "reaches its new target within 52 ms and is
            //     monotonic" would be unmeasurable. Retargeted only at edges it
            //     lands exactly on the target and stays.
            const float steady = gateSteady(i);
            if (steady != t.lastGateTarget) {
                t.gate.setTarget(steady);
                t.lastGateTarget = steady;
            }
            t.infrasonicFloored = (steady == 0.0f);
        }

        // (4) the FR-032 tracking law, with the S7.6 / S14 C-3 sensor guard.
        //     THIS ramp IS retargeted every step, deliberately: it is a
        //     tracking gain, not a gate, and a continuously-retargeted
        //     LinearRamp's exponential-approach behaviour is exactly the
        //     smoothing wanted. No criterion asserts its arrival time.
        //
        //     The guard is defence in depth behind renderChunk()'s per-sample
        //     bit test, and it is not optional. A NaN reaching
        //     trackGainRamp_.setTarget does NOT propagate: it MUTES the ramp
        //     instantly (target_ = current_ = increment_ = 0, smoother.h:343-348)
        //     with no counter and no way back, after which the sub is exactly
        //     0.0f - FINITE - so FR-055's trap in renderChunk() never fires and
        //     nothing in this component ever recovers.
        float env = follower_.getCurrentValue();
        if (!detail::isFinite(env)) {
            follower_.reset();
            env = 0.0f;
        }
        trackedEnvNorm_ = std::clamp(env / trackReferenceRms_, 0.0f, 1.0f);
        trackGainRamp_.setTarget((1.0f - trackingAmount_) + trackingAmount_ * trackedEnvNorm_);

        // (5) the plan S5.3 cutoff glide: ONE LinearRamp::process() and ONE
        //     std::exp2 per control step, pushed to the biquad only when the
        //     applied value really moved. setLowpassCutoffHz() writes the
        //     log2-domain target and NEVER touches the biquad directly - a
        //     direct configure() swaps b0 by three orders while z1_/z2_ still
        //     hold old-pole state, which is exactly the step ClickDetector's
        //     5-sigma test finds (SC-008). When the ramp is parked it early-outs
        //     (smoother.h:372-374) and steady-state cost is one compare per 64
        //     samples.
        const float applied = std::exp2(cutoffGlide_.process());
        if (std::fabs(applied - pushedCutoffHz_) > kCutoffPushRelative * pushedCutoffHz_) {
            lowpass_.setCutoff(applied);
            pushedCutoffHz_ = applied;
        }

        // (6) FR-025/FR-026: the dormancy predicate and THE SLEEP EDGE.
        //
        //     Evaluated ONCE PER CONTROL CHUNK, which is what makes it
        //     partition-invariant and what turns "a tone write that ends
        //     dormancy takes effect within at most 64 samples" into a contract
        //     rather than an accident (Q5). "Dormant for the WHOLE chunk" is
        //     satisfied by construction: dormancy requires the level ramp to be
        //     PARKED at zero (target and current both 0.0f), and nothing inside
        //     a chunk can move it - only a setter between two processBlock()
        //     calls can, and that is seen at the next chunk boundary.
        //
        //     chainActive_ is a MEMBER, not a predicate recomputed per sample:
        //     FR-026's clear must fire exactly ONCE, on the edge. Re-running
        //     the resets every chunk would erase the FR-025 CPU saving SC-013
        //     (c) gates and would keep re-zeroing state the skip never fills.
        const bool dormant = allTonesDormant();
        if (chainActive_ && dormant) {
            // THE THREE RESETS, in FR-026's order. What each one actually does,
            // checked against the shipped code - because the obvious reading of
            // "clear the chain's audio state" is wrong for one of the three:
            //  * lowpass_.reset() and blocker_.reset() clear REAL audio tails -
            //    the biquad y1_/y2_ (two_pole_lp.h:102, dc_blocker.h:307-308)
            //    that would otherwise replay on the wake. THESE TWO are what
            //    SC-014 (c) measures, and the (c2) mutation removes exactly
            //    these two.
            //  * saturator_.reset() clears NO audio tail reachable from here:
            //    processSample (saturation_processor.h:228-250) touches neither
            //    dryBuffer_ nor dcBlocker_, so the only state its reset() clears
            //    that this component can observe is the three OnePoleSmoothers
            //    (:151-153), which hold PARAMETER gains, not signal. It also
            //    runs a std::fill over dryBuffer_ (:159) - up to 32 KB - on the
            //    audio thread. Kept because FR-026 names it and the smoother
            //    snap is harmless, but PRICED, not free (plan S9, S12.4): it
            //    runs once per sleep edge, not per chunk, precisely because
            //    this is an edge latch.
            lowpass_.reset();
            saturator_.reset();
            blocker_.reset();
            chainActive_ = false;
        } else if (!chainActive_ && !dormant) {
            chainActive_ = true;
        }
        // follower_ IS NEVER RESET HERE (FR-026, last sentence). It is the
        // SENSOR: it must keep tracking the body while the subs sleep, or the
        // wake would begin from a zero envelope and the FR-032 tracking gain
        // would have to re-charge through the 800 ms release. It is also the
        // natural mistake, since the other three chain stages ARE cleared -
        // SC-014 (c3) is the arm that holds this line to the audio.
    }

    // ---- the per-sample law (plan S6) ----------------------------------------

    /// Plan S6. `n` never crosses a control-step boundary - processBlockTapped()
    /// splits at 64 - so every control-rate scalar read here is constant across
    /// the call.
    void renderChunk(const float* inL, const float* inR, float* outL, float* outR, float* tap,
                     std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            // (0) READ BOTH INPUTS FIRST. outL may alias inL (SC-012 (b)); every
            //     write below happens after both reads.
            const float xl = inL[i];
            const float xr = inR[i];

            // (1) THE GENERATORS ALWAYS ADVANCE, dormant or not (FR-025's stated
            //     deviation). Freezing a SubOscillator freezes its flip-flops
            //     and its sub-phase accumulator, so a wake would re-enter
            //     mid-waveform at a phase unrelated to the master's; at 12-30 Hz
            //     the 50 ms fade is one period or less and cannot mask it.
            //     While the chain is skipped the output would be discarded, so
            //     the state is advanced WITHOUT the waveform arithmetic
            //     (SubOscillator::advance, state-equivalent to process() by its
            //     own test): the three std::sin calls were ~45 % of the awake
            //     cost and were what dormancy failed to remove (SC-013 (c)).
            const bool wu = masterUnison_.advance();
            const bool wf = masterFifth_.advance();
            float s0 = 0.0f;
            float s1 = 0.0f;
            float s2 = 0.0f;
            if (chainActive_) {
                s0 = tones_[0].osc.process(wu, incUnison_);
                s1 = tones_[1].osc.process(wu, incUnison_);
                s2 = tones_[2].osc.process(wf, incFifth_);
            } else {
                tones_[0].osc.advance(wu, incUnison_);
                tones_[1].osc.advance(wu, incUnison_);
                tones_[2].osc.advance(wf, incFifth_);
            }

            // (2) EVERY PER-SAMPLE RAMP ALWAYS ADVANCES (FR-025), so a wake
            //     never begins from a stale gain. LinearRamp::process()
            //     early-outs when settled (smoother.h:372-374), so a parked ramp
            //     costs one compare. breathGain is the control-rate factor and
            //     is held constant across the chunk (FR-022).
            const float g0 =
                tones_[0].levelRamp.process() * tones_[0].breathGain * tones_[0].gate.process();
            const float g1 =
                tones_[1].levelRamp.process() * tones_[1].breathGain * tones_[1].gate.process();
            const float g2 =
                tones_[2].levelRamp.process() * tones_[2].breathGain * tones_[2].gate.process();
            const float tg = trackGainRamp_.process();
            const float wg = wetGainRamp_.process();

            // (3) THE SENSOR ALWAYS ADVANCES (FR-025, FR-026): it must keep
            //     tracking the body while the subs sleep. The isFinite guard is
            //     S14 C-3 and is NOT optional - plan S7.6 carries the exact
            //     poisoning trace it closes (EnvelopeFollower::processSample
            //     documents "Does NOT validate input", envelope_follower.h:162,
            //     and processRMS's release branch keeps a NaN forever).
            const float mono = 0.5f * (xl + xr);
            static_cast<void>(follower_.processSample(detail::isFinite(mono) ? mono : 0.0f));

            // (4) FR-025's SKIP. The chain - three biquad-class stages plus a
            //     tanh divide - and the three oscillators' waveform arithmetic
            //     (step (1)) are what dormancy skips. Everything above this
            //     line has already run: both masters, the three oscillators'
            //     state, all eight per-sample ramps and the follower. That is
            //     FR-025's stated deviation, and it is what SC-014 (d) measures.
            //
            //     `out = in` rather than `out = in + 0`: the dry path is copied
            //     verbatim, which is what makes SC-014 (a)'s BIT-IDENTICAL
            //     passthrough exact rather than approximate. The tap is zeroed
            //     (FR-062 promises a written tap on every rendered sample, and
            //     the sub really is zero here).
            if (!chainActive_) {
                outL[i] = xl;
                outR[i] = xr;
                if (tap != nullptr) {
                    tap[i] = 0.0f;
                }
                continue;
            }

            // (5) THE CHAIN, in the FR-040 order and no other.
            float y = g0 * s0 + g1 * s1 + g2 * s2;  // mono sub sum
            y = lowpass_.process(y);                // stage 1   (FR-040), Biquad self-heals
            y = saturator_.processSample(y);        // stage 2   (FR-041), tanh, mix_ == 1
            y *= tg;                                // stage 2.5 (FR-032)
            y = blocker_.process(y);                // stage 3   (FR-042), 18 Hz Bessel HP
            // The tracking multiply sits AFTER the saturator, not before: that
            // is what makes the drive level-independent (SC-004).

            // (6) FR-055 rung 4. DCBlocker2::process has no finiteness branch at
            //     all (dc_blocker.h:328-341) and the follower does not validate;
            //     this is the one place both are recovered. Unreachable through
            //     the public API - exercised by the plan S7.5 probe.
            if (!detail::isFinite(y)) {
                y = 0.0f;
                recoverNonFinite();
            }

            // (7) FR-062: the tap is post-chain, post-tracking, PRE-wet-gain,
            //     PRE-clamp, and independent of subToMainEnabled_ (FR-064).
            //     Post-tracking is load-bearing - SC-002 measures the FR-032 law
            //     through this tap, and a pre-tracking tap would read a flat
            //     line at every body level and pass for a broken implementation.
            if (tap != nullptr) {
                tap[i] = y;
            }

            // (8) FR-054 rung 3: the clamp binds the SUB CONTRIBUTION ONLY
            //     (Q6/D-12), never the summed output. It counts FINITE
            //     excursions only - step (6) has already removed the non-finite
            //     case, so the counter stays a pure statement about level.
            float sub = y * wg;
            if (sub > kOutputClamp) {
                sub = kOutputClamp;
                bumpClampCount();
            } else if (sub < -kOutputClamp) {
                sub = -kOutputClamp;
                bumpClampCount();
            }

            // (9) THE ADD (D-7), gated by FR-064. The SAME scalar on both
            //     channels - FR-043's mono-by-construction, which is the whole
            //     mechanism behind SC-007. The dry path is never touched by
            //     anything computed on the sub.
            const float add = subToMainEnabled_ ? sub : 0.0f;
            outL[i] = xl + add;
            outR[i] = xr + add;
        }
    }

    /// FR-054's counter SATURATES rather than wraps, so a long soak reports
    /// "many" instead of silently returning to zero.
    void bumpClampCount() noexcept {
        if (clampEngagements_ != 0xFFFFFFFFu) {
            ++clampEngagements_;
        }
    }

    /// The FR-055 recovery and nothing more. It does NOT touch the dry path,
    /// does NOT move clampEngagements_ and does NOT clear chainActive_.
    ///
    /// Cost asymmetry worth stating, since the call site is INSIDE the
    /// per-sample loop: saturator_.reset() carries a std::fill over dryBuffer_
    /// (saturation_processor.h:159), i.e. an O(maxBlockSamples) memset per
    /// invocation. That is acceptable ONLY because rung 4 is unreachable through
    /// the public API (plan S7.5) - the sub chain is generator-driven, so a
    /// non-finite INPUT never reaches it and a shipping audio thread never
    /// executes this line.
    void recoverNonFinite() noexcept {
        follower_.reset();
        lowpass_.reset();
        saturator_.reset();
        blocker_.reset();
    }

    /// FR-003 / plan S2.2. Called by the constructor and by prepare() step (9),
    /// and by nothing else. It is the ONLY path back to the defaults; reset() is
    /// configuration-preserving (FR-005). It allocates nothing, touches no
    /// PrepareConfig field, and MUST be correct on an unprepared object - none
    /// of the pushes below is gated on prepared_, or a default-constructed
    /// engine would report the composed objects' defaults instead of this
    /// component's and FR-003 would be violated.
    ///
    /// Three rows differ from the composed object's own shipped default and are
    /// therefore load-bearing rather than decorative:
    ///   * waveform Sine over SubOscillator's constructed Square
    ///     (sub_oscillator.h:381), which prepare() does not change;
    ///   * DetectionMode::RMS over the follower's Amplitude
    ///     (envelope_follower.h:380);
    ///   * follower 120 / 800 ms over the shipped 10 / 100
    ///     (envelope_follower.h:92-93) - an omitted push leaves the sensor 12x /
    ///     8x too fast, and S8's forwarding getters are what make it detectable.
    void applyDefaults() noexcept {
        for (std::size_t i = 0; i < kNumTones; ++i) {
            setToneWaveform(i, SubWaveform::Sine);            // FR-015, D-6
            setToneLevelDb(i, kDefaultToneLevelDb[i]);        // FR-020, Q3
            setToneBreathRate(i, kDefaultToneBreathRateHz[i]);  // FR-021
            setToneBreathDepth(i, kDefaultToneBreathDepth[i]);  // FR-021
            // S14 C-5: without a non-zero irregularity the modulator never
            // touches its RNG (breathing_modulator.h:262-271), the FR-070 seed
            // is inert and SC-011 (b) is unreachable.
            tones_[i].breath.setIrregularity(kDefaultBreathIrregularity);
        }

        follower_.setMode(DetectionMode::RMS);                // FR-030
        // The sidechain path starts at 20 Hz and would blind the follower to
        // exactly the low body the subs follow.
        follower_.setSidechainEnabled(false);                 // FR-030
        setFollowerAttackMs(kDefaultFollowerAttackMs);        // FR-031
        setFollowerReleaseMs(kDefaultFollowerReleaseMs);      // FR-031

        setFundamentalHz(kDefaultFundamentalHz);              // FR-013, A1
        setTrackingAmount(kDefaultTrackingAmount);            // FR-033
        setTrackReferenceDb(kDefaultTrackReferenceDb);        // FR-035
        setLowpassCutoffHz(kDefaultLowpassHz);                // FR-040
        setDriveDb(kDefaultDriveDb);                          // FR-041
        // Already the shipped default (saturation_processor.h:410); written
        // anyway so a future change to that default cannot silently retype this
        // chain. saturator_.setMix is NEVER called: mix_ stays at its
        // constructed 1.0f, above the < 0.0001f dry early-exit (:238-240).
        saturator_.setType(SaturationType::Tape);             // D-1
        setWetGainDb(kDefaultWetGainDb);                      // FR-051
        subToMainEnabled_ = true;                             // FR-064
    }
};

}  // namespace Krate::DSP
