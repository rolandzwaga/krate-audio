// ==============================================================================
// Profundum Phase 5 (specs/profundum-phase5-shape-character)
// ==============================================================================
// ClosedFormAdaa2<Law>, DiodeAdaaLaw, diodeH1Coefficient (T003; plan S1 C-1,
// C-11, S4.1): law antiderivative identities, DiodeAdaaLaw == Asymmetric::diode
// (C-1), ADAA2 slow ramp, fallback branches, reset/state, diodeH1Coefficient.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/closed_form_adaa2.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/xorshift32.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace Krate::DSP;

namespace {

// f = x^3, F1 = x^4/4, F2 = x^5/20
struct TestCubicLaw {
    static double f(double x) noexcept { return x * x * x; }
    static double F1(double x) noexcept { return x * x * x * x / 4.0; }
    static double F2(double x) noexcept { return x * x * x * x * x / 20.0; }
};

// f = |x|, F1 = x|x|/2, F2 = x^2|x|/6
struct TestAbsLaw {
    static double f(double x) noexcept { return std::abs(x); }
    static double F1(double x) noexcept { return 0.5 * x * std::abs(x); }
    static double F2(double x) noexcept { return x * x * std::abs(x) / 6.0; }
};

template <class Law>
void checkLawIdentities() {
    constexpr double kH = 1.0e-5;
    constexpr int kPoints = 3201;
    for (int i = 0; i < kPoints; ++i) {
        const double x = -8.0 + 16.0 * static_cast<double>(i) / static_cast<double>(kPoints - 1);
        if (std::abs(x) < 1.0e-3)
            continue;
        const double d1 = (Law::F1(x + kH) - Law::F1(x - kH)) / (2.0 * kH);
        const double d2 = (Law::F2(x + kH) - Law::F2(x - kH)) / (2.0 * kH);
        INFO("x = " << x);
        CHECK(std::abs(d1 - Law::f(x)) <= 1.0e-9 * std::max(1.0, std::abs(Law::f(x))));
        CHECK(std::abs(d2 - Law::F1(x)) <= 1.0e-9 * std::max(1.0, std::abs(Law::F1(x))));
    }
    CHECK(Law::F1(0.0) == 0.0);
    CHECK(Law::F2(0.0) == 0.0);
    // Continuity at the kink
    CHECK(std::abs(Law::F1(1.0e-12)) <= 1.0e-12);
    CHECK(std::abs(Law::F1(-1.0e-12)) <= 1.0e-12);
    CHECK(std::abs(Law::F2(1.0e-12)) <= 1.0e-12);
    CHECK(std::abs(Law::F2(-1.0e-12)) <= 1.0e-12);
}

// Slow ramp x[n] = -4 + 1e-4 n until x > 4. Straddling samples (x[n-2], x[n-1],
// x[n] on both sides of 0) get the kink allowance 0.5 * 1e-4 * |f'(0+) - f'(0-)|.
// Non-straddling bar 5e-6 (epoch 2, plan C-25): double rounding of F2 ~ 51 / h^2
// with h = 1e-4 puts the floor near 2e-6; the algorithm's own bias is ~2e-8.
template <class Law>
void checkSlowRamp(double kinkJump) {
    ClosedFormAdaa2<Law> adaa;
    std::vector<double> xs;
    xs.reserve(80002);
    double worst = 0.0;
    double worstStraddle = 0.0;
    for (int n = 0;; ++n) {
        const double x = -4.0 + 1.0e-4 * static_cast<double>(n);
        if (x > 4.0)
            break;
        xs.push_back(x);
        const double y = adaa.process(x);
        if (n < 2)
            continue;
        const double a = xs[static_cast<std::size_t>(n - 2)];
        const double b = xs[static_cast<std::size_t>(n - 1)];
        const double c = xs[static_cast<std::size_t>(n)];
        const double lo = std::min({a, b, c});
        const double hi = std::max({a, b, c});
        const double err = std::abs(y - Law::f(b));
        const bool straddles = (kinkJump > 0.0) && lo <= 0.0 && hi >= 0.0;
        if (straddles)
            worstStraddle = std::max(worstStraddle, err);
        else
            worst = std::max(worst, err);
    }
    INFO("worst non-straddling error " << worst << ", worst straddling error " << worstStraddle);
    CHECK(worst <= 5.0e-6);
    CHECK(worstStraddle <= 0.5 * 1.0e-4 * kinkJump + 1.0e-6);
}

} // namespace

TEST_CASE("ClosedFormAdaa2_LawIdentities", "[core][profundum][closed-form-adaa2]") {
    SECTION("DiodeAdaaLaw") { checkLawIdentities<DiodeAdaaLaw>(); }
    SECTION("TestCubicLaw") { checkLawIdentities<TestCubicLaw>(); }
    SECTION("TestAbsLaw") { checkLawIdentities<TestAbsLaw>(); }
}

// plan S1 C-1: the double law mirrors the float Asymmetric::diode (sigmoid.h:358-366).
// Epoch 2 (plan C-25): the float reference 1 - expf(-1.5x) cancels near 0, so the
// comparison is absolute, 2 ulp of max(1, |f(x)|): |float(f) - diode| <= 2 * 2^-24 * max(1, |f|).
TEST_CASE("ClosedFormAdaa2_DiodeMatchesSigmoid", "[core][profundum][closed-form-adaa2]") {
    constexpr int kPoints = 10001;
    constexpr double kTwoUlpOfOne = 2.0 / 16777216.0; // 2 * 2^-24
    double worstRatio = 0.0;
    double worstX = 0.0;
    for (int i = 0; i < kPoints; ++i) {
        const double x = -8.0 + 16.0 * static_cast<double>(i) / static_cast<double>(kPoints - 1);
        const double lawExact = DiodeAdaaLaw::f(x);
        const float lawValue = static_cast<float>(lawExact);
        const float sigmoidValue = Asymmetric::diode(static_cast<float>(x));
        const double diff =
            std::abs(static_cast<double>(lawValue) - static_cast<double>(sigmoidValue));
        const double bar = kTwoUlpOfOne * std::max(1.0, std::abs(lawExact));
        const double ratio = diff / bar;
        if (ratio > worstRatio) {
            worstRatio = ratio;
            worstX = x;
        }
    }
    INFO("worst |float(f) - diode| / bar = " << worstRatio << " at x = " << worstX);
    CHECK(worstRatio <= 1.0);
}

// plan S1 C-11: double ADAA2, eps = 1e-5.
TEST_CASE("ClosedFormAdaa2_SlowRampEqualsLaw", "[core][profundum][closed-form-adaa2]") {
    SECTION("TestCubicLaw") { checkSlowRamp<TestCubicLaw>(0.0); }
    SECTION("DiodeAdaaLaw") { checkSlowRamp<DiodeAdaaLaw>(0.5); } // f'(0+) - f'(0-) = 1.5 - 1
    SECTION("TestAbsLaw") { checkSlowRamp<TestAbsLaw>(2.0); }      // 1 - (-1)
}

TEST_CASE("ClosedFormAdaa2_FallbackBranches", "[core][profundum][closed-form-adaa2]") {
    SECTION("constant input takes the |x - x2| <= eps and |delta| <= eps branches") {
        ClosedFormAdaa2<DiodeAdaaLaw> adaa;
        const double expected = DiodeAdaaLaw::f(0.3);
        for (int n = 0; n < 8; ++n) {
            const double y = adaa.process(0.3);
            if (n >= 2) {
                INFO("n = " << n << ", y = " << y);
                CHECK(std::abs(y - expected) <= 1.0e-12);
            }
        }
    }
    SECTION("alternating 0.3 / 0.3 + 5e-6 keeps both difference quotients in the fallback") {
        // The fallback returns f at the midpoint (epoch 2, plan C-25).
        ClosedFormAdaa2<DiodeAdaaLaw> adaa;
        const double expected = DiodeAdaaLaw::f(0.3 + 2.5e-6);
        double worst = 0.0;
        bool allFinite = true;
        for (int n = 0; n < 64; ++n) {
            const double x = (n % 2 == 0) ? 0.3 : 0.3 + 5.0e-6;
            const double y = adaa.process(x);
            if (n < 2)
                continue;
            allFinite = allFinite && detail::isFinite(y);
            worst = std::max(worst, std::abs(y - expected));
        }
        INFO("worst |y - f(0.3 + 2.5e-6)| = " << worst);
        CHECK(allFinite);
        CHECK(worst <= 1.0e-9);
    }
}

TEST_CASE("ClosedFormAdaa2_ResetAndState", "[core][profundum][closed-form-adaa2]") {
    ClosedFormAdaa2<DiodeAdaaLaw> adaa;
    XorShift32 rng;
    rng.seed(0x1234u);
    for (int n = 0; n < 1000; ++n)
        (void)adaa.process(4.0 * static_cast<double>(rng.nextFloatSigned()));
    CHECK(adaa.stateFinite());
    adaa.reset();
    CHECK(adaa.process(0.0) == 0.0);
    CHECK(adaa.stateFinite());
}

TEST_CASE("DiodeH1Coefficient_MatchesIntegral", "[core][profundum][closed-form-adaa2]") {
    // 65536-point midpoint reference of (1/pi) * integral_0^{2pi} f(g sin phi) sin phi dphi.
    // The 256-point rule's error on a C0-kinked periodic integrand is O(N^-2).
    const auto reference = [](double gain) {
        constexpr int kN = 65536;
        constexpr double kTwoPi = 6.283185307179586476925286766559;
        double sum = 0.0;
        for (int k = 0; k < kN; ++k) {
            const double phi = kTwoPi * (static_cast<double>(k) + 0.5) / static_cast<double>(kN);
            const double s = std::sin(phi);
            sum += DiodeAdaaLaw::f(gain * s) * s;
        }
        return (2.0 / static_cast<double>(kN)) * sum;
    };
    for (const double g : {0.25, 1.0, 4.0, 31.6}) {
        const double got = diodeH1Coefficient(g);
        const double ref = reference(g);
        INFO("gain " << g << ": got " << got << ", reference " << ref);
        CHECK(std::abs(got - ref) <= 1.0e-4 * std::abs(ref));
    }
    // Small-signal slope: mean of f'(0+) = 1.5 and f'(0-) = 1.
    const double slope = diodeH1Coefficient(1.0e-4) / 1.0e-4;
    INFO("small-signal slope " << slope);
    CHECK(std::abs(slope - 1.25) <= 1.0e-3);
}
