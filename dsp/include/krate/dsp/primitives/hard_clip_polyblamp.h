// ==============================================================================
// Layer 1: Primitives - Hard Clip with polyBLAMP
// ==============================================================================
// Anti-aliased hard clipping using polyBLAMP (Polynomial Bandlimited Ramp)
// correction. polyBLAMP corrects derivative discontinuities at clipping
// transitions by spreading a polynomial correction across multiple samples.
//
// Based on:
// - DAFx-16 paper "Rounding Corners with BLAMP" (Esqueda, Välimäki, Bilbao)
// - Martin Finke's PolyBLEP implementation
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
// - Principle XI: Performance Budget (< 2x naive hard clip per sample)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/core/sigmoid.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// HardClipPolyBLAMP Class
// =============================================================================

/// @brief Anti-aliased hard clipping using 4-point polyBLAMP correction.
///
/// Uses a 2-sample delay to properly spread the derivative discontinuity
/// correction across 4 samples using a cubic B-spline kernel.
class HardClipPolyBLAMP {
public:
    HardClipPolyBLAMP() noexcept { reset(); }

    HardClipPolyBLAMP(const HardClipPolyBLAMP&) = default;
    HardClipPolyBLAMP& operator=(const HardClipPolyBLAMP&) = default;
    HardClipPolyBLAMP(HardClipPolyBLAMP&&) noexcept = default;
    HardClipPolyBLAMP& operator=(HardClipPolyBLAMP&&) noexcept = default;
    ~HardClipPolyBLAMP() = default;

    void setThreshold(float threshold) noexcept {
        threshold_ = (threshold < 0.0f) ? -threshold : threshold;
    }

    void reset() noexcept {
        for (size_t i = 0; i < kBufferSize; ++i) {
            xBuffer_[i] = 0.0f;
            yBuffer_[i] = 0.0f;
            corrections_[i] = 0.0f;
        }
        writeIdx_ = 0;
        sampleCount_ = 0;
        // Note: threshold_ is NOT reset - it's a configuration parameter
    }

    [[nodiscard]] float getThreshold() const noexcept {
        return threshold_;
    }

    [[nodiscard]] float process(float x) noexcept {
        if (threshold_ == 0.0f) {
            return 0.0f;
        }

        if (detail::isNaN(x)) {
            return x;
        }

        if (detail::isInf(x)) {
            x = (x > 0.0f) ? threshold_ * 10.0f : -threshold_ * 10.0f;
        }

        // Hard clip the input
        const float y = Sigmoid::hardClip(x, threshold_);

        // Store in circular buffers
        const size_t currIdx = writeIdx_;
        const size_t prevIdx = (writeIdx_ + kBufferSize - 1) % kBufferSize;

        xBuffer_[currIdx] = x;
        yBuffer_[currIdx] = y;
        // NOTE: Do NOT reset corrections_[currIdx] here!
        // This slot may have accumulated corrections from previous crossings
        // when it was in the "future" position (idx_p1).

        // Advance write pointer
        writeIdx_ = (writeIdx_ + 1) % kBufferSize;
        ++sampleCount_;

        // Need at least 3 samples before we can output with corrections
        if (sampleCount_ < 3) {
            return y;
        }

        // Check for threshold crossing between previous and current sample
        const float x0 = xBuffer_[prevIdx];
        const float x1 = x;
        const float dx = x1 - x0;

        if (dx != 0.0f) {
            // Per DAFx-16 paper: scale by "magnitude and direction (i.e. rising or
            // falling edge) of the discontinuity introduced in the first derivative"
            //
            // For hard clipping, derivative of output w.r.t. time:
            // - In linear region: dy/dt = dx/dt (follows input)
            // - In clipped region: dy/dt = 0 (constant output)
            //
            // Derivative discontinuity magnitude:
            // - Entering clipping: Δslope = 0 - dx = -dx
            // - Leaving clipping: Δslope = dx - 0 = +dx

            // Entering positive clipping (signal rising through threshold)
            // The residual should pull DOWN the peak to round the corner.
            // Negative correction: -dx because we're losing the slope.
            if (x0 < threshold_ && x1 > threshold_) {
                const float d = (threshold_ - x0) / dx;
                applyCorrection(d, -dx, prevIdx, currIdx);
            }
            // Entering negative clipping (signal falling through -threshold)
            // Negative correction (but dx is negative, so -dx is positive)
            else if (x0 > -threshold_ && x1 < -threshold_) {
                const float d = (-threshold_ - x0) / dx;
                applyCorrection(d, -dx, prevIdx, currIdx);
            }
            // Leaving positive clipping (signal falling from above threshold)
            // Positive correction to smooth the exit.
            if (x0 > threshold_ && x1 < threshold_) {
                const float d = (threshold_ - x0) / dx;
                applyCorrection(d, dx, prevIdx, currIdx);
            }
            // Leaving negative clipping (signal rising from below -threshold)
            else if (x0 < -threshold_ && x1 > -threshold_) {
                const float d = (-threshold_ - x0) / dx;
                applyCorrection(d, dx, prevIdx, currIdx);
            }
        }

        // Output the sample from 2 positions ago (with full corrections applied)
        const size_t outputIdx = (writeIdx_ + kBufferSize - 3) % kBufferSize;
        const float output = yBuffer_[outputIdx] + corrections_[outputIdx];

        // Reset correction for this slot AFTER reading it, ready for reuse
        corrections_[outputIdx] = 0.0f;

        return output;
    }

    void processBlock(float* buffer, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Static BLAMP Residual Functions (4-point kernel)
    // =========================================================================

    /// Cubic B-spline basis function for 4-point kernel.
    /// This is the fundamental building block - NOT the BLAMP residual directly.
    [[nodiscard]] static float blamp4(float t) noexcept {
        if (t < 0.0f || t >= 4.0f) return 0.0f;

        if (t < 1.0f) {
            return (t * t * t) / 6.0f;
        } else if (t < 2.0f) {
            const float u = t - 1.0f;
            return (-3.0f * u * u * u + 3.0f * u * u + 3.0f * u + 1.0f) / 6.0f;
        } else if (t < 3.0f) {
            const float u = t - 2.0f;
            return (3.0f * u * u * u - 6.0f * u * u + 4.0f) / 6.0f;
        } else {
            const float diff = 4.0f - t;
            return (diff * diff * diff) / 6.0f;
        }
    }

    /// Four-point polyBLAMP residual from DAFx-16 paper "Rounding Corners with BLAMP"
    /// (Esqueda, Välimäki, Bilbao) - Table 1
    ///
    /// For d ∈ [0, 1) representing fractional position of discontinuity between
    /// samples -1 and 0. The residual is evaluated at each of the 4 neighboring samples.
    ///
    /// @param d Fractional position of discontinuity [0, 1]
    /// @param n Sample position relative to crossing: -2, -1, 0, or +1
    [[nodiscard]] static float blampResidual(float d, int n) noexcept {
        const float d2 = d * d;
        const float d3 = d2 * d;
        const float d4 = d3 * d;
        const float d5 = d4 * d;

        // Four-point polyBLAMP residual coefficients from Table 1 of DAFx-16 paper
        // The spans correspond to sample positions relative to the crossing point
        switch (n) {
            case -2:
                // Span [−2T, −T]: d⁵/120
                return d5 / 120.0f;

            case -1:
                // Span [−T, 0]: −d⁵/40 + d⁴/24 + d³/12 + d²/12 + d/24 + 1/120
                return -d5 / 40.0f + d4 / 24.0f + d3 / 12.0f + d2 / 12.0f + d / 24.0f + 1.0f / 120.0f;

            case 0:
                // Span [0, T]: d⁵/40 − d⁴/12 + d²/3 − d/2 + 7/30
                // Per https://ryukau.github.io/filter_notes/polyblep_residual/
                return d5 / 40.0f - d4 / 12.0f + d2 / 3.0f - d / 2.0f + 7.0f / 30.0f;

            case 1:
                // Span [T, 2T]: −d⁵/120 + d⁴/24 − d³/12 + d²/12 − d/24 + 1/120
                return -d5 / 120.0f + d4 / 24.0f - d3 / 12.0f + d2 / 12.0f - d / 24.0f + 1.0f / 120.0f;

            default:
                return 0.0f;
        }
    }

    // Backwards compatibility
    [[nodiscard]] static float polyBlampResidual(float d) noexcept {
        return blampResidual(d, 0) + blampResidual(d, -1);
    }

    [[nodiscard]] static float polyBlampAfter(float d) noexcept {
        return blampResidual(d, 0);
    }

    [[nodiscard]] static float polyBlampBefore(float d) noexcept {
        return blampResidual(d, -1);
    }

private:
    static constexpr size_t kBufferSize = 4;

    void applyCorrection(float d, float slopeChange, size_t prevIdx, size_t currIdx) noexcept {
        // Clamp d to valid range
        if (d < 0.0f) d = 0.0f;
        if (d > 1.0f) d = 1.0f;

        // Apply 4-point polyBLAMP correction kernel (from DAFx-16 paper Table 1)
        // The crossing is between prevIdx (sample -1) and currIdx (sample 0)
        const size_t idx_m2 = (prevIdx + kBufferSize - 1) % kBufferSize;  // sample -2
        const size_t idx_m1 = prevIdx;                                     // sample -1
        const size_t idx_0 = currIdx;                                      // sample 0
        const size_t idx_p1 = (currIdx + 1) % kBufferSize;                // sample +1

        // Apply residuals scaled by the slope discontinuity magnitude
        // Note: Residual is SUBTRACTED (negated) to correct naive signal toward bandlimited
        corrections_[idx_m2] -= slopeChange * blampResidual(d, -2);
        corrections_[idx_m1] -= slopeChange * blampResidual(d, -1);
        corrections_[idx_0]  -= slopeChange * blampResidual(d, 0);
        corrections_[idx_p1] -= slopeChange * blampResidual(d, 1);
    }

    std::array<float, kBufferSize> xBuffer_{};    // Circular buffer of unclipped inputs
    std::array<float, kBufferSize> yBuffer_{};    // Circular buffer of clipped outputs
    std::array<float, kBufferSize> corrections_{}; // Accumulated corrections
    size_t writeIdx_{0};
    size_t sampleCount_{0};
    float threshold_{1.0f};  // Default threshold
};

} // namespace DSP
} // namespace Krate
