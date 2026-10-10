// ==============================================================================
// Test helpers: ProfundumCore render and measurement helpers
//                                    (specs/profundum-phase1-harmonic-core)
// ==============================================================================
// Plan S8.2 "Shared measurement helpers"; tasks.md T009 "Helpers header".
// Test-side only: allocation is fine here.
//
// Every helper is an `inline` function in namespace Krate::DSP::ProfundumTest
// (never an anonymous namespace in a header: unused-function warnings would
// break the zero-warning rule; tasks.md rule 4).
//
// Spectral transforms for the BH7 analyses (aliasedPower, sidebandPower) run
// in double (fftDouble below), not through the float pffft-backed FFT class.
// Measured 2026-10-10 on the T009 self-check fixture (full-scale 1 kHz sine,
// 48 kHz, 8192 points, BH7, the aliasedPower exclusions): the float FFT's own
// round-off floor is -140.0 dBFS, which cannot meet the self-check bar of
// <= -150 dBFS; a double transform of the same frame measures -152.7 dBFS with
// the windowed frame rounded to float, and lower with it kept in double (as
// here). The bar is kept; the transform precision is what changed.
// ==============================================================================

#pragma once

#include "../processors/spectral_shape_recipe_test_helpers.h"

#include "allocation_detector.h"
#include "artifact_detection.h"
#include "low_frequency_metrics.h"

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/profundum_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Krate::DSP::ProfundumTest {

inline constexpr double kTwoPi = 6.283185307179586476925286766559;

/// 7-term Blackman-Harris coefficients (plan S8.2).
inline constexpr std::array<double, 7> kBh7Coefficients{
    0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803,
    0.01081174209837, 0.00077658482522, 0.00001388721735};

/// 10*log10(x) with a floor, so an exactly-zero energy never reaches log10(0)
/// (this header is used from -ffast-math TUs).
inline double safeDb10(double x) noexcept { return 10.0 * std::log10(std::max(x, 1e-300)); }

/// SC-014(a): L and R bit-identical.
inline void requireLREqual(const float* L, const float* R, std::size_t n) {
    REQUIRE(samplesBitEqual(L, R, n));
}

/// The core's sample rate, recovered from its public Δmax observer
/// (Δmax = 375·√P0·kControlInterval/fs, S4.3) and rounded to the nearest Hz. Exact for every
/// integer rate in [22 050, 192 000]: the float rounding of Δmax is ~6e-8 relative, i.e.
/// < 0.02 Hz at 192 kHz.
inline double coreSampleRate(const ProfundumCore& core) {
    const double dMax = static_cast<double>(core.maxShapeStepPerInterval());
    REQUIRE(dMax > 0.0);
    const double fs = static_cast<double>(ProfundumCore::kMaxGainSlewPerSecOverSqrtP0)
                      * std::sqrt(static_cast<double>(SpectralShapeRecipe::kPowerTarget))
                      * static_cast<double>(ProfundumCore::kControlInterval) / dMax;
    return std::round(fs);
}

struct Render {
    std::vector<float> L, R;
    std::vector<std::vector<float>> shapes, delivered;
    std::vector<float> maskF0, baseF0;
};

/// The caller prepares the core. Sets the policy and controls, calls noteOn(f0), then renders
/// `seconds` in `block`-sized blocks (the last one may be shorter), passing the matching slice of
/// f0Trajectory (which must hold the whole render) when it is non-null. With `record`, appends
/// shapeGains(), deliveredGains(), maskFrequency() and baseFrequency() after every block. A
/// grid-aligned kControlInterval-sample block (the default) holds exactly one control update, at
/// its first sample (plan S8.2).
inline Render renderCore(ProfundumCore& core, const SpectralShapeRecipe::Controls& c, float f0,
                         double seconds,
                         ProfundumCore::RetriggerPhase policy = ProfundumCore::RetriggerPhase::Reset,
                         bool record = false, std::size_t block = ProfundumCore::kControlInterval,
                         const float* f0Trajectory = nullptr) {
    REQUIRE(core.isPrepared());
    REQUIRE(block > 0);
    const double fs = coreSampleRate(core);
    const auto total = static_cast<std::size_t>(std::llround(seconds * fs));

    Render r;
    r.L.assign(total, 0.0f);
    r.R.assign(total, 0.0f);

    core.setRetriggerPhase(policy);
    core.setControls(c);
    core.noteOn(f0);

    for (std::size_t pos = 0; pos < total; pos += block) {
        const std::size_t n = std::min(block, total - pos);
        core.processBlock(r.L.data() + pos, r.R.data() + pos, n,
                          f0Trajectory != nullptr ? f0Trajectory + pos : nullptr);
        if (record) {
            const auto s = core.shapeGains();
            const auto d = core.deliveredGains();
            r.shapes.emplace_back(s.begin(), s.end());
            r.delivered.emplace_back(d.begin(), d.end());
            r.maskF0.push_back(core.maskFrequency());
            r.baseF0.push_back(core.baseFrequency());
        }
    }
    return r;
}

/// RMS in dBFS re 1.0 over the largest integer number of f0 periods that fits in n; that span
/// must be >= minSeconds.
inline double rmsDbOverPeriods(const float* x, std::size_t n, double fs, double f0,
                               double minSeconds = 1.0) {
    REQUIRE(x != nullptr);
    REQUIRE(f0 > 0.0);
    const double period = fs / f0;
    const double periods = std::floor(static_cast<double>(n) / period);
    REQUIRE(periods >= 1.0);
    REQUIRE(periods * period / fs >= minSeconds);
    const auto len = std::min(n, static_cast<std::size_t>(std::llround(periods * period)));
    double sum = 0.0;
    for (std::size_t i = 0; i < len; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return safeDb10(sum / static_cast<double>(len));
}

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
    REQUIRE(n >= 2);
    REQUIRE((n & (n - 1)) == 0);
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

inline Descriptors describeRendered(const float* x, double fs, double f0, int N) {
    const std::vector<double> w = renderedHarmonicPowers(x, fs, f0, N);
    return describePowers(w);
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

struct SidebandResult {
    double powerDbReTotal;     ///< remaining region power / total power (dB)
    double remainingFraction;  ///< remaining region bins / nominal region bins
};

/// Spec SC-010(b): BH7, 2^17 FFT. Region = ±20 Hz around k·fs/I ± n·f0 and (pitchInterval != 0)
/// k·fs/U ± n·f0, k = 1, 2, for every harmonic n whose level is >= −80 dB re h1; minus the
/// intended-modulation zones around n·f0 for those same n (a harmonic that is not there carries no
/// intended modulation; over every n up to Nyquist the Carson bands of a sine's vibrato tile the
/// whole spectrum and the 25 % non-vacuity guard can never pass): ±(5·amRateHz + 7 bins) for the
/// triangle AM and,
/// when vibratoPeakHz != 0, the Carson band ±(n·vibratoPeakHz + 2·5 Hz + 7 bins).
/// With `totalRef` (amended SC-010(b), D-2 ruling: x is then the residual core − ideal), the
/// harmonic levels that select the loud n and the total power come from the BH7 spectrum of
/// totalRef[0..2^17) (the core render), so the result is the residual's region power re the
/// core's total.
inline SidebandResult sidebandPower(const float* x, double fs, double f0, std::size_t controlInterval,
                                    std::size_t pitchInterval /*0 = none*/, double amRateHz,
                                    double vibratoPeakHz /*0 = none*/, const float* totalRef = nullptr) {
    constexpr std::size_t kLen = std::size_t{1} << 17;
    constexpr double kRegionHalfHz = 20.0;
    constexpr double kVibratoRateHz = 5.0;
    REQUIRE(f0 > 0.0);
    REQUIRE(controlInterval > 0);
    const std::vector<double> p = bh7PowerSpectrum(x, kLen);
    const std::vector<double> pRef = totalRef != nullptr ? bh7PowerSpectrum(totalRef, kLen) : p;
    const double binHz = fs / static_cast<double>(kLen);
    const std::size_t half = kLen / 2;
    const double nyquist = fs * 0.5;

    // Harmonic levels (peak bin within ±2 bins of n·f0), re h1, of the reference spectrum.
    const auto peakAt = [&](double hz) {
        const double centre = hz / binHz;
        const auto lo = static_cast<std::size_t>(std::max(0.0, std::floor(centre - 2.0)));
        const auto hi = std::min(half, static_cast<std::size_t>(std::ceil(centre + 2.0)));
        double m = 0.0;
        for (std::size_t k = lo; k <= hi; ++k)
            m = std::max(m, pRef[k]);
        return m;
    };
    const double h1 = peakAt(f0);
    REQUIRE(h1 > 0.0);
    std::vector<int> loud;
    for (int n = 1; static_cast<double>(n) * f0 < nyquist; ++n) {
        if (safeDb10(peakAt(static_cast<double>(n) * f0) / h1) >= -80.0)
            loud.push_back(n);
    }

    std::vector<char> inRegion(half + 1, 0);
    const auto markRegion = [&](double hz) {
        double f = std::fabs(hz);
        if (f > nyquist)
            f = fs - f;   // fold once
        if (f < 0.0 || f > nyquist)
            return;
        const double lo = std::max(0.0, std::ceil((f - kRegionHalfHz) / binHz));
        const double hi = std::min(static_cast<double>(half), std::floor((f + kRegionHalfHz) / binHz));
        for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
            inRegion[k] = 1;
    };
    std::vector<double> rates{fs / static_cast<double>(controlInterval)};
    if (pitchInterval != 0)
        rates.push_back(fs / static_cast<double>(pitchInterval));
    for (const double rate : rates) {
        for (int k = 1; k <= 2; ++k) {
            for (const int n : loud) {
                markRegion(static_cast<double>(k) * rate + static_cast<double>(n) * f0);
                markRegion(static_cast<double>(k) * rate - static_cast<double>(n) * f0);
            }
        }
    }

    std::vector<char> intended(half + 1, 0);
    for (const int n : loud) {
        double halfWidthHz = 5.0 * amRateHz + 7.0 * binHz;
        if (vibratoPeakHz != 0.0)
            halfWidthHz = std::max(halfWidthHz, static_cast<double>(n) * vibratoPeakHz
                                                    + 2.0 * kVibratoRateHz + 7.0 * binHz);
        const double centre = static_cast<double>(n) * f0;
        const double lo = std::max(0.0, std::ceil((centre - halfWidthHz) / binHz));
        const double hi = std::min(static_cast<double>(half), std::floor((centre + halfWidthHz) / binHz));
        for (auto k = static_cast<std::size_t>(lo); static_cast<double>(k) <= hi; ++k)
            intended[k] = 1;
    }

    double region = 0.0;
    double total = 0.0;
    std::size_t nominal = 0;
    std::size_t remaining = 0;
    for (std::size_t k = 0; k <= half; ++k) {
        total += pRef[k];
        if (inRegion[k] == 0)
            continue;
        ++nominal;
        if (intended[k] != 0)
            continue;
        ++remaining;
        region += p[k];
    }
    REQUIRE(total > 0.0);
    return SidebandResult{.powerDbReTotal = safeDb10(region / total),
                          .remainingFraction = nominal > 0 ? static_cast<double>(remaining)
                                                                 / static_cast<double>(nominal)
                                                           : 0.0};
}

/// n samples gliding log-linearly (constant semitones per sample) from f0a to f0b.
inline std::vector<float> logLinearRamp(float f0a, float f0b, std::size_t n) {
    std::vector<float> out(n, f0a);
    if (n < 2)
        return out;
    const double ratio = static_cast<double>(f0b) / static_cast<double>(f0a);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n - 1);
        out[i] = static_cast<float>(static_cast<double>(f0a) * std::pow(ratio, t));
    }
    return out;
}

/// SC-010 reference pitch: the per-sample trajectory traj delayed by the group delay of the core's
/// U-sample pitch hold (FR-043: the core samples f0PerSample at every multiple of U and holds it for
/// samples kU..kU+U−1, a centroid (U − 1)/2 samples after the sampled instant), linearly
/// interpolated. Still a per-sample f, never the U-held f: core − ideal is then the pitch-update
/// quantisation alone, without the hold's constant latency, which integrates into a permanent phase
/// offset of n·Δf·(U − 1)/(2·fs) cycles per partial across a glide (≈ 0.65 cycle at h64 over the
/// SC-010(a) ±12-semitone bend) that is not zipper.
inline double pitchHoldAlignedF0(const std::vector<float>& traj, std::size_t i) {
    if (traj.empty())
        return 0.0;
    constexpr double kHoldDelay = 0.5 * static_cast<double>(ProfundumCore::kPitchUpdateInterval - 1);
    const double t = static_cast<double>(i) - kHoldDelay;
    if (t <= 0.0)
        return static_cast<double>(traj.front());
    const auto k = static_cast<std::size_t>(t);
    if (k + 1 >= traj.size())
        return static_cast<double>(traj.back());
    const double frac = t - static_cast<double>(k);
    return static_cast<double>(traj[k]) * (1.0 - frac) + static_cast<double>(traj[k + 1]) * frac;
}

// =============================================================================
// SC-010 reference renders (spec SC-010 amended 2026-10-10, rulings on D-1/D-2 and D-3;
// tasks.md T017 RULING). Zipper is measured on the residual core − ideal.
// =============================================================================

/// RMS of x[0..n) in dBFS re 1.0, over every sample.
inline double rmsDbfs(const float* x, std::size_t n) {
    REQUIRE(x != nullptr);
    REQUIRE(n > 0);
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return safeDb10(sum / static_cast<double>(n));
}

/// r[i] = x[i] − ideal[i], rounded to float (the detector and the spectrum helpers take float).
template <class T>
inline std::vector<float> residual(const std::vector<T>& x, const std::vector<double>& ideal) {
    REQUIRE(x.size() == ideal.size());
    std::vector<float> r(x.size(), 0.0f);
    for (std::size_t i = 0; i < x.size(); ++i)
        r[i] = static_cast<float>(static_cast<double>(x[i]) - ideal[i]);
    return r;
}

/// Shared body of renderIdeal / renderHeldRaw: the L channel of a Reset noteOn at f0At(0),
/// rendered in double. controlsAt(i) and f0At(i) give the control and pitch trajectory at sample i
/// (the trajectory the core's latches sample at its control and pitch instants).
///
/// Ideal (heldRaw == false; D-3 ruling, option 2): every sample, the recipe target
/// evaluateShape(controlsAt(i)); FR-050's slew followed continuously, i.e. one great-circle step of
/// chord kSlewChordMargin·Δmax/kControlInterval per sample on the P0 sphere, snapping when the
/// remaining chord is <= Δmax/kControlInterval (the per-sample form of the core's
/// slewShapeTowardTarget, profundum_core.h); the mask evaluateMask(f0At(i)) and applyMask's
/// renormalisation to P0, |x| < 1e-12 flushed (deliverMaskedShape); then the bank's 2 ms one-pole
/// (coefficient formed as harmonic_oscillator_bank.h:140 forms it) from zero at noteOn, as the
/// bank's loadFrame seeds currentAmplitude_ = 0 (:295). The Reset noteOn starts the shape at its
/// target (S4.7, the FR-050 exemption).
///
/// Held raw (heldRaw == true; SC-010(b) positive control): the same targets, mask included,
/// latched at every multiple of kControlInterval from noteOn and held, with no slew and no one-pole.
///
/// Both: phases from sine phase 0 (Reset, FR-045), partial n at n·f0At(i) (the core's exact-ε law,
/// S4.6) through the bank's oscillator law -- the Gordon-Smith MCF with its elliptical-orbit
/// correction cos(π·n·f/fs) in the one-pole's target (harmonic_oscillator_bank.h:1084-1101), which
/// the bank refreshes in loadFrame (:365) but not in applyExternalFrequencyMultipliers (:665-672),
/// so during a glide it is part of what the core quantises -- summed through the bank's centre pan
/// gain. Precondition: numPartials·f0 < capHz on every
/// sample, so the anti-aliasing gain is 1 and no partial reaches the frozen cap frequency
/// (bank :1088-1096; SC-010 renders at C2 / C3 / 140.625 Hz, all far below).
template <class ControlsAt, class F0At>
inline std::vector<double> renderReference(double fs, std::size_t total, const ControlsAt& controlsAt,
                                           const F0At& f0At, bool heldRaw,
                                           int numPartials = ProfundumCore::kDefaultPartials) {
    using Controls = SpectralShapeRecipe::Controls;
    REQUIRE(fs > 0.0);
    REQUIRE(numPartials >= 1);
    REQUIRE(numPartials <= static_cast<int>(kMaxPartials));
    const auto n = static_cast<std::size_t>(numPartials);
    constexpr std::size_t kInterval = ProfundumCore::kControlInterval;

    const double p0 = static_cast<double>(SpectralShapeRecipe::kPowerTarget);
    const double sqrtP0 = std::sqrt(p0);
    const double capHz = static_cast<double>(SpectralShapeRecipe::capFrequency(fs));
    // Δmax exactly as ProfundumCore::prepare forms it (float), then per sample.
    const auto dMaxInterval = static_cast<float>(static_cast<double>(ProfundumCore::kMaxGainSlewPerSecOverSqrtP0)
                                                 * sqrtP0 * static_cast<double>(kInterval) / fs);
    const double dMaxSample = static_cast<double>(dMaxInterval) / static_cast<double>(kInterval);
    const double phi =
        2.0 * std::asin(static_cast<double>(ProfundumCore::kSlewChordMargin) * dMaxSample / (2.0 * sqrtP0));
    const float coeffF =
        1.0f - std::exp(-1.0f / (HarmonicOscillatorBank::kAmpSmoothTimeSec * static_cast<float>(fs)));
    const auto coeff = static_cast<double>(coeffF);
    const auto pan = static_cast<double>(HarmonicOscillatorBank::kCenterPanGain);

    std::array<float, kMaxPartials> targetF{};
    std::array<float, kMaxPartials> maskF{};
    std::array<double, kMaxPartials> target{};
    std::array<double, kMaxPartials> shape{};
    std::array<double, kMaxPartials> next{};
    std::array<double, kMaxPartials> delivered{};
    std::array<double, kMaxPartials> amp{};

    const auto sameControls = [](const Controls& a, const Controls& b) {
        return std::bit_cast<std::uint32_t>(a.depth) == std::bit_cast<std::uint32_t>(b.depth)
               && std::bit_cast<std::uint32_t>(a.body) == std::bit_cast<std::uint32_t>(b.body)
               && std::bit_cast<std::uint32_t>(a.bodyCurvature) == std::bit_cast<std::uint32_t>(b.bodyCurvature)
               && std::bit_cast<std::uint32_t>(a.bodyEmphasis) == std::bit_cast<std::uint32_t>(b.bodyEmphasis)
               && std::bit_cast<std::uint32_t>(a.edge) == std::bit_cast<std::uint32_t>(b.edge)
               && std::bit_cast<std::uint32_t>(a.shift) == std::bit_cast<std::uint32_t>(b.shift);
    };

    Controls latched{};
    bool haveControls = false;
    bool converged = true;
    bool deliverDirty = true;
    std::uint32_t maskBits = 0;
    bool haveMask = false;
    bool belowCap = true;
    std::array<double, kMaxPartials> mcfSin{};
    std::array<double, kMaxPartials> mcfCos{};
    std::array<double, kMaxPartials> eps{};
    std::array<double, kMaxPartials> mcfCorr{};
    mcfCos.fill(1.0);   // Reset: sin(0), cos(0) (bank loadFrame seeds from partial.phase = 0)
    std::uint64_t epsBits = 0;
    bool haveEps = false;
    std::vector<double> out(total, 0.0);

    for (std::size_t i = 0; i < total; ++i) {
        const double f = static_cast<double>(f0At(i));
        belowCap = belowCap && f > 0.0 && static_cast<double>(numPartials) * f < capHz;

        if (!heldRaw || i % kInterval == 0) {
            const Controls c = controlsAt(i);
            if (!haveControls || !sameControls(c, latched)) {
                latched = c;
                haveControls = true;
                SpectralShapeRecipe::evaluateShape(c, std::span<float>(targetF.data(), n));
                for (std::size_t k = 0; k < n; ++k)
                    target[k] = static_cast<double>(targetF[k]);
                if (i == 0 || heldRaw) {
                    shape = target;   // Reset noteOn exemption / held raw has no slew
                    converged = true;
                    deliverDirty = true;
                } else {
                    converged = false;
                }
            }
            const auto fF = static_cast<float>(f);
            if (!haveMask || std::bit_cast<std::uint32_t>(fF) != maskBits) {
                SpectralShapeRecipe::evaluateMask(fF, fs, std::span<float>(maskF.data(), n));
                maskBits = std::bit_cast<std::uint32_t>(fF);
                haveMask = true;
                deliverDirty = true;
            }
        }

        // FR-050 slew, one per-sample great-circle step (ideal only).
        if (!converged) {
            double chord2 = 0.0;
            double dot = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                chord2 += (target[k] - shape[k]) * (target[k] - shape[k]);
                dot += shape[k] * target[k];
            }
            const double omega = std::acos(std::clamp(dot / p0, 0.0, 1.0));
            if (std::sqrt(chord2) <= dMaxSample || !(omega > phi)) {
                shape = target;
                converged = true;
            } else {
                const double sinOmega = std::sin(omega);
                const double ca = std::sin(omega - phi) / sinOmega;
                const double cb = std::sin(phi) / sinOmega;
                double power = 0.0;
                for (std::size_t k = 0; k < n; ++k) {
                    next[k] = ca * shape[k] + cb * target[k];
                    power += next[k] * next[k];
                }
                const double scale = std::sqrt(p0 / power);
                for (std::size_t k = 0; k < n; ++k)
                    shape[k] = next[k] * scale;
            }
            deliverDirty = true;
        }

        // Mask and renormalise to P0 (applyMask), then the target hygiene flush.
        if (deliverDirty) {
            double power = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                delivered[k] = shape[k] * static_cast<double>(maskF[k]);
                power += delivered[k] * delivered[k];
            }
            const double scale = power > 0.0 ? std::sqrt(p0 / power) : 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                delivered[k] *= scale;
                if (std::abs(delivered[k]) < 1e-12)
                    delivered[k] = 0.0;
            }
            deliverDirty = false;
        }

        // Additive sum through the bank's oscillator law in double: the Gordon-Smith MCF
        // (harmonic_oscillator_bank.h:744-752: output s, then s += ε·c, c −= ε·s) with the core's
        // exact ε, ε_n = 2·sin(π·n·f/fs) (S4.6), and the bank's MCF elliptical-orbit correction
        // cos(π·n·f/fs) multiplied into the one-pole's target (antiAliasGain_, :1084-1101, smoothed
        // at :723-724), both formed from this sample's f. Static f: a unit-amplitude sine per partial.
        if (!haveEps || std::bit_cast<std::uint64_t>(f) != epsBits) {
            for (std::size_t k = 0; k < n; ++k) {
                const double halfOmega = 0.5 * kTwoPi * static_cast<double>(k + 1) * f / fs;
                eps[k] = 2.0 * std::sin(halfOmega);
                mcfCorr[k] = std::cos(halfOmega);
            }
            epsBits = std::bit_cast<std::uint64_t>(f);
            haveEps = true;
        }
        double sum = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            double a = delivered[k] * mcfCorr[k];
            if (!heldRaw) {
                amp[k] += coeff * (a - amp[k]);
                a = amp[k];
            }
            sum += a * mcfSin[k];
            mcfSin[k] += eps[k] * mcfCos[k];
            mcfCos[k] -= eps[k] * mcfSin[k];
        }
        out[i] = pan * sum;
    }
    INFO("reference precondition: 0 < numPartials * f0 < capHz (" << capHz << " Hz) on every sample");
    REQUIRE(belowCap);
    return out;
}

/// SC-010 ideal: per-sample targets -> continuous FR-050 slew -> 2 ms one-pole from zero.
template <class ControlsAt, class F0At>
inline std::vector<double> renderIdeal(double fs, std::size_t total, const ControlsAt& controlsAt, const F0At& f0At,
                                       int numPartials = ProfundumCore::kDefaultPartials) {
    return renderReference(fs, total, controlsAt, f0At, false, numPartials);
}

/// SC-010(b) positive control: targets held for kControlInterval samples, no smoothing.
template <class ControlsAt, class F0At>
inline std::vector<double> renderHeldRaw(double fs, std::size_t total, const ControlsAt& controlsAt,
                                         const F0At& f0At, int numPartials = ProfundumCore::kDefaultPartials) {
    return renderReference(fs, total, controlsAt, f0At, true, numPartials);
}

}  // namespace Krate::DSP::ProfundumTest
