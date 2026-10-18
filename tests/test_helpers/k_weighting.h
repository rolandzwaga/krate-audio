// ==============================================================================
// Test Helper: ITU-R BS.1770 K-weighting
// ==============================================================================
// Test-side helper for spec FR-050 / SC-003 (Profundum Phase 6,
// specs/profundum-phase6-mass-dynamics). K-weighting rolls off below about
// 38 Hz and under-reads sub-level drift (spec Descriptor definitions).
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location:  tests/test_helpers/k_weighting.h
// Namespace: Krate::DSP::TestUtils
//
// No CMake edit is needed: tests/test_helpers/CMakeLists.txt declares
// `add_library(test_helpers INTERFACE)` with no source list.
//
// The coefficients are derived from the Recommendation's analog prototype for
// any sample rate (high shelf + RLB high-pass, bilinear transform with
// K = tan(pi f0 / fs)); at 48 kHz they reproduce the published BS.1770-4
// tables. Validated in dsp/tests/unit/systems/k_weighting_helper_test.cpp
// (KWeighting_ReferenceLevels, SC-026).
// ==============================================================================

#pragma once

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>

namespace Krate::DSP::TestUtils {

/// Two biquad stages: stage 1 = high-shelf pre-filter, stage 2 = RLB high-pass.
/// a[0] is always 1.
struct KWeightingCoefficients {
    std::array<double, 3> b1;
    std::array<double, 3> a1;
    std::array<double, 3> b2;
    std::array<double, 3> a2;
};

inline KWeightingCoefficients kWeightingCoefficients(double fs) {
    constexpr double kPi = 3.14159265358979323846;
    KWeightingCoefficients c{};

    // Stage 1: high shelf.
    {
        constexpr double f0 = 1681.974450955533;
        constexpr double gainDb = 3.999843853973347;
        constexpr double q = 0.7071752369554196;
        const double vh = std::pow(10.0, gainDb / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double k = std::tan(kPi * f0 / fs);
        const double a0 = 1.0 + k / q + k * k;
        c.b1 = {(vh + vb * k / q + k * k) / a0,
                2.0 * (k * k - vh) / a0,
                (vh - vb * k / q + k * k) / a0};
        c.a1 = {1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
    }

    // Stage 2: RLB high-pass.
    {
        constexpr double f0 = 38.13547087602444;
        constexpr double q = 0.5003270373238773;
        const double k = std::tan(kPi * f0 / fs);
        const double den = 1.0 + k / q + k * k;
        c.b2 = {1.0, -2.0, 1.0};
        c.a2 = {1.0, 2.0 * (k * k - 1.0) / den, (1.0 - k / q + k * k) / den};
    }
    return c;
}

/// Double-precision K-weighting filter (two transposed direct form II biquads).
class KWeightingFilter {
public:
    void prepare(double fs) {
        coeffs_ = kWeightingCoefficients(fs);
        reset();
    }

    void reset() {
        s1z1_ = 0.0;
        s1z2_ = 0.0;
        s2z1_ = 0.0;
        s2z2_ = 0.0;
    }

    double process(double x) {
        const auto& c = coeffs_;
        const double y1 = c.b1[0] * x + s1z1_;
        s1z1_ = c.b1[1] * x - c.a1[1] * y1 + s1z2_;
        s1z2_ = c.b1[2] * x - c.a1[2] * y1;

        const double y2 = c.b2[0] * y1 + s2z1_;
        s2z1_ = c.b2[1] * y1 - c.a2[1] * y2 + s2z2_;
        s2z2_ = c.b2[2] * y1 - c.a2[2] * y2;
        return y2;
    }

private:
    KWeightingCoefficients coeffs_{};
    double s1z1_ = 0.0;
    double s1z2_ = 0.0;
    double s2z1_ = 0.0;
    double s2z2_ = 0.0;
};

/// K-weighted level (LKFS, one channel) of x[measureFrom, n): a fresh filter
/// runs over x[0, n) so the measured window can start past the filter
/// transient. Returns -0.691 + 10 log10(mean square), or -144 for zero.
inline double kWeightedLevelDb(const float* x, std::size_t n, double fs,
                               std::size_t measureFrom = 0) {
    REQUIRE(measureFrom < n);
    KWeightingFilter filter;
    filter.prepare(fs);
    double sumSq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double y = filter.process(static_cast<double>(x[i]));
        if (i >= measureFrom) {
            sumSq += y * y;
        }
    }
    const double meanSq = sumSq / static_cast<double>(n - measureFrom);
    if (meanSq <= 0.0) {
        return -144.0;
    }
    return -0.691 + 10.0 * std::log10(meanSq);
}

}  // namespace Krate::DSP::TestUtils
