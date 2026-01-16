// ==============================================================================
// Layer 1: DSP Primitive - Rolling Capture Buffer
// ==============================================================================
// Continuously recording stereo circular buffer for Pattern Freeze Mode.
//
// Maintains a rolling capture of the most recent audio, allowing slices to be
// extracted at any position for playback in freeze patterns. Optimized for
// real-time operation with no allocations during write/read operations.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept on write/read, no RT allocations)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/069-pattern-freeze/data-model.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

/// @brief Continuously recording stereo circular buffer for pattern freeze
///
/// Records incoming audio in a circular buffer, allowing slices to be extracted
/// from any position for use in freeze pattern playback. The buffer maintains
/// the most recent N seconds of audio.
///
/// @note prepare() allocates memory; write/read operations are allocation-free.
/// @note All read/write methods are noexcept for real-time safety.
///
/// @example
/// @code
/// RollingCaptureBuffer buffer;
/// buffer.prepare(44100.0, 2.0f);  // 2 seconds at 44.1kHz
///
/// // In audio callback:
/// buffer.writeStereo(inputL, inputR);
///
/// // When freeze triggers:
/// if (buffer.isReady(500.0f)) {
///     buffer.extractSlice(sliceL, sliceR, sliceLength, offsetSamples);
/// }
/// @endcode
class RollingCaptureBuffer {
public:
    /// @brief Default constructor - creates uninitialized buffer
    RollingCaptureBuffer() noexcept = default;

    /// @brief Destructor
    ~RollingCaptureBuffer() = default;

    // Non-copyable, movable
    RollingCaptureBuffer(const RollingCaptureBuffer&) = delete;
    RollingCaptureBuffer& operator=(const RollingCaptureBuffer&) = delete;
    RollingCaptureBuffer(RollingCaptureBuffer&&) noexcept = default;
    RollingCaptureBuffer& operator=(RollingCaptureBuffer&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare buffer for recording
    ///
    /// Allocates circular buffer sized for the specified duration.
    /// Buffer size is rounded up to next power of 2 for efficient wraparound.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxDurationSeconds Maximum recording duration in seconds
    void prepare(double sampleRate, float maxDurationSeconds) noexcept {
        sampleRate_ = sampleRate;

        // Calculate required capacity
        const size_t requiredSamples = static_cast<size_t>(
            sampleRate * static_cast<double>(maxDurationSeconds));

        // Round up to next power of 2 for efficient wraparound
        capacity_ = nextPowerOf2(requiredSamples);

        // Allocate stereo buffers
        bufferL_.resize(capacity_, 0.0f);
        bufferR_.resize(capacity_, 0.0f);

        // Calculate bitmask for efficient modulo
        mask_ = capacity_ - 1;

        reset();
    }

    /// @brief Reset buffer state (clear content, reset write position)
    void reset() noexcept {
        std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
        writeIndex_ = 0;
        samplesWritten_ = 0;
    }

    // =========================================================================
    // Recording (Real-Time Safe)
    // =========================================================================

    /// @brief Write a stereo sample to the buffer
    ///
    /// @param left Left channel sample
    /// @param right Right channel sample
    ///
    /// @note O(1) time, no allocations, noexcept - safe for audio thread
    void writeStereo(float left, float right) noexcept {
        bufferL_[writeIndex_] = left;
        bufferR_[writeIndex_] = right;

        writeIndex_ = (writeIndex_ + 1) & mask_;

        if (samplesWritten_ < capacity_) {
            ++samplesWritten_;
        }
    }

    // =========================================================================
    // Slice Extraction (Real-Time Safe)
    // =========================================================================

    /// @brief Extract a slice of audio from the buffer
    ///
    /// Copies a contiguous slice of audio from the circular buffer to the
    /// output arrays. The slice starts at the specified offset before the
    /// current write position.
    ///
    /// @param outLeft Output buffer for left channel (must have lengthSamples capacity)
    /// @param outRight Output buffer for right channel (must have lengthSamples capacity)
    /// @param lengthSamples Number of samples to extract
    /// @param offsetSamples Start position as samples before current write position
    ///
    /// @note Offset 0 means start at most recent sample
    /// @note If requested range exceeds available data, silently clamps
    void extractSlice(float* outLeft, float* outRight, size_t lengthSamples,
                      size_t offsetSamples) const noexcept {
        if (lengthSamples == 0 || outLeft == nullptr || outRight == nullptr) {
            return;
        }

        // Clamp length to available data
        const size_t available = getAvailableSamples();
        lengthSamples = std::min(lengthSamples, available);

        if (lengthSamples == 0) {
            return;
        }

        // Clamp offset to available range
        offsetSamples = std::min(offsetSamples, available - lengthSamples);

        // Calculate start read position
        // writeIndex_ points to next write location, so most recent sample is at writeIndex_ - 1
        // offset N means go back N more samples from most recent
        const size_t startOffset = offsetSamples + lengthSamples;
        const size_t startIndex = (writeIndex_ - startOffset + capacity_) & mask_;

        // Copy samples, handling wraparound
        for (size_t i = 0; i < lengthSamples; ++i) {
            const size_t readIdx = (startIndex + i) & mask_;
            outLeft[i] = bufferL_[readIdx];
            outRight[i] = bufferR_[readIdx];
        }
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Check if buffer has enough data for the specified duration
    ///
    /// @param minDurationMs Minimum required recording duration in milliseconds
    /// @return true if at least minDurationMs of audio has been recorded
    [[nodiscard]] bool isReady(float minDurationMs) const noexcept {
        const size_t requiredSamples = static_cast<size_t>(
            sampleRate_ * static_cast<double>(minDurationMs) / 1000.0);
        return samplesWritten_ >= requiredSamples;
    }

    /// @brief Get buffer capacity in samples
    [[nodiscard]] size_t getCapacitySamples() const noexcept {
        return capacity_;
    }

    /// @brief Get sample rate
    [[nodiscard]] double getSampleRate() const noexcept {
        return sampleRate_;
    }

    /// @brief Get number of samples written since prepare/reset
    [[nodiscard]] size_t getSamplesWritten() const noexcept {
        return samplesWritten_;
    }

    /// @brief Get number of samples available for extraction
    ///
    /// Returns the lesser of samples written or buffer capacity.
    [[nodiscard]] size_t getAvailableSamples() const noexcept {
        return std::min(samplesWritten_, capacity_);
    }

private:
    /// @brief Compute next power of 2 >= n
    [[nodiscard]] static constexpr size_t nextPowerOf2(size_t n) noexcept {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    // Buffer storage
    std::vector<float> bufferL_;    ///< Left channel circular buffer
    std::vector<float> bufferR_;    ///< Right channel circular buffer

    // Buffer state
    size_t capacity_ = 0;           ///< Buffer capacity (power of 2)
    size_t mask_ = 0;               ///< Bitmask for efficient wraparound
    size_t writeIndex_ = 0;         ///< Current write position
    size_t samplesWritten_ = 0;     ///< Total samples written (capped at capacity)

    // Configuration
    double sampleRate_ = 44100.0;   ///< Sample rate in Hz
};

}  // namespace Krate::DSP
