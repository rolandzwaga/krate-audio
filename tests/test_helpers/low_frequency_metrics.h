// ==============================================================================
// Test Helper: Low-Frequency Spectral & Output Metrics
// ==============================================================================
// Measurement utilities for the Vorago Phase 6 SubharmonicEngine criteria
// (specs/vorago-phase6-subharmonic, plan S10.1). Everything here measures a
// tone at or below ~100 Hz, where the shipped helpers cannot reach:
//
//   - signal_metrics.h:111 calculateTHD caps its FFT at 8192 points, so at
//     27.5 Hz its +/-2-bin harmonic windows overlap and it returns a silent
//     0.0f on an empty fundamental (a dead engine would "pass");
//   - spectral_analysis.h defines no peak-PICKING rule, which SC-005 needs as
//     part of the criterion rather than as an implementation detail.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location:  tests/test_helpers/low_frequency_metrics.h
// Namespace: Krate::DSP::TestUtils
//
// No CMake edit is needed: tests/test_helpers/CMakeLists.txt declares
// `add_library(test_helpers INTERFACE)` with no source list, so a new header in
// this directory is on the include path of every suite that links it.
//
// Reference: specs/vorago-phase6-subharmonic/plan.md  (S10.1, S10.3, S10.4)
//            specs/vorago-phase6-subharmonic/spec.md  (A-4, SC-003..SC-007)
//
// Self-validation: every function here is validated against a synthetic signal
// of known truth before any engine consumes it, in
//   dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp
//     (SubharmonicEngine_LowFrequencyMetricsSelfCheck)  and
//   dsp/tests/unit/systems/subharmonic_engine_test.cpp
//     (SubharmonicEngine_OutputMetricsSelfCheck)
// so a helper bug fails as a helper bug and never as an engine claim.
//
// NOT thread-safe and NOT re-entrant: the spectral entry points share one FFT
// and one set of scratch buffers (see the memory note on lowFreq::analysisFft).
// Test code is single-threaded; this is a deliberate trade for not allocating
// ~5 MB per call site.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/oversampler.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate::DSP::TestUtils {

// =============================================================================
// Constants
// =============================================================================

/// The frame every low-frequency spectral measurement in Phase 6 uses: 262 144
/// points at 48 kHz = 0.1831 Hz bins, 5.46 s of signal. FFT::prepare only
/// requires a power of two (fft.h:144-155) -- its doc comment's "[256, 8192]"
/// describes the sizes the original spec exercised, not a limit in the code.
/// Every consumer REQUIREs lowFreq::analysisFft(n).isPrepared() (fft.h:255)
/// before trusting a result.
inline constexpr std::size_t kLowFrequencyFftSize = 262144;

/// SC-005's exclusion window, in bins. Four bins is wider than the Hann main
/// lobe (+/-2 bins) plus its first sidelobe (~2.4 bins), and the second
/// sidelobe is already at -41.5 dB, below the -40 dB threshold SC-005 scans
/// with. Real content is never merged: 55 Hz harmonics are 300 bins apart in a
/// kLowFrequencyFftSize frame at 48 kHz.
inline constexpr std::size_t kPeakExclusionBins = 4;

/// Returned by the spectral measurements when the frame carries no usable
/// signal (below -60 dBFS) or cannot be analysed at all. Negative by design, so
/// a silent tone FAILS a criterion rather than reporting a flattering 0.
inline constexpr float kLowFrequencyMetricSentinel = -1.0f;

// =============================================================================
// Shared analysis machinery
// =============================================================================

namespace lowFreq {

/// @brief The single FFT every spectral entry point in this header uses.
///
/// Memory note (plan S10.1): one kLowFrequencyFftSize frame is a 1 MB input
/// buffer plus 131 073 Complex bins (~1 MB) plus the FFT's three internal
/// aligned buffers (~3 MB). Five criteria share the size, so the instance is a
/// function-local static that is re-prepared only when the requested size
/// changes -- never one FFT per call site.
///
/// @param n Frame size in samples. Must be a power of two; anything else leaves
///          the returned FFT un-prepared (fft.h:150-155).
[[nodiscard]] inline FFT& analysisFft(std::size_t n) {
    static FFT fft;
    static std::size_t preparedSize = 0;
    if (preparedSize != n) {
        fft.prepare(n);
        preparedSize = fft.isPrepared() ? n : 0;
    }
    return fft;
}

/// @brief Periodic Hann window of length n, generated once per size.
[[nodiscard]] inline const std::vector<float>& hannWindow(std::size_t n) {
    static std::vector<float> window;
    if (window.size() != n) {
        window.assign(n, 0.0f);
        Window::generateHann(window.data(), n);
    }
    return window;
}

/// @brief Hann-windowed magnitude spectrum of x[0..n).
/// @param mags Filled with n/2+1 magnitudes (DC to Nyquist).
/// @return false if the frame cannot be analysed (null, empty, or a size the
///         FFT refuses), in which case mags is left empty.
[[nodiscard]] inline bool magnitudeSpectrum(const float* x, std::size_t n,
                                            std::vector<float>& mags) {
    mags.clear();
    if (x == nullptr || n < 8) {
        return false;
    }

    FFT& fft = analysisFft(n);
    if (!fft.isPrepared()) {
        return false;
    }

    const std::size_t numBins = n / 2 + 1;

    // Scratch shared across call sites (see the re-entrancy note at the top).
    static std::vector<float> windowed;
    static std::vector<Complex> bins;
    if (windowed.size() != n) {
        windowed.assign(n, 0.0f);
    }
    if (bins.size() != numBins) {
        bins.assign(numBins, Complex{});
    }

    const std::vector<float>& window = hannWindow(n);
    for (std::size_t i = 0; i < n; ++i) {
        windowed[i] = x[i] * window[i];
    }

    fft.forward(windowed.data(), bins.data());

    mags.resize(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        mags[k] = bins[k].magnitude();
    }
    return true;
}

/// @brief RMS of x[0..n), accumulated in double so a 262 144-point frame does
///        not lose the small end of the sum.
[[nodiscard]] inline float frameRms(const float* x, std::size_t n) {
    if (x == nullptr || n == 0) {
        return 0.0f;
    }
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double v = static_cast<double>(x[i]);
        acc += v * v;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

/// @brief Main-lobe power of the harmonic nearest to `harmonicHz`.
///
/// The nominal bin is located from the requested frequency, the true peak is
/// taken within +/-2 bins of it (the tone is never bin-centred in practice),
/// and the power is summed over that peak bin +/- 2 -- the Hann main lobe. A
/// Hann-windowed sinusoid's transform is identically zero beyond 2 bins from
/// its centre, so this captures the whole lobe at ANY fractional bin offset,
/// which is what makes the ratio of two such sums exact.
///
/// @return 0.0 when the harmonic is too close to DC or beyond the last bin.
[[nodiscard]] inline double harmonicMainLobePower(const std::vector<float>& mags,
                                                  double harmonicHz, double binHz) {
    if (binHz <= 0.0 || mags.empty()) {
        return 0.0;
    }
    const double targetBin = harmonicHz / binHz;

    // Keep peak-2 >= 1 (never DC, never an unsigned wrap) and peak+2 in range.
    if (targetBin < 5.0) {
        return 0.0;
    }
    const long nominal = std::lround(targetBin);
    if (nominal + 4 >= static_cast<long>(mags.size())) {
        return 0.0;
    }

    std::size_t peak = static_cast<std::size_t>(nominal);
    for (long b = nominal - 2; b <= nominal + 2; ++b) {
        const std::size_t idx = static_cast<std::size_t>(b);
        if (mags[idx] > mags[peak]) {
            peak = idx;
        }
    }

    double power = 0.0;
    for (std::size_t b = peak - 2; b <= peak + 2; ++b) {
        const double m = static_cast<double>(mags[b]);
        power += m * m;
    }
    return power;
}

/// @brief Amplitude of the sinusoid that would produce `mainLobePower`.
///
/// For a Hann-windowed tone of amplitude A over N points the peak bin reads
/// A*N/4 and its two neighbours A*N/8, so the main-lobe power sum is
/// (A*N)^2 * 3/32 and sqrt(sum) = A*N*0.30619. Used only to decide "is there a
/// tone here at all", so the ~1 dB scalloping spread across fractional bin
/// offsets is immaterial.
[[nodiscard]] inline double amplitudeFromMainLobePower(double mainLobePower, std::size_t n) {
    if (n == 0) {
        return 0.0;
    }
    return std::sqrt(mainLobePower) / (0.30619 * static_cast<double>(n));
}

}  // namespace lowFreq

// =============================================================================
// SC-003: divider frequency accuracy
// =============================================================================

/// @brief Estimate the frequency of the dominant partial in a stationary frame.
///
/// Hann-windowed magnitude spectrum, peak bin located (DC excluded), then
/// PARABOLIC INTERPOLATION OF THE LOG MAGNITUDE across the peak bin and its two
/// neighbours -- the standard estimator for a Hann-windowed sinusoid, residual
/// bias well under 0.01 bin (~0.002 Hz in a kLowFrequencyFftSize frame at
/// 48 kHz).
///
/// @param x          Frame of n samples.
/// @param n          Frame length; must be a power of two.
/// @param sampleRate Sample rate in Hz.
/// @return Estimated frequency in Hz, or kLowFrequencyMetricSentinel (negative)
///         if the frame's RMS is below -60 dBFS or it cannot be analysed.
[[nodiscard]] inline float estimatePeakFrequencyHz(const float* x, std::size_t n,
                                                   float sampleRate) {
    if (sampleRate <= 0.0f) {
        return kLowFrequencyMetricSentinel;
    }
    if (lowFreq::frameRms(x, n) < dbToGain(-60.0f)) {
        return kLowFrequencyMetricSentinel;
    }

    std::vector<float> mags;
    if (!lowFreq::magnitudeSpectrum(x, n, mags) || mags.size() < 4) {
        return kLowFrequencyMetricSentinel;
    }

    // Search bins [1, numBins-2] so DC can never win and peak +/- 1 is in range.
    std::size_t peak = 1;
    for (std::size_t k = 2; k + 1 < mags.size(); ++k) {
        if (mags[k] > mags[peak]) {
            peak = k;
        }
    }
    if (peak + 1 >= mags.size()) {
        return kLowFrequencyMetricSentinel;
    }

    // Parabolic interpolation on the LOG magnitude. The floor keeps the log
    // finite on an exactly-zero neighbour bin.
    constexpr float kLogFloor = 1e-30f;
    const float lm = std::log(std::max(mags[peak - 1], kLogFloor));
    const float lc = std::log(std::max(mags[peak], kLogFloor));
    const float lp = std::log(std::max(mags[peak + 1], kLogFloor));

    const float denom = lm - 2.0f * lc + lp;
    float delta = 0.0f;
    if (denom < -1e-20f || denom > 1e-20f) {
        delta = 0.5f * (lm - lp) / denom;
    }
    delta = std::clamp(delta, -0.5f, 0.5f);

    const double binHz = static_cast<double>(sampleRate) / static_cast<double>(n);
    return static_cast<float>((static_cast<double>(peak) + static_cast<double>(delta)) * binHz);
}

// =============================================================================
// SC-004: divider THD
// =============================================================================

/// @brief Total harmonic distortion, in percent, of a low-frequency tone.
///
/// Power is summed over the peak bin +/- 2 (the Hann main lobe) at the
/// fundamental and at each harmonic k = 2..maxHarmonic, and
/// THD = sqrt(sum_k P_k) / sqrt(P_1) * 100.
///
/// At 27.5 Hz in a kLowFrequencyFftSize frame the harmonic spacing is 150 bins,
/// so the main lobes never touch and the Hann sidelobe envelope at that
/// distance is far below -100 dB (0.001 %) -- a leakage floor two orders under
/// SC-004's 2 % ceiling. That is exactly what signal_metrics.h:111 calculateTHD
/// cannot provide (8192-point cap, overlapping +/-2-bin windows below ~200 Hz,
/// and a silent 0.0f return on an empty fundamental).
///
/// @return THD in percent, or kLowFrequencyMetricSentinel (negative) when the
///         fundamental's main-lobe power corresponds to an amplitude below
///         -60 dBFS, so a silent tone FAILS rather than reporting 0 %.
[[nodiscard]] inline float measureLowFrequencyThdPercent(const float* x, std::size_t n,
                                                         float fundamentalHz, float sampleRate,
                                                         int maxHarmonic = 10) {
    if (fundamentalHz <= 0.0f || sampleRate <= 0.0f || maxHarmonic < 2) {
        return kLowFrequencyMetricSentinel;
    }

    std::vector<float> mags;
    if (!lowFreq::magnitudeSpectrum(x, n, mags)) {
        return kLowFrequencyMetricSentinel;
    }

    const double binHz = static_cast<double>(sampleRate) / static_cast<double>(n);
    const double f0 = static_cast<double>(fundamentalHz);

    const double p1 = lowFreq::harmonicMainLobePower(mags, f0, binHz);
    if (lowFreq::amplitudeFromMainLobePower(p1, n) < static_cast<double>(dbToGain(-60.0f))) {
        return kLowFrequencyMetricSentinel;
    }

    double harmonicPower = 0.0;
    for (int k = 2; k <= maxHarmonic; ++k) {
        harmonicPower += lowFreq::harmonicMainLobePower(mags, f0 * static_cast<double>(k), binHz);
    }

    return static_cast<float>(std::sqrt(harmonicPower) / std::sqrt(p1) * 100.0);
}

// =============================================================================
// SC-005: spectral peak picking
// =============================================================================

/// @brief Peaks of a Hann magnitude spectrum, by the rule SC-005 is written in.
///
/// Peak PICKING is part of the criterion, not an implementation detail: without
/// a stated rule SC-005 (a) is not implementable. A plain local-maxima scan
/// fails on a CORRECT implementation, because the Hann window's own first
/// sidelobe sits ~2.4 bins from the main lobe at -31.5 dB -- above SC-005's
/// -40 dB threshold and outside its +/-1 bin tolerance -- so the fundamental's
/// own leakage would be reported as inharmonic content; and a
/// global-maximum-only scan cannot detect inharmonic content at all.
///
/// THE RULE: take every bin at or above `relativeThresholdDb` relative to the
/// global maximum, visit the candidates in DESCENDING MAGNITUDE ORDER, and
/// accept a candidate only if no already-accepted peak lies within
/// +/-kPeakExclusionBins of it.
///
/// @return Accepted bin indices, sorted ASCENDING by bin (the acceptance order
///         is by magnitude; the returned order is by frequency). Empty if the
///         frame cannot be analysed or carries no energy at all.
[[nodiscard]] inline std::vector<std::size_t> findSpectralPeaks(const float* x, std::size_t n,
                                                                float relativeThresholdDb) {
    std::vector<std::size_t> peaks;

    std::vector<float> mags;
    if (!lowFreq::magnitudeSpectrum(x, n, mags) || mags.empty()) {
        return peaks;
    }

    const float maxMag = *std::max_element(mags.begin(), mags.end());
    if (!(maxMag > 0.0f)) {
        return peaks;
    }
    const float threshold = maxMag * dbToGain(relativeThresholdDb);

    std::vector<std::size_t> candidates;
    for (std::size_t k = 0; k < mags.size(); ++k) {
        if (mags[k] >= threshold) {
            candidates.push_back(k);
        }
    }

    // Descending magnitude; bin index breaks ties so the result is deterministic.
    std::sort(candidates.begin(), candidates.end(),
              [&mags](std::size_t a, std::size_t b) {
                  if (mags[a] > mags[b]) return true;
                  if (mags[b] > mags[a]) return false;
                  return a < b;
              });

    for (const std::size_t candidate : candidates) {
        bool excluded = false;
        for (const std::size_t accepted : peaks) {
            const std::size_t distance =
                (candidate > accepted) ? (candidate - accepted) : (accepted - candidate);
            if (distance <= kPeakExclusionBins) {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            peaks.push_back(candidate);
        }
    }

    std::sort(peaks.begin(), peaks.end());
    return peaks;
}

// =============================================================================
// SC-006: true peak
// =============================================================================

/// @brief 4x-oversampled true peak of a stereo render, in dBTP.
///
/// Measured on the same basis TruePeakLimiter::processChunk uses
/// (true_peak_limiter.h:125-146): an Oversampler<4, 1> per channel in
/// Economy/ZeroLatency mode, with the RAW sample folded into the max so the
/// returned figure is never below the sample peak.
///
/// `sampleRate` is a deviation from spec A-4's three-argument sketch (recorded
/// in plan S14 C-7): the shipped Oversampler::prepare needs it
/// (oversampler.h:288-293).
///
/// @return dBTP, or -200.0f when the render is silent or cannot be measured.
[[nodiscard]] inline float measureTruePeakDb(const float* l, const float* r, std::size_t n,
                                             double sampleRate) {
    constexpr float kSilentDbTp = -200.0f;
    if (l == nullptr || r == nullptr || n == 0 || sampleRate <= 0.0) {
        return kSilentDbTp;
    }

    constexpr std::size_t kChunk = 512;

    Oversampler<4, 1> osL;
    Oversampler<4, 1> osR;
    osL.prepare(sampleRate, kChunk, OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
    osR.prepare(sampleRate, kChunk, OversamplingQuality::Economy, OversamplingMode::ZeroLatency);
    if (!osL.isPrepared() || !osR.isPrepared()) {
        return kSilentDbTp;
    }

    std::vector<float> upL(kChunk * 4, 0.0f);
    std::vector<float> upR(kChunk * 4, 0.0f);

    float peak = 0.0f;
    for (std::size_t offset = 0; offset < n; offset += kChunk) {
        const std::size_t count = std::min(kChunk, n - offset);

        osL.upsample(l + offset, upL.data(), count, 0);
        osR.upsample(r + offset, upR.data(), count, 0);

        for (std::size_t i = 0; i < count; ++i) {
            peak = std::max(peak, std::fabs(l[offset + i]));
            peak = std::max(peak, std::fabs(r[offset + i]));
        }
        for (std::size_t i = 0; i < count * 4; ++i) {
            peak = std::max(peak, std::fabs(upL[i]));
            peak = std::max(peak, std::fabs(upR[i]));
        }
    }

    if (!(peak > 0.0f)) {
        return kSilentDbTp;
    }
    return 20.0f * std::log10(peak);
}

// =============================================================================
// SC-007: mono compatibility
// =============================================================================

/// @brief Zero-lag normalised correlation of two equal-length buffers.
///
/// The shipped form (buffer_comparison.h:201, namespace TestHelpers) is a
/// template<size_t N> over std::array and is unusable on a multi-minute heap
/// render. Different namespace, so there is no overload interaction with it.
/// Accumulated in double: a 30 s render at 48 kHz is 1.44 M terms.
///
/// @return r in [-1, 1]; 0.0f if either buffer carries no energy.
[[nodiscard]] inline float calculateCorrelation(const float* a, const float* b, std::size_t n) {
    if (a == nullptr || b == nullptr || n == 0) {
        return 0.0f;
    }

    double sumAB = 0.0;
    double sumA2 = 0.0;
    double sumB2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = static_cast<double>(a[i]);
        const double y = static_cast<double>(b[i]);
        sumAB += x * y;
        sumA2 += x * x;
        sumB2 += y * y;
    }

    const double denom = std::sqrt(sumA2 * sumB2);
    if (denom < 1e-20) {
        return 0.0f;
    }
    return static_cast<float>(sumAB / denom);
}

}  // namespace Krate::DSP::TestUtils
