// ==============================================================================
// API Contract: DelayLine
// Layer 1: DSP Primitive
// ==============================================================================
// This file defines the public API contract for the DelayLine class.
// Implementation details may vary, but the interface must match this contract.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in read/write)
// - Principle III: Modern C++ (RAII, value semantics)
// - Principle IX: Layer 1 (no dependencies on higher layers)
// - Principle XII: Test-First (tests written before implementation)
// ==============================================================================

#pragma once

#include <cstddef>
#include <vector>

namespace Iterum {
namespace DSP {

/// @brief Real-time safe circular buffer delay line with fractional interpolation.
///
/// Provides integer, linear, and allpass interpolation modes for different use cases:
/// - read(): Integer delay, fastest, for fixed sample-aligned delays
/// - readLinear(): Fractional delay with linear interpolation, for modulated delays
/// - readAllpass(): Fractional delay with allpass interpolation, for feedback loops
///
/// @note All read/write methods are noexcept and allocation-free for real-time safety.
/// @note Memory is allocated only in prepare(), which must be called before setActive().
///
/// @example Basic usage:
/// @code
/// DelayLine delay;
/// delay.prepare(44100.0, 1.0f);  // 1 second max delay
///
/// // In audio callback:
/// delay.write(inputSample);
/// float output = delay.read(22050);  // 0.5 second delay
/// @endcode
class DelayLine {
public:
    /// @brief Default constructor. Creates an uninitialized delay line.
    /// @note prepare() must be called before use.
    DelayLine() noexcept = default;

    /// @brief Destructor.
    ~DelayLine() = default;

    // Non-copyable, movable
    DelayLine(const DelayLine&) = delete;
    DelayLine& operator=(const DelayLine&) = delete;
    DelayLine(DelayLine&&) noexcept = default;
    DelayLine& operator=(DelayLine&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (call before audio processing)
    // =========================================================================

    /// @brief Prepare the delay line for processing.
    ///
    /// Allocates the internal buffer based on sample rate and maximum delay time.
    /// Buffer is automatically sized to the next power of 2 for efficient wraparound.
    ///
    /// @param sampleRate The sample rate in Hz (e.g., 44100.0, 48000.0, 96000.0)
    /// @param maxDelaySeconds Maximum delay time in seconds (up to 10 seconds at 192kHz)
    ///
    /// @note This method allocates memory and must be called before setActive(true).
    /// @note Calling prepare() again reconfigures the delay line and clears the buffer.
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;

    /// @brief Clear the buffer to silence without reallocating.
    ///
    /// Use this when starting playback to prevent artifacts from previous audio.
    /// Faster than prepare() when buffer size doesn't need to change.
    void reset() noexcept;

    // =========================================================================
    // Processing Methods (real-time safe)
    // =========================================================================

    /// @brief Write a sample to the delay line.
    ///
    /// @param sample The input sample to write.
    ///
    /// @note Call this once per sample, before any read operations.
    /// @note O(1) time complexity, no allocations.
    void write(float sample) noexcept;

    /// @brief Read a sample at an integer delay (no interpolation).
    ///
    /// @param delaySamples Number of samples to delay (0 = current sample).
    /// @return The delayed sample value.
    ///
    /// @note Delay is clamped to [0, maxDelaySamples].
    /// @note Fastest read method; use when delay time doesn't change.
    /// @note O(1) time complexity.
    [[nodiscard]] float read(size_t delaySamples) const noexcept;

    /// @brief Read a sample at a fractional delay with linear interpolation.
    ///
    /// @param delaySamples Number of samples to delay (fractional allowed).
    /// @return The interpolated sample value.
    ///
    /// @note Delay is clamped to [0, maxDelaySamples].
    /// @note Use for LFO-modulated delays (chorus, flanger, vibrato).
    /// @note O(1) time complexity.
    [[nodiscard]] float readLinear(float delaySamples) const noexcept;

    /// @brief Read a sample at a fractional delay with allpass interpolation.
    ///
    /// @param delaySamples Number of samples to delay (fractional allowed).
    /// @return The interpolated sample value.
    ///
    /// @note Delay is clamped to [0, maxDelaySamples].
    /// @note Use ONLY for fixed delays in feedback loops.
    /// @note Do NOT use for modulated delays (causes artifacts).
    /// @note O(1) time complexity.
    /// @warning Updates internal state; call order matters in feedback networks.
    [[nodiscard]] float readAllpass(float delaySamples) noexcept;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get the maximum delay in samples.
    /// @return Maximum delay samples, or 0 if not prepared.
    [[nodiscard]] size_t maxDelaySamples() const noexcept;

    /// @brief Get the current sample rate.
    /// @return Sample rate in Hz, or 0 if not prepared.
    [[nodiscard]] double sampleRate() const noexcept;

private:
    std::vector<float> buffer_;      ///< Circular buffer (power-of-2 size)
    size_t mask_ = 0;                ///< Bitmask for wraparound (bufferSize - 1)
    size_t writeIndex_ = 0;          ///< Current write position
    float allpassState_ = 0.0f;      ///< Previous output for allpass interpolation
    double sampleRate_ = 0.0;        ///< Current sample rate
    size_t maxDelaySamples_ = 0;     ///< Maximum delay (user-requested, not buffer size)
};

} // namespace DSP
} // namespace Iterum
