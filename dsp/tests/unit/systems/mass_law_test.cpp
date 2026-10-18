// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// MassLaw law level: SC-001(a), SC-004, SC-016 law arm, SC-023 law, SC-025 law; FR-020 - FR-030, FR-049
// ==============================================================================
// tasks.md T009 (plan S7, C-7 (i) RULED 2026-10-17, C-9, C-11, C-12, S8.2 seed). Tags
// [systems][profundum][mass-law]. No [long] case in this TU.
//
// This TU is compiled in the shipping FP mode (not -fno-fast-math): non-finite arguments are built
// from bit patterns through a volatile sink, never from std::numeric_limits, and are never compared.
// Every bit-exact comparison is between two evaluations in this binary (no stored float goldens).
// ==============================================================================

// allocation_detector.h ONLY: the global operator new/delete replacements are included from exactly
// one TU of dsp_systems_tests (selectable_oscillator_test.cpp).
#include <allocation_detector.h>

#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/mass_law.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace Krate::DSP;

namespace {

using Targets = MassLaw::Targets;
using Target = MassLaw::Target;
constexpr std::size_t kCI = MassLaw::kControlInterval;

/// A float from its bit pattern through a volatile sink (fast-math safe).
float floatFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t read = sink;
    return std::bit_cast<float>(read);
}
float quietNaN() noexcept { return floatFromBits(0x7FC00000u); }
float posInfF() noexcept { return floatFromBits(0x7F800000u); }
float negInfF() noexcept { return floatFromBits(0xFF800000u); }

bool bitEqual(float a, float b) noexcept { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); }
bool bitEqual(const Targets& a, const Targets& b) noexcept {
    return bitEqual(a.depth, b.depth) && bitEqual(a.body, b.body) &&
           bitEqual(a.transientDurationMs, b.transientDurationMs) && bitEqual(a.foldAmount, b.foldAmount) &&
           bitEqual(a.compressionAmount, b.compressionAmount) && bitEqual(a.anchorLevel, b.anchorLevel) &&
           bitEqual(a.outputTrimDb, b.outputTrimDb);
}

/// Test-side field access in Target order (independent of the header's private accessor).
float& fieldAt(Targets& t, std::size_t i) noexcept {
    switch (i) {
        case 0: return t.depth;
        case 1: return t.body;
        case 2: return t.transientDurationMs;
        case 3: return t.foldAmount;
        case 4: return t.compressionAmount;
        case 5: return t.anchorLevel;
        default: return t.outputTrimDb;
    }
}
float fieldAt(const Targets& t, std::size_t i) noexcept {
    Targets copy = t;
    return fieldAt(copy, i);
}

constexpr std::array<const char*, MassLaw::kNumTargets> kFieldNames{
    {"depth", "body", "transientDurationMs", "foldAmount", "compressionAmount", "anchorLevel", "outputTrimDb"}};

/// D-2 directions: Depth falls as Mass rises, and so does the FR-021 output trim (non-increasing, G8-5
/// ruling 2026-10-17); the other five rise.
constexpr bool fallsWithMass(std::size_t i) noexcept {
    return i == static_cast<std::size_t>(Target::Depth) || i == static_cast<std::size_t>(Target::OutputTrim);
}

/// The three fixture bases of SC-001(a) / SC-023: the stored reference (Targets{}), kSawAnchor and
/// kSineAnchor (their recipe depth / body; the non-recipe fields at the Targets{} defaults).
struct NamedBase {
    const char* name;
    Targets base;
};
std::array<NamedBase, 3> fixtureBases() {
    Targets saw{};
    saw.depth = SpectralShapeRecipe::kSawAnchor.depth;
    saw.body = SpectralShapeRecipe::kSawAnchor.body;
    Targets sine{};
    sine.depth = SpectralShapeRecipe::kSineAnchor.depth;
    sine.body = SpectralShapeRecipe::kSineAnchor.body;
    return {{{"reference", Targets{}}, {"kSawAnchor", saw}, {"kSineAnchor", sine}}};
}

/// Drive a MassLaw from host blocks of `block` samples over `totalSamples`, calling advanceInterval()
/// at every 32-sample boundary of an absolute sample counter (plan C-11). Records currentTargets()
/// before the first interval and after each one, and the smoothed Mass after each interval.
struct GridRun {
    std::vector<Targets> targets;   // [0] = before the first step
    std::vector<float> mass;        // [k] = smoothed Mass after step k + 1
};
GridRun gridDriver(MassLaw& law, std::size_t block, std::size_t totalSamples) {
    GridRun run;
    run.targets.push_back(law.currentTargets());
    std::size_t n = 0;
    while (n < totalSamples) {
        const std::size_t len = std::min(block, totalSamples - n);
        for (std::size_t i = 0; i < len; ++i) {
            if ((n + i) % kCI == 0) {
                run.mass.push_back(law.advanceInterval());
                run.targets.push_back(law.currentTargets());
            }
        }
        n += len;
    }
    return run;
}

/// The SC-004 fixture: prepared at fs, Mass 0 snapped, then a step to 100.
void prepareStep(MassLaw& law, double fs, const Targets& base) {
    law.prepare(fs);
    law.setBase(base);
    law.setMass(0.0f);
    law.reset();
    law.setMass(100.0f);
}

/// Number of advanceInterval() calls until the smoothed Mass first equals 100 (capped).
std::size_t stepsToTarget(MassLaw& law) {
    std::size_t steps = 0;
    while (law.smoothedMass() != MassLaw::kMaxMass && steps < 100000) {
        (void)law.advanceInterval();
        ++steps;
    }
    return steps;
}

std::size_t expectedSweepSteps(double fs) {
    return static_cast<std::size_t>(std::ceil(static_cast<double>(MassLaw::kMassSweepMs) * 0.001 * fs /
                                              static_cast<double>(kCI)));
}

} // namespace

// =============================================================================
// 1. Table invariants (FR-021, FR-022, FR-024, FR-030, SC-025 law): runtime mirror of the
//    compile-time checks in mass_law.h
// =============================================================================
TEST_CASE("MassLaw_TableInvariants", "[systems][profundum][mass-law]") {
    static_assert(MassLaw::kNumTargets == 7, "FR-021 as amended (G8-5 ruling 2026-10-17): six targets plus the output trim");
    static_assert(sizeof(MassLaw::Targets) == 7 * sizeof(float),
                  "FR-021 / SC-025: Targets holds the seven targets only (no attack / sustain field)");
    static_assert(MassLaw::kControlInterval == 32, "FR-049");
    static_assert(MassLaw::kMassSweepMs == 40.0f && MassLaw::kSlewBoundRefMs == 20.0f, "C-7 (i) RULED 2026-10-17");

    // FR-030: exactly one row writes the Anchor level.
    constexpr std::size_t kAnchorRows = [] {
        std::size_t n = 0;
        for (const auto& r : MassLaw::kRows) {
            if (r.target == MassLaw::Target::AnchorLevel) ++n;
        }
        return n;
    }();
    static_assert(kAnchorRows == 1, "FR-030: the Anchor level is the only Anchor lever, one row");

    REQUIRE(MassLaw::kRows.size() == MassLaw::kNumTargets);
    for (std::size_t i = 0; i < MassLaw::kRows.size(); ++i) {
        const auto& r = MassLaw::kRows[i];
        INFO("row " << i << " (" << kFieldNames[i] << ")");
        CHECK(static_cast<std::size_t>(r.target) == i);
        CHECK(r.lo < r.hi);
        CHECK(r.amountUp * r.amountDown >= 0.0f);
        if (fallsWithMass(i)) {
            CHECK(r.amountUp <= 0.0f);
            CHECK(r.amountDown <= 0.0f);
        } else {
            CHECK(r.amountUp >= 0.0f);
            CHECK(r.amountDown >= 0.0f);
        }
        CHECK(r.curve != ModCurve::Stepped);
        double slope = 1.5;   // SCurve
        if (r.curve == ModCurve::Linear)
            slope = 1.0;
        else if (r.curve == ModCurve::Exponential)
            slope = 2.0;
        CHECK(static_cast<double>(MassLaw::maxSlope(r.curve)) == slope);
        const double a = std::max(std::fabs(static_cast<double>(r.amountUp)), std::fabs(static_cast<double>(r.amountDown)));
        const double cap = static_cast<double>(r.hi - r.lo) * static_cast<double>(MassLaw::kMassSweepMs) /
                           (2.0 * static_cast<double>(MassLaw::kSlewBoundRefMs));
        std::printf("MassLaw_TableInvariants: %-20s lo %g hi %g down %g up %g  max|a|*slope %g <= cap %g\n",
                    kFieldNames[i], static_cast<double>(r.lo), static_cast<double>(r.hi),
                    static_cast<double>(r.amountDown), static_cast<double>(r.amountUp), a * slope, cap);
        CHECK(a * slope <= cap);
    }
    const auto& dur = MassLaw::kRows[static_cast<std::size_t>(Target::TransientDuration)];
    CHECK(dur.lo == TransientShaper::kMinDurationMs);
    CHECK(dur.hi == TransientShaper::kMaxDurationMs);

    // Targets{} defaults are the components' own defaults.
    const Targets d{};
    CHECK(d.anchorLevel == AnchorOscillator::kDefaultLevel);
    CHECK(d.depth == SpectralShapeRecipe::kDefaultControls.depth);
    CHECK(d.body == SpectralShapeRecipe::kDefaultControls.body);
    CHECK(d.transientDurationMs == TransientShaper::kDefaultDurationMs);
    CHECK(d.foldAmount == 0.0f);
    CHECK(d.compressionAmount == 0.0f);
}

// =============================================================================
// 2. SC-001(a): every law output is monotone in its D-2 direction (non-strict; clamped runs recorded)
// =============================================================================
TEST_CASE("MassLaw_TargetsMonotonic", "[systems][profundum][mass-law]") {
    constexpr int kPoints = 1001;
    for (const auto& nb : fixtureBases()) {
        std::vector<Targets> out(kPoints);
        for (int k = 0; k < kPoints; ++k) out[static_cast<std::size_t>(k)] = MassLaw::evaluate(static_cast<float>(k) / 10.0f, nb.base);

        for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
            const auto& row = MassLaw::kRows[f];
            int violations = 0;
            int clampedRuns = 0;
            bool inRun = false;
            for (std::size_t k = 0; k < out.size(); ++k) {
                const float v = fieldAt(out[k], f);
                const bool atEdge = (v == row.lo || v == row.hi);
                if (k > 0) {
                    const float prev = fieldAt(out[k - 1], f);
                    if (fallsWithMass(f) ? (v > prev) : (v < prev)) ++violations;
                    const bool clampedStep = atEdge && v == prev;
                    if (clampedStep && !inRun) ++clampedRuns;
                    inRun = clampedStep;
                }
            }
            std::printf("MassLaw_TargetsMonotonic: %-11s %-20s Mass0 %g Mass50 %g Mass100 %g  clamped runs %d\n", nb.name,
                        kFieldNames[f], static_cast<double>(fieldAt(out.front(), f)),
                        static_cast<double>(fieldAt(out[500], f)), static_cast<double>(fieldAt(out.back(), f)), clampedRuns);
            INFO(nb.name << " " << kFieldNames[f]);
            CHECK(violations == 0);
        }
    }
}

// =============================================================================
// 3. SC-023 law arm (FR-027): Mass 50 returns the base bit-exactly; edge clamping recorded
// =============================================================================
TEST_CASE("MassLaw_NeutralAtFifty", "[systems][profundum][mass-law]") {
    for (const auto& nb : fixtureBases()) {
        INFO(nb.name);
        CHECK(bitEqual(MassLaw::evaluate(50.0f, nb.base), nb.base));
    }

    // 100 seeded in-range bases (test-side seed 6006; raw mt19937 words, portable across stdlibs).
    std::mt19937 rng(6006u);
    int mismatches = 0;
    for (int i = 0; i < 100; ++i) {
        Targets b{};
        for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
            const auto& row = MassLaw::kRows[f];
            const float u = static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
            fieldAt(b, f) = std::clamp(row.lo + u * (row.hi - row.lo), row.lo, row.hi);
        }
        if (!bitEqual(MassLaw::evaluate(50.0f, b), b)) ++mismatches;
    }
    std::printf("MassLaw_NeutralAtFifty: seeded bases mismatching at Mass 50: %d / 100\n", mismatches);
    CHECK(mismatches == 0);

    // Edge bases: a base at a range end with a pushing bias clamps (recorded and asserted in range).
    Targets low{};
    Targets high{};
    for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
        fieldAt(low, f) = MassLaw::kRows[f].lo;
        fieldAt(high, f) = MassLaw::kRows[f].hi;
    }
    Targets mixed{};   // depth 1 (pushed up at Mass 0), fold 100 (pushed up at Mass 100)
    mixed.depth = 1.0f;
    mixed.foldAmount = 100.0f;
    const std::array<NamedBase, 3> edges{{{"all-lo", low}, {"all-hi", high}, {"depth1-fold100", mixed}}};
    for (const auto& nb : edges) {
        INFO(nb.name);
        CHECK(bitEqual(MassLaw::evaluate(50.0f, nb.base), nb.base));
        const Targets e0 = MassLaw::evaluate(0.0f, nb.base);
        const Targets e100 = MassLaw::evaluate(100.0f, nb.base);
        for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
            const auto& row = MassLaw::kRows[f];
            std::printf("MassLaw_NeutralAtFifty: edge %-15s %-20s base %g  Mass0 %g  Mass100 %g\n", nb.name, kFieldNames[f],
                        static_cast<double>(fieldAt(nb.base, f)), static_cast<double>(fieldAt(e0, f)),
                        static_cast<double>(fieldAt(e100, f)));
            CHECK(fieldAt(e0, f) >= row.lo);
            CHECK(fieldAt(e0, f) <= row.hi);
            CHECK(fieldAt(e100, f) >= row.lo);
            CHECK(fieldAt(e100, f) <= row.hi);
        }
    }
    // The two pushing clamps named in tasks.md T009.
    CHECK(MassLaw::evaluate(0.0f, mixed).depth == 1.0f);
    CHECK(MassLaw::evaluate(100.0f, mixed).foldAmount == 100.0f);
    CHECK(MassLaw::evaluate(100.0f, mixed).depth == 0.5f);
    CHECK(MassLaw::evaluate(0.0f, mixed).foldAmount == 50.0f);
}

// =============================================================================
// 4. SC-004 (FR-025, FR-049; C-7 (i) RULED, deviation 12): bounded per-interval slew, 40 ms traversal,
//    partition invariance
// =============================================================================
TEST_CASE("MassLaw_SlewBounded", "[systems][profundum][mass-law]") {
    constexpr std::array<double, 3> kRates{{48000.0, 44100.0, 96000.0}};
    constexpr std::array<std::size_t, 4> kBlocks{{1, 17, 64, 512}};
    for (const double fs : kRates) {
        const std::size_t expected = expectedSweepSteps(fs);
        for (const auto& nb : fixtureBases()) {
            std::vector<Targets> reference;
            for (const std::size_t block : kBlocks) {
                INFO("fs " << fs << " base " << nb.name << " block " << block);
                MassLaw law;
                prepareStep(law, fs, nb.base);
                REQUIRE(law.smoothedMass() == 0.0f);
                const std::size_t total = (expected + 8) * kCI;
                const GridRun run = gridDriver(law, block, total);

                // Traversal: intervals until the smoothed Mass first equals 100.
                std::size_t steps = 0;
                while (steps < run.mass.size() && run.mass[steps] != MassLaw::kMaxMass) ++steps;
                ++steps;   // count of steps including the one that reached 100
                CHECK(steps + 1 >= expected);
                CHECK(steps <= expected + 1);

                // Per-interval bound: |dTarget| <= range * (32 / fs) / (kSlewBoundRefMs * 1e-3) + 1e-6 * range.
                for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
                    const auto& row = MassLaw::kRows[f];
                    const double range = static_cast<double>(row.hi - row.lo);
                    const double bar = range * (static_cast<double>(kCI) / fs) /
                                       (static_cast<double>(MassLaw::kSlewBoundRefMs) * 0.001);
                    double maxStep = 0.0;
                    for (std::size_t k = 1; k < run.targets.size(); ++k) {
                        const double d = std::fabs(static_cast<double>(fieldAt(run.targets[k], f)) -
                                                   static_cast<double>(fieldAt(run.targets[k - 1], f)));
                        maxStep = std::max(maxStep, d);
                    }
                    if (block == kBlocks.front()) {
                        std::printf("MassLaw_SlewBounded: fs %g %-11s %-20s max step %.9g bar %.9g excess %.3g "
                                    "(allowance %.3g) steps %zu (expected %zu)\n",
                                    fs, nb.name, kFieldNames[f], maxStep, bar, maxStep - bar, 1e-6 * range, steps, expected);
                    }
                    INFO(kFieldNames[f]);
                    CHECK(maxStep <= bar + 1e-6 * range);
                }
                // The end state is the Mass 100 evaluation.
                CHECK(bitEqual(run.targets.back(), MassLaw::evaluate(100.0f, law.base())));

                if (reference.empty()) {
                    reference = run.targets;
                } else {
                    REQUIRE(run.targets.size() == reference.size());
                    CHECK(std::memcmp(run.targets.data(), reference.data(), reference.size() * sizeof(Targets)) == 0);
                }
            }
        }
    }
}

// =============================================================================
// 5. FR-026: pure evaluation
// =============================================================================
TEST_CASE("MassLaw_PureEvaluation", "[systems][profundum][mass-law]") {
    MassLaw a;
    a.prepare(48000.0);
    a.setMass(10.0f);
    for (int i = 0; i < 7; ++i) (void)a.advanceInterval();
    MassLaw b;   // unprepared, different pending Mass and base
    b.setMass(90.0f);
    Targets bBase{};
    bBase.foldAmount = 33.0f;
    b.setBase(bBase);

    const std::array<float, 9> masses{{0.0f, 0.1f, 12.5f, 49.9f, 50.0f, 50.1f, 77.7f, 99.9f, 100.0f}};
    for (const auto& nb : fixtureBases()) {
        for (const float m : masses) {
            INFO(nb.name << " mass " << m);
            const Targets t1 = MassLaw::evaluate(m, nb.base);
            const Targets t2 = MassLaw::evaluate(m, nb.base);
            const Targets ta = decltype(a)::evaluate(m, nb.base);   // through each instance's type
            const Targets tb = decltype(b)::evaluate(m, nb.base);
            CHECK(bitEqual(t1, t2));
            CHECK(bitEqual(t1, ta));
            CHECK(bitEqual(t1, tb));
            for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
                CHECK(bitEqual(fieldAt(t1, f), MassLaw::evaluateRow(MassLaw::kRows[f], m, fieldAt(nb.base, f))));
            }
        }
    }
    // The stateful view is the pure function of its state.
    CHECK(bitEqual(a.currentTargets(), MassLaw::evaluate(a.smoothedMass(), a.base())));
    CHECK(bitEqual(b.currentTargets(), MassLaw::evaluate(b.smoothedMass(), b.base())));
}

// =============================================================================
// 6. FR-029: input safety
// =============================================================================
TEST_CASE("MassLaw_InputSafety", "[systems][profundum][mass-law]") {
    const float nan = quietNaN();
    const float pinf = posInfF();
    const float ninf = negInfF();

    MassLaw law;
    law.prepare(48000.0);
    law.setMass(30.0f);
    CHECK(law.pendingMass() == 30.0f);
    law.setMass(nan);
    CHECK(law.pendingMass() == 30.0f);
    law.setMass(pinf);
    CHECK(law.pendingMass() == 30.0f);
    law.setMass(ninf);
    CHECK(law.pendingMass() == 30.0f);
    law.setMass(-5.0f);
    CHECK(law.pendingMass() == 0.0f);
    law.setMass(150.0f);
    CHECK(law.pendingMass() == 100.0f);
    law.reset();
    CHECK(law.smoothedMass() == 100.0f);

    // setBase: one non-finite field keeps that field, the others update; out-of-range fields clamp.
    const std::array<float, 3> bad{{nan, pinf, ninf}};
    for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
        for (const float x : bad) {
            MassLaw l;
            const Targets before = l.base();
            Targets in{};
            for (std::size_t g = 0; g < MassLaw::kNumTargets; ++g) {
                const auto& row = MassLaw::kRows[g];
                fieldAt(in, g) = row.lo + 0.25f * (row.hi - row.lo);
            }
            fieldAt(in, f) = x;
            l.setBase(in);
            INFO("field " << kFieldNames[f]);
            CHECK(bitEqual(fieldAt(l.base(), f), fieldAt(before, f)));
            for (std::size_t g = 0; g < MassLaw::kNumTargets; ++g) {
                if (g != f) {
                    CHECK(bitEqual(fieldAt(l.base(), g), fieldAt(in, g)));
                }
            }
        }
    }
    {
        MassLaw l;
        Targets lo{};
        Targets hi{};
        for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
            const auto& row = MassLaw::kRows[f];
            fieldAt(lo, f) = row.lo - 1000.0f;
            fieldAt(hi, f) = row.hi + 1000.0f;
        }
        l.setBase(lo);
        for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) CHECK(fieldAt(l.base(), f) == MassLaw::kRows[f].lo);
        l.setBase(hi);
        for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) CHECK(fieldAt(l.base(), f) == MassLaw::kRows[f].hi);
    }

    // evaluateRow: non-finite mass == the Mass 50 value; non-finite base uses the Targets{} default.
    const Targets defaults{};
    const std::array<float, 5> masses{{0.0f, 25.0f, 50.0f, 75.0f, 100.0f}};
    for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
        const auto& row = MassLaw::kRows[f];
        const float base = row.lo + 0.4f * (row.hi - row.lo);
        INFO("row " << kFieldNames[f]);
        for (const float x : bad) {
            CHECK(bitEqual(MassLaw::evaluateRow(row, x, base), MassLaw::evaluateRow(row, 50.0f, base)));
            for (const float m : masses) {
                CHECK(bitEqual(MassLaw::evaluateRow(row, m, x), MassLaw::evaluateRow(row, m, fieldAt(defaults, f))));
            }
        }
        // Out-of-range finite Mass clamps.
        CHECK(bitEqual(MassLaw::evaluateRow(row, -5.0f, base), MassLaw::evaluateRow(row, 0.0f, base)));
        CHECK(bitEqual(MassLaw::evaluateRow(row, 150.0f, base), MassLaw::evaluateRow(row, 100.0f, base)));
    }
}

// =============================================================================
// 7. Lifecycle (FR-047 law arm)
// =============================================================================
TEST_CASE("MassLaw_Lifecycle", "[systems][profundum][mass-law]") {
    MassLaw law;
    // Before prepare: advanceInterval returns 50 and changes nothing (recorded decision, mass_law.h).
    CHECK(law.smoothedMass() == MassLaw::kNeutralMass);
    CHECK(law.pendingMass() == MassLaw::kNeutralMass);
    law.setMass(80.0f);
    const Targets before = law.currentTargets();
    for (int i = 0; i < 10; ++i) CHECK(law.advanceInterval() == MassLaw::kNeutralMass);
    CHECK(law.smoothedMass() == MassLaw::kNeutralMass);
    CHECK(law.pendingMass() == 80.0f);
    CHECK(bitEqual(law.currentTargets(), before));
    // reset() snaps to the pending Mass.
    law.reset();
    CHECK(law.smoothedMass() == 80.0f);

    // After prepare(96000) a full sweep takes 120 intervals (+/- 1).
    MassLaw l96;
    prepareStep(l96, 96000.0, Targets{});
    const std::size_t steps = stepsToTarget(l96);
    std::printf("MassLaw_Lifecycle: 96 kHz full sweep %zu intervals (expected %zu)\n", steps, expectedSweepSteps(96000.0));
    CHECK(expectedSweepSteps(96000.0) == 120);
    CHECK(steps + 1 >= 120);
    CHECK(steps <= 121);
    // Once there it stays.
    CHECK(l96.advanceInterval() == MassLaw::kMaxMass);

    // prepare() snaps to the pending Mass (recorded decision): re-preparing mid-sweep restarts settled.
    MassLaw l;
    prepareStep(l, 48000.0, Targets{});
    for (int i = 0; i < 5; ++i) (void)l.advanceInterval();
    l.prepare(44100.0);
    CHECK(l.smoothedMass() == 100.0f);
}

// =============================================================================
// 8. SC-016 law arm: no allocation
// =============================================================================
TEST_CASE("MassLaw_NoAllocation", "[systems][profundum][mass-law]") {
    auto& detector = TestHelpers::AllocationDetector::instance();

    // Positive control: the scope sees a real allocation in this binary.
    std::size_t liveness = 0;
    int probeObserved = 0;
    {
        TestHelpers::AllocationScope scope;
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        int* probe = new int[16];
        probe[0] = 42;
        volatile int* probeSink = probe;   // defeat new/delete elision
        probeObserved = probeSink[0];
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete[] probe;
        liveness = detector.getAllocationCount();
    }
    REQUIRE(probeObserved == 42);
    REQUIRE(liveness >= std::size_t{1});

    const float nan = quietNaN();
    const float pinf = posInfF();
    MassLaw law;
    law.prepare(48000.0);   // control thread, outside the window
    Targets base{};
    base.foldAmount = 20.0f;
    Targets badBase = base;
    badBase.body = nan;

    std::size_t count = 0;
    float sink = 0.0f;
    {
        TestHelpers::AllocationScope scope;
        law.setMass(0.0f);
        law.setMass(nan);
        law.setMass(pinf);
        law.setBase(base);
        law.setBase(badBase);
        law.reset();
        law.setMass(100.0f);   // a Mass step
        for (int i = 0; i < 70; ++i) sink += law.advanceInterval();
        const Targets t = law.currentTargets();
        sink += t.depth;
        sink += MassLaw::evaluate(25.0f, base).foldAmount;
        sink += MassLaw::evaluate(nan, badBase).body;
        sink += MassLaw::evaluateRow(MassLaw::kRows[0], 75.0f, 0.5f);
        law.reset();
        count = detector.getAllocationCount();
    }
    std::printf("MassLaw_NoAllocation: %zu allocation(s) (positive control %zu), sink %g\n", count, liveness,
                static_cast<double>(sink));
    REQUIRE(count == 0);
}
