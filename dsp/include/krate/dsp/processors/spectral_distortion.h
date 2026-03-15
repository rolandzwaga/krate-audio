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

/// @brief Phase handling mode for spectral output
enum class SpectralPhaseMode : uint8_t {
    Preserve = 0,   ///< Keep phases from distortion output (default)
    Random = 1,     ///< Randomize phases (noise-like textures)
    Gradient = 2,   ///< Linear phase gradient (time-shift effect)
    Temporal = 3    ///< Use previous frame's phases (smearing/freeze)
};

/// @brief Magnitude scale mode for spectral processing
enum class MagnitudeScaleMode : uint8_t {
    Linear = 0,   ///< Process magnitudes directly (default)
    Log = 1,      ///< Log-domain: compresses dynamic range before distortion
    Bark = 2,     ///< Bark-weighted: emphasizes perceptually important bands
    ERB = 3       ///< ERB-weighted: smooth perceptual frequency weighting
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

        // Allocate phase storage for Temporal phase mode
        prevFramePhases_.resize(numBins_, 0.0f);

        // Pre-compute per-bin frequency weights for tilt/scale
        binFrequencies_.resize(numBins_);
        const float binWidth = static_cast<float>(sampleRate_) / static_cast<float>(fftSize_);
        for (std::size_t i = 0; i < numBins_; ++i) {
            binFrequencies_[i] = static_cast<float>(i) * binWidth;
        }

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
        std::fill(prevFramePhases_.begin(), prevFramePhases_.end(), 0.0f);
        prngState_ = 0x12345678u;
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
    // Spectral Shaping Parameters
    // =========================================================================

    /// @brief Set spectral tilt amount
    /// @param tilt 0.0 = darken (cut highs), 0.5 = neutral, 1.0 = brighten (boost highs)
    void setTilt(float tilt) noexcept {
        tilt_ = std::clamp(tilt, 0.0f, 1.0f);
    }

    /// @brief Get current tilt
    [[nodiscard]] float getTilt() const noexcept {
        return tilt_;
    }

    /// @brief Set spectral threshold for bin gating
    /// @param threshold 0.0 = pass all bins, 1.0 = gate all bins
    void setThreshold(float threshold) noexcept {
        threshold_ = std::clamp(threshold, 0.0f, 1.0f);
    }

    /// @brief Get current threshold
    [[nodiscard]] float getThreshold() const noexcept {
        return threshold_;
    }

    /// @brief Set magnitude scale mode
    /// @param mode How magnitudes are scaled during processing
    void setMagnitudeScaleMode(MagnitudeScaleMode mode) noexcept {
        magnitudeScaleMode_ = mode;
    }

    /// @brief Get current magnitude scale mode
    [[nodiscard]] MagnitudeScaleMode getMagnitudeScaleMode() const noexcept {
        return magnitudeScaleMode_;
    }

    /// @brief Set spectral frequency parameter (normalized)
    /// @param freq 0.0 to 1.0, maps to pivot frequency for tilt (0 = low, 1 = Nyquist)
    void setFrequency(float freq) noexcept {
        frequency_ = std::clamp(freq, 0.0f, 1.0f);
    }

    /// @brief Get current frequency parameter
    [[nodiscard]] float getFrequency() const noexcept {
        return frequency_;
    }

    /// @brief Set phase handling mode
    /// @param mode How output phases are generated
    void setPhaseMode(SpectralPhaseMode mode) noexcept {
        phaseMode_ = mode;
    }

    /// @brief Get current phase mode
    [[nodiscard]] SpectralPhaseMode getPhaseMode() const noexcept {
        return phaseMode_;
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
        // Step 1: Mode-specific distortion
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

        // Step 2: Apply magnitude scale mode (post-distortion reshaping)
        if (magnitudeScaleMode_ != MagnitudeScaleMode::Linear) {
            applyMagnitudeScaleMode();
        }

        // Step 3: Apply threshold gating
        if (threshold_ > 0.0f) {
            applyThresholdGating();
        }

        // Step 4: Apply spectral tilt
        if (tilt_ != 0.5f) {
            applySpectralTilt();
        }

        // Step 5: Apply phase mode
        if (phaseMode_ != SpectralPhaseMode::Preserve) {
            applyPhaseMode();
        }

        // Step 6: Store phases for temporal mode (always, so switching is seamless)
        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            prevFramePhases_[bin] = outputSpectrum_.getPhase(bin);
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

        // Use drive_ as waveshaper pre-gain so saturation intensity scales with drive
        waveshaper_.setDrive(drive_);

        // Process bins using rectangular coordinates (real + imaginary)
        // This allows natural phase evolution through the nonlinear function
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            float real = inputSpectrum_.getReal(bin);
            float imag = inputSpectrum_.getImag(bin);

            // Normalize to [-1, 1] range for waveshaper
            float normalizedReal = real * normFactor;
            float normalizedImag = imag * normFactor;

            // FR-020: Apply waveshaper with drive to each component independently
            // The waveshaper applies drive internally: shape(drive * x)
            // This creates coupled magnitude/phase modification through the nonlinearity
            float saturatedReal = waveshaper_.process(normalizedReal);
            float saturatedImag = waveshaper_.process(normalizedImag);

            // Denormalize back to spectral range
            float newReal = saturatedReal * invNormFactor;
            float newImag = saturatedImag * invNormFactor;

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

        // Use drive_ as waveshaper pre-gain so saturation intensity scales with drive
        waveshaper_.setDrive(drive_);

        // Store phases, process magnitudes, restore phases
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            // Store original phase
            storedPhases_[bin] = inputSpectrum_.getPhase(bin);

            float magnitude = inputSpectrum_.getMagnitude(bin);

            // Normalize magnitude to [0, 1] range for waveshaper
            float normalizedMag = magnitude * normFactor;

            // Apply waveshaping with drive to normalized magnitude
            // The waveshaper applies drive internally: shape(drive * x)
            float saturatedMag = waveshaper_.process(normalizedMag);

            // Denormalize back to spectral magnitude range
            float newMag = saturatedMag * invNormFactor;

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

            // Apply waveshaping with per-bin drive
            // The waveshaper applies drive internally: shape(drive * x)
            waveshaper_.setDrive(binDrive);
            float saturatedMag = waveshaper_.process(normalizedMag);

            // Denormalize back to spectral magnitude range
            float newMag = saturatedMag * invNormFactor;

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

        // Per-frame peak normalization: find the maximum magnitude across all
        // active bins, then quantize each bin relative to that peak. This is
        // analogous to how PCM bit reduction works — the full bit depth spans
        // the signal's dynamic range regardless of absolute FFT scale.
        float peakMag = 0.0f;
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            peakMag = std::max(peakMag, inputSpectrum_.getMagnitude(bin));
        }

        // If frame is silent, pass through zeros
        if (peakMag < 1e-20f) {
            for (std::size_t bin = startBin; bin < endBin; ++bin) {
                outputSpectrum_.setMagnitude(bin, 0.0f);
                outputSpectrum_.setPhase(bin, inputSpectrum_.getPhase(bin));
            }
            return;
        }

        const float invPeakMag = 1.0f / peakMag;

        // Process bins with quantization
        for (std::size_t bin = startBin; bin < endBin; ++bin) {
            // Store original phase for exact restoration (SC-001a)
            float phase = inputSpectrum_.getPhase(bin);
            float magnitude = inputSpectrum_.getMagnitude(bin);

            // Normalize to [0, 1] relative to frame peak
            float normalizedMag = magnitude * invPeakMag;

            // Quantize: snap to nearest of (levels+1) values: 0, 1/L, 2/L, ..., 1
            float quantized = std::round(normalizedMag * levels) * invLevels;

            // Denormalize back to original magnitude scale
            float newMag = quantized * peakMag;

            // Flush denormals (FR-027)
            newMag = detail::flushDenormal(newMag);

            outputSpectrum_.setMagnitude(bin, newMag);
            outputSpectrum_.setPhase(bin, phase);  // Exact phase restoration
        }
    }

    // =========================================================================
    // Post-Processing Methods
    // =========================================================================

    /// @brief Apply threshold gating — zero out bins below threshold
    void applyThresholdGating() noexcept {
        // Find peak magnitude for relative threshold
        float peakMag = 0.0f;
        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            peakMag = std::max(peakMag, outputSpectrum_.getMagnitude(bin));
        }

        if (peakMag < 1e-20f) return;

        const float threshLevel = threshold_ * peakMag;

        for (std::size_t bin = 0; bin < numBins_; ++bin) {
            if (outputSpectrum_.getMagnitude(bin) < threshLevel) {
                outputSpectrum_.setMagnitude(bin, 0.0f);
            }
        }
    }

    /// @brief Apply spectral tilt — frequency-dependent gain
    void applySpectralTilt() noexcept {
        // tilt_ range: 0 = darken (cut highs -12dB/oct), 0.5 = neutral, 1 = brighten (+12dB/oct)
        // Map to slope in dB/octave: -12 to +12
        const float slopeDb = (tilt_ - 0.5f) * 24.0f;

        if (std::abs(slopeDb) < 0.01f) return;

        // Pivot frequency from frequency_ parameter (mapped to Hz)
        // frequency_ 0..1 maps logarithmically: 100Hz to Nyquist
        const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        const float pivotHz = 100.0f * std::pow(nyquist / 100.0f, frequency_);

        // Convert slope to per-octave linear multiplier
        // gain_at_freq = (freq / pivot)^(slope_dB / 6.02)
        // Using 6.02 dB = 1 octave doubling
        const float exponent = slopeDb / 6.02f;

        for (std::size_t bin = 1; bin < numBins_; ++bin) {
            const float freqHz = binFrequencies_[bin];
            if (freqHz < 1.0f) continue;

            const float ratio = freqHz / pivotHz;
            const float gain = std::pow(ratio, exponent);

            float mag = outputSpectrum_.getMagnitude(bin) * gain;
            mag = detail::flushDenormal(mag);
            outputSpectrum_.setMagnitude(bin, mag);
        }
    }

    /// @brief Apply magnitude scale mode post-processing
    void applyMagnitudeScaleMode() noexcept {
        switch (magnitudeScaleMode_) {
            case MagnitudeScaleMode::Linear:
                break;  // No-op

            case MagnitudeScaleMode::Log: {
                // Log compression: reduces dynamic range between loud and quiet bins
                // Maps magnitude through log(1 + x) / log(2) to compress peaks
                float peakMag = 0.0f;
                for (std::size_t bin = 0; bin < numBins_; ++bin) {
                    peakMag = std::max(peakMag, outputSpectrum_.getMagnitude(bin));
                }
                if (peakMag < 1e-20f) break;
                const float invPeak = 1.0f / peakMag;
                const float invLog2 = 1.0f / std::log(2.0f);

                for (std::size_t bin = 0; bin < numBins_; ++bin) {
                    float mag = outputSpectrum_.getMagnitude(bin);
                    float normalized = mag * invPeak;
                    float compressed = std::log(1.0f + normalized) * invLog2;
                    outputSpectrum_.setMagnitude(bin, detail::flushDenormal(compressed * peakMag));
                }
                break;
            }

            case MagnitudeScaleMode::Bark: {
                // Bark-scale emphasis: boost perceptually important 1-4kHz region
                for (std::size_t bin = 1; bin < numBins_; ++bin) {
                    const float f = binFrequencies_[bin];
                    // Bark critical band rate
                    const float bark = 13.0f * std::atan(0.00076f * f)
                                     + 3.5f * std::atan(f * f / (7500.0f * 7500.0f));
                    // Weight: peak at ~13 Bark (3.5kHz), falloff at extremes
                    // Normalize so mid-frequencies get ~1.0 gain
                    const float weight = 0.5f + 0.5f * std::exp(-0.1f * (bark - 13.0f) * (bark - 13.0f));
                    float mag = outputSpectrum_.getMagnitude(bin) * weight;
                    outputSpectrum_.setMagnitude(bin, detail::flushDenormal(mag));
                }
                break;
            }

            case MagnitudeScaleMode::ERB: {
                // ERB emphasis: smooth perceptual weighting
                for (std::size_t bin = 1; bin < numBins_; ++bin) {
                    const float f = binFrequencies_[bin];
                    // ERB rate (Glasberg & Moore 1990)
                    const float erbRate = 21.4f * std::log10(4.37f * f / 1000.0f + 1.0f);
                    // Weight: peak around ERB rate 20-25 (~2-4kHz)
                    const float weight = 0.5f + 0.5f * std::exp(-0.05f * (erbRate - 22.0f) * (erbRate - 22.0f));
                    float mag = outputSpectrum_.getMagnitude(bin) * weight;
                    outputSpectrum_.setMagnitude(bin, detail::flushDenormal(mag));
                }
                break;
            }
        }
    }

    /// @brief Apply phase mode post-processing
    void applyPhaseMode() noexcept {
        switch (phaseMode_) {
            case SpectralPhaseMode::Preserve:
                break;  // No-op

            case SpectralPhaseMode::Random: {
                // Randomize phases using fast xorshift PRNG
                for (std::size_t bin = 0; bin < numBins_; ++bin) {
                    prngState_ ^= prngState_ << 13;
                    prngState_ ^= prngState_ >> 17;
                    prngState_ ^= prngState_ << 5;
                    // Map to [-pi, pi]
                    float phase = (static_cast<float>(prngState_) / 4294967296.0f) * kTwoPi - kPi;
                    outputSpectrum_.setPhase(bin, phase);
                }
                break;
            }

            case SpectralPhaseMode::Gradient: {
                // Linear phase gradient — creates a time-shift effect
                // frequency_ controls gradient steepness (0 = no shift, 1 = max shift)
                const float maxShift = kTwoPi * 4.0f;  // Up to 4 full rotations
                const float gradient = (frequency_ - 0.5f) * 2.0f * maxShift;

                for (std::size_t bin = 0; bin < numBins_; ++bin) {
                    float phase = outputSpectrum_.getPhase(bin);
                    phase += gradient * static_cast<float>(bin) / static_cast<float>(numBins_);
                    // Wrap to [-pi, pi]
                    phase = std::fmod(phase + kPi, kTwoPi);
                    if (phase < 0.0f) phase += kTwoPi;
                    phase -= kPi;
                    outputSpectrum_.setPhase(bin, phase);
                }
                break;
            }

            case SpectralPhaseMode::Temporal: {
                // Use previous frame's phases — creates smearing/freeze effect
                for (std::size_t bin = 0; bin < numBins_; ++bin) {
                    outputSpectrum_.setPhase(bin, prevFramePhases_[bin]);
                }
                break;
            }
        }
    }

    /// @brief Simple xorshift PRNG (next value)
    /// @note NOT cryptographic — for audio randomization only
    [[nodiscard]] uint32_t nextPrng() noexcept {
        prngState_ ^= prngState_ << 13;
        prngState_ ^= prngState_ >> 17;
        prngState_ ^= prngState_ << 5;
        return prngState_;
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

    // Spectral shaping parameters
    float tilt_ = 0.5f;
    float threshold_ = 0.0f;
    MagnitudeScaleMode magnitudeScaleMode_ = MagnitudeScaleMode::Linear;
    float frequency_ = 0.5f;
    SpectralPhaseMode phaseMode_ = SpectralPhaseMode::Preserve;
    uint32_t prngState_ = 0x12345678u;

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

    // Phase storage for Temporal phase mode
    std::vector<float> prevFramePhases_;

    // Pre-computed per-bin frequencies (Hz)
    std::vector<float> binFrequencies_;

    // Auxiliary buffers
    std::vector<float> zeroBuffer_;
};

} // namespace DSP
} // namespace Krate
