// ==============================================================================
// Layer 2: Processor Tests - Profundum Phase 4 (specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Reference: specs/profundum-phase4-drop-note-behaviour/plan.md (S3, S11.1-S11.3)
//            specs/profundum-phase4-drop-note-behaviour/tasks.md (T006 wires this TU,
//                                                                 T011, T012 fill it)
//
// Holds: SC-001-SC-005, SC-024(a), DropEnvelope_Lifecycle (FR-002, FR-008).
// T011: C¹ checker positive control, SC-001 (shape families, continuity), SC-002
//   (amount / time), SC-003 (rebound), R-5 (recurrence vs closed form), lifecycle.
// T012: SC-004 (C¹, bridged retrigger), E-1, E-4, SC-005 (block / rate invariance,
//   latching), SC-024(a) (phase integral).
//
// Shape values: a float parameter cannot hold 1/3 or 2/3 exactly, and float(2/3)
// rounds UP into the elastic segment (w = 3s - 2 ~ 6e-8, which grows a tail and
// moves the end of the planned curve to ~1.5 T). The tests therefore pass each
// anchor as the largest float at or below it (shapeParam), and every law that
// depends on s uses that float's exact value.
// ==============================================================================

#include "drop_c1_checker.h"

#include <krate/dsp/processors/drop_envelope.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using Krate::DSP::DropCurve;
using Krate::DSP::DropEnvelope;
using Krate::DSP::DropTest::DropC1Segment;
using Krate::DSP::DropTest::dropCheckC1;

namespace {

constexpr std::array<double, 3> kRates{44100.0, 48000.0, 96000.0};
constexpr double kCent = 0.01;  // 1 cent in semitones

/// The largest float at or below s (see the file comment).
float shapeParam(double s) {
    float f = static_cast<float>(s);
    if (static_cast<double>(f) > s)
        f = std::nextafter(f, std::numeric_limits<float>::lowest());
    return f;
}

/// Prepared envelope with the given parameters (not yet triggered).
DropEnvelope makeEnv(double fs, float amount, float timeMs, float shape, float rebound) {
    DropEnvelope env;
    env.prepare(fs);
    env.setAmount(amount);
    env.setTime(timeMs);
    env.setShape(shape);
    env.setRebound(rebound);
    return env;
}

std::vector<float> renderSamples(DropEnvelope& env, std::size_t count) {
    std::vector<float> out(count);
    for (auto& v : out)
        v = env.process();
    return out;
}

/// Least-squares fit y = a + b x: returns {R², b}.
std::array<double, 2> linearFit(const std::vector<double>& xs, const std::vector<double>& ys) {
    const auto n = static_cast<double>(xs.size());
    double mx = 0.0;
    double my = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        mx += xs[i];
        my += ys[i];
    }
    mx /= n;
    my /= n;
    double sxy = 0.0;
    double sxx = 0.0;
    double syy = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        sxy += (xs[i] - mx) * (ys[i] - my);
        sxx += (xs[i] - mx) * (xs[i] - mx);
        syy += (ys[i] - my) * (ys[i] - my);
    }
    return {sxy * sxy / (sxx * syy), sxy / sxx};
}

/// Curve area α: mean of p[n] = d[n]/A over n = 0 … ⌊N⌋.
double curveArea(const std::vector<float>& d, double amount, double N) {
    const auto last = static_cast<std::size_t>(std::floor(N));
    double sum = 0.0;
    for (std::size_t n = 0; n <= last; ++n)
        sum += static_cast<double>(d[n]) / amount;
    return sum / static_cast<double>(last + 1);
}

/// First sample with d ≤ 0 (the first landing), or d.size().
std::size_t firstLanding(const std::vector<float>& d) {
    for (std::size_t n = 0; n < d.size(); ++n) {
        if (d[n] <= 0.0f)
            return n;
    }
    return d.size();
}

/// Elastic undershoot ε = −min d after the first landing (0 if d never goes below 0 there).
double undershootAfterLanding(const std::vector<float>& d) {
    double mn = 0.0;
    for (std::size_t n = firstLanding(d); n < d.size(); ++n)
        mn = std::min(mn, static_cast<double>(d[n]));
    return -mn;
}

bool bitEqual(float a, float b) { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }

}  // namespace

// ------------------------------------------------------------------------------
// Checker positive control (SC-004 positive control; plan S11.2)
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_C1CheckerPositiveControl", "[processors][profundum][drop]") {
    // A cornered ramp 0 -> 1 over 100 samples, then held: must FAIL.
    std::vector<double> ramp(201);
    for (std::size_t n = 0; n < ramp.size(); ++n)
        ramp[n] = n <= 100 ? static_cast<double>(n) / 100.0 : 1.0;
    const std::array<DropC1Segment, 2> rampSegs{DropC1Segment{0, 100, 0.0}, DropC1Segment{100, 100, 0.0}};
    const auto rampRep = dropCheckC1(ramp, rampSegs, -0.1, 1.1);
    INFO("ramp: worstRatio " << rampRep.worstRatio << " worstBoundaryJump " << rampRep.worstBoundaryJump);
    REQUIRE_FALSE(rampRep.pass);

    // A sampled raised cosine (hold 0, cosine over 100 samples, hold 1) with its analytic
    // bound |a|·π²/(2L²): must PASS.
    constexpr std::size_t kL = 100;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<double> rc(10 + kL + 11);
    for (std::size_t n = 0; n < rc.size(); ++n) {
        if (n <= 10)
            rc[n] = 0.0;
        else if (n <= 10 + kL)
            rc[n] = 0.5 * (1.0 - std::cos(kPi * static_cast<double>(n - 10) / static_cast<double>(kL)));
        else
            rc[n] = 1.0;
    }
    const double bound = kPi * kPi / (2.0 * static_cast<double>(kL * kL));
    const std::array<DropC1Segment, 3> rcSegs{DropC1Segment{0, 10, 0.0}, DropC1Segment{10, kL, bound},
                                              DropC1Segment{10 + kL, 10, 0.0}};
    const auto rcRep = dropCheckC1(rc, rcSegs, -0.1, 1.1);
    INFO("raised cosine: worstRatio " << rcRep.worstRatio << " worstBoundaryJump " << rcRep.worstBoundaryJump);
    REQUIRE(rcRep.pass);

    // dropHermitePeakD2 matches the cubic's measured peak |Δ²| (sanity of the restated formula).
    {
        constexpr std::size_t kLen = 64;
        const double g0 = 3.0;
        const double gp0 = -0.2;
        const double g1 = -1.0;
        const double L = static_cast<double>(kLen);
        const double m0 = L * gp0;
        std::vector<double> h(kLen + 1);
        for (std::size_t n = 0; n <= kLen; ++n) {
            const double t = static_cast<double>(n) / L;
            h[n] = (2 * t * t * t - 3 * t * t + 1) * g0 + (t * t * t - 2 * t * t + t) * m0 + (-2 * t * t * t + 3 * t * t) * g1;
        }
        double peak = 0.0;
        for (std::size_t n = 1; n < kLen; ++n)
            peak = std::max(peak, std::abs(h[n + 1] - 2.0 * h[n] + h[n - 1]));
        const double analytic = Krate::DSP::DropTest::dropHermitePeakD2(g0, gp0, g1, kLen);
        INFO("measured " << peak << " analytic " << analytic);
        REQUIRE(peak <= 1.05 * analytic);
        REQUIRE(peak >= 0.9 * analytic);
    }
}

// ------------------------------------------------------------------------------
// SC-001 shape families
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_ShapeFamilies", "[processors][profundum][drop]") {
    constexpr float kA = 24.0f;
    for (const double fs : kRates) {
        for (const float T : {10.0f, 100.0f, 1000.0f}) {
            CAPTURE(fs, T);
            // ---- Exponential (s = 0)
            {
                auto env = makeEnv(fs, kA, T, 0.0f, 0.0f);
                env.trigger(true);
                const double N = env.plannedCurve().fallSamples;
                const auto d = renderSamples(env, static_cast<std::size_t>(std::ceil(2.0 * N)) + 64);
                const double alpha = curveArea(d, kA, N);
                std::vector<double> xs;
                std::vector<double> ys;
                for (std::size_t n = 0; n < d.size(); ++n) {
                    const double u = static_cast<double>(n) / N;
                    if (u >= 0.05 && u <= 0.8) {
                        xs.push_back(u);
                        ys.push_back(std::log(static_cast<double>(d[n]) / kA));
                    }
                }
                const auto fit = linearFit(xs, ys);
                INFO("exponential alpha " << alpha << " R2 " << fit[0] << " fitted rate " << -fit[1]);
                CHECK(alpha <= 0.2);
                CHECK(fit[0] >= 0.999);
            }
            // ---- Linear (s = 1/3)
            {
                auto env = makeEnv(fs, kA, T, shapeParam(1.0 / 3.0), 0.0f);
                env.trigger(true);
                const double N = env.plannedCurve().fallSamples;
                const auto d = renderSamples(env, static_cast<std::size_t>(std::ceil(2.0 * N)) + 64);
                const double alpha = curveArea(d, kA, N);
                double maxDev = 0.0;
                for (std::size_t n = 0; n < d.size(); ++n) {
                    const double u = static_cast<double>(n) / N;
                    if (u >= 0.05 && u <= 0.95)
                        maxDev = std::max(maxDev, std::abs(static_cast<double>(d[n]) / kA - (1.0 - u)));
                }
                INFO("linear alpha " << alpha << " max|p - (1 - u)| " << maxDev);
                CHECK(std::abs(alpha - 0.5) <= 0.005);
                CHECK(maxDev <= 0.01);
            }
            // ---- Logarithmic (s = 2/3)
            {
                auto env = makeEnv(fs, kA, T, shapeParam(2.0 / 3.0), 0.0f);
                env.trigger(true);
                const double N = env.plannedCurve().fallSamples;
                const auto d = renderSamples(env, static_cast<std::size_t>(std::ceil(2.0 * N)) + 64);
                const double alpha = curveArea(d, kA, N);
                std::vector<double> xs;
                std::vector<double> ys;
                for (std::size_t n = 0; n < d.size(); ++n) {
                    const double u = static_cast<double>(n) / N;
                    if (u >= 0.2 && u <= 0.95) {
                        xs.push_back(1.0 - u);
                        ys.push_back(std::log(1.0 - static_cast<double>(d[n]) / kA));
                    }
                }
                const auto fit = linearFit(xs, ys);
                INFO("logarithmic alpha " << alpha << " R2 " << fit[0]);
                CHECK(alpha >= 0.8);
                CHECK(fit[0] >= 0.999);
            }
            // ---- Elastic (s = 1)
            {
                auto env = makeEnv(fs, kA, T, 1.0f, 0.0f);
                env.trigger(true);
                const double N = env.plannedCurve().fallSamples;
                const auto settle = static_cast<std::size_t>(std::ceil(2.0 * N));
                const auto d = renderSamples(env, settle + 64);
                const double alpha = curveArea(d, kA, N);
                const std::size_t land = firstLanding(d);
                REQUIRE(land < d.size());
                int signChanges = 0;
                int runSign = 0;
                double runPeak = 0.0;
                std::vector<double> peaks;
                for (std::size_t n = land; n < d.size(); ++n) {
                    const double v = static_cast<double>(d[n]);
                    const int sg = static_cast<int>(v > 0.0) - static_cast<int>(v < 0.0);
                    if (sg == 0)
                        continue;
                    if (runSign != 0 && sg != runSign) {
                        ++signChanges;
                        peaks.push_back(runPeak);
                        runPeak = 0.0;
                    }
                    runSign = sg;
                    runPeak = std::max(runPeak, std::abs(v));
                }
                peaks.push_back(runPeak);
                const double eps = undershootAfterLanding(d);
                double tailMax = 0.0;
                for (std::size_t n = settle; n < d.size(); ++n)
                    tailMax = std::max(tailMax, std::abs(static_cast<double>(d[n])));
                INFO("elastic alpha (recorded) " << alpha << " sign changes " << signChanges << " epsilon " << eps
                                                 << " peaks " << peaks.size() << " tail max " << tailMax);
                CHECK(signChanges >= 2);
                CHECK(eps >= 3.528);
                CHECK(eps <= 3.672);
                for (std::size_t i = 1; i < peaks.size(); ++i) {
                    CAPTURE(i, peaks[i - 1], peaks[i]);
                    CHECK(peaks[i] <= 0.5 * peaks[i - 1]);
                }
                CHECK(tailMax <= kCent);
            }
        }
    }
}

TEST_CASE("DropEnvelope_ShapeContinuity", "[processors][profundum][drop]") {
    constexpr double kFs = 48000.0;
    constexpr float kT = 100.0f;
    constexpr float kA = 24.0f;
    const auto render = [&](float s) {
        auto env = makeEnv(kFs, kA, kT, s, 0.0f);
        env.trigger(true);
        const double N = env.plannedCurve().fallSamples;
        return renderSamples(env, static_cast<std::size_t>(std::floor(2.0 * N)) + 1);
    };
    const double N = static_cast<double>(kT) * kFs / 1000.0;

    // α over [0, 2/3]: 65 values, strictly increasing, adjacent steps ≤ 0.02.
    {
        double prev = -1.0;
        for (int k = 0; k <= 64; ++k) {
            const float s = shapeParam(static_cast<double>(k) * (2.0 / 3.0) / 64.0);
            const double alpha = curveArea(render(s), kA, N);
            CAPTURE(k, s, alpha, prev);
            if (k > 0) {
                CHECK(alpha > prev);
                CHECK(alpha - prev <= 0.02);
            }
            prev = alpha;
        }
    }
    // ε over [2/3, 1]: ε(s_0) = 0, strictly increasing, = w·0.15·A within ±2 %, steps ≤ 0.005·A.
    {
        double prev = 0.0;
        for (int k = 0; k <= 64; ++k) {
            const float s = shapeParam(2.0 / 3.0 + static_cast<double>(k) / 192.0);
            const double eps = undershootAfterLanding(render(s));
            const double w = std::max(0.0, 3.0 * static_cast<double>(s) - 2.0);
            const double law = w * DropCurve::kElasticOvershootFraction * kA;
            CAPTURE(k, s, eps, law, prev);
            if (k == 0) {
                CHECK(eps == 0.0);
            } else {
                CHECK(eps > prev);
                CHECK(std::abs(eps - law) <= 0.02 * law);
                CHECK(eps - prev <= 0.005 * kA);
            }
            prev = eps;
        }
    }
    // Whole range: 193 values s_k = k/192, max over n ≤ 2N of |p(s_k) − p(s_{k+1})| ≤ 0.025.
    {
        double worst = 0.0;
        int worstK = -1;
        std::vector<float> prev = render(0.0f);
        for (int k = 1; k <= 192; ++k) {
            const auto cur = render(shapeParam(static_cast<double>(k) / 192.0));
            for (std::size_t n = 0; n < cur.size(); ++n) {
                const double dp = std::abs(static_cast<double>(cur[n]) - static_cast<double>(prev[n])) / kA;
                if (dp > worst) {
                    worst = dp;
                    worstK = k;
                }
            }
            prev = cur;
        }
        INFO("192-step continuity: max |dp| " << worst << " at step " << worstK);
        CHECK(worst <= 0.025);
    }
}

// ------------------------------------------------------------------------------
// SC-002 amount and time
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_AmountTime", "[processors][profundum][drop]") {
    for (const double s : {0.0, 1.0 / 6.0, 1.0 / 3.0, 0.5, 2.0 / 3.0}) {
        for (const float A : {1.0f, 12.0f, 48.0f}) {
            for (const float T : {1.0f, 20.0f, 200.0f, 4000.0f}) {
                for (const double fs : kRates) {
                    const float sp = shapeParam(s);
                    CAPTURE(sp, A, T, fs);
                    auto env = makeEnv(fs, A, T, sp, 0.0f);
                    env.trigger(true);
                    const DropCurve curve = env.plannedCurve();
                    const double N = curve.fallSamples;
                    const auto tL = static_cast<std::size_t>(std::llround(N));
                    const std::size_t count = tL + 64;
                    std::vector<float> d(count);
                    std::vector<char> active(count);
                    for (std::size_t n = 0; n < count; ++n) {
                        d[n] = env.process();
                        active[n] = env.isActive() ? 1 : 0;
                    }
                    CHECK(std::abs(static_cast<double>(d[0]) - static_cast<double>(A)) <= 1e-5);

                    // (a) exactly 0.0f and idle from t_L + 1.
                    std::size_t firstBad = count;
                    for (std::size_t n = tL + 1; n < count; ++n) {
                        if (std::bit_cast<std::uint32_t>(d[n]) != 0u || active[n] != 0) {
                            firstBad = n;
                            break;
                        }
                    }
                    CAPTURE(tL, firstBad);
                    CHECK(firstBad == count);

                    // (b) first |d| ≤ 1 cent equals the analytic crossing n_1c ± 1.
                    std::size_t nMeas = count;
                    for (std::size_t n = 0; n < count; ++n) {
                        if (std::abs(static_cast<double>(d[n])) <= kCent) {
                            nMeas = n;
                            break;
                        }
                    }
                    std::size_t n1c = count;
                    for (std::size_t n = 0; n < count; ++n) {
                        if (std::abs(curve.valueAt(static_cast<double>(n))) <= kCent) {
                            n1c = n;
                            break;
                        }
                    }
                    double crossing = static_cast<double>(n1c);
                    if (n1c > 0 && n1c < count) {
                        double lo = static_cast<double>(n1c - 1);
                        double hi = static_cast<double>(n1c);
                        for (int it = 0; it < 60; ++it) {
                            const double mid = 0.5 * (lo + hi);
                            if (std::abs(curve.valueAt(mid)) <= kCent)
                                hi = mid;
                            else
                                lo = mid;
                        }
                        crossing = hi;
                    }
                    INFO("n_1c " << n1c << " continuous crossing " << crossing << " measured " << nMeas);
                    REQUIRE(n1c < count);
                    CHECK(std::abs(static_cast<double>(nMeas) - static_cast<double>(n1c)) <= 1.0);
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-003 rebound overshoot and settle (plan S1 C-7)
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_Rebound", "[processors][profundum][drop]") {
    for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 5.0 / 6.0, 1.0}) {
        for (const float A : {12.0f, 48.0f}) {
            for (const float T : {20.0f, 200.0f}) {
                for (const double fs : kRates) {
                    const float sp = shapeParam(s);
                    const bool elastic = static_cast<double>(sp) > 2.0 / 3.0;
                    const double w = std::max(0.0, 3.0 * static_cast<double>(sp) - 2.0);
                    double rhoPrev = -1.0;
                    for (const float R : {0.0f, 0.25f, 0.5f, 1.0f}) {
                        CAPTURE(sp, A, T, fs, R);
                        auto env = makeEnv(fs, A, T, sp, R);
                        env.trigger(true);
                        const double N = env.plannedCurve().fallSamples;
                        const auto tL = static_cast<double>(std::llround(N));
                        const auto d = renderSamples(env, static_cast<std::size_t>(std::ceil(2.0 * N)) + 64);
                        double mn = 0.0;
                        std::size_t settle = 0;
                        bool nonNegative = true;
                        for (std::size_t n = 0; n < d.size(); ++n) {
                            const double v = static_cast<double>(d[n]);
                            mn = std::min(mn, v);
                            if (std::abs(v) > kCent)
                                settle = n + 1;
                            if (v < 0.0)
                                nonNegative = false;
                        }
                        const double rho = std::max(0.0, -mn);
                        const double law = w * DropCurve::kElasticOvershootFraction * static_cast<double>(A) +
                                           static_cast<double>(R) * DropCurve::kMaxReboundFraction *
                                               static_cast<double>(A);
                        CAPTURE(rho, law, settle, tL, N);
                        if (R > 0.0f || elastic)
                            CHECK(std::abs(rho - law) <= 0.02 * law);
                        if (!elastic)
                            CHECK(static_cast<double>(settle) <= tL + DropCurve::kReboundSettleFactor * N + 1.0);
                        else
                            CHECK(static_cast<double>(settle) <= DropCurve::kElasticSettleFactor * N);
                        if (R == 0.0f && !elastic)
                            CHECK(nonNegative);
                        CHECK(rho > rhoPrev);
                        rhoPrev = rho;
                    }
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------
// R-5: the kernel's geometric recurrence matches the closed form
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_RecurrenceMatchesClosedForm", "[processors][profundum][drop]") {
    constexpr double kFs = 96000.0;
    double worstDiff = 0.0;
    for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) {
        for (const float R : {0.0f, 1.0f}) {
            for (const float T : {1.0f, 4000.0f}) {
                const float sp = shapeParam(s);
                CAPTURE(sp, R, T);
                auto env = makeEnv(kFs, 48.0f, T, sp, R);
                env.trigger(true);
                const DropCurve curve = env.plannedCurve();
                const auto d = renderSamples(env, static_cast<std::size_t>(std::ceil(curve.endSample)) + 16);
                std::size_t firstBad = d.size();
                for (std::size_t n = 0; n < d.size(); ++n) {
                    const double ref = curve.valueAt(static_cast<double>(n));
                    const auto f = static_cast<float>(ref);
                    worstDiff = std::max(worstDiff, std::abs(static_cast<double>(d[n]) - ref));
                    const bool ok = d[n] == f ||
                                    d[n] == std::nextafter(f, std::numeric_limits<float>::max()) ||
                                    d[n] == std::nextafter(f, std::numeric_limits<float>::lowest());
                    if (!ok && firstBad == d.size())
                        firstBad = n;
                }
                CAPTURE(firstBad);
                CHECK(firstBad == d.size());
            }
        }
    }
    // Recorded, not gated: the gate is the float-neighbour check above (plan R-5).
    WARN("worst |d[n] - valueAt(n)| (float output vs double closed form) " << worstDiff);
}

// ------------------------------------------------------------------------------
// FR-002, FR-008 lifecycle
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_Lifecycle", "[processors][profundum][drop]") {
    SECTION("before prepare every call is a no-op") {
        DropEnvelope touched;
        CHECK(touched.process() == 0.0f);
        CHECK_FALSE(touched.isActive());
        touched.setAmount(48.0f);
        touched.setTime(7.0f);
        touched.setShape(1.0f);
        touched.setRebound(1.0f);
        touched.trigger(true);
        std::vector<float> buf(64, 1.0f);
        touched.processBlock(buf.data(), buf.size());
        CHECK(std::all_of(buf.begin(), buf.end(), [](float v) { return v == 0.0f; }));
        CHECK_FALSE(touched.isActive());
        CHECK(touched.phaseIntegralSeconds() == 0.0);

        DropEnvelope fresh;
        touched.prepare(48000.0);
        fresh.prepare(48000.0);
        touched.setAmount(24.0f);  // Amount only: Time / Shape / Rebound stay at their defaults
        fresh.setAmount(24.0f);
        touched.trigger(true);
        fresh.trigger(true);
        CHECK(touched.plannedCurve().fallSamples == static_cast<double>(DropEnvelope::kDefaultTimeMs) * 48.0);
        const auto a = renderSamples(touched, 10000);
        const auto b = renderSamples(fresh, 10000);
        CHECK(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    }

    SECTION("prepare clamps the rate to [22050, 192000]") {
        DropEnvelope env;
        env.prepare(10000.0);
        env.setTime(100.0f);
        env.trigger(true);
        CHECK(env.plannedCurve().fallSamples == 2205.0);
        env.prepare(400000.0);
        env.setTime(100.0f);
        env.trigger(true);
        CHECK(env.plannedCurve().fallSamples == 19200.0);
    }

    SECTION("reset keeps the parameters") {
        auto a = makeEnv(48000.0, 36.0f, 50.0f, 0.8f, 0.6f);
        a.trigger(true);
        (void)renderSamples(a, 777);
        a.reset();
        CHECK_FALSE(a.isActive());
        CHECK(a.currentOffset() == 0.0f);
        a.trigger(true);
        auto b = makeEnv(48000.0, 36.0f, 50.0f, 0.8f, 0.6f);
        b.trigger(true);
        const auto ra = renderSamples(a, 6000);
        const auto rb = renderSamples(b, 6000);
        CHECK(std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(float)) == 0);
    }

    SECTION("processBlock with null or zero length changes nothing") {
        auto a = makeEnv(48000.0, 24.0f, 30.0f, 1.0f, 1.0f);
        auto b = makeEnv(48000.0, 24.0f, 30.0f, 1.0f, 1.0f);
        a.trigger(true);
        b.trigger(true);
        (void)renderSamples(a, 100);
        (void)renderSamples(b, 100);
        a.processBlock(nullptr, 64);
        std::array<float, 4> scratch{9.0f, 9.0f, 9.0f, 9.0f};
        a.processBlock(scratch.data(), 0);
        CHECK(scratch[0] == 9.0f);
        CHECK(a.currentOffset() == b.currentOffset());
        const auto ra = renderSamples(a, 2000);
        const auto rb = renderSamples(b, 2000);
        CHECK(std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(float)) == 0);
    }

    SECTION("currentOffset is the last returned sample; isActive false before any trigger") {
        auto env = makeEnv(48000.0, 24.0f, 20.0f, 0.5f, 0.5f);
        CHECK_FALSE(env.isActive());
        env.trigger(true);
        CHECK(env.isActive());
        bool allMatch = true;
        for (int i = 0; i < 300; ++i) {
            const float v = env.process();
            allMatch = allMatch && std::bit_cast<std::uint32_t>(v) == std::bit_cast<std::uint32_t>(env.currentOffset());
        }
        CHECK(allMatch);
        for (const std::size_t n : {std::size_t{1}, std::size_t{37}, std::size_t{512}}) {
            std::vector<float> buf(n);
            env.processBlock(buf.data(), n);
            CAPTURE(n);
            CHECK(std::bit_cast<std::uint32_t>(buf.back()) == std::bit_cast<std::uint32_t>(env.currentOffset()));
        }
    }
}

// ==============================================================================
// T012: bridged retrigger, C¹, invariance, latching, phase integral
// (FR-006, FR-007, FR-010, FR-056, FR-057; plan S1 C-2, C-8; SC-004, SC-005,
// SC-024(a); E-1, E-2, E-4). Under the 2026-10-12 O-1 ruling the bridge rise is
// L_min samples always, so the bridge has three branches (L_br = 1; L_br > 1 with
// a brake excursion ≤ 0.5 st; the L_min rise) and no per-sample upward-step bound.
// ==============================================================================
namespace {

constexpr std::array<double, 2> kC1Rates{44100.0, 96000.0};
constexpr double kBrakeExcursionBound = 2.7;  // plan S1 C-8: E-4 / SC-004 range [−ρ_max − 2.7, A]

std::vector<double> renderDouble(DropEnvelope& env, std::size_t count) {
    std::vector<double> out(count);
    for (auto& v : out)
        v = static_cast<double>(env.process());
    return out;
}

void appendRender(std::vector<double>& g, DropEnvelope& env, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        g.push_back(static_cast<double>(env.process()));
}

/// ρ law of the cell (FR-005, C-7): w·0.15·A + R·0.25·A, with w = max(0, 3s − 2) of the float s.
double rhoLaw(float s, float R, float A) {
    const double w = std::max(0.0, 3.0 * static_cast<double>(s) - 2.0);
    return w * DropCurve::kElasticOvershootFraction * static_cast<double>(A) +
           static_cast<double>(R) * DropCurve::kMaxReboundFraction * static_cast<double>(A);
}

/// One float ulp at |x|.
double floatUlp(double x) {
    const auto f = static_cast<float>(std::abs(x));
    return static_cast<double>(std::nextafter(f, std::numeric_limits<float>::max())) - static_cast<double>(f);
}

/// Float-output allowance on |Δ²d| (T012, surfaced): d is the float cast of a double that is within
/// 1e-9 st of the closed form (R-5), so each sample carries ≤ ½ ulp(|d|) + 1e-9 and a second
/// difference ≤ 4 × that. Without it the 1.05 × analytic bound (~2e-8 st/sample² at T = 4000 ms,
/// 96 kHz) sits below the float grid of d (ulp(48) = 3.8e-6 st).
double quantAllowance(const std::vector<double>& g, std::size_t lo, std::size_t hi) {
    const std::size_t a = lo > 0 ? lo - 1 : 0;
    const std::size_t b = std::min(hi + 1, g.size() - 1);
    double mx = 0.0;
    for (std::size_t n = a; n <= b; ++n)
        mx = std::max(mx, std::abs(g[n]));
    return 2.0 * floatUlp(mx) + 4e-9;
}

/// A latched curve that owns the samples [offset, stop] of a trace (its position 0 is `offset`).
struct CurveSpan {
    const DropCurve* curve;
    std::size_t offset;
    std::size_t stop;
};

/// SC-004 checker segments for a trace made of consecutive curves. Breaks fall on the first
/// integer sample of every piece and of the idle region after endSample. Each segment's bound is
/// the largest pieceD2Bound of every piece overlapping it (Δ²g at n is the hat-weighted mean of
/// g'' over [n − 1, n + 1], so a fractional piece break cannot exceed the larger adjacent bound),
/// 0 over the idle region, plus the float allowance.
std::vector<DropC1Segment> curveSegments(const std::vector<double>& g, const std::vector<CurveSpan>& spans) {
    std::vector<DropC1Segment> segs;
    for (const CurveSpan& sp : spans) {
        const DropCurve& c = *sp.curve;
        const auto o = static_cast<double>(sp.offset);
        const auto clampBreak = [&](double x) {
            const double b = std::ceil(o + x);
            return static_cast<std::size_t>(
                std::clamp(b, static_cast<double>(sp.offset), static_cast<double>(sp.stop)));
        };
        std::vector<std::size_t> br{sp.offset, sp.stop, clampBreak(c.endSample)};
        for (int j = 0; j < c.pieceCount(); ++j) {
            br.push_back(clampBreak(c.pieceStart(j)));
            br.push_back(clampBreak(c.pieceEnd(j)));
        }
        std::sort(br.begin(), br.end());
        br.erase(std::unique(br.begin(), br.end()), br.end());
        for (std::size_t k = 0; k + 1 < br.size(); ++k) {
            const std::size_t lo = br[k];
            const std::size_t hi = br[k + 1];
            double bound = 0.0;
            for (int j = 0; j < c.pieceCount(); ++j) {
                const double a = std::max(o + c.pieceStart(j), static_cast<double>(lo));
                const double b = std::min(o + c.pieceEnd(j), static_cast<double>(hi));
                if (a < b)
                    bound = std::max(bound, c.pieceD2Bound(j));
            }
            segs.push_back(DropC1Segment{lo, hi - lo, bound + quantAllowance(g, lo, hi)});
        }
    }
    return segs;
}

/// Bridge length law of S3.5 / FR-007 from the pre-trigger curve at the next sample position.
struct BridgeLaw {
    double lBr, lMin, excursion, slope;
};
BridgeLaw bridgeLaw(const DropCurve& prev, std::size_t nextPos, float T, double fs) {
    const double m = prev.slopeAt(static_cast<double>(nextPos));
    const double lMin =
        std::max(1.0, std::round(std::min(DropCurve::kDropBridgeMs, static_cast<double>(T)) * fs / 1000.0));
    double lBr = 0.0;
    if (m != 0.0)
        lBr = std::clamp(std::round(3.0 * DropCurve::kBrakeSemitones / std::abs(m)), 1.0, lMin);
    return {lBr, lMin, std::abs(m) * lBr / 3.0, m};
}

std::string cellName(const char* scenario, float s, float A, float T, double fs) {
    return std::string(scenario) + " s=" + std::to_string(s) + " A=" + std::to_string(A) + " T=" + std::to_string(T) +
           " fs=" + std::to_string(fs);
}

/// Composite Simpson of (2^{d/12} − 1)/fs over every piece (SC-024(a) reference; tasks.md
/// deviation 8): per piece max(2000, ⌈1000·len/shortestLen⌉) panels, rounded up to even.
double simpsonPhaseIntegralSeconds(const DropCurve& c, double fs) {
    const double ln2Over12 = std::log(2.0) / 12.0;
    double shortest = std::numeric_limits<double>::max();
    for (int i = 0; i < c.pieceCount(); ++i)
        shortest = std::min(shortest, c.pieceEnd(i) - c.pieceStart(i));
    double total = 0.0;
    for (int i = 0; i < c.pieceCount(); ++i) {
        const double a = c.pieceStart(i);
        const double b = c.pieceEnd(i);
        const double len = b - a;
        auto panels = std::max<std::size_t>(2000, static_cast<std::size_t>(std::ceil(1000.0 * len / shortest)));
        if ((panels & 1u) != 0u)
            ++panels;
        const double h = len / static_cast<double>(panels);
        const auto f = [&](double x) { return std::expm1(c.evalPiece(i, x) * ln2Over12); };
        double sum = f(a) + f(b);
        for (std::size_t k = 1; k < panels; ++k)
            sum += ((k & 1u) != 0u ? 4.0 : 2.0) * f(a + static_cast<double>(k) * h);
        total += sum * h / 3.0;
    }
    return total / fs;
}

bool bitZero(float v) { return std::bit_cast<std::uint32_t>(v) == 0u; }

}  // namespace

// ------------------------------------------------------------------------------
// SC-004 C¹ on full drops (snap trigger at sample 0 is the only step)
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_C1", "[processors][profundum][drop]") {
    constexpr std::array<double, 7> kShapes{0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0, 1.0 / 6.0, 0.5, 5.0 / 6.0};
    double worstRatio = 0.0;
    for (const double s : kShapes) {
        for (const float R : {0.0f, 1.0f}) {
            for (const float A : {1.0f, 12.0f, 48.0f}) {
                for (const float T : {1.0f, 20.0f, 200.0f, 4000.0f}) {
                    for (const double fs : kC1Rates) {
                        const float sp = shapeParam(s);
                        CAPTURE(sp, R, A, T, fs);
                        auto env = makeEnv(fs, A, T, sp, R);
                        env.trigger(true);
                        const DropCurve curve = env.plannedCurve();
                        const auto count = static_cast<std::size_t>(std::ceil(curve.endSample)) + 64;
                        const auto g = renderDouble(env, count);
                        const auto segs = curveSegments(g, {CurveSpan{&curve, 0, count - 1}});
                        const double lo = -rhoLaw(sp, R, A) - kBrakeExcursionBound;  // plan S1 C-8
                        const auto rep = dropCheckC1(g, segs, lo, static_cast<double>(A));
                        CAPTURE(rep.worstRatio, rep.worstBoundaryJump, rep.inRange);
                        CHECK(rep.pass);
                        worstRatio = std::max(worstRatio, rep.worstRatio);
                    }
                }
            }
        }
    }
    WARN("SC-004 full drops: worst |d2d| / (pieceD2Bound + float allowance) " << worstRatio);
}

// ------------------------------------------------------------------------------
// SC-004 / FR-007 / plan S1 C-8: bridged retrigger continuation
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_RetriggerContinuation", "[processors][profundum][drop]") {
    enum class Sc : std::uint8_t { MidFall, MidRebound, MidElastic, Idle };
    struct Cell {
        Sc sc;
        double s;
        float R;
        const char* name;
    };
    const std::array<Cell, 7> cells{Cell{Sc::MidFall, 0.0, 0.0f, "mid-fall exp"},
                                    Cell{Sc::MidFall, 1.0 / 3.0, 0.0f, "mid-fall lin"},
                                    Cell{Sc::MidFall, 2.0 / 3.0, 0.0f, "mid-fall log"},
                                    Cell{Sc::MidFall, 1.0, 0.0f, "mid-fall elastic"},
                                    Cell{Sc::MidRebound, 0.0, 1.0f, "mid-rebound"},
                                    Cell{Sc::MidElastic, 1.0, 0.0f, "mid-elastic"},
                                    Cell{Sc::Idle, 0.0, 0.0f, "idle"}};

    std::string hitSteep;     // L_br = 1
    std::string hitBrake;     // L_br > 1 with excursion ≤ 0.5 st
    std::string hitLMinRise;  // the L_min rise at A = 1
    double worstExcursionLbrGt1 = 0.0;
    double worstRiseStep = 0.0;
    double worstRatio = 0.0;

    for (const Cell& cell : cells) {
        for (const float T : {1.0f, 20.0f, 200.0f, 4000.0f}) {
            for (const float A : {1.0f, 12.0f, 48.0f}) {
                for (const double fs : kC1Rates) {
                    const float sp = shapeParam(cell.s);
                    const std::string name = cellName(cell.name, sp, A, T, fs);
                    INFO(name);
                    auto env = makeEnv(fs, A, T, sp, cell.R);
                    env.trigger(true);
                    const DropCurve prev = env.plannedCurve();
                    const double N = prev.fallSamples;
                    std::size_t nTrig = 0;
                    switch (cell.sc) {
                        case Sc::MidFall:
                            nTrig = static_cast<std::size_t>(std::llround(0.3 * N));
                            break;
                        case Sc::MidRebound: {
                            // Halfway between t_L (knot 0) and the tail's first knot after it.
                            REQUIRE(prev.hasTail);
                            double firstKnot = prev.knotT[0];
                            for (const double t : prev.knotT) {
                                if (t > prev.knotT[0]) {
                                    firstKnot = t;
                                    break;
                                }
                            }
                            REQUIRE(firstKnot > prev.knotT[0]);
                            nTrig = static_cast<std::size_t>(
                                std::llround(prev.fallStart + 0.5 * (prev.knotT[0] + firstKnot)));
                            break;
                        }
                        case Sc::MidElastic:
                            nTrig = static_cast<std::size_t>(std::llround(1.3 * N));
                            break;
                        case Sc::Idle:
                            nTrig = static_cast<std::size_t>(std::ceil(prev.endSample)) + 16;
                            break;
                    }
                    REQUIRE(nTrig >= 2);
                    std::vector<double> g = renderDouble(env, nTrig);
                    if (cell.sc == Sc::Idle)
                        REQUIRE_FALSE(env.isActive());
                    else
                        REQUIRE(env.isActive());

                    // Expected bridge from the pre-trigger curve at the next sample position (n + 1).
                    const BridgeLaw law = bridgeLaw(prev, nTrig, T, fs);
                    env.trigger(false);
                    const DropCurve cur = env.plannedCurve();
                    CAPTURE(law.slope, law.lBr, law.lMin, law.excursion, cur.fallStart);
                    CHECK(cur.brakeLen == law.lBr);
                    CHECK(cur.riseLen == law.lMin);
                    CHECK(cur.fallStart == law.lBr + law.lMin);  // FR-007: bridge length L_br + L_r exactly
                    if (cell.sc == Sc::Idle)
                        CHECK(law.lBr == 0.0);

                    // pieceD2Bound of the brake and the rise is the dropHermitePeakD2 form.
                    int idx = 0;
                    if (law.lBr > 0.0) {
                        REQUIRE(cur.pieceKind(0) == DropCurve::PieceKind::Brake);
                        const auto L = static_cast<std::size_t>(law.lBr);
                        const double ref = Krate::DSP::DropTest::dropHermitePeakD2(
                            cur.brakeY0, cur.brakeM0, cur.brakeY0 + cur.brakeM0 * law.lBr / 3.0, L);
                        CHECK(std::abs(cur.pieceD2Bound(0) - ref) <= 1e-12 * std::max(1.0, ref));
                        idx = 1;
                    }
                    REQUIRE(cur.pieceKind(idx) == DropCurve::PieceKind::Rise);
                    {
                        const auto L = static_cast<std::size_t>(law.lMin);
                        const double ref =
                            Krate::DSP::DropTest::dropHermitePeakD2(cur.riseY0, 0.0, static_cast<double>(A), L);
                        CHECK(std::abs(cur.pieceD2Bound(idx) - ref) <= 1e-12 * std::max(1.0, ref));
                    }
                    REQUIRE(cur.pieceKind(idx + 1) == DropCurve::PieceKind::OnsetFillet);

                    // Branch bookkeeping (three branches under O-1).
                    if (law.lBr == 1.0 && hitSteep.empty())
                        hitSteep = name;
                    if (law.lBr > 1.0) {
                        worstExcursionLbrGt1 = std::max(worstExcursionLbrGt1, law.excursion);
                        if (law.excursion <= DropCurve::kBrakeSemitones && hitBrake.empty())
                            hitBrake = name;
                    }
                    if (A == 1.0f && cur.riseLen == law.lMin && hitLMinRise.empty())
                        hitLMinRise = name;

                    appendRender(g, env, static_cast<std::size_t>(std::ceil(cur.endSample)) + 64);
                    // Recorded (O-1: no bound): the largest per-sample upward step during the rise.
                    const std::size_t riseStart = nTrig + static_cast<std::size_t>(law.lBr);
                    for (std::size_t n = riseStart; n < riseStart + static_cast<std::size_t>(law.lMin); ++n)
                        worstRiseStep = std::max(worstRiseStep, g[n + 1] - g[n]);

                    const auto segs =
                        curveSegments(g, {CurveSpan{&prev, 0, nTrig}, CurveSpan{&cur, nTrig, g.size() - 1}});
                    const double lo = -rhoLaw(sp, cell.R, A) - kBrakeExcursionBound;  // plan S1 C-8
                    const auto rep = dropCheckC1(g, segs, lo, static_cast<double>(A));
                    CAPTURE(rep.worstRatio, rep.worstBoundaryJump, rep.inRange);
                    CHECK(rep.pass);
                    worstRatio = std::max(worstRatio, rep.worstRatio);
                }
            }
        }
    }

    INFO("L_br = 1 first at: " << hitSteep);
    INFO("L_br > 1, excursion <= 0.5 st first at: " << hitBrake);
    INFO("L_min rise (A = 1) first at: " << hitLMinRise);
    CHECK_FALSE(hitSteep.empty());
    CHECK_FALSE(hitBrake.empty());
    CHECK_FALSE(hitLMinRise.empty());
    WARN("SC-004 bridges: L_br = 1 first at [" << hitSteep << "]; L_br > 1 with excursion <= 0.5 st first at ["
                                              << hitBrake << "]; L_min rise first at [" << hitLMinRise
                                              << "]; worst brake excursion with L_br > 1 " << worstExcursionLbrGt1
                                              << " st; worst per-sample rise step " << worstRiseStep
                                              << " st (recorded, O-1); worst ratio " << worstRatio);
}

// ------------------------------------------------------------------------------
// E-4: trigger(false) before every sample
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_EveryCellRetrigger", "[processors][profundum][drop]") {
    constexpr float kA = 48.0f;
    constexpr std::size_t kCount = 20000;
    auto env = makeEnv(44100.0, kA, 1.0f, 1.0f, 1.0f);
    const double rhoMax = rhoLaw(1.0f, 1.0f, kA);
    REQUIRE(std::abs(rhoMax - 19.2) <= 1e-12);
    const double lo = -rhoMax - kBrakeExcursionBound;  // plan S1 C-8
    std::size_t firstBad = kCount;
    double mn = 0.0;
    double mx = 0.0;
    for (std::size_t n = 0; n < kCount; ++n) {
        env.trigger(false);
        const float v = env.process();
        const auto d = static_cast<double>(v);
        const bool ok = Krate::DSP::detail::isFinite(v) && d >= lo && d <= static_cast<double>(kA);
        if (!ok && firstBad == kCount)
            firstBad = n;
        mn = std::min(mn, d);
        mx = std::max(mx, d);
    }
    INFO("E-4: d in [" << mn << ", " << mx << "], bound [" << lo << ", " << kA << "]");
    CHECK(firstBad == kCount);
}

// ------------------------------------------------------------------------------
// E-1: Amount 0 is inert under snap and bridged triggers
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_AmountZero", "[processors][profundum][drop]") {
    for (const double s : {0.0, 1.0 / 6.0, 1.0 / 3.0, 0.5, 2.0 / 3.0, 5.0 / 6.0, 1.0}) {
        for (const float R : {0.0f, 1.0f}) {
            for (const float T : {1.0f, 200.0f}) {
                const float sp = shapeParam(s);
                CAPTURE(sp, R, T);
                auto env = makeEnv(48000.0, 24.0f, T, sp, R);
                env.setAmount(0.0f);
                std::size_t firstBad = 0;
                bool allZero = true;
                std::size_t n = 0;
                const auto run = [&](std::size_t count) {
                    for (std::size_t i = 0; i < count; ++i, ++n) {
                        if (!bitZero(env.process()) && allZero) {
                            allZero = false;
                            firstBad = n;
                        }
                    }
                };
                env.trigger(true);
                run(5);
                env.trigger(false);  // bridged mid-"drop"
                run(static_cast<std::size_t>(std::ceil(env.plannedCurve().endSample)) + 32);
                CHECK_FALSE(env.isActive());
                env.trigger(false);  // bridged from idle
                run(static_cast<std::size_t>(std::ceil(env.plannedCurve().endSample)) + 32);
                CAPTURE(firstBad);
                CHECK(allZero);
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-005 block invariance (with a bridged retrigger at sample 3001)
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_BlockInvariance", "[processors][profundum][drop]") {
    constexpr double kFs = 48000.0;
    constexpr float kT = 200.0f;
    constexpr std::size_t kRetrig = 3001;
    const auto total = kRetrig + static_cast<std::size_t>(2.0 * static_cast<double>(kT) * kFs / 1000.0) + 1000;
    for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) {
        for (const float R : {0.0f, 1.0f}) {
            const float sp = shapeParam(s);
            CAPTURE(sp, R);
            // block == 0: per-sample process()
            const auto render = [&](std::size_t block) {
                auto env = makeEnv(kFs, 24.0f, kT, sp, R);
                env.trigger(true);
                std::vector<float> out(total);
                std::size_t pos = 0;
                const auto run = [&](std::size_t until) {
                    while (pos < until) {
                        if (block == 0) {
                            out[pos++] = env.process();
                        } else {
                            const std::size_t m = std::min(block, until - pos);
                            env.processBlock(out.data() + pos, m);
                            pos += m;
                        }
                    }
                };
                run(kRetrig);
                env.trigger(false);
                run(total);
                return out;
            };
            const auto ref = render(0);
            for (const std::size_t block : {std::size_t{1}, std::size_t{37}, std::size_t{512}}) {
                CAPTURE(block);
                const auto got = render(block);
                CHECK(std::equal(got.begin(), got.end(), ref.begin(), bitEqual));
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-005 / FR-010 rate invariance
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_RateInvariance", "[processors][profundum][drop]") {
    constexpr float kA = 24.0f;
    constexpr float kT = 100.0f;
    double worstDp = 0.0;
    for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) {
        for (const float R : {0.0f, 1.0f}) {
            const float sp = shapeParam(s);
            const bool elastic = static_cast<double>(sp) > 2.0 / 3.0;
            CAPTURE(sp, R);
            const auto render = [&](double fs) {
                auto env = makeEnv(fs, kA, kT, sp, R);
                env.trigger(true);
                return renderDouble(env, static_cast<std::size_t>(std::ceil(env.plannedCurve().endSample)) + 64);
            };

            // p(u) at the 44.1 kHz positions vs the 96 kHz render, linearly interpolated.
            {
                const double n44 = static_cast<double>(kT) * 44100.0 / 1000.0;
                const double n96 = static_cast<double>(kT) * 96000.0 / 1000.0;
                const auto lo = render(44100.0);
                const auto hi = render(96000.0);
                double worst = 0.0;
                for (std::size_t n = 0; n < lo.size(); ++n) {
                    const double x = static_cast<double>(n) / n44 * n96;
                    const auto i0 = static_cast<std::size_t>(std::floor(x));
                    if (i0 + 1 >= hi.size())
                        break;
                    const double fr = x - static_cast<double>(i0);
                    const double v = hi[i0] * (1.0 - fr) + hi[i0 + 1] * fr;
                    worst = std::max(worst, std::abs(lo[n] - v) / static_cast<double>(kA));
                }
                CAPTURE(worst);
                CHECK(worst <= 1e-4);
                worstDp = std::max(worstDp, worst);
            }

            // Times against their millisecond values, at each rate.
            for (const double fs : kRates) {
                CAPTURE(fs);
                const auto d = render(fs);
                const double perMs = fs / 1000.0;
                double settleFactor = 1.0;
                if (elastic)
                    settleFactor = DropCurve::kElasticSettleFactor;
                else if (R > 0.0f)
                    settleFactor = 1.0 + DropCurve::kReboundSettleFactor;
                const double tEndMs = settleFactor * static_cast<double>(kT);
                // Settle (R = 0, s ≤ 2/3: the landing itself): first sample of the exact-zero idle run.
                std::size_t settle = 0;
                for (std::size_t n = 0; n < d.size(); ++n) {
                    if (d[n] != 0.0)
                        settle = n + 1;
                }
                CAPTURE(settle, tEndMs * perMs);
                CHECK(std::abs(static_cast<double>(settle) - tEndMs * perMs) <= 1.0);
                if (elastic) {
                    // Landing: first d ≤ −ρ_R (steep there: slope −1.5·(A + ρ_R)/T).
                    const double rhoR = static_cast<double>(R) * DropCurve::kMaxReboundFraction * kA;
                    std::size_t land = d.size();
                    for (std::size_t n = 0; n < d.size(); ++n) {
                        if (d[n] <= -rhoR) {
                            land = n;
                            break;
                        }
                    }
                    // Elastic-period knots: the zero crossings at ELAY[2] and ELAY[4].
                    std::size_t up = d.size();
                    for (std::size_t n = land + 1; n < d.size(); ++n) {
                        if (d[n - 1] < 0.0 && d[n] >= 0.0) {
                            up = n;
                            break;
                        }
                    }
                    std::size_t down = d.size();
                    for (std::size_t n = up + 1; n < d.size(); ++n) {
                        if (d[n - 1] > 0.0 && d[n] <= 0.0) {
                            down = n;
                            break;
                        }
                    }
                    const double tUp = DropCurve::kElasticKnotLayout[2] * static_cast<double>(kT) * perMs;
                    const double tDown = DropCurve::kElasticKnotLayout[4] * static_cast<double>(kT) * perMs;
                    CAPTURE(land, up, down, tUp, tDown);
                    CHECK(std::abs(static_cast<double>(land) - static_cast<double>(kT) * perMs) <= 1.0);
                    CHECK(std::abs(static_cast<double>(up) - tUp) <= 1.0);
                    CHECK(std::abs(static_cast<double>(down) - tDown) <= 1.0);
                }
            }
        }
    }
    WARN("SC-005 rate invariance: worst |dp| 44.1 vs 96 kHz " << worstDp);
}

// ------------------------------------------------------------------------------
// SC-005 latching (FR-003, D-4)
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_Latching", "[processors][profundum][drop]") {
    using Krate::DSP::DropTest::dropCurveBitEqual;
    constexpr double kFs = 48000.0;
    for (const double s : {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) {
        for (const float R : {0.0f, 1.0f}) {
            const float sp = shapeParam(s);
            CAPTURE(sp, R);
            auto a = makeEnv(kFs, 24.0f, 200.0f, sp, R);
            auto b = makeEnv(kFs, 24.0f, 200.0f, sp, R);
            a.trigger(true);
            b.trigger(true);
            const auto rest = static_cast<std::size_t>(std::ceil(a.plannedCurve().endSample)) + 64;
            (void)renderSamples(a, 500);
            (void)renderSamples(b, 500);
            const DropCurve before = a.plannedCurve();
            a.setAmount(7.0f);
            a.setTime(33.0f);
            a.setShape(0.9f);
            a.setRebound(0.3f);
            CHECK(dropCurveBitEqual(a.plannedCurve(), before));
            const auto ra = renderSamples(a, rest);
            const auto rb = renderSamples(b, rest);
            CHECK(std::memcmp(ra.data(), rb.data(), rest * sizeof(float)) == 0);
            CHECK(dropCurveBitEqual(a.plannedCurve(), before));

            // The next trigger uses the new values.
            a.trigger(true);
            auto c = makeEnv(kFs, 7.0f, 33.0f, 0.9f, 0.3f);
            c.trigger(true);
            CHECK(dropCurveBitEqual(a.plannedCurve(), c.plannedCurve()));
            const auto na = renderSamples(a, 5000);
            const auto nc = renderSamples(c, 5000);
            CHECK(std::memcmp(na.data(), nc.data(), na.size() * sizeof(float)) == 0);
        }
    }
}

// ------------------------------------------------------------------------------
// SC-024(a): phaseIntegralSeconds (GL8 panels, plan S1 C-2) vs composite Simpson
// ------------------------------------------------------------------------------
TEST_CASE("DropEnvelope_PhaseIntegral", "[processors][profundum][drop]") {
    const auto t0 = std::chrono::steady_clock::now();
    double worstRel = 0.0;
    double worstAbsSmall = 0.0;
    int cells = 0;
    const auto check = [&](const DropEnvelope& env, double fs) {
        const double got = env.phaseIntegralSeconds();
        const double ref = simpsonPhaseIntegralSeconds(env.plannedCurve(), fs);
        CAPTURE(got, ref);
        if (std::abs(ref) < 1e-6) {
            CHECK(std::abs(got - ref) <= 1e-12);
            worstAbsSmall = std::max(worstAbsSmall, std::abs(got - ref));
        } else {
            const double rel = std::abs(got - ref) / std::abs(ref);
            CHECK(rel <= 1e-6);
            worstRel = std::max(worstRel, rel);
        }
    };
    for (int k = 0; k <= 64; ++k) {
        for (const float R : {0.0f, 0.5f, 1.0f}) {
            for (const float A : {1.0f, 12.0f, 48.0f}) {
                for (const float T : {1.0f, 20.0f, 200.0f, 4000.0f}) {
                    for (const double fs : kRates) {
                        const float sp = shapeParam(static_cast<double>(k) / 64.0);
                        CAPTURE(sp, R, A, T, fs);
                        auto env = makeEnv(fs, A, T, sp, R);
                        env.trigger(true);
                        check(env, fs);
                        ++cells;
                    }
                }
            }
        }
    }
    REQUIRE(cells == 7020);
    // Bridged curves: one bridged trigger per (A, T) at u = 0.3 (exponential and elastic, R = 0.5).
    int bridgedCells = 0;
    for (const double s : {0.0, 1.0}) {
        for (const float A : {1.0f, 12.0f, 48.0f}) {
            for (const float T : {1.0f, 20.0f, 200.0f, 4000.0f}) {
                for (const double fs : kRates) {
                    const float sp = shapeParam(s);
                    CAPTURE(sp, A, T, fs);
                    auto env = makeEnv(fs, A, T, sp, 0.5f);
                    env.trigger(true);
                    (void)renderSamples(env,
                                        static_cast<std::size_t>(std::llround(0.3 * env.plannedCurve().fallSamples)));
                    env.trigger(false);
                    REQUIRE(env.plannedCurve().onsetFillet);
                    check(env, fs);
                    ++bridgedCells;
                }
            }
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // tasks.md T012: if this exceeds 15 s the case is tagged [long] and a four-anchor smoke is added.
    WARN("SC-024(a): " << cells << " snap + " << bridgedCells << " bridged cells, worst relative error " << worstRel
                       << ", worst absolute error (|ref| < 1e-6 s) " << worstAbsSmall << " s; wall clock " << secs
                       << " s");
}
