// ==============================================================================
// Layer 1: DSP Primitive - Oversampler
// ==============================================================================
// Upsampling/downsampling primitive for anti-aliased nonlinear processing.
// Supports 2x and 4x oversampling with configurable filter quality and latency modes.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library / BiquadCascade)
// - Principle X: DSP Constraints (anti-aliasing for nonlinearities, denormal flushing)
// - Principle XII: Test-First Development
//
// Reference: specs/006-oversampler/spec.md
// ==============================================================================

#pragma once

#include "biquad.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace Iterum {
namespace DSP {

// =============================================================================
// Forward Declarations
// =============================================================================

template<size_t Factor, size_t NumChannels> class Oversampler;

// =============================================================================
// Enumerations
// =============================================================================

/// Oversampling factor
enum class OversamplingFactor : uint8_t {
    TwoX = 2,   ///< 2x oversampling (44.1k -> 88.2k)
    FourX = 4   ///< 4x oversampling (44.1k -> 176.4k)
};

/// Filter quality preset affecting stopband rejection and latency
enum class OversamplingQuality : uint8_t {
    Economy,   ///< IIR 8-pole, ~48dB stopband, 0 latency
    Standard,  ///< FIR 31-tap, ~80dB stopband, minimal latency
    High       ///< FIR 63-tap, ~100dB stopband, more latency
};

/// Latency/phase mode
enum class OversamplingMode : uint8_t {
    ZeroLatency,  ///< IIR filters (minimum-phase, no latency)
    LinearPhase   ///< FIR filters (symmetric, adds latency)
};

// =============================================================================
// Oversampler Class Template
// =============================================================================

/// @brief Upsampling/downsampling primitive for anti-aliased nonlinear processing.
///
/// @tparam Factor Oversampling factor (2 or 4)
/// @tparam NumChannels Number of audio channels (1 = mono, 2 = stereo)
template<size_t Factor = 2, size_t NumChannels = 2>
class Oversampler {
public:
    static_assert(Factor == 2 || Factor == 4, "Oversampler only supports 2x or 4x");
    static_assert(NumChannels >= 1 && NumChannels <= 2, "Oversampler supports 1-2 channels");

    // =========================================================================
    // Type Aliases
    // =========================================================================

    /// Callback type for stereo processing at oversampled rate
    using StereoCallback = std::function<void(float*, float*, size_t)>;

    /// Callback type for mono processing at oversampled rate
    using MonoCallback = std::function<void(float*, size_t)>;

    // =========================================================================
    // Constants
    // =========================================================================

    /// Oversampling factor as integer
    static constexpr size_t factor() noexcept {
        return Factor;
    }

    /// Number of cascaded 2x stages (1 for 2x, 2 for 4x)
    static constexpr size_t numStages() noexcept {
        return (Factor == 2) ? 1 : 2;
    }

    /// Number of channels
    static constexpr size_t numChannels() noexcept {
        return NumChannels;
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor (must call prepare() before use)
    Oversampler() noexcept = default;

    /// Destructor
    ~Oversampler() = default;

    // Non-copyable (contains filter state)
    Oversampler(const Oversampler&) = delete;
    Oversampler& operator=(const Oversampler&) = delete;

    // Movable
    Oversampler(Oversampler&&) noexcept = default;
    Oversampler& operator=(Oversampler&&) noexcept = default;

    // =========================================================================
    // Configuration (call before processing)
    // =========================================================================

    /// Prepare oversampler for processing.
    /// @param sampleRate Base sample rate in Hz (e.g., 44100)
    /// @param maxBlockSize Maximum samples per channel per process call
    /// @param quality Filter quality preset (default: Economy for IIR)
    /// @param mode Latency mode (default: ZeroLatency)
    /// @note NOT real-time safe (allocates memory)
    void prepare(
        double sampleRate,
        size_t maxBlockSize,
        OversamplingQuality quality = OversamplingQuality::Economy,
        OversamplingMode mode = OversamplingMode::ZeroLatency
    ) noexcept;

    /// Check if oversampler has been prepared
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// Get oversampling factor (2 or 4)
    [[nodiscard]] constexpr size_t getFactor() const noexcept { return Factor; }

    /// Get latency introduced by oversampling (in base-rate samples)
    [[nodiscard]] size_t getLatency() const noexcept { return latencySamples_; }

    /// Get current quality setting
    [[nodiscard]] OversamplingQuality getQuality() const noexcept { return quality_; }

    /// Get current mode setting
    [[nodiscard]] OversamplingMode getMode() const noexcept { return mode_; }

    // =========================================================================
    // Processing (real-time safe)
    // =========================================================================

    /// Process stereo audio with oversampling.
    /// @param left Left channel buffer (input/output)
    /// @param right Right channel buffer (input/output)
    /// @param numSamples Number of samples per channel
    /// @param callback User function applied at oversampled rate
    void process(
        float* left,
        float* right,
        size_t numSamples,
        const StereoCallback& callback
    ) noexcept;

    /// Process mono audio with oversampling.
    /// @param buffer Input/output buffer
    /// @param numSamples Number of samples
    /// @param callback User function applied at oversampled rate
    void process(
        float* buffer,
        size_t numSamples,
        const MonoCallback& callback
    ) noexcept;

    // =========================================================================
    // Low-Level Access
    // =========================================================================

    /// Upsample only (for manual processing pipeline)
    void upsample(const float* input, float* output, size_t numSamples, size_t channel = 0) noexcept;

    /// Downsample only (for manual processing pipeline)
    void downsample(const float* input, float* output, size_t numSamples, size_t channel = 0) noexcept;

    /// Get pointer to internal upsampled buffer
    [[nodiscard]] float* getOversampledBuffer(size_t channel = 0) noexcept;

    /// Get size of oversampled buffer
    [[nodiscard]] size_t getOversampledBufferSize() const noexcept;

    // =========================================================================
    // State Management
    // =========================================================================

    /// Clear all filter states
    void reset() noexcept;

private:
    // Configuration
    OversamplingQuality quality_ = OversamplingQuality::Economy;
    OversamplingMode mode_ = OversamplingMode::ZeroLatency;
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    size_t latencySamples_ = 0;
    bool prepared_ = false;

    // IIR filters for Economy/ZeroLatency mode (per channel, per stage)
    static constexpr size_t kNumStages = numStages();
    std::array<BiquadCascade<4>, NumChannels * kNumStages> upsampleFilters_;
    std::array<BiquadCascade<4>, NumChannels * kNumStages> downsampleFilters_;

    // Pre-allocated buffers
    std::vector<float> oversampledBuffer_;  // Size: maxBlockSize * Factor * NumChannels

    // Internal helpers
    void configureFilters() noexcept;
    size_t getFilterIndex(size_t channel, size_t stage) const noexcept {
        return channel * kNumStages + stage;
    }
};

// =============================================================================
// Common Type Aliases
// =============================================================================

/// 2x stereo oversampler (most common configuration)
using Oversampler2x = Oversampler<2, 2>;

/// 4x stereo oversampler (for heavy distortion)
using Oversampler4x = Oversampler<4, 2>;

/// 2x mono oversampler
using Oversampler2xMono = Oversampler<2, 1>;

/// 4x mono oversampler
using Oversampler4xMono = Oversampler<4, 1>;

// =============================================================================
// Implementation
// =============================================================================

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::prepare(
    double sampleRate,
    size_t maxBlockSize,
    OversamplingQuality quality,
    OversamplingMode mode
) noexcept {
    // Validate sample rate
    if (sampleRate <= 0.0) {
        return;
    }

    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    quality_ = quality;
    mode_ = mode;

    // For now, Economy/ZeroLatency always uses IIR (0 latency)
    // Standard/High with LinearPhase would use FIR (future implementation)
    if (mode_ == OversamplingMode::ZeroLatency || quality_ == OversamplingQuality::Economy) {
        latencySamples_ = 0;
    } else {
        // FIR latency will be implemented in US3
        latencySamples_ = 0;  // Placeholder
    }

    // Allocate oversampled buffer
    const size_t bufferSize = maxBlockSize_ * Factor * NumChannels;
    oversampledBuffer_.resize(bufferSize);
    std::fill(oversampledBuffer_.begin(), oversampledBuffer_.end(), 0.0f);

    // Configure anti-aliasing filters
    configureFilters();

    prepared_ = true;
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::configureFilters() noexcept {
    // Calculate cutoff frequency for anti-aliasing
    // Nyquist at base rate, but we filter at oversampled rate
    // Cutoff should be just below original Nyquist
    const float baseCutoff = static_cast<float>(sampleRate_) * 0.45f;  // 45% of base Nyquist

    // Configure each stage
    for (size_t stage = 0; stage < kNumStages; ++stage) {
        // Sample rate at this stage
        const double stageSampleRate = sampleRate_ * (1 << (stage + 1));
        const float stageCutoff = baseCutoff;  // Same cutoff in Hz

        for (size_t ch = 0; ch < NumChannels; ++ch) {
            const size_t idx = getFilterIndex(ch, stage);

            // Configure as Butterworth lowpass
            upsampleFilters_[idx].setButterworth(
                FilterType::Lowpass,
                stageCutoff,
                static_cast<float>(stageSampleRate)
            );

            downsampleFilters_[idx].setButterworth(
                FilterType::Lowpass,
                stageCutoff,
                static_cast<float>(stageSampleRate)
            );
        }
    }

    // Reset filter states
    reset();
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::reset() noexcept {
    for (auto& filter : upsampleFilters_) {
        filter.reset();
    }
    for (auto& filter : downsampleFilters_) {
        filter.reset();
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::upsample(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    if (!prepared_ || channel >= NumChannels) {
        // Fill with zeros if not prepared
        std::fill(output, output + numSamples * Factor, 0.0f);
        return;
    }

    if constexpr (Factor == 2) {
        // Single 2x stage: zero-stuff then filter
        const size_t idx = getFilterIndex(channel, 0);

        for (size_t i = 0; i < numSamples; ++i) {
            // Zero-stuffing: insert sample, then zero
            output[i * 2] = input[i] * 2.0f;  // Gain compensation
            output[i * 2 + 1] = 0.0f;
        }

        // Apply lowpass filter
        upsampleFilters_[idx].processBlock(output, numSamples * 2);
    } else {
        // 4x: Two cascaded 2x stages
        // Stage 1: 1x -> 2x
        const size_t idx0 = getFilterIndex(channel, 0);
        float* intermediate = output;  // Reuse output buffer

        for (size_t i = 0; i < numSamples; ++i) {
            intermediate[i * 2] = input[i] * 2.0f;
            intermediate[i * 2 + 1] = 0.0f;
        }
        upsampleFilters_[idx0].processBlock(intermediate, numSamples * 2);

        // Stage 2: 2x -> 4x (in-place expansion, work backwards)
        const size_t idx1 = getFilterIndex(channel, 1);
        for (size_t i = numSamples * 2; i > 0; --i) {
            const size_t srcIdx = i - 1;
            const size_t dstIdx = (i - 1) * 2;
            output[dstIdx + 1] = 0.0f;
            output[dstIdx] = intermediate[srcIdx] * 2.0f;
        }
        upsampleFilters_[idx1].processBlock(output, numSamples * 4);
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::downsample(
    const float* input,
    float* output,
    size_t numSamples,
    size_t channel
) noexcept {
    if (!prepared_ || channel >= NumChannels) {
        std::fill(output, output + numSamples, 0.0f);
        return;
    }

    // We need a temporary buffer for filtering since input is const
    // Use part of oversampledBuffer_ that's not being used
    float* temp = oversampledBuffer_.data() + (channel * maxBlockSize_ * Factor);
    std::copy(input, input + numSamples * Factor, temp);

    if constexpr (Factor == 2) {
        // Single 2x stage: filter then decimate
        const size_t idx = getFilterIndex(channel, 0);

        // Apply lowpass filter
        downsampleFilters_[idx].processBlock(temp, numSamples * 2);

        // Decimate: take every 2nd sample
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = temp[i * 2];
        }
    } else {
        // 4x: Two cascaded 2x stages (reverse order)
        // Stage 1: 4x -> 2x
        const size_t idx1 = getFilterIndex(channel, 1);
        downsampleFilters_[idx1].processBlock(temp, numSamples * 4);

        // Decimate to 2x
        for (size_t i = 0; i < numSamples * 2; ++i) {
            temp[i] = temp[i * 2];
        }

        // Stage 2: 2x -> 1x
        const size_t idx0 = getFilterIndex(channel, 0);
        downsampleFilters_[idx0].processBlock(temp, numSamples * 2);

        // Final decimation
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = temp[i * 2];
        }
    }
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::process(
    float* left,
    float* right,
    size_t numSamples,
    const StereoCallback& callback
) noexcept {
    static_assert(NumChannels == 2, "Stereo process requires 2 channels");

    if (!prepared_ || numSamples > maxBlockSize_) {
        return;
    }

    const size_t oversampledSize = numSamples * Factor;
    float* osLeft = oversampledBuffer_.data();
    float* osRight = oversampledBuffer_.data() + maxBlockSize_ * Factor;

    // Upsample
    upsample(left, osLeft, numSamples, 0);
    upsample(right, osRight, numSamples, 1);

    // Apply user callback at oversampled rate
    if (callback) {
        callback(osLeft, osRight, oversampledSize);
    }

    // Downsample
    downsample(osLeft, left, numSamples, 0);
    downsample(osRight, right, numSamples, 1);
}

template<size_t Factor, size_t NumChannels>
void Oversampler<Factor, NumChannels>::process(
    float* buffer,
    size_t numSamples,
    const MonoCallback& callback
) noexcept {
    if (!prepared_ || numSamples > maxBlockSize_) {
        return;
    }

    const size_t oversampledSize = numSamples * Factor;
    float* osBuffer = oversampledBuffer_.data();

    // Upsample
    upsample(buffer, osBuffer, numSamples, 0);

    // Apply user callback at oversampled rate
    if (callback) {
        callback(osBuffer, oversampledSize);
    }

    // Downsample
    downsample(osBuffer, buffer, numSamples, 0);
}

template<size_t Factor, size_t NumChannels>
float* Oversampler<Factor, NumChannels>::getOversampledBuffer(size_t channel) noexcept {
    if (channel >= NumChannels || oversampledBuffer_.empty()) {
        return nullptr;
    }
    return oversampledBuffer_.data() + (channel * maxBlockSize_ * Factor);
}

template<size_t Factor, size_t NumChannels>
size_t Oversampler<Factor, NumChannels>::getOversampledBufferSize() const noexcept {
    return maxBlockSize_ * Factor;
}

} // namespace DSP
} // namespace Iterum
