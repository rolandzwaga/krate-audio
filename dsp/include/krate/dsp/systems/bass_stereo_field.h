// ==============================================================================
// Layer 3: System - Profundum Phase 7 (specs/profundum-phase7-stereo-field)
// ==============================================================================
// BassStereoField: the Profundum stereo field (image modes, MOTION, mono-safe stage).
//   Carries: FR-001 - FR-004 (interface, Controls, validation, voice-count independence),
//            FR-010 - FR-019 (generation-domain width law), FR-020 - FR-028 (post-sum safety stage),
//            FR-030 - FR-036 (MOTION), FR-040 - FR-044 (WIDE offsets), FR-050 - FR-054 (image modes),
//            FR-072, FR-074; SC-001 - SC-021.
//   Spec:  specs/profundum-phase7-stereo-field/spec.md
//   Plan:  specs/profundum-phase7-stereo-field/plan.md (S2 - S8; C-3 inline M/S, C-4 reset snaps the
//          crossover through CrossoverLR4::prepare, C-5 warped raised cosine, C-6 slew 0.003, C-7
//          explicit advanceControl(), C-8 partial-count argument, C-9 octave-folded synced tempo,
//          C-10 offset signs {+K, -K}, C-11 g_side = floor * (1 - G), C-15 SOLID by slew, C-18 Efficient)
//   Tests: dsp/tests/unit/systems/bass_stereo_field_*_test.cpp (tag [stereo-field]);
//          probe: dsp/tests/unit/systems/bass_stereo_field_probe.h (tasks.md deviation 5)
//
// Two halves in one class (plan S2):
//   - a const per-voice law (computePanVector) whose per-voice memory is the caller's VoiceState;
//   - a global, stateful control-rate state (advanceControl, once per 32-sample interval, BEFORE that
//     interval's voice queries and processBlock; plan S1 C-7) and the post-sum safety stage.
//
// T003 (this skeleton): the full public API, controls / validation / tempo / seed, prepare / reset,
// advanceControl (S4.1 law cache, S5 MOTION, S7.1 stage targets) and the observers.
// T004: computePanVector / widthCurve (plan S4.2 law, S4.3 slew and handoff count; C-5, C-6, C-15).
// T005: wideOffsets / wideCeiling (plan S6, C-10).
// T006: processBlock, the post-sum safety stage (plan S7.3; C-1, C-2, C-3, C-18, R-11).
//
// Includes L0 - L2 plus the L3 peer bass_character_stage.h for Algorithm / kMaxChannelOffset only.
// Never profundum_core.h (C-8) and never midside_processor.h (C-3: its encode / decode formulas,
// midside_processor.h:200-201 and 225-226, are written inline by the stage).
// RT: heap-free, fixed-size members, every audio-thread method noexcept, no std::function, no SIMD.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/brownian_drift.h>
#include <krate/dsp/processors/crossover_filter.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/systems/bass_character_stage.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate::DSP {

namespace detail {
struct BassStereoFieldProbe;
}  // namespace detail

/// The Profundum stereo field (FR-001): per-voice generation-domain pan law + global safety stage.
class BassStereoField {
public:
    enum class ImageMode : std::uint8_t { Solid, Spread, Wide };   // FR-002, nested (ODR row)
    enum class MotionSource : std::uint8_t { Drift, Cycle };       // FR-031, nested

    // ---- constants (plan S3; S9 lists seeds and harvests) ----
    static constexpr std::size_t kControlInterval = 32;          // == ProfundumCore / MassDynamics (static_assert in tests)
    static constexpr int kDefaultPartials = 64;                  // == ProfundumCore::kDefaultPartials
    static constexpr float kMinCrossoverHz = 60.0f;              // Q5
    static constexpr float kMaxCrossoverHz = 250.0f;
    static constexpr float kDefaultCrossoverHz = 120.0f;
    static constexpr float kStageCrossoverMultiple = 2.0f;       // Q3, FR-024
    static constexpr float kUpperKneeOctaves = 2.0f;             // C-5
    static constexpr float kLowerKneeMaxOctaves = 0.25f;         // C-5
    static constexpr float kMaxCurveExponent = 4.0f;             // C-5: gamma = 4^curve
    static constexpr float kMaxPanStepPerInterval = 0.003f;      // C-6
    static constexpr float kMotionExcursion = 0.5f;              // kappa, Q7
    static constexpr float kCycleMinHz = 0.05f;                  // C-9
    static constexpr float kCycleMaxHz = 1.0f;                   // SC-008 bar
    static constexpr float kDriftTauMaxSec = 30.0f;              // SC-008 bar
    static constexpr float kDriftTauMinSec = 0.5f;               // SC-008 bar
    static constexpr float kDefaultTempoBpm = 120.0f;
    // HARVEST-BEGIN wide-floor
    // harvested 2026-10-18 from evidence/harvest-wide-floor.txt (plan S9.1)
    static constexpr float kSideFloorGain = 0.035f;
    static constexpr float kWideOffsetScale = 1.0f;
    // HARVEST-END wide-floor
    static constexpr float kWideCeilingRectifyFracture = 0.10f;  // Phase 5 handoff
    static constexpr float kSideGainSmoothMs = 10.0f;            // FR-027 (MidSideProcessor's 10 ms)
    static constexpr float kModeCrossfadeMs = 10.0f;             // FR-052, D-6
    static constexpr float kWideEngageStep = kMaxPanStepPerInterval;  // G7-2: offsets engage over the FR-015 slew length
    static constexpr float kCrossoverSmoothMs = 20.0f;           // law f_c smoothing (control rate)
    static constexpr float kStageCrossoverSmoothMs = kMinSmoothingTimeMs;   // 0.1 ms (FR-024, S7.2)
    static constexpr float kDefaultF0Hz = 55.0f;
    static constexpr std::uint32_t kDefaultDriftSeed = BrownianDrift::kDefaultDriftSeed;
    static constexpr bool kCycleSyncFold = true;  // plan S1 C-9 (A), ratified 2026-10-18

    struct Controls {                                            // FR-002: plain aggregate
        ImageMode mode = ImageMode::Spread;
        float spread = 0.5f;                       // [0, 1]
        float gravity = 0.5f;                      // [0, 1]
        float crossoverHz = kDefaultCrossoverHz;   // [60, 250]
        float curve = 0.5f;                        // [0, 1], continuous (Q5)
        float motionDepth = 0.0f;                  // [0, 1]
        float motionRate = 0.3f;                   // [0, 1] (C-9)
        MotionSource motionSource = MotionSource::Drift;
        bool cycleSync = false;
        NoteValue cycleNote = NoteValue::Whole;
        NoteModifier cycleModifier = NoteModifier::None;
    };

    struct VoiceState {                                          // FR-001: one per voice, caller-owned
        std::array<float, kMaxPartials> previous{};              // last emitted vector (FR-015)
        float lastF0Hz = kDefaultF0Hz;                           // used when a query's f0 is not valid
        float lastCrossoverHz = 0.0f;                            // f_c,s the last query used (SC-009(c))
        std::uint64_t handoffCount = 0;                          // FR-016 / SC-013(c)
        bool primed = false;                                     // false -> next query lands on target (Reset exemption)
        void reset() noexcept {
            previous.fill(0.0f);
            lastF0Hz = kDefaultF0Hz;
            lastCrossoverHz = 0.0f;
            primed = false;
        }
        // handoffCount survives reset(): it is a diagnostic counter (the caller zeroes it).
    };

    struct ChannelOffsets {                                      // FR-040
        float amount = 0.0f;
        float tone = 0.0f;
    };

    class MotionOutput final : public ModulationSource {         // FR-034
    public:
        [[nodiscard]] float getCurrentValue() const noexcept override { return value_; }
        [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }

    private:
        friend class BassStereoField;
        float value_ = 0.0f;
    };

    BassStereoField() noexcept {
        for (std::size_t k = 0; k < kMaxPartials; ++k) {
            log2Harmonic_[k] = std::log2(static_cast<float>(k + 1));
        }
        crossover_.setSmoothingTime(kStageCrossoverSmoothMs);
        crossover_.setTrackingMode(TrackingMode::Efficient);   // C-18
        crossover_.setCrossoverFrequency(kStageCrossoverMultiple * fcS_);
        sideGainSm_.snapTo(sideGainTarget());
        modeRamp_.snapTo(applied_.mode == ImageMode::Solid ? 0.0f : 1.0f);
        wideEngage_ = (applied_.mode == ImageMode::Wide) ? 1.0f : 0.0f;
        sideIdle_ = applied_.mode == ImageMode::Solid;
        applyMotionConfig();
        updateLawCache();
    }
    BassStereoField(const BassStereoField&) = delete;            // CrossoverLR4 / LFO are non-copyable
    BassStereoField& operator=(const BassStereoField&) = delete;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Not RT-safe (FR-001). maxBlockSize is unused: the stage needs no scratch (plan S7.3).
    void prepare(double sampleRate, [[maybe_unused]] std::size_t maxBlockSize) noexcept {
        // S3 step 1
        sampleRate_ = sampleRate;
        applied_ = pending_;
        prepared_ = true;
        const double controlRate = sampleRate_ / static_cast<double>(kControlInterval);
        // S3 step 2: the law crossover smoother runs on the control grid (OnePoleSmoother takes a float rate)
        fcSmoother_.configure(kCrossoverSmoothMs, static_cast<float>(controlRate));
        fcSmoother_.snapTo(applied_.crossoverHz);
        fcS_ = applied_.crossoverHz;
        // S3 step 3: the stage crossover (C-18 Efficient tracking, 0.1 ms smoothing)
        crossover_.setSmoothingTime(kStageCrossoverSmoothMs);
        crossover_.setTrackingMode(TrackingMode::Efficient);
        crossover_.setCrossoverFrequency(kStageCrossoverMultiple * fcS_);
        crossover_.prepare(sampleRate_);
        // S3 step 4: Drift
        drift_.prepare(sampleRate_);
        drift_.setSeed(driftSeed_);
        drift_.setDepth(1.0f);
        drift_.setMean(0.0f);
        // S3 step 5: Cycle, clocked once per interval (C-9 "LFO clocking")
        lfo_.prepare(controlRate);
        lfo_.setWaveform(Waveform::Sine);
        motionConfigured_ = false;   // force every MOTION setter (smoothness, frequency, sync)
        applyMotionConfig();
        // S3 step 6
        sideGainSm_.configure(kSideGainSmoothMs, static_cast<float>(sampleRate_));
        modeRamp_.configure(kModeCrossfadeMs, static_cast<float>(sampleRate_));
        // S3 step 7
        reset();
    }

    /// RT-safe (FR-028; plan S1 C-4). Never touches sideFloorGain_ / wideOffsetScale_ (deviation 5).
    void reset() noexcept {
        // S3 step 1
        applied_ = pending_;
        // S3 step 2 (C-4): CrossoverLR4::reset() leaves its smoother where it was and the class has no
        // public snap, so prepare() is the snap. Only once prepared: before that the crossover stays
        // unprepared and its process() returns {0, 0} (crossover_filter.h:290-292).
        fcSmoother_.snapTo(applied_.crossoverHz);
        fcS_ = applied_.crossoverHz;
        crossover_.setCrossoverFrequency(kStageCrossoverMultiple * fcS_);
        if (prepared_) {
            crossover_.prepare(sampleRate_);
        }
        // S3 step 3 (FR-033): drift_.reset() re-seeds from the configured seed (brownian_drift.h:133)
        applyMotionConfig();
        drift_.reset();
        lfo_.reset();
        // S3 step 4
        const bool solid = applied_.mode == ImageMode::Solid;
        sideGainSm_.snapTo(sideGainTarget());
        modeRamp_.snapTo(solid ? 0.0f : 1.0f);
        wideEngage_ = (applied_.mode == ImageMode::Wide) ? 1.0f : 0.0f;   // G7-2
        // S3 step 5
        sideIdle_ = solid;
        cycleValue_ = 0.0f;
        m_ = 0.0f;
        motionOutput_.value_ = 0.0f;
        // S3 step 6
        updateLawCache();
    }

    // =========================================================================
    // Controls
    // =========================================================================

    /// RT-safe. Clamps; a non-finite float or an undeclared enum keeps the previous value; the result is
    /// latched (pending) until the next advanceControl() (FR-074) or reset().
    void setControls(const Controls& c) noexcept {
        Controls n = pending_;
        if (isDeclared(c.mode)) {
            n.mode = c.mode;
        }
        n.spread = sanitize(c.spread, 0.0f, 1.0f, n.spread);
        n.gravity = sanitize(c.gravity, 0.0f, 1.0f, n.gravity);
        n.crossoverHz = sanitize(c.crossoverHz, kMinCrossoverHz, kMaxCrossoverHz, n.crossoverHz);
        n.curve = sanitize(c.curve, 0.0f, 1.0f, n.curve);
        n.motionDepth = sanitize(c.motionDepth, 0.0f, 1.0f, n.motionDepth);
        n.motionRate = sanitize(c.motionRate, 0.0f, 1.0f, n.motionRate);
        if (isDeclared(c.motionSource)) {
            n.motionSource = c.motionSource;
        }
        n.cycleSync = c.cycleSync;
        if (isDeclared(c.cycleNote)) {
            n.cycleNote = c.cycleNote;
        }
        if (isDeclared(c.cycleModifier)) {
            n.cycleModifier = c.cycleModifier;
        }
        pending_ = n;
    }

    /// The latched (pending) set.
    [[nodiscard]] const Controls& controls() const noexcept { return pending_; }

    /// FR-003: true iff every float is finite and inside its closed range and every enum is declared.
    /// The ranges are boxes, so a linear interpolation of two valid sets is valid (SC-021).
    [[nodiscard]] static bool isValidControls(const Controls& c) noexcept {
        return isDeclared(c.mode) && isDeclared(c.motionSource) && isDeclared(c.cycleNote) &&
               isDeclared(c.cycleModifier) && inClosedRange(c.spread, 0.0f, 1.0f) &&
               inClosedRange(c.gravity, 0.0f, 1.0f) &&
               inClosedRange(c.crossoverHz, kMinCrossoverHz, kMaxCrossoverHz) &&
               inClosedRange(c.curve, 0.0f, 1.0f) && inClosedRange(c.motionDepth, 0.0f, 1.0f) &&
               inClosedRange(c.motionRate, 0.0f, 1.0f);
    }

    /// FR-036: non-finite or <= 0 keeps the previous tempo; else clamp to [20, 300] BPM. Takes effect at
    /// the next advanceControl().
    void setTempoBpm(float bpm) noexcept {
        if (!detail::isFinite(bpm) || !(bpm > 0.0f)) {
            return;
        }
        tempoBpm_ = clampRange(bpm, static_cast<float>(kMinTempoSyncBPM), static_cast<float>(kMaxTempoSyncBPM));
    }

    /// FR-033: settable Drift seed, restored by reset().
    void setDriftSeed(std::uint32_t seed) noexcept {
        driftSeed_ = seed;
        drift_.setSeed(seed);
    }

    // =========================================================================
    // Control rate (plan S1 C-7)
    // =========================================================================

    /// Advance the global control-rate state by one 32-sample interval. Call at every absolute sample
    /// index that is a multiple of kControlInterval, before that interval's law queries and processBlock.
    void advanceControl() noexcept {
        applied_ = pending_;

        // G7-2 (FR-040, FR-043, FR-052): the WIDE offsets engage and release over the FR-015 slew length, so a
        // SPREAD <-> WIDE step is as slow as a pan slew and the Mass dynamics' shaper sees no attack. Lands exactly.
        {
            const float target = (applied_.mode == ImageMode::Wide) ? 1.0f : 0.0f;
            const float d = target - wideEngage_;
            wideEngage_ = (std::abs(d) <= kWideEngageStep) ? target : wideEngage_ + std::copysign(kWideEngageStep, d);
        }

        // S4.1 / S7.2: the law crossover f_c,s, smoothed on the control grid.
        if (fcSmoother_.getTarget() != applied_.crossoverHz) {
            fcSmoother_.setTarget(applied_.crossoverHz);
        }
        fcS_ = fcSmoother_.process();

        // S5: both sources advance every interval, whichever is selected (C-9).
        applyMotionConfig();
        drift_.processBlock(kControlInterval);
        const float driftValue = drift_.getCurrentValue();
        cycleValue_ = lfo_.process();
        const float raw = (applied_.motionSource == MotionSource::Drift) ? driftValue : cycleValue_;
        m_ = detail::isFinite(raw) ? std::clamp(raw, -1.0f, 1.0f) : 0.0f;
        motionOutput_.value_ = m_;

        // S4.1: the per-interval law cache (reads m_).
        updateLawCache();

        // S7.1: the stage targets.
        crossover_.setCrossoverFrequency(kStageCrossoverMultiple * fcS_);
        const float gTarget = sideGainTarget();
        if (sideGainSm_.getTarget() != gTarget) {
            sideGainSm_.setTarget(gTarget);
        }
        const bool solid = applied_.mode == ImageMode::Solid;
        const float kTarget = solid ? 0.0f : 1.0f;
        if (modeRamp_.getTarget() != kTarget) {
            // LinearRamp::setTarget recomputes the increment from the remaining distance: set it only on a
            // change, or the ramp stretches into a decaying curve (S7.1).
            modeRamp_.setTarget(kTarget);
        }
        if (!solid) {
            sideIdle_ = false;   // leaving SOLID wakes the side path (zero state, faded in by the ramp)
        }
    }

    // =========================================================================
    // Per-voice law (FR-010 - FR-019, FR-030)
    // =========================================================================

    /// plan S1 C-8 signature; plan S4.2 (law) and S4.3 (slew, handoff count). Const on the field: the
    /// only memory is the caller's VoiceState. Partial k sits at (k + 1) * f0; out[0] (h1, FR-013) and
    /// every entry k >= N are +0. A non-finite or non-positive f0 uses state.lastF0Hz; a valid one is
    /// stored. An unprimed state lands on the target (FR-015 Reset exemption); a primed one moves each
    /// entry by at most kMaxPanStepPerInterval and lands exactly once within one step (C-6, C-15).
    void computePanVector(VoiceState& state, float f0Hz, std::array<float, kMaxPartials>& out,
                          int numPartials = kDefaultPartials) const noexcept {
        const int n = std::clamp(numPartials, 0, static_cast<int>(kMaxPartials));
        if (detail::isFinite(f0Hz) && f0Hz > 0.0f) {
            state.lastF0Hz = f0Hz;
        }
        const float f0 = state.lastF0Hz;
        const float log2F0 = std::log2(f0);   // one log per query (S4.2)
        const bool primed = state.primed;

        out[0] = 0.0f;   // FR-013: h1 is never computed
        for (int k = 1; k < n; ++k) {
            const auto idx = static_cast<std::size_t>(k);
            const float fk = static_cast<float>(k + 1) * f0;
            const float c = lawCurve(fk, log2F0 + log2Harmonic_[idx]);
            const float mag = magnitudeScale_ * c;
            // FR-014: k even = odd harmonic = left (-), bank:1155-1156. Rule 4: zero is +0, never -0.
            const float target = (mag > 0.0f) ? ((k % 2 == 0) ? -mag : mag) : 0.0f;
            float v = target;
            if (primed) {
                const float prev = state.previous[idx];
                const float d = target - prev;
                if (!(std::abs(d) <= kMaxPanStepPerInterval)) {
                    v = prev + std::copysign(kMaxPanStepPerInterval, d);
                }
            }
            out[idx] = v;
        }
        for (std::size_t k = static_cast<std::size_t>(std::max(n, 1)); k < kMaxPartials; ++k) {
            out[k] = 0.0f;   // FR-010: entries k >= N are 0
        }

        // FR-016: bit compare (never ==, which equates -0 / +0, plan S11).
        bool changed = false;
        for (std::size_t k = 0; k < kMaxPartials; ++k) {
            if (std::bit_cast<std::uint32_t>(out[k]) != std::bit_cast<std::uint32_t>(state.previous[k])) {
                changed = true;
                break;
            }
        }
        if (changed) {
            ++state.handoffCount;
        }
        state.previous = out;
        state.primed = true;
        state.lastCrossoverHz = fcS_;
    }

    /// c(f) of the current interval (plan S1 C-5; tests, Phase 12). 0 for a non-finite or non-positive f.
    [[nodiscard]] float widthCurve(float frequencyHz) const noexcept {
        if (!detail::isFinite(frequencyHz) || !(frequencyHz > 0.0f)) {
            return 0.0f;
        }
        return lawCurve(frequencyHz, std::log2(frequencyHz));
    }

    // =========================================================================
    // WIDE (FR-040 - FR-044)
    // =========================================================================

    /// FR-040 / FR-041 / FR-043 (plan S6, C-10, G7-2): {+K, -K} with K = wideCeiling(a) * Spread * K_wide *
    /// wideEngage_ (the 0..1 WIDE engagement scalar advanceControl slews over the FR-015 slew length), else
    /// {+0, +0} while the scalar or Spread is 0. const and pure: reads only applied state, so the offsets change
    /// on the control grid (FR-074). K <= ceiling by construction (Spread <= 1, K_wide <= 1, scalar <= 1).
    [[nodiscard]] ChannelOffsets wideOffsets(BassCharacterStage::Algorithm a) const noexcept {
        if (!(wideEngage_ > 0.0f) || !(applied_.spread > 0.0f)) {
            return ChannelOffsets{.amount = 0.0f, .tone = 0.0f};
        }
        const float k = wideCeiling(a) * applied_.spread * wideOffsetScale_ * wideEngage_;
        return ChannelOffsets{.amount = k, .tone = -k};
    }

    /// FR-041 (Phase 5 handoff): 0.10 for Rectify and Fracture, kMaxChannelOffset (0.15) otherwise.
    [[nodiscard]] static float wideCeiling(BassCharacterStage::Algorithm a) noexcept {
        return (a == BassCharacterStage::Algorithm::Rectify || a == BassCharacterStage::Algorithm::Fracture)
                   ? kWideCeilingRectifyFracture
                   : BassCharacterStage::kMaxChannelOffset;
    }

    // =========================================================================
    // Safety stage (FR-020 - FR-028)
    // =========================================================================

    /// plan S7.3: in place, one pass per sample, no scratch, zero latency (topology (a), FR-022). M is
    /// never filtered; the split is on S only. A null buffer or 0 samples is a no-op (FR-001). A
    /// non-finite input sample is encoded as 0 and its output sample is written as exactly 0.0f (FR-072).
    /// `right == left` is one channel and no side: both smoothers still advance (their state matches a
    /// stereo call) and the sample passes through the non-finite guard only.
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        if (left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }
        if (right == left) {
            for (std::size_t i = 0; i < numSamples; ++i) {
                const float k = modeRamp_.process();
                static_cast<void>(sideGainSm_.process());
                updateSideIdle(k);
                const float x = left[i];
                left[i] = detail::isFinite(x) ? x : 0.0f;
            }
            return;
        }
        for (std::size_t i = 0; i < numSamples; ++i) {
            const float lIn = left[i];
            const float rIn = right[i];
            const bool badL = !detail::isFinite(lIn);   // FR-072 mask
            const bool badR = !detail::isFinite(rIn);
            const float l = badL ? 0.0f : lIn;
            const float r = badR ? 0.0f : rIn;
            // plan S1 C-3: MidSideProcessor's encode, midside_processor.h:200-201
            const float mid = (l + r) * 0.5f;
            const float side = (l - r) * 0.5f;
            const float k = modeRamp_.process();
            const float g = sideGainSm_.process();
            float sp = 0.0f;
            if (!sideIdle_) {
                const CrossoverLR4Outputs bands = crossover_.process(side);
                sp = k * (g * bands.low + bands.high);
            }
            // plan S1 C-3: MidSideProcessor's decode, midside_processor.h:225-226. A zero S' (either sign)
            // returns M itself to both channels, so L == R == M bit for bit wherever S' == 0 (plan S1 C-1
            // (iii), C-2) whatever the sign of a zero M, and whatever the FP mode does with M + 0.
            float outL = mid;
            float outR = mid;
            if (sp != 0.0f) {
                outL = mid + sp;
                outR = mid - sp;
            }
            updateSideIdle(k);
            left[i] = badL ? 0.0f : outL;
            right[i] = badR ? 0.0f : outR;
        }
    }

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    /// plan S1 C-11: g_side(G) = kSideFloorGain * (1 - G); g(1) is exactly +0.
    [[nodiscard]] static constexpr float sideGainForGravity(float gravity) noexcept {
        return kSideFloorGain * (1.0f - gravity);
    }

    // =========================================================================
    // Observers
    // =========================================================================

    [[nodiscard]] float smoothedCrossoverHz() const noexcept { return fcS_; }                          // f_c,s (FR-024)
    [[nodiscard]] float stageCrossoverHz() const noexcept { return crossover_.getCrossoverFrequency(); }  // the handed target
    [[nodiscard]] float motionValue() const noexcept { return m_; }                                    // m, [-1, 1]
    [[nodiscard]] const ModulationSource& motionSource() const noexcept { return motionOutput_; }
    [[nodiscard]] float modeFade() const noexcept { return modeRamp_.getCurrentValue(); }              // k
    [[nodiscard]] float currentSideGain() const noexcept { return sideGainSm_.getCurrentValue(); }     // g_side, smoothed
    [[nodiscard]] bool sideIdle() const noexcept { return sideIdle_; }                                 // SOLID settled
    [[nodiscard]] float driftTauSeconds() const noexcept { return driftTau_; }                         // tau configured (SC-008)
    [[nodiscard]] float cycleFrequencyHz() const noexcept { return cycleHz_; }                         // after the octave fold

private:
    friend struct detail::BassStereoFieldProbe;

    // ---- helpers ----

    [[nodiscard]] static constexpr bool isDeclared(ImageMode m) noexcept {
        return static_cast<std::uint8_t>(m) <= static_cast<std::uint8_t>(ImageMode::Wide);
    }
    [[nodiscard]] static constexpr bool isDeclared(MotionSource s) noexcept {
        return static_cast<std::uint8_t>(s) <= static_cast<std::uint8_t>(MotionSource::Cycle);
    }
    [[nodiscard]] static constexpr bool isDeclared(NoteValue n) noexcept {
        return static_cast<std::uint8_t>(n) <= static_cast<std::uint8_t>(NoteValue::SixtyFourth);
    }
    [[nodiscard]] static constexpr bool isDeclared(NoteModifier m) noexcept {
        return static_cast<std::uint8_t>(m) <= static_cast<std::uint8_t>(NoteModifier::Triplet);
    }

    /// Clamp a finite value. `v <= lo` also catches -0 and (under DAZ) negative denormals, which then
    /// leave as lo (+0 for the unit ranges: rule 4, zero is +0).
    [[nodiscard]] static float clampRange(float v, float lo, float hi) noexcept {
        if (v <= lo) {
            return lo;
        }
        if (v >= hi) {
            return hi;
        }
        return v;
    }

    [[nodiscard]] static float sanitize(float v, float lo, float hi, float previous) noexcept {
        return detail::isFinite(v) ? clampRange(v, lo, hi) : previous;
    }

    /// Every range has lo >= 0, so any negative non-zero bit pattern is outside. The bit test keeps a
    /// negative denormal outside under DAZ, where it would compare equal to 0.
    [[nodiscard]] static bool inClosedRange(float v, float lo, float hi) noexcept {
        if (!detail::isFinite(v)) {
            return false;
        }
        const auto bits = std::bit_cast<std::uint32_t>(v);
        if ((bits & 0x80000000u) != 0u && (bits & 0x7FFFFFFFu) != 0u) {
            return false;
        }
        return v >= lo && v <= hi;
    }

    /// plan S7.3 / FR-052: once the mode ramp has landed on SOLID's 0, the side path idles and its
    /// filter state is cleared (advanceControl wakes it on leaving SOLID).
    void updateSideIdle(float k) noexcept {
        if (!sideIdle_ && k == 0.0f && modeRamp_.getTarget() == 0.0f) {
            sideIdle_ = true;
            crossover_.reset();
        }
    }

    /// The instance's side-gain target (deviation 5: sideFloorGain_ starts at kSideFloorGain).
    [[nodiscard]] float sideGainTarget() const noexcept { return sideFloorGain_ * (1.0f - applied_.gravity); }

    /// plan S1 C-9 (A): bpm_eff = bpm / 2^j with the smallest j >= 0 (j <= 16) such that
    /// (bpm_eff / 60) / beats <= kCycleMaxHz. Halving a float is exact.
    [[nodiscard]] static float foldedSyncTempo(float bpm, NoteValue note, NoteModifier modifier) noexcept {
        float eff = bpm;
        if constexpr (kCycleSyncFold) {
            constexpr int kMaxFoldSteps = 16;
            const float beats = getBeatsForNote(note, modifier);
            for (int j = 0; j < kMaxFoldSteps && syncedRateHz(eff, beats) > kCycleMaxHz; ++j) {
                eff *= 0.5f;
            }
        }
        return eff;
    }

    /// The LFO's own synced-rate expression (lfo.h:488-498): (bpm / 60) / beats.
    [[nodiscard]] static float syncedRateHz(float bpm, float beats) noexcept { return (bpm / 60.0f) / beats; }

    /// S5 / C-9: run the reused sources' setters only when the value they derive from changes (or when
    /// forced by prepare()). Drift tau = 30 * (0.5 / 30)^rate as setSmoothness((tau - 0.2) / 29.8)
    /// (inverse of brownian_drift.h tau = lerp(kTauMin, kTauMax, s)); Cycle f = 0.05 * 20^rate.
    void applyMotionConfig() noexcept {
        const float rate = applied_.motionRate;
        if (!motionConfigured_ || rate != lastRate_) {
            const double r = static_cast<double>(rate);
            driftTau_ = static_cast<float>(
                static_cast<double>(kDriftTauMaxSec) *
                std::pow(static_cast<double>(kDriftTauMinSec) / static_cast<double>(kDriftTauMaxSec), r));
            drift_.setSmoothness((driftTau_ - BrownianDrift::kTauMin) /
                                 (BrownianDrift::kTauMax - BrownianDrift::kTauMin));
            freeCycleHz_ = static_cast<float>(
                static_cast<double>(kCycleMinHz) *
                std::pow(static_cast<double>(kCycleMaxHz) / static_cast<double>(kCycleMinHz), r));
            lfo_.setFrequency(freeCycleHz_);   // kept current while synced too, for the return to free
            lastRate_ = rate;
        }

        const bool sync = applied_.cycleSync;
        const NoteValue note = applied_.cycleNote;
        const NoteModifier modifier = applied_.cycleModifier;
        const bool syncChanged = !motionConfigured_ || sync != lastSync_;
        const bool syncInputsChanged =
            sync && (note != lastNote_ || modifier != lastModifier_ || tempoBpm_ != lastTempoBpm_);
        if (syncChanged || syncInputsChanged) {
            if (sync) {
                const float bpmEff = foldedSyncTempo(tempoBpm_, note, modifier);
                lfo_.setNoteValue(note, modifier);
                lfo_.setTempo(bpmEff);
                lfo_.setTempoSync(true);
                syncCycleHz_ = syncedRateHz(bpmEff, getBeatsForNote(note, modifier));
            } else {
                lfo_.setTempoSync(false);
            }
            lastSync_ = sync;
            lastNote_ = note;
            lastModifier_ = modifier;
            lastTempoBpm_ = tempoBpm_;
        }
        cycleHz_ = sync ? syncCycleHz_ : freeCycleHz_;
        motionConfigured_ = true;
    }

    /// 2^kUpperKneeOctaves: the upper knee as a frequency factor (C-5: fixed factor 4).
    static constexpr float kUpperKneeFactor = 4.0f;
    static_assert(kUpperKneeOctaves == 2.0f, "kUpperKneeFactor must equal 2^kUpperKneeOctaves");

    /// plan S1 C-5 / S4.2: c = 0.5 - 0.5 cos(pi x^gamma), x = clamp((u - uLow) / span, 0, 1), with
    /// u = log2(f) supplied by the caller (the pan query sums a table instead of taking a per-partial log).
    /// The upper knee is also tested in Hz (f >= 4 f_c,s, an exact power-of-two product), so every
    /// partial at or above it is exactly 1 whatever the log rounding.
    [[nodiscard]] float lawCurve(float frequencyHz, float log2Hz) const noexcept {
        if (frequencyHz >= kUpperKneeFactor * fcS_) {
            return 1.0f;
        }
        const float x = (log2Hz - uLow_) * invSpan_;
        if (x <= 0.0f) {
            return 0.0f;
        }
        if (x >= 1.0f) {
            return 1.0f;
        }
        return 0.5f - 0.5f * std::cos(kPi * std::exp2(gamma_ * std::log2(x)));
    }

    /// S4.1: the per-interval law cache from f_c,s, Gravity, curve, depth and m.
    void updateLawCache() noexcept {
        const float log2Fc = std::log2(fcS_);
        const float g = applied_.gravity;
        uLow_ = log2Fc - kLowerKneeMaxOctaves * (1.0f - g);
        const float uHigh = log2Fc + kUpperKneeOctaves;
        invSpan_ = 1.0f / (uHigh - uLow_);                    // in [1/2.25, 1/2]
        gamma_ = std::pow(kMaxCurveExponent, applied_.curve);  // 4^curve
        const float mu = applied_.motionDepth;
        magnitudeScale_ = (applied_.mode == ImageMode::Solid)
                              ? 0.0f
                              : applied_.spread * (1.0f - kMotionExcursion * mu + kMotionExcursion * mu * m_);
    }

    // ---- controls ----
    Controls pending_;
    Controls applied_;
    float tempoBpm_ = kDefaultTempoBpm;
    std::uint32_t driftSeed_ = kDefaultDriftSeed;
    double sampleRate_ = 44100.0;   // CrossoverLR4's default (crossover_filter.h:382)
    bool prepared_ = false;

    // ---- the law ----
    std::array<float, kMaxPartials> log2Harmonic_{};   // log2(k + 1), filled in the constructor
    float uLow_ = 0.0f;
    float invSpan_ = 0.5f;
    float gamma_ = 1.0f;
    float magnitudeScale_ = 0.0f;
    OnePoleSmoother fcSmoother_{kDefaultCrossoverHz};   // the law crossover, control rate
    float fcS_ = kDefaultCrossoverHz;                    // f_c,s

    // ---- MOTION ----
    BrownianDrift drift_;
    LFO lfo_;
    float cycleValue_ = 0.0f;
    float m_ = 0.0f;
    MotionOutput motionOutput_;
    float driftTau_ = kDriftTauMaxSec;
    float freeCycleHz_ = kCycleMinHz;
    float syncCycleHz_ = kCycleMinHz;
    float cycleHz_ = kCycleMinHz;
    // change caches (S5: the reused sources' setters run only on a change)
    bool motionConfigured_ = false;
    float lastRate_ = 0.0f;
    bool lastSync_ = false;
    NoteValue lastNote_ = NoteValue::Whole;
    NoteModifier lastModifier_ = NoteModifier::None;
    float lastTempoBpm_ = kDefaultTempoBpm;

    // ---- the stage ----
    CrossoverLR4 crossover_;
    OnePoleSmoother sideGainSm_;
    LinearRamp modeRamp_;
    bool sideIdle_ = false;
    float wideEngage_ = 0.0f;   // G7-2: 1 in WIDE, 0 otherwise, slewed by kWideEngageStep per interval

    // ---- calibration members (tasks.md deviation 5: only the probe changes them; reset() keeps them) ----
    float sideFloorGain_ = kSideFloorGain;
    float wideOffsetScale_ = kWideOffsetScale;
};

}  // namespace Krate::DSP
