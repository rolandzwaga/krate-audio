// ==============================================================================
// Test helpers: HarmonicFold render and measurement helpers
//                                    (Profundum Phase 2, specs/profundum-phase2-edge-fold)
// ==============================================================================
// Plan S8.1 (helper row); tasks.md T008 "Test helper first". Test-side only:
// allocation is fine here.
//
// LAYER RULE: no systems/ include, directly or indirectly. The Phase 1 systems
// helper (dsp/tests/unit/systems/profundum_core_test_helpers.h) includes
// <krate/dsp/systems/profundum_core.h>, so the analysis functions it holds are
// TRANSCRIBED here verbatim (same bodies, same arithmetic order) from its lines
// 52-305: kBh7Coefficients, safeDb10, bh7Window, goertzelPhase, fftDouble,
// bh7PowerSpectrum, renderedHarmonicPowers, aliasFftLength, AliasResult,
// aliasedPower. The Phase 1 helper stays untouched (SC-021);
// ProfundumEdgeFold_HelperParity (T015) proves the copies bit-identical.
//
// Every helper is an `inline` function in namespace Krate::DSP::HarmonicFoldTest
// (never an anonymous namespace in a header: unused-function warnings would
// break the zero-warning rule).
// ==============================================================================

#pragma once

#include "spectral_shape_recipe_test_helpers.h"

#include "low_frequency_metrics.h"

#include <krate/dsp/processors/harmonic_fold.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace Krate::DSP::HarmonicFoldTest {

inline constexpr double kTwoPi = 6.283185307179586476925286766559;

// -----------------------------------------------------------------------------
// Transcribed from profundum_core_test_helpers.h:52-305 (verbatim bodies)
// -----------------------------------------------------------------------------

/// 7-term Blackman-Harris coefficients (plan S8.2).
inline constexpr std::array<double, 7> kBh7Coefficients{
    0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
    0.01081174209837, 0.00077658482522, 0.00001388721735};

/// 10*log10(x) with a floor, so an exactly-zero energy never reaches log10(0)
/// (this header is used from -ffast-math TUs).
inline double safeDb10(double x) noexcept { return 10.0 * std::log10(std::max(x, 1e-300)); }

/// Periodic 7-term Blackman-Harris window in double: w[k] = Σⱼ (−1)ʲ aⱼ cos(2πjk/L).
inline std::vector<double> bh7Window(std::size_t L) {
    std::vector<double> w(L, 0.0);
    for (std::size_t k = 0; k < L; ++k) {
        double s = 0.0;
        for (std::size_t j = 0; j < kBh7Coefficients.size(); ++j) {
            const double sign = (j % 2 == 0) ? 1.0 : -1.0;
            s += sign * kBh7Coefficients[j]
                 * std::cos(kTwoPi * static_cast<double>(j) * static_cast<double>(k) / static_cast<double>(L));
        }
        w[k] = s;
    }
    return w;
}

/// BH7-windowed single-bin complex DFT in double at `hz`; returns atan2(Im, Re) (sample 0 = t 0).
inline double goertzelPhase(const float* x, std::size_t L, double fs, double hz) {
    REQUIRE(x != nullptr);
    const std::vector<double> w = bh7Window(L);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        const double ph = kTwoPi * hz * static_cast<double>(k) / fs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return std::atan2(im, re);
}

/// In-place iterative radix-2 complex FFT in double (forward, e^{-i}). a.size() must be a power
/// of two. Twiddles are evaluated directly (no recurrence), so the round-off floor is ~1e-16.
inline void fftDouble(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    REQUIRE(n >= 2u);
    REQUIRE((n & (n - 1)) == 0u);
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    std::vector<std::complex<double>> tw(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double ph = -kTwoPi * static_cast<double>(k) / static_cast<double>(n);
        tw[k] = std::complex<double>(std::cos(ph), std::sin(ph));
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len >> 1;
        const std::size_t step = n / len;
        for (std::size_t i = 0; i < n; i += len) {
            for (std::size_t k = 0; k < half; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + half] * tw[k * step];
                a[i + k] = u + v;
                a[i + k + half] = u - v;
            }
        }
    }
}

/// One-sided BH7 power spectrum (bins 0..L/2) of x[0..L), scaled so the bins sum to
/// mean((x·w)²)/mean(w²) (Parseval normalisation, spec SC-009). Full-scale sine -> sum ≈ 0.5.
inline std::vector<double> bh7PowerSpectrum(const float* x, std::size_t L) {
    REQUIRE(x != nullptr);
    const std::vector<double> w = bh7Window(L);
    std::vector<std::complex<double>> a(L);
    double sumXw2 = 0.0;
    double sumW2 = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        a[k] = std::complex<double>(v, 0.0);
        sumXw2 += v * v;
        sumW2 += w[k] * w[k];
    }
    fftDouble(a);
    std::vector<double> p(L / 2 + 1, 0.0);
    double sumP = 0.0;
    for (std::size_t k = 0; k <= L / 2; ++k) {
        p[k] = std::norm(a[k]);
        sumP += p[k];
    }
    REQUIRE(sumW2 > 0.0);
    const double target = sumXw2 / sumW2;   // == mean((x·w)²)/mean(w²)
    const double scale = sumP > 0.0 ? target / sumP : 0.0;
    for (double& v : p)
        v *= scale;
    return p;
}

/// Harmonic power weights wₙ (n = 1..N) of one lowFreq::kLowFrequencyFftSize frame of x, via the
/// Hann main-lobe sums (low_frequency_metrics.h). wₙ = 0 for n·f0 >= fs/2.
inline std::vector<double> renderedHarmonicPowers(const float* x, double fs, double f0, int N) {
    namespace lf = TestUtils::lowFreq;
    constexpr std::size_t kFrame = TestUtils::kLowFrequencyFftSize;
    std::vector<float> mags;
    REQUIRE(lf::magnitudeSpectrum(x, kFrame, mags));
    REQUIRE(lf::analysisFft(kFrame).isPrepared());
    const double binHz = fs / static_cast<double>(kFrame);
    std::vector<double> w(static_cast<std::size_t>(std::max(N, 0)), 0.0);
    for (int n = 1; n <= N; ++n) {
        const double hz = static_cast<double>(n) * f0;
        if (hz < fs * 0.5)
            w[static_cast<std::size_t>(n - 1)] = lf::harmonicMainLobePower(mags, hz, binHz);
    }
    return w;
}

/// The smallest power of two >= max(2^13, 64·fs/f0).
inline std::size_t aliasFftLength(double fs, double f0) {
    const double need = std::max(8192.0, 64.0 * fs / f0);
    std::size_t len = 8192;
    while (static_cast<double>(len) < need)
        len <<= 1;
    return len;
}

struct AliasResult {
    double aliasedDbfs;        ///< non-excluded bins 1..L/2, dB re 0.5 (0 dBFS sine)
    double excludedFraction;   ///< excluded bins / (L/2) over bins 1..L/2
    double totalDb;            ///< all bins 0..L/2, dB re 0.5 (the SC-006 "re total" variant)
};

/// SC-009 aliased power: BH7, Parseval-normalised, excluding ±8 bins around every n·f0 < fs/2
/// and bins 0-8.
inline AliasResult aliasedPower(const float* x, std::size_t fftLen, double fs, double f0) {
    REQUIRE(f0 > 0.0);
    const std::vector<double> p = bh7PowerSpectrum(x, fftLen);
    const double binHz = fs / static_cast<double>(fftLen);
    const std::size_t half = fftLen / 2;

    std::vector<char> excluded(half + 1, 0);
    for (std::size_t k = 0; k <= std::min<std::size_t>(8, half); ++k)
        excluded[k] = 1;
    for (int n = 1; static_cast<double>(n) * f0 < fs * 0.5; ++n) {
        const double centre = static_cast<double>(n) * f0 / binHz;
        const double lo = std::max(0.0, std::ceil(centre - 8.0));
        const double hi = std::min(static_cast<double>(half), std::floor(centre + 8.0));
        for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
            excluded[k] = 1;
    }

    double aliased = 0.0;
    double total = 0.0;
    std::size_t excludedCount = 0;
    for (std::size_t k = 0; k <= half; ++k) {
        total += p[k];
        if (k == 0)
            continue;
        if (excluded[k] != 0)
            ++excludedCount;
        else
            aliased += p[k];
    }
    return AliasResult{.aliasedDbfs = safeDb10(aliased / 0.5),
                       .excludedFraction = static_cast<double>(excludedCount) / static_cast<double>(half),
                       .totalDb = safeDb10(total / 0.5)};
}

// -----------------------------------------------------------------------------
// New Phase 2 helpers
// -----------------------------------------------------------------------------

/// BH7-windowed single-bin complex DFT in double at `hz`: the goertzelPhase body (same window,
/// same arithmetic, no normalisation), returning the complex value instead of its angle.
inline std::complex<double> goertzelPhasor(const float* x, std::size_t L, double fs, double hz) {
    REQUIRE(x != nullptr);
    const std::vector<double> w = bh7Window(L);
    double re = 0.0;
    double im = 0.0;
    for (std::size_t k = 0; k < L; ++k) {
        const double v = static_cast<double>(x[k]) * w[k];
        const double ph = kTwoPi * hz * static_cast<double>(k) / fs;
        re += v * std::cos(ph);
        im -= v * std::sin(ph);
    }
    return std::complex<double>(re, im);
}

/// Samples in the largest integer number of f0 periods that fits in n (rounded to whole samples).
/// REQUIREs at least `minSeconds` of signal in that span.
inline std::size_t integerPeriodLength(std::size_t n, double fs, double f0, double minSeconds) {
    REQUIRE(f0 > 0.0);
    const double period = fs / f0;
    const double periods = std::floor(static_cast<double>(n) / period);
    REQUIRE(periods >= 1.0);
    const auto len = std::min(n, static_cast<std::size_t>(std::llround(periods * period)));
    REQUIRE(static_cast<double>(len) / fs >= minSeconds);
    return len;
}

/// The Fold's calibration input (spec "Sine input"): peak·sin(2π·f0·n/fs) generated in double and
/// cast to float once. The default peak is the core's -12 dBFS RMS sine peak.
inline std::vector<float> calibrationSine(double fs, double f0, double seconds,
                                          double peak = static_cast<double>(HarmonicFold::kCalibrationPeak)) {
    REQUIRE(fs > 0.0);
    REQUIRE(seconds > 0.0);
    const auto total = static_cast<std::size_t>(std::llround(seconds * fs));
    std::vector<float> x(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n)
        x[n] = static_cast<float>(peak * std::sin(kTwoPi * f0 * static_cast<double>(n) / fs));
    return x;
}

/// The core's steady output for recipe state `c`, without the core: the masked recipe
/// (ProfundumTest::fullOf, Σ aₙ² = P₀) rendered as √½·Σ aₙ·sin(2π·n·f0·t) in double. √½ is the
/// bank's centre-pan gain, so the result sits at the core's -12 dBFS RMS per channel
/// (P₀/2·½ = 0.0631 -> -12.0 dBFS). REQUIREs the generated RMS within 0.1 dB of -12 dBFS
/// (over the largest integer number of f0 periods).
inline std::vector<float> recipeAdditive(const SpectralShapeRecipe::Controls& c, double f0, double fs,
                                         double seconds, int N = 64) {
    REQUIRE(fs > 0.0);
    REQUIRE(f0 > 0.0);
    REQUIRE(seconds > 0.0);
    REQUIRE(N > 0);
    const std::vector<float> a = ProfundumTest::fullOf(c, static_cast<float>(f0), fs, N);
    const auto total = static_cast<std::size_t>(std::llround(seconds * fs));
    const double centrePan = std::sqrt(0.5);
    std::vector<double> acc(total, 0.0);
    for (int h = 1; h <= N; ++h) {
        const double an = static_cast<double>(a[static_cast<std::size_t>(h - 1)]);
        if (an == 0.0)
            continue;
        const double hz = static_cast<double>(h) * f0;
        for (std::size_t n = 0; n < total; ++n)
            acc[n] += an * std::sin(kTwoPi * hz * static_cast<double>(n) / fs);
    }
    std::vector<float> x(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n)
        x[n] = static_cast<float>(centrePan * acc[n]);

    const std::size_t len = integerPeriodLength(total, fs, f0, 0.0);
    double sum = 0.0;
    for (std::size_t n = 0; n < len; ++n)
        sum += acc[n] * acc[n] * 0.5;
    const double rmsDb = safeDb10(sum / static_cast<double>(len));
    REQUIRE(std::abs(rmsDb - static_cast<double>(SpectralShapeRecipe::kCoreOutputRmsDb)) <= 0.1);
    return x;
}

/// Fold descriptors (spec "Descriptor definitions").
struct FoldDescriptors {
    double thdDb;                ///< Σₙ≥₂ wₙ / w₁ (dB)
    int h;                       ///< count of n >= 2 with n·f0 < fs/2 and wₙ >= w₁·1e-6
    double levelDb;              ///< output RMS re input RMS (dB)
    double crestDb;              ///< 20·log10(peak / RMS) of the output (dB)
    double centroidOct;          ///< Σ log2(n)·wₙ / Σ wₙ
    std::complex<double> t1;     ///< output h1 phasor / input h1 phasor (BH7 Goertzel at f0)
};

/// Steady-state descriptors of the Fold output `out` against its input `in` (both start at the
/// caller's t_settle). L, CF and T₁ use the largest integer number of f0 periods in n (>= 1 s).
/// The harmonic powers wₙ (THD, H, C) come from renderedHarmonicPowers on the first
/// kLowFrequencyFftSize samples of `out`, exactly as the spec's descriptor definitions say, so
/// n must be >= kLowFrequencyFftSize (262144 samples: 5.46 s at 48 kHz).
inline FoldDescriptors describeFold(const float* in, const float* out, std::size_t n, double fs, double f0) {
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    REQUIRE(n >= TestUtils::kLowFrequencyFftSize);
    const std::size_t len = integerPeriodLength(n, fs, f0, 1.0);

    FoldDescriptors d{};

    double sumIn = 0.0;
    double sumOut = 0.0;
    double peakOut = 0.0;
    for (std::size_t k = 0; k < len; ++k) {
        const double xi = static_cast<double>(in[k]);
        const double yo = static_cast<double>(out[k]);
        sumIn += xi * xi;
        sumOut += yo * yo;
        peakOut = std::max(peakOut, std::abs(yo));
    }
    REQUIRE(sumIn > 0.0);
    REQUIRE(sumOut > 0.0);
    const double msOut = sumOut / static_cast<double>(len);
    d.levelDb = safeDb10(sumOut / sumIn);
    d.crestDb = safeDb10(peakOut * peakOut / msOut);

    const std::complex<double> hIn = goertzelPhasor(in, len, fs, f0);
    const std::complex<double> hOut = goertzelPhasor(out, len, fs, f0);
    REQUIRE(std::norm(hIn) > 0.0);
    d.t1 = hOut / hIn;

    const int nHarm = static_cast<int>(std::ceil(fs * 0.5 / f0));
    const std::vector<double> w = renderedHarmonicPowers(out, fs, f0, nHarm);
    REQUIRE(!w.empty());
    const double w1 = w[0];
    REQUIRE(w1 > 0.0);
    double rest = 0.0;
    int count = 0;
    for (std::size_t i = 1; i < w.size(); ++i) {
        rest += w[i];
        if (static_cast<double>(i + 1) * f0 < fs * 0.5 && w[i] >= w1 * 1.0e-6)
            ++count;
    }
    d.thdDb = safeDb10(rest / w1);
    d.h = count;
    d.centroidOct = ProfundumTest::describePowers(w).centroidOct;
    return d;
}

/// Renders `inL` / `inR` through the Fold in `block`-sized host blocks (the last may be shorter).
inline std::pair<std::vector<float>, std::vector<float>> renderFold(HarmonicFold& fold,
                                                                    const std::vector<float>& inL,
                                                                    const std::vector<float>& inR,
                                                                    std::size_t block) {
    REQUIRE(block > 0u);
    REQUIRE(inL.size() == inR.size());
    std::vector<float> L = inL;
    std::vector<float> R = inR;
    const std::size_t total = L.size();
    for (std::size_t pos = 0; pos < total; pos += block) {
        const std::size_t n = std::min(block, total - pos);
        fold.processBlock(L.data() + pos, R.data() + pos, n);
    }
    return {std::move(L), std::move(R)};
}

/// Stereo render with L = R = `in`; returns L.
inline std::vector<float> renderFold(HarmonicFold& fold, const std::vector<float>& in, std::size_t block) {
    return renderFold(fold, in, in, block).first;
}

}  // namespace Krate::DSP::HarmonicFoldTest
