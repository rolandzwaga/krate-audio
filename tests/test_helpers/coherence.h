// ==============================================================================
// Test Helper: Welch Magnitude-Squared Coherence
// ==============================================================================
// Mean magnitude-squared coherence between two equal-length time-domain signals,
// measured inside a frequency band on an INDEPENDENT Welch-averaged analysis.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/coherence.h
// Namespace: Krate::DSP::TestUtils
//
// Reference: specs/vorago-phase5-feedback-ecology/plan.md S12.1, tasks.md T017.
//            Used by SC-002 (d), which REPORTS the figure and never gates on it.
//            Smoke case: FeedbackEcology_CoherenceHelperSmoke in
//            dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp.
//
// Shape follows tests/test_helpers/spectral_flux.h: its own analysis over the
// samples it is handed, never a read of a processor's internals; allocates;
// test-only. Built directly on primitives/fft.h + core/window_functions.h
// (no STFT/SpectralBuffer) because coherence needs the COMPLEX cross-spectrum,
// which SpectralBuffer's magnitude accessor does not preserve.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/fft.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate::DSP::TestUtils {

// =============================================================================
// Constants
// =============================================================================

/// Bins whose Sxx*Syy product falls below this are skipped: coherence is a
/// normalised quantity and its denominator has no meaning at silence. Same
/// 1e-12 floor render_fingerprint.h uses for its relative comparisons.
inline constexpr double kCoherenceDenomFloor = 1.0e-12;

// =============================================================================
// computeMeanCoherence
// =============================================================================

/// @brief Welch magnitude-squared coherence between two equal-length signals.
///
///   Sxx[k] = sum over frames |A_f[k]|^2
///   Syy[k] = sum over frames |B_f[k]|^2
///   Sxy[k] = sum over frames A_f[k] * conj(B_f[k])     (complex)
///   C[k]   = |Sxy[k]|^2 / (Sxx[k] * Syy[k])
///
/// and the return value is the mean of C[k] over the bins covering
/// [lowHz, highHz]. Bins whose denominator is at or below kCoherenceDenomFloor
/// contribute nothing; if every bin in the band is silent, the result is 0.0.
///
/// @param a           First signal (never a component's internals)
/// @param b           Second signal, same length as @p a
/// @param numSamples  Number of samples in each of @p a and @p b
/// @param sampleRate  Sample rate of both signals in Hz
/// @param fftSize     Analysis FFT size (power of two, [kMinFFTSize, kMaxFFTSize])
/// @param hopSize     Analysis hop in samples (0 < hopSize <= fftSize)
/// @param lowHz       Inclusive low edge of the measurement band
/// @param highHz      Inclusive high edge of the measurement band
/// @return Mean magnitude-squared coherence in [0, 1], or 0.0 when nothing
///         measurable was produced
///
/// @note WELCH AVERAGING IS THE ESTIMATOR, so the frame count is load-bearing:
///       with a SINGLE frame |Sxy|^2 == Sxx*Syy identically and the result is
///       1.0 for ANY pair of signals. The bias of the estimate on independent
///       inputs is ~1/numFrames, so callers wanting a meaningful "low" reading
///       must hand over enough samples for tens of frames (4096/2048 over a
///       few seconds at 48 kHz gives ~100).
/// @note Coherence is invariant under any linear time-invariant transform of
///       either input (a filter changes |H| and phase, not coherence) - which
///       is exactly why SC-002 (d) reports rather than gates.
/// @note Frames are taken at strides of hopSize; a trailing partial frame is
///       dropped rather than zero-padded, so no window ever sees a synthetic
///       edge that would depress the estimate.
/// @note Non-finite input samples are treated as 0.0f rather than poisoning
///       the whole accumulation (finiteness via detail::isFinite - fast-math
///       immune, never std::isnan/std::isfinite).
/// @note NOT real-time safe (allocates). Test-only.
[[nodiscard]] inline double computeMeanCoherence(const float* a, const float* b,
                                                 std::size_t numSamples, double sampleRate,
                                                 std::size_t fftSize, std::size_t hopSize,
                                                 float lowHz, float highHz) {
    // Ordered guards, matching computeMagnitudeFlux (spectral_flux.h:78-84):
    // everything below assumes a usable geometry and a usable band.
    if (a == nullptr || b == nullptr || numSamples == 0) return 0.0;
    if (fftSize == 0 || !std::has_single_bit(fftSize)) return 0.0;
    if (fftSize < kMinFFTSize || fftSize > kMaxFFTSize) return 0.0;
    if (hopSize == 0 || hopSize > fftSize) return 0.0;
    if (!detail::isFinite(sampleRate) || !(sampleRate > 0.0)) return 0.0;
    if (!detail::isFinite(lowHz) || !detail::isFinite(highHz)) return 0.0;

    const std::size_t numBins = fftSize / 2 + 1;
    const double      maxBin  = static_cast<double>(numBins - 1);

    // Band -> bin range: kLo = ceil(lowHz * fftSize / sampleRate),
    //                    kHi = floor(highHz * fftSize / sampleRate), both clamped
    // into [0, numBins); an empty band returns 0.0.
    const double binsPerHz = static_cast<double>(fftSize) / sampleRate;
    const double loRaw     = std::clamp(std::ceil(static_cast<double>(lowHz) * binsPerHz), 0.0, maxBin);
    const double hiRaw = std::clamp(std::floor(static_cast<double>(highHz) * binsPerHz), 0.0, maxBin);
    const auto   kLo   = static_cast<std::size_t>(loRaw);
    const auto   kHi   = static_cast<std::size_t>(hiRaw);
    if (kLo > kHi) return 0.0;

    FFT fft;
    fft.prepare(fftSize);
    if (!fft.isPrepared()) return 0.0;

    const std::vector<float> window = Window::generate(WindowType::Hann, fftSize);

    std::vector<float>   frameA(fftSize, 0.0f);
    std::vector<float>   frameB(fftSize, 0.0f);
    std::vector<Complex> specA(numBins);
    std::vector<Complex> specB(numBins);

    std::vector<double> sxx(numBins, 0.0);
    std::vector<double> syy(numBins, 0.0);
    std::vector<double> sxyRe(numBins, 0.0);
    std::vector<double> sxyIm(numBins, 0.0);

    std::size_t frameCount = 0;
    for (std::size_t start = 0; start + fftSize <= numSamples; start += hopSize) {
        for (std::size_t i = 0; i < fftSize; ++i) {
            const float sa = a[start + i];
            const float sb = b[start + i];
            frameA[i]      = detail::isFinite(sa) ? sa * window[i] : 0.0f;
            frameB[i]      = detail::isFinite(sb) ? sb * window[i] : 0.0f;
        }

        fft.forward(frameA.data(), specA.data());
        fft.forward(frameB.data(), specB.data());

        for (std::size_t k = kLo; k <= kHi; ++k) {
            const double ar = static_cast<double>(specA[k].real);
            const double ai = static_cast<double>(specA[k].imag);
            const double br = static_cast<double>(specB[k].real);
            const double bi = static_cast<double>(specB[k].imag);
            if (!detail::isFinite(ar) || !detail::isFinite(ai) || !detail::isFinite(br)
                || !detail::isFinite(bi)) {
                continue;
            }

            sxx[k] += ar * ar + ai * ai;
            syy[k] += br * br + bi * bi;
            // A * conj(B)
            sxyRe[k] += ar * br + ai * bi;
            sxyIm[k] += ai * br - ar * bi;
        }

        ++frameCount;
    }

    if (frameCount == 0) return 0.0;

    double      sum     = 0.0;
    std::size_t counted = 0;
    for (std::size_t k = kLo; k <= kHi; ++k) {
        const double denom = sxx[k] * syy[k];
        if (!(denom > kCoherenceDenomFloor)) continue;

        const double c = (sxyRe[k] * sxyRe[k] + sxyIm[k] * sxyIm[k]) / denom;
        // Analytically in [0, 1]; the clamp only absorbs floating-point slop at
        // the C == 1 end so a self-coherence never reports 1.0000000002.
        sum += std::clamp(c, 0.0, 1.0);
        ++counted;
    }

    if (counted == 0) return 0.0;
    return sum / static_cast<double>(counted);
}

} // namespace Krate::DSP::TestUtils
