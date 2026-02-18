// ==============================================================================
// Layer 2: DSP Processor - FormantPreserver
// ==============================================================================
// Cepstral envelope extraction for formant preservation in pitch shifting
// and spectral manipulation. Extracted from pitch_shift_processor.h for
// reuse by multiple consumers (PitchShiftProcessor, SpectralFreezeOscillator).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept processing, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends on Layer 0-1)
// - Principle XIV: ODR-safe (single definition, extracted from pitch_shift_processor.h)
//
// Algorithm:
// 1. Compute log magnitude spectrum
// 2. IFFT to get real cepstrum
// 3. Low-pass lifter (Hann-windowed) to isolate envelope
// 4. FFT to reconstruct smoothed log envelope
// 5. Apply envelope ratio to preserve formants during pitch shift
//
// References:
// - Julius O. Smith: Spectral Audio Signal Processing
// - stftPitchShift (https://github.com/jurihock/stftPitchShift)
// - Robel & Rodet: "Efficient Spectral Envelope Estimation"
// ==============================================================================

#pragma once

#include <cstddef>
#include <cmath>
#include <vector>
#include <algorithm>

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/spectral_simd.h>
#include <krate/dsp/primitives/fft.h>

namespace Krate::DSP {

/// @brief Extracts and applies spectral envelopes using cepstral analysis
///
/// Uses the cepstral method to separate spectral envelope (formants) from
/// fine harmonic structure. The algorithm:
/// 1. Compute log magnitude spectrum
/// 2. IFFT to get real cepstrum
/// 3. Low-pass lifter to isolate envelope (formants are slow-varying)
/// 4. FFT to reconstruct smoothed log envelope
/// 5. Apply envelope ratio to preserve formants during pitch shift
///
/// Quefrency Parameter:
/// - Controls the low-pass lifter cutoff (in seconds)
/// - Should be smaller than the fundamental period of the source
/// - Typical values: 1-2ms for vocals (0.001-0.002)
/// - Higher quefrency = more smoothing = coarser envelope
class FormantPreserver {
public:
    static constexpr float kDefaultQuefrencyMs = 1.5f; // 1.5ms default (~666Hz max F0)
    static constexpr float kMinMagnitude = 1e-10f;     // Avoid log(0)

    FormantPreserver() = default;

    /// @brief Prepare for given FFT size and sample rate
    /// @param fftSize FFT size (must be power of 2)
    /// @param sampleRate Sample rate in Hz
    void prepare(std::size_t fftSize, double sampleRate) noexcept {
        fftSize_ = fftSize;
        numBins_ = fftSize / 2 + 1;
        sampleRate_ = static_cast<float>(sampleRate);

        // Prepare FFT for cepstral analysis
        fft_.prepare(fftSize);

        // Allocate work buffers
        logMag_.resize(fftSize, 0.0f);
        cepstrum_.resize(fftSize, 0.0f);
        envelope_.resize(numBins_, 1.0f);
        complexBuf_.resize(numBins_);

        // Calculate quefrency cutoff in samples
        setQuefrencyMs(kDefaultQuefrencyMs);

        // Generate Hann window for smooth liftering
        lifterWindow_.resize(fftSize, 0.0f);
        updateLifterWindow();
    }

    /// @brief Reset internal state
    void reset() noexcept {
        fft_.reset();
        std::fill(envelope_.begin(), envelope_.end(), 1.0f);
    }

    /// @brief Set quefrency cutoff in milliseconds
    /// @param quefrencyMs Cutoff in ms (typical: 1-2ms for vocals)
    void setQuefrencyMs(float quefrencyMs) noexcept {
        quefrencyMs_ = std::max(0.5f, std::min(5.0f, quefrencyMs));
        // Convert to samples
        quefrencySamples_ = static_cast<std::size_t>(
            quefrencyMs_ * 0.001f * sampleRate_);
        // Clamp to valid range (at least 1, at most fftSize/4)
        quefrencySamples_ = std::max(std::size_t{1},
                            std::min(quefrencySamples_, fftSize_ / 4));
        updateLifterWindow();
    }

    /// @brief Get current quefrency cutoff in milliseconds
    [[nodiscard]] float getQuefrencyMs() const noexcept {
        return quefrencyMs_;
    }

    /// @brief Extract spectral envelope from magnitude spectrum
    /// @param magnitudes Input magnitude spectrum (numBins values)
    /// @param outputEnvelope Output envelope (numBins values)
    void extractEnvelope(const float* magnitudes, float* outputEnvelope) noexcept {
        if (fftSize_ == 0 || magnitudes == nullptr || outputEnvelope == nullptr) {
            return;
        }

        // Step 1: Compute log magnitude spectrum using SIMD batch log10
        // batchLog10 clamps non-positive inputs to kMinLogInput (== kMinMagnitude)
        batchLog10(magnitudes, logMag_.data(), numBins_);

        // Mirror to negative frequencies (symmetric log-mag spectrum)
        for (std::size_t k = 1; k < numBins_ - 1; ++k) {
            logMag_[fftSize_ - k] = logMag_[k];
        }

        // Step 2: Compute real cepstrum via IFFT
        computeCepstrum();

        // Step 3: Low-pass liftering with Hann window
        applyLifter();

        // Step 4: Reconstruct envelope via FFT
        reconstructEnvelope();

        // Copy to output
        for (std::size_t k = 0; k < numBins_; ++k) {
            outputEnvelope[k] = envelope_[k];
        }
    }

    /// @brief Extract envelope and store internally for later use
    void extractEnvelope(const float* magnitudes) noexcept {
        extractEnvelope(magnitudes, envelope_.data());
    }

    /// @brief Get the last extracted envelope
    [[nodiscard]] const float* getEnvelope() const noexcept {
        return envelope_.data();
    }

    /// @brief Apply formant preservation to pitch-shifted spectrum
    ///
    /// Formula: output[k] = shifted[k] * (originalEnv[k] / shiftedEnv[k])
    void applyFormantPreservation(
            const float* shiftedMagnitudes,
            const float* originalEnvelope,
            const float* shiftedEnvelope,
            float* outputMagnitudes,
            std::size_t numBins) const noexcept {

        for (std::size_t k = 0; k < numBins; ++k) {
            float shiftedEnv = std::max(shiftedEnvelope[k], kMinMagnitude);
            float ratio = originalEnvelope[k] / shiftedEnv;

            // Clamp ratio to avoid extreme amplification
            ratio = std::min(ratio, 100.0f);

            outputMagnitudes[k] = shiftedMagnitudes[k] * ratio;
        }
    }

    /// @brief Get number of frequency bins
    [[nodiscard]] std::size_t numBins() const noexcept { return numBins_; }

private:
    /// @brief Update Hann lifter window based on current quefrency
    void updateLifterWindow() noexcept {
        if (lifterWindow_.empty()) return;

        std::fill(lifterWindow_.begin(), lifterWindow_.end(), 0.0f);

        for (std::size_t q = 0; q <= quefrencySamples_; ++q) {
            float t = static_cast<float>(q) / static_cast<float>(quefrencySamples_);
            float window = 0.5f * (1.0f + std::cos(kPi * t));

            if (q < fftSize_ / 2) {
                lifterWindow_[q] = window;
            }
            if (q > 0 && q < fftSize_ / 2) {
                lifterWindow_[fftSize_ - q] = window;
            }
        }
    }

    /// @brief Compute real cepstrum from log-magnitude spectrum
    void computeCepstrum() noexcept {
        for (std::size_t k = 0; k < numBins_; ++k) {
            complexBuf_[k] = {logMag_[k], 0.0f};
        }

        fft_.inverse(complexBuf_.data(), cepstrum_.data());
    }

    /// @brief Apply low-pass lifter to cepstrum
    void applyLifter() noexcept {
        for (std::size_t q = 0; q < fftSize_; ++q) {
            cepstrum_[q] *= lifterWindow_[q];
        }
    }

    /// @brief Reconstruct envelope from liftered cepstrum
    void reconstructEnvelope() noexcept {
        fft_.forward(cepstrum_.data(), complexBuf_.data());

        // Stage: copy Complex::real fields into contiguous buffer for batchPow10
        for (std::size_t k = 0; k < numBins_; ++k) {
            logMag_[k] = complexBuf_[k].real;
        }

        // batchPow10 clamps output to [kMinLogInput, kMaxPow10Output] = [1e-10, 1e6]
        batchPow10(logMag_.data(), envelope_.data(), numBins_);
    }

    FFT fft_;
    std::size_t fftSize_ = 0;
    std::size_t numBins_ = 0;
    std::size_t quefrencySamples_ = 0;
    float sampleRate_ = 44100.0f;
    float quefrencyMs_ = kDefaultQuefrencyMs;

    std::vector<float> logMag_;
    std::vector<float> cepstrum_;
    std::vector<float> lifterWindow_;
    std::vector<float> envelope_;
    std::vector<Complex> complexBuf_;
};

} // namespace Krate::DSP
