// ==============================================================================
// Layer 2: DSP Processor - Spectral Distortion
// ==============================================================================
// Per-frequency-bin distortion in the spectral domain.
//
// Features:
// - Four distortion modes: PerBinSaturate, MagnitudeOnly, BinSelective, SpectralBitcrush
// - 9 waveshape curves via Waveshaper primitive
// - Frequency-selective distortion with configurable bands
// - DC/Nyquist bin exclusion by default (opt-in processing)
// - Phase preservation option for surgical processing
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
#include <algorithm>
#include <cmath>
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
    void prepare(double sampleRate, std::size_t fftSize = kDefaultFFTSize) noexcept {
        // Clamp FFT size to valid range
        if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;
        if (fftSize > kMaxFFTSize) fftSize = kMaxFFTSize;

        // Round to nearest power of 2
        std::size_t pow2 = 1;
        while (pow2 < fftSize) pow2 <<= 1;
        if (pow2 > kMaxFFTSize) pow2 = kMaxFFTSize;
        fftSize = pow2;

        sampleRate_ = sampleRate;
        fftSize_ = fftSize;
        hopSize_ = fftSize / 2;  // 50% overlap for COLA with Hann
        numBins_ = fftSize / 2 + 1;

        // Prepare STFT analyzer
        stft_.prepare(fftSize, hopSize_, WindowType::Hann);

        // Prepare overlap-add synthesizer
        overlapAdd_.prepare(fftSize, hopSize_, WindowType::Hann);

        // Prepare spectral buffers
        inputSpectrum_.prepare(fftSize);
        outputSpectrum_.prepare(fftSize);

        // Allocate phase storage for MagnitudeOnly mode
        storedPhases_.resize(numBins_, 0.0f);

        // Allocate zero buffer for null input handling
        zeroBuffer_.resize(fftSize * 4, 0.0f);

        // Update band bins
        updateBandBins();

        prepared_ = true;
    }

    /// @brief Reset all internal state buffers (FR-002)
    /// @note Real-time safe
    void reset() noexcept {
        if (!prepared_) return;

        stft_.reset();
        overlapAdd_.reset();
        inputSpectrum_.reset();
        outputSpectrum_.reset();

        std::fill(storedPhases_.begin(), storedPhases_.end(), 0.0f);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-025)
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) return 0.0f;

        // Check for NaN/Inf using bit-level checks (works with -ffast-math)
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Push sample to STFT
        stft_.pushSamples(&input, 1);

        // Check if we can analyze
        if (stft_.canAnalyze()) {
            stft_.analyze(inputSpectrum_);
            processSpectralFrame();
            overlapAdd_.synthesize(outputSpectrum_);
        }

        // Pull output sample if available
        if (overlapAdd_.samplesAvailable() > 0) {
            float result = 0.0f;
            overlapAdd_.pullSamples(&result, 1);
            return result;
        }

        return 0.0f;
    }

    /// @brief Process a block of audio (FR-003)
    /// @param input Input buffer
    /// @param output Output buffer (may be same as input)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe, noexcept (FR-025)
    void processBlock(const float* input, float* output, std::size_t numSamples) noexcept {
        if (!prepared_) {
            if (output != nullptr) {
                std::fill(output, output + numSamples, 0.0f);
            }
            return;
        }

        // Handle nullptr input
        const float* inputPtr = input;
        if (inputPtr == nullptr) {
            inputPtr = zeroBuffer_.data();
            numSamples = std::min(numSamples, zeroBuffer_.size());
        }

        if (numSamples == 0) return;

        // Check for NaN/Inf in input
        bool hasInvalidInput = false;
        for (std::size_t i = 0; i < numSamples; ++i) {
            if (detail::isNaN(inputPtr[i]) || detail::isInf(inputPtr[i])) {
                hasInvalidInput = true;
                break;
            }
        }

        if (hasInvalidInput) {
            reset();
            if (output != nullptr) {
                std::fill(output, output + numSamples, 0.0f);
            }
            return;
        }

        // Track output position
        std::size_t outputWritten = 0;

        // Push all samples to STFT
        stft_.pushSamples(inputPtr, numSamples);

        // Process spectral frames when ready
        while (stft_.canAnalyze()) {
            stft_.analyze(inputSpectrum_);
            processSpectralFrame();
            overlapAdd_.synthesize(outputSpectrum_);

            // Pull hopSize samples immediately if available
            while (overlapAdd_.samplesAvailable() >= hopSize_ && outputWritten < numSamples) {
                std::size_t toPull = std::min(hopSize_, numSamples - outputWritten);
                overlapAdd_.pullSamples(output + outputWritten, toPull);
                outputWritten += toPull;
            }
        }

        // Fill remaining output with zeros if needed (latency warmup period)
        if (outputWritten < numSamples && output != nullptr) {
            std::fill(output + outputWritten, output + numSamples, 0.0f);
        }
    }

    // =========================================================================
    // Mode Selection
    // =========================================================================

    /// @brief Set distortion mode (FR-009)
    /// @param mode Processing mode
    void setMode(SpectralDistortionMode mode) noexcept {
        mode_ = mode;
    }

    /// @brief Get current distortion mode
    [[nodiscard]] SpectralDistortionMode getMode() const noexcept {
        return mode_;
    }

    // =========================================================================
    // Global Parameters
    // =========================================================================

    /// @brief Set global drive amount (FR-010)
    /// @param drive Drive [0.0, 10.0], where 0 = bypass
    void setDrive(float drive) noexcept {
        drive_ = std::clamp(drive, kMinDrive, kMaxDrive);
    }

    /// @brief Get current drive setting
    [[nodiscard]] float getDrive() const noexcept {
        return drive_;
    }

    /// @brief Set saturation curve (FR-011)
    /// @param curve Waveshape type from WaveshapeType enum
    void setSaturationCurve(WaveshapeType curve) noexcept {
        saturationCurve_ = curve;
        waveshaper_.setType(curve);
    }

    /// @brief Get current saturation curve
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept {
        return saturationCurve_;
    }

    /// @brief Enable/disable DC and Nyquist bin processing (FR-012)
    /// @param enabled true to process DC/Nyquist, false to exclude (default)
    void setProcessDCNyquist(bool enabled) noexcept {
        processDCNyquist_ = enabled;
    }

    /// @brief Check if DC/Nyquist processing is enabled
    [[nodiscard]] bool getProcessDCNyquist() const noexcept {
        return processDCNyquist_;
    }

    // =========================================================================
    // Bin-Selective Parameters
    // =========================================================================

    /// @brief Configure low frequency band (FR-013)
    /// @param freqHz Upper frequency limit of low band in Hz
    /// @param drive Drive amount for low band
    void setLowBand(float freqHz, float drive) noexcept {
        lowBand_.highHz = std::max(0.0f, freqHz);
        lowBand_.lowHz = 0.0f;
        lowBand_.drive = std::clamp(drive, kMinDrive, kMaxDrive);
        updateBandBins();
    }

    /// @brief Configure mid frequency band (FR-014)
    /// @param lowHz Lower frequency limit in Hz
    /// @param highHz Upper frequency limit in Hz
    /// @param drive Drive amount for mid band
    void setMidBand(float lowHz, float highHz, float drive) noexcept {
        if (lowHz > highHz) std::swap(lowHz, highHz);
        midBand_.lowHz = std::max(0.0f, lowHz);
        midBand_.highHz = highHz;
        midBand_.drive = std::clamp(drive, kMinDrive, kMaxDrive);
        updateBandBins();
    }

    /// @brief Configure high frequency band (FR-015)
    /// @param freqHz Lower frequency limit of high band in Hz
    /// @param drive Drive amount for high band
    void setHighBand(float freqHz, float drive) noexcept {
        highBand_.lowHz = std::max(0.0f, freqHz);
        highBand_.highHz = static_cast<float>(sampleRate_) / 2.0f;  // Nyquist
        highBand_.drive = std::clamp(drive, kMinDrive, kMaxDrive);
        updateBandBins();
    }

    /// @brief Set gap handling behavior (FR-016)
    /// @param mode Passthrough or UseGlobalDrive
    void setGapBehavior(GapBehavior mode) noexcept {
        gapBehavior_ = mode;
    }

    /// @brief Get current gap behavior
    [[nodiscard]] GapBehavior getGapBehavior() const noexcept {
        return gapBehavior_;
    }

    // =========================================================================
    // SpectralBitcrush Parameters
    // =========================================================================

    /// @brief Set magnitude quantization bit depth (FR-017)
    /// @param bits Bit depth [1.0, 16.0], fractional values use continuous levels = 2^bits
    void setMagnitudeBits(float bits) noexcept {
        magnitudeBits_ = std::clamp(bits, kMinBits, kMaxBits);
    }

    /// @brief Get current magnitude bit depth
    [[nodiscard]] float getMagnitudeBits() const noexcept {
        return magnitudeBits_;
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get processing latency in samples (FR-004)
    /// @return Latency equal to FFT size
    [[nodiscard]] std::size_t latency() const noexcept {
        return fftSize_;
    }

    /// @brief Get configured FFT size
    [[nodiscard]] std::size_t getFftSize() const noexcept {
        return fftSize_;
    }

    /// @brief Get number of frequency bins
    [[nodiscard]] std::size_t getNumBins() const noexcept {
        return numBins_;
    }

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

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

    /// @brief Process a single spectral frame
    void processSpectralFrame() noexcept {
        switch (mode_) {
            case SpectralDistortionMode::PerBinSaturate:
                applyPerBinSaturate();
                break;
            case SpectralDistortionMode::MagnitudeOnly:
                applyMagnitudeOnly();
                break;
            case SpectralDistortionMode::BinSelective:
                applyBinSelective();
                break;
            case SpectralDistortionMode::SpectralBitcrush:
                applySpectralBitcrush();
                break;
        }
    }

    /// @brief Apply per-bin saturation using rectangular coordinates (FR-020)
    ///
    /// This mode applies waveshaping to real and imaginary parts independently,
    /// which naturally allows phase to evolve through the nonlinear function.
    /// This creates intermodulation between components, producing a different
    /// character than MagnitudeOnly mode (which preserves phase exactly).
    void applyPerBinSaturate() noexcept {
        // FR-019: drive=0 bypasses processing
        if (drive_ == 0.0f) {
            copyInputToOutput();
            return;
        }

        const std::size_t startBin = processDCNyquist_ ? 0 : 1;
        const std::size_t endBin = processDCNyquist_ ? numBins_ : (numBins_ > 1 ? numBins_ - 1 : numBins_);

        // Copy DC bin if excluded
        if (!processDCNyquist_ && numBins_ > 0) {
            outputSpectrum_.setCartesian(0, inputSpectrum_.getReal(0), inputSpectrum_.getImag(0));
        }

        // Copy Nyquist bin if excluded
        if (!processDCNyquist_ && numBins_ > 1) {
            outputSpectrum_.setCartesian(numBins_ - 1,
                inputSpectrum_.getReal(numBins_ - 1),
                inputSpectrum_.getImag(numBins_ - 1));
        }

        // Normalization factor: FFT components scale with fftSize/2 for unity input
        const float normFactor = 2.0f / static_cast<float>(fftSize_);
        const float invNormFactor = static_cast<float>(fftSize_) / 2.0f;

        // Set waveshaper drive once (applied to normalized components)
        waveshaper_.setDrive(1.0f);

        // Process bins using rectangular coordinates (real + imaginary)
        // This allows natural phase evolution through the nonlinear function
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            float real = inputSpectrum_.getReal(bin);
            float imag = inputSpectrum_.getImag(bin);

            // Normalize to [-1, 1] range for waveshaper
            float normalizedReal = real * normFactor;
            float normalizedImag = imag * normFactor;

            // FR-020: Apply drive and waveshaper to each component independently
            // This creates coupled magnitude/phase modification through the nonlinearity
            float drivenReal = normalizedReal * drive_;
            float drivenImag = normalizedImag * drive_;

            float saturatedReal = waveshaper_.process(drivenReal);
            float saturatedImag = waveshaper_.process(drivenImag);

            // Undo drive scaling to maintain approximate unity gain
            float newNormalizedReal = saturatedReal / drive_;
            float newNormalizedImag = saturatedImag / drive_;

            // Denormalize back to spectral range
            float newReal = newNormalizedReal * invNormFactor;
            float newImag = newNormalizedImag * invNormFactor;

            // Flush denormals (FR-027)
            newReal = detail::flushDenormal(newReal);
            newImag = detail::flushDenormal(newImag);

            outputSpectrum_.setCartesian(bin, newReal, newImag);
        }
    }

    /// @brief Apply magnitude-only saturation with exact phase preservation (FR-021)
    void applyMagnitudeOnly() noexcept {
        // FR-019: drive=0 bypasses processing
        if (drive_ == 0.0f) {
            copyInputToOutput();
            return;
        }

        const std::size_t startBin = processDCNyquist_ ? 0 : 1;
        const std::size_t endBin = processDCNyquist_ ? numBins_ : (numBins_ > 1 ? numBins_ - 1 : numBins_);

        // Copy DC bin if excluded
        if (!processDCNyquist_ && numBins_ > 0) {
            outputSpectrum_.setMagnitude(0, inputSpectrum_.getMagnitude(0));
            outputSpectrum_.setPhase(0, inputSpectrum_.getPhase(0));
        }

        // Copy Nyquist bin if excluded
        if (!processDCNyquist_ && numBins_ > 1) {
            outputSpectrum_.setMagnitude(numBins_ - 1, inputSpectrum_.getMagnitude(numBins_ - 1));
            outputSpectrum_.setPhase(numBins_ - 1, inputSpectrum_.getPhase(numBins_ - 1));
        }

        // Normalization factor: FFT magnitudes scale with fftSize/2 for unity input
        const float normFactor = 2.0f / static_cast<float>(fftSize_);
        const float invNormFactor = static_cast<float>(fftSize_) / 2.0f;

        // Store phases, process magnitudes, restore phases
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            // Store original phase
            storedPhases_[bin] = inputSpectrum_.getPhase(bin);

            float magnitude = inputSpectrum_.getMagnitude(bin);

            // Normalize magnitude to [0, 1] range for waveshaper
            float normalizedMag = magnitude * normFactor;

            // Apply waveshaping to normalized magnitude
            float drivenMag = normalizedMag * drive_;
            waveshaper_.setDrive(1.0f);
            float saturatedMag = waveshaper_.process(drivenMag);
            float newNormalizedMag = saturatedMag / drive_;

            // Denormalize back to spectral magnitude range
            float newMag = newNormalizedMag * invNormFactor;

            // Flush denormals (FR-027)
            newMag = detail::flushDenormal(newMag);

            // Set magnitude and restore exact phase (SC-001)
            outputSpectrum_.setMagnitude(bin, newMag);
            outputSpectrum_.setPhase(bin, storedPhases_[bin]);
        }
    }

    /// @brief Apply bin-selective saturation with per-band drive (FR-022)
    void applyBinSelective() noexcept {
        const std::size_t startBin = processDCNyquist_ ? 0 : 1;
        const std::size_t endBin = processDCNyquist_ ? numBins_ : (numBins_ > 1 ? numBins_ - 1 : numBins_);

        // Copy DC bin if excluded
        if (!processDCNyquist_ && numBins_ > 0) {
            outputSpectrum_.setMagnitude(0, inputSpectrum_.getMagnitude(0));
            outputSpectrum_.setPhase(0, inputSpectrum_.getPhase(0));
        }

        // Copy Nyquist bin if excluded
        if (!processDCNyquist_ && numBins_ > 1) {
            outputSpectrum_.setMagnitude(numBins_ - 1, inputSpectrum_.getMagnitude(numBins_ - 1));
            outputSpectrum_.setPhase(numBins_ - 1, inputSpectrum_.getPhase(numBins_ - 1));
        }

        // Normalization factor: FFT magnitudes scale with fftSize/2 for unity input
        const float normFactor = 2.0f / static_cast<float>(fftSize_);
        const float invNormFactor = static_cast<float>(fftSize_) / 2.0f;

        // Process each bin with its band's drive
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            float magnitude = inputSpectrum_.getMagnitude(bin);
            float phase = inputSpectrum_.getPhase(bin);

            float binDrive = getDriveForBin(bin);

            // FR-019: drive=0 bypasses processing for this bin
            if (binDrive == 0.0f) {
                outputSpectrum_.setMagnitude(bin, magnitude);
                outputSpectrum_.setPhase(bin, phase);
                continue;
            }

            // Normalize magnitude to [0, 1] range for waveshaper
            float normalizedMag = magnitude * normFactor;

            // Apply waveshaping with bin's drive
            float drivenMag = normalizedMag * binDrive;
            waveshaper_.setDrive(1.0f);
            float saturatedMag = waveshaper_.process(drivenMag);
            float newNormalizedMag = saturatedMag / binDrive;

            // Denormalize back to spectral magnitude range
            float newMag = newNormalizedMag * invNormFactor;

            // Flush denormals (FR-027)
            newMag = detail::flushDenormal(newMag);

            outputSpectrum_.setMagnitude(bin, newMag);
            outputSpectrum_.setPhase(bin, phase);
        }
    }

    /// @brief Apply spectral bitcrushing (FR-024)
    void applySpectralBitcrush() noexcept {
        const std::size_t startBin = processDCNyquist_ ? 0 : 1;
        const std::size_t endBin = processDCNyquist_ ? numBins_ : (numBins_ > 1 ? numBins_ - 1 : numBins_);

        // Copy DC bin if excluded
        if (!processDCNyquist_ && numBins_ > 0) {
            outputSpectrum_.setMagnitude(0, inputSpectrum_.getMagnitude(0));
            outputSpectrum_.setPhase(0, inputSpectrum_.getPhase(0));
        }

        // Copy Nyquist bin if excluded
        if (!processDCNyquist_ && numBins_ > 1) {
            outputSpectrum_.setMagnitude(numBins_ - 1, inputSpectrum_.getMagnitude(numBins_ - 1));
            outputSpectrum_.setPhase(numBins_ - 1, inputSpectrum_.getPhase(numBins_ - 1));
        }

        // Calculate quantization levels
        // levels = 2^bits - 1 (e.g., 4 bits -> 15 levels)
        const float levels = std::pow(2.0f, magnitudeBits_) - 1.0f;
        const float invLevels = 1.0f / levels;

        // Process bins with quantization
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            // Store original phase for exact restoration (SC-001a)
            float phase = inputSpectrum_.getPhase(bin);
            float magnitude = inputSpectrum_.getMagnitude(bin);

            // Quantize magnitude: quantized = round(mag * levels) / levels
            float quantized = std::round(magnitude * levels) * invLevels;

            // Flush denormals (FR-027)
            quantized = detail::flushDenormal(quantized);

            outputSpectrum_.setMagnitude(bin, quantized);
            outputSpectrum_.setPhase(bin, phase);  // Exact phase restoration
        }
    }

    /// @brief Get drive value for a specific bin in BinSelective mode
    [[nodiscard]] float getDriveForBin(std::size_t bin) const noexcept {
        float maxDrive = -1.0f;
        bool inAnyBand = false;

        // Check low band
        if (bin >= lowBand_.lowBin && bin <= lowBand_.highBin && lowBand_.highHz > 0.0f) {
            maxDrive = std::max(maxDrive, lowBand_.drive);
            inAnyBand = true;
        }

        // Check mid band
        if (bin >= midBand_.lowBin && bin <= midBand_.highBin && midBand_.highHz > midBand_.lowHz) {
            maxDrive = std::max(maxDrive, midBand_.drive);
            inAnyBand = true;
        }

        // Check high band
        if (bin >= highBand_.lowBin && bin <= highBand_.highBin && highBand_.highHz > highBand_.lowHz) {
            maxDrive = std::max(maxDrive, highBand_.drive);
            inAnyBand = true;
        }

        // FR-016: Handle gaps
        if (!inAnyBand) {
            if (gapBehavior_ == GapBehavior::UseGlobalDrive) {
                return drive_;
            } else {
                // Passthrough: return 0 to indicate bypass
                return 0.0f;
            }
        }

        // FR-023: Return highest drive among overlapping bands
        return maxDrive;
    }

    /// @brief Update band bin indices from frequency settings
    void updateBandBins() noexcept {
        if (!prepared_) return;

        const float sampleRateF = static_cast<float>(sampleRate_);

        // Low band: 0 to highHz
        lowBand_.lowBin = 0;
        lowBand_.highBin = frequencyToBinNearest(lowBand_.highHz, fftSize_, sampleRateF);
        if (lowBand_.highBin >= numBins_) lowBand_.highBin = numBins_ - 1;

        // Mid band: lowHz to highHz
        midBand_.lowBin = frequencyToBinNearest(midBand_.lowHz, fftSize_, sampleRateF);
        midBand_.highBin = frequencyToBinNearest(midBand_.highHz, fftSize_, sampleRateF);
        if (midBand_.highBin >= numBins_) midBand_.highBin = numBins_ - 1;

        // High band: lowHz to Nyquist
        highBand_.lowBin = frequencyToBinNearest(highBand_.lowHz, fftSize_, sampleRateF);
        highBand_.highBin = numBins_ - 1;
    }

    /// @brief Copy input spectrum to output unchanged
    void copyInputToOutput() noexcept {
        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            outputSpectrum_.setMagnitude(bin, inputSpectrum_.getMagnitude(bin));
            outputSpectrum_.setPhase(bin, inputSpectrum_.getPhase(bin));
        }
    }

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
    WaveshapeType saturationCurve_ = WaveshapeType::Tanh;
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

    // Auxiliary buffers
    std::vector<float> zeroBuffer_;
};

} // namespace DSP
} // namespace Krate
