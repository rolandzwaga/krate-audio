// ==============================================================================
// API Contract: Spectral Distortion Processor
// ==============================================================================
// Layer 2: DSP Processor - Per-frequency-bin distortion in spectral domain
//
// This is an API contract file, not the implementation.
// See: dsp/include/krate/dsp/processors/spectral_distortion.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle X: DSP Constraints (COLA windows, proper overlap)
// - Principle XII: Test-First Development
//
// Reference: specs/103-spectral-distortion/spec.md
// ==============================================================================

#pragma once

// Layer 0: Core
#include <krate/dsp/core/math_constants.h>

// Layer 1: Primitives
#include <krate/dsp/primitives/stft.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/primitives/spectral_utils.h>
#include <krate/dsp/primitives/waveshaper.h>

// Standard library
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Spectral distortion processing modes (FR-005 to FR-008)
enum class SpectralDistortionMode : uint8_t {
    PerBinSaturate = 0,   ///< Per-bin waveshaping, phase may evolve naturally
    MagnitudeOnly = 1,    ///< Per-bin waveshaping, phase preserved exactly
    BinSelective = 2,     ///< Per-band drive control with frequency crossovers
    SpectralBitcrush = 3  ///< Magnitude quantization, phase preserved exactly
};

/// @brief Behavior for unassigned bins in BinSelective mode (FR-016)
enum class GapBehavior : uint8_t {
    Passthrough = 0,      ///< Unassigned bins pass through unmodified
    UseGlobalDrive = 1    ///< Unassigned bins use global drive parameter
};

// =============================================================================
// SpectralDistortion Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Per-frequency-bin distortion
///
/// Applies distortion algorithms to individual frequency bins in the spectral
/// domain, creating effects impossible with time-domain processing alone.
///
/// @par Features
/// - Four distortion modes: PerBinSaturate, MagnitudeOnly, BinSelective, SpectralBitcrush
/// - 9 waveshape curves via Waveshaper primitive
/// - Frequency-selective distortion with configurable bands
/// - DC/Nyquist bin exclusion by default (opt-in processing)
/// - Phase preservation option for surgical processing
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept process, allocation in prepare())
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 2 (depends on Layer 0-1 only)
/// - Principle X: DSP Constraints (COLA windows, proper overlap)
///
/// @par Usage
/// @code
/// SpectralDistortion distortion;
/// distortion.prepare(44100.0, 2048);
/// distortion.setMode(SpectralDistortionMode::PerBinSaturate);
/// distortion.setDrive(2.0f);
/// distortion.setSaturationCurve(WaveshapeType::Tanh);
///
/// // In process callback
/// distortion.processBlock(input, output, numSamples);
/// @endcode
class SpectralDistortion {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr std::size_t kMinFFTSize = 256;
    static constexpr std::size_t kMaxFFTSize = 8192;
    static constexpr std::size_t kDefaultFFTSize = 2048;
    static constexpr float kMinDrive = 0.0f;
    static constexpr float kMaxDrive = 10.0f;
    static constexpr float kDefaultDrive = 1.0f;
    static constexpr float kMinBits = 1.0f;
    static constexpr float kMaxBits = 16.0f;
    static constexpr float kDefaultBits = 16.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SpectralDistortion() noexcept = default;
    ~SpectralDistortion() noexcept = default;

    // Non-copyable, movable
    SpectralDistortion(const SpectralDistortion&) = delete;
    SpectralDistortion& operator=(const SpectralDistortion&) = delete;
    SpectralDistortion(SpectralDistortion&&) noexcept = default;
    SpectralDistortion& operator=(SpectralDistortion&&) noexcept = default;

    /// @brief Prepare for processing (FR-001)
    /// @param sampleRate Sample rate in Hz
    /// @param fftSize FFT size (power of 2, 256-8192)
    /// @pre fftSize is power of 2 within [kMinFFTSize, kMaxFFTSize]
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept;

    /// @brief Reset all internal state buffers (FR-002)
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-025)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of audio (FR-003)
    /// @param input Input buffer
    /// @param output Output buffer (may be same as input)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-025)
    void processBlock(const float* input, float* output, std::size_t numSamples) noexcept;

    // =========================================================================
    // Mode Selection
    // =========================================================================

    /// @brief Set distortion mode (FR-009)
    /// @param mode Processing mode
    void setMode(SpectralDistortionMode mode) noexcept;

    /// @brief Get current distortion mode
    [[nodiscard]] SpectralDistortionMode getMode() const noexcept;

    // =========================================================================
    // Global Parameters
    // =========================================================================

    /// @brief Set global drive amount (FR-010)
    /// @param drive Drive [0.0, 10.0], where 0 = bypass
    void setDrive(float drive) noexcept;

    /// @brief Get current drive setting
    [[nodiscard]] float getDrive() const noexcept;

    /// @brief Set saturation curve (FR-011)
    /// @param curve Waveshape type from WaveshapeType enum
    void setSaturationCurve(WaveshapeType curve) noexcept;

    /// @brief Get current saturation curve
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;

    /// @brief Enable/disable DC and Nyquist bin processing (FR-012)
    /// @param enabled true to process DC/Nyquist, false to exclude (default)
    void setProcessDCNyquist(bool enabled) noexcept;

    /// @brief Check if DC/Nyquist processing is enabled
    [[nodiscard]] bool getProcessDCNyquist() const noexcept;

    // =========================================================================
    // Bin-Selective Parameters
    // =========================================================================

    /// @brief Configure low frequency band (FR-013)
    /// @param freqHz Upper frequency limit of low band in Hz
    /// @param drive Drive amount for low band
    void setLowBand(float freqHz, float drive) noexcept;

    /// @brief Configure mid frequency band (FR-014)
    /// @param lowHz Lower frequency limit in Hz
    /// @param highHz Upper frequency limit in Hz
    /// @param drive Drive amount for mid band
    void setMidBand(float lowHz, float highHz, float drive) noexcept;

    /// @brief Configure high frequency band (FR-015)
    /// @param freqHz Lower frequency limit of high band in Hz
    /// @param drive Drive amount for high band
    void setHighBand(float freqHz, float drive) noexcept;

    /// @brief Set gap handling behavior (FR-016)
    /// @param mode Passthrough or UseGlobalDrive
    void setGapBehavior(GapBehavior mode) noexcept;

    /// @brief Get current gap behavior
    [[nodiscard]] GapBehavior getGapBehavior() const noexcept;

    // =========================================================================
    // SpectralBitcrush Parameters
    // =========================================================================

    /// @brief Set magnitude quantization bit depth (FR-017)
    /// @param bits Bit depth [1.0, 16.0], fractional values use continuous levels = 2^bits
    void setMagnitudeBits(float bits) noexcept;

    /// @brief Get current magnitude bit depth
    [[nodiscard]] float getMagnitudeBits() const noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get processing latency in samples (FR-004)
    /// @return Latency equal to FFT size
    [[nodiscard]] std::size_t latency() const noexcept;

    /// @brief Get configured FFT size
    [[nodiscard]] std::size_t getFftSize() const noexcept;

    /// @brief Get number of frequency bins
    [[nodiscard]] std::size_t getNumBins() const noexcept;

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    struct BandConfig {
        float lowHz = 0.0f;
        float highHz = 0.0f;
        float drive = 1.0f;
        std::size_t lowBin = 0;
        std::size_t highBin = 0;
    };

    // =========================================================================
    // Internal Methods
    // =========================================================================

    void processSpectralFrame() noexcept;
    void applyPerBinSaturate() noexcept;
    void applyMagnitudeOnly() noexcept;
    void applyBinSelective() noexcept;
    void applySpectralBitcrush() noexcept;
    [[nodiscard]] float getDriveForBin(std::size_t bin) const noexcept;
    void updateBandBins() noexcept;

    // =========================================================================
    // State
    // =========================================================================

    // STFT components
    STFT stft_;
    OverlapAdd overlapAdd_;
    SpectralBuffer inputSpectrum_;
    SpectralBuffer outputSpectrum_;

    // Processing
    Waveshaper waveshaper_;

    // Mode and parameters
    SpectralDistortionMode mode_ = SpectralDistortionMode::PerBinSaturate;
    float drive_ = kDefaultDrive;
    float magnitudeBits_ = kDefaultBits;
    bool processDCNyquist_ = false;
    GapBehavior gapBehavior_ = GapBehavior::Passthrough;

    // Band configuration
    BandConfig lowBand_;
    BandConfig midBand_;
    BandConfig highBand_;

    // Cached values
    double sampleRate_ = 44100.0;
    std::size_t fftSize_ = kDefaultFFTSize;
    std::size_t hopSize_ = kDefaultFFTSize / 2;
    std::size_t numBins_ = kDefaultFFTSize / 2 + 1;
    bool prepared_ = false;

    // Phase storage for MagnitudeOnly mode
    std::vector<float> storedPhases_;
};

} // namespace DSP
} // namespace Krate
