// ==============================================================================
// Unit Tests: Statistical Utilities
// ==============================================================================
// Tests for statistical computation functions used by artifact detection.
//
// Constitution Compliance:
// - Principle XII: Test-First Development (tests written FIRST)
// - Principle VIII: Testing Discipline
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-005, FR-008
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "statistical_utils.h"

#include <array>
#include <cmath>
#include <numeric>

using namespace Krate::DSP::TestUtils;
using Catch::Approx;

// =============================================================================
// T005: Tests for StatisticalUtils
// =============================================================================

TEST_CASE("StatisticalUtils::computeMean - computes arithmetic mean", "[statistical-utils][mean]") {
    SECTION("mean of simple values") {
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        REQUIRE(mean == Approx(3.0f).margin(1e-6f));
    }

    SECTION("mean of zeros") {
        std::array<float, 4> data = {0.0f, 0.0f, 0.0f, 0.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        REQUIRE(mean == Approx(0.0f).margin(1e-6f));
    }

    SECTION("mean of single value") {
        std::array<float, 1> data = {42.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        REQUIRE(mean == Approx(42.0f).margin(1e-6f));
    }

    SECTION("mean of negative values") {
        std::array<float, 4> data = {-1.0f, -2.0f, -3.0f, -4.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        REQUIRE(mean == Approx(-2.5f).margin(1e-6f));
    }

    SECTION("empty data returns zero") {
        const float mean = StatisticalUtils::computeMean(nullptr, 0);
        REQUIRE(mean == 0.0f);
    }
}

TEST_CASE("StatisticalUtils::computeVariance - computes sample variance", "[statistical-utils][variance]") {
    SECTION("variance of values with known variance") {
        // Data: {1, 2, 3, 4, 5}, mean = 3
        // Sample variance = sum((x-mean)^2) / (n-1) = (4+1+0+1+4)/4 = 2.5
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float variance = StatisticalUtils::computeVariance(data.data(), data.size(), mean);
        REQUIRE(variance == Approx(2.5f).margin(1e-6f));
    }

    SECTION("variance of identical values is zero") {
        std::array<float, 5> data = {7.0f, 7.0f, 7.0f, 7.0f, 7.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float variance = StatisticalUtils::computeVariance(data.data(), data.size(), mean);
        REQUIRE(variance == Approx(0.0f).margin(1e-6f));
    }

    SECTION("variance of single value is zero") {
        std::array<float, 1> data = {5.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float variance = StatisticalUtils::computeVariance(data.data(), data.size(), mean);
        REQUIRE(variance == 0.0f);  // Division by (n-1) = 0, handled specially
    }
}

TEST_CASE("StatisticalUtils::computeStdDev - computes sample standard deviation", "[statistical-utils][stddev]") {
    SECTION("stddev of values with known variance") {
        // Data: {1, 2, 3, 4, 5}, variance = 2.5, stddev = sqrt(2.5) ~= 1.5811
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float stddev = StatisticalUtils::computeStdDev(data.data(), data.size(), mean);
        REQUIRE(stddev == Approx(std::sqrt(2.5f)).margin(1e-5f));
    }

    SECTION("stddev of identical values is zero") {
        std::array<float, 4> data = {3.0f, 3.0f, 3.0f, 3.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float stddev = StatisticalUtils::computeStdDev(data.data(), data.size(), mean);
        REQUIRE(stddev == Approx(0.0f).margin(1e-6f));
    }

    SECTION("stddev uses Bessel's correction (n-1 denominator)") {
        // Data: {0, 4}, mean = 2
        // Population variance = ((0-2)^2 + (4-2)^2) / 2 = 4
        // Sample variance = ((0-2)^2 + (4-2)^2) / 1 = 8
        // Sample stddev = sqrt(8) ~= 2.828
        std::array<float, 2> data = {0.0f, 4.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float stddev = StatisticalUtils::computeStdDev(data.data(), data.size(), mean);
        REQUIRE(stddev == Approx(std::sqrt(8.0f)).margin(1e-5f));
    }
}

TEST_CASE("StatisticalUtils::computeMedian - computes median value", "[statistical-utils][median]") {
    SECTION("median of odd-sized array") {
        std::array<float, 5> data = {3.0f, 1.0f, 4.0f, 1.0f, 5.0f};
        // Sorted: {1, 1, 3, 4, 5} -> median = 3
        const float median = StatisticalUtils::computeMedian(data.data(), data.size());
        REQUIRE(median == Approx(3.0f).margin(1e-6f));
    }

    SECTION("median of even-sized array") {
        std::array<float, 4> data = {1.0f, 2.0f, 3.0f, 4.0f};
        // Sorted: {1, 2, 3, 4} -> median = (2+3)/2 = 2.5
        const float median = StatisticalUtils::computeMedian(data.data(), data.size());
        REQUIRE(median == Approx(2.5f).margin(1e-6f));
    }

    SECTION("median of single value") {
        std::array<float, 1> data = {7.0f};
        const float median = StatisticalUtils::computeMedian(data.data(), data.size());
        REQUIRE(median == Approx(7.0f).margin(1e-6f));
    }

    SECTION("median of already sorted array") {
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float median = StatisticalUtils::computeMedian(data.data(), data.size());
        REQUIRE(median == Approx(3.0f).margin(1e-6f));
    }

    SECTION("median sorts data in place") {
        std::array<float, 5> data = {5.0f, 3.0f, 1.0f, 4.0f, 2.0f};
        [[maybe_unused]] const float median = StatisticalUtils::computeMedian(data.data(), data.size());
        // Data should now be sorted
        REQUIRE(data[0] <= data[1]);
        REQUIRE(data[1] <= data[2]);
        REQUIRE(data[2] <= data[3]);
        REQUIRE(data[3] <= data[4]);
    }

    SECTION("median of empty array returns zero") {
        const float median = StatisticalUtils::computeMedian(nullptr, 0);
        REQUIRE(median == 0.0f);
    }
}

TEST_CASE("StatisticalUtils::computeMAD - computes Median Absolute Deviation", "[statistical-utils][mad]") {
    SECTION("MAD of symmetric distribution") {
        // Data: {1, 2, 3, 4, 5}, median = 3
        // |1-3|=2, |2-3|=1, |3-3|=0, |4-3|=1, |5-3|=2
        // Sorted deviations: {0, 1, 1, 2, 2} -> MAD = 1
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float median = StatisticalUtils::computeMedian(data.data(), data.size());
        // Need to restore data for MAD calculation (median sorts in-place)
        data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float mad = StatisticalUtils::computeMAD(data.data(), data.size(), median);
        REQUIRE(mad == Approx(1.0f).margin(1e-6f));
    }

    SECTION("MAD with outliers") {
        // Data: {1, 2, 3, 4, 100}, median = 3
        // |1-3|=2, |2-3|=1, |3-3|=0, |4-3|=1, |100-3|=97
        // Sorted deviations: {0, 1, 1, 2, 97} -> MAD = 1
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 100.0f};
        const float median = 3.0f;  // Pre-computed
        const float mad = StatisticalUtils::computeMAD(data.data(), data.size(), median);
        REQUIRE(mad == Approx(1.0f).margin(1e-6f));
    }

    SECTION("MAD of identical values is zero") {
        std::array<float, 4> data = {5.0f, 5.0f, 5.0f, 5.0f};
        const float median = 5.0f;
        const float mad = StatisticalUtils::computeMAD(data.data(), data.size(), median);
        REQUIRE(mad == Approx(0.0f).margin(1e-6f));
    }
}

TEST_CASE("StatisticalUtils::computeMoment - computes central moments", "[statistical-utils][moment]") {
    SECTION("second moment equals variance times (n-1)/n") {
        // For 2nd moment (population variance), we use n denominator
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float m2 = StatisticalUtils::computeMoment(data.data(), data.size(), mean, 2);
        // Second central moment = sum((x-mean)^2) / n = 2.0
        REQUIRE(m2 == Approx(2.0f).margin(1e-5f));
    }

    SECTION("fourth moment for kurtosis calculation") {
        // Data: {1, 2, 3, 4, 5}, mean = 3
        // (x-mean)^4: 16, 1, 0, 1, 16, sum = 34
        // Fourth moment = 34/5 = 6.8
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float m4 = StatisticalUtils::computeMoment(data.data(), data.size(), mean, 4);
        REQUIRE(m4 == Approx(6.8f).margin(1e-5f));
    }

    SECTION("first moment is zero (centered)") {
        std::array<float, 5> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        const float mean = StatisticalUtils::computeMean(data.data(), data.size());
        const float m1 = StatisticalUtils::computeMoment(data.data(), data.size(), mean, 1);
        REQUIRE(m1 == Approx(0.0f).margin(1e-5f));
    }
}
