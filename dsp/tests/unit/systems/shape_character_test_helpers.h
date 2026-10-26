// ==============================================================================
// Test helpers: Shape & Character systems-level helpers
//                          (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// tasks.md T008 (plan S9.2; C-2, C-3; OQ-S). Test-side only: allocation is fine here.
// Read-only after T008.
//
// Adds, on top of the processors-level metric helpers (T006,
// ../processors/shape_character_metric_helpers.h: differentialClicks, steadyDeltaMax,
// d11Ratio, centroidOct, inharmonicFractionDb, levelDb, whiteNoise):
//   - note constants (tasks.md rule 8) and the four spectral fixtures (rule 9)
//   - Descriptors / describe()        (spec "Descriptor definitions")
//   - Separation / separate()         (OQ-S floor, mirrors shape-character-model.js:371-377)
//   - amountMoved()                   (SC-007: the same rule over THD, I and R_oe only)
//   - levelMatch(), fixture(), fold100()
//   - ShsCandidates / shsCandidates() (C-2 ambiguity rule, C-3 readsF0)
//   - centsBetween()
//
// Every helper is an `inline` function in namespace Krate::DSP::ShapeCharacterTest
// (never an anonymous namespace in a header). HarmonicFoldTest and ProfundumTest define
// same-named functions, so every call into them is qualified (tasks.md rule 5).
// ==============================================================================

#pragma once

#include "../processors/shape_character_metric_helpers.h"
#include "../processors/harmonic_fold_test_helpers.h"
#include "anchor_oscillator_test_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/harmonic_fold.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate::DSP::ShapeCharacterTest {

// -----------------------------------------------------------------------------
// Notes (tasks.md rule 8): 440·2^((m − 69)/12) in double
// -----------------------------------------------------------------------------

inline double noteHz(int midi) { return 440.0 * std::exp2((static_cast<double>(midi) - 69.0) / 12.0); }

inline const double kE0Hz = noteHz(16);   // 20.60172
inline const double kE1Hz = noteHz(28);   // 41.20344
inline const double kE2Hz = noteHz(40);   // 82.40689
inline const double kC1Hz = noteHz(24);   // 32.70320
inline const double kC2Hz = noteHz(36);   // 65.40639
inline const double kC3Hz = noteHz(48);   // 130.81278

// -----------------------------------------------------------------------------
// Fixtures (tasks.md rule 9)
// -----------------------------------------------------------------------------

struct Fixture {
    const char* name;
    SpectralShapeRecipe::Controls controls;
};

inline const std::array<Fixture, 4> kFixtures{{
    {.name = "sine", .controls = SpectralShapeRecipe::kSineAnchor},
    {.name = "triangle", .controls = SpectralShapeRecipe::kTriangleAnchor},
    {.name = "saw", .controls = SpectralShapeRecipe::kSawAnchor},
    {.name = "growl", .controls = SpectralShapeRecipe::kGrowl},
}};

/// The core-equivalent steady render of recipe state `c` (= HarmonicFoldTest::recipeAdditive).
inline std::vector<float> fixture(const SpectralShapeRecipe::Controls& c, double f0, double fs, double seconds) {
    return HarmonicFoldTest::recipeAdditive(c, f0, fs, seconds);
}

/// Fold-100 input (rule 9): a fresh HarmonicFold, prepare(fs, 512), setAmount(100),
/// setFundamentalHz(f0), renderFold(fold, in, 512).
inline std::vector<float> fold100(const std::vector<float>& in, double fs, double f0) {
    HarmonicFold fold;
    fold.prepare(fs, 512);
    fold.setAmount(100.0f);
    fold.setFundamentalHz(static_cast<float>(f0));
    return HarmonicFoldTest::renderFold(fold, in, 512);
}

// -----------------------------------------------------------------------------
// Descriptors (spec "Descriptor definitions")
// -----------------------------------------------------------------------------

/// All power ratios in dB, UNCLAMPED (the OQ-S floor is applied by separate / amountMoved).
struct Descriptors {
    double L;       ///< level, RMS dBFS over integerPeriodLength (levelDb)
    double Elow;    ///< Σ_{n=2..8} w / Σ w
    double Ehigh;   ///< Σ_{n≥9} w / Σ w
    double RoeNum;  ///< Σ_{odd n≥3} w / Σ w (R_oe numerator, re total)
    double RoeDen;  ///< Σ_{even n≥2} w / Σ w (R_oe denominator, re total)
    double Thd;     ///< Σ_{n≥2} w / w_1
    double I;       ///< inharmonic fraction (inharmonicFractionDb)
    double C;       ///< centroid, octaves above f0 (centroidOct)
    std::vector<double> w;   ///< w_n, n = 1.. (index n − 1); 0 for n·f0 ≥ fs/2
};

/// Descriptors of y[0..n): w_n from ProfundumTest::renderedHarmonicPowers on the last
/// TestUtils::kLowFrequencyFftSize samples; I, C, L from the T006 helpers on their own frames.
inline Descriptors describe(const float* y, std::size_t n, double fs, double f0) {
    REQUIRE(y != nullptr);
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);
    constexpr std::size_t kFrame = TestUtils::kLowFrequencyFftSize;
    REQUIRE(n >= kFrame);

    const int N = static_cast<int>(std::ceil(0.5 * fs / f0));
    Descriptors d{};
    d.w = ProfundumTest::renderedHarmonicPowers(y + (n - kFrame), fs, f0, N);

    double total = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    double odd = 0.0;
    double even = 0.0;
    for (std::size_t i = 0; i < d.w.size(); ++i) {
        const std::size_t h = i + 1;
        const double p = d.w[i];
        total += p;
        if (h == 1)
            continue;
        if (h <= 8)
            lo += p;
        else
            hi += p;
        if ((h & 1u) != 0u)
            odd += p;
        else
            even += p;
    }
    const double w1 = d.w.empty() ? 0.0 : d.w[0];
    REQUIRE(total > 0.0);

    d.Elow = HarmonicFoldTest::safeDb10(lo / total);
    d.Ehigh = HarmonicFoldTest::safeDb10(hi / total);
    d.RoeNum = HarmonicFoldTest::safeDb10(odd / total);
    d.RoeDen = HarmonicFoldTest::safeDb10(even / total);
    d.Thd = HarmonicFoldTest::safeDb10((lo + hi) / std::max(w1, 1e-300));
    d.I = inharmonicFractionDb(y, n, fs, f0);
    d.C = centroidOct(y, n, fs, f0);
    d.L = levelDb(y, n, fs, f0);
    return d;
}

// -----------------------------------------------------------------------------
// OQ-S separation (mirrors shape-character-model.js:371-377 exactly)
// -----------------------------------------------------------------------------

inline constexpr double kFloorClampDb = -60.0;    ///< values clamped to ≥ −60 dB before differencing
inline constexpr double kFloorCountDb = -40.0;    ///< counts only if one side is above −40 dB
inline constexpr double kSeparationDb = 3.0;      ///< OQ-S bar
inline constexpr double kSeparationOct = 0.25;    ///< OQ-S centroid bar

struct Separation {
    double maxDb;        ///< largest qualifying floored dB difference (0 if none counts)
    const char* which;   ///< "Elow", "Ehigh", "I", "Roe", "THD" or "" (none counted)
    double dC;           ///< |C(a) − C(b)|, octaves
    bool distinct;       ///< maxDb ≥ 3 or dC ≥ 0.25
};

inline double floorClamp(double v) { return std::max(v, kFloorClampDb); }
inline bool floorCounts(double u, double v) { return u > kFloorCountDb || v > kFloorCountDb; }

inline Separation separate(const Descriptors& a, const Descriptors& b) {
    double best = 0.0;
    const char* why = "";
    const auto consider = [&](double u, double v, const char* name) {
        if (!floorCounts(u, v))
            return;
        const double d = std::abs(floorClamp(u) - floorClamp(v));
        if (d > best) {
            best = d;
            why = name;
        }
    };
    consider(a.Elow, b.Elow, "Elow");
    consider(a.Ehigh, b.Ehigh, "Ehigh");
    consider(a.I, b.I, "I");
    if (floorCounts(a.RoeNum, b.RoeNum) || floorCounts(a.RoeDen, b.RoeDen)) {
        const double d = std::abs((floorClamp(a.RoeNum) - floorClamp(a.RoeDen)) -
                                  (floorClamp(b.RoeNum) - floorClamp(b.RoeDen)));
        if (d > best) {
            best = d;
            why = "Roe";
        }
    }
    consider(a.Thd, b.Thd, "THD");
    const double dC = std::abs(a.C - b.C);
    return Separation{.maxDb = best, .which = why, .dC = dC,
                      .distinct = best >= kSeparationDb || dC >= kSeparationOct};
}

/// SC-007 Amount movement: the OQ-S rule over THD, I and R_oe only; returns the largest
/// qualifying floored dB move (0 if none counts).
inline double amountMoved(const Descriptors& a, const Descriptors& b) {
    double mv = 0.0;
    if (floorCounts(a.Thd, b.Thd))
        mv = std::max(mv, std::abs(floorClamp(a.Thd) - floorClamp(b.Thd)));
    if (floorCounts(a.I, b.I))
        mv = std::max(mv, std::abs(floorClamp(a.I) - floorClamp(b.I)));
    if (floorCounts(a.RoeNum, b.RoeNum) || floorCounts(a.RoeDen, b.RoeDen))
        mv = std::max(mv, std::abs((floorClamp(a.RoeNum) - floorClamp(a.RoeDen)) -
                                   (floorClamp(b.RoeNum) - floorClamp(b.RoeDen))));
    return mv;
}

/// Scales y in place so levelDb(y) equals targetDb.
inline void levelMatch(std::vector<float>& y, double targetDb, double fs, double f0) {
    REQUIRE(!y.empty());
    const double current = levelDb(y.data(), y.size(), fs, f0);
    REQUIRE(Krate::DSP::detail::isFinite(current));
    const double g = std::pow(10.0, (targetDb - current) / 20.0);
    for (float& v : y)
        v = static_cast<float>(static_cast<double>(v) * g);
}

// -----------------------------------------------------------------------------
// SHS candidates (plan S9.2; C-2, C-3)
// -----------------------------------------------------------------------------

inline double centsBetween(double a, double b) { return 1200.0 * std::log2(a / b); }

inline constexpr double kAmbiguityDb = 1.0;   ///< plan S1 C-2: best − second < 1 dB is ambiguous

struct ShsCandidates {
    double fHat;               ///< ProfundumTest::anchorShsF0(x, fs, f0)
    double sHalfDb;            ///< S(f̂/2) re the best candidate, dB (20·log10)
    double sOneDb;             ///< S(f̂) re the best candidate, dB
    double sDoubleDb;          ///< S(2f̂) re the best candidate, dB
    double bestMinusSecondDb;  ///< best − second-best candidate salience, dB (≥ 0)
    bool ambiguous;            ///< bestMinusSecondDb < 1 dB (C-2)
    bool readsF0;              ///< !ambiguous && |1200·log2(f̂/f0)| ≤ 1 cent (C-3)
};

/// x must hold TestUtils::kLowFrequencyFftSize (262144) samples (the analysis frame).
/// The saliences use the stage-2 score of ProfundumTest::anchorShsF0
/// (anchor_oscillator_test_helpers.h, "Stage 2: exact Hann-windowed DTFT refinement"),
/// re-implemented with the same constants: Σ_{h=1}^{15} 0.84^{h−1}·|X(h·f)| on the exact
/// Hann-windowed DTFT, harmonics at or above fs/2 dropped. Each candidate's salience is maximised
/// by golden-section search over ±2 cents (to 1e-4 cent).
inline ShsCandidates shsCandidates(const float* x, double fs, double f0) {
    constexpr std::size_t kFrame = TestUtils::kLowFrequencyFftSize;
    constexpr int kHarmonics = 15;
    constexpr double kCompression = 0.84;
    constexpr double kSearchOct = 2.0 / 1200.0;    // ±2 cents
    constexpr double kRefineTol = 1e-4 / 1200.0;   // octaves
    constexpr double kInvPhi = 0.6180339887498949;
    REQUIRE(x != nullptr);
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);

    const double fHat = ProfundumTest::anchorShsF0(x, fs, f0);
    REQUIRE(fHat > 0.0);
    const double nyquist = 0.5 * fs;

    std::vector<double> xw(kFrame, 0.0);
    for (std::size_t i = 0; i < kFrame; ++i) {
        const double w = 0.5 - 0.5 * std::cos(HarmonicFoldTest::kTwoPi * static_cast<double>(i) /
                                              static_cast<double>(kFrame));
        xw[i] = static_cast<double>(x[i]) * w;
    }
    const auto exactScore = [&](double f) {
        double s = 0.0;
        double w = 1.0;
        for (int h = 1; h <= kHarmonics; ++h) {
            const double hz = static_cast<double>(h) * f;
            if (hz >= nyquist)
                break;
            const double om = HarmonicFoldTest::kTwoPi * hz / fs;
            const double cr = std::cos(om);
            const double ci = -std::sin(om);
            double pr = 1.0;
            double pi = 0.0;
            double re = 0.0;
            double im = 0.0;
            for (std::size_t i = 0; i < kFrame; ++i) {
                re += xw[i] * pr;
                im += xw[i] * pi;
                if (((i + 1) & 1023u) == 0) {   // re-anchor the rotating phasor every 1024 samples
                    const double ang = -om * static_cast<double>(i + 1);
                    pr = std::cos(ang);
                    pi = std::sin(ang);
                } else {
                    const double t = pr * cr - pi * ci;
                    pi = pr * ci + pi * cr;
                    pr = t;
                }
            }
            s += w * std::sqrt(re * re + im * im);
            w *= kCompression;
        }
        return s;
    };
    const auto peakSalience = [&](double fc) {
        const double centre = std::log2(fc);
        double a = centre - kSearchOct;
        double b = centre + kSearchOct;
        double c = b - kInvPhi * (b - a);
        double d = a + kInvPhi * (b - a);
        double sc = exactScore(std::exp2(c));
        double sd = exactScore(std::exp2(d));
        double bestSeen = std::max(sc, sd);
        while (b - a > kRefineTol) {
            if (sc > sd) {
                b = d;
                d = c;
                sd = sc;
                c = b - kInvPhi * (b - a);
                sc = exactScore(std::exp2(c));
                bestSeen = std::max(bestSeen, sc);
            } else {
                a = c;
                c = d;
                sc = sd;
                d = a + kInvPhi * (b - a);
                sd = exactScore(std::exp2(d));
                bestSeen = std::max(bestSeen, sd);
            }
        }
        return bestSeen;
    };

    const std::array<double, 3> s{peakSalience(0.5 * fHat), peakSalience(fHat), peakSalience(2.0 * fHat)};
    const double best = std::max({s[0], s[1], s[2]});
    REQUIRE(best > 0.0);
    std::array<double, 3> db{};
    for (std::size_t i = 0; i < 3; ++i)
        db[i] = 20.0 * std::log10(std::max(s[i], 1e-300) / best);
    std::array<double, 3> sorted = db;
    std::sort(sorted.begin(), sorted.end());   // ascending: sorted[2] == 0 (best)
    const double gap = sorted[2] - sorted[1];

    ShsCandidates r{};
    r.fHat = fHat;
    r.sHalfDb = db[0];
    r.sOneDb = db[1];
    r.sDoubleDb = db[2];
    r.bestMinusSecondDb = gap;
    r.ambiguous = gap < kAmbiguityDb;
    r.readsF0 = !r.ambiguous && std::abs(centsBetween(fHat, f0)) <= 1.0;
    return r;
}

}  // namespace Krate::DSP::ShapeCharacterTest
