// ==============================================================================
// Layer 3: System Component - ResonanceDriftNetwork
// ==============================================================================
// A bank of up to twelve resonant peaks whose frequency, Q, gain and stereo
// position drift on slow, bounded, individually seeded Brownian lanes. Vorago's
// third sound source: it colours whatever is fed to it rather than generating,
// so the harmonic cloud and the noise organism both pass through it.
//
// Feature: vorago-phase3-resonance-drift
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept throughout; NOTHING allocates,
//   here or in prepare - see getAllocatedBytes below)
// - Principle III: Modern C++ (C++20, RAII, value semantics, no owning ptrs)
// - Principle IX: Layer 3 (composes Layer 0-2 components only)
// - Principle X: DSP Constraints (bounded stochastic motion, slew-limited
//   coefficient writes, de-zippered gates)
// - Principle XI: Performance Budget (< 0.75 % CPU per voice at 48 kHz, SC-004)
//
// Reference: specs/vorago-phase3-resonance-drift/spec.md
//            specs/vorago-phase3-resonance-drift/plan.md
//
// BUILD STATE (tasks.md T006 - the skeleton pass). What is REAL here:
//   * every constant, every static_assert, the nested AnchorMode / PrepareConfig
//     / Peak types and the FR-005 salt table;
//   * the complete public setter/getter surface with the FR-008 / FR-050
//     contract enforced (non-finite argument => no-op, previous value stands;
//     out-of-range peak => silent no-op / documented neutral; out-of-range
//     float => clamped, and the getter reports the clamped value);
//   * setWanderRate's FR-037 decimation mapping (prepare step 8 needs it);
//   * prepare()'s 13-step order, reset(), clearAudioState(), setSeed();
//   * processBlock's guard ladder and the ABSOLUTE 64-sample control grid;
//   * (T007) recomputeAnchors() and the three AnchorModes in the log2 domain,
//     the FR-035 per-control-step slew ceiling on the applied values, and the
//     C-8 unslewed tracking while a peak's engine is inactive;
//   * (T008) the FR-036/FR-037 decimated lane advance at the top of
//     updateControl(), and the four FR-030..FR-033 / FR-038 wander maps inside
//     computeTargets() - frequency, Q, gain and pan, computed for EVERY peak
//     including a dormant one;
//   * (T009) the S9.1 six-inline bank seam plus bankApply, the FR-014/FR-015
//     indivisible frequency+Q write pair in updateControl(), the unconditional
//     FR-004 re-apply in snapControlState(), refreshGates() with its
//     lastGateTarget guard (wired here only from setNumPeaks), and the complete
//     FR-044 per-sample render tail in renderChunk();
//   * (T010) the FR-040/FR-042 peak life cycle - refreshGates() wired from
//     setPeakWake and setPeakDormant, the WAKE EDGE inside refreshGates()
//     (bank re-apply + enable, deliberately OUTSIDE the lastGateTarget guard),
//     the SLEEP EDGE in updateControl() (per-slot state clear + mandatory
//     re-apply + disable), and the C-6 per-peak base-level ramp, which
//     renderChunk() already advances for EVERY peak before the engineActive
//     early-out;
//   * (T011) FR-038's per-peak pan - the seeded one-shot default-position draw
//     in setSeed() (kSaltPanPosition, with the mandatory per-lane reset() after
//     every re-seed), the panPosition + panWanderDepth * lane map in
//     computeTargets(), and the equal-power cos/sin split in updatePanGains(),
//     reached once per peak per control step from composeApplied();
//   * (T012) FR-004's configuration-preserving reset() and FR-046's lighter
//     clearAudioState(), which differ by exactly one loop - the 48 lane
//     rewinds - plus the FR-016 global re-pin snapControlState() now carries so
//     that BOTH of them survive a future change to ResonatorBank::reset()'s own
//     defaults;
//   * (T016 build stage, spec corrections C-15 and C-16) the FR-018 non-finite
//     guard on the wet sum with its self-healing per-peak ring clear, and the
//     per-sample interpolation of the two factors the control grid used to step
//     once per chunk - the peak's wander gain (the bank's own per-slot gain is
//     now pinned at unity) and the equal-power pan pair. Both were forced by
//     measurement, both are documented at their sites, and SC-002's numbers
//     before and after are in
//     dsp/tests/unit/systems/resonance_drift_network_spectral_test.cpp.
//
// ENGINE TIER (FR-013). tasks.md T002's stage-cost probe ran BEFORE this file
// existed and tripped FR-013's gate 1, so Tier 1 is in force: the network holds
// ONE ResonatorBank whose slot `i` is peak `i`, and reads each peak's individual
// contribution through the purely additive ResonatorBank::processIndividual
// (resonator_bank.h:554) with ResonatorBank::resetResonatorState
// (resonator_bank.h:613) as the per-slot state clear. Tier 0's twelve
// single-resonator banks pay ResonatorBank::process()'s fixed per-call overhead
// twelve times per sample instead of once. Nothing else in the component
// changes between tiers - every bank interaction goes through the six seam
// inlines T009 adds.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>          // Layer 0: isFinite, flushDenormal, dbToGain
#include <krate/dsp/core/math_constants.h>    // Layer 0: kPi
#include <krate/dsp/core/random.h>            // Layer 0: Xorshift32, deriveStreamSeed
#include <krate/dsp/primitives/smoother.h>    // Layer 1: OnePoleSmoother, LinearRamp
#include <krate/dsp/processors/brownian_drift.h>  // Layer 2
#include <krate/dsp/processors/resonator_bank.h>  // Layer 2

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Every include above reaches DOWNWARD only (Layers 0-2). There is no <vector>
// and no <memory>: this component has no heap term at all (spec correction C-1).

namespace Krate {
namespace DSP {

/// @brief Up to twelve slowly drifting resonant peaks, stereo in / stereo out.
///
/// The network takes a stereo input, sums it to mono for the engine (FR-011),
/// runs it through one resonant bandpass per peak, gates each peak with a
/// per-sample LinearRamp, pans each into a stereo sum, normalises by
/// 1/sqrt(numPeaks), applies the FR-045 wet trim and crossfades the result
/// against the untouched stereo dry path (FR-043).
///
/// Every peak's frequency, Q, gain and pan follow its own BrownianDrift lane -
/// four lanes per peak, 48 per network, each on its own derived seed stream, so
/// two instances with the same seed render identically and two peaks never move
/// in lockstep.
///
/// RT safety: every method is noexcept, nothing allocates anywhere (prepare
/// included), there is no lock, no exception, no I/O and no virtual dispatch on
/// the per-sample path.
class ResonanceDriftNetwork {
public:
    // =========================================================================
    // Constants (plan S1.2)
    // =========================================================================

    /// Roadmap line 219. 12 <= ResonatorBank::kMaxResonators = 16 (resonator_bank.h:39).
    static constexpr std::size_t kMaxPeaks = 12;
    static_assert(kMaxPeaks >= 1 && kMaxPeaks <= kMaxResonators,
                  "a peak must map onto a ResonatorBank slot");

    /// The shared library-wide control clock (harmonic_cloud.h:144,
    /// continuous_body.h:97, noise_organism.h:150). A component that drifted off
    /// it would decorrelate the per-voice modulation grid in Vorago Phase 10.
    static constexpr std::size_t kControlChunkSamples = 64;
    static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");

    /// FR-037. 17 * BrownianDrift::kTauMax = 510 s >= the 500 s FR-016 admits.
    static constexpr std::size_t kMaxLaneDecimation = 17;
    static_assert(static_cast<float>(kMaxLaneDecimation) * BrownianDrift::kTauMax >= 500.0f,
                  "kMaxLaneDecimation must reach the slowest FR-016 wander rate");

    static constexpr float kGainRampMs  = 50.0f;  ///< FR-041 (noise_organism.h:178)
    static constexpr float kOutputClamp = 4.0f;   ///< FR-018 (noise_organism.h:180)
    static constexpr float kMixSmoothMs = kResonatorSmoothingTimeMs;  ///< 20 ms (resonator_bank.h:69)

    static constexpr float kDefaultWanderRateHz    = 0.03f;  ///< FR-034 (noise_organism.h:163)
    static constexpr float kMinWanderRateHz        = 0.002f;
    static constexpr float kMaxWanderRateHz        = 1.0f;
    static constexpr float kDefaultFreqStepOctaves = 0.02f;  ///< FR-035
    static constexpr float kDefaultQStepOctaves    = 0.05f;  ///< FR-035
    static constexpr float kMinSlewOctaves         = 0.001f;
    static constexpr float kMaxSlewOctaves         = 24.0f;

    /// FR-045's default wet trim. It exists because this component is a bank of
    /// twelve NARROW bandpasses: without a trim, `mix` would be a near-mute
    /// below mix ~ 0.95 rather than a blend (Clarifications Q2).
    ///
    /// THIS CONSTANT IS MEASURED, NOT AUTHORED.
    ///   Measuring case: ResonanceDriftNetwork_MeasureThresholds, section
    ///                   "FR-045 kDefaultWetGainDb and SC-020 (a) the
    ///                   wet-vs-drive window"
    ///                   (dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp,
    ///                   tagged [.calibration] - hidden, run on demand).
    ///   Method:         the constant is bounded from BOTH sides, and the
    ///                   measurement is of the two bounds, not of one centre.
    ///                   FLOOR, from SC-020 (b): render the FR-016 reference
    ///                   patch (numPeaks = 12, all defaults, AnchorMode::Free,
    ///                   mix = 1) on SC-001's broadband drive - white noise at
    ///                   -12 dBFS on both channels - at a 0 dB trim, discard
    ///                   1 s of settling, measure 10 s, and take the PASSIVE
    ///                   gap (wet RMS - drive RMS) across >= 8 seeds; the trim
    ///                   must lift the worst of those to within SC-020's 6 dB
    ///                   of the drive. CEILING, from SC-001 (b)+(e): render
    ///                   SC-001's worst-case patch (every peakQ at
    ///                   kMaxResonatorQ, all four lanes at maximum depth,
    ///                   wanderRate at its 1.0 Hz ceiling) on the same drive,
    ///                   60 s plus the ring-out tail, and take the peak; the
    ///                   trim must keep the worst of those below kOutputClamp,
    ///                   because FR-018 requires ZERO clamp engagements in
    ///                   every in-spec configuration.
    ///   Cross-check:    ~ -33 dB of passive attenuation is the analytic
    ///                   estimate for the floor - (pi/2) * sum(f_i / Q) ~ 605 Hz
    ///                   of summed noise-equivalent bandwidth against a 24 kHz
    ///                   white bandwidth (~ -16 dB), plus FR-019's 1/sqrt(12)
    ///                   (-10.8 dB), plus the -6 dB default peak level.
    ///
    ///   Measured gap:   FLOOR. Passive gap -38.241 dB mean over the 8
    ///                   calibration seeds (sd 0.465 dB, min -39.050,
    ///                   max -37.414; per-seed -38.290, -38.365, -38.047,
    ///                   -38.124, -39.050, -38.534, -37.414, -38.106), zero
    ///                   clamp engagements at the 0 dB trim so the figure is
    ///                   the passive network's and not FR-018's. Against the
    ///                   ~ -33 dB analytic estimate it is 5 dB deeper - the
    ///                   same order, the excess being the bandpasses'
    ///                   shortfall against the flat-topped idealisation the
    ///                   estimate uses. SC-020's 6 dB window therefore puts
    ///                   the floor at 39.050 - 6 = +33.05 dB.
    ///                   CEILING. SC-001 (e)'s clip ceiling over 12 seeds:
    ///                   min 35.74, mean 37.67, sd 1.53 dB (36.44, 38.59,
    ///                   36.78, 39.56, 36.61, 37.04, 35.74, 37.77, 35.85,
    ///                   40.66, 38.33, 38.70). The base SC-001 (a)(b)(d) patch
    ///                   is not the binding one - its ceiling is +54.08 dB
    ///                   worst of the same 12 - because it is the GAIN lane
    ///                   that costs the headroom: FR-033's 24 dB of wander
    ///                   against FR-017's +12 dB ceiling puts a peak up to
    ///                   18 dB above the -6 dB default the floor was measured
    ///                   at, and FR-019 normalises for numPeaks only.
    ///   Chosen value:   +34.5 dB - the midpoint of the measured admissible
    ///                   interval [+33.05, +35.74], rounded to 0.5 dB. It sits
    ///                   1.45 dB above the floor (worst-seed |wet - drive| =
    ///                   4.55 dB against SC-020's 6 dB) and 1.24 dB below the
    ///                   ceiling. NOTE, DELIBERATELY LOUD: that interval is
    ///                   only 2.7 dB wide, so this constant has ~1 dB of
    ///                   margin on each side and BOTH neighbouring criteria
    ///                   will move if it is nudged. It is not free to retune.
    ///                   Two superseded values, both measured RED, which is
    ///                   what shows the bounds discriminate rather than merely
    ///                   bracket: the provisional +30 dB left an 8.24 dB mean
    ///                   gap and SC-020 (a)/(b) failed; the floor-only value
    ///                   +38.0 dB (the passive mean negated, which is what the
    ///                   calibration suggests if the ceiling is ignored) put
    ///                   23 samples of SC-001 (e)'s render into the clamp and
    ///                   that arm failed.
    ///   Date:           2026-09-10.
    ///
    /// Its enforcing criteria are SC-020 (ResonanceDriftNetwork_WetGainTrim,
    /// resonance_drift_network_test.cpp) on the floor side and SC-001 (b)/(e)
    /// (ResonanceDriftNetwork_MaxQRingOut, resonance_drift_network_spectral_test.cpp)
    /// on the ceiling side. Both are COMPLIANCE ROWS against the recorded
    /// interval, never re-measurements.
    static constexpr float kDefaultWetGainDb = 34.5f;
    static constexpr float kMinWetGainDb     = -24.0f;
    static constexpr float kMaxWetGainDb     = 48.0f;

    static constexpr float kMinPeakLevelDb     = -60.0f;  ///< FR-017
    static constexpr float kMaxPeakLevelDb     = 12.0f;
    static constexpr float kMaxFreqWanderSemis = 24.0f;   ///< FR-030
    static constexpr float kMaxQWanderOctaves  = 2.0f;    ///< FR-031
    static constexpr float kMaxGainWanderDb    = 24.0f;   ///< FR-033
    static constexpr float kMinRatio           = 0.25f;   ///< FR-022
    static constexpr float kMaxRatio           = 64.0f;
    static constexpr float kMinNoteHz          = 8.0f;    ///< FR-022

    /// FR-031's Q range in the log2 domain the wander maps work in. `std::log2`
    /// is NOT constexpr in C++20: `static constexpr float k = std::log2(x);`
    /// compiles only as a GCC/MSVC builtin extension and is REJECTED by Clang,
    /// so it would break the macOS and Linux legs while a Windows build stayed
    /// green. The repo carries this lesson twice already - harmonic_cloud.h:253
    /// and entropy_processor.h:82-85 - and both resolve it the same way, with
    /// detail::constexprLn (core/db_utils.h:156) over detail::kLn2 (:144).
    /// Both are pinned to std::log2 by a RUNTIME equivalence check in
    /// ResonanceDriftNetwork_ControlSurfaceClamps arm (vi), so the constexpr
    /// series and the library function cannot drift apart on any toolchain.
    static constexpr float kMinLog2Q = detail::constexprLn(kMinResonatorQ) / detail::kLn2;
    static constexpr float kMaxLog2Q = detail::constexprLn(kMaxResonatorQ) / detail::kLn2;
    static_assert(kMinLog2Q < kMaxLog2Q, "the FR-031 Q range must be ordered in log2");

    /// FR-042. gateSteady() snaps any wake at or below this to EXACTLY 0.0f.
    /// This is what keeps the sleep edge's exact `== 0.0f` test valid by
    /// construction once Phase 10 writes `getEnvelopeValue() * getActiveDepth()`
    /// into setPeakWake: a release tail that stops at 1e-8, or a depth product
    /// that lands on a denormal, would otherwise leave the gate target nonzero
    /// forever, engineActive would never clear, FR-042's state clear would never
    /// run, and SC-004 (c)'s CPU saving would never be realised.
    static constexpr float kWakeSilenceEpsilon = 1.0e-6f;

    /// prepare()'s sample-rate floor (spec correction C-9). NOT 1 Hz:
    /// `kMaxResonatorFrequencyRatio * 1 Hz = 0.45 Hz` is BELOW
    /// `kMinResonatorFrequency = 20 Hz`, which INVERTS every clamp built from
    /// that pair - and std::clamp has the precondition !(hi < lo); violating it
    /// is undefined behaviour, and MSVC's <algorithm> fires
    /// _STL_VERIFY("invalid bounds argument passed to std::clamp"). The
    /// inversion is not confined to this header: the shipped
    /// ResonatorBank::clampFrequency is a bare
    /// std::clamp(hz, kMinResonatorFrequency, 0.45f * fs)
    /// (resonator_bank.h:631-635), so at 1 Hz EVERY frequency write would be UB
    /// inside a component SC-013 forbids amending. 8000 Hz gives
    /// 0.45 * fs = 3600 Hz, two orders clear of the 20 Hz floor, and sits below
    /// any rate a host presents.
    static constexpr double kMinUsableSampleRate = 8000.0;
    static_assert(static_cast<double>(kMaxResonatorFrequencyRatio) * kMinUsableSampleRate
                      > static_cast<double>(kMinResonatorFrequency),
                  "the frequency clamp range must stay ordered at the lowest accepted rate");

    // =========================================================================
    // Nested types
    // =========================================================================

    /// FR-020. APPEND ONLY - this becomes a persisted plugin parameter at
    /// Phase 12. Deliberately NOT named TuningMode: that name already exists at
    /// namespace scope in resonator_bank.h:133, which this header includes.
    enum class AnchorMode : std::uint8_t { Free = 0, Keyed = 1, Hybrid = 2 };

    /// FR-002. Callers MUST use designated initialisers -
    /// PrepareConfig{.numPeaks = 8} - so no narrowing conversion hides in a
    /// positional brace init (Clang errors where MSVC does not). Nested,
    /// following NoiseOrganism::PrepareConfig (noise_organism.h:190).
    struct PrepareConfig {
        /// Clamped [64, 8192], retained and reported - but it SIZES NOTHING in
        /// this component (spec correction C-1). The render is per-sample with
        /// local dry capture, so there is no scratch buffer, no heap term, and
        /// processBlock accepts any numSamples whatever this says.
        std::size_t maxBlockSamples = 2048;
        std::size_t numPeaks = kMaxPeaks;  ///< Clamped [1, kMaxPeaks].
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Configure for a sample rate, restore the FR-016 defaults, and
    ///        snap every derived value to its steady state (FR-002).
    /// @param sampleRate Host sample rate; non-finite is substituted by 48000,
    ///        then floored at kMinUsableSampleRate (C-9).
    /// @param config Peak count and (reported-only) maximum block size.
    ///
    /// Allocates nothing. Re-preparing a live object is legal and fully
    /// re-initialises; lane seeds survive because step 12 re-applies seed_.
    /// The step order below is load-bearing (plan S2.1) - each numbered comment
    /// says what breaks if that step moves.
    void prepare(double sampleRate, const PrepareConfig& config) noexcept {
        // 1. C-9's floor, not 1 Hz: below 8 kHz the derived clamp pair inverts.
        sampleRate_ = std::max(kMinUsableSampleRate, sanitise(sampleRate, 48000.0));
        const auto fs = static_cast<float>(sampleRate_);

        // 2. Clamp the caller's request ONCE, here; applyDefaults never touches
        //    PrepareConfig fields (the noise_organism.h:1895-1900 rule).
        config_.maxBlockSamples =
            std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});
        config_.numPeaks = std::clamp(config.numPeaks, std::size_t{1}, kMaxPeaks);

        // 3. Cache the Nyquist-derived clamps. Step 1's floor already makes both
        //    std::max calls no-ops at every accepted rate; they are written so
        //    the ordering std::clamp requires is visible HERE rather than
        //    inferred from a constant three pages away (C-9).
        minLog2Hz_ = std::log2(kMinResonatorFrequency);
        maxHz_ = std::max(kMinResonatorFrequency, fs * kMaxResonatorFrequencyRatio);
        maxLog2Hz_ = std::max(minLog2Hz_, std::log2(maxHz_));

        // 4. The engine. The three globals are pinned ONCE for the life of the
        //    object (FR-016). Each is already the constructed default
        //    (resonator_bank.h:625-627) and prepare snaps its smoother to it
        //    (:194-196); the explicit writes are documentation that survives a
        //    future change of those defaults, and they cost three stores.
        bank_.prepare(sampleRate_);
        bank_.setDamping(0.0f);
        bank_.setExciterMix(0.0f);
        bank_.setSpectralTilt(0.0f);
        perPeakOut_.fill(0.0f);

        // 5. The 48 wander lanes.
        for (Peak& p : peaks_) {
            p.freqLane.prepare(sampleRate_);
            p.qLane.prepare(sampleRate_);
            p.gainLane.prepare(sampleRate_);
            p.panLane.prepare(sampleRate_);
        }

        // 6. prepare() is the ONLY path back to the FR-016 defaults; reset() is
        //    configuration-preserving (FR-004).
        applyDefaults();

        // 7. Before step 8, or the configuration push below is a no-op.
        prepared_ = true;

        // 8. The single owner of laneDecimation_ and of every lane's
        //    setSmoothness (FR-037). Never recomputed anywhere else, so prepare
        //    and a later caller cannot disagree about the mapping.
        setWanderRate(kDefaultWanderRateHz);

        // 9. Output-stage smoothers, snapped (not ramped) at prepare.
        mixSmoother_.configure(kMixSmoothMs, fs);
        mixSmoother_.snapTo(mix_);
        wetScaleRamp_.configure(kGainRampMs, fs);
        wetScaleRamp_.snapTo(wetScaleTarget());

        // 10. Per-peak ramps. lastGateTarget must START in sync with the gate,
        //     or the first refreshGates() would skip a real re-target.
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            const float steady = gateSteady(i);
            p.gate.configure(kGainRampMs, fs);
            p.gate.snapTo(steady);
            p.lastGateTarget = steady;
            p.levelRamp.configure(kGainRampMs, fs);
            p.levelRamp.snapTo(dbToGain(p.levelDb));
        }

        // 11. Counters and grid phase.
        controlPhase_ = 0;
        laneCounter_ = 0;
        clampEngagements_ = 0;
        anchorsDirty_ = true;

        // 12. LAST, for the same reason NoiseOrganism does it last
        //     (noise_organism.h:299-302): it calls lane.reset(), which re-seeds
        //     the RNG and snaps the output smoother, so a seed distributed
        //     before step 5 would be discarded by that prepare.
        setSeed(seed_);

        // 13. One full control pass with NO slew limiting. Without it the first
        //     control step would slew from appliedLog2Hz = 0 (1 Hz) toward the
        //     anchor at 0.02 octaves/step, taking ~250 control steps to arrive.
        snapControlState();
    }

    /// @brief Rewind all audio and modulation state, PRESERVING configuration
    ///        (FR-004). Identical in meaning to NoiseOrganism::reset(), so
    ///        Phase 10 can reset both siblings at the same moment.
    ///
    /// The FR-038 default pan positions are CONFIGURATION and are therefore NOT
    /// redrawn here: setSeed() is the only thing that ever draws them, so a
    /// caller's setPeakPan survives every reset() and dies only on a re-seed.
    void reset() noexcept {
        controlPhase_ = 0;
        laneCounter_ = 0;
        clampEngagements_ = 0;
        anchorsDirty_ = true;

        // ResonatorBank::reset() is a configuration WIPE, not just a state
        // clear - it leaves every slot at 440 Hz, default Q and enabled = false
        // (resonator_bank.h:225-231) - so step 5's re-apply is mandatory. A
        // forwarded reset() without it renders digital silence.
        bank_.reset();
        perPeakOut_.fill(0.0f);

        // BrownianDrift::reset() re-seeds from configuredSeed_ and rewinds x_
        // to mean_ (brownian_drift.h:133-135, :242-247). This is the ONE line
        // that separates reset() from clearAudioState().
        for (Peak& p : peaks_) {
            p.freqLane.reset();
            p.qLane.reset();
            p.gainLane.reset();
            p.panLane.reset();
        }

        snapOutputStage();
        snapControlState();
    }

    /// @brief Clear audio state only, leaving the wander lanes freewheeling
    ///        (FR-046). Exactly reset() minus the four lane rewinds - that single
    ///        omission is the whole difference between the two methods.
    ///
    /// Nothing in this phase calls it; Phase 10 is its consumer.
    void clearAudioState() noexcept {
        controlPhase_ = 0;
        laneCounter_ = 0;
        clampEngagements_ = 0;
        anchorsDirty_ = true;

        bank_.reset();
        perPeakOut_.fill(0.0f);

        snapOutputStage();
        snapControlState();
    }

    /// @brief Re-seed every lane and rewind its stream (FR-005).
    /// @param seed Network seed; 0 is legal (deriveStreamSeed substitutes a
    ///        non-zero hash, core/random.h:109).
    ///
    /// DOCUMENTED CONSEQUENCE: this also redraws the FR-038 default pan
    /// positions, so setSeed OVERWRITES a previous setPeakPan. A caller that
    /// wants both must call setSeed FIRST.
    void setSeed(std::uint32_t seed) noexcept {
        seed_ = seed;
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            // The reset() after each setSeed is MANDATORY: setSeed re-seeds the
            // RNG but leaves x_ and the output smoother where they were
            // (brownian_drift.h:145-148), so without it a late setSeed would
            // change the increments but not the position. reset() re-seeds again
            // from configuredSeed_ - the same value just written - so the two
            // calls compose correctly.
            p.freqLane.setSeed(deriveStreamSeed(seed_, kSaltFreqLane + i));
            p.freqLane.reset();
            p.qLane.setSeed(deriveStreamSeed(seed_, kSaltQLane + i));
            p.qLane.reset();
            p.gainLane.setSeed(deriveStreamSeed(seed_, kSaltGainLane + i));
            p.gainLane.reset();
            p.panLane.setSeed(deriveStreamSeed(seed_, kSaltPanLane + i));
            p.panLane.reset();
        }

        // FR-038's default pan spread, from a dedicated one-shot generator.
        // Deterministic alternating spread: peaks alternate sides with
        // increasing |pan|, so the LOWEST peaks sit near centre (mono-compatible
        // bass, which a subterranean instrument needs) and the highest sit
        // widest, and adjacent peaks never share a side - pitch is decorrelated
        // from position.
        Xorshift32 panRng{deriveStreamSeed(seed_, kSaltPanPosition)};
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            const float side = ((i % 2u) == 0u) ? 1.0f : -1.0f;
            const float step = static_cast<float>((i / 2u) + 1u)
                               / (static_cast<float>(kMaxPeaks) * 0.5f);
            const float base = side * step;
            const float jitter = panRng.nextFloat() * (1.0f / static_cast<float>(kMaxPeaks));
            Peak& p = peaks_[i];
            p.panPosition = std::clamp(base + jitter, -1.0f, 1.0f);
        }
    }

    /// @brief Render a stereo block, OVERWRITING the outputs (FR-003).
    /// @param inL,inR Input channels (may alias the outputs, in either pairing)
    /// @param outL,outR Output channels
    /// @param numSamples Any length, including far above config_.maxBlockSamples
    ///
    /// Guard ladder, in order (harmonic_cloud.h:880-891, noise_organism.h:414-430):
    /// ANY null pointer writes nothing on EITHER channel and advances nothing;
    /// numSamples == 0 is a no-op consuming no control step; an un-prepared
    /// network fills exactly numSamples zeros on both channels and advances
    /// nothing.
    void processBlock(const float* inL, const float* inR,
                      float* outL, float* outR, std::size_t numSamples) noexcept {
        if (inL == nullptr || inR == nullptr || outL == nullptr || outR == nullptr) {
            return;  // nothing written on EITHER channel, NOTHING advanced
        }
        if (numSamples == 0) {
            return;  // no control step consumed, buffers untouched
        }
        if (!prepared_) {
            std::fill_n(outL, numSamples, 0.0f);  // exactly numSamples zeros
            std::fill_n(outR, numSamples, 0.0f);
            return;                               // ...and no state advance
        }

        // ABSOLUTE 64-sample control grid. controlPhase_ is a RESIDUE carried
        // ACROSS CALLS, deliberately NOT HarmonicCloud's block-relative chunking
        // (harmonic_cloud.h:144 measures min(64, numSamples - done) from the
        // block start): that runs TWO control steps for a 36 + 28 split where an
        // unsplit 64 runs one, and SC-010 requires the irregular partition to
        // agree with a 512-block render within kSampleTolerance.
        std::size_t done = 0;
        while (done < numSamples) {
            if (controlPhase_ == 0) {
                updateControl();
            }
            const std::size_t chunk =
                std::min(numSamples - done, kControlChunkSamples - controlPhase_);
            renderChunk(inL + done, inR + done, outL + done, outR + done, chunk);
            controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples;
            done += chunk;
        }
    }

    // =========================================================================
    // Control surface - the NORMATIVE argument contract (plan S1.4)
    // =========================================================================
    // Out-of-range `peak` (FR-050): every setter is a SILENT NO-OP
    //   (the resonator_bank.h:329 idiom); every getter returns the documented
    //   neutral and NEVER indexes the array - 0.0f for floats, 0 for sizes,
    //   false for bools, AnchorMode::Free for the mode.
    //
    // Non-finite float argument (FR-008): the setter is a NO-OP and the
    //   PREVIOUS VALUE STANDS. This differs deliberately from
    //   NoiseOrganism::sanitise, which substitutes a per-argument neutral
    //   (noise_organism.h:1099-1101); FR-008 specifies rejection, and SC-009 (b)
    //   asserts the previous value is still reported. The guard is one line at
    //   the top of every float setter, and it is LOAD-BEARING, not defensive
    //   hygiene: std::clamp does NOT reject NaN (with v = NaN both v < lo and
    //   hi < v are false, so v is returned unchanged), and the trace is fatal -
    //   setPeakAnchorHz(i, NaN) -> anchor NaN -> ResonatorBank::setFrequency
    //   clamps with a bare std::clamp -> omega NaN -> sin/cos NaN -> NaN
    //   coefficients, and Biquad::process resets only on a non-finite INPUT
    //   SAMPLE, never on non-finite coefficients, so the resonator would emit
    //   NaN forever - through FR-018's clamp, which is itself a std::clamp and
    //   propagates NaN.
    //
    // Out-of-range float argument: clamped into FR-016's range, and the
    //   matching getter reports the CLAMPED value.
    // =========================================================================

    /// @brief Number of peaks that render and that FR-019 normalises by.
    /// @param n Clamped [1, kMaxPeaks]. Never reallocates.
    void setNumPeaks(std::size_t n) noexcept {
        config_.numPeaks = std::clamp(n, std::size_t{1}, kMaxPeaks);
        retargetWetScale();
        // Peaks dropped by a reduction are silenced THROUGH the FR-041 ramp -
        // gateSteady() returns 0 for an out-of-count peak, so refreshGates()
        // hands each of them a 50 ms fade rather than cutting them off.
        refreshGates();
    }

    /// @brief Select how peak anchors are derived (FR-020).
    void setAnchorMode(AnchorMode mode) noexcept {
        anchorMode_ = mode;
        anchorsDirty_ = true;
    }

    /// @brief Set peak `peak`'s Free-mode anchor (FR-021).
    /// @param hz Clamped [kMinResonatorFrequency, 0.45 * fs].
    ///
    /// Anchor clamping is SILENT - it does not touch the FR-052 clamp
    /// engagement counter - because at low sample rates it is a legitimate
    /// configuration outcome, not a signal-path event (FR-025).
    void setPeakAnchorHz(std::size_t peak, float hz) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(hz)) return;
        Peak& p = peaks_[peak];
        p.freeAnchorHz = std::clamp(hz, kMinResonatorFrequency, maxHz_);
        p.freeLog2Hz = std::log2(p.freeAnchorHz);
        // anchorLog2Hz is NOT written here: recomputeAnchors() is its single
        // owner (T007), and it runs on the control grid whenever anchorsDirty_.
        // A setter that also wrote it would resolve the anchor in Free-mode
        // terms even when the network is in Keyed or Hybrid mode.
        anchorsDirty_ = true;
    }

    /// @brief Set peak `peak`'s Keyed-mode ratio against noteFrequency (FR-022).
    /// @param ratio Clamped [kMinRatio, kMaxRatio].
    void setPeakRatio(std::size_t peak, float ratio) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(ratio)) return;
        Peak& p = peaks_[peak];
        p.ratio = std::clamp(ratio, kMinRatio, kMaxRatio);
        p.ratioLog2 = std::log2(p.ratio);
        anchorsDirty_ = true;
    }

    /// @brief Set the note the Keyed and Hybrid modes track (FR-022).
    /// @param hz Clamped [kMinNoteHz, 0.45 * fs].
    void setNoteFrequency(float hz) noexcept {
        if (!detail::isFinite(hz)) return;
        noteHz_ = std::clamp(hz, kMinNoteHz, std::max(kMinNoteHz, maxHz_));
        anchorsDirty_ = true;
    }

    /// @brief Hybrid-mode pull from the Free anchor toward the Keyed anchor
    ///        (FR-023). 0 is bit-equal to Free; +1 is Keyed; -1 mirrors away.
    /// @param g Clamped [-1, +1].
    void setGravity(float g) noexcept {
        if (!detail::isFinite(g)) return;
        gravity_ = std::clamp(g, -1.0f, 1.0f);
        anchorsDirty_ = true;
    }

    /// @brief Set peak `peak`'s BASE level in dB (FR-017).
    /// @param dB Clamped [kMinPeakLevelDb, kMaxPeakLevelDb].
    ///
    /// The base level is owned by a per-peak, per-sample LinearRamp at
    /// kGainRampMs (spec correction C-6) - NOT by the bank's dB write, which
    /// carries only appliedGainDb - levelDb. A build without this ramp steps
    /// 72 dB in one sample on a -60 -> +12 move.
    void setPeakLevel(std::size_t peak, float dB) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(dB)) return;
        Peak& p = peaks_[peak];
        p.levelDb = std::clamp(dB, kMinPeakLevelDb, kMaxPeakLevelDb);
        p.levelRamp.setTarget(dbToGain(p.levelDb));
    }

    /// @brief Set peak `peak`'s base Q (FR-031).
    /// @param q Clamped [kMinResonatorQ, kMaxResonatorQ] = [0.1, 100].
    void setPeakQ(std::size_t peak, float q) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(q)) return;
        Peak& p = peaks_[peak];
        p.baseQ = std::clamp(q, kMinResonatorQ, kMaxResonatorQ);
        p.baseLog2Q = std::log2(p.baseQ);
    }

    /// @brief Set peak `peak`'s frequency-wander depth (FR-030).
    /// @param semitones Clamped [0, kMaxFreqWanderSemis].
    void setFreqWander(std::size_t peak, float semitones) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(semitones)) return;
        peaks_[peak].freqWanderSemis = std::clamp(semitones, 0.0f, kMaxFreqWanderSemis);
    }

    /// @brief Set peak `peak`'s Q-wander depth in octaves of Q (FR-031).
    /// @param octaves Clamped [0, kMaxQWanderOctaves].
    void setQWander(std::size_t peak, float octaves) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(octaves)) return;
        peaks_[peak].qWanderOct = std::clamp(octaves, 0.0f, kMaxQWanderOctaves);
    }

    /// @brief Set peak `peak`'s gain-wander depth in dB (FR-033).
    /// @param dB Clamped [0, kMaxGainWanderDb].
    void setGainWander(std::size_t peak, float dB) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(dB)) return;
        peaks_[peak].gainWanderDb = std::clamp(dB, 0.0f, kMaxGainWanderDb);
    }

    /// @brief Set peak `peak`'s stereo position (FR-038).
    /// @param position Clamped [-1, +1]. NOTE: a later setSeed redraws this.
    void setPeakPan(std::size_t peak, float position) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(position)) return;
        peaks_[peak].panPosition = std::clamp(position, -1.0f, 1.0f);
    }

    /// @brief Set peak `peak`'s pan-wander depth (FR-038).
    /// @param depth Clamped [0, 1] - a fraction of the full pan range.
    void setPeakPanWander(std::size_t peak, float depth) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(depth)) return;
        peaks_[peak].panWanderDepth = std::clamp(depth, 0.0f, 1.0f);
    }

    /// @brief Set the network-wide wander rate (FR-034), and with it the FR-037
    ///        lane decimation and every lane's smoothness.
    /// @param hz Clamped [kMinWanderRateHz, kMaxWanderRateHz] = [0.002, 1.0].
    ///
    /// THE SINGLE OWNER of laneDecimation_ and of every lane's setSmoothness.
    /// A BrownianDrift's tau saturates at kTauMax = 30 s, so WITHOUT the
    /// decimation the whole sub-range [0.002, 0.0333] Hz - INCLUDING this
    /// component's own 0.03 Hz default - would be a dead zone where every rate
    /// rendered identically. Decimating the lane advance by D multiplies the
    /// reachable period by D: 17 * 30 s = 510 s covers the 500 s the slowest
    /// rate asks for.
    void setWanderRate(float hz) noexcept {
        if (!detail::isFinite(hz)) return;
        wanderRateHz_ = std::clamp(hz, kMinWanderRateHz, kMaxWanderRateHz);

        const float requestedTau = 1.0f / wanderRateHz_;  // [1, 500] s
        const auto ceilSteps = static_cast<std::size_t>(
            std::ceil(requestedTau / BrownianDrift::kTauMax));
        const std::size_t newDecimation =
            std::clamp(ceilSteps, std::size_t{1}, kMaxLaneDecimation);
        const float tau = requestedTau / static_cast<float>(newDecimation);
        const float smoothness = std::clamp(
            (tau - BrownianDrift::kTauMin) / (BrownianDrift::kTauMax - BrownianDrift::kTauMin),
            0.0f, 1.0f);

        for (Peak& p : peaks_) {
            p.freqLane.setSmoothness(smoothness);
            p.qLane.setSmoothness(smoothness);
            p.gainLane.setSmoothness(smoothness);
            p.panLane.setSmoothness(smoothness);
        }

        // REBASE, do not reset: a rate change mid-render must neither force an
        // extra lane advance nor skip one.
        laneCounter_ %= newDecimation;
        laneDecimation_ = newDecimation;
    }

    /// @brief Master switch for all wander (FR-034). Off zeroes every DEPTH; it
    ///        does not rewind a lane, so re-enabling does not jump.
    void setWanderEnabled(bool enabled) noexcept { wanderEnabled_ = enabled; }

    /// @brief Set the per-control-step slew ceilings, in octaves (FR-035).
    /// @param freqOctavesPerStep Clamped [kMinSlewOctaves, kMaxSlewOctaves].
    /// @param qOctavesPerStep Clamped to the same range.
    ///
    /// Each argument is rejected INDEPENDENTLY when non-finite, so one bad
    /// value cannot discard a good one.
    void setSlewCeilings(float freqOctavesPerStep, float qOctavesPerStep) noexcept {
        if (detail::isFinite(freqOctavesPerStep)) {
            freqSlewOct_ = std::clamp(freqOctavesPerStep, kMinSlewOctaves, kMaxSlewOctaves);
        }
        if (detail::isFinite(qOctavesPerStep)) {
            qSlewOct_ = std::clamp(qOctavesPerStep, kMinSlewOctaves, kMaxSlewOctaves);
        }
    }

    /// @brief Set peak `peak`'s wake amount (FR-040).
    /// @param amount Clamped [0, 1].
    ///
    /// The clamp is LOAD-BEARING, not cosmetic. A Phase-8 caller driving wake
    /// from agent energy, or a Phase-10 caller writing
    /// getEnvelopeValue() * getActiveDepth(), that passed 1.5 would scale one
    /// peak 50 % past unity into the FR-045 wet trim; a negative value would
    /// invert that peak's polarity against the other eleven. No other criterion
    /// in this phase looks at either. Clamping in the setter is the only place
    /// that cannot be bypassed.
    void setPeakWake(std::size_t peak, float amount) noexcept {
        if (peak >= kMaxPeaks) return;
        if (!detail::isFinite(amount)) return;
        peaks_[peak].wakeAmount = std::clamp(amount, 0.0f, 1.0f);
        // FR-040/FR-042. refreshGates() is the ONLY writer of gate targets, and
        // it also carries the wake edge - so a peak whose wake leaves zero has
        // its bank slot re-applied and enabled on THIS call rather than up to 63
        // samples later at the next control step.
        refreshGates();
    }

    /// @brief Put peak `peak` to sleep or wake it (FR-042). A dormant peak's
    ///        bank slot is not called at all.
    void setPeakDormant(std::size_t peak, bool dormant) noexcept {
        if (peak >= kMaxPeaks) return;
        peaks_[peak].dormant = dormant;
        // Identical treatment to setPeakWake: gateSteady() folds the dormant
        // flag and wakeAmount into ONE steady-state number, which is what makes
        // setPeakDormant(i, true) and setPeakWake(i, 0.0f) behaviourally
        // indistinguishable (the cross-cutting Dormancy rule, SC-015 (d)).
        refreshGates();
    }

    /// @brief Set the dry/wet crossfade (FR-043).
    /// @param mix Clamped [0, 1]; 0 is bit-exact dry, 1 is fully wet.
    void setMix(float mix) noexcept {
        if (!detail::isFinite(mix)) return;
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Set the network-wide static wet trim (FR-045).
    /// @param dB Clamped [kMinWetGainDb, kMaxWetGainDb].
    void setWetGain(float dB) noexcept {
        if (!detail::isFinite(dB)) return;
        wetGainDb_ = std::clamp(dB, kMinWetGainDb, kMaxWetGainDb);
        retargetWetScale();
    }

    // =========================================================================
    // Configuration read surface (FR-051)
    // =========================================================================

    [[nodiscard]] std::size_t getNumPeaks() const noexcept { return config_.numPeaks; }
    [[nodiscard]] AnchorMode getAnchorMode() const noexcept { return anchorMode_; }
    [[nodiscard]] float getNoteFrequency() const noexcept { return noteHz_; }
    [[nodiscard]] float getGravity() const noexcept { return gravity_; }
    [[nodiscard]] float getWanderRate() const noexcept { return wanderRateHz_; }
    [[nodiscard]] float getFreqSlewCeiling() const noexcept { return freqSlewOct_; }
    [[nodiscard]] float getQSlewCeiling() const noexcept { return qSlewOct_; }
    [[nodiscard]] bool isWanderEnabled() const noexcept { return wanderEnabled_; }
    [[nodiscard]] float getMix() const noexcept { return mix_; }
    [[nodiscard]] float getWetGain() const noexcept { return wetGainDb_; }

    [[nodiscard]] float getPeakAnchorHz(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].freeAnchorHz : 0.0f;
    }
    [[nodiscard]] float getPeakRatio(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].ratio : 0.0f;
    }
    [[nodiscard]] float getPeakLevel(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].levelDb : 0.0f;
    }
    [[nodiscard]] float getPeakQ(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].baseQ : 0.0f;
    }
    [[nodiscard]] float getFreqWander(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].freqWanderSemis : 0.0f;
    }
    [[nodiscard]] float getQWander(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].qWanderOct : 0.0f;
    }
    [[nodiscard]] float getGainWander(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].gainWanderDb : 0.0f;
    }
    [[nodiscard]] float getPeakPan(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].panPosition : 0.0f;
    }
    [[nodiscard]] float getPeakPanWander(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].panWanderDepth : 0.0f;
    }
    [[nodiscard]] bool isPeakDormant(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) && peaks_[peak].dormant;
    }
    [[nodiscard]] float getPeakWakeAmount(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].wakeAmount : 0.0f;
    }

    // =========================================================================
    // Realised-state read surface (FR-052)
    // =========================================================================

    [[nodiscard]] float getPeakCurrentFrequency(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].appliedHz : 0.0f;
    }
    [[nodiscard]] float getPeakCurrentQ(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].appliedQ : 0.0f;
    }
    /// The peak's TOTAL composed dB - base level plus wander - EXCLUDING the
    /// FR-041 gate, which is a separate read.
    [[nodiscard]] float getPeakCurrentGainDb(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].appliedGainDb : 0.0f;
    }
    [[nodiscard]] float getPeakCurrentPan(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].appliedPan : 0.0f;
    }
    [[nodiscard]] float getPeakGate(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) ? peaks_[peak].gate.getCurrentValue() : 0.0f;
    }

    /// @brief The peak's equivalent RT60, Q * ln1000 / (pi * f) (FR-032).
    /// REPORTED ONLY - ResonatorBank::setDecay is NEVER called by this
    /// component, so no decay this returns is ever written anywhere.
    [[nodiscard]] float getPeakEquivalentRt60(std::size_t peak) const noexcept {
        if (peak >= kMaxPeaks) return 0.0f;
        // kLn1000 is the shipped namespace-scope constant (resonator_bank.h:81),
        // the same one ResonatorBank::rt60ToQ inverts - so this getter and the
        // bank's own conversion can never disagree.
        const Peak& p = peaks_[peak];
        if (p.appliedHz <= 0.0f) return 0.0f;
        return (p.appliedQ * kLn1000) / (kPi * p.appliedHz);
    }

    /// @brief FR-019's N. This is numPeaks, NEVER the awake count (D-11): an
    /// "awake" reading computes 1/sqrt(0) = inf when every peak sleeps, and
    /// inf * 0.0f = NaN.
    [[nodiscard]] std::size_t getNormalisationPeakCount() const noexcept {
        return config_.numPeaks;
    }
    [[nodiscard]] bool isPeakEngineActive(std::size_t peak) const noexcept {
        return (peak < kMaxPeaks) && peaks_[peak].engineActive;
    }
    [[nodiscard]] std::size_t getLaneDecimation() const noexcept { return laneDecimation_; }
    [[nodiscard]] std::uint32_t getClampEngagementCount() const noexcept {
        return clampEngagements_;
    }

    /// @brief Always 0 (spec correction C-1). This component has NO heap term:
    /// every buffer is a fixed-size member, and the render is per-sample with
    /// local dry capture, so even the dry path needs no scratch. The getter
    /// exists so the Phase-10 host can total its children uniformly.
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept { return 0u; }

    /// @brief The CLAMPED PrepareConfig::maxBlockSamples, in [64, 8192].
    ///
    /// FR-062 keeps the field and requires it to be "reported by the read
    /// surface": it sizes nothing here, so without a getter a caller cannot see
    /// that its request was clamped, and SC-011's "a footprint independent of
    /// the configured block size" would be a statement about a value no test
    /// can read back. processBlock accepts any numSamples whatever this says
    /// (SC-019 (e) renders 65 536 against a configured 64).
    [[nodiscard]] std::size_t getMaxBlockSamples() const noexcept {
        return config_.maxBlockSamples;
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // =========================================================================
    // FR-005 salt table (plan S2.4)
    // =========================================================================
    // APPEND ONLY. Renumbering a base silently changes every Phase-3 render,
    // because each lane's stream is deriveStreamSeed(seed_, base + peak)
    // (core/random.h:102).
    static constexpr std::size_t kSaltFreqLane = 0;      // + peak
    static constexpr std::size_t kSaltQLane = 16;        // + peak
    static constexpr std::size_t kSaltGainLane = 32;     // + peak
    static constexpr std::size_t kSaltPanLane = 48;      // + peak
    static constexpr std::size_t kSaltPanPosition = 64;  // one-shot draw, not a lane
    static constexpr std::size_t kSaltNextFree = 80;

    static_assert(kSaltFreqLane + kMaxPeaks <= kSaltQLane, "freq salts overlap the Q block");
    static_assert(kSaltQLane + kMaxPeaks <= kSaltGainLane, "Q salts overlap the gain block");
    static_assert(kSaltGainLane + kMaxPeaks <= kSaltPanLane, "gain salts overlap the pan block");
    static_assert(kSaltPanLane + kMaxPeaks <= kSaltPanPosition,
                  "pan-lane salts overlap the position block");
    static_assert(kSaltPanPosition + kMaxPeaks <= kSaltNextFree, "salt table overflow");

    // =========================================================================
    // FR-016 normative default table (plan S3)
    // =========================================================================

    /// Geometric, ratio ~1.37, deliberately low-biased (roadmap line 17,
    /// "subterranean").
    static constexpr std::array<float, kMaxPeaks> kDefaultAnchorHz{
        40.0f, 55.0f, 75.0f, 103.0f, 141.0f, 193.0f,
        265.0f, 363.0f, 497.0f, 681.0f, 933.0f, 1278.0f};

    /// Harmonic-ISH: integers detuned by up to +/-0.8 % so a keyed patch does
    /// not collapse onto an exact harmonic comb. NORMATIVE - used verbatim,
    /// NEVER regenerated to fit a tighter prose bound (SC-014 (b)/(d)/(e) assert
    /// against exactly these twelve numbers).
    static constexpr std::array<float, kMaxPeaks> kDefaultRatio{
        0.5f, 1.0f, 1.5f, 2.0f, 2.98f, 4.0f,
        5.04f, 6.0f, 7.02f, 8.0f, 9.98f, 12.0f};

    static constexpr float kDefaultLevelDb = -6.0f;
    static constexpr float kDefaultBaseQ = 12.0f;
    static constexpr float kDefaultFreqWanderSemis = 3.0f;
    static constexpr float kDefaultQWanderOct = 0.5f;
    static constexpr float kDefaultGainWanderDb = 6.0f;
    static constexpr float kDefaultPanWanderDepth = 0.2f;
    static constexpr float kDefaultNoteHz = 55.0f;

    /// Pre-prepare values for the Nyquist-derived clamps, so that a setter
    /// called BEFORE prepare cannot hand std::clamp an inverted [lo, hi] pair.
    /// Built with detail::constexprLn for the same reason kMinLog2Q is.
    static constexpr float kDefaultMaxHz = 48000.0f * kMaxResonatorFrequencyRatio;
    static constexpr float kInitMinLog2Hz =
        detail::constexprLn(kMinResonatorFrequency) / detail::kLn2;
    static constexpr float kInitMaxLog2Hz = detail::constexprLn(kDefaultMaxHz) / detail::kLn2;
    static_assert(kInitMinLog2Hz < kInitMaxLog2Hz,
                  "the pre-prepare frequency clamp range must be ordered");

    static constexpr std::uint32_t kMaxClampCount = 0xFFFFFFFFu;

    // =========================================================================
    // State layout (plan S1.5)
    // =========================================================================

    /// One peak. Nested POD-ish aggregate: `Peak` at namespace scope is NOT free
    /// (a file-local `struct Peak` already exists in two Seraphis test TUs);
    /// nesting removes the question entirely - the SlowEventScheduler::Event
    /// precedent (slow_event_scheduler.h:187-191).
    struct Peak {
        // ---- configuration (FR-016 defaults, restored only by prepare) ------
        float freeAnchorHz = 40.0f;
        float ratio = 0.5f;
        float levelDb = kDefaultLevelDb;
        float baseQ = kDefaultBaseQ;
        float freqWanderSemis = kDefaultFreqWanderSemis;
        float qWanderOct = kDefaultQWanderOct;
        float gainWanderDb = kDefaultGainWanderDb;
        float panPosition = 0.0f;  ///< seeded one-shot draw at prepare/setSeed
        float panWanderDepth = kDefaultPanWanderDepth;
        float wakeAmount = 1.0f;
        bool dormant = false;

        // ---- cached log-domain configuration (recomputed only on a setter) --
        float anchorLog2Hz = 0.0f;  ///< log2 of the mode-resolved, clamped anchor
        float freeLog2Hz = 0.0f;    ///< log2(freeAnchorHz), for the Hybrid crossfade
        float ratioLog2 = 0.0f;     ///< log2(ratio), for the Keyed anchor
        float baseLog2Q = 0.0f;     ///< log2(baseQ)

        // ---- lanes (FR-005 salts; 4 per peak, 48 per network) ---------------
        BrownianDrift freqLane;
        BrownianDrift qLane;
        BrownianDrift gainLane;
        BrownianDrift panLane;

        // ---- per-sample ramps (FR-041, FR-017/C-6) --------------------------
        LinearRamp gate;                  ///< target set ONLY via refreshGates()
        float lastGateTarget = 0.0f;      ///< re-target ONLY when this actually changes
        LinearRamp levelRamp;             ///< FR-017 base level as dbToGain(levelDb)

        // ---- computed targets, maintained for EVERY peak incl. dormant ------
        float targetLog2Hz = 0.0f;
        float targetLog2Q = 0.0f;
        float targetGainDb = 0.0f;
        float targetPan = 0.0f;

        // ---- applied (slew-limited) state = the FR-052 read surface ---------
        float appliedLog2Hz = 0.0f;
        float appliedLog2Q = 0.0f;
        float appliedHz = 0.0f;      ///< exp2(appliedLog2Hz), cached for the getter
        float appliedQ = 0.0f;
        float appliedGainDb = 0.0f;  ///< TOTAL dB = clamp(levelDb + wander)
        float appliedBankGainDb = 0.0f;  ///< = appliedGainDb - levelDb; the only dB the bank is told
        float appliedPan = 0.0f;
        float panGainL = 0.70710678f;  ///< cos(theta), FR-038 - the chunk TARGET
        float panGainR = 0.70710678f;  ///< sin(theta) - the chunk TARGET

        // ---- per-sample interpolation of the two per-chunk MULTIPLIES -------
        // Spec correction C-16. Everything else the control grid produces is
        // either a coefficient (frequency, Q - a change in the output's SLOPE,
        // which a second-difference criterion barely sees) or already ramped per
        // sample (the FR-041 gate, the FR-017 base level). These two are
        // multiplies applied straight to the sample, so writing them once per
        // 64-sample chunk steps the output every 1.33 ms - a zipper SC-002 (a)
        // measures at B/P = 15.7 (gain alone) and 8.8 (pan alone) against a
        // bound of 1.5. They are therefore carried ACROSS the chunk instead: one
        // linear segment per control step, retargeted from wherever the previous
        // one ended, so the trajectory stays a pure function of the ABSOLUTE
        // sample index and SC-010's block-size invariance is untouched.
        float wanderGain = 1.0f;     ///< dbToGain(appliedBankGainDb), interpolated
        float wanderGainInc = 0.0f;
        float panCurL = 0.70710678f;
        float panIncL = 0.0f;
        float panCurR = 0.70710678f;
        float panIncR = 0.0f;

        // ---- change detection against the bank (FR-015) ---------------------
        // Frequency and Q only: since C-16 there is no control-rate gain value
        // left to detect a change in (the slot gain is pinned at unity).
        float lastWrittenHz = 0.0f;
        float lastWrittenQ = 0.0f;

        // ---- engine ---------------------------------------------------------
        bool engineActive = false;  ///< FR-052's isPeakEngineActive
    };

    std::array<Peak, kMaxPeaks> peaks_{};

    // ---- the audio engine, behind the FR-013 seam (Tier 1) ------------------
    /// ONE bank; peak `i` is slot `i`. See the ENGINE TIER note in the banner.
    ResonatorBank bank_{};
    /// Per-sample scratch for ResonatorBank::processIndividual. A fixed-size
    /// MEMBER, not heap - T009's renderChunk fills it once per sample before the
    /// peak loop and bankProcess(i, x) reads perPeakOut_[i].
    std::array<float, kMaxResonators> perPeakOut_{};

    // ---- network-level state ------------------------------------------------
    double sampleRate_ = 48000.0;
    PrepareConfig config_{};
    bool prepared_ = false;
    std::uint32_t seed_ = 0;
    AnchorMode anchorMode_ = AnchorMode::Free;
    float noteHz_ = kDefaultNoteHz;
    float gravity_ = 0.0f;
    float wanderRateHz_ = kDefaultWanderRateHz;
    bool wanderEnabled_ = true;
    float freqSlewOct_ = kDefaultFreqStepOctaves;
    float qSlewOct_ = kDefaultQStepOctaves;
    float mix_ = 1.0f;
    float wetGainDb_ = kDefaultWetGainDb;

    /// Cached Nyquist-derived clamps, recomputed in prepare only. They start at
    /// the 48 kHz values rather than 0 so that the pair [kMinResonatorFrequency,
    /// maxHz_] is ORDERED even before the first prepare.
    float minLog2Hz_ = kInitMinLog2Hz;
    float maxLog2Hz_ = kInitMaxLog2Hz;
    float maxHz_ = kDefaultMaxHz;

    /// FR-037. Per NETWORK, not per lane: a rate change costs one integer
    /// recompute and lane phase cannot drift apart.
    std::size_t laneDecimation_ = 1;
    std::size_t laneCounter_ = 0;

    /// FR-007's ABSOLUTE grid residue, carried across calls.
    std::size_t controlPhase_ = 0;

    bool anchorsDirty_ = true;

    /// FR-019's 1/sqrt(N) and FR-045's trim folded into ONE per-sample ramp
    /// (correction C-4): FR-019 needs the normalisation ramped over kGainRampMs
    /// on a setNumPeaks change, and folding the static trim in makes setWetGain
    /// click-free for free, at the cost of one ramp instead of two.
    LinearRamp wetScaleRamp_;
    OnePoleSmoother mixSmoother_;

    std::uint32_t clampEngagements_ = 0;

    // =========================================================================
    // The FR-013 engine seam (plan S9.1)
    // =========================================================================
    // EVERY bank interaction in this component goes through these six inlines
    // and nothing else. That is not speculative abstraction: FR-013 makes the
    // Tier 0 <-> Tier 1 substitution a pre-approved, probe-gated outcome, and
    // the seam is what keeps it a one-file, six-function change with the public
    // surface and every other FR untouched.
    //
    // TIER 1 is in force (see the ENGINE TIER note in the banner): ONE bank_,
    // peak `i` is slot `i`, and the per-peak sample comes from the scratch that
    // renderChunk fills once per sample with ResonatorBank::processIndividual
    // (resonator_bank.h:554). Under Tier 0 the same six inlines forward to
    // banks_[i] slot 0 and bankProcess is banks_[i].process(x).

    /// @brief Peak `i`'s individual contribution for the CURRENT sample.
    /// @param i Peak index, < kMaxPeaks
    /// @param x The mono engine input - unused under Tier 1, where the bank has
    ///        already been advanced for this sample by processIndividual. It
    ///        stays in the signature because Tier 0's body is
    ///        `banks_[i].process(x)`.
    [[nodiscard]] float bankProcess(std::size_t i, [[maybe_unused]] float x) const noexcept {
        return perPeakOut_[i];
    }

    void bankSetFrequency(std::size_t i, float hz) noexcept { bank_.setFrequency(i, hz); }
    void bankSetQ(std::size_t i, float q) noexcept { bank_.setQ(i, q); }
    void bankSetGain(std::size_t i, float dB) noexcept { bank_.setGain(i, dB); }
    void bankSetEnabled(std::size_t i, bool on) noexcept { bank_.setEnabled(i, on); }

    /// @brief STATE CLEAR ONLY - slot `i`'s biquad delay line and nothing else.
    ///
    /// The caller owns the FR-014-order re-apply and the setEnabled write. That
    /// deliberately narrow contract is what makes ResonatorBank's Tier-1
    /// companion resetResonatorState (resonator_bank.h:613) a DROP-IN here:
    /// Tier 0's ResonatorBank::reset() is a state clear AND a configuration
    /// wipe, so only a caller-owned re-apply makes the two tiers agree.
    void bankReset(std::size_t i) noexcept { bank_.resetResonatorState(i); }

    /// @brief The FR-014 write triple, unconditional, in the mandatory order,
    ///        seeding the FR-015 change-detection state with what it just wrote.
    ///
    /// Frequency FIRST, then Q, then gain - never any other order, and never a
    /// frequency write without the Q write immediately after it, because
    /// ResonatorBank::setFrequency re-derives
    /// qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])
    /// (resonator_bank.h:333) off a decay table this component NEVER writes
    /// (ResonatorBank::setDecay is not called anywhere here), so decays_[i]
    /// stands at kDefaultDecayTime = 1.0 s forever and a stale Q would silently
    /// become rt60ToQ(f, 1.0) = 0.4548*f.
    void bankApply(std::size_t i, Peak& p) noexcept {
        bankSetFrequency(i, p.appliedHz);
        bankSetQ(i, p.appliedQ);
        // C-16: the slot's own gain is PINNED AT UNITY and the wander part of
        // the dB rides Peak::wanderGain per sample instead. The write itself
        // stays, in its mandated third position, because it is what pins a slot
        // the seam may have wiped (Tier 0's bankReset is ResonatorBank::reset(),
        // a configuration wipe) back to the unity this component now assumes.
        // ResonatorBank::setGain is a bare gains_[i] = dbToGain(dB) with no
        // smoother of any kind (resonator_bank.h:367-371), which is exactly why
        // a drifting factor cannot live there.
        bankSetGain(i, 0.0f);
        p.lastWrittenHz = p.appliedHz;
        p.lastWrittenQ = p.appliedQ;
    }

    /// @brief C-16: aim the three per-sample interpolators at what
    ///        composeApplied just produced, over exactly ONE control chunk.
    ///
    /// The increment is (target - current) / kControlChunkSamples and NOT a
    /// smoother's time constant: a segment that lands exactly on its target at
    /// the end of the chunk leaves no lag to accumulate, and re-deriving it from
    /// the CURRENT value every step means a block boundary in the middle of a
    /// segment or a float rounding residue heals on the next step instead of
    /// drifting.
    static void retargetPerSampleFactors(Peak& p) noexcept {
        constexpr float kInvChunk = 1.0f / static_cast<float>(kControlChunkSamples);
        p.wanderGainInc = (dbToGain(p.appliedBankGainDb) - p.wanderGain) * kInvChunk;
        p.panIncL = (p.panGainL - p.panCurL) * kInvChunk;
        p.panIncR = (p.panGainR - p.panCurR) * kInvChunk;
    }

    /// @brief C-16: put the interpolators ON their targets with zero slope -
    /// prepare/reset/clearAudioState (through snapControlState) and the FR-042
    /// wake edge, where the gate is provably exactly 0 so a snap is inaudible.
    static void snapPerSampleFactors(Peak& p) noexcept {
        p.wanderGain = dbToGain(p.appliedBankGainDb);
        p.wanderGainInc = 0.0f;
        p.panCurL = p.panGainL;
        p.panIncL = 0.0f;
        p.panCurR = p.panGainR;
        p.panIncR = 0.0f;
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    /// @brief Returns `v` when finite, otherwise the documented neutral.
    /// detail::isFinite is the fast-math-immune exponent test
    /// (core/db_utils.h:118) - a plain `v != v` or std::isnan folds away on the
    /// macOS -ffast-math leg.
    [[nodiscard]] static constexpr float sanitise(float v, float neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }

    /// @brief Double overload, for prepare()'s sampleRate (db_utils.h:125).
    [[nodiscard]] static constexpr double sanitise(double v, double neutral) noexcept {
        return detail::isFinite(v) ? v : neutral;
    }

    /// @brief FR-019's normalisation times FR-045's trim, as one linear scalar.
    [[nodiscard]] float wetScaleTarget() const noexcept {
        const auto n = static_cast<float>(config_.numPeaks);  // >= 1 always (D-11)
        return (1.0f / std::sqrt(n)) * dbToGain(wetGainDb_);
    }

    void retargetWetScale() noexcept { wetScaleRamp_.setTarget(wetScaleTarget()); }

    /// @brief The steady-state gate for peak `i` (FR-042).
    /// The kWakeSilenceEpsilon snap happens HERE, at the source, which is what
    /// keeps the sleep edge's exact `== 0.0f` test valid by construction.
    [[nodiscard]] float gateSteady(std::size_t i) const noexcept {
        if (i >= kMaxPeaks || i >= config_.numPeaks) return 0.0f;
        const Peak& p = peaks_[i];
        if (p.dormant) return 0.0f;
        return (p.wakeAmount <= kWakeSilenceEpsilon) ? 0.0f : p.wakeAmount;
    }

    /// @brief Re-target every gate that actually moved (FR-041).
    ///
    /// The `target != p.lastGateTarget` guard is LOAD-BEARING, not an
    /// optimisation: LinearRamp::setTarget recomputes
    /// increment_ = (target - current) / (rampMs * 0.001 * fs) on EVERY call
    /// (smoother.h:342-354), so re-targeting a gate that is mid-ramp to the
    /// value it is already heading for RESTARTS its 50 ms from wherever it has
    /// got to. A Phase-10 scheduler writing one peak's wake per block would
    /// otherwise stretch every OTHER peak's ramp without bound. The compare is
    /// exact because the target is either a literal 0.0f or the value
    /// setPeakWake stored verbatim.
    ///
    /// gate.setTarget() is called from HERE and from nowhere else - in
    /// particular never from updateControl(). Its callers are exactly
    /// setPeakWake, setPeakDormant and setNumPeaks (whose dropped peaks must be
    /// silenced THROUGH the ramp rather than abruptly); prepare(), reset() and
    /// clearAudioState() bypass it deliberately, snapping the gate and seeding
    /// lastGateTarget in the same breath so the shadow can never start out of
    /// sync.
    ///
    /// THE WAKE EDGE LIVES HERE (FR-042, plan S7.3), and deliberately OUTSIDE
    /// the change-detection guard. Doing it in the setter rather than at the
    /// next control step matters: a setter called mid-chunk starts the ramp
    /// moving on the NEXT SAMPLE, so a peak left engineActive == false would be
    /// silent for up to 63 samples while its gate was already rising - a
    /// <= 1.3 ms attack notch, and a direct falsification of FR-042's "the first
    /// audible sample is already filtered". Keeping the test unconditional costs
    /// one bool and one float compare per peak and makes the bad state
    /// (engineActive == false with a non-zero gate target) unreachable.
    void refreshGates() noexcept {
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            const float target = gateSteady(i);

            if (!p.engineActive && target != 0.0f) {
                // WAKE EDGE. Snap the stored applied values into the bank in ONE
                // write, in the FR-014 order, exempt from FR-035's per-step
                // limiter. The exemption is inaudible BY CONSTRUCTION: the ramp
                // has not lifted off yet, so this peak's gate is provably still
                // exactly 0.0f at this instant. appliedHz / appliedQ /
                // appliedBankGainDb are already the live drifted values, because
                // updateControl() keeps applied == target for every peak while
                // !engineActive (C-8) and computeTargets() runs for every peak
                // including a dormant one.
                bankApply(i, p);
                snapPerSampleFactors(p);  // C-16, the gate is provably 0 here
                bankSetEnabled(i, true);
                p.engineActive = true;
            }

            if (target != p.lastGateTarget) {
                p.gate.setTarget(target);
                p.lastGateTarget = target;
            }
        }
    }

    /// @brief FR-034's master switch, expressed as a multiplier on every DEPTH.
    ///
    /// Off scales the four depths to zero; it does NOT stop the lanes, which
    /// keep advancing in updateControl() regardless (plan S6.1, correction C-3).
    /// That is what makes FR-034's "freezes the drift without rewinding it"
    /// true: re-enabling resumes the trajectory an always-on lane would have
    /// been on, and the FR-035 ceiling - not a snap - carries the applied value
    /// back to it. Same shape as NoiseOrganism::wanderScale()
    /// (noise_organism.h:2200-2202).
    [[nodiscard]] float wanderScale() const noexcept {
        return wanderEnabled_ ? 1.0f : 0.0f;
    }

    /// @brief One lane's bounded contribution, in [-1, +1] (FR-030..FR-033).
    ///
    /// BrownianDrift::getCurrentValue() already clamps to [-1, +1]
    /// (brownian_drift.h:212-214), so the guard here is belt and braces that
    /// FR-008 asks for anyway. Taken through the CONCRETE BrownianDrift type,
    /// never through a ModulationSource& - the ABC's getCurrentValue is virtual
    /// (modulation_source.h:37) and there is no virtual dispatch anywhere on
    /// this component's control or audio path (FR-006).
    [[nodiscard]] static float laneValue(const BrownianDrift& lane) noexcept {
        return std::clamp(sanitise(lane.getCurrentValue(), 0.0f), -1.0f, 1.0f);
    }

    /// @brief FR-038's equal-power pan law. pan = -1 is hard left.
    static void updatePanGains(Peak& p) noexcept {
        const float theta = (p.appliedPan + 1.0f) * 0.25f * kPi;  // [0, pi/2]
        p.panGainL = std::cos(theta);
        p.panGainR = std::sin(theta);
    }

    /// @brief Restore the entire FR-016 default table (plan S3).
    /// panPosition is NOT written here - setSeed's seeded draw owns it, and
    /// prepare's step 12 runs after step 6. PrepareConfig fields are excluded:
    /// prepare has already clamped them from the caller's request.
    void applyDefaults() noexcept {
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            p.freeAnchorHz = std::clamp(kDefaultAnchorHz[i], kMinResonatorFrequency, maxHz_);
            p.freeLog2Hz = std::log2(p.freeAnchorHz);
            // anchorLog2Hz belongs to recomputeAnchors(), which prepare's step 13
            // (snapControlState) runs unconditionally right after this.
            p.ratio = kDefaultRatio[i];
            p.ratioLog2 = std::log2(p.ratio);
            p.levelDb = kDefaultLevelDb;
            p.baseQ = kDefaultBaseQ;
            p.baseLog2Q = std::log2(p.baseQ);
            p.freqWanderSemis = kDefaultFreqWanderSemis;
            p.qWanderOct = kDefaultQWanderOct;
            p.gainWanderDb = kDefaultGainWanderDb;
            p.panWanderDepth = kDefaultPanWanderDepth;
            p.wakeAmount = 1.0f;
            p.dormant = false;
        }
        anchorMode_ = AnchorMode::Free;
        noteHz_ = kDefaultNoteHz;
        gravity_ = 0.0f;
        wanderEnabled_ = true;
        freqSlewOct_ = kDefaultFreqStepOctaves;
        qSlewOct_ = kDefaultQStepOctaves;
        mix_ = 1.0f;
        wetGainDb_ = kDefaultWetGainDb;
        anchorsDirty_ = true;
    }

    /// @brief Re-configure and SNAP the output stage and every per-peak ramp.
    /// snapTo, not setTarget - a reset is a state clear, not a fade.
    void snapOutputStage() noexcept {
        const auto fs = static_cast<float>(sampleRate_);
        mixSmoother_.configure(kMixSmoothMs, fs);
        mixSmoother_.snapTo(mix_);
        wetScaleRamp_.configure(kGainRampMs, fs);
        wetScaleRamp_.snapTo(wetScaleTarget());
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            const float steady = gateSteady(i);
            p.gate.configure(kGainRampMs, fs);
            p.gate.snapTo(steady);
            p.lastGateTarget = steady;
            p.levelRamp.configure(kGainRampMs, fs);
            p.levelRamp.snapTo(dbToGain(p.levelDb));
        }
    }

    /// @brief Resolve every peak's anchor from the current AnchorMode (plan S4,
    ///        FR-020..FR-025). Runs on the control grid, ONLY when anchorsDirty_
    ///        (set by setAnchorMode, setPeakAnchorHz, setPeakRatio,
    ///        setNoteFrequency and setGravity - FR-024).
    ///
    /// Entirely in log2, which is the same closed form FR-023 states in natural
    /// logs (log f = log f_free + g * (log f_keyed - log f_free) is
    /// base-invariant) minus the exp/log round trip a linear-domain version
    /// would need. Cost: one std::log2 for noteHz_ plus, per peak, one multiply
    /// -add and one clamp - the clamp being the only per-peak branch.
    void recomputeAnchors() noexcept {
        const float noteLog2 = std::log2(noteHz_);
        for (Peak& p : peaks_) {
            // The initialiser IS the Free anchor (cached by setPeakAnchorHz and
            // applyDefaults), and it is also the fallback for an AnchorMode
            // value outside the three enumerators - which is why there is no
            // dead store here and no uninitialised path either.
            float log2Hz = p.freeLog2Hz;
            switch (anchorMode_) {
            case AnchorMode::Free:
                break;
            case AnchorMode::Keyed:
                log2Hz = noteLog2 + p.ratioLog2;  // == log2(noteHz_ * ratio)
                break;
            case AnchorMode::Hybrid:
                if (gravity_ == 0.0f) {
                    // FR-023's identity, and Edge Cases' "g = 0 must be BIT-EQUAL
                    // to Free mode, which forbids an implementation that always
                    // runs the log/exp round trip". THIS BRANCH IS THAT
                    // PROHIBITION: `fl + 0 * (kl - fl)` is exact in IEEE
                    // arithmetic only while (kl - fl) is finite and the product
                    // is exactly zero, and it stops being bit-equal the moment a
                    // future edit reorders the expression. The short-circuit is
                    // the assertion, not an optimisation.
                    log2Hz = p.freeLog2Hz;
                } else {
                    const float keyedLog2 = noteLog2 + p.ratioLog2;
                    // Index-PAIRED (FR-023): peak i moves between its OWN free
                    // anchor and its OWN keyed anchor, so the twelve results stay
                    // twelve distinct frequencies at g = 1 instead of collapsing
                    // onto a shared grid the way a nearest-neighbour law does.
                    log2Hz = p.freeLog2Hz + gravity_ * (keyedLog2 - p.freeLog2Hz);
                }
                break;
            }
            // FR-025. SILENT - no clamp-engagement counter - because at a low
            // sample rate a clamped anchor is a legitimate configuration
            // outcome, not a signal-path event. The pair is ordered by
            // prepare()'s kMinUsableSampleRate floor (C-9).
            p.anchorLog2Hz = std::clamp(log2Hz, minLog2Hz_, maxLog2Hz_);
        }
        anchorsDirty_ = false;
    }

    /// @brief The per-peak control-rate targets: the four wander maps (plan
    ///        S6.4, FR-030 / FR-031 / FR-033 / FR-038).
    ///
    /// Run for EVERY peak on every control step, dormant and out-of-count
    /// included (FR-042, Q6): a peak that slept through an octave of drift must
    /// wake onto the value it would have had, not the value it left.
    ///
    /// Three of the four maps are ADDITIVE OFFSETS IN A LOG DOMAIN, which is
    /// the whole reason this component works in log2: FR-035's ceiling is
    /// stated in octaves per control step, i.e. a difference of log2, so the
    /// limiter in updateControl() is a std::clamp and the only transcendental
    /// left per peak per step is composeApplied()'s exp2. It also makes FR-030's
    /// bound provable by inspection - |targetLog2Hz - anchorLog2Hz| <=
    /// freqWanderSemis/12 because laneValue is in [-1, +1] - which is exactly
    /// what the LaneBounds criterion asserts.
    ///
    /// With setWanderEnabled(false) every term collapses to zero (wanderScale)
    /// and this reduces EXACTLY to the unmodulated steady state the T007
    /// criteria were written against, which is why they keep meaning the same
    /// thing now that the lanes are live.
    void computeTargets(Peak& p) const noexcept {
        const float ws = wanderScale();

        // FREQUENCY (FR-030): f = anchor * 2^(semitones * lane / 12). The clamp
        // is the [20 Hz, 0.45*fs] pair cached by prepare, ordered by C-9's
        // sample-rate floor.
        p.targetLog2Hz = std::clamp(
            p.anchorLog2Hz + (ws * p.freqWanderSemis * laneValue(p.freqLane)) * (1.0f / 12.0f),
            minLog2Hz_, maxLog2Hz_);

        // Q (FR-031): MULTIPLICATIVE - Q = baseQ * 2^(octaves * lane) - so a
        // given depth means the same proportional bandwidth swing at every base
        // Q. ResonatorBank's constant-0 dB-peak bandpass keeps peak height
        // independent of Q (resonator_bank.h:560-591), so this does not double
        // as a gain modulation.
        p.targetLog2Q = std::clamp(p.baseLog2Q + ws * p.qWanderOct * laneValue(p.qLane),
                                   kMinLog2Q, kMaxLog2Q);

        // GAIN (FR-033): additive in dB. This is the TOTAL - base level plus
        // wander - jointly clamped exactly as FR-017 states, and it stays the
        // FR-052 read surface (getPeakCurrentGainDb).
        p.targetGainDb = std::clamp(p.levelDb + ws * p.gainWanderDb * laneValue(p.gainLane),
                                    kMinPeakLevelDb, kMaxPeakLevelDb);

        // PAN (FR-038): the position drifts around its seeded default; the
        // equal-power split of it happens in composeApplied().
        p.targetPan = std::clamp(p.panPosition + ws * p.panWanderDepth * laneValue(p.panLane),
                                 -1.0f, 1.0f);
    }

    /// @brief Derive everything the render tail and the FR-052 read surface need
    ///        from the (already resolved) applied log-domain values.
    ///
    /// Two exp2, one sin and one cos per peak per control step - 48
    /// transcendentals per 64 samples, none of them on the per-sample path.
    static void composeApplied(Peak& p) noexcept {
        p.appliedHz = std::exp2(p.appliedLog2Hz);
        p.appliedQ = std::exp2(p.appliedLog2Q);
        // FR-035 exempts gain from the ceiling: it is ramped per sample instead,
        // by the FR-041 gate and by the C-6 base-level ramp (S6.6).
        p.appliedGainDb = p.targetGainDb;
        // The bank is told ONLY the wander part. dbToGain(bankDb) *
        // dbToGain(levelDb) == dbToGain(appliedGainDb), so FR-017's composition
        // and getPeakCurrentGainDb's jointly-clamped total are unchanged.
        p.appliedBankGainDb = p.appliedGainDb - p.levelDb;
        p.appliedPan = p.targetPan;
        updatePanGains(p);
    }

    /// @brief One full control pass with NO slew limiting: applied := target for
    /// every peak, pushed into the bank in FR-014 order (plan S2.1 step 13).
    ///
    /// The bankApply is UNCONDITIONAL here, deliberately bypassing FR-015's
    /// change detection: prepare(), reset() and clearAudioState() all reach this
    /// through a bank whose configuration was just wiped
    /// (ResonatorBank::reset() leaves every slot at 440 Hz with
    /// enabled_[i] = false, resonator_bank.h:225-231), so a change-detected
    /// write that decided "nothing moved" would leave the engine silent.
    /// FR-004's "mandatorily re-applies the network's current configuration" is
    /// exactly this line.
    void snapControlState() noexcept {
        // FR-016's three pinned globals, RE-PINNED here rather than only in
        // prepare's step 4, because reset() and clearAudioState() both reach
        // this line through a ResonatorBank::reset() that restored the bank's
        // OWN defaults for all three (damping_, exciterMix_ and spectralTilt_
        // are written at resonator_bank.h:234-236, and the three smoothers are
        // reset to 0/0 at :220-222). Those defaults happen to coincide with
        // what FR-016 pins today, so the three stores are currently a no-op -
        // and that is exactly why they belong here: the day a shipped default
        // moves, the pin is what keeps a reset network sounding like a prepared
        // one instead of silently acquiring damping or an exciter mix. Cheap by
        // construction - three stores per reset, never on the render path.
        bank_.setDamping(0.0f);
        bank_.setExciterMix(0.0f);
        bank_.setSpectralTilt(0.0f);

        recomputeAnchors();
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            computeTargets(p);
            p.appliedLog2Hz = p.targetLog2Hz;
            p.appliedLog2Q = p.targetLog2Q;
            composeApplied(p);
            snapPerSampleFactors(p);  // C-16
            p.engineActive = (gateSteady(i) > 0.0f);
            bankApply(i, p);
            bankSetEnabled(i, p.engineActive);
        }
    }

    /// @brief One control step, run on the ABSOLUTE 64-sample grid.
    ///
    /// Order inside the per-peak loop is normative: targets, then the FR-035
    /// limiter (or C-8's unslewed tracking), then composeApplied, then FR-042's
    /// SLEEP EDGE, then the FR-014/FR-015 bank writes. The sleep edge sits
    /// BEFORE the writes so that the peak it just disabled falls through the
    /// engineActive early-out on the same step instead of being written twice.
    /// The WAKE edge is not here at all - it lives in refreshGates(), i.e. in
    /// the setter, for the reason documented there.
    void updateControl() noexcept {
        // ---- FR-036 / FR-037: the decimated lane advance --------------------
        // FIRST, before anything reads a lane - computeTargets() below is that
        // reader, and a step that mapped a lane value and only then advanced it
        // would report a control step's stale value through the whole FR-052
        // read surface.
        //
        // UNCONDITIONAL BY CONTRACT, in three separate ways that all have a
        // criterion behind them: a ZERO-DEPTH lane advances (so raising a depth
        // resumes the trajectory an always-on lane would have been on rather
        // than starting from wherever it was left); a DORMANT peak's lanes
        // advance (the roadmap's Dormancy rule, lines 490-491: "modulation lanes
        // keep running"); and setWanderEnabled(false) scales the DEPTHS via
        // wanderScale() without freezing the motion.
        //
        // The fixed kControlChunkSamples argument is what makes the decimation
        // sample-rate independent: between advances a lane HOLDS its output, so
        // it experiences 64/fs seconds of its own evolution per
        // laneDecimation_ * 64/fs seconds elapsed, and the realised correlation
        // time is tau * laneDecimation_ with fs cancelling. That is the second
        // half of setWanderRate's mapping - a BrownianDrift's tau saturates at
        // kTauMax = 30 s, and without this the whole [0.002, 0.0333] Hz
        // sub-range, this component's own 0.03 Hz default included, would be a
        // dead zone.
        //
        // The counter is REBASED, never restarted, by setWanderRate, so a rate
        // change mid-render neither forces an extra advance nor skips one.
        if (laneCounter_ == 0) {
            for (Peak& p : peaks_) {
                p.freqLane.processBlock(kControlChunkSamples);
                p.qLane.processBlock(kControlChunkSamples);
                p.gainLane.processBlock(kControlChunkSamples);
                p.panLane.processBlock(kControlChunkSamples);
            }
        }
        laneCounter_ = (laneCounter_ + std::size_t{1}) % laneDecimation_;

        // FR-024: the anchors are resolved only when a setter dirtied them, so a
        // network nobody is retuning pays one predicted branch per control step.
        if (anchorsDirty_) {
            recomputeAnchors();
        }

        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            Peak& p = peaks_[i];
            computeTargets(p);

            if (p.engineActive) {
                // FR-035. The ceiling is stated in OCTAVES PER CONTROL STEP,
                // i.e. a difference in log2 - which is the whole reason this
                // component works in log2: the limiter is two std::clamps and
                // the only transcendental left is composeApplied's exp2. Both
                // bounds are ordered by construction (freqSlewOct_ and qSlewOct_
                // are clamped to >= kMinSlewOctaves > 0 by setSlewCeilings).
                p.appliedLog2Hz = std::clamp(p.targetLog2Hz,
                                             p.appliedLog2Hz - freqSlewOct_,
                                             p.appliedLog2Hz + freqSlewOct_);
                p.appliedLog2Q = std::clamp(p.targetLog2Q,
                                            p.appliedLog2Q - qSlewOct_,
                                            p.appliedLog2Q + qSlewOct_);
            } else {
                // Spec correction C-8. A peak whose engine is inactive has a gate
                // of exactly 0, so there is nothing audible to protect and
                // nothing to slew toward: applied TRACKS target directly for the
                // WHOLE inactive interval, not only at the wake edge. That is
                // what makes FR-042's "written in full on that control step
                // REGARDLESS of how far it moved during the dormant interval"
                // true - a slew-limited dormant peak would still be fifty
                // control steps from target at the wake edge after a mid-
                // dormancy octave jump. SC-015 (f) is the criterion that
                // distinguishes the two designs.
                p.appliedLog2Hz = p.targetLog2Hz;
                p.appliedLog2Q = p.targetLog2Q;
            }

            composeApplied(p);

            // C-16, for EVERY peak - dormant, out-of-count and asleep included,
            // exactly like the ramps renderChunk advances unconditionally, and
            // for the same reason: the interpolated factors must be a function
            // of the absolute sample index and of nothing else.
            retargetPerSampleFactors(p);

            // ---- FR-042: the SLEEP EDGE, before the bank writes -------------
            // The exact == 0.0f comparison is correct BY CONSTRUCTION, not by
            // luck: gateSteady() returns a LITERAL 0.0f for a dormant peak, an
            // out-of-count peak and a vanishing wake alike (kWakeSilenceEpsilon
            // is what makes the third case true), and LinearRamp::process()
            // lands exactly on its target rather than approaching it
            // (smoother.h:379-383). Same exact test as the shipped sibling
            // (noise_organism.h:2194-2195).
            //
            // All three steps are load-bearing:
            //  1. bankReset(i) CLEARS THE STORED RING. Without it the biquad
            //     would hold whatever energy it had - RT60 = Q*ln1000/(pi*f) =
            //     5.5 s at Q = 100 and a 40 Hz anchor - and release it at full
            //     amplitude seconds after the 50 ms gate had finished
            //     attenuating it, into coefficients that had drifted for the
            //     whole dormant interval.
            //  2. The re-apply is MANDATORY: under Tier 0 the seam's bankReset
            //     is ResonatorBank::reset(), a CONFIGURATION WIPE leaving every
            //     slot at 440 Hz, default Q and enabled = false
            //     (resonator_bank.h:225-231). Tier 1's resetResonatorState is a
            //     pure state clear, so the write is merely redundant there and
            //     costs one setFrequency/setQ/setGain triple per sleep event
            //     against a 20-90 s event schedule. Keeping it unconditional is
            //     what makes the two tiers agree (S9.1).
            //  3. bankSetEnabled(i, false) plus engineActive = false is what
            //     stops bank.process being called for this peak at all. The
            //     bank's own disabled-slot skip (:487-488) is only half the
            //     saving - an entered-but-idle bank still runs its three global
            //     smoothers per sample (:474-476) - and the other half is
            //     SC-004 (c)'s >= 40 % arm.
            if (p.engineActive && p.gate.isComplete() && p.gate.getCurrentValue() == 0.0f) {
                bankReset(i);      // STATE CLEAR ONLY (S9.1)
                bankApply(i, p);   // MANDATORY re-apply, in FR-014 order
                bankSetEnabled(i, false);
                p.engineActive = false;
            }

            // ---- FR-014 / FR-015: the control-step bank writes --------------
            // THE SILENT-FAILURE SITE (plan S8). A dormant peak's values never
            // reach the bank at all - FR-042 defers that to a single exempt
            // write on the wake edge (refreshGates()'s bankApply), which is why
            // the whole block sits behind engineActive.
            if (!p.engineActive) {
                continue;
            }

            // Frequency and Q are ONE INDIVISIBLE WRITE PAIR whose change
            // detection is the OR of the two comparisons - the form FR-015
            // states it prefers, and the ONLY form that is correct here.
            //
            // ResonatorBank::setFrequency re-derives
            // qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])
            // (resonator_bank.h:333) off a decay table this component NEVER
            // writes: ResonatorBank::setDecay is called from nowhere in this
            // file, so decays_[i] stands at kDefaultDecayTime = 1.0 s forever.
            // Two failure modes follow, and both are SILENT - the audio still
            // sounds plausible and getPeakCurrentQ() still reports the intended
            // number, because FR-052 makes it this network's own applied value
            // rather than a read of the filter:
            //   * a frequency write AFTER a Q write discards the Q;
            //   * skipping an "unchanged" Q write after a frequency write
            //     leaves the filter at rt60ToQ(f, 1.0) = 0.4548 * f - Q ~ 18 at
            //     40 Hz for a peak configured at 12, and clamped to 100 above
            //     ~220 Hz - permanently, for every peak.
            // Only the realised-bandwidth criterion can fail on either.
            const bool freqOrQMoved =
                (p.appliedHz != p.lastWrittenHz) || (p.appliedQ != p.lastWrittenQ);
            if (freqOrQMoved) {
                bankSetFrequency(i, p.appliedHz);  // FIRST
                bankSetQ(i, p.appliedQ);           // ALWAYS immediately after
                p.lastWrittenHz = p.appliedHz;
                p.lastWrittenQ = p.appliedQ;
            }

            // NO per-control-step gain write (C-16). BOTH dB factors are now
            // per-sample: the base level on Peak::levelRamp (FR-017/C-6) and the
            // wander part on Peak::wanderGain, retargeted above. The bank's
            // gains_[i] stays at the unity bankApply pinned, so there is nothing
            // here to change-detect - the FR-015 machinery that remains is the
            // frequency/Q pair above, which is where it was ever needed
            // (setFrequency re-derives Q; setGain re-derives nothing).
        }
    }

    /// @brief Render `n` samples inside one control chunk (FR-044).
    ///
    /// Per-sample with LOCAL DRY CAPTURE, which is what makes every aliasing
    /// case FR-003 admits safe with no scratch buffer: index `s` of both inputs
    /// is read before either output is written, so inL == outL / inR == outR is
    /// safe, and so is the cross-aliased inL == outR / inR == outL (writing
    /// outR[s] clobbers inL[s], but that value was already consumed this
    /// iteration and is never read again). This is also why there is no heap
    /// term (C-1).
    ///
    /// Exact-zero properties this composition guarantees, each with a criterion
    /// behind it: a closed gate makes a peak's contribution EXACTLY 0.0f by
    /// multiplication, never through a dB path whose -60 dB floor is 1.0e-3 of
    /// full scale; every peak dormant at mix = 1 gives digital silence, because
    /// OnePoleSmoother::process snaps current_ to target_ inside
    /// kCompletionThreshold (smoother.h:199-202) so m reaches EXACTLY 1.0f; and
    /// mix = 0 is a bit-exact stereo pass-through for the same reason.
    void renderChunk(const float* inL, const float* inR,
                     float* outL, float* outR, std::size_t n) noexcept {
        // FR-018's chunk-local engagement tally. Kept out of the member for the
        // length of the loop so the render path writes the shared counter once
        // per chunk rather than twice per sample.
        std::uint32_t engagements = 0;

        for (std::size_t s = 0; s < n; ++s) {
            // ---- (0) sanitise, per channel INDEPENDENTLY (FR-009) ----------
            const float dryL = detail::isFinite(inL[s]) ? inL[s] : 0.0f;
            const float dryR = detail::isFinite(inR[s]) ? inR[s] : 0.0f;

            // ---- (1) the mono engine input (FR-011) -------------------------
            const float x = 0.5f * (dryL + dryR);

            // Tier 1's one engine call per sample: it advances the bank's three
            // global smoothers exactly once and writes every slot's individual
            // contribution into perPeakOut_, which bankProcess(i, x) then reads.
            // A disabled slot writes an exact 0.0f, so a sleeping peak never
            // returns stale data even before its gate closes. (Under Tier 0
            // this line does not exist and bankProcess calls banks_[i].process
            // itself - the ONLY difference between the two tiers.)
            bank_.processIndividual(x, perPeakOut_.data());

            // ---- (2) gated mono peak, equal-power panned into a stereo sum --
            float wetL = 0.0f;
            float wetR = 0.0f;
            for (std::size_t i = 0; i < kMaxPeaks; ++i) {
                Peak& p = peaks_[i];
                // ALWAYS advanced, for EVERY peak - dormant and out-of-count
                // included, and BEFORE the engineActive early-out. The ramps
                // must be a pure function of the sample count however the caller
                // partitions its blocks, which is exactly what SC-010's
                // irregular partition asserts; a LinearRamp already at its
                // target early-outs (smoother.h:372-374), so the cost of doing
                // it unconditionally is one predicted branch. This is the
                // noise_organism.h:1819-1821 rule.
                const float gate = p.gate.process();
                const float levelGain = p.levelRamp.process();  // FR-017 base level, C-6
                // C-16's interpolated multiplies, advanced on the SAME
                // unconditional rule as the ramps above and for the same reason.
                p.wanderGain += p.wanderGainInc;
                p.panCurL += p.panIncL;
                p.panCurR += p.panIncR;
                if (!p.engineActive) {
                    continue;  // FR-042: the peak contributes exactly nothing
                }
                // FR-044 step 1. BOTH dB factors are per-sample now: the base
                // level on levelRamp (S6.6, C-6) and the wander part on the
                // wanderGain interpolator (C-16), with the bank told nothing
                // about the peak's dB at all. levelGain * wanderGain settles at
                // dbToGain(levelDb) * dbToGain(appliedGainDb - levelDb) ==
                // dbToGain(appliedGainDb) exactly, so FR-017's composition and
                // getPeakCurrentGainDb's meaning are unchanged.
                const float y = bankProcess(i, x) * gate * levelGain * p.wanderGain;
                wetL += y * p.panCurL;
                wetR += y * p.panCurR;
            }

            // ---- (2b) FR-018's NON-FINITE GUARD, the last line before the ---
            // wet sum leaves the engine (spec correction C-15).
            //
            // The FR-018 clamp below bounds +/-inf (inf > kOutputClamp is true)
            // but CANNOT bound a NaN: every comparison against NaN is false, so
            // a NaN walks through clampCount untouched and out of the component.
            // And a NaN is reachable WITHOUT a non-finite input, through the
            // public control surface alone: a resonator whose centre frequency
            // is retuned fast and far is a PARAMETRICALLY PUMPED oscillator, and
            // SC-002 (c)'s own injection - setSlewCeilings(24, 24) plus a
            // +/-1-octave setPeakAnchorHz jump on every 64-sample control chunk,
            // i.e. a 750 Hz square modulation of twelve resonances that sit at
            // 110-880 Hz - is the textbook 2f case. Measured on the shipped
            // build before this guard: the wet sum passes 3.4e38 and goes
            // non-finite 1.83 s into that render and STAYS non-finite for the
            // remaining 97 % of it, at every wet trim (the trim is a post-sum
            // multiply, so it moves nothing).
            //
            // Two checks per sample on the fast path, and the repair loop only
            // on the sample that actually diverged. It is self-healing rather
            // than latching: clearing the offending slot's ring (a state clear
            // only - configuration, gate and lanes are untouched) lets the peak
            // resume, where zeroing the output alone would leave a NaN parked in
            // the biquad and that peak silent for the rest of the session.
            if (!detail::isFinite(wetL) || !detail::isFinite(wetR)) {
                healDivergedPeaks();
                wetL = 0.0f;
                wetR = 0.0f;
                ++engagements;
            }

            // ---- (3)+(4) normalise and trim: ONE scalar, both channels ------
            // FR-019's 1/sqrt(numPeaks) and FR-045's static trim are folded into
            // this single LinearRamp (correction C-4). N is numPeaks, NEVER the
            // awake count: an awake reading computes 1/sqrt(0) = inf when every
            // peak sleeps, and inf * 0.0f = NaN.
            const float scale = wetScaleRamp_.process();

            // ---- (5) crossfade against the stereo dry path (FR-043) ---------
            const float m = mixSmoother_.process();
            const float l = (1.0f - m) * dryL + m * (wetL * scale);
            const float r = (1.0f - m) * dryR + m * (wetR * scale);

            // ---- (6) final clamp, per channel, ONE shared counter (FR-018) --
            // The counter the two calls bump is the LOCAL chunk tally declared
            // above, folded into clampEngagements_ once below. Both channels
            // feed the SAME tally - FR-018 counts engagements, not engaged
            // samples - and the flush follows the clamp on each, per channel
            // independently.
            outL[s] = detail::flushDenormal(clampCount(l, engagements));
            outR[s] = detail::flushDenormal(clampCount(r, engagements));
        }

        // ---- FR-018's fold: SATURATING, never wrapping ----------------------
        // The noise_organism.h:2611-2616 idiom. The count is a diagnostic, and
        // every other criterion in this phase reads it expecting zero or an
        // unchanged value; a wrap would let a pathological render - the one case
        // where the number actually matters - report ZERO engagements. The
        // local tally cannot itself overflow: a chunk is at most
        // kControlChunkSamples samples, i.e. at most 2 * 64 engagements.
        if (engagements > 0) {
            const std::uint32_t headroom = kMaxClampCount - clampEngagements_;
            clampEngagements_ += std::min(engagements, headroom);
        }
    }

    /// @brief Clear the ring of every peak whose engine output has gone
    ///        non-finite (spec correction C-15). Called ONLY from the sample
    ///        that diverged, never on the fast path.
    ///
    /// RT-safe: bankReset is ResonatorBank::resetResonatorState
    /// (resonator_bank.h:613), a Biquad::reset() and nothing else - no
    /// allocation, no configuration wipe, no lane rewind. The peak keeps its
    /// frequency, Q, gain, gate, level ramp and lane positions and simply starts
    /// ringing again from silence.
    void healDivergedPeaks() noexcept {
        for (std::size_t i = 0; i < kMaxPeaks; ++i) {
            if (!detail::isFinite(perPeakOut_[i])) {
                bankReset(i);
                perPeakOut_[i] = 0.0f;
            }
        }
    }

    /// @brief FR-018's output clamp. Every engagement bumps the caller's LOCAL
    ///        chunk tally, which renderChunk folds into clampEngagements_ with
    ///        saturation at the end of the chunk.
    /// @param v          The sample to bound to +/- kOutputClamp.
    /// @param engagements The chunk-local tally, incremented on engagement.
    [[nodiscard]] static float clampCount(float v, std::uint32_t& engagements) noexcept {
        if (v > kOutputClamp) {
            ++engagements;
            return kOutputClamp;
        }
        if (v < -kOutputClamp) {
            ++engagements;
            return -kOutputClamp;
        }
        return v;
    }
};

} // namespace DSP
} // namespace Krate
