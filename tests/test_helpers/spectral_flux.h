// ==============================================================================
// Test Helper: Per-Bin Magnitude Flux
// ==============================================================================
// Mean normalised frame-to-frame L1 magnitude flux of a time-domain signal,
// measured inside a frequency band on an INDEPENDENT analysis STFT.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/spectral_flux.h
// Namespace: Krate::DSP::TestUtils
//
// Reference: specs/vorago-phase4-spectral-smear/spec.md (SC-004, Clarifications
//            Q2), plan.md S13.1. Sanity case: SpectralSmear_FluxHelperSanity in
//            dsp/tests/unit/processors/spectral_smear_spectral_test.cpp.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/window_functions.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/stft.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate::DSP::TestUtils {

// =============================================================================
// Constants
// =============================================================================

/// Frames whose in-band magnitude sum falls below this are treated as silence:
/// a normalised flux has no meaning when the normaliser is zero, and the ratio
/// would otherwise explode on the leading warm-up zeros of any STFT render.
inline constexpr double kFluxSilenceFloor = 1.0e-9;

// =============================================================================
// computeMagnitudeFlux
// =============================================================================

/// @brief Mean normalised frame-to-frame L1 magnitude flux inside [lowHz, highHz].
///
///   flux = mean over frames f of ( sum_k |mag_f[k] - mag_{f-1}[k]| / sum_k mag_f[k] )
///
/// Frames whose denominator is below kFluxSilenceFloor are skipped (silence has no
/// flux) and do not seed the predecessor either; returns 0.0 if every frame is
/// skipped.
///
/// "REDUCTION" IS ALWAYS THE RATIO flux(amount=0)/flux(amount=1), NEVER the
/// subtractive form 1 - flux(1)/flux(0): at SpectralSmear's shipped endpoints the
/// ratio reads ~4.8 at tilt 0 and ~114 at tilt +1, while the subtractive form reads
/// 1.10 and 1.91 and FAILS SC-004 (b)'s factor-of-2 gate on a correct build.
///
/// @param signal      Time-domain samples to analyse (never a component's internals)
/// @param numSamples  Number of samples in @p signal
/// @param sampleRate  Sample rate of @p signal in Hz
/// @param fftSize     Analysis FFT size (power of two)
/// @param hopSize     Analysis hop in samples (0 < hopSize <= fftSize)
/// @param lowHz       Inclusive low edge of the measurement band
/// @param highHz      Inclusive high edge of the measurement band
/// @return Mean normalised flux, or 0.0 when nothing measurable was produced
///
/// @note This helper runs its OWN, independent STFT + SpectralBuffer over the
///       samples it is handed. It never reads a processor's internal buffers.
/// @note It pushes in chunks of at most hopSize and drains with
///       `while (canAnalyze())`. STFT::pushSamples has NO overflow guard
///       (primitives/stft.h:104-124) and its ring is fftSize * 8 (:78), so a
///       helper that pushed a 30-second render in one call would corrupt memory.
///       This is the single most likely way to get the helper wrong.
/// @note NOT real-time safe (allocates). Test-only.
[[nodiscard]] inline double computeMagnitudeFlux(const float* signal, std::size_t numSamples,
                                                 double sampleRate, std::size_t fftSize,
                                                 std::size_t hopSize, float lowHz, float highHz) {
    // Ordered guards: everything below assumes a usable geometry and a usable band.
    if (signal == nullptr || numSamples == 0) return 0.0;
    if (fftSize == 0 || !std::has_single_bit(fftSize)) return 0.0;
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
    const double hiRaw     = std::clamp(std::floor(static_cast<double>(highHz) * binsPerHz), 0.0, maxBin);
    const auto   kLo       = static_cast<std::size_t>(loRaw);
    const auto   kHi       = static_cast<std::size_t>(hiRaw);
    if (kLo > kHi) return 0.0;

    STFT           stft;
    SpectralBuffer spectrum;
    stft.prepare(fftSize, hopSize, WindowType::Hann);
    spectrum.prepare(fftSize);

    std::vector<float> prevMag(numBins, 0.0f);
    std::vector<float> curMag(numBins, 0.0f);

    bool        havePrev   = false;
    double      fluxSum    = 0.0;
    std::size_t frameCount = 0;

    std::size_t offset = 0;
    while (offset < numSamples) {
        const std::size_t chunk = std::min(hopSize, numSamples - offset);
        stft.pushSamples(signal + offset, chunk);
        offset += chunk;

        // analyze() consumes only hopSize, so this must be a while, not an if.
        while (stft.canAnalyze()) {
            stft.analyze(spectrum);

            double denom = 0.0;
            for (std::size_t k = kLo; k <= kHi; ++k) {
                const float m = spectrum.getMagnitude(k);
                curMag[k]     = detail::isFinite(m) ? m : 0.0f;
                denom += static_cast<double>(curMag[k]);
            }

            if (!(denom > kFluxSilenceFloor)) {
                // Silence: no flux, and not a usable predecessor either.
                havePrev = false;
                continue;
            }

            if (havePrev) {
                double l1 = 0.0;
                for (std::size_t k = kLo; k <= kHi; ++k) {
                    l1 += std::fabs(static_cast<double>(curMag[k]) - static_cast<double>(prevMag[k]));
                }
                fluxSum += l1 / denom;
                ++frameCount;
            }

            // The first analysed frame has no predecessor and only seeds mag_{f-1}.
            std::copy(curMag.begin(), curMag.end(), prevMag.begin());
            havePrev = true;
        }
    }

    if (frameCount == 0) return 0.0;
    return fluxSum / static_cast<double>(frameCount);
}

} // namespace Krate::DSP::TestUtils
