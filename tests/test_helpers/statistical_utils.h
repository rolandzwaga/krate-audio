// ==============================================================================
// Test Helper: Statistical Utilities
// ==============================================================================
// Statistical computation functions for artifact detection and signal analysis.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/statistical_utils.h
// Namespace: Krate::DSP::TestUtils::StatisticalUtils
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-005, FR-008
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

// =============================================================================
// StatisticalUtils Namespace
// =============================================================================

namespace StatisticalUtils {

// -----------------------------------------------------------------------------
// Basic Statistics
// -----------------------------------------------------------------------------

/// @brief Compute arithmetic mean of data
/// @param data Pointer to data array
/// @param n Number of elements
/// @return Mean value, or 0 if n == 0
[[nodiscard]] inline float computeMean(const float* data, size_t n) noexcept {
    if (data == nullptr || n == 0) {
        return 0.0f;
    }

    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += data[i];
    }
    return sum / static_cast<float>(n);
}

/// @brief Compute sample variance using Bessel's correction (n-1 denominator)
/// @param data Pointer to data array
/// @param n Number of elements
/// @param mean Pre-computed mean value
/// @return Sample variance, or 0 if n <= 1
[[nodiscard]] inline float computeVariance(const float* data, size_t n, float mean) noexcept {
    if (data == nullptr || n <= 1) {
        return 0.0f;
    }

    float sumSquaredDiff = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float diff = data[i] - mean;
        sumSquaredDiff += diff * diff;
    }
    return sumSquaredDiff / static_cast<float>(n - 1);
}

/// @brief Compute sample standard deviation using Bessel's correction
/// @param data Pointer to data array
/// @param n Number of elements
/// @param mean Pre-computed mean value
/// @return Sample standard deviation, or 0 if n <= 1
[[nodiscard]] inline float computeStdDev(const float* data, size_t n, float mean) noexcept {
    return std::sqrt(computeVariance(data, n, mean));
}

// -----------------------------------------------------------------------------
// Robust Statistics
// -----------------------------------------------------------------------------

/// @brief Compute median value (MODIFIES data array by sorting in-place)
/// @param data Pointer to data array (will be sorted)
/// @param n Number of elements
/// @return Median value, or 0 if n == 0
/// @note For efficiency, this function sorts the data in-place. Make a copy
///       if you need to preserve the original order.
[[nodiscard]] inline float computeMedian(float* data, size_t n) noexcept {
    if (data == nullptr || n == 0) {
        return 0.0f;
    }

    std::sort(data, data + n);

    if (n % 2 == 0) {
        // Even: average of two middle values
        return (data[n / 2 - 1] + data[n / 2]) / 2.0f;
    } else {
        // Odd: middle value
        return data[n / 2];
    }
}

/// @brief Compute Median Absolute Deviation (MAD)
/// @param data Pointer to data array (will be modified)
/// @param n Number of elements
/// @param median Pre-computed median value
/// @return MAD value, or 0 if n == 0
/// @note MAD is a robust measure of spread, less affected by outliers than stddev
[[nodiscard]] inline float computeMAD(float* data, size_t n, float median) noexcept {
    if (data == nullptr || n == 0) {
        return 0.0f;
    }

    // Compute absolute deviations from median
    for (size_t i = 0; i < n; ++i) {
        data[i] = std::abs(data[i] - median);
    }

    // Return median of absolute deviations
    return computeMedian(data, n);
}

// -----------------------------------------------------------------------------
// Higher-Order Moments
// -----------------------------------------------------------------------------

/// @brief Compute nth central moment
/// @param data Pointer to data array
/// @param n Number of elements
/// @param mean Pre-computed mean value
/// @param order Moment order (1, 2, 3, 4, ...)
/// @return nth central moment, or 0 if n == 0
/// @note Central moments: E[(X - mu)^order]
///       - 1st central moment is always 0
///       - 2nd central moment is population variance
///       - 4th central moment is used for kurtosis
[[nodiscard]] inline float computeMoment(
    const float* data,
    size_t n,
    float mean,
    int order
) noexcept {
    if (data == nullptr || n == 0 || order < 1) {
        return 0.0f;
    }

    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float diff = data[i] - mean;
        float term = diff;
        for (int p = 1; p < order; ++p) {
            term *= diff;
        }
        sum += term;
    }
    return sum / static_cast<float>(n);
}

} // namespace StatisticalUtils

} // namespace TestUtils
} // namespace DSP
} // namespace Krate
