// ==============================================================================
// IFeedbackProcessor - Interface for Feedback Path Processors
// ==============================================================================
// Layer 1: DSP Primitives (Interface)
//
// Abstract interface enabling injection of arbitrary processors into feedback
// paths. Designed for use with FlexibleFeedbackNetwork to support advanced
// effects like shimmer delay (pitch shifting) and freeze mode.
//
// Constitution Compliance:
// - Principle IX: Moved to Layer 1 to allow Layer 2 processors to implement
//
// All implementations must be real-time safe:
// - No allocations in process()
// - All operations noexcept
// - Pre-allocation in prepare()
// ==============================================================================
#pragma once

#include <cstddef>

namespace Iterum::DSP {

/// @brief Interface for processors that can be injected into feedback paths
///
/// Implementors can provide any stereo processing (pitch shifting, diffusion,
/// granular processing, etc.) that will be applied in the feedback loop.
///
/// @note All methods must be real-time safe (no allocations in process)
class IFeedbackProcessor {
public:
    virtual ~IFeedbackProcessor() = default;

    /// @brief Prepare the processor for audio processing
    /// @param sampleRate The sample rate in Hz
    /// @param maxBlockSize Maximum number of samples per process() call
    /// @note Allocations are permitted here, but not in process()
    virtual void prepare(double sampleRate, std::size_t maxBlockSize) noexcept = 0;

    /// @brief Process stereo audio in-place
    /// @param left Left channel buffer (modified in place)
    /// @param right Right channel buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @note Must be real-time safe - no allocations, no blocking
    virtual void process(float* left, float* right, std::size_t numSamples) noexcept = 0;

    /// @brief Reset all internal state (clear delay lines, etc.)
    /// @note Call when starting playback or when discontinuity occurs
    virtual void reset() noexcept = 0;

    /// @brief Report the latency introduced by this processor
    /// @return Latency in samples
    /// @note Used by FlexibleFeedbackNetwork to report total latency
    [[nodiscard]] virtual std::size_t getLatencySamples() const noexcept = 0;
};

} // namespace Iterum::DSP
