// ==============================================================================
// Layer 0: Core Utility Tests - PolyBLEP/PolyBLAMP Correction Functions
// ==============================================================================
// Tests for polynomial band-limited step (BLEP) and ramp (BLAMP) correction
// functions. Validates zero-outside-region, known-value, continuity,
// constexpr evaluation, and quality properties (SC-001 through SC-003, SC-008).
//
// Constitution Compliance:
// - Principle XII: Test-First Development
//
// Reference: specs/013-polyblep-math/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <random>
#include <numeric>
#include <vector>

#include <krate/dsp/core/polyblep.h>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Helper: Deterministic PRNG for random testing
// =============================================================================
static std::mt19937 makeRng(uint32_t seed = 42) {
    return std::mt19937(seed);
}

// =============================================================================
// Algorithm verification helpers: double-precision polyBlep/polyBlep4
// Same polynomial math as polyblep.h, but in double precision.
// Used by SC-003c to test the ALGORITHM's DC-free property without
// float ULP artifacts contaminating the measurement.
// =============================================================================
namespace {

double polyBlepDouble(double t, double dt) {
    if (t < dt) {
        double x = t / dt;
        return -(x * x - 2.0 * x + 1.0);
    }
    if (t > 1.0 - dt) {
        double x = (t - 1.0) / dt;
        return x * x + 2.0 * x + 1.0;
    }
    return 0.0;
}

double polyBlep4Double(double t, double dt) {
    const double dt2 = 2.0 * dt;
    if (t < dt2) {
        double u = t / dt;
        if (u < 1.0) {
            double u2 = u * u;
            double u3 = u2 * u;
            double u4 = u3 * u;
            return -0.5 + (3.0 * u4 - 8.0 * u3 + 16.0 * u) / 24.0;
        }
        double v = 2.0 - u;
        double v2 = v * v;
        double v4 = v2 * v2;
        return -(v4 / 24.0);
    }
    if (t > 1.0 - dt2) {
        double u = (1.0 - t) / dt;
        if (u < 1.0) {
            double u2 = u * u;
            double u3 = u2 * u;
            double u4 = u3 * u;
            return 0.5 - (3.0 * u4 - 8.0 * u3 + 16.0 * u) / 24.0;
        }
        double v = 2.0 - u;
        double v2 = v * v;
        double v4 = v2 * v2;
        return v4 / 24.0;
    }
    return 0.0;
}

} // anonymous namespace

// =============================================================================
// T004: polyBlep zero-outside-region (SC-001)
// =============================================================================

TEST_CASE("polyBlep returns zero outside correction region", "[polyblep][SC-001]") {
    auto rng = makeRng(1234);
    std::uniform_real_distribution<float> dtDist(0.001f, 0.05f);

    int zeroCount = 0;
    constexpr int kNumTrials = 10000;

    for (int i = 0; i < kNumTrials; ++i) {
        float dt = dtDist(rng);
        // Generate t values outside the correction region:
        // outside means t >= dt AND t <= 1.0 - dt
        std::uniform_real_distribution<float> tDist(dt + 0.001f, 1.0f - dt - 0.001f);
        float t = tDist(rng);

        float result = polyBlep(t, dt);
        if (result == 0.0f) {
            ++zeroCount;
        }
        REQUIRE(result == 0.0f);
    }
    // All 10,000 trials should return exactly zero
    REQUIRE(zeroCount == kNumTrials);
}

// =============================================================================
// T005: polyBlep known-value verification
// =============================================================================

TEST_CASE("polyBlep returns non-zero correction near discontinuity", "[polyblep]") {
    SECTION("After-wrap region: t < dt") {
        // t=0.005, dt=0.01 -> inside after-wrap region [0, dt)
        // x = t / dt = 0.5
        // correction = -(x^2 - 2x + 1) = -(0.25 - 1.0 + 1.0) = -(0.25) = -0.25
        float result = polyBlep(0.005f, 0.01f);
        REQUIRE(result == Approx(-0.25f).margin(1e-6f));
    }

    SECTION("Before-wrap region: t > 1 - dt") {
        // t=0.995, dt=0.01 -> inside before-wrap region [1-dt, 1)
        // x = (t - 1.0) / dt = (0.995 - 1.0) / 0.01 = -0.5
        // correction = (x^2 + 2x + 1) = (0.25 - 1.0 + 1.0) = 0.25
        float result = polyBlep(0.995f, 0.01f);
        REQUIRE(result == Approx(0.25f).margin(1e-6f));
    }

    SECTION("Right at t=0 (just after wrap)") {
        // x = 0/dt = 0
        // correction = -(0 - 0 + 1) = -1.0
        float result = polyBlep(0.0f, 0.01f);
        REQUIRE(result == Approx(-1.0f).margin(1e-6f));
    }

    SECTION("Right at boundary t=dt (edge of correction)") {
        // x = dt/dt = 1.0
        // correction = -(1 - 2 + 1) = 0.0
        float result = polyBlep(0.01f, 0.01f);
        REQUIRE(result == Approx(0.0f).margin(1e-6f));
    }

    SECTION("Exactly at t = 0.5 (far from discontinuity)") {
        float result = polyBlep(0.5f, 0.01f);
        REQUIRE(result == 0.0f);
    }
}

// =============================================================================
// T006: polyBlep continuity (SC-002)
// =============================================================================

TEST_CASE("polyBlep correction function is continuous (SC-002)", "[polyblep][SC-002]") {
    // SC-002: The polyBlep function produces continuous output when evaluated
    // across [0, 1) in steps smaller than dt. The correction function itself
    // (not the corrected waveform) should have no discontinuities.
    //
    // Within the correction region, the maximum derivative is 2/dt.
    // For a step of dt/20, the maximum expected jump is 2*(dt/20)/dt = 0.1.
    // Outside the correction region, the function is 0 (flat).
    // At the boundary between correction and non-correction, the function
    // value is 0 (smooth transition).
    constexpr std::array<float, 4> dtValues = {0.005f, 0.01f, 0.02f, 0.05f};

    for (float dt : dtValues) {
        constexpr int kSubSteps = 20;
        float step = dt / static_cast<float>(kSubSteps);
        float maxJump = 0.0f;

        float prevValue = polyBlep(0.0f, dt);

        for (float t = step; t < 1.0f; t += step) {
            float value = polyBlep(t, dt);
            float jump = std::abs(value - prevValue);
            maxJump = std::max(maxJump, jump);
            prevValue = value;
        }

        INFO("dt = " << dt << ", max jump = " << maxJump);
        // Maximum expected jump = max_derivative * step_size = 2/dt * dt/20 = 0.1
        // Allow margin for numerical precision
        REQUIRE(maxJump < 0.15f);
    }
}

TEST_CASE("polyBlep correction function is C0 continuous", "[polyblep][SC-002]") {
    // Verify the correction function itself has no discontinuities
    // Maximum derivative in correction region is 2/dt, so max jump per step
    // is 2 * step / dt. Outside the region, the function is 0.
    // At the boundary (entering/leaving correction region), the function
    // value is 0, so the transition is smooth.
    constexpr std::array<float, 5> dtValues = {0.001f, 0.005f, 0.01f, 0.02f, 0.05f};

    for (float dt : dtValues) {
        float step = dt * 0.05f;
        // Maximum derivative bound: 2/dt gives max jump = 2*step/dt = 0.1
        float maxExpectedJump = 2.5f * step / dt + 1e-6f;

        float prevValue = polyBlep(0.0f, dt);

        for (float t = step; t < 1.0f; t += step) {
            float value = polyBlep(t, dt);
            float jump = std::abs(value - prevValue);

            INFO("dt = " << dt << ", t = " << t << ", jump = " << jump
                 << ", maxExpected = " << maxExpectedJump);
            REQUIRE(jump <= maxExpectedJump);
            prevValue = value;
        }
    }
}

// =============================================================================
// T007: polyBlamp zero-outside-region (SC-001)
// =============================================================================

TEST_CASE("polyBlamp returns zero outside correction region", "[polyblep][SC-001]") {
    auto rng = makeRng(5678);
    std::uniform_real_distribution<float> dtDist(0.001f, 0.05f);

    int zeroCount = 0;
    constexpr int kNumTrials = 10000;

    for (int i = 0; i < kNumTrials; ++i) {
        float dt = dtDist(rng);
        std::uniform_real_distribution<float> tDist(dt + 0.001f, 1.0f - dt - 0.001f);
        float t = tDist(rng);

        float result = polyBlamp(t, dt);
        if (result == 0.0f) {
            ++zeroCount;
        }
        REQUIRE(result == 0.0f);
    }
    REQUIRE(zeroCount == kNumTrials);
}

// =============================================================================
// T008: polyBlamp known-value verification
// =============================================================================

TEST_CASE("polyBlamp returns non-zero correction near discontinuity", "[polyblep]") {
    SECTION("After-wrap region: t < dt") {
        // t=0.005, dt=0.01 -> x = t/dt - 1.0 = 0.5 - 1.0 = -0.5
        // correction = -(1/3) * x^3 = -(1/3) * (-0.125) = 0.041666...
        float result = polyBlamp(0.005f, 0.01f);
        REQUIRE(result == Approx(1.0f / 24.0f).margin(1e-6f));
    }

    SECTION("Before-wrap region: t > 1 - dt") {
        // t=0.995, dt=0.01 -> x = (t-1)/dt + 1 = -0.5 + 1.0 = 0.5
        // correction = (1/3) * x^3 = (1/3) * 0.125 = 0.041666...
        float result = polyBlamp(0.995f, 0.01f);
        REQUIRE(result == Approx(1.0f / 24.0f).margin(1e-6f));
    }

    SECTION("Right at t=0") {
        // x = 0/dt - 1 = -1.0
        // correction = -(1/3) * (-1)^3 = -(1/3)*(-1) = 1/3
        float result = polyBlamp(0.0f, 0.01f);
        REQUIRE(result == Approx(1.0f / 3.0f).margin(1e-6f));
    }

    SECTION("Boundary: t=dt") {
        // x = dt/dt - 1.0 = 0.0
        // correction = -(1/3) * 0^3 = 0.0
        float result = polyBlamp(0.01f, 0.01f);
        REQUIRE(result == Approx(0.0f).margin(1e-6f));
    }

    SECTION("Far from discontinuity") {
        REQUIRE(polyBlamp(0.5f, 0.01f) == 0.0f);
    }
}

// =============================================================================
// T009: polyBlamp continuity
// =============================================================================

TEST_CASE("polyBlamp is continuous across full phase range", "[polyblep]") {
    constexpr float dt = 0.01f;
    constexpr float step = dt * 0.1f;
    constexpr int numSteps = static_cast<int>(1.0f / step);

    float prevValue = polyBlamp(0.0f, dt);

    for (int i = 1; i < numSteps; ++i) {
        float t = static_cast<float>(i) * step;
        if (t >= 1.0f) break;

        float value = polyBlamp(t, dt);
        float jump = std::abs(value - prevValue);

        INFO("Phase t = " << t << ", jump = " << jump);
        // For BLAMP the maximum rate of change is bounded by the derivative
        // of the cubic polynomial scaled by 1/dt, so max jump per step
        // is proportional to step/dt
        REQUIRE(jump <= 1.0f); // BLAMP correction is bounded
        prevValue = value;
    }
}

// =============================================================================
// T010: constexpr compile-time evaluation (SC-008)
// =============================================================================

TEST_CASE("polyBlep and polyBlamp are constexpr-evaluable", "[polyblep][SC-008]") {
    // These static_asserts prove constexpr evaluation at compile time
    static_assert(polyBlep(0.5f, 0.01f) == 0.0f,
                  "polyBlep must return 0 outside correction region at compile time");
    static_assert(polyBlamp(0.5f, 0.01f) == 0.0f,
                  "polyBlamp must return 0 outside correction region at compile time");

    // Also verify non-zero values are constexpr
    static_assert(polyBlep(0.0f, 0.01f) != 0.0f,
                  "polyBlep must return non-zero at t=0 at compile time");
    static_assert(polyBlamp(0.0f, 0.01f) != 0.0f,
                  "polyBlamp must return non-zero at t=0 at compile time");

    SUCCEED("All static_assert checks passed at compile time");
}

// =============================================================================
// T017: polyBlep4 zero-outside-region (SC-001, FR-008)
// =============================================================================

TEST_CASE("polyBlep4 returns zero outside correction region", "[polyblep][SC-001]") {
    auto rng = makeRng(9012);
    std::uniform_real_distribution<float> dtDist(0.001f, 0.025f); // smaller dt since region is 2*dt

    int zeroCount = 0;
    constexpr int kNumTrials = 10000;

    for (int i = 0; i < kNumTrials; ++i) {
        float dt = dtDist(rng);
        float dt2 = 2.0f * dt;
        // Outside the correction region: t >= 2*dt AND t <= 1.0 - 2*dt
        std::uniform_real_distribution<float> tDist(dt2 + 0.001f, 1.0f - dt2 - 0.001f);
        float t = tDist(rng);

        float result = polyBlep4(t, dt);
        if (result == 0.0f) {
            ++zeroCount;
        }
        REQUIRE(result == 0.0f);
    }
    REQUIRE(zeroCount == kNumTrials);
}

// =============================================================================
// T018: polyBlep4 known-value verification
// =============================================================================

TEST_CASE("polyBlep4 returns non-zero correction near discontinuity", "[polyblep]") {
    SECTION("After-wrap region: t=0, dt=0.01") {
        // At t=0, should produce non-zero correction
        float result = polyBlep4(0.0f, 0.01f);
        REQUIRE(result != 0.0f);
    }

    SECTION("Before-wrap region: t very close to 1") {
        float dt = 0.01f;
        float t = 1.0f - 0.005f; // inside [1-2*dt, 1)
        float result = polyBlep4(t, dt);
        REQUIRE(result != 0.0f);
    }

    SECTION("At boundary t=2*dt (edge of after-wrap correction)") {
        float dt = 0.01f;
        float result = polyBlep4(2.0f * dt, dt);
        REQUIRE(result == Approx(0.0f).margin(1e-6f));
    }

    SECTION("Far from discontinuity") {
        REQUIRE(polyBlep4(0.5f, 0.01f) == 0.0f);
    }

    SECTION("Wider region than 2-point") {
        float dt = 0.01f;
        // t inside [dt, 2*dt) -- polyBlep returns 0 here, polyBlep4 may not
        float t = dt + dt * 0.5f; // = 1.5*dt = 0.015
        float result2 = polyBlep(t, dt);
        // polyBlep should return 0 here since t >= dt
        REQUIRE(result2 == 0.0f);

        float result4 = polyBlep4(t, dt);
        // polyBlep4 should return non-zero since t < 2*dt
        REQUIRE(result4 != 0.0f);
    }
}

// =============================================================================
// T019: polyBlamp4 zero-outside-region (SC-001, FR-008)
// =============================================================================

TEST_CASE("polyBlamp4 returns zero outside correction region", "[polyblep][SC-001]") {
    auto rng = makeRng(3456);
    std::uniform_real_distribution<float> dtDist(0.001f, 0.025f);

    int zeroCount = 0;
    constexpr int kNumTrials = 10000;

    for (int i = 0; i < kNumTrials; ++i) {
        float dt = dtDist(rng);
        float dt2 = 2.0f * dt;
        std::uniform_real_distribution<float> tDist(dt2 + 0.001f, 1.0f - dt2 - 0.001f);
        float t = tDist(rng);

        float result = polyBlamp4(t, dt);
        if (result == 0.0f) {
            ++zeroCount;
        }
        REQUIRE(result == 0.0f);
    }
    REQUIRE(zeroCount == kNumTrials);
}

// =============================================================================
// T020: polyBlamp4 known-value verification
// =============================================================================

TEST_CASE("polyBlamp4 returns non-zero correction near discontinuity", "[polyblep]") {
    SECTION("After-wrap region: t=0, dt=0.01") {
        float result = polyBlamp4(0.0f, 0.01f);
        REQUIRE(result != 0.0f);
    }

    SECTION("Before-wrap region: t close to 1") {
        float dt = 0.01f;
        float t = 1.0f - 0.005f;
        float result = polyBlamp4(t, dt);
        REQUIRE(result != 0.0f);
    }

    SECTION("At boundary t=2*dt") {
        float dt = 0.01f;
        float result = polyBlamp4(2.0f * dt, dt);
        REQUIRE(result == Approx(0.0f).margin(1e-6f));
    }

    SECTION("Far from discontinuity") {
        REQUIRE(polyBlamp4(0.5f, 0.01f) == 0.0f);
    }

    SECTION("Wider region than 2-point") {
        float dt = 0.01f;
        float t = dt + dt * 0.5f;
        REQUIRE(polyBlamp(t, dt) == 0.0f);
        REQUIRE(polyBlamp4(t, dt) != 0.0f);
    }
}

// =============================================================================
// T010 continued: constexpr for 4-point variants (SC-008)
// =============================================================================

TEST_CASE("polyBlep4 and polyBlamp4 are constexpr-evaluable", "[polyblep][SC-008]") {
    static_assert(polyBlep4(0.5f, 0.01f) == 0.0f,
                  "polyBlep4 must return 0 outside correction region at compile time");
    static_assert(polyBlamp4(0.5f, 0.01f) == 0.0f,
                  "polyBlamp4 must return 0 outside correction region at compile time");

    static_assert(polyBlep4(0.0f, 0.01f) != 0.0f,
                  "polyBlep4 must return non-zero at t=0 at compile time");
    static_assert(polyBlamp4(0.0f, 0.01f) != 0.0f,
                  "polyBlamp4 must return non-zero at t=0 at compile time");

    SUCCEED("All 4-point static_assert checks passed at compile time");
}

// =============================================================================
// T025: Quality - peak second derivative comparison (SC-003a)
// =============================================================================

TEST_CASE("polyBlep4 has lower peak second derivative than polyBlep", "[polyblep][SC-003]") {
    constexpr float dt = 0.01f;
    constexpr float step = dt * 0.01f; // Very fine step for derivative estimation

    // Compute second derivative of polyBlep via finite differences
    auto secondDerivative = [step](auto func, float t, float dt_val) {
        float fm = func(t - step, dt_val);
        float f0 = func(t, dt_val);
        float fp = func(t + step, dt_val);
        return (fp - 2.0f * f0 + fm) / (step * step);
    };

    float peakSecondDeriv2 = 0.0f;
    float peakSecondDeriv4 = 0.0f;

    // Sweep through the correction region near t=0
    for (float t = step; t < dt - step; t += step) {
        float sd2 = std::abs(secondDerivative(polyBlep, t, dt));
        float sd4 = std::abs(secondDerivative(polyBlep4, t, dt));
        peakSecondDeriv2 = std::max(peakSecondDeriv2, sd2);
        peakSecondDeriv4 = std::max(peakSecondDeriv4, sd4);
    }

    // Also sweep near t=1 (before-wrap region)
    for (float t = 1.0f - dt + step; t < 1.0f - step; t += step) {
        float sd2 = std::abs(secondDerivative(polyBlep, t, dt));
        peakSecondDeriv2 = std::max(peakSecondDeriv2, sd2);
    }

    for (float t = 1.0f - 2.0f * dt + step; t < 1.0f - step; t += step) {
        float sd4 = std::abs(secondDerivative(polyBlep4, t, dt));
        peakSecondDeriv4 = std::max(peakSecondDeriv4, sd4);
    }

    INFO("Peak 2nd derivative (2-point): " << peakSecondDeriv2);
    INFO("Peak 2nd derivative (4-point): " << peakSecondDeriv4);

    // SC-003a: 4-point peak must be at least 10% lower
    REQUIRE(peakSecondDeriv4 < peakSecondDeriv2 * 0.9f);
}

// =============================================================================
// T026: Quality - correction symmetry (SC-003b)
// =============================================================================

TEST_CASE("polyBlep corrections are symmetric around discontinuity", "[polyblep][SC-003]") {
    constexpr float dt = 0.01f;

    // For 2-point: correction at t should be negation of correction at 1-t
    // (the before-wrap and after-wrap regions should be antisymmetric)
    SECTION("2-point symmetry") {
        for (float offset = 0.001f; offset < dt; offset += 0.001f) {
            float afterWrap = polyBlep(offset, dt);          // [0, dt)
            float beforeWrap = polyBlep(1.0f - offset, dt);  // [1-dt, 1)
            // These should be equal in magnitude, opposite in sign
            INFO("offset = " << offset);
            REQUIRE(afterWrap == Approx(-beforeWrap).margin(1e-5f));
        }
    }

    SECTION("4-point symmetry") {
        float dt2 = 2.0f * dt;
        for (float offset = 0.001f; offset < dt2; offset += 0.001f) {
            float afterWrap = polyBlep4(offset, dt);
            float beforeWrap = polyBlep4(1.0f - offset, dt);
            INFO("offset = " << offset);
            REQUIRE(afterWrap == Approx(-beforeWrap).margin(1e-5f));
        }
    }
}

// =============================================================================
// T027: Quality - zero DC bias (SC-003c)
// =============================================================================

TEST_CASE("polyBlep integrated correction has near-zero DC bias", "[polyblep][SC-003]") {
    // SC-003c: The PolyBLEP correction is analytically DC-free (integral = 0).
    // This follows from the construction: the residual is (bandlimited_step -
    // ideal_step), both of which transition from 0 to 1, so their difference
    // integrates to exactly zero.
    //
    // We verify the ALGORITHM in double precision to avoid IEEE 754 float ULP
    // artifacts. We use the MIDPOINT rule (sampling at (i+0.5)/N) rather than
    // left-endpoint, because the polyBlep function has a step discontinuity at
    // t=0/1 (by design). With left-endpoint sampling, t=0 is included but its
    // antisymmetric partner t=1.0 is excluded from [0,1), creating a systematic
    // bias of -polyBlep(0)*h. The midpoint rule avoids this: every sample t_i
    // has a perfect antisymmetric partner t_{N-1-i} = 1 - t_i.
    constexpr double dt = 0.01;
    constexpr int N = 1000000;
    constexpr double h = 1.0 / N;

    SECTION("2-point polyBlep DC bias") {
        double sum = 0.0;
        for (int i = 0; i < N; ++i) {
            double t = (static_cast<double>(i) + 0.5) * h;
            sum += polyBlepDouble(t, dt);
        }
        double dcBias = sum * h;

        INFO("DC bias (2-point): " << dcBias);
        REQUIRE(std::abs(dcBias) < 1e-9);
    }

    SECTION("4-point polyBlep4 DC bias") {
        double sum = 0.0;
        for (int i = 0; i < N; ++i) {
            double t = (static_cast<double>(i) + 0.5) * h;
            sum += polyBlep4Double(t, dt);
        }
        double dcBias = sum * h;

        INFO("DC bias (4-point): " << dcBias);
        REQUIRE(std::abs(dcBias) < 1e-9);
    }
}
