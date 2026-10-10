// ==============================================================================
// Layer 2: Processor Tests - SpectralShapeRecipe
//                                    (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/profundum-phase1-harmonic-core/spec.md
//            specs/profundum-phase1-harmonic-core/plan.md  (S8.1, S8.3)
//            specs/profundum-phase1-harmonic-core/tasks.md (T003 creates and
//                                                           wires this TU)
//
// SCOPE OF THIS TU (plan S8.1: recipe-only SCs, fast; tags [processors][profundum]):
//   SC-002, SC-003 (recipe arm), SC-004 (recipe-vector arm), SC-006, SC-007,
//   SC-010(c), SC-011 (b)-(e), SC-015 (recipe arm), SC-019 (recipe-vector arm),
//   SC-021(a), SC-022 (recipe arm), FR-002/FR-003 (ControlsSanitize), FR-010,
//   FR-030.
//
// T005: API, constants, named coordinates, sanitize, descriptor helpers.
// T006: evaluateShape, the plan S3.2 law (SC-002, SC-006, SC-022 recipe arms;
//       FR-003/005/010/013/015/017/018/021/023).
// T007: SC-004 / SC-005 / SC-019 vector arms and the L_c ceiling (FR-006).
// T008: evaluateMask, applyMask, evaluate, the plan S3.3 mask (FR-030/031/032,
//       FR-051 stages; SC-003, SC-007, SC-010(c) f0 arm, SC-011(b-d), SC-015,
//       SC-016 ceiling, SC-021(a), recipe side).
// This TU is compiled in the shipping fast-math mode (NOT in the
// -fno-fast-math list): no non-finite input is injected here (that is the
// NonFinite TU's job, T014), and the descriptor infinities are classified via
// ProfundumTest::isPosInf / isNegInf (barrier bit reads).
// ==============================================================================

#include "spectral_shape_recipe_test_helpers.h"

#include <krate/dsp/processors/harmonic_oscillator_bank.h>  // test-only: constant cross-checks
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <allocation_detector.h>  // operator new/delete replacements live in brownian_drift_test.cpp

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

using Controls = SpectralShapeRecipe::Controls;

template <class T>
concept Init6 = requires { T{0.f, 0.f, 0.f, 0.f, 0.f, 0.f}; };

template <class T>
concept Init7 = requires { T{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}; };

/// The six fields in declaration order, with their documented ranges (FR-002).
struct FieldSpec {
    const char* name;
    float Controls::*member;
    float lo;
    float hi;
};

constexpr std::array<FieldSpec, 6> kFields{{
    {"depth", &Controls::depth, 0.0f, 1.0f},
    {"body", &Controls::body, 0.0f, 1.0f},
    {"bodyCurvature", &Controls::bodyCurvature, 0.0f, 1.0f},
    {"bodyEmphasis", &Controls::bodyEmphasis, -1.0f, 0.0f},
    {"edge", &Controls::edge, 0.0f, 1.0f},
    {"shift", &Controls::shift, -1.0f, 1.0f},
}};

std::uint32_t bitsOf(float x) { return std::bit_cast<std::uint32_t>(x); }

void requireControlsExactly(const Controls& c, float depth, float body, float curvature, float emphasis,
                            float edge, float shift) {
    REQUIRE(c.depth == depth);
    REQUIRE(c.body == body);
    REQUIRE(c.bodyCurvature == curvature);
    REQUIRE(c.bodyEmphasis == emphasis);
    REQUIRE(c.edge == edge);
    REQUIRE(c.shift == shift);
}

/// 33 evenly spaced points over [lo, hi], both ends included (spec sweep resolution).
std::vector<float> sweep33(float lo, float hi) {
    std::vector<float> xs(33);
    for (std::size_t i = 0; i < xs.size(); ++i)
        xs[i] = lo + (hi - lo) * static_cast<float>(i) / 32.0f;
    return xs;
}

/// h1 / rest in dB of a stage-1 vector (+inf only if rest == 0).
double h1RestDbOf(const Controls& c, int n) { return ProfundumTest::describe(ProfundumTest::shapeOf(c, n)).h1RestDb; }

/// a_n / a_1 in dB relative to the reference ratio r_n (both re h1).
double ratioErrorDb(const std::vector<float>& a, std::size_t n, double rn) {
    const double ratio = static_cast<double>(a[n - 1]) / static_cast<double>(a[0]);
    return 20.0 * std::log10(ratio / rn);
}

/// Relative L2 error ||a/a1 - r||2 / ||r||2 over the whole vector (r_1 = 1).
double relativeL2(const std::vector<float>& a, const std::vector<double>& r) {
    double num = 0.0;
    double den = 0.0;
    const double a1 = static_cast<double>(a[0]);
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double e = static_cast<double>(a[i]) / a1 - r[i];
        num += e * e;
        den += r[i] * r[i];
    }
    return std::sqrt(num / den);
}

/// Least-squares slope of ln a_n against ln n over n = 1..nMax.
double logLogSlope(const std::vector<float>& a, std::size_t nMax) {
    double mx = 0.0;
    double my = 0.0;
    for (std::size_t n = 1; n <= nMax; ++n) {
        mx += std::log(static_cast<double>(n));
        my += std::log(static_cast<double>(a[n - 1]));
    }
    mx /= static_cast<double>(nMax);
    my /= static_cast<double>(nMax);
    double sxy = 0.0;
    double sxx = 0.0;
    for (std::size_t n = 1; n <= nMax; ++n) {
        const double dx = std::log(static_cast<double>(n)) - mx;
        const double dy = std::log(static_cast<double>(a[n - 1])) - my;
        sxy += dx * dy;
        sxx += dx * dx;
    }
    return sxy / sxx;
}

/// The 81-point D/B/E/S grid plus mid grid x edge {0, 0.5, 1} x curvature {0, 0.5, 1}.
std::vector<Controls> emphasisGrid() {
    std::vector<Controls> pts;
    for (int iD = 0; iD < 3; ++iD)
        for (int iB = 0; iB < 3; ++iB)
            for (int iE = 0; iE < 3; ++iE)
                for (int iS = 0; iS < 3; ++iS)
                    pts.push_back(ProfundumTest::gridPoint(iD, iB, iE, iS));
    for (const float edge : {0.0f, 0.5f, 1.0f}) {
        for (const float curvature : {0.0f, 0.5f, 1.0f}) {
            Controls c = ProfundumTest::midGrid();
            c.edge = edge;
            c.bodyCurvature = curvature;
            pts.push_back(c);
        }
    }
    return pts;
}

/// The three rates of the full-evaluation sample sets (SC-003, SC-010(c), SC-021(a)).
constexpr std::array<double, 3> kFullRates{44100.0, 48000.0, 96000.0};

/// One full-evaluation sample: Controls x f0 x rate.
struct FullSample {
    Controls c;
    float f0;
    double fs;
};

/// Controls uniform per range (kFields order), mt19937{0x5EED}.
Controls randomControls(std::mt19937& rng) {
    std::uniform_real_distribution<float> unit01(0.0f, 1.0f);
    Controls c{};
    for (const FieldSpec& spec : kFields)
        c.*spec.member = spec.lo + (spec.hi - spec.lo) * unit01(rng);
    return c;
}

/// T008 sample set (SC-021(a) / SC-003 full arm): 10 000 random Controls, each with an f0
/// drawn log-uniform over [8, capHz] at 44.1, 48 and 96 kHz (30 000 samples).
std::vector<FullSample> fullSampleSet() {
    std::mt19937 rng{0x5EED};
    std::uniform_real_distribution<double> unitD(0.0, 1.0);
    std::vector<FullSample> set;
    set.reserve(30000);
    for (int trial = 0; trial < 10000; ++trial) {
        const Controls c = randomControls(rng);
        for (const double fs : kFullRates) {
            const double cap = static_cast<double>(SpectralShapeRecipe::capFrequency(fs));
            const double f0 = 8.0 * std::pow(cap / 8.0, unitD(rng));
            set.push_back(FullSample{c, std::min(static_cast<float>(f0), static_cast<float>(cap)), fs});
        }
    }
    return set;
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-011(e), FR-002: the guard is a fixed constant; Controls holds exactly six
// fields and no note-behaviour (guard) field.
//
// No entry point takes a guard argument: evaluateShape(Controls, span),
// evaluateMask(f0Hz, sampleRate, span), applyMask(shape, mask, out) and
// evaluate(Controls, f0Hz, sampleRate, span) are the whole API, and the later
// tasks (T006, T008) call exactly those signatures.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_GuardIsConstant", "[processors][profundum]") {
    static_assert(SpectralShapeRecipe::kLowNoteGuardOnsetHz == 32.70f);
    static_assert(sizeof(SpectralShapeRecipe::Controls) == 6 * sizeof(float));
    static_assert(Init6<Controls> && !Init7<Controls>);

    auto [a, b, c, d, e, f] = SpectralShapeRecipe::kDefaultControls;
    REQUIRE(a == 0.5f);
    REQUIRE(b == 0.5f);
    REQUIRE(c == 0.5f);
    REQUIRE(d == 0.0f);
    REQUIRE(e == 0.5f);
    REQUIRE(f == 0.0f);

    const Controls defaulted{};
    requireControlsExactly(defaulted, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f);
}

// ------------------------------------------------------------------------------
// SC-006 analytic clause, FR-018, FR-014/FR-020: the anchors and every named
// coordinate of plan S3.5, field by field, exactly.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_AnchorCoordinatesAnalytic", "[processors][profundum]") {
    static_assert(SpectralShapeRecipe::kSawAnchor.depth == 0.0f);
    static_assert(SpectralShapeRecipe::kSineAnchor.depth == 1.0f);
    REQUIRE(SpectralShapeRecipe::baseExponent(SpectralShapeRecipe::kTriangleAnchor.depth) == 2.0f);

    for (const Controls* anchor : {&SpectralShapeRecipe::kSawAnchor, &SpectralShapeRecipe::kTriangleAnchor,
                                   &SpectralShapeRecipe::kSineAnchor}) {
        REQUIRE(anchor->body == 0.0f);
        REQUIRE(anchor->edge == 0.0f);
    }
    REQUIRE(SpectralShapeRecipe::kTriangleAnchor.bodyEmphasis == -1.0f);
    REQUIRE(SpectralShapeRecipe::kSawAnchor.bodyEmphasis == 0.0f);
    REQUIRE(SpectralShapeRecipe::kSineAnchor.bodyEmphasis == 0.0f);

    struct Row {
        const char* name;
        const Controls* actual;
        std::array<float, 6> expected;  // depth, body, curv, emph, edge, shift (plan S3.5)
    };
    const std::array<Row, 12> table{{
        {"kDefaultControls", &SpectralShapeRecipe::kDefaultControls, {0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f}},
        {"kSineAnchor", &SpectralShapeRecipe::kSineAnchor, {1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f}},
        {"kTriangleAnchor", &SpectralShapeRecipe::kTriangleAnchor, {0.5f, 0.0f, 0.5f, -1.0f, 0.0f, 0.0f}},
        {"kSawAnchor", &SpectralShapeRecipe::kSawAnchor, {0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f}},
        {"kHeavy", &SpectralShapeRecipe::kHeavy, {0.5f, 0.3f, 0.5f, 0.0f, 0.0f, -1.0f}},
        {"kHollow", &SpectralShapeRecipe::kHollow, {0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f}},
        {"kGrowl", &SpectralShapeRecipe::kGrowl, {0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f}},
        {"kBodyRound", &SpectralShapeRecipe::kBodyRound, {0.2f, 0.0f, 0.1f, -0.8f, 0.1f, -0.4f}},
        {"kBodyHollow", &SpectralShapeRecipe::kBodyHollow, {0.0f, 0.25f, 0.6f, -1.0f, 0.9f, 0.85f}},
        {"kBodyWoody", &SpectralShapeRecipe::kBodyWoody, {0.65f, 0.5f, 0.95f, -0.1f, 0.3f, 0.05f}},
        {"kBodyNasal", &SpectralShapeRecipe::kBodyNasal, {0.6f, 0.7f, 0.95f, -0.2f, 0.3f, -0.95f}},
        {"kBodyThick", &SpectralShapeRecipe::kBodyThick, {0.5f, 0.9f, 0.05f, 0.0f, 0.0f, -0.05f}},
    }};

    for (const Row& row : table) {
        INFO(row.name);
        const std::array<float, 6>& x = row.expected;
        requireControlsExactly(*row.actual, x[0], x[1], x[2], x[3], x[4], x[5]);
    }
}

// ------------------------------------------------------------------------------
// FR-002 clamp arm: out-of-range values clamp to the range end; in-range values
// pass through bit-unchanged; evaluateShape of an out-of-range input is
// memcmp-identical to evaluateShape of the clamped input (T006 arm).
// (Non-finite inputs: NonFinite TU, T014.)
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_ControlsSanitize", "[processors][profundum]") {
    const std::array<float, 4> outOfRange{-10.0f, 10.0f, FLT_MAX, -FLT_MAX};

    for (const FieldSpec& field : kFields) {
        INFO("field " << field.name);
        for (const float v : outOfRange) {
            INFO("input " << v);
            Controls in = SpectralShapeRecipe::kDefaultControls;
            in.*field.member = v;
            const Controls out = SpectralShapeRecipe::sanitize(in);
            const float expected = v < field.lo ? field.lo : field.hi;
            REQUIRE(out.*field.member == expected);
            // Every other field is untouched (the default is in range).
            for (const FieldSpec& other : kFields) {
                if (other.member == field.member)
                    continue;
                REQUIRE(bitsOf(out.*other.member) == bitsOf(SpectralShapeRecipe::kDefaultControls.*other.member));
            }
        }
    }

    // In-range values, including both range ends, pass through bit-unchanged.
    const std::array<Controls, 5> inRange{{
        Controls{.depth = 0.0f, .body = 0.0f, .bodyCurvature = 0.0f, .bodyEmphasis = -1.0f, .edge = 0.0f,
                 .shift = -1.0f},
        Controls{.depth = 1.0f, .body = 1.0f, .bodyCurvature = 1.0f, .bodyEmphasis = 0.0f, .edge = 1.0f,
                 .shift = 1.0f},
        Controls{.depth = 0.3f, .body = 0.123456f, .bodyCurvature = 0.77f, .bodyEmphasis = -0.37f,
                 .edge = 0.999f, .shift = -0.25f},
        Controls{.depth = 1.0e-7f, .body = 0.9999999f, .bodyCurvature = 0.5f, .bodyEmphasis = -1.0e-7f,
                 .edge = 1.0e-30f, .shift = 0.9999999f},
        SpectralShapeRecipe::kBodyNasal,
    }};
    for (const Controls& in : inRange) {
        const Controls out = SpectralShapeRecipe::sanitize(in);
        for (const FieldSpec& field : kFields) {
            INFO("field " << field.name << " input " << in.*field.member);
            REQUIRE(bitsOf(out.*field.member) == bitsOf(in.*field.member));
        }
    }

    // T006 arm: evaluateShape sanitises first, so an out-of-range input renders
    // exactly the clamped input's vector.
    for (const FieldSpec& field : kFields) {
        INFO("field " << field.name);
        for (const float v : outOfRange) {
            INFO("input " << v);
            Controls in = SpectralShapeRecipe::kDefaultControls;
            in.*field.member = v;
            Controls clamped = SpectralShapeRecipe::kDefaultControls;
            clamped.*field.member = v < field.lo ? field.lo : field.hi;
            for (const int n : {1, 64, 96}) {
                INFO("N " << n);
                const std::vector<float> a = ProfundumTest::shapeOf(in, n);
                const std::vector<float> b = ProfundumTest::shapeOf(clamped, n);
                REQUIRE(ProfundumTest::samplesBitEqual(a.data(), b.data(), a.size()));
            }
        }
    }
}

// ------------------------------------------------------------------------------
// FR-005: P0 derived from FR-049's output level and the bank's centre gain;
// the cap fraction equals the bank's anti-alias fade start (FR-032).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_PowerTargetDerivation", "[processors][profundum]") {
    const double derived = 2.0 * std::pow(std::pow(10.0, -12.0 / 20.0) / 0.7071067811865476, 2.0);
    REQUIRE(static_cast<double>(SpectralShapeRecipe::kPowerTarget) == Approx(derived).epsilon(1e-7));
    REQUIRE(static_cast<double>(SpectralShapeRecipe::kPowerTarget) == Approx(0.2523829).epsilon(1e-6));

    STATIC_REQUIRE(SpectralShapeRecipe::kCoreOutputRmsDb == -12.0f);
    STATIC_REQUIRE(SpectralShapeRecipe::kCenterPanGain == HarmonicOscillatorBank::kCenterPanGain);
    STATIC_REQUIRE(SpectralShapeRecipe::kNyquistCapFraction == HarmonicOscillatorBank::kAntiAliasFadeStart);

    REQUIRE(SpectralShapeRecipe::capFrequency(48000.0) == Approx(19200.0f));
    REQUIRE(SpectralShapeRecipe::capFrequency(44100.0) == Approx(17640.0f));
}

// ------------------------------------------------------------------------------
// Descriptor helpers self-check (spec "Descriptor definitions", plan S8.2).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_DescriptorHelpersSelfCheck", "[processors][profundum]") {
    SECTION("h1 only: h1/rest is +inf, centroid 0") {
        std::vector<float> a(16, 0.0f);
        a[0] = 1.0f;
        const ProfundumTest::Descriptors d = ProfundumTest::describe(a);
        REQUIRE(ProfundumTest::isPosInf(d.h1RestDb));
        REQUIRE(d.centroidOct == 0.0);
        REQUIRE(d.spreadOct == 0.0);
        REQUIRE(d.eSub == 1.0);
        REQUIRE(d.eTotal == 1.0);
    }

    SECTION("w2 only: centroid 1 octave, spread 0") {
        std::vector<double> w(16, 0.0);
        w[1] = 1.0;
        const ProfundumTest::Descriptors d = ProfundumTest::describePowers(w);
        REQUIRE(d.centroidOct == 1.0);
        REQUIRE(d.spreadOct == 0.0);
        REQUIRE(d.eBody == 1.0);
        REQUIRE(ProfundumTest::isNegInf(d.h1RestDb));
    }

    SECTION("a = {1, 1}: odd/even is -inf, clipped to -30") {
        const std::vector<float> a{1.0f, 1.0f};
        const ProfundumTest::Descriptors d = ProfundumTest::describe(a);
        REQUIRE(ProfundumTest::isNegInf(d.oddEvenDb));
        REQUIRE(d.oddEvenClippedDb == -30.0);
        REQUIRE(d.h1RestDb == Approx(0.0).margin(1e-12));
        REQUIRE(d.centroidOct == Approx(0.5).margin(1e-12));
        REQUIRE(d.spreadOct == Approx(0.5).margin(1e-12));
    }

    SECTION("distances: zero to self, body-colour adds the odd/even axis") {
        const std::vector<float> a{1.0f, 0.5f, 0.25f, 0.125f, 0.1f, 0.05f, 0.02f, 0.01f, 0.01f, 0.01f};
        const std::vector<float> b{1.0f, 0.0f, 0.3f, 0.0f, 0.2f, 0.0f, 0.1f, 0.0f, 0.05f, 0.0f};
        const ProfundumTest::Descriptors da = ProfundumTest::describe(a);
        const ProfundumTest::Descriptors db = ProfundumTest::describe(b);
        REQUIRE(ProfundumTest::descriptorDistance(da, da) == 0.0);
        REQUIRE(ProfundumTest::bodyColourDistance(da, da) == 0.0);
        // b has no evens: odd/even +inf, clipped to +30.
        REQUIRE(ProfundumTest::isPosInf(db.oddEvenDb));
        REQUIRE(db.oddEvenClippedDb == 30.0);
        REQUIRE(ProfundumTest::bodyColourDistance(da, db) > ProfundumTest::descriptorDistance(da, db));
    }

    SECTION("grid helpers") {
        const Controls mid = ProfundumTest::midGrid();
        requireControlsExactly(mid, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f);
        requireControlsExactly(ProfundumTest::gridPoint(0, 2, 0, 0), 0.0f, 1.0f, 0.5f, 0.0f, 0.0f, -1.0f);
        requireControlsExactly(ProfundumTest::gridPoint(2, 0, 2, 2), 1.0f, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f);
    }
}

// ==============================================================================
// T006: evaluateShape, the plan S3.2 law
// ==============================================================================

// ------------------------------------------------------------------------------
// SC-006 (recipe arm), FR-018/FR-021: the three waveform anchors.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_WaveformAnchors", "[processors][profundum]") {
    for (const int n : {64, 96}) {
        INFO("N " << n);
        const auto size = static_cast<std::size_t>(n);

        // sine: rest <= -60 dB re h1 (model -77.6)
        {
            const double h1Rest = h1RestDbOf(SpectralShapeRecipe::kSineAnchor, n);
            INFO("sine h1/rest " << h1Rest << " dB");
            REQUIRE_FALSE(ProfundumTest::isPosInf(h1Rest));
            REQUIRE(h1Rest >= 60.0);
        }

        // triangle: odd 1/n^2 re h1, every even element exactly 0
        {
            const std::vector<float> a = ProfundumTest::shapeOf(SpectralShapeRecipe::kTriangleAnchor, n);
            REQUIRE(a[0] > 0.0f);
            std::vector<double> r(size, 0.0);
            for (std::size_t k = 1; k <= size; ++k) {
                INFO("triangle n " << k);
                if (k % 2 == 0) {
                    REQUIRE(a[k - 1] == 0.0f);  // <= -50 dB trivially (SC-006)
                    continue;
                }
                r[k - 1] = 1.0 / (static_cast<double>(k) * static_cast<double>(k));
                if (k <= 15)
                    REQUIRE(std::abs(ratioErrorDb(a, k, r[k - 1])) <= 1.5);
            }
            const double l2 = relativeL2(a, r);
            INFO("triangle relative L2 " << l2);
            REQUIRE(l2 <= 0.05);
        }

        // saw: 1/n re h1 (+-1.5 dB to n = 16, +-3 dB above)
        {
            const std::vector<float> a = ProfundumTest::shapeOf(SpectralShapeRecipe::kSawAnchor, n);
            REQUIRE(a[0] > 0.0f);
            std::vector<double> r(size, 0.0);
            for (std::size_t k = 1; k <= size; ++k) {
                INFO("saw n " << k);
                r[k - 1] = 1.0 / static_cast<double>(k);
                const double err = std::abs(ratioErrorDb(a, k, r[k - 1]));
                REQUIRE(err <= (k <= 16 ? 1.5 : 3.0));
            }
            const double l2 = relativeL2(a, r);
            INFO("saw relative L2 " << l2);
            REQUIRE(l2 <= 0.05);
        }
    }
}

// ------------------------------------------------------------------------------
// SC-002 (recipe arm), FR-010: h1/rest strictly increasing in depth at every
// one of the 27 body/edge/shift grid points (depth is the swept axis).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_DepthMonotonic", "[processors][profundum]") {
    std::vector<float> depths = sweep33(0.0f, 1.0f);
    depths.push_back(SpectralShapeRecipe::kDepthTriangle);
    std::sort(depths.begin(), depths.end());
    depths.erase(std::unique(depths.begin(), depths.end()), depths.end());
    REQUIRE(depths.size() >= 33);

    for (int iB = 0; iB < 3; ++iB) {
        for (int iE = 0; iE < 3; ++iE) {
            for (int iS = 0; iS < 3; ++iS) {
                Controls c = ProfundumTest::gridPoint(0, iB, iE, iS);
                INFO("body " << c.body << " edge " << c.edge << " shift " << c.shift);
                double prev = 0.0;
                for (std::size_t i = 0; i < depths.size(); ++i) {
                    c.depth = depths[i];
                    const double h1Rest = h1RestDbOf(c, 64);
                    INFO("depth " << c.depth << " h1/rest " << h1Rest << " prev " << prev);
                    REQUIRE_FALSE(ProfundumTest::isPosInf(h1Rest));
                    if (i > 0)
                        REQUIRE(h1Rest > prev);
                    prev = h1Rest;
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-002, FR-018: depth walks saw (slope -1) -> triangle (slope -2) -> sine.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_DepthWalksWaveformPath", "[processors][profundum]") {
    Controls c{.depth = 0.0f, .body = 0.0f, .bodyCurvature = 0.5f, .bodyEmphasis = 0.0f, .edge = 0.0f,
               .shift = 0.0f};

    const double slope0 = logLogSlope(ProfundumTest::shapeOf(c, 64), 15);
    INFO("slope at depth 0: " << slope0);
    REQUIRE(slope0 == Approx(-1.0).margin(0.05));

    c.depth = 0.5f;
    const double slopeHalf = logLogSlope(ProfundumTest::shapeOf(c, 64), 15);
    INFO("slope at depth 0.5: " << slopeHalf);
    REQUIRE(slopeHalf == Approx(-2.0).margin(0.05));

    c.depth = 1.0f;
    const double h1Rest = h1RestDbOf(c, 64);
    INFO("h1/rest at depth 1: " << h1Rest);
    REQUIRE_FALSE(ProfundumTest::isPosInf(h1Rest));
    REQUIRE(h1Rest >= 60.0);
}

// ------------------------------------------------------------------------------
// FR-010: depth 1 is sine-like at any body/edge/curvature/shift (81 points),
// at emphasis 0 and -1, N = 64 and 96. Rest <= -30 dB re h1 (model worst -43.8).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_DepthOneSineAtAnyShape", "[processors][profundum]") {
    constexpr std::array<float, 3> kUnit{0.0f, 0.5f, 1.0f};
    constexpr std::array<float, 3> kSigned{-1.0f, 0.0f, 1.0f};
    double worst = 1.0e9;
    for (const int n : {64, 96}) {
        for (const float emphasis : {0.0f, -1.0f}) {
            for (const float body : kUnit) {
                for (const float edge : kUnit) {
                    for (const float curvature : kUnit) {
                        for (const float shift : kSigned) {
                            const Controls c{.depth = 1.0f, .body = body, .bodyCurvature = curvature,
                                             .bodyEmphasis = emphasis, .edge = edge, .shift = shift};
                            const double h1Rest = h1RestDbOf(c, n);
                            INFO("N " << n << " emph " << emphasis << " body " << body << " edge " << edge
                                      << " curv " << curvature << " shift " << shift << " h1/rest " << h1Rest);
                            REQUIRE_FALSE(ProfundumTest::isPosInf(h1Rest));
                            REQUIRE(h1Rest >= 30.0);
                            worst = std::min(worst, h1Rest);
                        }
                    }
                }
            }
        }
    }
    WARN("FR-010 depth 1 worst rest re h1: " << -worst << " dB (bar -30, model -43.8)");
}

// ------------------------------------------------------------------------------
// SC-022 (recipe arm), FR-013/FR-023: emphasis acts on the whole vector.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_EmphasisActsOnWholeVector", "[processors][profundum]") {
    const std::vector<Controls> grid = emphasisGrid();
    REQUIRE(grid.size() == 90);

    SECTION("emphasis -1: every even element is exactly 0") {
        for (Controls c : grid) {
            c.bodyEmphasis = -1.0f;
            INFO("depth " << c.depth << " body " << c.body << " curv " << c.bodyCurvature << " edge " << c.edge
                          << " shift " << c.shift);
            const std::vector<float> a = ProfundumTest::shapeOf(c, 64);
            for (std::size_t k = 2; k <= a.size(); k += 2) {
                INFO("n " << k);
                REQUIRE(a[k - 1] == 0.0f);
            }
        }
        // Spot-check: at edge = 1 the shelf raises n >= 10, yet the evens there stay 0.
        Controls c = ProfundumTest::midGrid();
        c.edge = 1.0f;
        c.bodyEmphasis = -1.0f;
        const std::vector<float> a = ProfundumTest::shapeOf(c, 96);
        for (std::size_t k = 10; k <= a.size(); ++k) {
            INFO("n " << k);
            if (k % 2 == 0)
                REQUIRE(a[k - 1] == 0.0f);
            else
                REQUIRE(a[k - 1] > 0.0f);
        }
    }

    SECTION("FR-023 floor: odd/even never below the emphasis-0 value") {
        const std::vector<float> emphases = sweep33(-1.0f, 0.0f);
        for (Controls c : grid) {
            c.bodyEmphasis = 0.0f;
            const double neutral = ProfundumTest::describe(ProfundumTest::shapeOf(c, 64)).oddEvenDb;
            INFO("depth " << c.depth << " body " << c.body << " curv " << c.bodyCurvature << " edge " << c.edge
                          << " shift " << c.shift << " neutral " << neutral);
            REQUIRE_FALSE(ProfundumTest::isPosInf(neutral));
            REQUIRE_FALSE(ProfundumTest::isNegInf(neutral));
            for (const float emphasis : emphases) {
                c.bodyEmphasis = emphasis;
                const double oddEven = ProfundumTest::describe(ProfundumTest::shapeOf(c, 64)).oddEvenDb;
                INFO("emphasis " << emphasis << " odd/even " << oddEven);
                REQUIRE_FALSE(ProfundumTest::isNegInf(oddEven));
                if (ProfundumTest::isPosInf(oddEven))
                    continue;  // evens all 0: above any finite floor
                REQUIRE(oddEven >= neutral);
            }
        }
    }
}

// ------------------------------------------------------------------------------
// FR-005/FR-003 (shape arm): power-normalised to P0, finite, non-negative, a1 > 0.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_PowerNormalised", "[processors][profundum]") {
    std::mt19937 rng{0x5EED};
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> emph(-1.0f, 0.0f);
    std::uniform_real_distribution<float> sgn(-1.0f, 1.0f);

    const double p0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);
    constexpr std::array<std::size_t, 5> kSizes{1, 2, 16, 64, 96};
    std::array<float, 96> buffer{};
    double worstRel = 0.0;
    int nonFinite = 0;
    int negative = 0;
    int h1NotPositive = 0;
    int evaluations = 0;

    for (int trial = 0; trial < 10000; ++trial) {
        Controls c{};
        c.depth = unit(rng);
        c.body = unit(rng);
        c.bodyCurvature = unit(rng);
        c.bodyEmphasis = emph(rng);
        c.edge = unit(rng);
        c.shift = sgn(rng);
        for (const std::size_t n : kSizes) {
            const std::span<float> out(buffer.data(), n);
            SpectralShapeRecipe::evaluateShape(c, out);
            ++evaluations;
            double power = 0.0;
            for (const float v : out) {
                if (!detail::isFinite(v))
                    ++nonFinite;
                else if (v < 0.0f)
                    ++negative;
                power += static_cast<double>(v) * static_cast<double>(v);
            }
            if (!(out[0] > 0.0f))
                ++h1NotPositive;
            worstRel = std::max(worstRel, std::abs(power - p0) / p0);
        }
    }

    INFO("evaluations " << evaluations << " worst relative power error " << worstRel);
    REQUIRE(evaluations == 50000);
    REQUIRE(nonFinite == 0);
    REQUIRE(negative == 0);
    REQUIRE(h1NotPositive == 0);
    REQUIRE(worstRel <= 1.0e-4);

    // T008 full arm (SC-003): the SC-021(a) sample set through evaluate, N = 96.
    SECTION("full arm: evaluate is P0-normalised, finite, non-negative") {
        const std::vector<FullSample> set = fullSampleSet();
        REQUIRE(set.size() == 30000);
        double worstFull = 0.0;
        int badElements = 0;
        std::array<float, 96> full{};
        for (const FullSample& s : set) {
            SpectralShapeRecipe::evaluate(s.c, s.f0, s.fs, full);
            double power = 0.0;
            for (const float v : full) {
                if (!detail::isFinite(v) || v < 0.0f)
                    ++badElements;
                power += static_cast<double>(v) * static_cast<double>(v);
            }
            worstFull = std::max(worstFull, std::abs(power - p0) / p0);
        }
        INFO("full arm worst relative power error " << worstFull);
        REQUIRE(badElements == 0);
        REQUIRE(worstFull <= 1.0e-4);
    }
}

// ------------------------------------------------------------------------------
// Edge cases: N = 1, N = 0, and an output span longer than kMaxPartials.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_ShapeEdgeCases", "[processors][profundum]") {
    const double p0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);

    SECTION("size 1: a1^2 == P0") {
        const std::array<Controls, 4> states{SpectralShapeRecipe::kDefaultControls, SpectralShapeRecipe::kSineAnchor,
                                             SpectralShapeRecipe::kGrowl, SpectralShapeRecipe::kBodyHollow};
        for (const Controls& c : states) {
            std::array<float, 1> a{-1.0f};
            SpectralShapeRecipe::evaluateShape(c, a);
            const double a1 = static_cast<double>(a[0]);
            REQUIRE(a1 * a1 == Approx(p0).margin(1.0e-4));
        }
    }

    SECTION("size 0: writes nothing") {
        std::array<float, 4> sentinel{-1.0f, -1.0f, -1.0f, -1.0f};
        SpectralShapeRecipe::evaluateShape(SpectralShapeRecipe::kDefaultControls,
                                           std::span<float>(sentinel.data(), 0));
        SpectralShapeRecipe::evaluateShape(SpectralShapeRecipe::kDefaultControls, std::span<float>{});
        for (const float v : sentinel)
            REQUIRE(v == -1.0f);
    }

    SECTION("size 128: writes the first 96, leaves 96..127 untouched") {
        std::vector<float> a(128, -1.0f);
        SpectralShapeRecipe::evaluateShape(SpectralShapeRecipe::kDefaultControls, a);
        double power = 0.0;
        for (std::size_t i = 0; i < 96; ++i) {
            INFO("index " << i);
            REQUIRE(a[i] >= 0.0f);
            power += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        }
        REQUIRE(power == Approx(p0).epsilon(1.0e-4));
        for (std::size_t i = 96; i < 128; ++i) {
            INFO("index " << i);
            REQUIRE(a[i] == -1.0f);
        }
        // The first 96 equal an exact-size N = 96 evaluation.
        const std::vector<float> exact = ProfundumTest::shapeOf(SpectralShapeRecipe::kDefaultControls, 96);
        REQUIRE(ProfundumTest::samplesBitEqual(a.data(), exact.data(), 96));
    }
}

// ==============================================================================
// T007: SC-004 / SC-005 / SC-019 vector arms and the L_c ceiling
//       (FR-006, FR-011/012/015/016; plan S1 C-1/C-2/C-3)
// ==============================================================================

namespace {

/// Descriptors of the stage-1 vector over a 33-point sweep of one field from `base`.
std::vector<ProfundumTest::Descriptors> sweepDescriptors(Controls base, float Controls::*field, float lo, float hi,
                                                         int n) {
    std::vector<ProfundumTest::Descriptors> out;
    for (const float x : sweep33(lo, hi)) {
        base.*field = x;
        out.push_back(ProfundumTest::describe(ProfundumTest::shapeOf(base, n)));
    }
    return out;
}

std::string controlsText(const Controls& c) {
    std::ostringstream os;
    os << "depth " << c.depth << " body " << c.body << " curv " << c.bodyCurvature << " emph " << c.bodyEmphasis
       << " edge " << c.edge << " shift " << c.shift;
    return os.str();
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-004 (recipe-vector arm), FR-011/FR-012/FR-015/FR-016: every macro lever
// moves its descriptor monotonically and audibly at every grid point of the
// other three controls (curvature 0.5, emphasis 0, N = 64).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_AudibilityGate_BodyEdgeShift", "[processors][profundum]") {
    constexpr int kN = 64;
    constexpr std::array<float, 3> kUnit{0.0f, 0.5f, 1.0f};
    constexpr std::array<float, 3> kSigned{-1.0f, 0.0f, 1.0f};

    SECTION("Body: R_body strictly increasing; span vs headroom (plan S1 C-1)") {
        double minSpan6 = 1.0e9;
        double minRatioLow = 1.0e9;
        int nHeadroom6 = 0;
        int nLow = 0;
        for (const float depth : kUnit) {
            for (const float edge : kUnit) {
                for (const float shift : kSigned) {
                    Controls base = ProfundumTest::midGrid();
                    base.depth = depth;
                    base.edge = edge;
                    base.shift = shift;
                    INFO(controlsText(base));
                    const auto d = sweepDescriptors(base, &Controls::body, 0.0f, 1.0f, kN);
                    for (std::size_t i = 1; i < d.size(); ++i) {
                        INFO("step " << i << " R_body " << d[i].rBodyDb << " prev " << d[i - 1].rBodyDb);
                        REQUIRE(d[i].rBodyDb > d[i - 1].rBodyDb);
                    }
                    // plan S1 C-1: headroom = -R_body(body = 0).
                    const double headroom = -d.front().rBodyDb;
                    const double span = d.back().rBodyDb - d.front().rBodyDb;
                    INFO("headroom " << headroom << " dB, span " << span << " dB");
                    if (headroom >= 6.0) {
                        REQUIRE(span >= 6.0);
                        minSpan6 = std::min(minSpan6, span);
                        ++nHeadroom6;
                    } else {
                        REQUIRE(headroom > 0.0);
                        REQUIRE(span >= 0.7 * headroom);
                        minRatioLow = std::min(minRatioLow, span / headroom);
                        ++nLow;
                    }
                }
            }
        }
        // Exactly 5 low-headroom points: a constant change cannot silently move points between the sets.
        REQUIRE(nLow == 5);
        REQUIRE(nHeadroom6 == 22);
        WARN("SC-004 Body min span (headroom >= 6 dB, " << nHeadroom6 << " points): " << minSpan6
                                                         << " dB (bar 6, model 8.65)");
        WARN("SC-004 Body min span / headroom (headroom < 6 dB, " << nLow << " points): " << minRatioLow
                                                                   << " (bar 0.7, model 0.740)");
    }

    SECTION("Edge: R_pres strictly increasing, span >= 6 dB") {
        double minSpan = 1.0e9;
        for (const float depth : kUnit) {
            for (const float body : kUnit) {
                for (const float shift : kSigned) {
                    Controls base = ProfundumTest::midGrid();
                    base.depth = depth;
                    base.body = body;
                    base.shift = shift;
                    INFO(controlsText(base));
                    const auto d = sweepDescriptors(base, &Controls::edge, 0.0f, 1.0f, kN);
                    for (std::size_t i = 1; i < d.size(); ++i) {
                        INFO("step " << i << " R_pres " << d[i].rPresDb << " prev " << d[i - 1].rPresDb);
                        REQUIRE(d[i].rPresDb > d[i - 1].rPresDb);
                    }
                    const double span = d.back().rPresDb - d.front().rPresDb;
                    INFO("span " << span << " dB");
                    REQUIRE(span >= 6.0);
                    minSpan = std::min(minSpan, span);
                }
            }
        }
        WARN("SC-004 Edge min span: " << minSpan << " dB (bar 6, model 8.89)");
    }

    SECTION("Shift: C strictly increasing (plan S1 C-2)") {
        // depth in {0, 0.5}: 16 points, span >= 1 octave.
        double minSpan = 1.0e9;
        int nPoints = 0;
        for (const float depth : {0.0f, 0.5f}) {
            for (const float body : kUnit) {
                for (const float edge : kUnit) {
                    if (body == 0.0f && edge == 0.0f)
                        continue;
                    Controls base = ProfundumTest::midGrid();
                    base.depth = depth;
                    base.body = body;
                    base.edge = edge;
                    INFO(controlsText(base));
                    const auto d = sweepDescriptors(base, &Controls::shift, -1.0f, 1.0f, kN);
                    for (std::size_t i = 1; i < d.size(); ++i) {
                        INFO("step " << i << " C " << d[i].centroidOct << " prev " << d[i - 1].centroidOct);
                        REQUIRE(d[i].centroidOct > d[i - 1].centroidOct);
                    }
                    const double span = d.back().centroidOct - d.front().centroidOct;
                    INFO("span " << span << " oct");
                    REQUIRE(span >= 1.0);
                    minSpan = std::min(minSpan, span);
                    ++nPoints;
                }
            }
        }
        REQUIRE(nPoints == 16);

        // depth 1: strictly increasing on the float vectors when body + edge > 0 (8 points);
        // constant within 1e-6 relative at body = edge = 0 (FR-016 as amended by C-2).
        double minRelStep = 1.0e9;
        int nDepthOne = 0;
        for (const float body : kUnit) {
            for (const float edge : kUnit) {
                Controls base = ProfundumTest::midGrid();
                base.depth = 1.0f;
                base.body = body;
                base.edge = edge;
                INFO(controlsText(base));
                const auto d = sweepDescriptors(base, &Controls::shift, -1.0f, 1.0f, kN);
                if (body == 0.0f && edge == 0.0f) {
                    const double c0 = d.front().centroidOct;
                    REQUIRE(c0 > 0.0);
                    for (std::size_t i = 1; i < d.size(); ++i) {
                        INFO("step " << i << " C " << d[i].centroidOct << " C(shift=-1) " << c0);
                        REQUIRE(std::abs(d[i].centroidOct - c0) <= 1.0e-6 * std::abs(c0));
                    }
                    continue;
                }
                for (std::size_t i = 1; i < d.size(); ++i) {
                    INFO("step " << i << " C " << d[i].centroidOct << " prev " << d[i - 1].centroidOct);
                    REQUIRE(d[i].centroidOct > d[i - 1].centroidOct);
                    minRelStep =
                        std::min(minRelStep, (d[i].centroidOct - d[i - 1].centroidOct) / d[i - 1].centroidOct);
                }
                ++nDepthOne;
            }
        }
        REQUIRE(nDepthOne == 8);
        WARN("SC-004 Shift min span (depth {0, 0.5}, " << nPoints << " points): " << minSpan
                                                        << " oct (bar 1, model 1.427)");
        WARN("FR-016/C-2 depth 1 min per-step dC/C: " << minRelStep << " (bar > 0, model 2.68e-3)");
    }

    SECTION("Endpoints: pairwise descriptor distance >= 6 dB") {
        Controls bodyEnd = ProfundumTest::midGrid();
        bodyEnd.body = 1.0f;
        Controls edgeEnd = ProfundumTest::midGrid();
        edgeEnd.edge = 1.0f;
        Controls shiftEnd = ProfundumTest::midGrid();
        shiftEnd.shift = 1.0f;
        const auto dB = ProfundumTest::describe(ProfundumTest::shapeOf(bodyEnd, kN));
        const auto dE = ProfundumTest::describe(ProfundumTest::shapeOf(edgeEnd, kN));
        const auto dS = ProfundumTest::describe(ProfundumTest::shapeOf(shiftEnd, kN));
        const double bodyEdge = ProfundumTest::descriptorDistance(dB, dE);
        const double bodyShift = ProfundumTest::descriptorDistance(dB, dS);
        const double edgeShift = ProfundumTest::descriptorDistance(dE, dS);
        INFO("body-edge " << bodyEdge << " body-shift " << bodyShift << " edge-shift " << edgeShift);
        REQUIRE(bodyEdge >= 6.0);
        REQUIRE(bodyShift >= 6.0);
        REQUIRE(edgeShift >= 6.0);
        WARN("SC-004 endpoint min pairwise distance: " << std::min({bodyEdge, bodyShift, edgeShift})
                                                       << " dB (bar 6, model 7.07)");
    }
}

// ------------------------------------------------------------------------------
// SC-005 (vector arm), FR-013: the Body sub-controls at mid grid, N = 64.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_BodySubControls", "[processors][profundum]") {
    constexpr int kN = 64;

    SECTION("curvature: sigma strictly decreasing, |span| >= 0.25 oct") {
        const auto d = sweepDescriptors(ProfundumTest::midGrid(), &Controls::bodyCurvature, 0.0f, 1.0f, kN);
        for (std::size_t i = 1; i < d.size(); ++i) {
            INFO("step " << i << " sigma " << d[i].spreadOct << " prev " << d[i - 1].spreadOct);
            REQUIRE(d[i].spreadOct < d[i - 1].spreadOct);
        }
        const double span = d.back().spreadOct - d.front().spreadOct;
        INFO("sigma span " << span << " oct");
        REQUIRE(std::abs(span) >= 0.25);
        WARN("SC-005 curvature sigma span: " << span << " oct (bar |span| >= 0.25, model -0.375)");
    }

    SECTION("emphasis: odd/even strictly decreasing over [-1, 0], span >= 20 dB") {
        const auto d = sweepDescriptors(ProfundumTest::midGrid(), &Controls::bodyEmphasis, -1.0f, 0.0f, kN);
        // emphasis -1: every even is exactly 0, so odd/even is +inf.
        REQUIRE(ProfundumTest::isPosInf(d.front().oddEvenDb));
        for (std::size_t i = 1; i < d.size(); ++i) {
            INFO("step " << i << " odd/even " << d[i].oddEvenDb);
            REQUIRE_FALSE(ProfundumTest::isPosInf(d[i].oddEvenDb));
            REQUIRE_FALSE(ProfundumTest::isNegInf(d[i].oddEvenDb));
            if (i >= 2) {
                INFO("prev " << d[i - 1].oddEvenDb);
                REQUIRE(d[i].oddEvenDb < d[i - 1].oddEvenDb);
            }
        }
        const double span = d[1].oddEvenDb - d.back().oddEvenDb;
        INFO("odd/even " << d[1].oddEvenDb << " (2nd point) -> " << d.back().oddEvenDb << " dB, span " << span);
        REQUIRE(span >= 20.0);
        WARN("SC-005 emphasis odd/even: " << d[1].oddEvenDb << " (2nd point) -> " << d.back().oddEvenDb
                                          << " dB, span " << span << " dB (bar 20, model 34.8 -> 4.7)");
    }
}

// ------------------------------------------------------------------------------
// SC-019 (recipe-vector arm), FR-014/FR-020: the five Body colours are distinct
// and each owns its extremal descriptor. Spec-mandated name.
// ------------------------------------------------------------------------------
TEST_CASE("ProfundumCore_BodyColoursDistinct", "[processors][profundum]") {
    constexpr int kN = 64;
    struct Colour {
        const char* name;
        ProfundumTest::Descriptors d;
    };
    const std::array<Colour, 5> colours{{
        {"kBodyRound", ProfundumTest::describe(ProfundumTest::shapeOf(SpectralShapeRecipe::kBodyRound, kN))},
        {"kBodyHollow", ProfundumTest::describe(ProfundumTest::shapeOf(SpectralShapeRecipe::kBodyHollow, kN))},
        {"kBodyWoody", ProfundumTest::describe(ProfundumTest::shapeOf(SpectralShapeRecipe::kBodyWoody, kN))},
        {"kBodyNasal", ProfundumTest::describe(ProfundumTest::shapeOf(SpectralShapeRecipe::kBodyNasal, kN))},
        {"kBodyThick", ProfundumTest::describe(ProfundumTest::shapeOf(SpectralShapeRecipe::kBodyThick, kN))},
    }};
    constexpr std::size_t kRound = 0;
    constexpr std::size_t kHollow = 1;
    constexpr std::size_t kNasal = 3;
    constexpr std::size_t kThick = 4;

    // Pairwise body-colour distance >= 6 dB.
    double minDistance = 1.0e9;
    for (std::size_t i = 0; i < colours.size(); ++i) {
        for (std::size_t j = i + 1; j < colours.size(); ++j) {
            const double dist = ProfundumTest::bodyColourDistance(colours[i].d, colours[j].d);
            INFO(colours[i].name << " vs " << colours[j].name << ": " << dist << " dB");
            REQUIRE(dist >= 6.0);
            minDistance = std::min(minDistance, dist);
        }
    }

    // Extremal margins against the other four.
    double roundMargin = 1.0e9;  // min over others of C, minus Round's C
    double nasalMargin = 1.0e9;  // min over others of sigma, minus Nasal's sigma
    double thickMargin = 1.0e9;  // Thick's R_body minus max over others
    double hollowMarginClipped = 1.0e9;
    const ProfundumTest::Descriptors& hollow = colours[kHollow].d;
    for (std::size_t k = 0; k < colours.size(); ++k) {
        const ProfundumTest::Descriptors& o = colours[k].d;
        INFO("other " << colours[k].name);
        if (k != kRound)
            roundMargin = std::min(roundMargin, o.centroidOct - colours[kRound].d.centroidOct);
        if (k != kNasal)
            nasalMargin = std::min(nasalMargin, o.spreadOct - colours[kNasal].d.spreadOct);
        if (k != kThick)
            thickMargin = std::min(thickMargin, colours[kThick].d.rBodyDb - o.rBodyDb);
        if (k != kHollow) {
            // Unclipped odd/even: Hollow must exceed every other by >= 1 dB. A +inf Hollow
            // (every even exactly 0) beats any finite value; a tie at +inf fails.
            REQUIRE_FALSE(ProfundumTest::isPosInf(o.oddEvenDb));
            if (!ProfundumTest::isPosInf(hollow.oddEvenDb)) {
                REQUIRE_FALSE(ProfundumTest::isNegInf(hollow.oddEvenDb));
                if (!ProfundumTest::isNegInf(o.oddEvenDb))
                    REQUIRE(hollow.oddEvenDb - o.oddEvenDb >= 1.0);
            }
            hollowMarginClipped = std::min(hollowMarginClipped, hollow.oddEvenClippedDb - o.oddEvenClippedDb);
        }
    }
    INFO("round C margin " << roundMargin << " nasal sigma margin " << nasalMargin << " thick R_body margin "
                           << thickMargin << " hollow clipped odd/even margin " << hollowMarginClipped);
    REQUIRE(roundMargin >= 0.05);
    REQUIRE(nasalMargin >= 0.05);
    REQUIRE(thickMargin >= 1.0);
    REQUIRE(hollowMarginClipped > 0.0);

    const std::string hollowText =
        ProfundumTest::isPosInf(hollow.oddEvenDb) ? std::string("+inf") : std::to_string(hollow.oddEvenDb);
    WARN("SC-019 min pairwise body-colour distance: " << minDistance << " dB (bar 6, model 16.24)");
    WARN("SC-019 kBodyRound lowest C by " << roundMargin << " oct (bar 0.05, model 0.375)");
    WARN("SC-019 kBodyHollow highest odd/even: unclipped " << hollowText << " dB, clipped margin "
                                                           << hollowMarginClipped << " dB (bar 1, model 18.9)");
    WARN("SC-019 kBodyNasal smallest sigma by " << nasalMargin << " oct (bar 0.05, model 0.064)");
    WARN("SC-019 kBodyThick highest R_body by " << thickMargin << " dB (bar 1, model 1.27)");
}

// ------------------------------------------------------------------------------
// FR-006 / SC-010(c) control arm: the documented control Lipschitz ceiling L_c,
// max|da_n|/dc <= kLipschitzControlCeiling * sqrt(P0), N = 96. Never raise the
// ceiling to pass (tasks.md T007 R-1 fallback narrows kBodyWidthNarrowOct instead).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_LipschitzBound", "[processors][profundum]") {
    constexpr int kN = 96;
    std::mt19937 rng{0x5EED};
    std::uniform_int_distribution<int> pickField(0, 5);
    std::uniform_real_distribution<float> unit01(0.0f, 1.0f);
    std::uniform_real_distribution<float> step(0.0f, 0.01f);  // [0, 0.01), mapped to (0, 0.01] below

    const double sqrtP0 = std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget));
    const double ceiling = static_cast<double>(SpectralShapeRecipe::kLipschitzControlCeiling);
    double worst = 0.0;
    int samples = 0;
    std::string worstState;

    for (int trial = 0; trial < 10000; ++trial) {
        Controls base{};
        for (const FieldSpec& spec : kFields)
            base.*spec.member = spec.lo + (spec.hi - spec.lo) * unit01(rng);
        const FieldSpec& f = kFields[static_cast<std::size_t>(pickField(rng))];
        const float delta = 0.01f - step(rng);  // (0, 0.01]

        Controls moved = base;
        const float up = base.*f.member + delta;
        moved.*f.member = up <= f.hi ? up : base.*f.member - delta;  // kept inside the range
        const double dc = std::abs(static_cast<double>(moved.*f.member) - static_cast<double>(base.*f.member));
        if (!(dc > 0.0))
            continue;  // float rounding swallowed the step
        ++samples;

        const std::vector<float> a = ProfundumTest::shapeOf(base, kN);
        const std::vector<float> b = ProfundumTest::shapeOf(moved, kN);
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double l = std::abs(static_cast<double>(b[i]) - static_cast<double>(a[i])) / dc / sqrtP0;
            if (l > worst) {
                worst = l;
                worstState = controlsText(base) + " field " + f.name + " dc " + std::to_string(dc) + " n " +
                             std::to_string(i + 1);
            }
        }
    }

    INFO("samples " << samples << " worst L_c " << worst << " sqrt(P0) at " << worstState);
    REQUIRE(samples >= 9990);
    REQUIRE(worst <= ceiling);
    WARN("FR-006 L_c worst: " << worst << " sqrt(P0) (ceiling " << ceiling << ", model 7.13) at " << worstState);

    // T008 f0 arm (SC-010(c), FR-006): f0 pairs at most 1 cent apart, N = 96, 3 rates;
    // max|da_n|/d(oct) <= kLipschitzOctaveCeiling * sqrt(P0). Thirds: straddling the guard
    // onset (30-35 Hz), inside the cap taper of some n, within 10 cents of the clamp ceiling.
    // The step is drawn from [0.5, 1] cent so the float f0 / log2 quantisation (~1e-4 cent)
    // stays far below the step it is divided by.
    SECTION("f0 arm: L_f <= kLipschitzOctaveCeiling") {
        std::mt19937 frng{0x5EED};
        std::uniform_real_distribution<double> unitD(0.0, 1.0);
        std::uniform_int_distribution<int> pickHarmonic(2, kN);
        const double octCeiling = static_cast<double>(SpectralShapeRecipe::kLipschitzOctaveCeiling);
        const double taperCents = static_cast<double>(SpectralShapeRecipe::kCapTaperCents);
        std::array<double, 3> worstByKind{};
        int pairs = 0;
        std::array<float, kN> a{};
        std::array<float, kN> b{};
        std::string worstF0State;
        double worstF0 = 0.0;

        for (int trial = 0; trial < 10000; ++trial) {
            const int kind = trial % 3;
            const double fs = kFullRates[static_cast<std::size_t>((trial / 3) % 3)];
            const Controls c = randomControls(frng);
            const double cap = static_cast<double>(SpectralShapeRecipe::capFrequency(fs));
            const double stepCents = 0.5 + 0.5 * unitD(frng);  // (0, 1] cent, here [0.5, 1]
            double f0a = 0.0;
            if (kind == 0) {
                f0a = 30.0 + (35.0 * std::pow(2.0, -1.0 / 1200.0) - 30.0) * unitD(frng);
            } else if (kind == 1) {
                const double n = static_cast<double>(pickHarmonic(frng));
                const double y = taperCents * unitD(frng);  // cents below the cap for harmonic n
                f0a = cap / n * std::pow(2.0, -y / 1200.0);
            } else {
                const double u = 1.0 + 9.0 * unitD(frng);  // 1..10 cents below the clamp ceiling
                f0a = cap * std::pow(2.0, -u / 1200.0);
            }
            const auto fa = static_cast<float>(f0a);
            const float fb = std::min(static_cast<float>(f0a * std::pow(2.0, stepCents / 1200.0)),
                                      static_cast<float>(cap));
            const double dOct = std::log2(static_cast<double>(fb) / static_cast<double>(fa));
            if (!(dOct > 0.0))
                continue;
            ++pairs;
            SpectralShapeRecipe::evaluate(c, fa, fs, a);
            SpectralShapeRecipe::evaluate(c, fb, fs, b);
            for (std::size_t i = 0; i < a.size(); ++i) {
                const double l = std::abs(static_cast<double>(b[i]) - static_cast<double>(a[i])) / dOct / sqrtP0;
                worstByKind[static_cast<std::size_t>(kind)] = std::max(worstByKind[static_cast<std::size_t>(kind)], l);
                if (l > worstF0) {
                    worstF0 = l;
                    worstF0State = controlsText(c) + " fs " + std::to_string(fs) + " f0 " + std::to_string(fa) +
                                   " -> " + std::to_string(fb) + " n " + std::to_string(i + 1);
                }
            }
        }
        INFO("pairs " << pairs << " worst L_f " << worstF0 << " sqrt(P0)/oct at " << worstF0State);
        REQUIRE(pairs >= 9990);
        REQUIRE(worstF0 <= octCeiling);
        WARN("FR-006 L_f worst: " << worstF0 << " sqrt(P0)/oct (ceiling " << octCeiling
                                  << ", model 4.78); onset " << worstByKind[0] << ", cap taper "
                                  << worstByKind[1] << ", clamp ceiling " << worstByKind[2] << " at "
                                  << worstF0State);
    }
}

// ==============================================================================
// T008: evaluateMask, applyMask, evaluate -- the plan S3.3 mask
//       (FR-030/031/032, FR-051 stages; plan S1 C-8)
// ==============================================================================

namespace {

/// evaluateMask of length n (pre-filled with -1 so an unwritten element shows).
std::vector<float> maskOf(float f0, double fs, int n) {
    std::vector<float> m(static_cast<std::size_t>(n), -1.0f);
    SpectralShapeRecipe::evaluateMask(f0, fs, m);
    return m;
}

/// shape x mask, renormalised (applyMask into a separate buffer).
std::vector<float> maskedOf(const std::vector<float>& shape, float f0, double fs) {
    const std::vector<float> m = maskOf(f0, fs, static_cast<int>(shape.size()));
    std::vector<float> out(shape.size(), -1.0f);
    SpectralShapeRecipe::applyMask(shape, m, out);
    return out;
}

/// h1's share of E_total, in double.
double h1Share(std::span<const float> a) {
    double total = 0.0;
    for (const float v : a)
        total += static_cast<double>(v) * static_cast<double>(v);
    const double a1 = static_cast<double>(a[0]);
    return a1 * a1 / total;
}

/// f0 from `hi` down to `lo` (while >= lo) in 1-cent steps; element 0 is (float)hi.
std::vector<float> centsDown(double hi, double lo) {
    std::vector<float> f;
    for (int k = 0;; ++k) {
        const double v = hi * std::pow(2.0, -static_cast<double>(k) / 1200.0);
        if (v < lo)
            break;
        f.push_back(static_cast<float>(v));
    }
    return f;
}

/// f0 from `lo` up to `hi` (while <= hi) in 1-cent steps; element 0 is (float)lo.
std::vector<float> centsUp(double lo, double hi) {
    std::vector<float> f;
    for (int k = 0;; ++k) {
        const double v = lo * std::pow(2.0, static_cast<double>(k) / 1200.0);
        if (v > hi)
            break;
        f.push_back(static_cast<float>(v));
    }
    return f;
}

/// Cap-taper start in Hz: capHz * 2^(-kCapTaperCents / 1200), in double.
double taperStartHz(double fs) {
    return static_cast<double>(SpectralShapeRecipe::capFrequency(fs)) *
           std::pow(2.0, -static_cast<double>(SpectralShapeRecipe::kCapTaperCents) / 1200.0);
}

/// The +44.28 dB corner (plan S1 C-8): the highest pre-mask n >= 2 level re h1.
constexpr Controls kCornerControls{.depth = 0.0f, .body = 1.0f, .bodyCurvature = 1.0f, .bodyEmphasis = 0.0f,
                                   .edge = 1.0f, .shift = 1.0f};

}  // namespace

// ------------------------------------------------------------------------------
// FR-030: the Low Note Guard is monotone. evaluateMask at 22.05 / 48 / 192 kHz,
// N = 96, f0 from 32.70 Hz down to 8 Hz in 1-cent steps.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_GuardMonotone", "[processors][profundum]") {
    constexpr int kN = 96;
    const std::vector<float> down = centsDown(32.70, 8.0);
    REQUIRE(down.front() == SpectralShapeRecipe::kLowNoteGuardOnsetHz);
    REQUIRE(down.size() > 2400);
    const std::vector<float> up = centsUp(32.70, 60.0);
    REQUIRE(up.front() == SpectralShapeRecipe::kLowNoteGuardOnsetHz);

    for (const double fs : {22050.0, 48000.0, 192000.0}) {
        INFO("fs " << fs);
        // Below the onset no harmonic reaches the cap taper (S3.3: n*f0 <= 3 139 Hz < 3 861 Hz).
        REQUIRE(static_cast<double>(kN) * 32.70 < taperStartHz(fs));

        int h1NotOne = 0;
        int notStrictlyFalling = 0;
        int increasingInN = 0;
        int outOfRange = 0;
        std::string firstFailure;
        std::vector<float> prev;
        for (const float f0 : down) {
            const std::vector<float> m = maskOf(f0, fs, kN);
            if (m[0] != 1.0f)
                ++h1NotOne;
            for (std::size_t i = 0; i < m.size(); ++i) {
                const bool inRange = m[i] >= 0.0f && m[i] <= 1.0f;  // NaN fails both, so it counts
                if (!inRange)
                    ++outOfRange;
                if (i >= 1 && m[i] > m[i - 1]) {
                    ++increasingInN;
                    if (firstFailure.empty())
                        firstFailure = "increasing in n at f0 " + std::to_string(f0) + " n " + std::to_string(i + 1);
                }
                if (i >= 1 && !prev.empty() && !(m[i] < prev[i])) {
                    ++notStrictlyFalling;
                    if (firstFailure.empty())
                        firstFailure = "not strictly decreasing at f0 " + std::to_string(f0) + " n " +
                                       std::to_string(i + 1) + ": " + std::to_string(m[i]) + " vs " +
                                       std::to_string(prev[i]);
                }
            }
            prev = m;
        }
        INFO("first failure: " << firstFailure);
        REQUIRE(h1NotOne == 0);
        REQUIRE(outOfRange == 0);
        REQUIRE(increasingInN == 0);
        REQUIRE(notStrictlyFalling == 0);

        // At and above the onset the guard is the identity. At 48 and 192 kHz no harmonic of
        // f0 <= 60 Hz (N = 96) reaches the cap taper, so every element is exactly 1. At 22.05 kHz
        // the taper starts at 3 861 Hz, so n * f0 above it (n >= 65 near 60 Hz) carries the cap
        // factor (plan S3.3 law): there the identity is asserted for the harmonics below the
        // taper start, and every element is still checked non-increasing in n.
        const double start = taperStartHz(fs);
        const bool capFree = static_cast<double>(kN) * 60.0 < start;
        if (fs != 22050.0)
            REQUIRE(capFree);
        int notIdentity = 0;
        int checked = 0;
        for (const float f0 : up) {
            const std::vector<float> m = maskOf(f0, fs, kN);
            for (std::size_t i = 0; i < m.size(); ++i) {
                if (i >= 1 && m[i] > m[i - 1])
                    ++increasingInN;
                const double nf = static_cast<double>(i + 1) * static_cast<double>(f0);
                if (nf >= start * (1.0 - 1.0e-5))
                    continue;
                ++checked;
                if (m[i] != 1.0f) {
                    ++notIdentity;
                    if (firstFailure.empty())
                        firstFailure =
                            "guard not identity at f0 " + std::to_string(f0) + " n " + std::to_string(i + 1);
                }
            }
        }
        INFO("first failure: " << firstFailure << " (checked " << checked << ")");
        REQUIRE(increasingInN == 0);
        REQUIRE(notIdentity == 0);
        if (capFree)
            REQUIRE(checked == static_cast<int>(up.size()) * kN);

        // h1's share of E_total after applyMask is non-decreasing as f0 falls.
        for (const Controls* c : {&SpectralShapeRecipe::kDefaultControls, &SpectralShapeRecipe::kSawAnchor}) {
            const std::vector<float> shape = ProfundumTest::shapeOf(*c, kN);
            double prevShare = h1Share(maskedOf(shape, down.front(), fs));
            int shareFell = 0;
            std::string shareFailure;
            for (std::size_t k = 1; k < down.size(); ++k) {
                const double share = h1Share(maskedOf(shape, down[k], fs));
                if (share < prevShare) {
                    ++shareFell;
                    if (shareFailure.empty())
                        shareFailure = "share fell at f0 " + std::to_string(down[k]) + ": " + std::to_string(share) +
                                       " < " + std::to_string(prevShare);
                }
                prevShare = share;
            }
            INFO(controlsText(*c) << " " << shareFailure);
            REQUIRE(shareFell == 0);
        }
    }
}

// ------------------------------------------------------------------------------
// SC-011(b)-(d): low-note guard continuity (plan S1 C-8).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_LowNoteGuardContinuity", "[processors][profundum]") {
    const double p0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);
    const double sqrtP0 = std::sqrt(p0);

    SECTION("(b) at and above the onset the full vector equals the onset vector") {
        const Controls mid = ProfundumTest::midGrid();
        const std::vector<float> ref = ProfundumTest::fullOf(mid, 32.70f, 48000.0, 64);
        for (const float f0 : {32.70f, 40.0f, 65.41f, 130.81f}) {
            const std::vector<float> a = ProfundumTest::fullOf(mid, f0, 48000.0, 64);
            for (std::size_t i = 0; i < a.size(); ++i) {
                INFO("f0 " << f0 << " n " << (i + 1) << " a " << a[i] << " ref " << ref[i]);
                REQUIRE(std::abs(static_cast<double>(a[i]) - static_cast<double>(ref[i])) <=
                        1.0e-6 * std::abs(static_cast<double>(ref[i])));
            }
        }
    }

    SECTION("(c) 1-cent continuity over MIDI 0-127 (plan S1 C-8)") {
        constexpr int kN = 96;
        // The 6 named coordinates, the 5 colours, mid grid and the +44.28 dB corner (the
        // recipe-model.js SC-011(c) state list).
        const std::vector<Controls> states{
            SpectralShapeRecipe::kSineAnchor, SpectralShapeRecipe::kTriangleAnchor, SpectralShapeRecipe::kSawAnchor,
            SpectralShapeRecipe::kHeavy,      SpectralShapeRecipe::kHollow,         SpectralShapeRecipe::kGrowl,
            SpectralShapeRecipe::kBodyRound,  SpectralShapeRecipe::kBodyHollow,     SpectralShapeRecipe::kBodyWoody,
            SpectralShapeRecipe::kBodyNasal,  SpectralShapeRecipe::kBodyThick,      ProfundumTest::midGrid(),
            kCornerControls,
        };
        REQUIRE(states.size() == 13);
        const double dbRatioBar = std::pow(10.0, 0.1 / 20.0);  // |20 log10(a'/a)| <= 0.1 dB
        const double absBound = 8.0 * sqrtP0 / 1200.0;
        double worstRatio = 1.0;  // max over max(a'/a, a/a') where both >= -80 dB re a1
        double worstAbs = 0.0;    // max |da| / absBound elsewhere
        std::string worstDbState;
        std::vector<float> prev(kN);
        std::vector<float> cur(kN);
        std::vector<float> mask(kN);
        for (const Controls& c : states) {
            const std::vector<float> shape = ProfundumTest::shapeOf(c, kN);
            for (const double fs : kFullRates) {
                const float ceiling = SpectralShapeRecipe::capFrequency(fs);
                float prevF0 = 0.0f;
                for (int ct = 0; ct <= 12700; ++ct) {
                    const double midi = static_cast<double>(ct) / 100.0;
                    const float f0 =
                        std::min(static_cast<float>(440.0 * std::pow(2.0, (midi - 69.0) / 12.0)), ceiling);
                    // evaluate is literally this composition (SC-021(a)); the shape is hoisted.
                    SpectralShapeRecipe::evaluateMask(f0, fs, mask);
                    SpectralShapeRecipe::applyMask(shape, mask, cur);
                    if (ct > 0 && f0 > prevF0) {
                        const double a1 = static_cast<double>(cur[0]);
                        const double b1 = static_cast<double>(prev[0]);
                        for (std::size_t i = 0; i < cur.size(); ++i) {
                            const double a = static_cast<double>(cur[i]);
                            const double b = static_cast<double>(prev[i]);
                            if (a >= 1.0e-4 * a1 && b >= 1.0e-4 * b1) {  // both >= -80 dB re a1
                                const double r = a >= b ? a / b : b / a;
                                if (r > worstRatio) {
                                    worstRatio = r;
                                    worstDbState = controlsText(c) + " fs " + std::to_string(fs) + " f0 " +
                                                   std::to_string(f0) + " n " + std::to_string(i + 1);
                                }
                            } else {
                                worstAbs = std::max(worstAbs, std::abs(a - b) / absBound);
                            }
                        }
                    }
                    std::swap(prev, cur);
                    prevF0 = f0;
                }
            }
        }
        const double worstDb = 20.0 * std::log10(worstRatio);
        INFO("worst " << worstDb << " dB at " << worstDbState << "; worst |da| " << worstAbs << " x bound");
        REQUIRE(worstRatio <= dbRatioBar);
        REQUIRE(worstAbs <= 1.0);
        WARN("SC-011(c) worst 1-cent change: " << worstDb << " dB (bar 0.1, model 0.0900) at " << worstDbState
                                               << "; below -80 dB: " << worstAbs
                                               << " x (8 sqrt(P0)/1200) (bar 1, model 0.001)");
    }

    SECTION("(d) h1's share below the onset is >= its share at the onset") {
        for (const Controls* c : {&SpectralShapeRecipe::kDefaultControls, &SpectralShapeRecipe::kSawAnchor}) {
            const double atOnset = h1Share(ProfundumTest::fullOf(*c, 32.70f, 48000.0, 96));
            for (const float f0 : {8.0f, 12.0f, 16.35f, 24.0f, 32.0f}) {
                const double share = h1Share(ProfundumTest::fullOf(*c, f0, 48000.0, 96));
                INFO(controlsText(*c) << " f0 " << f0 << " share " << share << " at onset " << atOnset);
                REQUIRE(share >= atOnset);
            }
        }
    }
}

// ------------------------------------------------------------------------------
// FR-032, SC-016 ceiling clause: the cap taper is exact (plan S1 C-8).
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_CapTaperExact", "[processors][profundum]") {
    constexpr int kN = 96;
    const double p0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);
    constexpr std::array<double, 5> kRates{22050.0, 44100.0, 48000.0, 96000.0, 192000.0};

    SECTION("n*f0 >= capHz: mask is exactly 0") {
        for (const double fs : kRates) {
            const float capHz = SpectralShapeRecipe::capFrequency(fs);
            int atOrAbove = 0;
            int notZero = 0;
            std::string firstFailure;
            // Log-spaced f0 over [8, capHz], plus every f0 = capHz / n.
            std::vector<float> f0s;
            for (int k = 0; k <= 2000; ++k)
                f0s.push_back(static_cast<float>(8.0 * std::pow(static_cast<double>(capHz) / 8.0,
                                                                static_cast<double>(k) / 2000.0)));
            for (int n = 2; n <= kN; ++n)
                f0s.push_back(capHz / static_cast<float>(n));
            for (const float f0 : f0s) {
                const std::vector<float> m = maskOf(f0, fs, kN);
                for (std::size_t i = 1; i < m.size(); ++i) {
                    if (static_cast<float>(i + 1) * f0 >= capHz) {
                        ++atOrAbove;
                        if (m[i] != 0.0f) {
                            ++notZero;
                            if (firstFailure.empty())
                                firstFailure = "f0 " + std::to_string(f0) + " n " + std::to_string(i + 1) +
                                               " mask " + std::to_string(m[i]);
                        }
                    }
                }
            }
            INFO("fs " << fs << " " << firstFailure);
            REQUIRE(atOrAbove > 0);
            REQUIRE(notZero == 0);
        }
    }

    SECTION("f0 = capFrequency(fs): evaluate is h1-only with a1^2 = P0") {
        for (const double fs : kRates) {
            const float capHz = SpectralShapeRecipe::capFrequency(fs);
            for (const Controls& c : {SpectralShapeRecipe::kDefaultControls, SpectralShapeRecipe::kGrowl,
                                      SpectralShapeRecipe::kSineAnchor, kCornerControls}) {
                INFO("fs " << fs << " " << controlsText(c));
                const std::vector<float> a = ProfundumTest::fullOf(c, capHz, fs, kN);
                for (std::size_t i = 1; i < a.size(); ++i) {
                    INFO("n " << (i + 1));
                    REQUIRE(a[i] == 0.0f);
                }
                const double a1 = static_cast<double>(a[0]);
                REQUIRE(std::abs(a1 * a1 - p0) / p0 <= 1.0e-4);
            }
        }
    }

    SECTION("taper start = capHz * 2^(-1430/1200); 1 cent below it the mask is 1") {
        REQUIRE(taperStartHz(48000.0) == Approx(8406.0).margin(1.0));
        const double oneStepDown =
            std::pow(10.0, -static_cast<double>(SpectralShapeRecipe::kCapTaperDbPerCent) / 20.0);
        for (const double fs : kRates) {
            const double start = taperStartHz(fs);
            for (const int n : {2, 7, 16, 64, 96}) {
                INFO("fs " << fs << " n " << n << " taper start " << start);
                const auto idx = static_cast<std::size_t>(n - 1);
                const auto nd = static_cast<double>(n);
                // 1 cent below the start; f0 is above the guard onset here (start / 96 >= 40 Hz),
                // so the mask is exactly 1.
                const auto below = static_cast<float>(start * std::pow(2.0, -1.0 / 1200.0) / nd);
                REQUIRE(below > SpectralShapeRecipe::kLowNoteGuardOnsetHz);
                REQUIRE(maskOf(below, fs, kN)[idx] == 1.0f);
                // 1 cent above the start: inside the dB-linear part, kCapTaperDbPerCent down.
                const auto above = static_cast<float>(start * std::pow(2.0, 1.0 / 1200.0) / nd);
                const float mAbove = maskOf(above, fs, kN)[idx];
                REQUIRE(mAbove < 1.0f);
                REQUIRE(static_cast<double>(mAbove) == Approx(oneStepDown).epsilon(1.0e-3));
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-021(a): evaluate is literally evaluateShape -> evaluateMask -> applyMask.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_ShapeMaskComposition", "[processors][profundum]") {
    const std::vector<FullSample> set = fullSampleSet();
    REQUIRE(set.size() == 30000);
    std::array<float, 96> shape{};
    std::array<float, 96> mask{};
    std::array<float, 96> composed{};
    std::array<float, 96> full{};
    int mismatches = 0;
    std::string firstFailure;
    for (const FullSample& s : set) {
        SpectralShapeRecipe::evaluateShape(s.c, shape);
        SpectralShapeRecipe::evaluateMask(s.f0, s.fs, mask);
        SpectralShapeRecipe::applyMask(shape, mask, composed);
        SpectralShapeRecipe::evaluate(s.c, s.f0, s.fs, full);
        for (std::size_t i = 0; i < full.size(); ++i) {
            const double x = static_cast<double>(composed[i]);
            const double y = static_cast<double>(full[i]);
            if (std::abs(x - y) > std::max(1.0e-6 * std::abs(y), 1.0e-12)) {
                ++mismatches;
                if (firstFailure.empty())
                    firstFailure = controlsText(s.c) + " f0 " + std::to_string(s.f0) + " fs " +
                                   std::to_string(s.fs) + " n " + std::to_string(i + 1);
            }
        }
    }
    INFO(firstFailure);
    REQUIRE(mismatches == 0);

    // out may alias shape (applyMask contract).
    SpectralShapeRecipe::evaluateShape(SpectralShapeRecipe::kGrowl, shape);
    SpectralShapeRecipe::evaluateMask(12.0f, 44100.0, mask);
    SpectralShapeRecipe::applyMask(shape, mask, composed);
    SpectralShapeRecipe::applyMask(shape, mask, shape);
    REQUIRE(ProfundumTest::samplesBitEqual(shape.data(), composed.data(), shape.size()));
}

// ------------------------------------------------------------------------------
// SC-007 (recipe arm): the named distributions own their regions, 48 kHz, N = 64.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_NamedDistributions", "[processors][profundum]") {
    struct Named {
        const char* name;
        const Controls* c;
        std::array<std::size_t, 3> order;  // region indices, dominant first: 0 = sub, 1 = body, 2 = pres
    };
    const std::array<Named, 3> named{{
        {"kHeavy", &SpectralShapeRecipe::kHeavy, {0, 1, 2}},
        {"kHollow", &SpectralShapeRecipe::kHollow, {1, 0, 2}},
        {"kGrowl", &SpectralShapeRecipe::kGrowl, {2, 1, 0}},
    }};
    for (const Named& nm : named) {
        double minMargin = 1.0e9;
        for (const float f0 : {32.70f, 65.41f, 130.81f}) {
            const ProfundumTest::Descriptors d =
                ProfundumTest::describe(ProfundumTest::fullOf(*nm.c, f0, 48000.0, 64));
            const std::array<double, 3> e{d.eSub, d.eBody, d.ePres};
            const double first = e[nm.order[0]];
            const double second = e[nm.order[1]];
            const double third = e[nm.order[2]];
            INFO(nm.name << " f0 " << f0 << " E_sub " << d.eSub << " E_body " << d.eBody << " E_pres " << d.ePres);
            REQUIRE(first > second);
            REQUIRE(second > third);
            const double dominantDb = 10.0 * std::log10(first / second);
            REQUIRE(dominantDb >= 3.0);
            minMargin = std::min({minMargin, dominantDb, 10.0 * std::log10(second / third)});
        }
        WARN("SC-007 " << nm.name << " min region margin over C1/C2/C3: " << minMargin
                       << " dB (bar 3, model 7.5 / 25.7 / 12.8 for kHeavy / kHollow / kGrowl)");
    }
}

// ------------------------------------------------------------------------------
// SC-015 (recipe arm): the four entry points never allocate and are noexcept.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_NoAllocation", "[processors][profundum]") {
    static_assert(noexcept(SpectralShapeRecipe::evaluateShape(std::declval<const Controls&>(),
                                                              std::declval<std::span<float>>())));
    static_assert(noexcept(SpectralShapeRecipe::evaluateMask(0.0f, 0.0, std::declval<std::span<float>>())));
    static_assert(noexcept(SpectralShapeRecipe::applyMask(std::declval<std::span<const float>>(),
                                                          std::declval<std::span<const float>>(),
                                                          std::declval<std::span<float>>())));
    static_assert(noexcept(SpectralShapeRecipe::evaluate(std::declval<const Controls&>(), 0.0f, 0.0,
                                                         std::declval<std::span<float>>())));

    std::array<float, 96> shape{};
    std::array<float, 96> mask{};
    std::array<float, 96> out{};
    std::array<float, 96> full{};
    Controls c = SpectralShapeRecipe::kDefaultControls;
    float sink = 0.0f;

    auto& detector = TestHelpers::AllocationDetector::instance();
    detector.startTracking();
    for (int i = 0; i < 1000; ++i) {
        c.shift = -1.0f + 2.0f * static_cast<float>(i) / 999.0f;
        const float f0 = 8.0f + 20.0f * static_cast<float>(i);  // 8 Hz .. ~20 kHz: guard, plain and cap
        SpectralShapeRecipe::evaluateShape(c, shape);
        SpectralShapeRecipe::evaluateMask(f0, 48000.0, mask);
        SpectralShapeRecipe::applyMask(shape, mask, out);
        SpectralShapeRecipe::evaluate(c, f0, 48000.0, full);
        sink += out[1] + full[1];
    }
    const std::size_t allocations = detector.stopTracking();
    INFO("sink " << sink);
    REQUIRE(allocations == 0);
}

// ------------------------------------------------------------------------------
// Plan S11 step 2: the C++ recipe matches the calibration model.
// full(c, 65.41, 48000, 64) from recipe-model.js, printed with 9 significant
// digits, for mid grid, kGrowl and the +44.28 dB corner {0, 1, 1, 0, 1, 1}.
// This is a tolerance (1e-5 * sqrt(P0) per element), not a golden.
// ------------------------------------------------------------------------------
TEST_CASE("SpectralShapeRecipe_MatchesCalibrationModel", "[processors][profundum]") {
    // values from recipe-model.js; regenerate after ANY constant change
    constexpr std::array<double, 64> kModelMidGrid{{
        0.268814585,   0.104785663,   0.356644870,   0.167911827,   0.0330246172,  0.00989422257, 0.00808394647,
        0.0119135398,  0.0197320967,  0.0291276845,  0.0368160011,  0.0405089742,  0.0397793629,  0.0356590776,
        0.0310630187,  0.0273014813,  0.0241840111,  0.0215715408,  0.0193606072,  0.0174729480,  0.0158484789,
        0.0144404529,  0.0132120590,  0.0121339917,  0.0111826867,  0.0103390225,  0.00958735145, 0.00891476939,
        0.00831055791, 0.00776575467, 0.00727281915, 0.00682537032, 0.00641797907, 0.00604600277, 0.00570545241,
        0.00539288519, 0.00510531717, 0.00484015180, 0.00459512111, 0.00436823700, 0.00415775087, 0.00396211973,
        0.00377997794, 0.00361011323, 0.00345144652, 0.00330301475, 0.00316395618, 0.00303349792, 0.00291094511,
        0.00279567168, 0.00268711234, 0.00258475562, 0.00248813784, 0.00239683786, 0.00231047246, 0.00222869235,
        0.00215117858, 0.00207763948, 0.00200780787, 0.00194143867, 0.00187830669, 0.00181820479, 0.00176094210,
        0.00170634258,
    }};
    // values from recipe-model.js; regenerate after ANY constant change
    constexpr std::array<double, 64> kModelGrowl{{
        0.00627715600, 0.00381108438, 0.0167645935,  0.0484863201,  0.0655982828,  0.0580077650, 0.0402600658,
        0.0242604578,  0.0135782683,  0.00772242552, 0.00560791810, 0.00678315153, 0.0113956553, 0.0196956918,
        0.0315383688,  0.0461307385,  0.0620887173,  0.0777222679,  0.0913986133,  0.101846778,  0.108327556,
        0.110658809,   0.109130459,   0.104881131,   0.100685746,   0.0968131458,  0.0932274364, 0.0898978655,
        0.0867979288,  0.0839046589,  0.0811980541,  0.0786606132,  0.0762769574,  0.0740335170, 0.0719182734,
        0.0699205434,  0.0680307989,  0.0662405147,  0.0645420399,  0.0629284889,  0.0613936477, 0.0599318942,
        0.0585381292,  0.0572077172,  0.0559364346,  0.0547204251,  0.0535561607,  0.0524404074, 0.0513701950,
        0.0503427911,  0.0493556775,  0.0484065299,  0.0474931991,  0.0466136955,  0.0457661737, 0.0449489206,
        0.0441603431,  0.0433989578,  0.0426633823,  0.0419523259,  0.0412645829,  0.0405990251, 0.0399545961,
        0.0393303055,
    }};
    // values from recipe-model.js; regenerate after ANY constant change
    constexpr std::array<double, 64> kModelCorner{{
        0.00233450577, 0.00116725289,  0.000778176035, 0.00318283015,  0.266324091,   0.382183283,   0.0474154127,
        0.00159784817, 0.000316958873, 0.000431613082, 0.000873839437, 0.00193325366, 0.00395452353, 0.00718910377,
        0.0116642190,  0.0171249893,   0.0230759995,   0.0288979512,   0.0339879973,  0.0378755433,  0.0402866703,
        0.0411541163,  0.0405859361,   0.0390057006,   0.0374454726,   0.0360052621,  0.0346717339,  0.0334334577,
        0.0322805798,  0.0312045605,   0.0301979618,   0.0292542755,   0.0283677823,  0.0275334357,  0.0267467661,
        0.0260038004,  0.0253009950,   0.0246351793,   0.0240035081,   0.0234034204,  0.0228326052,  0.0222889718,
        0.0217706236,  0.0212758367,   0.0208030403,   0.0203508003,   0.0199178046,  0.0195028503,  0.0191048330,
        0.0187227363,  0.0183556238,   0.0180026311,   0.0176629588,   0.0173358669,  0.0170206694,  0.0167167288,
        0.0164234529,  0.0161402899,   0.0158667257,   0.0156022802,   0.0153465052,  0.0150989809,  0.0148593145,
        0.0146271377,
    }};

    const double tolerance = 1.0e-5 * std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget));
    struct Case {
        const char* name;
        Controls c;
        const std::array<double, 64>* model;
    };
    const std::array<Case, 3> cases{{
        {"mid grid", ProfundumTest::midGrid(), &kModelMidGrid},
        {"kGrowl", SpectralShapeRecipe::kGrowl, &kModelGrowl},
        {"+44.28 dB corner", kCornerControls, &kModelCorner},
    }};
    double worst = 0.0;
    for (const Case& k : cases) {
        const std::vector<float> a = ProfundumTest::fullOf(k.c, 65.41f, 48000.0, 64);
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double err = std::abs(static_cast<double>(a[i]) - (*k.model)[i]);
            INFO(k.name << " n " << (i + 1) << " cpp " << a[i] << " model " << (*k.model)[i] << " err " << err);
            REQUIRE(err <= tolerance);
            worst = std::max(worst, err);
        }
    }
    WARN("Calibration-model match: worst |a_cpp - a_model| " << worst << " (tolerance " << tolerance << ")");
}
