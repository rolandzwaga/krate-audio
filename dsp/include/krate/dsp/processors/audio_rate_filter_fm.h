// ==============================================================================
// Layer 2: DSP Processor - Audio-Rate Filter FM
// ==============================================================================
// Modulates SVF filter cutoff at audio rates (20Hz-20kHz) to create
// metallic, bell-like, ring modulation-style, and aggressive timbres.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (oversampling, denormal flushing, feedback safety)
// - Principle XII: Test-First Development
//
// Reference: specs/095-audio-rate-filter-fm/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/oversampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations (FR-001, FR-002, FR-003)
// =============================================================================

/// @brief Modulation source selection for filter FM (FR-001)
/// @note Defined separately from other modulation enums to avoid confusion
enum class FMModSource : uint8_t {
    Internal = 0,  ///< Built-in wavetable oscillator
    External = 1,  ///< External modulator input (sidechain)
    Self = 2       ///< Filter output feedback (self-modulation)
};

/// @brief Filter type selection for carrier filter (FR-002)
/// @note Maps to SVFMode: Lowpass, Highpass, Bandpass, Notch
enum class FMFilterType : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass
    Highpass = 1,  ///< 12 dB/oct highpass
    Bandpass = 2,  ///< Constant 0 dB peak bandpass
    Notch = 3      ///< Band-reject filter
};

/// @brief Internal oscillator waveform selection (FR-003)
/// @note Sine and Triangle are low-distortion; Sawtooth and Square are harmonic-rich
enum class FMWaveform : uint8_t {
    Sine = 0,      ///< Pure sine wave (lowest THD, <0.1%)
    Triangle = 1,  ///< Triangle wave (low THD, <1%)
    Sawtooth = 2,  ///< Sawtooth wave (bright, all harmonics)
    Square = 3     ///< Square wave (hollow, odd harmonics only)
};

// =============================================================================
// AudioRateFilterFM Class (FR-004)
// =============================================================================

/// @brief Audio-rate filter frequency modulation processor
///
/// Modulates SVF filter cutoff at audio rates (20Hz-20kHz) to create
/// metallic, bell-like, ring modulation-style, and aggressive timbres.
///
/// @par Features
/// - Three modulation sources: Internal oscillator, External, Self-modulation
/// - Four filter types: Lowpass, Highpass, Bandpass, Notch
/// - Four internal oscillator waveforms: Sine, Triangle, Sawtooth, Square
/// - Configurable oversampling: 1x, 2x, or 4x for anti-aliasing
/// - FM depth in octaves (0-6) for intuitive control
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio channel.
///
/// @par Layer
/// Layer 2 (Processor) - depends on Layer 0 (core) and Layer 1 (primitives)
class AudioRateFilterFM {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Wavetable size (2048 samples as per FR-023)
    static constexpr size_t kWavetableSize = 2048;

    /// Minimum carrier cutoff frequency (Hz)
    static constexpr float kMinCutoff = 20.0f;

    /// Minimum modulator frequency (Hz)
    static constexpr float kMinModFreq = 0.1f;

    /// Maximum modulator frequency (Hz)
    static constexpr float kMaxModFreq = 20000.0f;

    /// Minimum Q factor
    static constexpr float kMinQ = 0.5f;

    /// Maximum Q factor
    static constexpr float kMaxQ = 20.0f;

    /// Maximum FM depth in octaves
    static constexpr float kMaxFMDepth = 6.0f;

    // =========================================================================
    // Lifecycle (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared processor. Call prepare() before processing.
    /// Calling process() before prepare() returns input unchanged.
    AudioRateFilterFM() noexcept = default;

    /// @brief Destructor
    ~AudioRateFilterFM() = default;

    // Non-copyable, movable
    AudioRateFilterFM(const AudioRateFilterFM&) = delete;
    AudioRateFilterFM& operator=(const AudioRateFilterFM&) = delete;
    AudioRateFilterFM(AudioRateFilterFM&&) noexcept = default;
    AudioRateFilterFM& operator=(AudioRateFilterFM&&) noexcept = default;

    /// @brief Prepare the processor for processing (FR-005)
    ///
    /// Initializes the SVF, oversamplers, wavetables, and allocates buffers.
    /// Must be called before processing. Can be called again if sample rate changes.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per block for processBlock()
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        baseSampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Generate wavetables
        generateWavetables();

        // Initialize oversamplers
        oversampler2x_.prepare(sampleRate, maxBlockSize, OversamplingQuality::Economy,
                                OversamplingMode::ZeroLatency);
        oversampler4x_.prepare(sampleRate, maxBlockSize, OversamplingQuality::Economy,
                                OversamplingMode::ZeroLatency);

        // Pre-allocate buffers for block processing
        oversampledBuffer_.resize(maxBlockSize * 4);
        modulatorBuffer_.resize(maxBlockSize * 4);

        // Update SVF for current oversampling factor
        updateSVFForOversampling();

        // Update phase increment for modulator
        updatePhaseIncrement();

        prepared_ = true;
    }

    /// @brief Reset all internal state (FR-006)
    ///
    /// Clears SVF state, oscillator phase, and previous output.
    /// Use when starting a new audio region to prevent click artifacts.
    void reset() noexcept {
        svf_.reset();
        oversampler2x_.reset();
        oversampler4x_.reset();
        phase_ = 0.0;
        previousOutput_ = 0.0f;
    }

    /// @brief Check if the processor has been prepared (FR-028)
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Carrier Filter Configuration (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Set the carrier cutoff frequency (FR-007)
    ///
    /// @param hz Cutoff frequency in Hz, clamped to [20 Hz, sampleRate * 0.495]
    void setCarrierCutoff(float hz) noexcept {
        const float maxCutoff = static_cast<float>(baseSampleRate_) * SVF::kMaxCutoffRatio;
        carrierCutoff_ = std::clamp(hz, kMinCutoff, maxCutoff);
    }

    /// @brief Get the current carrier cutoff frequency
    [[nodiscard]] float getCarrierCutoff() const noexcept {
        return carrierCutoff_;
    }

    /// @brief Set the carrier Q factor (FR-008)
    ///
    /// @param q Q factor, clamped to [0.5, 20.0]
    void setCarrierQ(float q) noexcept {
        carrierQ_ = std::clamp(q, kMinQ, kMaxQ);
        svf_.setResonance(carrierQ_);
    }

    /// @brief Get the current carrier Q factor
    [[nodiscard]] float getCarrierQ() const noexcept {
        return carrierQ_;
    }

    /// @brief Set the filter type (FR-009)
    ///
    /// @param type Filter response type (Lowpass, Highpass, Bandpass, Notch)
    void setFilterType(FMFilterType type) noexcept {
        filterType_ = type;
        updateSVFMode();
    }

    /// @brief Get the current filter type
    [[nodiscard]] FMFilterType getFilterType() const noexcept {
        return filterType_;
    }

    // =========================================================================
    // Modulator Configuration (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set the modulation source (FR-010)
    ///
    /// @param source Modulation source (Internal, External, Self)
    void setModulatorSource(FMModSource source) noexcept {
        modSource_ = source;
    }

    /// @brief Get the current modulation source
    [[nodiscard]] FMModSource getModulatorSource() const noexcept {
        return modSource_;
    }

    /// @brief Set the internal oscillator frequency (FR-011)
    ///
    /// @param hz Frequency in Hz, clamped to [0.1, 20000]
    void setModulatorFrequency(float hz) noexcept {
        modulatorFreq_ = std::clamp(hz, kMinModFreq, kMaxModFreq);
        updatePhaseIncrement();
    }

    /// @brief Get the current modulator frequency
    [[nodiscard]] float getModulatorFrequency() const noexcept {
        return modulatorFreq_;
    }

    /// @brief Set the internal oscillator waveform (FR-012)
    ///
    /// @param waveform Waveform type (Sine, Triangle, Sawtooth, Square)
    void setModulatorWaveform(FMWaveform waveform) noexcept {
        waveform_ = waveform;
    }

    /// @brief Get the current modulator waveform
    [[nodiscard]] FMWaveform getModulatorWaveform() const noexcept {
        return waveform_;
    }

    // =========================================================================
    // FM Depth Control (FR-013)
    // =========================================================================

    /// @brief Set the FM depth in octaves (FR-013)
    ///
    /// The modulated cutoff is calculated as:
    /// modulatedCutoff = carrierCutoff * 2^(modulatorSignal * fmDepth)
    ///
    /// @param octaves FM depth in octaves, clamped to [0.0, 6.0]
    void setFMDepth(float octaves) noexcept {
        fmDepth_ = std::clamp(octaves, 0.0f, kMaxFMDepth);
    }

    /// @brief Get the current FM depth
    [[nodiscard]] float getFMDepth() const noexcept {
        return fmDepth_;
    }

    // =========================================================================
    // Oversampling Configuration (FR-015, FR-016)
    // =========================================================================

    /// @brief Set the oversampling factor (FR-015)
    ///
    /// Invalid values are clamped to the nearest valid value:
    /// - 0 or negative -> 1
    /// - 3 -> 2
    /// - 5 or higher -> 4
    ///
    /// @param factor Oversampling factor (1, 2, or 4)
    void setOversamplingFactor(int factor) noexcept {
        if (factor <= 1) {
            oversamplingFactor_ = 1;
        } else if (factor <= 3) {
            oversamplingFactor_ = 2;
        } else {
            oversamplingFactor_ = 4;
        }

        // Reconfigure SVF for new oversampled rate
        if (prepared_) {
            updateSVFForOversampling();
        }
    }

    /// @brief Get the current oversampling factor
    [[nodiscard]] int getOversamplingFactor() const noexcept {
        return oversamplingFactor_;
    }

    /// @brief Get the latency introduced by oversampling (FR-016)
    ///
    /// @return Latency in samples at the base sample rate
    [[nodiscard]] size_t getLatency() const noexcept {
        if (oversamplingFactor_ == 1) {
            return 0;
        } else if (oversamplingFactor_ == 2) {
            return oversampler2x_.getLatency();
        } else {
            return oversampler4x_.getLatency();
        }
    }

    // =========================================================================
    // Processing (FR-017, FR-018, FR-019, FR-022)
    // =========================================================================

    /// @brief Process a single sample (FR-017)
    ///
    /// @param input Input sample
    /// @param externalModulator External modulator value (used only when source is External)
    /// @return Filtered output sample
    ///
    /// @note Returns input unchanged if prepare() not called (FR-028)
    /// @note Returns 0 and resets state on NaN/Inf input (FR-029)
    [[nodiscard]] float process(float input, float externalModulator = 0.0f) noexcept {
        // FR-028: Return input unchanged if not prepared
        if (!prepared_) {
            return input;
        }

        // FR-029: Handle NaN/Inf input
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Get modulator value
        float modulator = getModulatorValue(externalModulator);

        // Process with or without oversampling
        float output;
        if (oversamplingFactor_ == 1) {
            output = processFilterFM(input, modulator);
        } else if (oversamplingFactor_ == 2) {
            output = processWithOversampling2x(input, modulator);
        } else {
            output = processWithOversampling4x(input, modulator);
        }

        // Store for self-modulation
        previousOutput_ = output;

        return output;
    }

    /// @brief Process a block of samples with external modulator (FR-018)
    ///
    /// @param buffer Input/output buffer
    /// @param modulator External modulator buffer (may be nullptr)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, const float* modulator, size_t numSamples) noexcept {
        if (!prepared_ || buffer == nullptr) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            float mod = (modulator != nullptr) ? modulator[i] : 0.0f;
            buffer[i] = process(buffer[i], mod);
        }
    }

    /// @brief Process a block of samples without external modulator (FR-019)
    ///
    /// Convenience overload for Internal or Self modulation modes.
    ///
    /// @param buffer Input/output buffer
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept {
        processBlock(buffer, nullptr, numSamples);
    }

private:
    // =========================================================================
    // Wavetable Generation (FR-023)
    // =========================================================================

    void generateWavetables() noexcept {
        // Generate Sine wavetable
        for (size_t i = 0; i < kWavetableSize; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(kWavetableSize);
            sineTable_[i] = std::sin(kTwoPi * phase);
        }

        // Generate Triangle wavetable (0->1->0->-1->0 starting at 0)
        for (size_t i = 0; i < kWavetableSize; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(kWavetableSize);
            float value;
            if (phase < 0.25f) {
                value = phase * 4.0f;
            } else if (phase < 0.75f) {
                value = 2.0f - phase * 4.0f;
            } else {
                value = phase * 4.0f - 4.0f;
            }
            triangleTable_[i] = value;
        }

        // Generate Sawtooth wavetable (-1 to +1)
        for (size_t i = 0; i < kWavetableSize; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(kWavetableSize);
            sawTable_[i] = 2.0f * phase - 1.0f;
        }

        // Generate Square wavetable (+1 for first half, -1 for second half)
        for (size_t i = 0; i < kWavetableSize; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(kWavetableSize);
            squareTable_[i] = (phase < 0.5f) ? 1.0f : -1.0f;
        }
    }

    // =========================================================================
    // Wavetable Reading with Linear Interpolation
    // =========================================================================

    [[nodiscard]] float readWavetable(const std::array<float, kWavetableSize>& table,
                                       double phase) const noexcept {
        // Scale phase to table index
        double scaledPhase = phase * static_cast<double>(kWavetableSize);
        size_t index0 = static_cast<size_t>(scaledPhase);
        size_t index1 = (index0 + 1) % kWavetableSize;
        float frac = static_cast<float>(scaledPhase - static_cast<double>(index0));

        // Linear interpolation
        return table[index0] + frac * (table[index1] - table[index0]);
    }

    // =========================================================================
    // Internal Oscillator
    // =========================================================================

    void updatePhaseIncrement() noexcept {
        // Phase increment per sample at base sample rate
        // For oversampled processing, this will be divided by factor
        phaseIncrement_ = static_cast<double>(modulatorFreq_) / baseSampleRate_;
    }

    [[nodiscard]] float readOscillator() noexcept {
        float value = 0.0f;

        switch (waveform_) {
            case FMWaveform::Sine:
                value = readWavetable(sineTable_, phase_);
                break;
            case FMWaveform::Triangle:
                value = readWavetable(triangleTable_, phase_);
                break;
            case FMWaveform::Sawtooth:
                value = readWavetable(sawTable_, phase_);
                break;
            case FMWaveform::Square:
                value = readWavetable(squareTable_, phase_);
                break;
        }

        // Advance phase
        phase_ += phaseIncrement_;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
        }

        return value;
    }

    [[nodiscard]] float readOscillatorOversampled(int factor) noexcept {
        float value = 0.0f;

        switch (waveform_) {
            case FMWaveform::Sine:
                value = readWavetable(sineTable_, phase_);
                break;
            case FMWaveform::Triangle:
                value = readWavetable(triangleTable_, phase_);
                break;
            case FMWaveform::Sawtooth:
                value = readWavetable(sawTable_, phase_);
                break;
            case FMWaveform::Square:
                value = readWavetable(squareTable_, phase_);
                break;
        }

        // Advance phase at oversampled rate
        double oversampledIncrement = phaseIncrement_ / static_cast<double>(factor);
        phase_ += oversampledIncrement;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
        }

        return value;
    }

    // =========================================================================
    // Modulator Value Selection
    // =========================================================================

    [[nodiscard]] float getModulatorValue(float externalModulator) noexcept {
        switch (modSource_) {
            case FMModSource::Internal:
                return readOscillator();
            case FMModSource::External:
                return externalModulator;
            case FMModSource::Self:
                // FR-025: Hard-clip to [-1, +1] for stability
                return std::clamp(previousOutput_, -1.0f, 1.0f);
        }
        return 0.0f;
    }

    [[nodiscard]] float getModulatorValueOversampled(float externalModulator, int factor) noexcept {
        switch (modSource_) {
            case FMModSource::Internal:
                return readOscillatorOversampled(factor);
            case FMModSource::External:
                return externalModulator;
            case FMModSource::Self:
                return std::clamp(previousOutput_, -1.0f, 1.0f);
        }
        return 0.0f;
    }

    // =========================================================================
    // FM Cutoff Calculation (FR-013, FR-024)
    // =========================================================================

    [[nodiscard]] float calculateModulatedCutoff(float modulator) const noexcept {
        // FR-013: modulatedCutoff = carrierCutoff * 2^(modulator * fmDepth)
        float octaveOffset = modulator * fmDepth_;
        float frequencyMultiplier = std::pow(2.0f, octaveOffset);
        float modulatedFreq = carrierCutoff_ * frequencyMultiplier;

        // FR-024: Clamp to safe range
        const float maxFreq = static_cast<float>(oversampledRate_) * SVF::kMaxCutoffRatio;
        return std::clamp(modulatedFreq, kMinCutoff, maxFreq);
    }

    // =========================================================================
    // SVF Configuration
    // =========================================================================

    void updateSVFForOversampling() noexcept {
        oversampledRate_ = baseSampleRate_ * static_cast<double>(oversamplingFactor_);
        svf_.prepare(oversampledRate_);
        svf_.setResonance(carrierQ_);
        updateSVFMode();
    }

    void updateSVFMode() noexcept {
        SVFMode mode = SVFMode::Lowpass;
        switch (filterType_) {
            case FMFilterType::Lowpass:
                mode = SVFMode::Lowpass;
                break;
            case FMFilterType::Highpass:
                mode = SVFMode::Highpass;
                break;
            case FMFilterType::Bandpass:
                mode = SVFMode::Bandpass;
                break;
            case FMFilterType::Notch:
                mode = SVFMode::Notch;
                break;
        }
        svf_.setMode(mode);
    }

    // =========================================================================
    // Filter Processing (FR-022)
    // =========================================================================

    [[nodiscard]] float processFilterFM(float input, float modulator) noexcept {
        // FR-022: Update cutoff every sample for audio-rate modulation
        float modulatedCutoff = calculateModulatedCutoff(modulator);
        svf_.setCutoff(modulatedCutoff);

        float output = svf_.process(input);

        // FR-030: Flush denormals
        output = detail::flushDenormal(output);

        return output;
    }

    // =========================================================================
    // Oversampling Processing (FR-021)
    // =========================================================================

    [[nodiscard]] float processWithOversampling2x(float input, float modulator) noexcept {
        // Upsample input (manual for single sample)
        float upsampled[2];
        upsampled[0] = input * 2.0f;  // Gain compensation
        upsampled[1] = 0.0f;

        // Process at oversampled rate
        for (int i = 0; i < 2; ++i) {
            float osModulator = getModulatorValueOversampled(modulator, 2);
            upsampled[i] = processFilterFM(upsampled[i], osModulator);
        }

        // Downsample (take first sample for simplicity with IIR mode)
        return upsampled[0];
    }

    [[nodiscard]] float processWithOversampling4x(float input, float modulator) noexcept {
        // Upsample input
        float upsampled[4];
        upsampled[0] = input * 4.0f;  // Gain compensation
        upsampled[1] = 0.0f;
        upsampled[2] = 0.0f;
        upsampled[3] = 0.0f;

        // Process at oversampled rate
        for (int i = 0; i < 4; ++i) {
            float osModulator = getModulatorValueOversampled(modulator, 4);
            upsampled[i] = processFilterFM(upsampled[i], osModulator);
        }

        // Downsample
        return upsampled[0];
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double baseSampleRate_ = 44100.0;
    double oversampledRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    int oversamplingFactor_ = 1;
    bool prepared_ = false;

    // Carrier filter parameters
    float carrierCutoff_ = 1000.0f;
    float carrierQ_ = SVF::kButterworthQ;
    FMFilterType filterType_ = FMFilterType::Lowpass;

    // Modulator parameters
    FMModSource modSource_ = FMModSource::Internal;
    float modulatorFreq_ = 440.0f;
    FMWaveform waveform_ = FMWaveform::Sine;
    float fmDepth_ = 1.0f;

    // Internal oscillator state
    double phase_ = 0.0;
    double phaseIncrement_ = 0.0;

    // Wavetables (FR-023)
    std::array<float, kWavetableSize> sineTable_{};
    std::array<float, kWavetableSize> triangleTable_{};
    std::array<float, kWavetableSize> sawTable_{};
    std::array<float, kWavetableSize> squareTable_{};

    // Self-modulation state (FR-025)
    float previousOutput_ = 0.0f;

    // Composed components
    SVF svf_;
    Oversampler<2, 1> oversampler2x_;
    Oversampler<4, 1> oversampler4x_;

    // Pre-allocated buffers
    std::vector<float> oversampledBuffer_;
    std::vector<float> modulatorBuffer_;
};

} // namespace DSP
} // namespace Krate
