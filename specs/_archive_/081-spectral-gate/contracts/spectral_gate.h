// ==============================================================================
// API Contract: Spectral Gate
// ==============================================================================
// This file defines the public interface for SpectralGate.
// Implementation will be in dsp/include/krate/dsp/processors/spectral_gate.h
//
// Feature: 081-spectral-gate
// Date: 2026-01-22
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/window_functions.h>

// Layer 1: Primitives
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/smoother.h>

// Standard library
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Per-bin spectral noise gate
///
/// Performs noise gating in the frequency domain by independently gating
/// each frequency bin based on its magnitude relative to a threshold.
/// Features attack/release envelopes per bin, expansion ratio control,
/// frequency range limiting, and spectral smearing for reduced artifacts.
///
/// @par Features
/// - Per-bin noise gating with configurable threshold (FR-001, FR-004)
/// - Configurable FFT sizes: 256, 512, 1024, 2048, 4096 (FR-002)
/// - COLA-compliant overlap-add synthesis (FR-003)
/// - Expansion ratio from 1:1 (bypass) to 100:1 (hard gate) (FR-005)
/// - Per-bin attack/release envelope tracking (FR-006, FR-007, FR-008)
/// - Frequency range limiting (FR-009, FR-010)
/// - Spectral smearing for reduced musical noise (FR-011, FR-012, FR-013)
/// - Real-time safe processing (FR-018, FR-019, FR-020)
/// - Click-free parameter changes (FR-021, FR-022)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 2 (depends on Layer 0-1 only)
/// - Principle X: DSP Constraints (COLA windows, proper overlap)
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// SpectralGate gate;
/// gate.prepare(44100.0, 1024);
/// gate.setThreshold(-40.0f);
/// gate.setRatio(100.0f);  // Hard gate
/// gate.setAttack(10.0f);
/// gate.setRelease(100.0f);
///
/// // In process callback
/// gate.processBlock(buffer, numSamples);
/// @endcode
class SpectralGate {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// FR-002: Supported FFT sizes
    static constexpr std::size_t kMinFFTSize = 256;
    static constexpr std::size_t kMaxFFTSize = 4096;
    static constexpr std::size_t kDefaultFFTSize = 1024;

    /// FR-004: Threshold range (dB)
    static constexpr float kMinThresholdDb = -96.0f;
    static constexpr float kMaxThresholdDb = 0.0f;
    static constexpr float kDefaultThresholdDb = -40.0f;

    /// FR-005: Ratio range (100:1 = practical infinity for hard gate)
    static constexpr float kMinRatio = 1.0f;
    static constexpr float kMaxRatio = 100.0f;
    static constexpr float kDefaultRatio = 100.0f;

    /// FR-006: Attack time range (ms)
    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kDefaultAttackMs = 10.0f;

    /// FR-007: Release time range (ms)
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;

    /// FR-009: Frequency range bounds (Hz)
    static constexpr float kMinFrequencyHz = 20.0f;
    static constexpr float kMaxFrequencyHz = 20000.0f;

    /// FR-011: Smearing amount range
    static constexpr float kMinSmearAmount = 0.0f;
    static constexpr float kMaxSmearAmount = 1.0f;

    /// Parameter smoothing time constant
    static constexpr float kSmoothingTimeMs = 50.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SpectralGate() noexcept = default;
    ~SpectralGate() noexcept = default;

    // Non-copyable, movable
    SpectralGate(const SpectralGate&) = delete;
    SpectralGate& operator=(const SpectralGate&) = delete;
    SpectralGate(SpectralGate&&) noexcept = default;
    SpectralGate& operator=(SpectralGate&&) noexcept = default;

    /// @brief Prepare for processing (FR-014)
    /// @param sampleRate Sample rate in Hz
    /// @param fftSize FFT size (power of 2, 256-4096)
    /// @pre fftSize is power of 2 within [kMinFFTSize, kMaxFFTSize]
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept;

    /// @brief Reset all internal state buffers (FR-015)
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample (FR-016)
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-019)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of audio in-place (FR-017)
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-019)
    void processBlock(float* buffer, std::size_t numSamples) noexcept;

    // =========================================================================
    // Threshold and Ratio Parameters
    // =========================================================================

    /// @brief Set gate threshold (FR-004)
    /// @param dB Threshold in decibels [-96, 0]
    /// @note Smoothed internally to prevent clicks (FR-021)
    void setThreshold(float dB) noexcept;

    /// @brief Get current threshold setting
    /// @return Threshold in decibels
    [[nodiscard]] float getThreshold() const noexcept;

    /// @brief Set expansion ratio (FR-005)
    /// @param ratio Expansion ratio [1.0, 100.0] (100.0 = hard gate)
    /// @note Smoothed internally to prevent clicks (FR-022)
    void setRatio(float ratio) noexcept;

    /// @brief Get current ratio setting
    /// @return Expansion ratio
    [[nodiscard]] float getRatio() const noexcept;

    // =========================================================================
    // Envelope Parameters
    // =========================================================================

    /// @brief Set per-bin attack time (FR-006)
    /// @param ms Attack time in milliseconds [0.1, 500]
    /// @note 10%-90% rise time measurement
    void setAttack(float ms) noexcept;

    /// @brief Get current attack time
    /// @return Attack time in milliseconds
    [[nodiscard]] float getAttack() const noexcept;

    /// @brief Set per-bin release time (FR-007)
    /// @param ms Release time in milliseconds [1, 5000]
    /// @note 90%-10% fall time measurement
    void setRelease(float ms) noexcept;

    /// @brief Get current release time
    /// @return Release time in milliseconds
    [[nodiscard]] float getRelease() const noexcept;

    // =========================================================================
    // Frequency Range Parameters
    // =========================================================================

    /// @brief Set frequency range for gating (FR-009)
    /// @param lowHz Lower frequency bound in Hz
    /// @param highHz Upper frequency bound in Hz
    /// @note Bins outside range pass through unaffected (FR-010)
    /// @note Boundaries rounded to nearest bin center
    void setFrequencyRange(float lowHz, float highHz) noexcept;

    /// @brief Get lower frequency bound
    /// @return Low frequency in Hz
    [[nodiscard]] float getLowFrequency() const noexcept;

    /// @brief Get upper frequency bound
    /// @return High frequency in Hz
    [[nodiscard]] float getHighFrequency() const noexcept;

    // =========================================================================
    // Smearing Parameters
    // =========================================================================

    /// @brief Set spectral smearing amount (FR-011)
    /// @param amount Smearing [0, 1] (0 = off, 1 = maximum)
    /// @note 0 = independent per-bin processing (FR-012)
    /// @note 1 = maximum neighbor influence (FR-013)
    void setSmearing(float amount) noexcept;

    /// @brief Get current smearing amount
    /// @return Smearing amount [0, 1]
    [[nodiscard]] float getSmearing() const noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency equal to FFT size (SC-003)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    /// @brief Get current FFT size
    [[nodiscard]] std::size_t getFftSize() const noexcept;

    /// @brief Get number of frequency bins
    [[nodiscard]] std::size_t getNumBins() const noexcept;

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // =========================================================================
    // Internal Processing Methods
    // =========================================================================

    /// @brief Process a single spectral frame
    void processSpectralFrame() noexcept;

    /// @brief Update per-bin envelopes from current magnitude spectrum
    void updateBinEnvelopes() noexcept;

    /// @brief Compute gate gains from envelopes and threshold
    void computeGateGains() noexcept;

    /// @brief Apply smearing to gate gains
    void applySmearing() noexcept;

    /// @brief Apply gate gains to spectrum
    void applyGains() noexcept;

    /// @brief Update attack/release coefficients
    void updateCoefficients() noexcept;

    /// @brief Update frequency range bin indices
    void updateFrequencyRange() noexcept;

    /// @brief Update smearing kernel size
    void updateSmearKernel() noexcept;

    /// @brief Convert Hz to bin index (round to nearest)
    [[nodiscard]] std::size_t hzToBin(float hz) const noexcept;

    // =========================================================================
    // State
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    std::size_t fftSize_ = kDefaultFFTSize;
    std::size_t hopSize_ = kDefaultFFTSize / 2;
    std::size_t numBins_ = kDefaultFFTSize / 2 + 1;
    float frameRate_ = 86.13f;
    bool prepared_ = false;

    // STFT components
    STFT stft_;
    OverlapAdd overlapAdd_;
    SpectralBuffer inputSpectrum_;
    SpectralBuffer outputSpectrum_;

    // Parameters (user-facing values)
    float thresholdDb_ = kDefaultThresholdDb;
    float ratio_ = kDefaultRatio;
    float attackMs_ = kDefaultAttackMs;
    float releaseMs_ = kDefaultReleaseMs;
    float lowHz_ = kMinFrequencyHz;
    float highHz_ = kMaxFrequencyHz;
    float smearAmount_ = kMinSmearAmount;

    // Computed values
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    std::size_t lowBin_ = 0;
    std::size_t highBin_ = 0;
    std::size_t smearKernelSize_ = 1;

    // Parameter smoothing
    OnePoleSmoother thresholdSmoother_;
    OnePoleSmoother ratioSmoother_;

    // Per-bin state
    std::vector<float> binEnvelopes_;
    std::vector<float> gateGains_;
    std::vector<float> smearedGains_;

    // Auxiliary buffers
    std::vector<float> zeroBuffer_;
    std::vector<float> singleSampleInputBuffer_;
    std::vector<float> singleSampleOutputBuffer_;
    std::size_t singleSampleWritePos_ = 0;
    std::size_t singleSampleReadPos_ = 0;
};

} // namespace DSP
} // namespace Krate
