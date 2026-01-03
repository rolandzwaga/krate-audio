// ==============================================================================
// Layer 2: DSP Processor - ReverseFeedbackProcessor
// ==============================================================================
// Implements IFeedbackProcessor for injection into FlexibleFeedbackNetwork.
// Provides stereo reverse processing with crossfade.
//
// Feature: 030-reverse-delay
// Layer: 2 (Processor)
// Reference: specs/030-reverse-delay/data-model.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (uses Layer 1 primitives only)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/reverse_buffer.h>
#include <krate/dsp/primitives/i_feedback_processor.h>
#include <krate/dsp/core/random.h>

#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// PlaybackMode Enum
// =============================================================================

/// @brief Playback direction modes for reverse delay
enum class PlaybackMode : std::uint8_t {
    FullReverse,   ///< Every chunk plays reversed
    Alternating,   ///< Alternates: reverse, forward, reverse, forward...
    Random         ///< Random direction per chunk (50/50)
};

// =============================================================================
// ReverseFeedbackProcessor
// =============================================================================

/// @brief Feedback path processor that applies stereo reverse processing
///
/// Implements IFeedbackProcessor to be injected into FlexibleFeedbackNetwork.
/// Wraps two ReverseBuffer instances (stereo pair) and manages playback mode
/// logic for chunk direction selection.
///
/// @note All processing methods are noexcept and real-time safe
class ReverseFeedbackProcessor : public IFeedbackProcessor {
public:
    // Constants
    static constexpr float kMinChunkMs = 10.0f;
    static constexpr float kMaxChunkMs = 2000.0f;
    static constexpr float kDefaultChunkMs = 500.0f;
    static constexpr float kDefaultCrossfadeMs = 20.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    ReverseFeedbackProcessor() noexcept = default;
    ~ReverseFeedbackProcessor() override = default;

    // Non-copyable, movable
    ReverseFeedbackProcessor(const ReverseFeedbackProcessor&) = delete;
    ReverseFeedbackProcessor& operator=(const ReverseFeedbackProcessor&) = delete;
    ReverseFeedbackProcessor(ReverseFeedbackProcessor&&) noexcept = default;
    ReverseFeedbackProcessor& operator=(ReverseFeedbackProcessor&&) noexcept = default;

    // =========================================================================
    // IFeedbackProcessor Interface
    // =========================================================================

    /// @brief Prepare the processor for audio processing
    /// @param sampleRate The sample rate in Hz
    /// @param maxBlockSize Maximum number of samples per process() call
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept override {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Prepare both channels with max chunk size
        bufferL_.prepare(sampleRate, kMaxChunkMs);
        bufferR_.prepare(sampleRate, kMaxChunkMs);

        // Set default chunk size
        setChunkSizeMs(kDefaultChunkMs);

        // Initialize RNG with a fixed seed for reproducibility
        rng_.seed(42);
    }

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in place)
    /// @param right Right channel buffer (modified in place)
    /// @param numSamples Number of samples to process
    void process(float* left, float* right, std::size_t numSamples) noexcept override {
        if (numSamples == 0) return;

        for (std::size_t i = 0; i < numSamples; ++i) {
            // Check for chunk boundary on left channel (both are synchronized)
            if (bufferL_.isAtChunkBoundary()) {
                // Determine direction for next chunk based on mode
                bool shouldReverse = shouldReverseNextChunk();
                bufferL_.setReversed(shouldReverse);
                bufferR_.setReversed(shouldReverse);
                chunkCounter_++;
            }

            // Process both channels
            left[i] = bufferL_.process(left[i]);
            right[i] = bufferR_.process(right[i]);
        }
    }

    /// @brief Reset all internal state
    void reset() noexcept override {
        bufferL_.reset();
        bufferR_.reset();
        chunkCounter_ = 0;

        // Set initial direction based on mode
        bool initialReverse = (mode_ != PlaybackMode::Alternating);
        bufferL_.setReversed(initialReverse);
        bufferR_.setReversed(initialReverse);
    }

    /// @brief Report the latency introduced by this processor
    /// @return Latency in samples (equals chunk size)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept override {
        return bufferL_.getLatencySamples();
    }

    // =========================================================================
    // Configuration Methods
    // =========================================================================

    /// @brief Set chunk size in milliseconds
    /// @param ms Chunk size (clamped to [10, 2000] ms)
    void setChunkSizeMs(float ms) noexcept {
        // Clamp to valid range
        if (ms < kMinChunkMs) ms = kMinChunkMs;
        if (ms > kMaxChunkMs) ms = kMaxChunkMs;

        bufferL_.setChunkSizeMs(ms);
        bufferR_.setChunkSizeMs(ms);
    }

    /// @brief Get current chunk size in milliseconds
    [[nodiscard]] float getChunkSizeMs() const noexcept {
        return bufferL_.getChunkSizeMs();
    }

    /// @brief Set crossfade duration in milliseconds
    /// @param ms Crossfade duration (0 = no crossfade)
    void setCrossfadeMs(float ms) noexcept {
        bufferL_.setCrossfadeMs(ms);
        bufferR_.setCrossfadeMs(ms);
    }

    /// @brief Set manual reverse mode (used by playback modes internally)
    /// @param reversed If true, playback is reversed
    void setReversed(bool reversed) noexcept {
        bufferL_.setReversed(reversed);
        bufferR_.setReversed(reversed);
    }

    /// @brief Set playback mode (FullReverse, Alternating, Random)
    /// @param mode The playback mode
    void setPlaybackMode(PlaybackMode mode) noexcept {
        mode_ = mode;

        // Set initial direction based on mode
        bool initialReverse = (mode != PlaybackMode::Alternating);
        bufferL_.setReversed(initialReverse);
        bufferR_.setReversed(initialReverse);
    }

    /// @brief Get current playback mode
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept {
        return mode_;
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Determine if next chunk should be reversed based on current mode
    /// @return true if next chunk should play reversed, false for forward
    [[nodiscard]] bool shouldReverseNextChunk() noexcept {
        switch (mode_) {
            case PlaybackMode::FullReverse:
                return true;

            case PlaybackMode::Alternating:
                // Odd chunks are reversed, even are forward
                return (chunkCounter_ % 2) == 0;

            case PlaybackMode::Random:
                // 50/50 random choice using LSB of next random value
                return (rng_.next() & 1) == 1;

            default:
                return true;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Stereo reverse buffers
    ReverseBuffer bufferL_;
    ReverseBuffer bufferR_;

    // Playback mode
    PlaybackMode mode_ = PlaybackMode::FullReverse;
    std::size_t chunkCounter_ = 0;

    // Random number generator for Random mode (Layer 0 Xorshift32)
    Xorshift32 rng_{42};

    // Configuration
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
};

} // namespace Krate::DSP
