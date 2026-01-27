// ==============================================================================
// API Contract: MultimodeFilter
// ==============================================================================
// Layer 2: DSP Processor - Multimode Filter
//
// This file defines the PUBLIC API for MultimodeFilter.
// Implementation must match this interface exactly.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle XII: Test-First Development
//
// Reference: specs/008-multimode-filter/spec.md
// ==============================================================================

#pragma once

#include "dsp/primitives/biquad.h"       // FilterType, Biquad
#include "dsp/primitives/smoother.h"     // OnePoleSmoother
#include "dsp/primitives/oversampler.h"  // Oversampler

#include <array>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// FilterSlope Enumeration
// =============================================================================

/// @brief Filter slope selection (applies to LP/HP/BP/Notch only)
enum class FilterSlope : uint8_t {
    Slope12dB = 1,  ///< 12 dB/octave (1 biquad stage)
    Slope24dB = 2,  ///< 24 dB/octave (2 biquad stages)
    Slope36dB = 3,  ///< 36 dB/octave (3 biquad stages)
    Slope48dB = 4   ///< 48 dB/octave (4 biquad stages)
};

/// @brief Convert slope enum to number of filter stages
[[nodiscard]] constexpr size_t slopeToStages(FilterSlope slope) noexcept {
    return static_cast<size_t>(slope);
}

/// @brief Convert slope enum to dB per octave value
[[nodiscard]] constexpr float slopeTodBPerOctave(FilterSlope slope) noexcept {
    return static_cast<float>(static_cast<size_t>(slope) * 12);
}

// =============================================================================
// MultimodeFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Complete filter module with drive
///
/// Composes Layer 1 primitives (Biquad, OnePoleSmoother, Oversampler) into
/// a unified filter processor with:
/// - 8 filter types (LP/HP/BP/Notch/Allpass/Shelf/Peak)
/// - Selectable slopes for LP/HP/BP/Notch (12/24/36/48 dB/oct)
/// - Coefficient smoothing for click-free modulation
/// - Optional pre-filter drive/saturation with oversampling
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Usage
/// @code
/// MultimodeFilter filter;
/// filter.prepare(44100.0, 512);
/// filter.setType(FilterType::Lowpass);
/// filter.setCutoff(1000.0f);
/// filter.setResonance(2.0f);
/// filter.setSlope(FilterSlope::Slope24dB);
///
/// // In process callback
/// filter.process(buffer, numSamples);
/// @endcode
class MultimodeFilter {
public:
    MultimodeFilter() noexcept = default;
    ~MultimodeFilter() noexcept = default;

    // Non-copyable (contains filter state)
    MultimodeFilter(const MultimodeFilter&) = delete;
    MultimodeFilter& operator=(const MultimodeFilter&) = delete;

    // Movable
    MultimodeFilter(MultimodeFilter&&) noexcept = default;
    MultimodeFilter& operator=(MultimodeFilter&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// @brief Prepare filter for processing
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset filter state without reallocation
    /// @note Real-time safe
    void reset() noexcept;

    // -------------------------------------------------------------------------
    // Processing
    // -------------------------------------------------------------------------

    /// @brief Process audio buffer with current settings
    /// @param buffer Audio samples (in-place processing)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe. Parameters smoothed per-block.
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process single sample (for modulation sources)
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Real-time safe. Recalculates coefficients per sample (expensive).
    [[nodiscard]] float processSample(float input) noexcept;

    // -------------------------------------------------------------------------
    // Parameter Setters (all real-time safe)
    // -------------------------------------------------------------------------

    /// @brief Set filter type
    /// @param type Filter type (Lowpass, Highpass, etc.)
    void setType(FilterType type) noexcept;

    /// @brief Set filter slope (LP/HP/BP/Notch only)
    /// @param slope Slope in dB/octave (ignored for Allpass/Shelf/Peak)
    void setSlope(FilterSlope slope) noexcept;

    /// @brief Set cutoff frequency
    /// @param hz Cutoff frequency in Hz (clamped to [20, Nyquist/2])
    void setCutoff(float hz) noexcept;

    /// @brief Set resonance (Q factor)
    /// @param q Resonance value (clamped to [0.1, 100])
    void setResonance(float q) noexcept;

    /// @brief Set gain for Shelf/Peak types
    /// @param dB Gain in decibels (clamped to [-24, +24], ignored for other types)
    void setGain(float dB) noexcept;

    /// @brief Set pre-filter drive amount
    /// @param dB Drive in decibels (0 = bypass, max 24dB)
    void setDrive(float dB) noexcept;

    /// @brief Set parameter smoothing time
    /// @param ms Smoothing time in milliseconds (0 = instant, may click)
    void setSmoothingTime(float ms) noexcept;

    // -------------------------------------------------------------------------
    // Parameter Getters
    // -------------------------------------------------------------------------

    [[nodiscard]] FilterType getType() const noexcept;
    [[nodiscard]] FilterSlope getSlope() const noexcept;
    [[nodiscard]] float getCutoff() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] float getGain() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// @brief Get processing latency in samples
    /// @return Latency from oversampler (0 for Economy mode)
    [[nodiscard]] size_t getLatency() const noexcept;

    /// @brief Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get configured sample rate
    [[nodiscard]] double sampleRate() const noexcept;

private:
    // Parameters
    FilterType type_ = FilterType::Lowpass;
    FilterSlope slope_ = FilterSlope::Slope12dB;
    float cutoff_ = 1000.0f;
    float resonance_ = 0.7071067811865476f;  // butterworthQ()
    float gain_ = 0.0f;
    float drive_ = 0.0f;
    float smoothingTime_ = 5.0f;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Filter stages (always allocate 4, use activeStages_)
    std::array<Biquad, 4> stages_;
    size_t activeStages_ = 1;

    // Parameter smoothing
    OnePoleSmoother cutoffSmooth_;
    OnePoleSmoother resonanceSmooth_;
    OnePoleSmoother gainSmooth_;
    OnePoleSmoother driveSmooth_;

    // Drive processing (2x mono oversampler)
    Oversampler<2, 1> oversampler_;
    std::vector<float> oversampledBuffer_;  // Pre-allocated in prepare()

    // Internal methods
    void updateCoefficients() noexcept;
    void applyDrive(float* buffer, size_t numSamples) noexcept;
    [[nodiscard]] size_t getActiveStages() const noexcept;
};

} // namespace DSP
} // namespace Iterum
