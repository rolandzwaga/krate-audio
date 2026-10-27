// ==============================================================================
// Layer 3: System - Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// ShapeMorph: the SHAPE morph stage, ROUND (0) / HOLLOW (0.5) / BITE (1) behind
// one control (plan S6.2-S6.4; C-6, C-13, C-16, C-22, C-23; deviation 9).
//   Carries: FR-010..FR-016, FR-018, FR-019, FR-052, FR-056; SC-001, SC-002,
//            SC-003, SC-004, SC-008, SC-010, SC-011, SC-013(a) (Shape arms).
//   Spec:  specs/profundum-phase5-shape-character/spec.md
//   Plan:  specs/profundum-phase5-shape-character/plan.md
//   Tests: dsp/tests/unit/systems/shape_morph_test.cpp,
//          dsp/tests/unit/systems/shape_morph_spectral_test.cpp
//
// Includes L0-L2 only.
//
// Per base sample and channel (plan S2, S6.3 with C-23: only the nonlinearities
// run at the oversampled rate F·fs, every linear filter runs at fs):
//   s  = smoothed SHAPE, t = min(1, 2s), t2 = max(0, 2s - 1)
//   w_R = s <= 1/2 ? 1 - t : 0,  w_H = s <= 1/2 ? t : 1 - t2,  w_B = t2
//   xin = BITE on ? Tilt[4·f0, +12·t2 dB](x) : x                 (base rate, C-23)
//   u   = OversampledResidualPath::upsample(xin)                  (F samples)
//   per F-sample: l = u[n-1] (the one linear tap of every regime; ROUND's
//     ADAA1 1/2-sample offset falls into the residual)
//     ROUND: yR = (P/0.8)·TanhADAA[0.8](u/P)
//     BITE:  yB = P·DiodeAdaa2(g_B·u/P) / c_B(t2)                 (deviation 9)
//     lin = w_NL·l,  res = w_NL·(y_NL - l)   (w_R and w_B are never both > 0)
//   nl = path.downsample(lin, res) = w_NL·lb + rb  (lb: round-tripped l, rb: the
//        notched, DC-blocked residual)
//   z  = (ROUND on ? SVF_LP[10·2^t·f0, Q 0.707](nl) : nl)          (base rate)
//      + w_H·(SVF_A.notch(h) + 1.0·SVF_B.band(h)),   h = BITE on ? Inv(lb) : lb
//   z *= 10^{L(s)/20}                     (kLoudnessTableDb, after the downsample)
//   y  = (1 - e)·x + e·z, e = 10 ms linear engage ramp; clamp ±kOutputCeiling.
//
// HOLLOW's tap lb (T010 design item, evidence/run-log/T010.txt):
// - OversampledResidualPath::downsample returns lb + rb and keeps lb
//   (lastLinearBase, Phase 9 T029); HOLLOW reads it. Until T029 a second
//   PolyphaseHalfbandOversampler (hollowDown) decimated the same l through the
//   same filter with the same history, i.e. produced the same bits.
// - C-23 gap: with BITE's emphasis before the shared upsample, lb carries it while
//   BITE and HOLLOW are both on (s in (0.5, 1)). Inv is the exact first-order
//   inverse of the emphasis tilt, run at fs; it runs whenever HOLLOW is on, keyed
//   flat (unity) while BITE is off, and is not reset at BITE entry, so its state
//   fades instead of being dropped at the handover (C-32). The tilt is
//   H = gH + (gL - gH)·(1 - a)/(1 - a z^-1), so
//   1/H = (1 - a z^-1) / (b0 - gH·a z^-1),  b0 = gL·(1 - a) + gH·a,
//   minimum phase (pole gH·a/b0 < 1). a, gL, gH are recomputed with the tilt's own
//   formulas (keytracked_tilt.h updateGains, one_pole.h clamp and coefficient).
//
// Regime weights are applied after the shared path and after ROUND's low-pass
// (C-32, main-loop fix 2026-10-16): a leaving regime's filter memory fades with its
// weight instead of being cut at the s = 0.5 handover (measured before the fix: a
// one-sample step of 0.088 on kSawAnchor E0, 3x the steady grid's max |dy|).
//
// ROUND and BITE are reset on entry (weight 0 -> > 0) and re-keyed with the current f0
// and s before their first sample (S6.3 Edge Case); HOLLOW's linear chain runs warm from
// prepare and is never reset on entry (C-32). Every keytracked multiple,
// g_B, c_B, the inverse and the loudness gain are recomputed whenever the
// smoothed s or f0 changes bit-wise (per sample during a ramp, S8).
//
// Engage (C-22): the processed path runs warm on every sample after prepare,
// enabled or not; disabling only ramps e to 0. At e == 0 the output is the
// sanitised input copied bit-identically (SC-008); at e == 1 it is z.
//
// Mono link (plan S2, HarmonicFold's mechanism): while the sanitised L and R
// inputs are bit-identical channel 0 runs alone and is copied; at the first
// difference channel 1 takes channel 0's state. It stays unlinked until reset().
//
// RT: heap-free, fixed-size members, every audio-thread method noexcept; only
// prepare() fills cBiteShape_ (65 x 256 diode evaluations). Precondition: FTZ/DAZ
// on (the halfband sections are not flushed per call).
// ==============================================================================

#pragma once

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/keytracked_tilt.h>
#include <krate/dsp/processors/oversampled_residual_path.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

namespace detail {
struct ShapeMorphProbe;
}

class ShapeMorph {
public:
    static constexpr float kRoundDrive = 0.8f;                 // TanhADAA drive at the calibration peak
    static constexpr float kRoundCutoffLo = 10.0f, kRoundCutoffHi = 20.0f;  // × f0, log-interpolated over SHAPE [0, 0.5]
    static constexpr float kRoundQ = 0.70710678f;
    static constexpr float kHollowNotchMultiple = 3.0f, kHollowNotchQ = 1.0f;
    static constexpr float kHollowPeakMultiple = 9.0f, kHollowPeakQ = 3.0f, kHollowPeakGain = 1.0f;
    static constexpr float kBitePivotMultiple = 4.0f, kBiteMaxEmphasisDb = 12.0f;
    static constexpr float kBiteDriveLoDb = -12.0f, kBiteDriveHiDb = 15.0f;
    static constexpr float kShapeSmoothMs = 10.0f, kEngageMs = 10.0f, kOutputCeiling = 6.0f;

    // Equal-loudness table over SHAPE = i / 100 (FR-056, C-6): the T024 close-lane harvest
    // (ShapeMorph_LoudnessTableHarvest, mean of kSawAnchor / kTriangleAnchor / kSineAnchor at
    // E1 / C2 / C3; evidence/harvest-loudness.txt), pasted by paste-harvest.js between the
    // markers on 2026-10-16. Model seed was 1.31, 1.26, 0.80, 3.08, 3.76 dB at 0 / 0.25 / 0.5 / 0.75 / 1.
    // HARVEST-BEGIN kLoudnessTableDb
    static constexpr std::array<float, 101> kLoudnessTableDb{
        1.3090f, 1.3159f, 1.3221f, 1.3277f, 1.3325f, 1.3366f, 1.3399f, 1.3425f, 1.3443f, 1.3454f,
        1.3456f, 1.3452f, 1.3439f, 1.3419f, 1.3390f, 1.3355f, 1.3311f, 1.3260f, 1.3201f, 1.3135f,
        1.3061f, 1.2981f, 1.2892f, 1.2797f, 1.2695f, 1.2585f, 1.2469f, 1.2346f, 1.2217f, 1.2081f,
        1.1939f, 1.1790f, 1.1636f, 1.1475f, 1.1309f, 1.1138f, 1.0960f, 1.0778f, 1.0590f, 1.0396f,
        1.0198f, 0.9995f, 0.9787f, 0.9575f, 0.9358f, 0.9137f, 0.8911f, 0.8681f, 0.8448f, 0.8210f,
        0.7968f, 0.8020f, 0.8145f, 0.8345f, 0.8623f, 0.8980f, 0.9416f, 0.9931f, 1.0527f, 1.1203f,
        1.1957f, 1.2787f, 1.3695f, 1.4674f, 1.5724f, 1.6843f, 1.8026f, 1.9270f, 2.0570f, 2.1925f,
        2.3327f, 2.4771f, 2.6252f, 2.7762f, 2.9295f, 3.0841f, 3.2392f, 3.3937f, 3.5465f, 3.6963f,
        3.8418f, 3.9814f, 4.1136f, 4.2366f, 4.3488f, 4.4485f, 4.5340f, 4.6033f, 4.6554f, 4.6891f,
        4.7031f, 4.6969f, 4.6703f, 4.6233f, 4.5556f, 4.4685f, 4.3624f, 4.2382f, 4.0970f, 3.9402f,
        3.7691f};
    // HARVEST-END kLoudnessTableDb

    // -------------------------------------------------------------------------
    // Control thread
    // -------------------------------------------------------------------------

    /// Prepares every regime member at its rate, fills cBiteShape_, resets and re-keys every
    /// regime with the current f0. maxBlockSize is unused: the stage is per-sample and accepts
    /// any numSamples.
    void prepare(double sampleRate, [[maybe_unused]] std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        const auto fs = static_cast<float>(sampleRate);

        for (std::size_t i = 0; i < kBiteTablePoints; ++i) {
            const double t2 = static_cast<double>(i) / static_cast<double>(kBiteTablePoints - 1);
            cBiteShape_[i] = static_cast<float>(diodeH1Coefficient(biteDriveGainD(t2)));
        }

        for (auto& ch : channels_) {
            ch.path.prepare(sampleRate);
            ch.roundAdaa.setDrive(kRoundDrive);
            // C-23: ROUND's low-pass, HOLLOW's notch + band and BITE's emphasis run at fs.
            ch.roundLp.prepare(sampleRate);
            ch.roundLp.setMode(SVFMode::Lowpass);
            ch.roundLp.setResonance(kRoundQ);
            ch.hollowNotch.prepare(sampleRate);
            ch.hollowNotch.setMode(SVFMode::Notch);
            ch.hollowNotch.setResonance(kHollowNotchQ);
            ch.hollowBand.prepare(sampleRate);
            ch.hollowBand.setMode(SVFMode::Bandpass);
            ch.hollowBand.setResonance(kHollowPeakQ);
            ch.biteTilt.prepare(sampleRate);
            ch.biteTilt.configure(kBitePivotMultiple, kBiteMaxEmphasisDb);
        }

        shapeSm_.configure(kShapeSmoothMs, fs);
        shapeSm_.setTarget(shape_);
        engage_.configure(kEngageMs, fs);
        engage_.setTarget(enabled_ ? 1.0f : 0.0f);
        prepared_ = true;
        reset();
    }

    /// Clears every filter / oversampler / ADAA / tilt state, snaps the SHAPE smoother and the
    /// engage ramp to their targets, re-links the channels and re-keys every regime.
    void reset() noexcept {
        for (auto& ch : channels_)
            resetChannel(ch);
        shapeSm_.snapToTarget();
        engage_.snapToTarget();
        roundOn_ = false;
        hollowOn_ = false;
        biteOn_ = false;
        runningRegimes_ = 0;
        maxRunningRegimes_ = 0;
        linked_ = true;
        peak_ = 0.0f;
        if (prepared_) {
            const float s = shapeSm_.getCurrentValue();
            computeControls(s, true, true);
            for (auto& ch : channels_)
                keyChannel(ch, true, true, true);
        }
        needsRekey_ = true;
    }

    /// [0, 1]; non-finite keeps the previous value; a denormal becomes 0.
    void setShape(float position) noexcept {
        shape_ = sanitizeControl(position, shape_, 0.0f, 1.0f);
        shapeSm_.setTarget(shape_);
        if (seamUnsmoothed_)
            shapeSm_.snapToTarget();
    }

    /// D-2. Engaging and disengaging ramp e linearly over kEngageMs (C-16).
    void setEnabled(bool enabled) noexcept {
        enabled_ = enabled;
        engage_.setTarget(enabled ? 1.0f : 0.0f);
    }

    /// The per-block fallback of FR-052. Clamped to [8, 20000] Hz; non-finite keeps the
    /// previous value.
    void setFundamentalHz(float hz) noexcept {
        f0_ = sanitizeControl(hz, f0_, kMinFundamentalHz, kMaxFundamentalHz);
    }

    // -------------------------------------------------------------------------
    // Audio thread
    // -------------------------------------------------------------------------

    /// Any numSamples. Before prepare, with a null channel or with 0 samples the buffers are
    /// left untouched. right == left processes one channel with channel 0's state.
    /// f0PerSample (FR-052): when non-null, entry n is the f0 of sample n (clamped as in
    /// setFundamentalHz; a non-finite entry keeps the previous f0).
    void processBlock(float* left, float* right, std::size_t numSamples,
                      const float* f0PerSample = nullptr) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0)
            return;
        const std::size_t numChannels = (left == right) ? std::size_t{1} : std::size_t{2};
        const std::array<float*, 2> io{left, right};

        for (std::size_t n = 0; n < numSamples; ++n) {
            if (f0PerSample != nullptr)
                f0_ = sanitizeControl(f0PerSample[n], f0_, kMinFundamentalHz, kMaxFundamentalHz);

            std::array<float, 2> x{};
            for (std::size_t c = 0; c < numChannels; ++c)
                x[c] = sanitizeSample(io[c][n]);
            if (linked_ && numChannels == 2
                && std::bit_cast<std::uint32_t>(x[0]) != std::bit_cast<std::uint32_t>(x[1])) {
                channels_[1] = channels_[0];
                linked_ = false;
            }
            const std::size_t active = linked_ ? std::size_t{1} : numChannels;

            const float s = shapeSm_.process();
            const float e = engage_.process();
            const Weights w = weightsAt(s);

            // Regime entry (S6.3): reset on 0 -> > 0, then re-key before the first sample.
            bool rekey = needsRekey_;
            const bool roundEnters = (w.round > 0.0f) && !roundOn_;
            const bool hollowEnters = (w.hollow > 0.0f) && !hollowOn_;
            const bool biteEnters = (w.bite > 0.0f) && !biteOn_;
            roundOn_ = w.round > 0.0f;
            hollowOn_ = w.hollow > 0.0f;
            biteOn_ = w.bite > 0.0f;
            if (roundEnters || hollowEnters || biteEnters) {
                for (std::size_t c = 0; c < active; ++c)
                    enterRegimes(channels_[c], roundEnters, hollowEnters, biteEnters);
                rekey = true;
            }
            const std::uint32_t sBits = std::bit_cast<std::uint32_t>(s);
            const std::uint32_t f0Bits = std::bit_cast<std::uint32_t>(f0_);
            if (rekey || sBits != appliedShapeBits_ || f0Bits != appliedF0Bits_) {
                computeControls(s, roundOn_, biteOn_);
                for (std::size_t c = 0; c < active; ++c)
                    keyChannel(channels_[c], roundOn_, true, biteOn_);  // C-32: HOLLOW always keyed
                appliedShapeBits_ = sBits;
                appliedF0Bits_ = f0Bits;
                needsRekey_ = false;
            }
            runningRegimes_ = static_cast<int>(roundOn_) + static_cast<int>(hollowOn_)
                              + static_cast<int>(biteOn_);
            maxRunningRegimes_ = std::max(maxRunningRegimes_, runningRegimes_);

            for (std::size_t c = 0; c < active; ++c) {
                // C-22: the processed path runs warm whether or not the stage is engaged.
                const float z = processChannel(channels_[c], x[c], w);
                if (e == 0.0f) {
                    peak_ = std::max(peak_, std::abs(x[c]));
                    io[c][n] = x[c];  // the bypass copy, selected at the output (SC-008)
                    continue;
                }
                const float y = (e == 1.0f) ? z : (1.0f - e) * x[c] + e * z;
                peak_ = std::max(peak_, std::abs(y));
                io[c][n] = std::clamp(y, -kOutputCeiling, kOutputCeiling);
            }
            if (active < numChannels)
                io[1][n] = io[0][n];
        }
    }

    /// True when every piece of recurrent state is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!detail::isFinite(shapeSm_.getCurrentValue()) || !detail::isFinite(engage_.getCurrentValue()))
            return false;
        for (const auto& ch : channels_) {
            if (!ch.path.stateFinite() || !ch.biteAdaa.stateFinite())
                return false;
            if (!ch.inverse.stateFinite())
                return false;
            if (!detail::isFinite(ch.uPrev) || !detail::isFinite(ch.roundAdaaOut)
                || !detail::isFinite(ch.roundOut) || !detail::isFinite(ch.hollowOut)
                || !detail::isFinite(ch.biteTiltOut) || !detail::isFinite(ch.biteOut))
                return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    /// Largest |y| before the kOutputCeiling clamp since prepare / reset.
    [[nodiscard]] float preCeilingPeak() const noexcept { return peak_; }

private:
    friend struct detail::ShapeMorphProbe;

    static constexpr float kP = HarmonicFold::kCalibrationPeak;  // calibration peak P (harmonic_fold.h:83)
    static constexpr float kInvP = 1.0f / kP;
    static constexpr float kRoundOutGain = kP / kRoundDrive;     // unity small-signal gain of ROUND's drive
    static constexpr float kMinFundamentalHz = 8.0f;             // FR-052 clamp
    static constexpr float kMaxFundamentalHz = 20000.0f;
    static constexpr float kDefaultFundamentalHz = 65.40639f;    // C2 until the owner sets f0 (deviation 13)
    static constexpr std::size_t kBiteTablePoints = 65;
    static constexpr std::size_t kMaxFactor = OversampledResidualPath::kMaxFactor;

    struct Weights {
        float round = 1.0f;
        float hollow = 0.0f;
        float bite = 0.0f;
    };

    /// The exact first-order inverse of BITE's emphasis tilt, for HOLLOW's tap (C-23 gap):
    /// y[n] = (x[n] - a·x[n-1]) / b0 + (gH·a / b0)·y[n-1].
    /// The SHAPE ramp (C-33): a restartable 10 ms raised-cosine from the current value to the target,
    /// so a move starts and ends with zero slope. The one-pole it replaces reached 99 % in 10 ms, which
    /// put its steepest slope (1 / 104 samples) at the first sample of every move; on kSawAnchor E0 the
    /// BITE -> HOLLOW blend (outputs 0.32 apart at the same instant) then moved 0.010 per sample against
    /// references at 0.003 and counted as a click (SC-003). Same API as OnePoleSmoother for the owner.
    struct ShapeRamp {
        float from = 0.0f;
        float target = 0.0f;
        float current = 0.0f;
        std::size_t count = 0;
        std::size_t len = 1;
        void configure(float ms, float sampleRate) noexcept {
            len = static_cast<std::size_t>(std::max(1.0f, std::round(ms * 0.001f * sampleRate)));
        }
        void setTarget(float t) noexcept {
            if (t == target && (count >= len || current == target))
                return;
            if (t == target)
                return;  // same target mid-ramp: keep the ramp in flight
            from = current;
            target = t;
            count = 0;
        }
        void snapToTarget() noexcept {
            current = target;
            from = target;
            count = len;
        }
        [[nodiscard]] float getCurrentValue() const noexcept { return current; }
        [[nodiscard]] float getTarget() const noexcept { return target; }
        float process() noexcept {
            if (count >= len) {
                current = target;
                return current;
            }
            ++count;
            const float p = static_cast<float>(count) / static_cast<float>(len);
            const float w = 0.5f * (1.0f - std::cos(0.5f * kTwoPi * p));
            current = (count >= len) ? target : from + (target - from) * w;
            return current;
        }
    };

    struct InverseEmphasis {
        float invB0 = 1.0f;
        float a = 0.0f;
        float pole = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;

        void set(float coeff, float gL, float gH) noexcept {
            a = coeff;
            invB0 = 1.0f / (gL * (1.0f - coeff) + gH * coeff);
            pole = gH * coeff * invB0;
        }
        void reset() noexcept {
            x1 = 0.0f;
            y1 = 0.0f;
        }
        [[nodiscard]] float process(float x) noexcept {
            const float y = detail::flushDenormal(invB0 * (x - a * x1) + pole * y1);
            x1 = x;
            y1 = y;
            return y;
        }
        [[nodiscard]] bool stateFinite() const noexcept { return detail::isFinite(x1) && detail::isFinite(y1); }
    };

    struct Channel {
        OversampledResidualPath path;
        // ROUND
        TanhADAA roundAdaa;
        SVF roundLp;
        // HOLLOW
        SVF hollowNotch;
        SVF hollowBand;
        InverseEmphasis inverse;
        // BITE
        ClosedFormAdaa2<DiodeAdaaLaw> biteAdaa;
        KeytrackedTilt biteTilt;
        float uPrev = 0.0f;  // l = u[n-1] at the oversampled rate (the alignment tap)
        // Mirrors of each regime's last output (the probe's regimeStateWarm and stateFinite).
        float roundAdaaOut = 0.0f;
        float roundOut = 0.0f;
        float hollowOut = 0.0f;
        float biteTiltOut = 0.0f;
        float biteOut = 0.0f;
    };

    /// Non-finite keeps `previous`; a denormal (or signed zero) bit pattern becomes +0; then
    /// clamp to [lo, hi] (harmonic_fold.h sanitizeControl).
    [[nodiscard]] static float sanitizeControl(float v, float previous, float lo, float hi) noexcept {
        if (!detail::isFinite(v))
            return previous;
        if ((std::bit_cast<std::uint32_t>(v) & 0x7F800000u) == 0u)
            v = 0.0f;
        return std::min(std::max(lo, v), hi);
    }

    /// A non-finite input sample becomes 0.
    [[nodiscard]] static float sanitizeSample(float v) noexcept { return detail::isFinite(v) ? v : 0.0f; }

    /// g_B(t2) = 10^{(kBiteDriveLoDb + (kBiteDriveHiDb - kBiteDriveLoDb)·t2)/20} (S6.3).
    [[nodiscard]] static double biteDriveGainD(double t2) noexcept {
        const double db = static_cast<double>(kBiteDriveLoDb)
                          + static_cast<double>(kBiteDriveHiDb - kBiteDriveLoDb) * t2;
        return std::pow(10.0, db / 20.0);
    }

    /// S6.3 weights at the smoothed SHAPE s (the hard-regime seam keeps only the nearest anchor).
    [[nodiscard]] Weights weightsAt(float s) const noexcept {
        if (seamHardRegime_) {
            if (s < 0.25f)
                return Weights{.round = 1.0f, .hollow = 0.0f, .bite = 0.0f};
            if (s <= 0.75f)
                return Weights{.round = 0.0f, .hollow = 1.0f, .bite = 0.0f};
            return Weights{.round = 0.0f, .hollow = 0.0f, .bite = 1.0f};
        }
        const float t = std::min(1.0f, 2.0f * s);
        const float t2 = std::max(0.0f, 2.0f * s - 1.0f);
        return Weights{.round = (s <= 0.5f) ? 1.0f - t : 0.0f,
                       .hollow = (s <= 0.5f) ? t : 1.0f - t2,
                       .bite = t2};
    }

    /// Everything keyed by (s, f0), computed once per change and shared by both channels.
    /// ROUND's cutoff and BITE's controls (with the inverse) are computed only for running
    /// regimes; an entering regime triggers a recompute before its first sample.
    void computeControls(float s, bool round, bool bite) noexcept {
        const float t = std::min(1.0f, 2.0f * s);
        const float t2 = std::max(0.0f, 2.0f * s - 1.0f);
        if (round)
            roundCutoffHz_ = kRoundCutoffLo * std::exp2(t) * f0_;  // k_R = 10·2^t
        if (bite)
            computeBiteControls(t2);
        computeInverse(bite ? t2 : 0.0f);  // C-32: HOLLOW's inverse is keyed flat while BITE is off
        // Loudness (FR-056): L(s) linearly interpolated over the 101-point table.
        if (seamLoudnessBypassed_) {
            loudnessGain_ = 1.0f;
        } else {
            const float lp = s * 100.0f;
            const std::size_t j = std::min(static_cast<std::size_t>(lp), std::size_t{99});
            const float lf = lp - static_cast<float>(j);
            const float db = kLoudnessTableDb[j] + lf * (kLoudnessTableDb[j + 1] - kLoudnessTableDb[j]);
            loudnessGain_ = std::pow(10.0f, db / 20.0f);
        }
    }

    /// BITE's tone, drive gain g_B, output scale P / c_B and the emphasis inverse at t2.
    void computeBiteControls(float t2) noexcept {
        biteTone_ = 0.5f * (1.0f + t2);                        // +12·t2 dB emphasis span around 4·f0
        biteGain_ = static_cast<float>(biteDriveGainD(static_cast<double>(t2)));
        // c_B(t2): linear interpolation in the 65-point table filled by prepare.
        const float pos = t2 * static_cast<float>(kBiteTablePoints - 1);
        const std::size_t i = std::min(static_cast<std::size_t>(pos), kBiteTablePoints - 2);
        const float frac = pos - static_cast<float>(i);
        const float cB = cBiteShape_[i] + frac * (cBiteShape_[i + 1] - cBiteShape_[i]);
        biteOutScale_ = kP / cB;
    }

    /// The inverse of the emphasis tilt at t2: the tilt's own a, gL, gH (keytracked_tilt.h
    /// updateGains in double; one_pole.h clampCutoff / updateCoefficient in float). t2 = 0 is unity.
    void computeInverse(float t2) noexcept {
        {
            // T032: the tilt's own float expressions (keytracked_tilt.h updateGains), same order, so the
            // inverse's gains are the tilt's bit for bit; the tilt's Tone is 0.5 · (1 + t2) and its
            // shelf depth kBiteMaxEmphasisDb, i.e. g = (2 · tone − 1) · maxDb = t2 · maxDb as here.
            const float g = (2.0f * (0.5f * (1.0f + t2)) - 1.0f) * kBiteMaxEmphasisDb;
            const float lo = std::exp2(-g * KeytrackedTilt::kLog2TenOver20);
            const float hi = std::exp2(g * KeytrackedTilt::kLog2TenOver20);
            const float nu = std::sqrt(2.0f / (lo * lo + hi * hi));
            inverseGL_ = lo * nu;
            inverseGH_ = hi * nu;
            const auto fsF = static_cast<float>(std::max(sampleRate_, 1000.0));
            float fc = kBitePivotMultiple * f0_;
            if (fc <= 0.0f)
                fc = 1.0f;
            fc = std::min(fc, fsF * 0.495f);
            inverseA_ = std::exp(-kTwoPi * fc / fsF);
        }
    }

    /// Applies computeControls' values to the selected regimes of one channel.
    void keyChannel(Channel& ch, bool round, bool hollow, bool bite) const noexcept {
        if (round)
            ch.roundLp.setCutoff(roundCutoffHz_);
        if (hollow) {
            ch.hollowNotch.setCutoff(kHollowNotchMultiple * f0_);
            ch.hollowBand.setCutoff(kHollowPeakMultiple * f0_);
            ch.inverse.set(inverseA_, inverseGL_, inverseGH_);  // C-32: keyed with HOLLOW
        }
        if (bite) {
            ch.biteTilt.setTone(biteTone_);
            ch.biteTilt.setFundamentalHz(f0_);
        }
    }

    /// Reset-on-entry (S6.3 Edge Case "SHAPE at 0, 0.5 or 1").
    static void enterRegimes(Channel& ch, bool round, bool hollow, bool bite) noexcept {
        if (round) {
            ch.roundAdaa.reset();
            ch.roundLp.reset();
            ch.roundAdaaOut = 0.0f;
            ch.roundOut = 0.0f;
        }
        // C-32: HOLLOW is not reset on entry; its linear chain runs warm from prepare, so there is no
        // cold start to weight in (measured before: its tap filled from zero in three samples while
        // its weight was already 2-8 %, a detectable step on kSawAnchor E0).
        static_cast<void>(hollow);
        if (bite) {  // C-32: the inverse keeps its state (it has been running flat under HOLLOW)
            ch.biteAdaa.reset();
            ch.biteTilt.reset();
            ch.biteTiltOut = 0.0f;
            ch.biteOut = 0.0f;
        }
    }

    static void resetChannel(Channel& ch) noexcept {
        ch.path.reset();
        ch.roundAdaa.reset();
        ch.roundLp.reset();
        ch.hollowNotch.reset();
        ch.hollowBand.reset();
        ch.inverse.reset();
        ch.biteAdaa.reset();
        ch.biteTilt.reset();
        ch.uPrev = 0.0f;
        ch.roundAdaaOut = 0.0f;
        ch.roundOut = 0.0f;
        ch.hollowOut = 0.0f;
        ch.biteTiltOut = 0.0f;
        ch.biteOut = 0.0f;
    }

    /// One base sample of one channel; returns the loudness-compensated z (before the engage mix).
    [[nodiscard]] float processChannel(Channel& ch, float x, const Weights& w) noexcept {
        float xin = x;
        if (biteOn_) {
            xin = ch.biteTilt.process(x);  // C-23: BITE's emphasis before the shared upsample
            ch.biteTiltOut = xin;
        }
        std::array<float, kMaxFactor> u{};
        std::array<float, kMaxFactor> lin{};
        std::array<float, kMaxFactor> res{};
        ch.path.upsample(xin, u.data());
        const std::size_t factor = ch.path.factor();
        const float wNl = roundOn_ ? w.round : (biteOn_ ? w.bite : 0.0f);
        for (std::size_t k = 0; k < factor; ++k) {
            const float l = ch.uPrev;  // l = 1·u[n-1] for every regime
            ch.uPrev = u[k];
            float yNl = l;
            if (roundOn_) {
                yNl = kRoundOutGain * ch.roundAdaa.processFast(u[k] * kInvP);
                ch.roundAdaaOut = yNl;
            } else if (biteOn_) {
                const double v = static_cast<double>(biteGain_ * u[k] * kInvP);
                yNl = static_cast<float>(ch.biteAdaa.process(v)) * biteOutScale_;  // P·adaa(v)/c_B (deviation 9)
                ch.biteOut = yNl;
            }
            lin[k] = l;
            res[k] = yNl - l;
        }
        // C-32: the regime weight is applied after the shared path and ROUND's low-pass, so a
        // leaving regime's filter memory fades with its weight instead of being dropped.
        const float nl = ch.path.downsample(lin.data(), res.data());  // lb + rb (unweighted)
        float zNl = nl;
        if (roundOn_) {
            zNl = ch.roundLp.process(nl);  // ROUND's low-pass at fs (C-23)
            ch.roundOut = zNl;
        }
        float z = wNl * zNl;
        {  // C-32: HOLLOW's linear chain runs warm from prepare (C-22 pattern); its weight alone switches it in
            const float lb = ch.path.lastLinearBase();   // D(l), the same bits hollowDown produced (T029)
            const float h = ch.inverse.process(lb);  // unity while BITE is off
            const float yH = ch.hollowNotch.processMulti(h).notch + kHollowPeakGain * ch.hollowBand.processMulti(h).band;
            ch.hollowOut = yH;
            z += w.hollow * yH;
        }
        return z * loudnessGain_;
    }

    std::array<Channel, 2> channels_{};
    std::array<float, kBiteTablePoints> cBiteShape_{};
    ShapeRamp shapeSm_;  // C-33: raised-cosine 10 ms ramp (was OnePoleSmoother, 99 % in 10 ms)
    LinearRamp engage_;

    double sampleRate_ = 48000.0;
    float shape_ = 0.0f;                     // ROUND by default (deviation 13)
    float f0_ = kDefaultFundamentalHz;
    bool enabled_ = true;                    // deviation 13
    bool prepared_ = false;
    bool linked_ = true;
    bool needsRekey_ = true;
    bool roundOn_ = false;
    bool hollowOn_ = false;
    bool biteOn_ = false;
    std::uint32_t appliedShapeBits_ = 0;
    std::uint32_t appliedF0Bits_ = 0;
    int runningRegimes_ = 0;
    int maxRunningRegimes_ = 0;
    float peak_ = 0.0f;

    // Controls keyed by (s, f0) (computeControls).
    float roundCutoffHz_ = kRoundCutoffLo * kDefaultFundamentalHz;
    float biteTone_ = 0.5f;
    float biteGain_ = 1.0f;
    float biteOutScale_ = 1.0f;
    float inverseA_ = 0.0f;
    float inverseGL_ = 1.0f;
    float inverseGH_ = 1.0f;
    float loudnessGain_ = 1.0f;

    // Seams (probe only; default off).
    bool seamUnsmoothed_ = false;        // SHAPE snaps (T013 positive control)
    bool seamHardRegime_ = false;        // weights become the nearest regime only (T013 positive control)
    bool seamLoudnessBypassed_ = false;  // kLoudnessTableDb not applied (T024 harvest)
};

} // namespace Krate::DSP
