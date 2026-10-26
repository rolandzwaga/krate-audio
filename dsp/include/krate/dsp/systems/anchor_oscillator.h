// ==============================================================================
// Layer 3: System Component — Profundum Phase 3 (specs/profundum-phase3-anchor)
// ==============================================================================
// AnchorOscillator: the Profundum Anchor (sub) oscillator.
// Includes Layer 0, Layer 1 and Layer 2 only.
//
// Reference: specs/profundum-phase3-anchor/spec.md
//            specs/profundum-phase3-anchor/plan.md (S3, S4, S5)
//            specs/profundum-phase3-anchor/tasks.md (T006 creates this stub,
//                                                    T007 fills it)
//
// T007 scope: API, lifecycle, sanitising (S4.3, C-11), the S5 coefficient
// mirror (C-5), sine / LOCK processBlock (S4.4). Level and a1 are applied as
// snapped (held) values (tasks.md deviation 6: T012 adds the smoothers); the
// mode envelope is Hold(1) (T013); FOLLOW lands in T014.
// T012 scope: level and a1 through OnePoleSmoothers at kLevelSmoothMs (plan S4.9):
// setTarget on the Controls latch and on setCoreH1Amplitude, snapToTarget on a
// Reset noteOn (or one from not sounding) and in reset().
// T011 scope: the five waveforms (S4.5: triangle by Clenshaw over the cap-tapered
// coefficients keyed on f_T, with the T005 cents-domain weight cache; rounded,
// asymmetric with the C-6 onset ramp, octave-down) and the S4.8 waveform
// crossfade with its C-8 / E-7 queue.
// T013 scope: the S4.7 mode envelope g — a program of <= 3 segments (Hold, raised
// Cosine, cubic Hermite bridge) with exact stored end values (C-12), snap arms
// (Reset noteOn or from not sounding), bridged arms (FreeRunning re-arm while
// sounding), the FR-044 mode-switch bridge, the C-3 bridge-length rule (the
// largest N <= N_nominal whose cubic stays in [0, max(B, g0)], integer
// bisection), and the FR-048 rearm semantics (rearm acts on g only; C-8: a
// re-arm wins over a mode switch in the same block).
// T014 scope: FOLLOW (plan S4.10; FR-045, FR-049): r_eff = r in FOLLOW, else 1;
// δ_tgt = min(r_eff·δ_lock, capHz/fs); a bitwise change of r_eff glides δ linearly
// over kFollowGlideSamples (ending on the next grid sample), a Reset noteOn snaps it
// (snapRatio_); the taper key f_T is the larger δ of an active glide (S4.4).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                      // L0: detail::isFinite, detail::flushDenormal, opaqueFloatBits
#include <krate/dsp/core/math_constants.h>                // L0: kPi (the bank's float pi, S5.1)
#include <krate/dsp/primitives/smoother.h>                // L1: OnePoleSmoother (level, a1; plan S4.9)
#include <krate/dsp/processors/spectral_shape_recipe.h>   // L2: capFrequency, cap-taper constants

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Krate::DSP {

/// @note Precondition (FR-057): the owner runs processBlock with FTZ/DAZ on (as ProfundumCore,
///       profundum_core.h:89-91).
class AnchorOscillator {
public:
    enum class Waveform : std::uint8_t { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine };   // FR-003, append-only
    enum class Mode : std::uint8_t { Lock, Follow, Punch, Ghost };                                       // FR-003, append-only
    enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning };   // D-6: same values as ProfundumCore::RetriggerPhase

    // ---- grid and range (mirroring ProfundumCore, profundum_core.h:97-105) ----
    static constexpr std::size_t kControlInterval = 32;
    static constexpr std::size_t kPitchUpdateInterval = 16;
    static constexpr float kMinF0Hz = 8.0f;
    static constexpr float kDefaultF0Hz = 55.0f;
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;

    // ---- calibration (FR-023, FR-024; S1 C-2) ----
    static constexpr float kAnchorToH1 = 1.0f;                          // k
    static constexpr float kMaxCoreH1Amplitude = 0.3552343859f;         // == HarmonicFold::kCalibrationPeak (test static_assert)
    static constexpr float kDefaultLevel = 0.5f;

    // ---- time constants (D-3, D-4, D-5, FR-045; all <= 10 ms where the spec bounds them) ----
    static constexpr float kLevelSmoothMs = 5.0f;          // FR-020 (OnePoleSmoother: 99.3 % at 5 ms)
    static constexpr float kWaveformCrossfadeMs = 5.0f;    // FR-014
    static constexpr float kPunchRiseMs = 1.0f;            // D-4
    static constexpr float kGhostDipMs = 2.0f;             // D-5
    static constexpr float kModeBridgeMs = 5.0f;           // FR-044
    static constexpr std::size_t kFollowGlideSamples = 16; // FR-045 (== kPitchUpdateInterval: a glide ends on the next grid point)
    static constexpr float kAsymOnsetPeriods = 1.0f;       // FR-016, S1 C-6

    // ---- Controls ranges (D-3, D-4) and FOLLOW ratio range (FR-045) ----
    static constexpr float kMinPunchBoostDb = 0.0f, kMaxPunchBoostDb = 12.0f;
    static constexpr float kMinPunchTimeMs = 5.0f, kMaxPunchTimeMs = 250.0f;
    static constexpr float kMinGhostTimeMs = 1.0f, kMaxGhostTimeMs = 100.0f;
    static constexpr float kMinFollowRatio = 0.25f, kMaxFollowRatio = 4.0f;

    // ---- waveform set (S1 C-1, FR-017) ----
    static constexpr std::size_t kTriangleTerms = 16;      // odd harmonics 1..31
    static constexpr float kRoundedSineA3 = 0.25f;         // FR-017
    static constexpr float kAsymmetricSineA2 = 0.25f;      // FR-017

    // ---- FR-055 output ceiling: (k·a1max/√2) · √3 · 10^(12/20) · 1.01 = 1.7493 at k = 1 ----
    static constexpr float kOutputCeiling = 1.75f * kAnchorToH1;
    static_assert(kOutputCeiling <= 2.0f, "FR-055: k must keep the ceiling at or below 2.0");

    /// FR-002: plain interpolable aggregate. Designated initialisers only.
    struct Controls {
        Waveform waveform = Waveform::Sine;
        Mode mode = Mode::Lock;
        float level = kDefaultLevel;   // [0, 1]
        float punchBoostDb = 6.0f;     // [0, 12]
        float punchTimeMs = 40.0f;     // [5, 250]
        float ghostTimeMs = 15.0f;     // [1, 100]
    };

    // TEST-ONLY diagnostic: not a Phase 8/9/11 contract, never serialised (S1 C-4; S8.3 MirrorTracksCore)
    struct MirrorState {
        float baseHz;
        float currentHz;
        float maskHz;
        float shadow;
        float bankEps;
        float epsEff;
    };

    AnchorOscillator() noexcept = default;   // copyable and movable (FR-001): every member is a trivially copyable value

    // ---- control thread ----

    /// Clamps the rate to [22050, 192000], sizes and resets. Allocation-free (FR-004).
    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate);
        capHz_ = SpectralShapeRecipe::capFrequency(sampleRate_);
        invFsF_ = 1.0f / static_cast<float>(sampleRate_);   // the bank's inverseSampleRate_ (harmonic_oscillator_bank.h:135)

        // S4.2: the taper start and tail floor in float, as evaluateMask (spectral_shape_recipe.h:161-163).
        taperStartHz_ = capHz_ * std::exp2(-SpectralShapeRecipe::kCapTaperCents / 1200.0f);
        tailFloor_ = std::pow(10.0f, -SpectralShapeRecipe::kCapTaperDbPerCent
                                         * (SpectralShapeRecipe::kCapTaperCents - SpectralShapeRecipe::kCapTailCents)
                                         / 20.0f);
        // T005 cents-domain weight cache (plan S9.8): harmonic n sits 1200·log2(n) cents below h1, so its
        // dB-linear taper weight is wRef(f_T) · 2^(−s·cents(n)). A refresh then costs one log2 and one
        // exp2 regardless of how many terms are in the band.
        for (std::size_t i = 0; i < kTaperTableSize; ++i) {
            centsOfN_[i] = 1200.0 * std::log2(static_cast<double>(i + 1));
            taperFactorOfN_[i] = std::exp2(-kTaperLog2PerCent * centsOfN_[i]);
        }
        // S4.5 / C-1: a_k = (−1)^k · 8/(π²(2k+1)²); unit-RMS scale 1/√(Σ a_k²/2) of the untapered series.
        double sumSq = 0.0;
        for (std::size_t k = 0; k < kTriangleTerms; ++k) {
            const auto n = static_cast<double>(2 * k + 1);
            const double ak = 8.0 / (kPiDouble * kPiDouble * n * n);
            triBase_[k] = (k % 2 == 0) ? ak : -ak;
            sumSq += ak * ak;
        }
        triScale_ = 1.0 / std::sqrt(0.5 * sumSq);
        // S4.8: crossfade length and its rotation step.
        xfadeLen_ = static_cast<std::uint32_t>(std::max<long long>(
            1, std::llround(static_cast<double>(kWaveformCrossfadeMs) * sampleRate_ / 1000.0)));
        xCd_ = std::cos(kPiDouble / static_cast<double>(xfadeLen_));
        xSd_ = std::sin(kPiDouble / static_cast<double>(xfadeLen_));
        // S4.7: the fixed envelope lengths (the Controls-dependent ones are read at arm time, C-8).
        nRise_ = msToSamples(kPunchRiseMs);
        nDip_ = msToSamples(kGhostDipMs);
        nModeBridge_ = msToSamples(kModeBridgeMs);
        // S4.9: level and a1 smoothers (99.3 % in kLevelSmoothMs; time constant kLevelSmoothMs/5).
        levelS_.configure(kLevelSmoothMs, static_cast<float>(sampleRate_));
        a1S_.configure(kLevelSmoothMs, static_cast<float>(sampleRate_));
        prepared_ = true;
        reset();   // pending Controls: defaults on the first prepare (setControls is a no-op before it), kept after
    }

    /// Keeps the pending Controls, the held follow ratio and the held a1; clears sounding (FR-004, S4.2).
    void reset() noexcept {
        if (!prepared_)
            return;
        sounding_ = false;
        noteOnPending_ = false;
        noteOnRearm_ = true;
        heldF0_ = kDefaultF0Hz;
        prevSampleF0_ = kDefaultF0Hz;
        noteOnF0_ = kDefaultF0Hz;
        prevSampleRatio_ = heldRatio_;
        intervalPhase_ = 0;
        phi2_ = 0.0;
        delta_ = 0.0;
        lastInc_ = 0.0;
        // S4.10: no glide; the next noteOn (always a snap from not sounding) snaps δ.
        deltaFrom_ = 0.0;
        deltaTo_ = 0.0;
        glideLeft_ = 0;
        glideLen_ = 0;
        rEffPrev_ = 1.0f;
        snapRatio_ = true;
        g_ = 1.0;   // envelope Hold(1)
        setProgram(holdSeg(1.0));
        // Mirror: the core's initialiseState values (profundum_core.h:424-437).
        mBase_ = kDefaultF0Hz;
        mCurrentF_ = kDefaultF0Hz;
        mMaskF_ = 0.0f;
        mShadow_ = 1.0f;
        mEpsBase_ = 0.0;
        mBankEps_ = 0.0f;
        mEpsEff_ = 0.0f;
        deltaLock_ = 0.0;
        // Waveform state (S4.5, S4.8, C-6): untapered coefficients, no crossfade, no onset ramp. The taper
        // key is invalidated so the first grid sample after a noteOn refreshes it.
        triCoef_ = triBase_;
        triActive_ = kTriangleTerms;
        a3Eff_ = static_cast<double>(kRoundedSineA3);
        a2Eff_ = static_cast<double>(kAsymmetricSineA2);
        taperFr_ = -1.0f;
        taperFast_ = false;
        xfadeActive_ = false;
        queued_ = false;
        xfadeN_ = 0;
        xRc_ = 1.0;
        xRs_ = 0.0;
        onsetN_ = 0;
        onsetLen_ = 0;
        latchControls();   // not sounding: snaps the waveform (requestWaveform)
        // S4.2: both smoothers snapped to their targets (level = pending level, a1 = the held a1).
        levelS_.snapToTarget();
        a1S_.setTarget(a1Held_);
        a1S_.snapToTarget();
    }

    // ---- audio thread (noexcept, allocation-free, no-ops before prepare) ----

    /// Writes the sanitised pending copy (S4.3, C-11: flush first, then clamp); latched at offset 0
    /// of the next block (C-8).
    void setControls(const Controls& controls) noexcept {
        if (!prepared_)
            return;
        const Controls& p = pending_;
        const Controls next{
            .waveform = static_cast<std::uint8_t>(controls.waveform) <= static_cast<std::uint8_t>(Waveform::OctaveDownSine)
                            ? controls.waveform
                            : p.waveform,
            .mode = static_cast<std::uint8_t>(controls.mode) <= static_cast<std::uint8_t>(Mode::Ghost) ? controls.mode
                                                                                                        : p.mode,
            .level = sanitizeField(controls.level, p.level, 0.0f, 1.0f),
            .punchBoostDb = sanitizeField(controls.punchBoostDb, p.punchBoostDb, kMinPunchBoostDb, kMaxPunchBoostDb),
            .punchTimeMs = sanitizeField(controls.punchTimeMs, p.punchTimeMs, kMinPunchTimeMs, kMaxPunchTimeMs),
            .ghostTimeMs = sanitizeField(controls.ghostTimeMs, p.ghostTimeMs, kMinGhostTimeMs, kMaxGhostTimeMs),
        };
        if (next.waveform == p.waveform && next.mode == p.mode && sameBits(next.level, p.level)
            && sameBits(next.punchBoostDb, p.punchBoostDb) && sameBits(next.punchTimeMs, p.punchTimeMs)
            && sameBits(next.ghostTimeMs, p.ghostTimeMs))
            return;
        pending_ = next;
        controlsDirty_ = true;
    }

    /// Held f0 (FR-005): the core's sanitizeF0 (profundum_core.h:795-800).
    void setFrequency(float f0Hz) noexcept {
        if (!prepared_)
            return;
        heldF0_ = sanitizeF0(f0Hz, heldF0_);
    }

    /// Held perceived-pitch ratio (FR-045): non-finite -> previous, else clamp to [0.25, 4].
    void setFollowRatio(float r) noexcept {
        if (!prepared_)
            return;
        heldRatio_ = detail::isFinite(r) ? std::clamp(r, kMinFollowRatio, kMaxFollowRatio) : heldRatio_;
    }

    /// FR-023, per control block: non-finite -> previous, else clamp to [0, kMaxCoreH1Amplitude].
    void setCoreH1Amplitude(float a1) noexcept {
        if (!prepared_)
            return;
        a1Held_ = detail::isFinite(a1) ? std::clamp(a1, 0.0f, kMaxCoreH1Amplitude) : a1Held_;
        a1S_.setTarget(a1Held_);   // S4.3 / S4.9: smoothed from the next rendered sample
    }

    void setRetriggerPhase(RetriggerPhase policy) noexcept {
        if (!prepared_)
            return;
        policy_ = policy;
    }

    /// Effective at offset 0 of the next block (FR-005, FR-048). The last call before a block wins.
    void noteOn(float f0Hz, bool rearm = true) noexcept {
        if (!prepared_)
            return;
        noteOnF0_ = sanitizeF0(f0Hz, heldF0_);
        noteOnRearm_ = rearm;
        noteOnPending_ = true;
    }

    /// S4.4 normative order. Overwrites both buffers (FR-007); an aliased left == right is written once.
    void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr,
                      const float* followRatioPerSample = nullptr) noexcept {
        if (left == nullptr || right == nullptr || numSamples == 0)
            return;   // FR-007: no state change
        if (!prepared_) {   // FR-004
            std::fill(left, left + numSamples, 0.0f);
            std::fill(right, right + numSamples, 0.0f);
            return;
        }
        if (noteOnPending_)
            applyNoteOn();   // S4.6, offset 0 (latches the Controls itself)
        else if (controlsDirty_)
            latchControls();   // C-8

        if (!sounding_) {   // C-7: silence, trajectory bookkeeping only (as the core, :285-287)
            if (f0PerSample != nullptr)
                heldF0_ = trajectoryF0(f0PerSample, 0, numSamples - 1, prevSampleF0_);
            prevSampleF0_ = heldF0_;
            if (followRatioPerSample != nullptr)
                heldRatio_ = trajectoryRatio(followRatioPerSample, 0, numSamples - 1, prevSampleRatio_);
            prevSampleRatio_ = heldRatio_;
            std::fill(left, left + numSamples, 0.0f);
            std::fill(right, right + numSamples, 0.0f);
            return;
        }

        float trajF = prevSampleF0_;
        float trajR = prevSampleRatio_;
        std::size_t resolvedF = 0;
        std::size_t resolvedR = 0;
        for (std::size_t i = 0; i < numSamples; ++i) {
            if (intervalPhase_ % kPitchUpdateInterval == 0) {
                float f = heldF0_;
                if (f0PerSample != nullptr) {
                    trajF = trajectoryF0(f0PerSample, resolvedF, i, trajF);
                    resolvedF = i + 1;
                    f = trajF;
                }
                float r = heldRatio_;
                if (followRatioPerSample != nullptr) {
                    trajR = trajectoryRatio(followRatioPerSample, resolvedR, i, trajR);
                    resolvedR = i + 1;
                    r = trajR;
                }
                if (intervalPhase_ == 0)
                    mirrorControlUpdate(f);
                else
                    mirrorPitchUpdate(f);
                frequencyUpdate(r);
                updateTaper();   // S4.4: keyed on f_T, not on f
            }
            const double w = waveformSample();
            const double g = envelopeStep();   // S4.7: g at this sample, then advance
            const double level = static_cast<double>(levelS_.process());   // S4.9
            const double a1 = static_cast<double>(a1S_.process());
            const float y = static_cast<float>(kGainScale * level * a1 * g * w);   // formed in double, rounded once
            left[i] = y;
            if (right != left)
                right[i] = y;   // FR-050: one value, both channels
            advancePhase();
            intervalPhase_ = static_cast<std::uint32_t>((static_cast<std::size_t>(intervalPhase_) + 1) % kControlInterval);
        }
        if (f0PerSample != nullptr)   // the scalar becomes the trajectory's last value (core :285-286)
            heldF0_ = trajectoryF0(f0PerSample, resolvedF, numSamples - 1, trajF);
        prevSampleF0_ = heldF0_;
        if (followRatioPerSample != nullptr)
            heldRatio_ = trajectoryRatio(followRatioPerSample, resolvedR, numSamples - 1, trajR);
        prevSampleRatio_ = heldRatio_;
    }

    // ---- observers (FR-008, S1 C-4, S4.11) ----

    [[nodiscard]] double modeGain() const noexcept { return g_; }   // g at the last rendered sample
    [[nodiscard]] double phase() const noexcept { return phi2_ >= 1.0 ? phi2_ - 1.0 : phi2_; }   // cycles [0, 1)
    [[nodiscard]] double phaseIncrement() const noexcept { return lastInc_; }   // δ applied after the last rendered sample
    [[nodiscard]] bool isSounding() const noexcept { return sounding_; }
    [[nodiscard]] Controls pendingControls() const noexcept { return pending_; }   // C-11 read-back (SC-015)
    [[nodiscard]] bool stateFinite() const noexcept {
        return detail::isFinite(phi2_) && detail::isFinite(delta_) && detail::isFinite(lastInc_)
               && detail::isFinite(deltaFrom_) && detail::isFinite(deltaTo_)
               && detail::isFinite(xRc_) && detail::isFinite(xRs_)
               && detail::isFinite(g_) && envelopeFinite() && detail::isFinite(levelS_.getCurrentValue())
               && detail::isFinite(a1S_.getCurrentValue()) && detail::isFinite(a1Held_)
               && detail::isFinite(heldF0_) && detail::isFinite(prevSampleF0_) && detail::isFinite(noteOnF0_)
               && detail::isFinite(heldRatio_) && detail::isFinite(prevSampleRatio_) && detail::isFinite(mBase_)
               && detail::isFinite(mEpsBase_) && detail::isFinite(mBankEps_) && detail::isFinite(mShadow_)
               && detail::isFinite(mCurrentF_) && detail::isFinite(mMaskF_) && detail::isFinite(mEpsEff_)
               && detail::isFinite(deltaLock_);
    }
    /// TEST-ONLY diagnostic: not a Phase 8/9/11 contract, never serialised (C-4).
    [[nodiscard]] MirrorState mirrorState() const noexcept {
        return MirrorState{.baseHz = mBase_,
                           .currentHz = mCurrentF_,
                           .maskHz = mMaskF_,
                           .shadow = mShadow_,
                           .bankEps = mBankEps_,
                           .epsEff = mEpsEff_};
    }
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }   // FR-033

private:
    static constexpr double kPiDouble = 3.14159265358979323846;   // == ProfundumCore::kPiDouble (profundum_core.h:813)
    static constexpr double kTwoPiD = 2.0 * kPiDouble;            // S4.5 angle reduction
    static constexpr double kMaxBankEpsilonD = 1.99;              // the core's kMaxBankEpsilon (profundum_core.h:814)
    static constexpr float kMaxBankEpsilonF = 1.99f;              // the bank's kMaxEpsilon (harmonic_oscillator_bank.h:1060)
    static constexpr float kMaxBaseStepSemitones = 0.9f;          // == ProfundumCore::kMaxBaseStepSemitones (profundum_core.h:103)
    static constexpr double kSqrt2D = 1.4142135623730950488;      // S4.5: the sine's unit-RMS scale
    static constexpr double kGainScale = static_cast<double>(kAnchorToH1) * 0.70710678118654752;   // k/√2 (S4.4)
    static constexpr double kInvRmsA025 = 1.3719886811400708;     // 1/√0.53125 = √(32/17): rounded and asymmetric (S4.5)
    static constexpr std::size_t kTaperTableSize = 2 * kTriangleTerms - 1;   // harmonics 1..31
    // dB-linear cap taper in log2 units per cent: (kCapTaperDbPerCent / 20) · log2(10)
    static constexpr double kTaperLog2PerCent =
        static_cast<double>(SpectralShapeRecipe::kCapTaperDbPerCent) / 20.0 * 3.32192809488736234787;

    // ---- sanitising (S4.3) ----

    [[nodiscard]] static bool sameBits(float a, float b) noexcept {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }

    /// C-11: non-finite -> previous (pending) value; otherwise flush a denormal to 0, then clamp.
    [[nodiscard]] static float sanitizeField(float x, float previous, float lo, float hi) noexcept {
        return detail::isFinite(x) ? std::clamp(detail::flushDenormal(x), lo, hi) : previous;
    }

    /// profundum_core.h:795-800 verbatim.
    [[nodiscard]] float sanitizeF0(float f0, float previous) const noexcept {
        if (!detail::isFinite(f0))
            return previous;
        return std::clamp(f0, kMinF0Hz, capHz_);
    }

    /// profundum_core.h:805-811 verbatim (FR-005 per-element rule).
    [[nodiscard]] float trajectoryF0(const float* x, std::size_t from, std::size_t i, float previous) const noexcept {
        for (std::size_t j = i + 1; j-- > from;) {
            if (detail::isFinite(x[j]))
                return std::clamp(x[j], kMinF0Hz, capHz_);
        }
        return previous;
    }

    /// The ratio twin of trajectoryF0 (FR-045 range).
    [[nodiscard]] static float trajectoryRatio(const float* x, std::size_t from, std::size_t i, float previous) noexcept {
        for (std::size_t j = i + 1; j-- > from;) {
            if (detail::isFinite(x[j]))
                return std::clamp(x[j], kMinFollowRatio, kMaxFollowRatio);
        }
        return previous;
    }

    // ---- controls and note-on (C-8, S4.6) ----

    void latchControls() noexcept {
        const Mode previousMode = active_.mode;
        active_ = pending_;
        controlsDirty_ = false;
        levelS_.setTarget(active_.level);   // S4.9: smoothed; a Reset noteOn snaps it (applyNoteOn)
        requestWaveform(active_.waveform);
        // FR-044: a mode change while sounding never steps g; it bridges to 1 over kModeBridgeMs. A
        // re-arm in the same block replaces this program (C-8: the re-arm wins, applyNoteOn).
        if (sounding_ && active_.mode != previousMode)
            bridgeToRest();
    }

    /// S4.8 / C-8: not sounding -> snap; during a crossfade -> queue (latest wins, E-7); otherwise a
    /// change starts a crossfade from the current waveform.
    void requestWaveform(Waveform w) noexcept {
        if (!sounding_) {
            current_ = w;
            xfadeActive_ = false;
            queued_ = false;
            return;
        }
        if (xfadeActive_) {
            queued_ = w != next_;
            queuedWave_ = w;
            return;
        }
        if (w != current_)
            startCrossfade(w);
    }

    void startCrossfade(Waveform to) noexcept {
        next_ = to;
        xfadeActive_ = true;
        xfadeN_ = 0;
        xRc_ = 1.0;   // c(0) = ½(1 − cos 0) = 0
        xRs_ = 0.0;
    }

    void applyNoteOn() noexcept {
        const float f = noteOnF0_;
        heldF0_ = f;
        prevSampleF0_ = f;
        prevSampleRatio_ = heldRatio_;
        const bool fromSilence = !sounding_;
        const bool rearm = noteOnRearm_ || fromSilence;   // FR-048 (e): from not sounding always arms
        latchControls();                                   // C-8
        if (policy_ == RetriggerPhase::Reset || fromSilence) {   // Reset branch (mirrors core :528-557)
            phi2_ = 0.0;
            intervalPhase_ = 0;
            mirrorResetNoteOn(f);
            snapRatio_ = true;   // FR-006 / S4.6: the r glide snaps at the grid sample at offset 0
            glideLeft_ = 0;
            // FR-006 / S4.8: a Reset noteOn completes a running crossfade and drops a queued switch.
            current_ = active_.waveform;
            xfadeActive_ = false;
            queued_ = false;
            levelS_.snapToTarget();   // S4.6 / S4.9: a Reset noteOn starts at the latched level and held a1
            a1S_.snapToTarget();
            // C-6: one period of f on the asymmetric sine's h2 term (FR-016); every other waveform is 0 at φ = 0.
            onsetN_ = 0;
            onsetLen_ = 0;
            if (current_ == Waveform::AsymmetricSine) {
                const long long nOn = std::llround(static_cast<double>(kAsymOnsetPeriods) * sampleRate_
                                                   / static_cast<double>(f));
                onsetLen_ = static_cast<std::uint32_t>(
                    std::clamp<long long>(nOn, 1, static_cast<long long>(4.0 * sampleRate_)));
            }
            if (rearm)
                snapArm();   // S4.7 / FR-048: g restarts at the mode's start value
            // rearm == false on a sounding voice: g continues untouched (FR-048, SC-027(d)).
        } else {   // FreeRunning while sounding: phase and grid continue
            mirrorFreeRunningNoteOn(f);
            // tasks.md deviation 5: the core delivers a FreeRunning noteOn's f to the bank at offset 0
            // (profundum_core.h:587-596), so the new δ is in force from offset 0, not the next grid point.
            frequencyUpdate(heldRatio_);
            updateTaper();   // the taper follows the new δ from offset 0 too (S4.4 key)
            if (rearm)
                bridgedArm();   // S4.7 / FR-048: Hermite bridge from the current (g, g')
            // rearm == false: g continues untouched (FR-048). The phase branch above never reads rearm.
        }
        sounding_ = true;
        noteOnPending_ = false;
    }

    // ---- S5 coefficient mirror (C-5): each expression keeps its source line's operand order and types ----

    /// Bank h1 epsilon at base b (harmonic_oscillator_bank.h:1063-1065; n = 1, deviation 0: freq == b).
    [[nodiscard]] float bankEps(float b) const noexcept {
        const float eps = 2.0f * std::sin(kPi * b * invFsF_);
        return std::clamp(eps, -kMaxBankEpsilonF, kMaxBankEpsilonF);
    }

    /// Core setBase epsilon estimate, k = 0 (profundum_core.h:690-697).
    [[nodiscard]] double epsBase(float b) const noexcept {
        const double w = kPiDouble * static_cast<double>(b) / sampleRate_;
        return std::clamp(2.0 * std::sin(static_cast<double>(1) * w), -kMaxBankEpsilonD, kMaxBankEpsilonD);
    }

    /// Core computeEpsilonTargets (k = 0) + refreshMultipliers (profundum_core.h:702-739), then the
    /// bank's effective coefficient (harmonic_oscillator_bank.h:732 / simd :102) and δ_lock.
    void refresh(float f0) noexcept {
        const double f = static_cast<double>(f0);
        const double capHz = static_cast<double>(capHz_);
        const double theta = kPiDouble * f / sampleRate_;
        double epsStar = 2.0 * std::sin(theta);
        if (static_cast<double>(1) * f >= capHz)
            epsStar = 2.0 * std::sin(kPiDouble * capHz / sampleRate_);
        const double eb = mEpsBase_;
        const float dTarget = std::abs(eb) > 1e-9 ? static_cast<float>(epsStar / eb) : 1.0f;
        // The core stores dTarget / shadow to memory and multiplies in a separate loop. The barrier
        // keeps a fast-math build from folding shadow * (dTarget / shadow) to dTarget here (C-5).
        const float m = std::bit_cast<float>(detail::opaqueFloatBits(dTarget / mShadow_));
        mShadow_ = mShadow_ * m;
        mEpsEff_ = std::clamp(mBankEps_ * mShadow_, -kMaxBankEpsilonF, kMaxBankEpsilonF);
        deltaLock_ = std::asin(0.5 * static_cast<double>(mEpsEff_)) / kPiDouble;
    }

    /// ProfundumCore::chaseBase restated (profundum_core.h:666-687) against mBase_.
    [[nodiscard]] float chase(float f0) const noexcept {
        const double ratio = std::exp2(static_cast<double>(kMaxBaseStepSemitones) / 12.0);
        const double base = static_cast<double>(mBase_);
        const double hi = base * ratio;
        const double lo = base / ratio;
        const double f = static_cast<double>(f0);
        if (f > hi) {
            float b = static_cast<float>(hi);
            if (static_cast<double>(b) > hi)
                b = std::nextafter(b, 0.0f);
            return b;
        }
        if (f < lo) {
            float b = static_cast<float>(lo);
            if (static_cast<double>(b) < lo)
                b = std::nextafter(b, std::numeric_limits<float>::max());
            return b;
        }
        return f0;
    }

    void setMirrorBase(float b) noexcept {
        mBase_ = b;
        mEpsBase_ = epsBase(b);
        mBankEps_ = bankEps(b);
    }

    /// S5.2: core Reset branch (:528-557).
    void mirrorResetNoteOn(float f) noexcept {
        mShadow_ = 1.0f;
        setMirrorBase(f);
        refresh(f);
        mCurrentF_ = f;
        mMaskF_ = f;
    }

    /// S5.3: core controlUpdate (:448-507), n = 1, shapeChanged assumed false (C-5 residual).
    void mirrorControlUpdate(float f) noexcept {
        const bool fChanged = !sameBits(f, mCurrentF_);
        mCurrentF_ = f;
        const float baseNew = chase(f);   // sounding_ is true whenever the mirror runs (C-7)
        const bool maskChanged = !sameBits(f, mMaskF_);
        if (maskChanged)
            mMaskF_ = f;
        const bool baseChanged = !sameBits(baseNew, mBase_);
        if (maskChanged || baseChanged) {
            if (baseChanged)
                setMirrorBase(baseNew);
            refresh(f);
        } else if (fChanged) {
            refresh(f);
        }
    }

    /// S5.3: core pitchUpdate (:511-517).
    void mirrorPitchUpdate(float f) noexcept {
        if (sameBits(f, mCurrentF_))
            return;
        mCurrentF_ = f;
        refresh(f);
    }

    /// S5.3: core FreeRunning branch (:560-600), base = f with no chase clamp.
    void mirrorFreeRunningNoteOn(float f) noexcept {
        const bool fChanged = !sameBits(f, mCurrentF_);
        mCurrentF_ = f;
        const bool maskChanged = !sameBits(f, mMaskF_);
        if (maskChanged)
            mMaskF_ = f;
        const bool baseChanged = !sameBits(f, mBase_);
        if (maskChanged || baseChanged) {
            if (baseChanged)
                setMirrorBase(f);
            refresh(f);
        } else if (fChanged) {
            refresh(f);
        }
    }

    // ---- frequency, waveform, phase (S4.4, S4.5, S4.10) ----

    /// S4.10: r_eff = r in FOLLOW, else 1 (so every other mode, and FOLLOW at r = 1, is exactly
    /// 1.0·δ_lock: FOLLOW at r = 1 is bit-identical to LOCK, SC-008(c)). δ_tgt is capped at capHz/fs.
    /// A bitwise change of r_eff (a ratio change in FOLLOW, or a mode switch to or from FOLLOW,
    /// FR-044) glides δ linearly to δ_tgt, ending exactly on the next grid sample: 16 samples from a
    /// grid sample; from the off-grid FreeRunning noteOn call (tasks.md deviation 5) the samples left
    /// to the next grid sample. A Reset noteOn snaps (snapRatio_, FR-006). With r_eff unchanged δ
    /// snaps to δ_tgt (LOCK's law); only an off-grid glide still running at a grid sample (possible
    /// only after a FreeRunning noteOn) keeps running and is retargeted to the new δ_tgt.
    void frequencyUpdate(float r) noexcept {
        const float rEff = active_.mode == Mode::Follow ? r : 1.0f;
        const double target =
            std::min(static_cast<double>(rEff) * deltaLock_, static_cast<double>(capHz_) / sampleRate_);
        const bool sameRatio = sameBits(rEff, rEffPrev_);
        if (snapRatio_ || (sameRatio && glideLeft_ == 0)) {
            delta_ = target;
            glideLeft_ = 0;
            snapRatio_ = false;
        } else if (sameRatio) {
            deltaTo_ = target;
        } else {
            deltaFrom_ = delta_;
            deltaTo_ = target;
            glideLen_ = static_cast<std::uint32_t>(kFollowGlideSamples
                                                   - static_cast<std::size_t>(intervalPhase_) % kPitchUpdateInterval);
            glideLeft_ = glideLen_;
        }
        rEffPrev_ = rEff;
    }

    // ---- cap taper (S4.5, FR-012), keyed on f_T (S4.4) ----

    /// f_T = the clamped frequency the waveform runs at (δ is already capped, S4.10); during an r
    /// glide the larger of its two endpoints, so no harmonic of either passes the cap. Refreshes the
    /// taper only when f_T changed bitwise.
    void updateTaper() noexcept {
        const double dT = glideLeft_ > 0 ? std::max(deltaFrom_, deltaTo_) : delta_;
        const auto fT = static_cast<float>(dT * sampleRate_);
        if (!sameBits(fT, taperFr_)) {
            refreshTaper(fT);
            taperFr_ = fT;
        }
    }

    /// T(n·f_T): the recipe's per-harmonic cap branch (spectral_shape_recipe.h:173-182) without the
    /// low-note guard, h1 exempt (as the recipe's out[0] = 1, :165). Band membership in float as the
    /// recipe; inside the band the cents come from the T005 cache: y_n = y1 − 1200·log2(n), and the
    /// dB-linear weight 10^(−kCapTaperDbPerCent·(kCapTaperCents − y_n)/20) = wRef · taperFactorOfN_.
    [[nodiscard]] double taperWeight(std::size_t n, float fT, double y1, double wRef) const noexcept {
        if (n == 1)
            return 1.0;
        const float nf = static_cast<float>(n) * fT;
        if (nf >= capHz_)
            return 0.0;
        if (!(nf > taperStartHz_))
            return 1.0;
        const double y = y1 - centsOfN_[n - 1];
        if (y >= static_cast<double>(SpectralShapeRecipe::kCapTaperCents))
            return 1.0;
        if (y > static_cast<double>(SpectralShapeRecipe::kCapTailCents))
            return wRef * taperFactorOfN_[n - 1];
        return static_cast<double>(tailFloor_) * std::max(0.0, y)
               / static_cast<double>(SpectralShapeRecipe::kCapTailCents);
    }

    /// S4.5 refreshTaper(f_T). Fast path (31·f_T ≤ taperStartHz_): every weight is 1, the untapered
    /// constants (copied once on entering the fast path). Slow path: one log2 and one exp2 (the T005
    /// cents-domain cache), then a multiply per band term; K_a = the odd terms below the cap.
    void refreshTaper(float fT) noexcept {
        if (static_cast<float>(kTaperTableSize) * fT <= taperStartHz_) {
            if (!taperFast_) {
                triCoef_ = triBase_;
                triActive_ = kTriangleTerms;
                a3Eff_ = static_cast<double>(kRoundedSineA3);
                a2Eff_ = static_cast<double>(kAsymmetricSineA2);
                taperFast_ = true;
            }
            return;
        }
        taperFast_ = false;
        const double y1 = 1200.0 * std::log2(static_cast<double>(capHz_) / static_cast<double>(fT));
        const double wRef =
            std::exp2(kTaperLog2PerCent * (y1 - static_cast<double>(SpectralShapeRecipe::kCapTaperCents)));
        triActive_ = 0;
        for (std::size_t k = 0; k < kTriangleTerms; ++k) {
            const double t = taperWeight(2 * k + 1, fT, y1, wRef);
            triCoef_[k] = triBase_[k] * t;
            if (t > 0.0)
                triActive_ = k + 1;
        }
        a3Eff_ = static_cast<double>(kRoundedSineA3) * taperWeight(3, fT, y1, wRef);
        a2Eff_ = static_cast<double>(kAsymmetricSineA2) * taperWeight(2, fT, y1, wRef);
    }

    // ---- waveforms (S4.5), crossfade (S4.8), onset ramp (C-6) ----

    /// Triangle by Clenshaw in double over the triActive_ tapered coefficients:
    /// Σ a_k sin((2k+1)x) with c = 2 cos 2x = 2 − 4u² and b_k = a_k + c·b_{k+1} − b_{k+2} gives
    /// u·(b_0 + b_1) (sin 3x − c·sin x = −sin(−x) = u). u = 0 gives exactly 0 (FR-016).
    [[nodiscard]] double triangleUnit(double u) const noexcept {
        const double c = 2.0 - 4.0 * u * u;
        double b1 = 0.0;   // b_{k+1}
        double b2 = 0.0;   // b_{k+2}
        for (std::size_t k = triActive_; k-- > 0;) {
            const double b0 = triCoef_[k] + c * b1 - b2;
            b2 = b1;
            b1 = b0;
        }
        return triScale_ * u * (b1 + b2);
    }

    /// One waveform at unit RMS (S4.5 table). `ramp` is the C-6 onset weight on the asymmetric h2 term.
    [[nodiscard]] double shapeUnit(Waveform w, double u, double ramp) const noexcept {
        switch (w) {
            case Waveform::Sine:
                return kSqrt2D * u;
            case Waveform::Triangle:
                return triangleUnit(u);
            case Waveform::RoundedSine:   // u + a3·sin 3x, sin 3x = 3u − 4u³
                return kInvRmsA025 * (u + a3Eff_ * (3.0 * u - 4.0 * u * u * u));
            case Waveform::AsymmetricSine:   // u − a2·r(n)·cos 2x, cos 2x = 1 − 2u²
                return kInvRmsA025 * (u - a2Eff_ * ramp * (1.0 - 2.0 * u * u));
            case Waveform::OctaveDownSine: {   // sin(φ/2): φ2 ∈ [0, 2) reduced to [−1, 1) half-cycles
                const auto v = std::sin(static_cast<float>(kPiDouble * (phi2_ >= 1.0 ? phi2_ - 2.0 : phi2_)));
                return kSqrt2D * static_cast<double>(v);
            }
        }
        return 0.0;
    }

    /// S4.5 + S4.8 + C-6 at the current φ, then advances the onset ramp and the crossfade by one sample.
    [[nodiscard]] double waveformSample() noexcept {
        const double phi1 = phi2_ >= 1.0 ? phi2_ - 1.0 : phi2_;
        const float a = static_cast<float>(kTwoPiD * (phi1 >= 0.5 ? phi1 - 1.0 : phi1));
        const auto u = static_cast<double>(std::sin(a));

        double ramp = 1.0;   // C-6: r(n) = ½(1 − cos(π·n/N_on)), then 1
        if (onsetLen_ != 0) {
            ramp = 0.5 * (1.0 - std::cos(kPiDouble * static_cast<double>(onsetN_) / static_cast<double>(onsetLen_)));
            if (++onsetN_ >= onsetLen_) {
                onsetN_ = 0;
                onsetLen_ = 0;
            }
        }

        double w = shapeUnit(current_, u, ramp);
        if (xfadeActive_) {   // equal gain on the same φ: w_old + c·(w_new − w_old), c(n) = ½(1 − cos(π·n/N_x))
            const double wNew = shapeUnit(next_, u, ramp);
            const double c = 0.5 * (1.0 - xRc_);
            w = w + c * (wNew - w);
            advanceCrossfade();
        }
        return w;
    }

    /// One crossfade step: the S4.7 rotation recurrence for cos(π·n/N_x); at N_x the new waveform takes
    /// over and a queued switch (E-7) starts its own crossfade.
    void advanceCrossfade() noexcept {
        ++xfadeN_;
        if (xfadeN_ >= xfadeLen_) {
            current_ = next_;
            xfadeActive_ = false;
            xRc_ = 1.0;
            xRs_ = 0.0;
            if (queued_) {
                queued_ = false;
                if (queuedWave_ != current_)
                    startCrossfade(queuedWave_);
            }
            return;
        }
        const double rc = xRc_ * xCd_ - xRs_ * xSd_;
        xRs_ = xRs_ * xCd_ + xRc_ * xSd_;
        xRc_ = rc;
    }

    /// φ₂ += δ. During an r glide the k-th sample (k = 0 … len − 1) uses
    /// δ = δ_from + (δ_to − δ_from)·(k + 1)/len and the last one δ_to exactly (S4.10), so f_inst is
    /// monotone between the endpoints with no overshoot.
    void advancePhase() noexcept {
        if (glideLeft_ > 0) {
            --glideLeft_;
            delta_ = glideLeft_ == 0 ? deltaTo_
                                     : deltaFrom_
                                           + (deltaTo_ - deltaFrom_) * static_cast<double>(glideLen_ - glideLeft_)
                                                 / static_cast<double>(glideLen_);
        }
        lastInc_ = delta_;
        phi2_ += delta_;
        if (phi2_ >= 2.0)
            phi2_ -= 2.0;
    }

    // ---- mode envelope g (S4.7; FR-041-FR-044, FR-048; S1 C-3, C-8, C-12) ----

    /// One segment of the g program. Cosine: g = g0 + a·½(1 − cos(π·n/len)), cos/sin advanced by a
    /// double rotation recurrence whose step is evaluated once when the segment is built. Hermite:
    /// cubic from (g0, m0 = len·g'0) to (g1, 0) over len samples. Hold: g0 forever (len 0). The next
    /// segment's g0 is this segment's stored end value g1, so the end value is exact (C-12: every
    /// return to 1 stores the literal 1.0).
    struct Segment {
        enum class Kind : std::uint8_t { Hold, Cosine, Hermite };
        Kind kind = Kind::Hold;
        double g0 = 1.0;   // value at n = 0
        double a = 0.0;    // Cosine height (g1 − g0)
        double m0 = 0.0;   // Hermite start tangent in t units (len·g'0)
        double g1 = 1.0;   // stored end value
        std::uint32_t n = 0;
        std::uint32_t len = 0;   // 0 for Hold
        double rc = 1.0;   // cos(π·n/len)
        double rs = 0.0;   // sin(π·n/len)
        double cd = 1.0;   // cos(π/len)
        double sd = 0.0;   // sin(π/len)
    };
    static constexpr std::size_t kMaxSegments = 3;

    struct ValueSlope {
        double g;    // value at the next sample to render
        double gp;   // analytic derivative per sample there
    };

    [[nodiscard]] std::uint32_t msToSamples(float ms) const noexcept {   // N = max(1, round(ms·fs/1000))
        return static_cast<std::uint32_t>(
            std::max<long long>(1, std::llround(static_cast<double>(ms) * sampleRate_ / 1000.0)));
    }

    /// B = 10^(punchBoostDb/20) of the active Controls (read at arm time, C-8).
    [[nodiscard]] double boostGain() const noexcept {
        return std::pow(10.0, static_cast<double>(active_.punchBoostDb) / 20.0);
    }

    [[nodiscard]] static Segment holdSeg(double v) noexcept {
        Segment s{};
        s.kind = Segment::Kind::Hold;
        s.g0 = v;
        s.g1 = v;
        return s;
    }

    [[nodiscard]] static Segment cosineSeg(double g0, double g1, std::uint32_t len) noexcept {
        Segment s{};
        s.kind = Segment::Kind::Cosine;
        s.g0 = g0;
        s.a = g1 - g0;
        s.g1 = g1;
        s.len = len;
        s.cd = std::cos(kPiDouble / static_cast<double>(len));
        s.sd = std::sin(kPiDouble / static_cast<double>(len));
        return s;
    }

    [[nodiscard]] static Segment hermiteSeg(double g0, double gp0, double g1, std::uint32_t len) noexcept {
        Segment s{};
        s.kind = Segment::Kind::Hermite;
        s.g0 = g0;
        s.m0 = static_cast<double>(len) * gp0;
        s.g1 = g1;
        s.len = len;
        return s;
    }

    /// Replaces the program. Unused slots hold the last segment's value (never reached: every
    /// program ends with a Hold).
    void setProgram(const Segment& s0, const Segment& s1 = holdSeg(1.0), const Segment& s2 = holdSeg(1.0),
                    std::size_t count = 1) noexcept {
        seg_[0] = s0;
        seg_[1] = s1;
        seg_[2] = s2;
        segIdx_ = 0;
        segCount_ = count;
    }

    [[nodiscard]] static double segmentValue(const Segment& s) noexcept {
        switch (s.kind) {
            case Segment::Kind::Hold:
                return s.g0;
            case Segment::Kind::Cosine:
                return s.g0 + s.a * (0.5 * (1.0 - s.rc));   // n = 0: g0 exactly
            case Segment::Kind::Hermite: {
                const double t = static_cast<double>(s.n) / static_cast<double>(s.len);
                const double t2 = t * t;
                const double t3 = t2 * t;
                const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
                const double h10 = t3 - 2.0 * t2 + t;
                const double h01 = -2.0 * t3 + 3.0 * t2;
                return h00 * s.g0 + h10 * s.m0 + h01 * s.g1;   // n = 0: g0 exactly
            }
        }
        return s.g0;
    }

    [[nodiscard]] static double segmentSlope(const Segment& s) noexcept {
        switch (s.kind) {
            case Segment::Kind::Hold:
                return 0.0;
            case Segment::Kind::Cosine:   // a·π/(2·len)·sin(π·n/len)
                return s.a * kPiDouble / (2.0 * static_cast<double>(s.len)) * s.rs;
            case Segment::Kind::Hermite: {
                const double t = static_cast<double>(s.n) / static_cast<double>(s.len);
                const double t2 = t * t;
                const double d00 = 6.0 * t2 - 6.0 * t;
                const double d10 = 3.0 * t2 - 4.0 * t + 1.0;
                const double d01 = -6.0 * t2 + 6.0 * t;
                return (d00 * s.g0 + d10 * s.m0 + d01 * s.g1) / static_cast<double>(s.len);
            }
        }
        return 0.0;
    }

    /// (g, g') at the next sample to render: where a bridge starts, so g and g' are continuous (D-5).
    [[nodiscard]] ValueSlope currentValueSlope() const noexcept {
        const Segment& s = seg_[segIdx_];
        return ValueSlope{.g = segmentValue(s), .gp = segmentSlope(s)};
    }

    /// Returns g at the current sample, then advances (S4.7). At a segment's end the next segment
    /// starts at n = 0, whose value is the stored end value exactly.
    [[nodiscard]] double envelopeStep() noexcept {
        Segment& s = seg_[segIdx_];
        const double g = segmentValue(s);
        g_ = g;
        if (s.kind != Segment::Kind::Hold) {
            ++s.n;
            if (s.kind == Segment::Kind::Cosine) {
                const double rc = s.rc * s.cd - s.rs * s.sd;
                s.rs = s.rs * s.cd + s.rc * s.sd;
                s.rc = rc;
            }
            if (s.n >= s.len && segIdx_ + 1 < segCount_)
                ++segIdx_;
        }
        return g;
    }

    /// S1 C-3: the cubic from (g0, m0 = N·g'0) to (g1, 0) has its interior extrema at the roots in
    /// (0, 1) of (3m0 − 6Δ)t² + (6Δ − 4m0)t + m0; it fits iff the cubic at each lies in [lo, hi].
    /// The end slope is 0 by construction, so the coefficients sum to 0 and the quadratic factors
    /// exactly as (t − 1)·((3m0 − 6Δ)t − m0): t = 1 is the segment's end (value g1, in range), and
    /// the only candidate interior extremum is t* = m0/(3m0 − 6Δ). Solving the quadratic numerically
    /// instead returns the t = 1 root as 1 − ε under /fp:fast or -ffast-math, where the cubic reads
    /// g1 + 1 ulp > hi and a fitting bridge is wrongly shortened (N_b 35 for 45 at the PUNCH mid-rise).
    [[nodiscard]] static bool bridgeFits(double g0, double gp0, double g1, std::uint32_t len, double lo,
                                         double hi) noexcept {
        const double m0 = static_cast<double>(len) * gp0;
        const double qa = 3.0 * m0 - 6.0 * (g1 - g0);
        if (qa == 0.0)
            return true;   // g' ∝ −m0·(t − 1): no interior extremum
        const double t = m0 / qa;
        if (!(t > 0.0 && t < 1.0))
            return true;   // not interior
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double v = (2.0 * t3 - 3.0 * t2 + 1.0) * g0 + (t3 - 2.0 * t2 + t) * m0 + (-2.0 * t3 + 3.0 * t2) * g1;
        return v >= lo && v <= hi;
    }

    /// S1 C-3: N_b = N_nominal if it fits, else the largest N in [1, N_nominal) that fits, by integer
    /// bisection (≤ 1 + ⌈log2 N_nominal⌉ evaluations). Excursion is monotone in m0 = N·g'0, so fits
    /// is monotone in N. N_b ≥ 1. The range is [0, max(B, g0)], B from the active Controls (≥ 1).
    [[nodiscard]] std::uint32_t bridgeLength(double g0, double gp0, double g1, std::uint32_t nominal) const noexcept {
        const double hi = std::max(boostGain(), g0);
        if (bridgeFits(g0, gp0, g1, nominal, 0.0, hi))
            return nominal;
        std::uint32_t good = 1;          // N = 1 is used even if its continuous cubic does not fit: only
        std::uint32_t bad = nominal;     // its two end samples (g0 and g1, both in range) are rendered
        while (bad - good > 1) {
            const std::uint32_t mid = good + (bad - good) / 2;
            if (bridgeFits(g0, gp0, g1, mid, 0.0, hi))
                good = mid;
            else
                bad = mid;
        }
        return good;
    }

    /// S4.7 snap arm (Reset noteOn or from not sounding carrying a re-arm).
    void snapArm() noexcept {
        switch (active_.mode) {
            case Mode::Lock:
            case Mode::Follow:
                setProgram(holdSeg(1.0));
                return;
            case Mode::Punch: {
                const double b = boostGain();
                setProgram(cosineSeg(1.0, b, nRise_), cosineSeg(b, 1.0, msToSamples(active_.punchTimeMs)),
                           holdSeg(1.0), 3);
                return;
            }
            case Mode::Ghost:
                setProgram(cosineSeg(0.0, 1.0, msToSamples(active_.ghostTimeMs)), holdSeg(1.0), holdSeg(1.0), 2);
                return;
        }
    }

    /// S4.7 bridged arm (a re-arm on a sounding FreeRunning voice): Hermite from the current (g, g').
    void bridgedArm() noexcept {
        const ValueSlope v = currentValueSlope();
        switch (active_.mode) {
            case Mode::Lock:
            case Mode::Follow:
                bridgeToRest();
                return;
            case Mode::Punch: {
                const double b = boostGain();
                const Segment fall = cosineSeg(b, 1.0, msToSamples(active_.punchTimeMs));
                if (v.g == b && v.gp == 0.0) {   // already at B at rest: nothing to bridge
                    setProgram(fall, holdSeg(1.0), holdSeg(1.0), 2);
                    return;
                }
                setProgram(hermiteSeg(v.g, v.gp, b, bridgeLength(v.g, v.gp, b, nRise_)), fall, holdSeg(1.0), 3);
                return;
            }
            case Mode::Ghost: {
                const Segment emerge = cosineSeg(0.0, 1.0, msToSamples(active_.ghostTimeMs));
                if (v.g == 0.0 && v.gp == 0.0) {   // already at 0 at rest
                    setProgram(emerge, holdSeg(1.0), holdSeg(1.0), 2);
                    return;
                }
                setProgram(hermiteSeg(v.g, v.gp, 0.0, bridgeLength(v.g, v.gp, 0.0, nDip_)), emerge, holdSeg(1.0),
                           3);
                return;
            }
        }
    }

    /// FR-044 mode switch (and a LOCK / FOLLOW bridged arm): Hermite to 1 over kModeBridgeMs, then
    /// Hold(1); nothing to bridge (Hold(1)) when g = 1 and g' = 0.
    void bridgeToRest() noexcept {
        const ValueSlope v = currentValueSlope();
        if (v.g == 1.0 && v.gp == 0.0) {
            setProgram(holdSeg(1.0));
            return;
        }
        setProgram(hermiteSeg(v.g, v.gp, 1.0, bridgeLength(v.g, v.gp, 1.0, nModeBridge_)), holdSeg(1.0),
                   holdSeg(1.0), 2);
    }

    [[nodiscard]] bool envelopeFinite() const noexcept {
        for (const Segment& s : seg_) {
            if (!(detail::isFinite(s.g0) && detail::isFinite(s.a) && detail::isFinite(s.m0) && detail::isFinite(s.g1)
                  && detail::isFinite(s.rc) && detail::isFinite(s.rs)))
                return false;
        }
        return true;
    }

    // ---- S4.1 state (fixed-size, trivially copyable) ----
    bool prepared_ = false;
    double sampleRate_ = 0.0;
    float capHz_ = 0.0f;
    float invFsF_ = 0.0f;
    float taperStartHz_ = 0.0f;   // S4.2, as evaluateMask :162
    float tailFloor_ = 0.0f;      // S4.2, as evaluateMask :163

    Controls pending_{};
    Controls active_{};
    bool controlsDirty_ = false;

    RetriggerPhase policy_ = RetriggerPhase::Reset;
    bool sounding_ = false;
    bool noteOnPending_ = false;
    bool noteOnRearm_ = true;
    float noteOnF0_ = kDefaultF0Hz;

    float heldF0_ = kDefaultF0Hz;
    float prevSampleF0_ = kDefaultF0Hz;
    float heldRatio_ = 1.0f;
    float prevSampleRatio_ = 1.0f;
    std::uint32_t intervalPhase_ = 0;

    // Mirror (S5), n = 1 only.
    float mBase_ = kDefaultF0Hz;
    double mEpsBase_ = 0.0;
    float mBankEps_ = 0.0f;
    float mShadow_ = 1.0f;
    float mCurrentF_ = kDefaultF0Hz;
    float mMaskF_ = 0.0f;
    float mEpsEff_ = 0.0f;
    double deltaLock_ = 0.0;

    // Frequency and phase.
    double phi2_ = 0.0;      // [0, 2): two cycles of the fundamental (octave-down, S4.5)
    double delta_ = 0.0;     // δ in force (cycles per sample)
    double lastInc_ = 0.0;   // δ applied after the last rendered sample (phaseIncrement)
    double deltaFrom_ = 0.0;            // S4.10 r glide: δ at its start
    double deltaTo_ = 0.0;              // S4.10 r glide: δ_tgt
    std::uint32_t glideLeft_ = 0;       // samples of the glide still to advance (0: none)
    std::uint32_t glideLen_ = 0;        // the glide's length (16 from a grid sample)
    float rEffPrev_ = 1.0f;             // r_eff at the last frequencyUpdate
    bool snapRatio_ = true;             // FR-006: the next frequencyUpdate snaps (Reset noteOn)

    // Waveforms (S4.5): tapered triangle coefficients and effective a3 / a2, keyed on f_T.
    std::array<double, kTaperTableSize> centsOfN_{};        // 1200·log2(n), n = 1..31 (T005 cache)
    std::array<double, kTaperTableSize> taperFactorOfN_{};  // 2^(−s·cents(n))
    std::array<double, kTriangleTerms> triBase_{};          // untapered a_k (the fast-path constants)
    std::array<double, kTriangleTerms> triCoef_{};          // tapered a_k in force
    std::size_t triActive_ = kTriangleTerms;                // K_a
    double triScale_ = 0.0;                                 // 1/√(Σ a_k²/2)
    double a3Eff_ = 0.25;
    double a2Eff_ = 0.25;
    float taperFr_ = -1.0f;   // f_T at the last refresh (−1: none yet)
    bool taperFast_ = false;

    // Crossfade (S4.8, C-8 / E-7).
    Waveform current_ = Waveform::Sine;
    Waveform next_ = Waveform::Sine;
    Waveform queuedWave_ = Waveform::Sine;
    bool xfadeActive_ = false;
    bool queued_ = false;
    std::uint32_t xfadeN_ = 0;
    std::uint32_t xfadeLen_ = 1;
    double xRc_ = 1.0;   // cos(π·n/N_x)
    double xRs_ = 0.0;   // sin(π·n/N_x)
    double xCd_ = 1.0;   // cos(π/N_x)
    double xSd_ = 0.0;   // sin(π/N_x)

    // Asymmetric-sine onset ramp (C-6); onsetLen_ == 0: inactive.
    std::uint32_t onsetN_ = 0;
    std::uint32_t onsetLen_ = 0;

    // Mode envelope (S4.7) and gain.
    std::array<Segment, kMaxSegments> seg_{};
    std::size_t segIdx_ = 0;
    std::size_t segCount_ = 1;
    std::uint32_t nRise_ = 1;         // kPunchRiseMs in samples
    std::uint32_t nDip_ = 1;          // kGhostDipMs in samples
    std::uint32_t nModeBridge_ = 1;   // kModeBridgeMs in samples
    double g_ = 1.0;                  // g at the last rendered sample (modeGain)
    OnePoleSmoother levelS_{kDefaultLevel};       // S4.9
    OnePoleSmoother a1S_{kMaxCoreH1Amplitude};    // S4.9
    float a1Held_ = kMaxCoreH1Amplitude;
};

}  // namespace Krate::DSP
