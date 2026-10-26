// ==============================================================================
// Test helpers: SpectralShapeRecipe vector descriptors
//                                    (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// Plan S8.2 "Vector descriptors"; definitions exactly per spec.md
// "Descriptor definitions". Test-side only: allocation is fine here.
//
// Every helper is an `inline` function in namespace Krate::DSP::ProfundumTest
// (never an anonymous namespace in a header: unused-function warnings would
// break the zero-warning rule; tasks.md rule 4).
//
// Infinities. This header is included by TUs compiled under the shipping
// -ffast-math / /fp:fast mode (the Recipe TU is NOT in the -fno-fast-math
// list). Under -ffinite-math-only a std::numeric_limits<>::infinity() literal
// is UB (-Wnan-infinity-disabled) and a comparison against it may fold to
// poison. So the +/-inf a descriptor returns is built from its IEEE-754 bit
// pattern through a volatile sink, and it is classified only through
// detail::opaqueDoubleBits (core/db_utils.h), the barrier-hardened bit read.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Krate::DSP::ProfundumTest {

inline constexpr std::uint64_t kPosInfBits = 0x7FF0000000000000ULL;
inline constexpr std::uint64_t kNegInfBits = 0xFFF0000000000000ULL;

/// Odd/even clip range for distance axes (spec: [-30, +30] dB).
inline constexpr double kOddEvenClipDb = 30.0;

/// A double built from its bit pattern through a volatile sink, so a real bit
/// pattern exists at runtime whatever the FP mode.
inline double doubleFromBits(std::uint64_t bits) noexcept {
    volatile std::uint64_t sink = bits;
    const std::uint64_t read = sink;
    return std::bit_cast<double>(read);
}

inline double posInf() noexcept { return doubleFromBits(kPosInfBits); }
inline double negInf() noexcept { return doubleFromBits(kNegInfBits); }

/// Fast-math-immune classification (barrier read, no FP comparison).
inline bool isPosInf(double x) noexcept { return detail::opaqueDoubleBits(x) == kPosInfBits; }
inline bool isNegInf(double x) noexcept { return detail::opaqueDoubleBits(x) == kNegInfBits; }

/// True when the first n floats of a and b have identical bit patterns (a
/// per-element compare, not memcmp: float has no unique object representation).
inline bool samplesBitEqual(const float* a, const float* b, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i]))
            return false;
    }
    return true;
}

/// 10*log10(num/den) for non-negative energies. den == 0 -> +inf; num == 0 (den > 0) -> -inf.
inline double energyRatioDb(double num, double den) noexcept {
    if (den <= 0.0)
        return posInf();
    if (num <= 0.0)
        return negInf();
    return 10.0 * std::log10(num / den);
}

struct Descriptors {
    double eSub;              ///< w1
    double eBody;             ///< sum w_n, n = 2..8
    double ePres;             ///< sum w_n, n >= 9
    double eTotal;            ///< sum w_n
    double rBodyDb;           ///< E_body / E_total (dB)
    double rPresDb;           ///< E_pres / E_total (dB)
    double h1RestDb;          ///< w1 / sum_{n>=2} w_n (dB); +inf when rest == 0
    double centroidOct;       ///< C = sum log2(n) w_n / sum w_n
    double spreadOct;         ///< energy-weighted standard deviation of log2(n)
    double oddEvenDb;         ///< sum w_n (odd n >= 3) / sum w_n (even n) (dB); +inf / -inf at the ends
    double oddEvenClippedDb;  ///< oddEvenDb clamped to [-30, +30]
};

/// Descriptors from harmonic power weights w_n (index 0 is harmonic n = 1).
inline Descriptors describePowers(std::span<const double> w) {
    Descriptors d{};
    double rest = 0.0;
    double oddHigh = 0.0;  // odd n >= 3
    double even = 0.0;
    double weightedLog = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) {
        const std::size_t n = i + 1;
        const double wn = w[i];
        d.eTotal += wn;
        if (n == 1)
            d.eSub += wn;
        else
            rest += wn;
        if (n >= 2 && n <= 8)
            d.eBody += wn;
        if (n >= 9)
            d.ePres += wn;
        if (n % 2 == 0)
            even += wn;
        else if (n >= 3)
            oddHigh += wn;
        weightedLog += std::log2(static_cast<double>(n)) * wn;
    }

    d.rBodyDb = energyRatioDb(d.eBody, d.eTotal);
    d.rPresDb = energyRatioDb(d.ePres, d.eTotal);
    d.h1RestDb = energyRatioDb(d.eSub, rest);

    if (d.eTotal > 0.0) {
        d.centroidOct = weightedLog / d.eTotal;
        double var = 0.0;
        for (std::size_t i = 0; i < w.size(); ++i) {
            const double dev = std::log2(static_cast<double>(i + 1)) - d.centroidOct;
            var += dev * dev * w[i];
        }
        d.spreadOct = std::sqrt(var / d.eTotal);
    }

    d.oddEvenDb = energyRatioDb(oddHigh, even);
    if (isPosInf(d.oddEvenDb))
        d.oddEvenClippedDb = kOddEvenClipDb;
    else if (isNegInf(d.oddEvenDb))
        d.oddEvenClippedDb = -kOddEvenClipDb;
    else
        d.oddEvenClippedDb = std::clamp(d.oddEvenDb, -kOddEvenClipDb, kOddEvenClipDb);
    return d;
}

/// Descriptors from a gain vector a (w_n = a_n^2, computed in double).
inline Descriptors describe(std::span<const float> a) {
    std::vector<double> w(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double an = static_cast<double>(a[i]);
        w[i] = an * an;
    }
    return describePowers(w);
}

/// Normalised descriptor distance: Euclidean over (R_body dB, R_pres dB, 6*C, 6*sigma).
inline double descriptorDistance(const Descriptors& x, const Descriptors& y) {
    const double dBody = x.rBodyDb - y.rBodyDb;
    const double dPres = x.rPresDb - y.rPresDb;
    const double dC = 6.0 * (x.centroidOct - y.centroidOct);
    const double dS = 6.0 * (x.spreadOct - y.spreadOct);
    return std::sqrt(dBody * dBody + dPres * dPres + dC * dC + dS * dS);
}

/// Body-colour distance: descriptorDistance plus the clipped odd/even axis (dB).
inline double bodyColourDistance(const Descriptors& x, const Descriptors& y) {
    const double base = descriptorDistance(x, y);
    const double dOE = x.oddEvenClippedDb - y.oddEvenClippedDb;
    return std::sqrt(base * base + dOE * dOE);
}

/// Stage-1 shape vector (evaluateShape) of length N.
inline std::vector<float> shapeOf(const SpectralShapeRecipe::Controls& c, int N) {
    std::vector<float> out(static_cast<std::size_t>(N), 0.0f);
    SpectralShapeRecipe::evaluateShape(c, out);
    return out;
}

/// Full evaluation (shape -> mask -> renormalise) of length N.
inline std::vector<float> fullOf(const SpectralShapeRecipe::Controls& c, float f0, double fs, int N) {
    std::vector<float> out(static_cast<std::size_t>(N), 0.0f);
    SpectralShapeRecipe::evaluate(c, f0, fs, out);
    return out;
}

/// 27-point grid: {0, 0.5, 1} for depth, body and edge; {-1, 0, 1} for shift;
/// curvature 0.5, emphasis 0. Indices are 0..2.
inline SpectralShapeRecipe::Controls gridPoint(int iD, int iB, int iE, int iS) {
    constexpr std::array<float, 3> kUnit{0.0f, 0.5f, 1.0f};
    constexpr std::array<float, 3> kSigned{-1.0f, 0.0f, 1.0f};
    return SpectralShapeRecipe::Controls{.depth = kUnit[static_cast<std::size_t>(iD)],
                                         .body = kUnit[static_cast<std::size_t>(iB)],
                                         .bodyCurvature = 0.5f,
                                         .bodyEmphasis = 0.0f,
                                         .edge = kUnit[static_cast<std::size_t>(iE)],
                                         .shift = kSigned[static_cast<std::size_t>(iS)]};
}

/// The grid centre: {0.5, 0.5, 0.5, 0, 0.5, 0} (== kDefaultControls).
inline SpectralShapeRecipe::Controls midGrid() { return gridPoint(1, 1, 1, 1); }

}  // namespace Krate::DSP::ProfundumTest
