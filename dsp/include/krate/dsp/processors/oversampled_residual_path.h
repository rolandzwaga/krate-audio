// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// OversampledResidualPath: the 4x round trip with the notched, DC-blocked
// nonlinear residual shared by both L3 stages (plan S1 C-1, S2, S3).
//   Carries: SC-010, SC-011 (through ShapeMorph and BassCharacterStage).
//   Spec:  specs/profundum-phase5-shape-character/spec.md
//   Plan:  specs/profundum-phase5-shape-character/plan.md (S2, S3)
//   Tests: dsp/tests/unit/processors/oversampled_residual_path_test.cpp
//
// Structure (plan S2): the owner upsamples its input once, runs its branches at F·fs and
// splits their sum into a linear part `lin` and a nonlinear residual `res`. `lin` decimates
// through a flat halfband round trip; `res` decimates through a second halfband whose 2fs
// stage runs HarmonicFold's three residual notches (harmonic_fold.h:300-301), then a 2 Hz DC
// blocker at the base rate. With res == 0 the output is exactly the halfband round trip.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>

#include <array>
#include <cstddef>

namespace Krate::DSP {

/// One channel of the Phase 5 oversampled residual path (plan S2/S3). Heap-free, copyable, noexcept.
/// Precondition: FTZ/DAZ on (the halfband sections, notches and DC blocker are not flushed per call).
class OversampledResidualPath {
public:
    static constexpr std::size_t kMaxFactor = PolyphaseHalfbandOversampler::kMaxFactor;   // 4
    static constexpr std::array<float, 3> kNotchRatios{0.50f, 0.52f, 0.55f};             // re fs, at 2fs (origin harmonic_fold.h:300, private there; C-1)
    static constexpr std::array<float, 3> kNotchQs{1.5f, 2.0f, 1.5f};                    // (origin harmonic_fold.h:301, private there)
    static constexpr float kResidualDcHz = 2.0f;                                         // (== HarmonicFold::kDcCornerHz, public, harmonic_fold.h:91)
    static constexpr double kFourTimesLimitHz = 100000.0;                                // 4x up to 100 kHz, 2x above (FR-068)

    /// Factor (4x up to kFourTimesLimitHz, 2x above), notches at 2fs, DC blocker at fs; resets.
    void prepare(double sampleRate) noexcept {
        const std::size_t factor = (sampleRate <= kFourTimesLimitHz) ? std::size_t{4} : std::size_t{2};
        up_.setFactor(factor);
        downLin_.setFactor(factor);
        downRes_.setFactor(factor);
        const auto fs = static_cast<float>(sampleRate);
        const auto twoXRate = static_cast<float>(sampleRate * 2.0);
        for (std::size_t k = 0; k < notch_.size(); ++k)
            notch_[k].configure(FilterType::Notch, kNotchRatios[k] * fs, kNotchQs[k], 0.0f, twoXRate);
        // Exact corner up to 96 kHz; at 192 kHz DCBlocker clamps R to 0.9999 (dc_blocker.h:236),
        // an effective corner of about 3.06 Hz (plan S3, recorded by the test).
        dc_.prepare(sampleRate, kResidualDcHz);
        reset();
    }

    /// Zero every section's state; the factor and coefficients are kept.
    void reset() noexcept {
        up_.reset();
        downLin_.reset();
        downRes_.reset();
        for (auto& n : notch_)
            n.reset();
        dc_.reset();
        dcIn_ = 0.0f;
        dcOut_ = 0.0f;
    }

    [[nodiscard]] std::size_t factor() const noexcept { return up_.factor(); }

    /// The residual DC blocker's last input (the decimated, notched residual at the base rate).
    [[nodiscard]] float dcInput() const noexcept { return dcIn_; }

    /// Prime the residual DC blocker to a known residual mean so it starts settled: its state becomes
    /// (x1 = last input, y1 = last input - mean), so the next output is x - mean up to (1 - R)·(x1 - mean).
    /// BassCharacterStage calls it at the end of a branch pre-roll (FR-026, FR-054).
    void primeDc(float mean) noexcept {
        dc_.setState(dcIn_, dcIn_ - mean);
        dcOut_ = dcIn_ - mean;
    }

    /// Writes factor() samples at the oversampled rate.
    void upsample(float x, float* u) noexcept { up_.upsample(x, u); }

    /// lin, res: factor() samples each. Returns D(lin) + DC(D_notch(res)).
    [[nodiscard]] float downsample(const float* lin, const float* res) noexcept {
        const float linBase = downLin_.downsample(lin);
        const float resBase = downRes_.downsample(res, [this](float v) noexcept {
            for (auto& n : notch_)
                v = n.process(v);
            return v;
        });
        dcIn_ = resBase;
        dcOut_ = dc_.process(resBase);
        return linBase + dcOut_;
    }

    /// True when every piece of recurrent state is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        if (!up_.stateFinite() || !downLin_.stateFinite() || !downRes_.stateFinite())
            return false;
        for (const auto& n : notch_) {
            if (!detail::isFinite(n.getZ1()) || !detail::isFinite(n.getZ2()))
                return false;
        }
        return detail::isFinite(dcIn_) && detail::isFinite(dcOut_);
    }

private:
    PolyphaseHalfbandOversampler up_, downLin_, downRes_;  // up_ upsamples only; down* decimate only
    std::array<Biquad, 3> notch_{};
    DCBlocker dc_;
    float dcIn_ = 0.0f, dcOut_ = 0.0f;                     // mirrors for stateFinite (harmonic_fold.h:312-313 pattern)
};

}  // namespace Krate::DSP
