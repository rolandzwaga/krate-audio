// ==============================================================================
// Layer 1: DSP Primitive - ReverseBuffer
// ==============================================================================
// Double-buffer system for capturing audio and playing back in reverse.
// Supports smooth crossfade transitions between chunks.
//
// Constitution Principle IX: Layered Architecture
// - Layer 1 primitives have no dependencies on Layer 2+ components
// ==============================================================================

#pragma once

#include <cstddef>
#include <vector>

namespace Iterum::DSP {

/// @brief Double-buffer for reverse playback with crossfade support
///
/// ReverseBuffer captures audio into one buffer while playing back from another,
/// then swaps at chunk boundaries. When reversed=true, playback reads from
/// end to start of the captured buffer, creating backwards audio.
///
/// @note This is a stub implementation. Tests should FAIL until implemented.
class ReverseBuffer {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare the buffer with sample rate and maximum chunk size
    /// @param sampleRate Sample rate in Hz
    /// @param maxChunkMs Maximum chunk size in milliseconds
    void prepare(double sampleRate, float maxChunkMs) noexcept;

    /// @brief Reset buffer state (clear audio, reset positions)
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the chunk size in milliseconds
    /// @param ms Chunk size (10-2000ms, clamped to prepared maximum)
    void setChunkSizeMs(float ms) noexcept;

    /// @brief Set the crossfade duration in milliseconds
    /// @param ms Crossfade duration (0 = no crossfade)
    void setCrossfadeMs(float ms) noexcept;

    /// @brief Set playback direction (true = reverse, false = forward)
    /// @param reversed If true, playback is reversed
    void setReversed(bool reversed) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample to capture
    /// @return Output sample from playback buffer (reversed if enabled)
    [[nodiscard]] float process(float input) noexcept;

    // =========================================================================
    // State Queries
    // =========================================================================

    /// @brief Check if currently at a chunk boundary
    /// @return true if at chunk boundary (just swapped buffers)
    [[nodiscard]] bool isAtChunkBoundary() const noexcept;

    /// @brief Get current chunk size in milliseconds
    [[nodiscard]] float getChunkSizeMs() const noexcept;

    /// @brief Get latency in samples (equals chunk size)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

private:
    // Double buffers for capture and playback
    std::vector<float> bufferA_;
    std::vector<float> bufferB_;

    // State
    std::size_t writePos_ = 0;
    std::size_t readPos_ = 0;
    std::size_t chunkSizeSamples_ = 0;
    std::size_t maxChunkSamples_ = 0;
    bool activeBufferIsA_ = true;
    bool reversed_ = true;
    bool atChunkBoundary_ = false;

    // Crossfade
    float crossfadeMs_ = 20.0f;
    std::size_t crossfadeSamples_ = 0;
    std::size_t crossfadePos_ = 0;

    // Configuration
    double sampleRate_ = 44100.0;
    float chunkSizeMs_ = 500.0f;
};

// =============================================================================
// Inline Implementation (Stub - tests should FAIL)
// =============================================================================

inline void ReverseBuffer::prepare(double sampleRate, float maxChunkMs) noexcept {
    sampleRate_ = sampleRate;
    chunkSizeMs_ = maxChunkMs;
    chunkSizeSamples_ = static_cast<std::size_t>(sampleRate * maxChunkMs / 1000.0);
    maxChunkSamples_ = chunkSizeSamples_;

    // Pre-allocate buffers
    bufferA_.resize(maxChunkSamples_, 0.0f);
    bufferB_.resize(maxChunkSamples_, 0.0f);

    reset();
}

inline void ReverseBuffer::reset() noexcept {
    writePos_ = 0;
    readPos_ = 0;
    crossfadePos_ = 0;
    atChunkBoundary_ = false;
    activeBufferIsA_ = true;

    // Clear buffers
    std::fill(bufferA_.begin(), bufferA_.end(), 0.0f);
    std::fill(bufferB_.begin(), bufferB_.end(), 0.0f);
}

inline void ReverseBuffer::setChunkSizeMs(float ms) noexcept {
    // Clamp to valid range
    constexpr float kMinChunkMs = 10.0f;
    if (ms < kMinChunkMs) ms = kMinChunkMs;
    if (ms > static_cast<float>(maxChunkSamples_) * 1000.0f / static_cast<float>(sampleRate_)) {
        ms = static_cast<float>(maxChunkSamples_) * 1000.0f / static_cast<float>(sampleRate_);
    }

    chunkSizeMs_ = ms;
    chunkSizeSamples_ = static_cast<std::size_t>(sampleRate_ * ms / 1000.0);
}

inline void ReverseBuffer::setCrossfadeMs(float ms) noexcept {
    crossfadeMs_ = ms;
    crossfadeSamples_ = static_cast<std::size_t>(sampleRate_ * ms / 1000.0);
}

inline void ReverseBuffer::setReversed(bool reversed) noexcept {
    reversed_ = reversed;
}

inline float ReverseBuffer::process(float input) noexcept {
    // Get pointers to capture and playback buffers
    std::vector<float>& captureBuffer = activeBufferIsA_ ? bufferA_ : bufferB_;
    std::vector<float>& playbackBuffer = activeBufferIsA_ ? bufferB_ : bufferA_;

    // Write input to capture buffer
    captureBuffer[writePos_] = input;

    // Read from playback buffer
    float output = 0.0f;
    if (writePos_ < chunkSizeSamples_) {
        // During first chunk, playback buffer may be empty
        if (reversed_) {
            // Read from end to start
            std::size_t readIdx = chunkSizeSamples_ - 1 - readPos_;
            if (readIdx < playbackBuffer.size()) {
                output = playbackBuffer[readIdx];
            }
        } else {
            // Read forward
            if (readPos_ < playbackBuffer.size()) {
                output = playbackBuffer[readPos_];
            }
        }
    }

    // Advance positions
    writePos_++;
    readPos_++;
    atChunkBoundary_ = false;

    // Check for chunk boundary
    if (writePos_ >= chunkSizeSamples_) {
        // Swap buffers
        activeBufferIsA_ = !activeBufferIsA_;
        writePos_ = 0;
        readPos_ = 0;
        atChunkBoundary_ = true;
    }

    return output;
}

inline bool ReverseBuffer::isAtChunkBoundary() const noexcept {
    return atChunkBoundary_;
}

inline float ReverseBuffer::getChunkSizeMs() const noexcept {
    return chunkSizeMs_;
}

inline std::size_t ReverseBuffer::getLatencySamples() const noexcept {
    return chunkSizeSamples_;
}

} // namespace Iterum::DSP
