// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// Rectifier: half/full-wave rectification with a continuous blend, ADAA2 at the
// oversampled rate and a DC-blocked residual (plan S4.2, S4.3, S5.5; C-1, C-19).
//   Carries: FR-001..FR-005, FR-066, FR-069; SC-010, SC-011, SC-014, SC-016,
//            SC-017, SC-018, SC-019, SC-021 (Rectifier arms).
//   Spec:  specs/profundum-phase5-shape-character/spec.md
//   Plan:  specs/profundum-phase5-shape-character/plan.md (S2, S3, S4.2, S4.3, S5.5)
//   Tests: dsp/tests/unit/processors/rectifier_test.cpp,
//          dsp/tests/unit/processors/rectifier_nonfinite_test.cpp
//
// Law (FR-002): y = (1 - b)·max(x, 0) + b·|x| = c(b)·x + (1 + b)/2·|x|, c(b) = (1 - b)/2.
//
// Structure (plan S2/S3, duplicated over L0/L1 parts, C-1): per base sample the
// sanitised input is upsampled (4x up to 100 kHz, 2x above). At the oversampled
// rate the law runs through second-order ADAA in double (ClosedFormAdaa2 over
// RampLaw and AbsLaw; exact by linearity, so a moving b needs no history
// recompute). The linear part lin = c(b)·u[n-1] (aligned with the ADAA2 one-sample
// delay) decimates through a flat halfband; the residual y - lin decimates through
// a second halfband whose 2fs stage runs the three residual notches, then a 2 Hz
// DC blocker. No gain stage (SC-014(b)). Output clamped to ±kOutputCeiling.
//
// Includes L0 and L1 only (it does not include OversampledResidualPath, also L2;
// the four shared constants are duplicated and their equality is tested, C-1).
//
// Mono link (plan S2, HarmonicFold's mechanism): while the sanitised L and R inputs
// are bit-identical, channel 0 runs alone and is copied; at the first difference
// channel 1 takes channel 0's state. Stays unlinked until reset() / prepare().
//
// RT: heap-free, fixed-size members, every audio-thread method noexcept.
// Precondition: FTZ/DAZ on (the IIR sections are not flushed per call).
// ==============================================================================

#pragma once

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/db_utils.h>
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
struct RectifierProbe;
}  // namespace detail

/// Standalone half/full-wave rectifier with a continuous blend (FR-001..FR-005).
/// Zero latency; in place; `right == left` processes one channel (HarmonicFold convention).
class Rectifier {
public:
    /// max(x, 0) with its antiderivatives (F1' = f, F2' = F1, F1(0) = F2(0) = 0).
    struct RampLaw {
        [[nodiscard]] static double f(double x) noexcept { return x > 0.0 ? x : 0.0; }
        [[nodiscard]] static double F1(double x) noexcept { return x > 0.0 ? 0.5 * x * x : 0.0; }
        [[nodiscard]] static double F2(double x) noexcept { return x > 0.0 ? x * x * x / 6.0 : 0.0; }
    };
    /// |x| with its antiderivatives.
    struct AbsLaw {
        [[nodiscard]] static double f(double x) noexcept { return x < 0.0 ? -x : x; }
        [[nodiscard]] static double F1(double x) noexcept { return 0.5 * x * (x < 0.0 ? -x : x); }
        [[nodiscard]] static double F2(double x) noexcept { return x * x * (x < 0.0 ? -x : x) / 6.0; }
    };

    static constexpr float kBlendSmoothMs = 10.0f;
    static constexpr float kOutputCeiling = 6.0f;  // FR-069
    // C-1: equal to OversampledResidualPath's (tested by Rectifier_Constants).
    static constexpr std::array<float, 3> kNotchRatios{0.50f, 0.52f, 0.55f};  // re fs, at 2fs
    static constexpr std::array<float, 3> kNotchQs{1.5f, 2.0f, 1.5f};
    static constexpr float kResidualDcHz = 2.0f;
    static constexpr double kFourTimesLimitHz = 100000.0;  // 4x up to 100 kHz, 2x above

    // -------------------------------------------------------------------------
    // Control thread
    // -------------------------------------------------------------------------

    /// maxBlockSize is unused: the core is per-sample and accepts any numSamples. Resets.
    void prepare(double sampleRate, [[maybe_unused]] std::size_t maxBlockSize) noexcept {
        const std::size_t factor = (sampleRate <= kFourTimesLimitHz) ? std::size_t{4} : std::size_t{2};
        const auto fs = static_cast<float>(sampleRate);
        const auto twoXRate = static_cast<float>(sampleRate * 2.0);
        for (auto& ch : ch_) {
            ch.up.setFactor(factor);
            ch.downLin.setFactor(factor);
            ch.downRes.setFactor(factor);
            for (std::size_t k = 0; k < ch.notch.size(); ++k)
                ch.notch[k].configure(FilterType::Notch, kNotchRatios[k] * fs, kNotchQs[k], 0.0f, twoXRate);
            ch.dc.prepare(sampleRate, kResidualDcHz);
        }
        blend_.configure(kBlendSmoothMs, fs);
        prepared_ = true;
        reset();
    }

    /// Zero every filter / oversampler / ADAA state and the pre-ceiling peak, snap the blend
    /// smoother to the pending blend and re-link the channels. The pending blend is kept.
    void reset() noexcept {
        for (auto& ch : ch_)
            resetChannel(ch);
        blend_.snapTo(pendingBlend_);
        peak_ = 0.0f;
        linked_ = true;
    }

    // -------------------------------------------------------------------------
    // Audio thread
    // -------------------------------------------------------------------------

    /// [0, 1]: 0 half-wave, 1 full-wave. Non-finite keeps the previous value; a denormal
    /// becomes 0; then clamp (FR-066). Smoothed over kBlendSmoothMs (FR-005). Valid before
    /// prepare: the value is stored and applied (snapped) by prepare.
    void setBlend(float b) noexcept {
        pendingBlend_ = sanitizeControl(b, pendingBlend_, 0.0f, 1.0f);
        blend_.setTarget(pendingBlend_);
    }

    /// The sanitised target blend.
    [[nodiscard]] float blend() const noexcept { return pendingBlend_; }

    /// In place. Before prepare, with a null channel or with 0 samples the buffers are left
    /// untouched. right == left processes one channel with channel 0's state.
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0)
            return;
        const std::size_t numChannels = (left == right) ? std::size_t{1} : std::size_t{2};
        const std::array<float*, 2> io{left, right};

        for (std::size_t n = 0; n < numSamples; ++n) {
            // (1) sanitise (FR-066).
            std::array<float, 2> x{};
            for (std::size_t c = 0; c < numChannels; ++c)
                x[c] = detail::isFinite(io[c][n]) ? io[c][n] : 0.0f;
            if (linked_ && numChannels == 2
                && std::bit_cast<std::uint32_t>(x[0]) != std::bit_cast<std::uint32_t>(x[1])) {
                ch_[1] = ch_[0];
                linked_ = false;
            }
            const std::size_t active = linked_ ? std::size_t{1} : numChannels;
            const float b = blend_.process();

            // (2)-(5) per active channel.
            for (std::size_t c = 0; c < active; ++c) {
                const float z = processChannel(ch_[c], x[c], b);
                peak_ = std::max(peak_, std::abs(z));
                io[c][n] = std::clamp(z, -kOutputCeiling, kOutputCeiling);
            }
            if (active < numChannels)
                io[1][n] = io[0][n];
        }
    }

    /// True when every piece of recurrent state is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!detail::isFinite(blend_.getCurrentValue()))
            return false;
        for (const auto& ch : ch_) {
            if (!ch.up.stateFinite() || !ch.downLin.stateFinite() || !ch.downRes.stateFinite())
                return false;
            for (const auto& q : ch.notch) {
                if (!detail::isFinite(q.getZ1()) || !detail::isFinite(q.getZ2()))
                    return false;
            }
            if (!detail::isFinite(ch.dcIn) || !detail::isFinite(ch.dcOut) || !detail::isFinite(ch.uPrev))
                return false;
            if (!ch.ramp.stateFinite() || !ch.abs.stateFinite())
                return false;
        }
        return true;
    }

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }

    /// Largest |output| before the kOutputCeiling clamp since prepare / reset (SC-019).
    [[nodiscard]] float preCeilingPeak() const noexcept { return peak_; }

    [[nodiscard]] std::size_t oversamplingFactor() const noexcept { return ch_[0].up.factor(); }

    // -------------------------------------------------------------------------
    // The law (FR-001, FR-002, S5.5)
    // -------------------------------------------------------------------------

    /// FR-001/FR-002: the memoryless law y = (1 - b)·max(x, 0) + b·|x|. Evaluated in double and
    /// rounded once, so it is within half a float ulp of the exact law (SC-014(a)). The
    /// processing path uses the same law through RampLaw / AbsLaw.
    [[nodiscard]] static float transfer(float x, float b) noexcept {
        const auto xd = static_cast<double>(x);
        const auto bd = static_cast<double>(b);
        return static_cast<float>((1.0 - bd) * RampLaw::f(xd) + bd * AbsLaw::f(xd));
    }

    /// Odd part of the law: transfer(x, b) = c·x + (1 + b)/2·|x| with c = (1 - b)/2 (exact).
    [[nodiscard]] static constexpr float linearCoefficient(float b) noexcept { return 0.5f * (1.0f - b); }

    /// Level-preserving makeup on a sine (S5.5): sqrt(0.5 / ac²(b)),
    /// ac²(b) = (1 + b²)/4 - (1 + b)²/π². b is clamped to [0, 1].
    [[nodiscard]] static float sineMakeup(float b) noexcept {
        const double bd = std::min(std::max(static_cast<double>(b), 0.0), 1.0);
        const double onePlusB = 1.0 + bd;
        const double ac2 = (1.0 + bd * bd) / 4.0 - (onePlusB * onePlusB) / (kPi * kPi);
        return static_cast<float>(std::sqrt(0.5 / ac2));
    }

private:
    friend struct detail::RectifierProbe;

    static constexpr double kPi = 3.14159265358979323846;
    static constexpr std::size_t kMaxFactor = PolyphaseHalfbandOversampler::kMaxFactor;

    struct Channel {
        PolyphaseHalfbandOversampler up;       // upsamples only
        PolyphaseHalfbandOversampler downLin;  // decimates the linear part (flat)
        PolyphaseHalfbandOversampler downRes;  // decimates the residual (notches at 2fs)
        std::array<Biquad, 3> notch{};
        DCBlocker dc;
        ClosedFormAdaa2<RampLaw> ramp;
        ClosedFormAdaa2<AbsLaw> abs;
        float uPrev = 0.0f;  // previous oversampled input: alignment with the ADAA2 delay
        float dcIn = 0.0f;   // mirrors of the DC blocker's last input / output (stateFinite)
        float dcOut = 0.0f;
    };

    /// HarmonicFold::sanitizeControl's rule (private there): non-finite keeps `previous`, a
    /// denormal (or signed zero) bit pattern becomes +0, then clamp to [lo, hi].
    [[nodiscard]] static float sanitizeControl(float v, float previous, float lo, float hi) noexcept {
        if (!detail::isFinite(v))
            return previous;
        if ((std::bit_cast<std::uint32_t>(v) & 0x7F800000u) == 0u)
            v = 0.0f;
        return std::min(std::max(lo, v), hi);
    }

    static void resetChannel(Channel& ch) noexcept {
        ch.up.reset();
        ch.downLin.reset();
        ch.downRes.reset();
        for (auto& q : ch.notch)
            q.reset();
        ch.dc.reset();
        ch.ramp.reset();
        ch.abs.reset();
        ch.uPrev = 0.0f;
        ch.dcIn = 0.0f;
        ch.dcOut = 0.0f;
    }

    /// Plan S4.3 steps 2-4 for one channel and one base sample; returns z (before the clamp).
    [[nodiscard]] static float processChannel(Channel& ch, float x, float b) noexcept {
        std::array<float, kMaxFactor> u{};
        std::array<float, kMaxFactor> lin{};
        std::array<float, kMaxFactor> res{};
        ch.up.upsample(x, u.data());
        const std::size_t factor = ch.up.factor();
        const auto bd = static_cast<double>(b);
        const auto c = static_cast<double>(linearCoefficient(b));
        for (std::size_t i = 0; i < factor; ++i) {
            const auto ui = static_cast<double>(u[i]);
            // Exact by linearity of ADAA in the law: both laws always run.
            const double y = (1.0 - bd) * ch.ramp.process(ui) + bd * ch.abs.process(ui);
            const double l = c * static_cast<double>(ch.uPrev);
            lin[i] = static_cast<float>(l);
            res[i] = static_cast<float>(y - l);
            ch.uPrev = u[i];
        }
        const float linBase = ch.downLin.downsample(lin.data());
        const float resBase = ch.downRes.downsample(res.data(), [&ch](float v) noexcept {
            for (auto& q : ch.notch)
                v = q.process(v);
            return v;
        });
        ch.dcIn = resBase;
        ch.dcOut = ch.dc.process(resBase);
        return linBase + ch.dcOut;
    }

    std::array<Channel, 2> ch_{};
    OnePoleSmoother blend_;
    float pendingBlend_ = 0.0f;  // constructor default: half-wave (not ruled; T007 run-log)
    float peak_ = 0.0f;
    bool linked_ = true;
    bool prepared_ = false;
};

}  // namespace Krate::DSP
