// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 2 (specs/profundum-phase2-edge-fold)
// ==============================================================================
// HarmonicFold: the Profundum Fold stage. Includes Layer 0 and Layer 1 only.
//   Spec:  specs/profundum-phase2-edge-fold/spec.md (FR-020 series)
//   Plan:  specs/profundum-phase2-edge-fold/plan.md (S2, S4, S8.1, S8.2)
//   Carries: SC-005 (Fold arm), SC-006, SC-007, SC-008, SC-010, SC-011,
//            SC-012, SC-013, SC-015, SC-016, SC-017, SC-019, SC-024, SC-025,
//            SC-026 (Fold arms; plan S8.2).
//
// One amount control (0-100) walks three regimes (FR-022): soft saturation
// [0, kSoftEnd], harmonic compression [kSoftEnd, kCompressionEnd] and sine
// folding [kCompressionEnd, 100]. The static map (S4.3) is tabulated over the
// amount grid in prepare(); a per-channel windowed-peak detector (S4.4, C-4
// ratified per-channel 2026-10-10) normalises the compression and folding
// regimes. Fixed-size state, no heap anywhere (FR-040, C-1).
//
// Per sample (S4.5): sanitise, detector, smoothed amount, then either the idle
// bit-exact copy or the S2.1 split stage (zero-delay linear base band plus the
// oversampled residual Phi_fold(u) - c·(split of u), decimated through the
// 2fs notch hook and a 2 Hz DC blocker), then the kOutputCeiling clamp.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/wavefold_math.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

namespace detail {

/// Amount-table grid size (amount 0, 0.25, ..., 100). HarmonicFold::kTablePoints aliases it; it is
/// declared here so the S4.3 lookup below can precede the class (member bodies see it).
inline constexpr std::size_t kAmountTablePoints = 401;

/// S4.3 lookup index: pos = a * 4 in [0, 400]; clamped to kAmountTablePoints - 2 (= 399) so
/// a = 100 interpolates T[399]..T[400] with t = 1 and never reads T[401].
[[nodiscard]] constexpr std::size_t amountTableIndex(float pos) noexcept {
    return std::min<std::size_t>(static_cast<std::size_t>(pos), kAmountTablePoints - 2);
}

/// S4.3 linear interpolation of an amount table at amount a in [0, 100] (already clamped).
/// a = 100 -> i = 399, t = 1 -> 0 * T[399] + 1 * T[400], exactly T[400].
[[nodiscard]] inline float lerpAmountTable(const std::array<float, kAmountTablePoints>& t,
                                           float a) noexcept {
    const float pos = a * 4.0f;
    const std::size_t i = amountTableIndex(pos);
    const float frac = pos - static_cast<float>(i);
    return (1.0f - frac) * t[i] + frac * t[i + 1];
}

}  // namespace detail

/// @brief Profundum's three-regime Fold stage (Layer 2).
///
/// @note Precondition: the owner runs processBlock with FTZ/DAZ on (ScopedDenormalMode on x86, the
///       default FPCR on AArch64), mirroring profundum_core.h:37-38. The halfband sections,
///       split/notch biquads and DC blocker are not flushed. The detector env is flushed
///       explicitly (S4.4), so silence cannot stall it.
///
/// Copyable and movable (FR-020's non-copyability only applied to an owned Oversampler). All
/// methods are noexcept and allocation-free; prepare() computes the static map tables.
class HarmonicFold {
public:
    // Regimes (D-12, FR-022)
    static constexpr float kSoftEnd = 30.0f;
    static constexpr float kCompressionEnd = 65.0f;
    static constexpr float kNormalisationRampEnd = 40.0f;       // beta: 0 at kSoftEnd -> 1 here (FR-036)
    // Static map (S4.3; calibrated by edge-fold-model.js)
    static constexpr float kCalibrationPeak = 0.3552343859f;     // sqrt(2)·10^(-12/20), the core's -12 dBFS RMS sine peak
    static constexpr float kSoftDriveAtSoftEnd = 0.43f;          // g(kSoftEnd)
    static constexpr float kCompressionDriveAtEnd = 1.20f;       // g(kCompressionEnd); g keeps its geometric law to 100
    static constexpr float kFoldIndexMax = 3.0f;                 // theta(100), rad at the calibration peak (plan S1 C-11)
    static constexpr float kFoldIndexEaseExponent = 1.5f;        // p: theta = thetaMax·(1 − (1 − t)^p), t over [65, 100] (D-1)
    // Structure (S2)
    static constexpr float kSplitHz = 500.0f;
    static constexpr float kSplitEngageAmount = 10.0f;
    static constexpr float kDcCornerHz = 2.0f;                   // <= 96 kHz: exact (DCBlocker R clamp, S0)
    static constexpr float kAmountSmoothMs = 10.0f;
    static constexpr float kOutputCeiling = 6.0f;
    // Detector (FR-037, S4.4)
    static constexpr float kDetectorHoldMs = 125.0f;             // one period of the core's 8 Hz floor
    static constexpr std::size_t kDetectorSegments = 8;
    static constexpr float kDetectorAttackMs = 0.0f;             // 0 = instant: env := raw on a rising raw (C-4 step clause)
    static constexpr float kDetectorReleasePeriods = 4.0f;
    static constexpr float kDetectorReleaseHysteresis = 0.005f;  // D-4: release only when raw < env·(1 − h)
    static constexpr std::size_t kDetectorPeakOversampling = 4;  // D-4: true-peak estimate on 4x-upsampled |x|
    static constexpr float kDefaultFundamentalHz = 20.6017223f;  // E0 (FR-037 "default sized for E0")
    static constexpr float kMinFundamentalHz = 8.0f;
    static constexpr float kMaxFundamentalHz = 20000.0f;
    static constexpr float kDetectorFloor = 1.0e-6f;
    static constexpr std::size_t kTablePoints = detail::kAmountTablePoints;  // amount grid, step 0.25

    static_assert(kTablePoints == 401);
    static_assert((kDetectorSegments & (kDetectorSegments - 1)) == 0,
                  "the detector ring index wraps with a mask");
    static_assert(kDetectorPeakOversampling == 2 || kDetectorPeakOversampling == 4,
                  "PolyphaseHalfbandOversampler::setFactor supports 2 and 4 only");
    static_assert(kDetectorReleaseHysteresis > 0.0f && kDetectorReleaseHysteresis < 0.01f);

    using AmountTable = std::array<float, kTablePoints>;

    // -------------------------------------------------------------------------
    // Control thread (allocation-free; prepare computes the tables, S4.3)
    // -------------------------------------------------------------------------

    /// maxBlockSize is unused: the core is per-sample and accepts any numSamples.
    void prepare(double sampleRate, [[maybe_unused]] std::size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        computeTables();

        // Factor table (FR-045, S3.3): 4x up to 100 kHz, 2x above.
        const std::size_t factor = (sampleRate <= 100000.0) ? std::size_t{4} : std::size_t{2};
        const auto fs = static_cast<float>(sampleRate);
        const auto osRate = static_cast<float>(sampleRate * static_cast<double>(factor));
        const auto twoXRate = static_cast<float>(sampleRate * 2.0);
        for (auto& ch : channels_) {
            ch.os.setFactor(factor);
            ch.detOs.setFactor(kDetectorPeakOversampling);  // D-4: at every rate
            ch.splitBase.configure(FilterType::Lowpass, kSplitHz, kButterworthQ, 0.0f, fs);
            ch.splitOs.configure(FilterType::Lowpass, kSplitHz, kButterworthQ, 0.0f, osRate);
            for (std::size_t k = 0; k < ch.notch.size(); ++k) {
                ch.notch[k].configure(FilterType::Notch, kNotchRatios[k] * fs, kNotchQs[k], 0.0f,
                                      twoXRate);
            }
            ch.dc.prepare(sampleRate, kDcCornerHz);
        }

        amountSmoother_.configure(kAmountSmoothMs, fs);

        // Detector: segLen = ceil(round(hold * fs) / 8) (S4.4), alpha_r from the fundamental.
        const double holdSamples = std::round(static_cast<double>(kDetectorHoldMs) * 1.0e-3 * sampleRate);
        segLen_ = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::ceil(holdSamples / static_cast<double>(kDetectorSegments))));
        prepared_ = true;
        updateReleaseCoeff();

        resetState();
        idle_ = true;
    }

    /// Zero every filter / oversampler / detector state and the pre-ceiling peak; snap the
    /// smoother to the pending amount. The pending amount and fundamental are kept.
    void reset() noexcept {
        resetState();
        idle_ = (pendingAmount_ == 0.0f);
    }

    // -------------------------------------------------------------------------
    // Audio thread
    // -------------------------------------------------------------------------

    /// [0, 100]; non-finite keeps the previous value; a denormal becomes 0 (FR-043). Valid before
    /// prepare (FR-042): the sanitised value is stored and applied by prepare.
    void setAmount(float amount) noexcept {
        pendingAmount_ = sanitizeControl(amount, pendingAmount_, 0.0f, 100.0f);
        amountSmoother_.setTarget(pendingAmount_);
    }

    /// The sanitised target.
    [[nodiscard]] float amount() const noexcept { return pendingAmount_; }

    /// Release = kDetectorReleasePeriods / hz (FR-037). Non-finite keeps the previous value; a
    /// denormal becomes 0; the result is clamped to [kMinFundamentalHz, kMaxFundamentalHz].
    void setFundamentalHz(float hz) noexcept {
        fundamentalHz_ = sanitizeControl(hz, fundamentalHz_, kMinFundamentalHz, kMaxFundamentalHz);
        updateReleaseCoeff();
    }

    [[nodiscard]] float fundamentalHz() const noexcept { return fundamentalHz_; }

    /// Any numSamples. Before prepare (FR-042), with a null channel or with 0 samples the buffers
    /// are left untouched. right == left processes one channel with channel 0's state.
    /// Per-sample core (S4.5), no internal block ceiling.
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0)
            return;
        const std::size_t numChannels = (left == right) ? std::size_t{1} : std::size_t{2};
        const std::array<float*, 2> io{left, right};

        for (std::size_t n = 0; n < numSamples; ++n) {
            // (1) sanitise (FR-043).
            std::array<float, 2> x{};
            for (std::size_t c = 0; c < numChannels; ++c)
                x[c] = sanitizeSample(io[c][n]);
            // Mono link: from prepare / reset both channels hold bit-identical state, so while the
            // sanitised inputs are bit-identical, channel 1 would compute exactly channel 0's
            // samples. Channel 0 runs alone and its output is copied; at the first differing
            // sample channel 1 takes channel 0's state and both run from then on.
            if (linked_ && numChannels == 2
                && std::bit_cast<std::uint32_t>(x[0]) != std::bit_cast<std::uint32_t>(x[1])) {
                channels_[1] = channels_[0];
                linked_ = false;
            }
            const std::size_t active = linked_ ? std::size_t{1} : numChannels;
            // (2) detector, then the smoothed amount.
            for (std::size_t c = 0; c < active; ++c)
                advanceDetector(channels_[c], x[c]);
            const float a = amountSmoother_.process();

            // (3) idle: bit-exact copy; only splitBase keeps running so lo is valid at engage.
            if (a == 0.0f && amountSmoother_.getTarget() == 0.0f) {
                idle_ = true;
                for (std::size_t c = 0; c < active; ++c) {
                    (void)channels_[c].splitBase.process(x[c]);
                    preCeilingPeak_ = std::max(preCeilingPeak_, std::abs(x[c]));
                    io[c][n] = x[c];
                }
                if (active < numChannels)
                    io[1][n] = io[0][n];
                continue;
            }
            if (idle_) {
                // Engaging from idle resets the residual path (S2.1), so the render does not
                // depend on stale state (SC-017).
                for (auto& ch : channels_)
                    resetResidual(ch);
                idle_ = false;
            }

            // (4) the S2.1 stage per channel; (5) peak, then clamp.
            const MapCoeffs m = mapCoeffs(a);
            for (std::size_t c = 0; c < active; ++c) {
                const float y = processStage(channels_[c], x[c], m);
                preCeilingPeak_ = std::max(preCeilingPeak_, std::abs(y));
                io[c][n] = std::clamp(y, -kOutputCeiling, kOutputCeiling);
            }
            if (active < numChannels)
                io[1][n] = io[0][n];
        }
    }

    /// True when every piece of recurrent state is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!detail::isFinite(amountSmoother_.getCurrentValue()))
            return false;
        for (const auto& ch : channels_) {
            if (!ch.os.stateFinite() || !ch.detOs.stateFinite())
                return false;
            if (!biquadFinite(ch.splitBase) || !biquadFinite(ch.splitOs))
                return false;
            if (!detail::isFinite(ch.dcIn) || !detail::isFinite(ch.dcOut))
                return false;
            for (const auto& n : ch.notch) {
                if (!biquadFinite(n))
                    return false;
            }
            if (!detail::isFinite(ch.env) || !detail::isFinite(ch.segMax) || !detail::isFinite(ch.ringMax))
                return false;
            for (const float r : ch.ring) {
                if (!detail::isFinite(r))
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    // -------------------------------------------------------------------------
    // Read-only observers (tests; Phase 6/9 diagnostics)
    // -------------------------------------------------------------------------

    /// Channel 0's (left's) detector env (S4.4; C-3 gate). Detectors are per channel (C-4).
    [[nodiscard]] float detectorLevel() const noexcept { return channels_[0].env; }

    /// The release time constant in effect, kDetectorReleasePeriods / fundamentalHz() (SC-024(a)).
    /// The quotient is formed in double and rounded once, so it is the correctly rounded float
    /// quotient even where fast-math would turn a float division into a reciprocal multiply.
    [[nodiscard]] float releaseSeconds() const noexcept {
        return static_cast<float>(static_cast<double>(kDetectorReleasePeriods)
                                  / static_cast<double>(fundamentalHz_));
    }

    [[nodiscard]] std::size_t oversamplingFactor() const noexcept { return channels_[0].os.factor(); }

    /// Largest |y| before the kOutputCeiling clamp since prepare / reset / clearPreCeilingPeak
    /// (SC-019: the test asserts it stays below the ceiling, so a design that relies on the
    /// clamp fails). One max per sample.
    [[nodiscard]] float preCeilingPeak() const noexcept { return preCeilingPeak_; }

    void clearPreCeilingPeak() noexcept { preCeilingPeak_ = 0.0f; }

private:
    static constexpr float kButterworthQ = 0.70710678f;
    /// Residual notch pre-filter at the 2fs stage (S2.2): centres re the base rate, and their Qs.
    static constexpr std::array<float, 3> kNotchRatios{0.50f, 0.52f, 0.55f};
    static constexpr std::array<float, 3> kNotchQs{1.5f, 2.0f, 1.5f};
    /// S4.3: points on q in (0, P] for the peak-preserving makeup, and phase points for c(a).
    static constexpr std::size_t kMakeupPoints = 4096;
    static constexpr std::size_t kH1PhasePoints = 512;

    struct Channel {
        PolyphaseHalfbandOversampler os;
        Biquad splitBase;
        Biquad splitOs;
        std::array<Biquad, 3> notch{};
        DCBlocker dc;
        float dcIn = 0.0f;   // mirrors of the DC blocker's last input / output (its x1_/y1_
        float dcOut = 0.0f;  // are private), so stateFinite() can see them
        std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
        // Detector (S4.4), one per channel (C-4).
        std::array<float, kDetectorSegments> ring{};
        float ringMax = 0.0f;   // max over ring, refreshed when a segment completes
        float segMax = 0.0f;
        std::size_t segCount = 0;
        std::size_t ringPos = 0;
        float env = 0.0f;
        // True-peak estimate (D-4): a 4x upsampler and |u| of the previous two upsampled samples.
        PolyphaseHalfbandOversampler detOs;
        std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> detBuf{};
        float dPrev1 = 0.0f;
        float dPrev2 = 0.0f;
    };

    /// FR-043: non-finite keeps `previous`; a denormal (or signed zero) bit pattern becomes +0;
    /// then clamp to [lo, hi].
    [[nodiscard]] static float sanitizeControl(float v, float previous, float lo, float hi) noexcept {
        if (!detail::isFinite(v))
            return previous;
        if ((std::bit_cast<std::uint32_t>(v) & 0x7F800000u) == 0u)
            v = 0.0f;
        return std::min(std::max(lo, v), hi);
    }

    [[nodiscard]] static bool biquadFinite(const Biquad& b) noexcept {
        return detail::isFinite(b.getZ1()) && detail::isFinite(b.getZ2());
    }

    void updateReleaseCoeff() noexcept {
        if (!prepared_)
            return;
        // alpha_r = 1 - exp(-f0 / (N * fs)): tau_r = N / f0 (S4.4).
        releaseCoeff_ = static_cast<float>(
            1.0 - std::exp(-static_cast<double>(fundamentalHz_)
                           / (static_cast<double>(kDetectorReleasePeriods) * sampleRate_)));
    }

    /// The residual-path state (os, splitOs, notches, DC), reset on leaving idle (S2.1, S4.5). At
    /// 4x the residual reads detOs's interpolation, which is not reset here (processStage).
    static void resetResidual(Channel& ch) noexcept {
        ch.os.reset();
        ch.splitOs.reset();
        for (auto& n : ch.notch)
            n.reset();
        ch.dc.reset();
        ch.dcIn = 0.0f;
        ch.dcOut = 0.0f;
        ch.buf.fill(0.0f);
    }

    void resetState() noexcept {
        for (auto& ch : channels_) {
            resetResidual(ch);
            ch.splitBase.reset();
            ch.ring.fill(0.0f);
            ch.ringMax = 0.0f;
            ch.segMax = 0.0f;
            ch.segCount = 0;
            ch.ringPos = 0;
            ch.env = 0.0f;
            ch.detOs.reset();
            ch.detBuf.fill(0.0f);
            ch.dPrev1 = 0.0f;
            ch.dPrev2 = 0.0f;
        }
        amountSmoother_.snapTo(pendingAmount_);
        preCeilingPeak_ = 0.0f;
        linked_ = true;   // both channels now hold identical state (processBlock mono link)
    }

    /// One detector step on the sanitised sample x of one channel (S4.4, per channel).
    /// C-4 ratified 2026-10-10: d = |x_ch| per channel; the rejected shared variant used
    /// d = max(|xL|, |xR|) for both channels and narrowed the image at high amounts.
    ///
    /// D-4 (tasks.md, 2026-10-11). The plain sample maximum of a periodic signal whose period is
    /// not an integer number of samples depends on where the samples fall on the waveform, so the
    /// windowed maximum stepped as the window slid, the release followed it, and G was modulated
    /// at a non-harmonic rate that SC-005 read as aliasing at amount 100 (E1 / 48 kHz chain:
    /// -74.9 dBFS against -80). Two parts, both needed (scratch probes, tasks.md D-4):
    /// - d is a true-peak estimate: the larger of |x| and the parabolic-interpolated local maxima
    ///   of |x| upsampled kDetectorPeakOversampling x (detOs). On a 500-partial saw the window
    ///   ripple drops from 4e-2 to 0; base-rate (5e-2 with (2)) or 2x interpolation does not.
    /// - The release runs only while raw < env·(1 − kDetectorReleaseHysteresis), so the residual
    ///   per-period peak variation of the excited core output (about 2e-3) cannot pull env down
    ///   and back up. After a real level drop env settles at most h above the new peak (G low
    ///   by <= 0.043 dB); rising levels are still caught instantly.
    /// d >= |x|, so the instant-attack guarantee G·|x| <= P is unchanged despite detOs's delay.
    void advanceDetector(Channel& ch, float x) noexcept {
        float d = std::abs(x);
        ch.detOs.upsample(x, ch.detBuf.data());
        for (std::size_t i = 0; i < ch.detOs.factor(); ++i) {
            const float d0 = std::abs(ch.detBuf[i]);
            const float a = ch.dPrev2;
            const float b = ch.dPrev1;
            d = std::max(d, d0);
            if (b >= a && b > d0) {
                // a − 2b + d0 < 0 here (b >= a and b > d0), so the division is safe.
                const float offset = 0.5f * (a - d0) / (a - 2.0f * b + d0);
                d = std::max(d, b - 0.25f * (a - d0) * offset);
            }
            ch.dPrev2 = b;
            ch.dPrev1 = d0;
        }
        ch.segMax = std::max(ch.segMax, d);
        if (++ch.segCount >= segLen_) {
            ch.ring[ch.ringPos] = ch.segMax;
            ch.ringPos = (ch.ringPos + 1) & (kDetectorSegments - 1);
            ch.segMax = 0.0f;
            ch.segCount = 0;
            ch.ringMax = 0.0f;
            for (const float r : ch.ring)
                ch.ringMax = std::max(ch.ringMax, r);
        }
        const float raw = std::max(ch.segMax, ch.ringMax);
        static_assert(kDetectorAttackMs == 0.0f,
                      "the instant-attack branch below implements kDetectorAttackMs == 0 only");
        if (raw > ch.env)
            ch.env = raw;  // instant attack
        else if (raw < ch.env * (1.0f - kDetectorReleaseHysteresis))
            ch.env = detail::flushDenormal(ch.env + releaseCoeff_ * (raw - ch.env));
    }

    /// FR-043 sample rule: a non-finite input sample becomes 0.0f.
    [[nodiscard]] static float sanitizeSample(float v) noexcept {
        return detail::isFinite(v) ? v : 0.0f;
    }

    /// smoothstep(t) = t²(3 − 2t) on t clamped to [0, 1] (S4.3 beta and w).
    [[nodiscard]] static float smoothstep(float t) noexcept {
        t = std::min(std::max(t, 0.0f), 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    /// The static map at the smoothed amount a, shared by both channels (S4.3).
    struct MapCoeffs {
        float pOverG = 1.0f;      // P / g (sat)
        float gOverP = 0.0f;      // g / P (sat)
        float theta = 0.0f;       // fold index
        float invV = 1.0f;        // 1 / V
        float vOverTheta = 1.0f;  // V / theta
        float makeup = 1.0f;      // mk(a)
        float c = 1.0f;           // h1 coefficient c(a)
        float oneMinusW = 1.0f;   // 1 − w(a), w = smoothstep(a / kSplitEngageAmount)
        float beta = 0.0f;        // smoothstep((a − kSoftEnd) / (kNormalisationRampEnd − kSoftEnd))
        bool satOn = false;       // g >= 1e-6 (else sat is the identity)
        bool foldOn = false;      // theta >= kSineFoldGainEpsilon (else z = sat)
    };

    [[nodiscard]] MapCoeffs mapCoeffs(float a) const noexcept {
        MapCoeffs m;
        const float g = detail::lerpAmountTable(gTable_, a);
        const float V = detail::lerpAmountTable(vTable_, a);
        m.theta = detail::lerpAmountTable(thetaTable_, a);
        m.makeup = detail::lerpAmountTable(makeupTable_, a);
        m.c = detail::lerpAmountTable(h1Table_, a);
        m.satOn = (g >= 1.0e-6f);
        if (m.satOn) {
            m.pOverG = kCalibrationPeak / g;
            m.gOverP = g / kCalibrationPeak;
        }
        m.foldOn = (m.theta >= WavefoldMath::kSineFoldGainEpsilon);
        if (m.foldOn) {
            m.invV = 1.0f / V;
            m.vOverTheta = V / m.theta;
        }
        m.oneMinusW = 1.0f - smoothstep(a / kSplitEngageAmount);
        m.beta = smoothstep((a - kSoftEnd) / (kNormalisationRampEnd - kSoftEnd));
        return m;
    }

    /// Phi_fold(u) = mk · z(G·u) / G (S4.3), with invG = 1 / G.
    [[nodiscard]] static float phiFold(float u, float G, float invG, const MapCoeffs& m) noexcept {
        const float q = G * u;
        const float s = m.satOn ? m.pOverG * Sigmoid::recipSqrt(m.gOverP * q) : q;
        const float z = m.foldOn ? m.vOverTheta * WavefoldMath::sineFold(s * m.invV, m.theta) : s;
        return m.makeup * z * invG;
    }

    /// The S2.1 stage on one channel's sanitised sample x; returns y before the clamp.
    [[nodiscard]] static float processStage(Channel& ch, float x, const MapCoeffs& m) noexcept {
        // Zero-delay linear base band.
        const float lo = ch.splitBase.process(x);
        const float base = m.c * (lo + m.oneMinusW * (x - lo));

        // Detector gain (S4.4): G = (1 − beta) + beta · P / max(env, kDetectorFloor).
        const float G = (1.0f - m.beta) + m.beta * kCalibrationPeak / std::max(ch.env, kDetectorFloor);
        const float invG = 1.0f / G;

        // Oversampled residual: Phi_fold(u) − c·(LPo(u) + (1 − w)(u − LPo(u))).
        // When the residual factor equals the detector's (4x, fs <= 100 kHz), u is the detector's
        // interpolation of this same sample (advanceDetector ran first): one interpolator serves
        // both, and it stays warm through idle as the core's excitation interpolator does
        // (profundum_core.h engageExcitationFromIdle). Above 100 kHz the residual's own 2x
        // interpolator runs, reset on engage with the rest of the residual path.
        const std::size_t factor = ch.os.factor();
        const float* up = ch.detBuf.data();
        if (factor != ch.detOs.factor()) {
            ch.os.upsample(x, ch.buf.data());
            up = ch.buf.data();
        }
        for (std::size_t i = 0; i < factor; ++i) {
            const float u = up[i];
            const float loOs = ch.splitOs.process(u);
            ch.buf[i] = phiFold(u, G, invG, m) - m.c * (loOs + m.oneMinusW * (u - loOs));
        }

        // Decimate through the 2fs notch hook (S2.2), then the 2 Hz DC blocker.
        auto& notch = ch.notch;
        const float r = ch.os.downsample(ch.buf.data(), [&notch](float v) noexcept {
            for (auto& bq : notch)
                v = bq.process(v);
            return v;
        });
        ch.dcIn = r;
        ch.dcOut = ch.dc.process(r);
        return base + ch.dcOut;
    }

    /// S4.3: the five amount tables, computed in double over a = 0, 0.25, ..., 100 with the
    /// float functions' formulas (Sigmoid::recipSqrt, WavefoldMath::sineFold), not by calling them.
    void computeTables() noexcept {
        const double P = static_cast<double>(kCalibrationPeak);
        const double gS = static_cast<double>(kSoftDriveAtSoftEnd);
        const double gC = static_cast<double>(kCompressionDriveAtEnd);
        const double thetaMax = static_cast<double>(kFoldIndexMax);
        const double easeP = static_cast<double>(kFoldIndexEaseExponent);
        const double softEnd = static_cast<double>(kSoftEnd);
        const double compEnd = static_cast<double>(kCompressionEnd);
        const double compSpan = compEnd - softEnd;
        const double foldSpan = 100.0 - compEnd;
        const double thetaEps = static_cast<double>(WavefoldMath::kSineFoldGainEpsilon);
        constexpr double kTwoPiD = 6.283185307179586476925286766559;

        for (std::size_t i = 0; i < kTablePoints; ++i) {
            const double a = static_cast<double>(i) * 0.25;
            const double g = (a <= softEnd) ? gS * a / softEnd
                                            : gS * std::pow(gC / gS, (a - softEnd) / compSpan);
            // D-1 (tasks.md, 2026-10-11): ease-out fold index, zero slope at 100, theta(100) = thetaMax.
            const double theta =
                (a <= compEnd) ? 0.0 : thetaMax * (1.0 - std::pow(1.0 - (a - compEnd) / foldSpan, easeP));

            const auto sat = [g, P](double q) noexcept {
                if (g < 1.0e-6)
                    return q;
                const double v = g * q / P;
                return (P / g) * (v / std::sqrt(v * v + 1.0));
            };
            const double V = sat(P);
            const auto z = [&sat, theta, V, thetaEps](double q) noexcept {
                const double s = sat(q);
                if (theta < thetaEps)
                    return s;
                return (V / theta) * std::sin(theta * s / V);
            };

            double peak = 0.0;
            for (std::size_t j = 1; j <= kMakeupPoints; ++j) {
                const double q = P * static_cast<double>(j) / static_cast<double>(kMakeupPoints);
                peak = std::max(peak, std::abs(z(q)));
            }
            const double mk = (peak > 0.0) ? P / peak : 1.0;

            double h1 = 0.0;
            for (std::size_t k = 0; k < kH1PhasePoints; ++k) {
                const double s = std::sin(kTwoPiD * static_cast<double>(k)
                                          / static_cast<double>(kH1PhasePoints));
                h1 += z(P * s) * s;
            }
            const double c = mk * (2.0 / static_cast<double>(kH1PhasePoints)) * h1 / P;

            gTable_[i] = static_cast<float>(g);
            thetaTable_[i] = static_cast<float>(theta);
            vTable_[i] = static_cast<float>(V);
            makeupTable_[i] = static_cast<float>(mk);
            h1Table_[i] = static_cast<float>(c);
        }
    }

    std::array<Channel, 2> channels_{};
    OnePoleSmoother amountSmoother_;

    // Static map tables (S4.3): g, theta, V, makeup, c over the amount grid.
    AmountTable gTable_{};
    AmountTable thetaTable_{};
    AmountTable vTable_{};
    AmountTable makeupTable_{};
    AmountTable h1Table_{};

    std::size_t segLen_ = 1;
    float releaseCoeff_ = 0.0f;
    float fundamentalHz_ = kDefaultFundamentalHz;
    float pendingAmount_ = 0.0f;
    double sampleRate_ = 0.0;
    bool prepared_ = false;
    bool idle_ = true;
    bool linked_ = true;   // channel 1 == channel 0 bit for bit (processBlock mono link)
    float preCeilingPeak_ = 0.0f;
};

static_assert(detail::amountTableIndex(400.0f) == HarmonicFold::kTablePoints - 2);
static_assert(detail::amountTableIndex(0.0f) == 0);

}  // namespace Krate::DSP
