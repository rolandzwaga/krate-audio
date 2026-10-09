// ==============================================================================
// Test Helper: Reverb / Room Analysis Metrics
// ==============================================================================
// Shared measurement toolkit for reverb and cavern-space tests (FR-080).
//
// This is TEST INFRASTRUCTURE, not production DSP code. Everything here is
// header-only and `inline`; allocation is allowed (no audio thread runs here).
//
// Location:  tests/test_helpers/reverb_metrics.h
// Namespace: Krate::DSP::TestUtils
//
// Origin:
//   - normalisedEchoDensity() is LIFTED from the inline block that used to live
//     in dsp/tests/unit/effects/fdn_reverb_test.cpp (SC-005 echo density). With
//     startWindow = 0 and windowCount = all windows it reproduces that block
//     exactly.
//   - The band / T60 / noise helpers are RE-DERIVED here. The equivalents in
//     dsp/tests/unit/effects/aether_reverb_test.cpp are file-local and that TU
//     is frozen (specs/vorago-phase9-cavern-space, SC-012 (b)), so nothing was
//     moved out of it.
//
// Portability: no std::isnan / std::isinf / std::isfinite anywhere in this
// header, so it is safe to include from TUs compiled with -ffast-math.
//
// Reference: specs/vorago-phase9-cavern-space/spec.md (FR-080)
// ==============================================================================

#pragma once

#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

// =============================================================================
// Constants
// =============================================================================

/// Pi in double precision (the shared math_constants.h only carries the float).
inline constexpr double kPiDouble = 3.14159265358979323846;

/// Butterworth Q values for a 4th-order (two-biquad) section.
inline constexpr double kButterworthQ4[2] = {0.541196100146197, 1.306562964876377};

/// Butterworth Q for a 2nd-order (single-biquad) section: 1 / sqrt(2).
inline constexpr double kButterworthQ2 = 0.7071067811865476;

// =============================================================================
// Normalised echo density (the lift)
// =============================================================================

/// @brief Normalised echo density (NED): the fraction of analysis windows whose
///        RMS sits above -40 dB relative to the loudest analysed window.
///
/// Per-window RMS over `windowSamples`, threshold = peak * 0.01 (-40 dB) with the
/// **peak taken over the analysed window range only**, result = occupied / windowCount.
///
/// Windows that run past the end of `monoIr` simply see zeros for the missing
/// samples; the denominator is always `windowCount`, exactly as asked for.
///
/// @param monoIr        Mono impulse response / render to analyse.
/// @param windowSamples Window length in samples (e.g. 48 = 1 ms at 48 kHz).
/// @param startWindow   Index of the first window to analyse (0 = start of buffer).
/// @param windowCount   Number of windows to analyse.
/// @return Occupied fraction in [0, 1]; 0.0 when `windowCount` or `windowSamples` is 0.
[[nodiscard]] inline double normalisedEchoDensity(std::span<const float> monoIr,
                                                  std::size_t windowSamples,
                                                  std::size_t startWindow,
                                                  std::size_t windowCount) {
    if ((windowCount == 0u) || (windowSamples == 0u)) {
        return 0.0;
    }

    std::vector<double> amplitude(windowCount, 0.0);
    double peakAmp = 0.0;

    for (std::size_t w = 0; w < windowCount; ++w) {
        const std::size_t base = (startWindow + w) * windowSamples;
        double sum = 0.0;
        for (std::size_t i = 0; i < windowSamples; ++i) {
            const std::size_t idx = base + i;
            if (idx >= monoIr.size()) {
                break;
            }
            const double s = static_cast<double>(monoIr[idx]);
            sum += s * s;
        }
        amplitude[w] = std::sqrt(sum / static_cast<double>(windowSamples));
        peakAmp = std::max(peakAmp, amplitude[w]);
    }

    const double threshold = peakAmp * 0.01;  // -40 dB
    std::size_t occupied = 0;
    for (std::size_t w = 0; w < windowCount; ++w) {
        if (amplitude[w] > threshold) {
            ++occupied;
        }
    }

    return static_cast<double>(occupied) / static_cast<double>(windowCount);
}

// =============================================================================
// Biquad / octave-band analysis filters (RBJ cookbook)
// =============================================================================

/// @brief Direct-form-I biquad. Test-only: `double` coefficients baked to float.
struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    [[nodiscard]] float process(float x) noexcept {
        const float y = (b0 * x) + (b1 * x1) + (b2 * x2) - (a1 * y1) - (a2 * y2);
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

/// @brief RBJ low-pass biquad.
[[nodiscard]] inline Biquad makeLowpass(double sampleRate, double cutoffHz, double q) {
    const double w0 = 2.0 * kPiDouble * cutoffHz / sampleRate;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = static_cast<float>(((1.0 - cw) * 0.5) / a0);
    b.b1 = static_cast<float>((1.0 - cw) / a0);
    b.b2 = b.b0;
    b.a1 = static_cast<float>((-2.0 * cw) / a0);
    b.a2 = static_cast<float>((1.0 - alpha) / a0);
    return b;
}

/// @brief RBJ high-pass biquad.
[[nodiscard]] inline Biquad makeHighpass(double sampleRate, double cutoffHz, double q) {
    const double w0 = 2.0 * kPiDouble * cutoffHz / sampleRate;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = static_cast<float>(((1.0 + cw) * 0.5) / a0);
    b.b1 = static_cast<float>((-(1.0 + cw)) / a0);
    b.b2 = b.b0;
    b.a1 = static_cast<float>((-2.0 * cw) / a0);
    b.a2 = static_cast<float>((1.0 - alpha) / a0);
    return b;
}

/// @brief One-octave band-pass: 4th-order Butterworth HP then LP.
struct OctaveBand {
    Biquad hp[2];
    Biquad lp[2];

    [[nodiscard]] float process(float x) noexcept {
        float y = x;
        for (auto& s : hp) {
            y = s.process(y);
        }
        for (auto& s : lp) {
            y = s.process(y);
        }
        return y;
    }
};

/// @brief Build a one-octave band centred on `centreHz`: edges centreHz / sqrt(2)
///        and centreHz * sqrt(2), as 4th-order Butterworth sections.
/// @note The upper edge is additionally capped at 0.45 * sampleRate so a high band
///       at a low sample rate still yields a valid (stable) filter.
[[nodiscard]] inline OctaveBand makeOctaveBand(double sampleRate, double centreHz) {
    const double lo = centreHz / std::sqrt(2.0);
    const double hi = std::min(centreHz * std::sqrt(2.0), sampleRate * 0.45);
    OctaveBand f;
    for (std::size_t k = 0; k < 2u; ++k) {
        f.hp[k] = makeHighpass(sampleRate, lo, kButterworthQ4[k]);
        f.lp[k] = makeLowpass(sampleRate, hi, kButterworthQ4[k]);
    }
    return f;
}

// =============================================================================
// Schroeder T60
// =============================================================================

/// @brief T60 from Schroeder backward integration of a per-hop energy envelope.
///
/// Backward-integrates `hopEnergy`, converts to dB relative to the integral's
/// start, least-squares fits a straight line over the -5 dB .. -25 dB span and
/// extrapolates that slope to a 60 dB drop.
///
/// @param hopEnergy  Energy (not amplitude) per analysis hop, in time order.
/// @param hopSeconds Hop length in seconds.
/// @return T60 in seconds; 0.0 when the -5 dB .. -25 dB span is never reached,
///         when the fit is degenerate, or when the envelope carries no energy.
[[nodiscard]] inline double schroederT60(const std::vector<double>& hopEnergy,
                                         double hopSeconds) {
    const std::size_t n = hopEnergy.size();
    if ((n < 8u) || !(hopSeconds > 0.0)) {
        return 0.0;
    }

    std::vector<double> edc(n, 0.0);
    double acc = 0.0;
    for (std::size_t i = n; i-- > 0u;) {
        acc += hopEnergy[i];
        edc[i] = acc;
    }
    if (!(edc[0] > 0.0)) {
        return 0.0;
    }
    const double ref = edc[0];

    std::size_t iStart = n;
    std::size_t iEnd = n;
    for (std::size_t i = 0; i < n; ++i) {
        const double db = 10.0 * std::log10(std::max(edc[i], 1e-300) / ref);
        if ((iStart == n) && (db <= -5.0)) {
            iStart = i;
        }
        if (db <= -25.0) {
            iEnd = i;
            break;
        }
    }
    if ((iStart == n) || (iEnd == n) || (iEnd < (iStart + 4u))) {
        return 0.0;
    }

    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    double count = 0.0;
    for (std::size_t i = iStart; i <= iEnd; ++i) {
        const double x = static_cast<double>(i) * hopSeconds;
        const double y = 10.0 * std::log10(std::max(edc[i], 1e-300) / ref);
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
        count += 1.0;
    }

    const double denom = (count * sxx) - (sx * sx);
    if (!(std::abs(denom) > 0.0)) {
        return 0.0;
    }
    const double slopeDbPerSecond = ((count * sxy) - (sx * sy)) / denom;
    if (!(slopeDbPerSecond < 0.0)) {
        return 0.0;
    }

    return -60.0 / slopeDbPerSecond;
}

// =============================================================================
// Spectral centroid
// =============================================================================

/// @brief Magnitude-weighted spectral centroid of a block, in Hz.
///
/// Hann-windows the block, takes one real FFT and returns
/// sum(f_k * |X_k|) / sum(|X_k|).
///
/// The repo FFT accepts powers of two in [256, 8192], so the transform size is
/// the largest such power of two that fits `x`, clamped into that range; a block
/// shorter than the chosen size is zero-padded, a longer one is truncated.
///
/// @param x  Time-domain block.
/// @param sr Sample rate in Hz.
/// @return Centroid in Hz; 0.0 for an empty block or an all-zero spectrum.
[[nodiscard]] inline double spectralCentroidHz(std::span<const float> x, double sr) {
    if (x.empty() || !(sr > 0.0)) {
        return 0.0;
    }

    std::size_t fftSize = 256u;
    while ((fftSize * 2u) <= x.size() && (fftSize < 8192u)) {
        fftSize *= 2u;
    }

    FFT fft;
    fft.prepare(fftSize);
    if (fft.size() == 0u) {
        return 0.0;
    }

    std::vector<float> windowed(fftSize, 0.0f);
    const std::size_t copyCount = std::min(fftSize, x.size());
    const double denomWindow = static_cast<double>(fftSize);
    for (std::size_t i = 0; i < copyCount; ++i) {
        const double w = 0.5 - (0.5 * std::cos(2.0 * kPiDouble * static_cast<double>(i)
                                               / denomWindow));
        windowed[i] = x[i] * static_cast<float>(w);
    }

    std::vector<Complex> spectrum((fftSize / 2u) + 1u);
    fft.forward(windowed.data(), spectrum.data());

    double weighted = 0.0;
    double total = 0.0;
    const double binHz = sr / static_cast<double>(fftSize);
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const double re = static_cast<double>(spectrum[k].real);
        const double im = static_cast<double>(spectrum[k].imag);
        const double mag = std::sqrt((re * re) + (im * im));
        weighted += mag * (static_cast<double>(k) * binHz);
        total += mag;
    }

    if (!(total > 0.0)) {
        return 0.0;
    }
    return weighted / total;
}

// =============================================================================
// Correlation
// =============================================================================

/// @brief Pearson correlation coefficient over the overlapping prefix of a and b.
/// @return r in [-1, 1]; 0.0 when fewer than two samples overlap or either input
///         has zero variance.
[[nodiscard]] inline double pearson(std::span<const double> a, std::span<const double> b) {
    const std::size_t n = std::min(a.size(), b.size());
    if (n < 2u) {
        return 0.0;
    }

    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        meanA += a[i];
        meanB += b[i];
    }
    meanA /= static_cast<double>(n);
    meanB /= static_cast<double>(n);

    double num = 0.0;
    double varA = 0.0;
    double varB = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double da = a[i] - meanA;
        const double db = b[i] - meanB;
        num += da * db;
        varA += da * da;
        varB += db * db;
    }

    const double denom = std::sqrt(varA * varB);
    if (!(denom > 0.0)) {
        return 0.0;
    }
    return num / denom;
}

// =============================================================================
// Band-limited noise generator (G-2 as a stream, never a looped buffer)
// =============================================================================

/// @brief Persistent state of the band-limited noise stream.
///
/// Holding the PRNG *and* the filter state here is what makes successive
/// fillBandLimitedNoise() calls continue one single stream with no seam and no
/// periodicity - a 30-minute render must not repeat.
struct NoiseState {
    Xorshift32 rng{1u};
    Biquad hp;
    Biquad lp;
    bool initialised = false;
};

/// Fixed output gain. NOT peak normalisation: a streaming generator cannot know
/// the peak of a render it has not produced yet, and normalising per call would
/// put a seam at every block boundary.
inline constexpr float kBandLimitedNoiseGain = 0.5f;

/// @brief Fill `dst` with the next slice of a band-limited (80 Hz .. 11 kHz) noise
///        stream.
///
/// White noise from Xorshift32 through a 2nd-order 80 Hz high-pass and a 2nd-order
/// 11 kHz low-pass whose state lives in `state`.
///
/// @param dst         Destination block.
/// @param sr          Sample rate in Hz (used when the stream is initialised).
/// @param seed        PRNG seed (used when the stream is initialised).
/// @param startSample Absolute sample index of `dst[0]`. Only 0 is meaningful: it
///                    (re)initialises the stream. Any other value continues it.
/// @param state       Stream state, carried across calls by the caller.
inline void fillBandLimitedNoise(std::span<float> dst, double sr, std::uint32_t seed,
                                 std::uint64_t startSample, NoiseState& state) {
    if ((startSample == 0u) || !state.initialised) {
        state.rng.seed(seed);
        state.hp = makeHighpass(sr, 80.0, kButterworthQ2);
        state.lp = makeLowpass(sr, std::min(11000.0, sr * 0.45), kButterworthQ2);
        state.initialised = true;
    }

    for (std::size_t i = 0; i < dst.size(); ++i) {
        float v = state.rng.nextFloat();
        v = state.hp.process(v);
        v = state.lp.process(v);
        dst[i] = v * kBandLimitedNoiseGain;
    }
}

}  // namespace TestUtils
}  // namespace DSP
}  // namespace Krate
