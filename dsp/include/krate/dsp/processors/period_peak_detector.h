// ==============================================================================
// Layer 2: DSP Processor - Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// PeriodPeakDetector: true-peak estimate with a segmented windowed-maximum hold, for a
// ripple-free level reading on a steady bass note. Includes Layer 0 and Layer 1 only.
//   Spec:  specs/profundum-phase6-mass-dynamics/spec.md (FR-004, FR-011 - FR-013)
//   Plan:  specs/profundum-phase6-mass-dynamics/plan.md (S1 C-4, C-6; S3; R-11)
//   Tests: dsp/tests/unit/processors/period_peak_detector_test.cpp (tag [period-peak])
//
// Recorded duplication of HarmonicFold's private detector (plan S1 C-4); a follow-up proposes
// HarmonicFold compose this class. HarmonicFold is not modified in Phase 6 (FR-044).
// truePeak() ports harmonic_fold.h advanceDetector's true-peak half line for line; pushHeld()
// ports its ring-max half, with a settable hold that takes effect at the next segment boundary
// and the output max(segMax, ringMax). There is no follower, attack or release here: those
// belong to the EnvelopeFollowers / DynamicsProcessor downstream.
//
// Contract: fixed size, zero heap, every method noexcept and allocation-free. Inputs must be
// finite (callers sanitise first). FTZ / DAZ is a precondition (the oversampler sections are
// not flushed). Latency 0: the oversampler's ~1.9-sample delay only affects the peak estimate,
// and the output is >= |x| of the current sample always.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

class PeriodPeakDetector {
public:
    static constexpr std::size_t kSegments = 8;              // == HarmonicFold::kDetectorSegments (test static_assert)
    static constexpr std::size_t kPeakOversampling = 4;      // == HarmonicFold::kDetectorPeakOversampling
    static constexpr float kMinHoldMs = 1.0f;
    static constexpr float kMaxHoldMs = 125.0f;              // HarmonicFold::kDetectorHoldMs (8 Hz floor)
    static constexpr std::size_t kMaxChannels = 2;

    static_assert((kSegments & (kSegments - 1)) == 0, "kSegments must be a power of two (ring wrap mask)");
    static_assert(kPeakOversampling <= PolyphaseHalfbandOversampler::kMaxFactor);

    /// Allocation-free. Sets the hold (same rules as setHoldMs) and its segment length at once,
    /// then reset().
    void prepare(double sampleRate, float holdMs) noexcept {
        sampleRate_ = sampleRate;
        for (auto& e : est_)
            e.os.setFactor(kPeakOversampling);
        setHoldMs(holdMs);
        segLen_ = pendingSegLen_;
        reset();
    }

    /// Zero the ring, the segment, the upsampler and the parabola state. A pending hold change
    /// takes effect here (a new segment starts).
    void reset() noexcept {
        for (auto& e : est_) {
            e.os.reset();
            e.buf.fill(0.0f);
            e.prev1 = 0.0f;
            e.prev2 = 0.0f;
        }
        ring_.fill(0.0f);
        ringMax_ = 0.0f;
        segMax_ = 0.0f;
        held_ = 0.0f;
        segCount_ = 0;
        ringPos_ = 0;
        segLen_ = pendingSegLen_;
    }

    /// Non-finite keeps the previous value; clamped to [kMinHoldMs, kMaxHoldMs]. holdMs() reports
    /// the new value at once; the segment length changes when the current segment completes.
    void setHoldMs(float holdMs) noexcept {
        if (!detail::isFinite(holdMs))
            return;
        holdMs_ = std::clamp(holdMs, kMinHoldMs, kMaxHoldMs);
        // segLen = max(1, ceil(round(hold · fs) / kSegments)), as harmonic_fold.h:145-147.
        const double holdSamples = std::round(static_cast<double>(holdMs_) * 1.0e-3 * sampleRate_);
        pendingSegLen_ = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::ceil(holdSamples / static_cast<double>(kSegments))));
    }

    /// One channel: the held true-peak of |x|.
    [[nodiscard]] float process(float x) noexcept { return pushHeld(truePeak(est_[0], x)); }

    /// Two channels: the held max(tp_L, tp_R).
    [[nodiscard]] float process(float xL, float xR) noexcept {
        const float dL = truePeak(est_[0], xL);
        const float dR = truePeak(est_[1], xR);
        return pushHeld(std::max(dL, dR));
    }

    /// The last output.
    [[nodiscard]] float held() const noexcept { return held_; }

    [[nodiscard]] float holdMs() const noexcept { return holdMs_; }

    /// True when every state value (oversampler sections included) is finite.
    [[nodiscard]] bool stateFinite() const noexcept {
        for (const auto& e : est_) {
            if (!e.os.stateFinite() || !detail::isFinite(e.prev1) || !detail::isFinite(e.prev2))
                return false;
            for (const float v : e.buf)
                if (!detail::isFinite(v))
                    return false;
        }
        for (const float r : ring_)
            if (!detail::isFinite(r))
                return false;
        return detail::isFinite(ringMax_) && detail::isFinite(segMax_) && detail::isFinite(held_);
    }

private:
    struct Estimator {
        PolyphaseHalfbandOversampler os;
        std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> buf{};
        float prev1 = 0.0f;
        float prev2 = 0.0f;
    };

    /// HarmonicFold advanceDetector's first half (harmonic_fold.h:403-417), verbatim: the larger
    /// of |x| and the parabolic-interpolated local maxima of |x| upsampled kPeakOversampling x.
    [[nodiscard]] static float truePeak(Estimator& e, float x) noexcept {
        float d = std::abs(x);
        e.os.upsample(x, e.buf.data());
        for (std::size_t i = 0; i < e.os.factor(); ++i) {
            const float d0 = std::abs(e.buf[i]);
            const float a = e.prev2;
            const float b = e.prev1;
            d = std::max(d, d0);
            if (b >= a && b > d0) {
                // a − 2b + d0 < 0 here (b >= a and b > d0), so the division is safe.
                const float offset = 0.5f * (a - d0) / (a - 2.0f * b + d0);
                d = std::max(d, b - 0.25f * (a - d0) * offset);
            }
            e.prev2 = b;
            e.prev1 = d0;
        }
        return d;
    }

    /// Its ring-max half (harmonic_fold.h:418-427), with the pending segment length taken when a
    /// segment completes; the output is max(segMax, ringMax).
    [[nodiscard]] float pushHeld(float d) noexcept {
        segMax_ = std::max(segMax_, d);
        if (++segCount_ >= segLen_) {
            ring_[ringPos_] = segMax_;
            ringPos_ = (ringPos_ + 1) & (kSegments - 1);
            segMax_ = 0.0f;
            segCount_ = 0;
            segLen_ = pendingSegLen_;
            ringMax_ = 0.0f;
            for (const float r : ring_)
                ringMax_ = std::max(ringMax_, r);
        }
        held_ = std::max(segMax_, ringMax_);
        return held_;
    }

    std::array<Estimator, kMaxChannels> est_{};
    std::array<float, kSegments> ring_{};
    float ringMax_ = 0.0f;
    float segMax_ = 0.0f;
    float held_ = 0.0f;
    std::size_t segCount_ = 0;
    std::size_t ringPos_ = 0;
    std::size_t segLen_ = 1;
    std::size_t pendingSegLen_ = 1;
    double sampleRate_ = 48000.0;
    float holdMs_ = 50.0f;
};

}  // namespace Krate::DSP
