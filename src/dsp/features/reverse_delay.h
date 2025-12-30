// ==============================================================================
// Layer 4: User Feature - ReverseDelay
// ==============================================================================
// Reverse delay effect using FlexibleFeedbackNetwork with injected
// ReverseFeedbackProcessor. Follows the ShimmerDelay architectural pattern.
//
// Feature: 030-reverse-delay
// Layer: 4 (User Feature)
// Reference: specs/030-reverse-delay/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include "dsp/core/block_context.h"
#include "dsp/core/db_utils.h"
#include "dsp/core/note_value.h"
#include "dsp/primitives/smoother.h"
#include "dsp/processors/multimode_filter.h"
#include "dsp/processors/reverse_feedback_processor.h"
#include "dsp/systems/delay_engine.h"  // For TimeMode enum
#include "dsp/systems/flexible_feedback_network.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Iterum::DSP {

// =============================================================================
// ReverseDelay
// =============================================================================

/// @brief Reverse delay effect with chunk-based backward playback
///
/// Creates ethereal, otherworldly effects where audio is captured in chunks
/// and played back in reverse. Supports multiple playback modes (Full Reverse,
/// Alternating, Random), feedback with optional filtering, and tempo-synced
/// chunk sizes.
///
/// Uses FlexibleFeedbackNetwork with injected ReverseFeedbackProcessor,
/// following the same architectural pattern as ShimmerDelay.
///
/// @par Usage
/// @code
/// ReverseDelay delay;
/// delay.prepare(44100.0, 512, 2000.0f);
/// delay.setChunkSizeMs(500.0f);         // 500ms chunks
/// delay.setPlaybackMode(PlaybackMode::FullReverse);
/// delay.setFeedbackAmount(0.5f);        // 50% feedback
/// delay.setDryWetMix(50.0f);            // 50/50 mix
/// delay.snapParameters();
///
/// // In process callback
/// delay.process(left, right, numSamples, ctx);
/// @endcode
class ReverseDelay {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    // Chunk time limits (FR-001, FR-005)
    static constexpr float kMinChunkMs = 10.0f;
    static constexpr float kMaxChunkMs = 2000.0f;
    static constexpr float kDefaultChunkMs = 500.0f;

    // Crossfade (FR-008)
    static constexpr float kMinCrossfade = 0.0f;
    static constexpr float kMaxCrossfade = 100.0f;
    static constexpr float kDefaultCrossfade = 50.0f;

    // Feedback (FR-016)
    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;  // 120% for self-oscillation
    static constexpr float kDefaultFeedback = 0.0f;

    // Filter (FR-018, FR-019)
    static constexpr float kMinFilterCutoff = 20.0f;
    static constexpr float kMaxFilterCutoff = 20000.0f;
    static constexpr float kDefaultFilterCutoff = 4000.0f;

    // Output (FR-020)
    static constexpr float kMinDryWetMix = 0.0f;
    static constexpr float kMaxDryWetMix = 100.0f;
    static constexpr float kDefaultDryWetMix = 50.0f;

    // Internal
    static constexpr float kSmoothingTimeMs = 20.0f;
    static constexpr float kMinDelayForNetwork = 1.0f;  // Minimal delay in FFN

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    ReverseDelay() noexcept = default;
    ~ReverseDelay() = default;

    // Non-copyable, movable
    ReverseDelay(const ReverseDelay&) = delete;
    ReverseDelay& operator=(const ReverseDelay&) = delete;
    ReverseDelay(ReverseDelay&&) noexcept = default;
    ReverseDelay& operator=(ReverseDelay&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxChunkMs Maximum chunk size in milliseconds
    void prepare(double sampleRate, std::size_t maxBlockSize, float maxChunkMs) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        maxChunkMs_ = std::min(maxChunkMs, kMaxChunkMs);

        // Prepare FlexibleFeedbackNetwork
        // Use a small delay time since the reverse processor provides the timing
        feedbackNetwork_.prepare(sampleRate, maxBlockSize);
        feedbackNetwork_.setDelayTimeMs(kMinDelayForNetwork);

        // Prepare reverse processor
        reverseProcessor_.prepare(sampleRate, maxBlockSize);
        reverseProcessor_.setChunkSizeMs(kDefaultChunkMs);

        // Inject processor into feedback network
        feedbackNetwork_.setProcessor(&reverseProcessor_);
        feedbackNetwork_.setProcessorMix(100.0f);  // 100% reverse processing

        // Prepare smoothers
        dryWetSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

        // Allocate dry signal buffers
        dryBufferL_.resize(maxBlockSize, 0.0f);
        dryBufferR_.resize(maxBlockSize, 0.0f);

        // Set defaults
        setChunkSizeMs(kDefaultChunkMs);
        setDryWetMix(kDefaultDryWetMix);
        setFeedbackAmount(kDefaultFeedback);

        prepared_ = true;
    }

    /// @brief Reset all internal state
    void reset() noexcept {
        feedbackNetwork_.reset();
        reverseProcessor_.reset();
        dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
    }

    /// @brief Snap all smoothers to current targets
    void snapParameters() noexcept {
        dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
        feedbackNetwork_.snapParameters();
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio
    /// @param left Left channel buffer (modified in place)
    /// @param right Right channel buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @param ctx Block context with tempo information
    void process(float* left, float* right, std::size_t numSamples,
                 const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0) return;

        // Update chunk size from tempo if synced
        if (timeMode_ == TimeMode::Synced) {
            float syncedMs = calculateTempoSyncedChunk(ctx);
            reverseProcessor_.setChunkSizeMs(syncedMs);
        }

        // Store dry signal
        for (std::size_t i = 0; i < numSamples; ++i) {
            dryBufferL_[i] = left[i];
            dryBufferR_[i] = right[i];
        }

        // Process through feedback network (includes reverse processor)
        feedbackNetwork_.process(left, right, numSamples, ctx);

        // Apply dry/wet mix
        for (std::size_t i = 0; i < numSamples; ++i) {
            float wetAmount = dryWetSmoother_.process();
            float dryAmount = 1.0f - wetAmount;

            left[i] = dryBufferL_[i] * dryAmount + left[i] * wetAmount;
            right[i] = dryBufferR_[i] * dryAmount + right[i] * wetAmount;
        }
    }

    // =========================================================================
    // Chunk Configuration (FR-001, FR-005, FR-006)
    // =========================================================================

    /// @brief Set chunk size in milliseconds
    /// @param ms Chunk size [10, 2000]
    void setChunkSizeMs(float ms) noexcept {
        chunkSizeMs_ = std::clamp(ms, kMinChunkMs, maxChunkMs_);
        reverseProcessor_.setChunkSizeMs(chunkSizeMs_);
    }

    /// @brief Get current chunk size in milliseconds
    [[nodiscard]] float getCurrentChunkMs() const noexcept {
        return reverseProcessor_.getChunkSizeMs();
    }

    /// @brief Set time mode (free or synced)
    void setTimeMode(TimeMode mode) noexcept {
        timeMode_ = mode;
    }

    /// @brief Get current time mode
    [[nodiscard]] TimeMode getTimeMode() const noexcept {
        return timeMode_;
    }

    /// @brief Set note value for tempo sync
    void setNoteValue(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept {
        noteValue_ = note;
        noteModifier_ = modifier;
    }

    /// @brief Get current note value
    [[nodiscard]] NoteValue getNoteValue() const noexcept {
        return noteValue_;
    }

    // =========================================================================
    // Crossfade Configuration (FR-008, FR-009, FR-010)
    // =========================================================================

    /// @brief Set crossfade percentage
    /// @param percent Crossfade amount [0, 100]
    void setCrossfadePercent(float percent) noexcept {
        crossfadePercent_ = std::clamp(percent, kMinCrossfade, kMaxCrossfade);
        float crossfadeMs = crossfadePercent_ * chunkSizeMs_ / 100.0f;
        reverseProcessor_.setCrossfadeMs(crossfadeMs);
    }

    // =========================================================================
    // Playback Mode (FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set playback mode
    void setPlaybackMode(PlaybackMode mode) noexcept {
        reverseProcessor_.setPlaybackMode(mode);
    }

    /// @brief Get current playback mode
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept {
        return reverseProcessor_.getPlaybackMode();
    }

    // =========================================================================
    // Feedback (FR-016, FR-017)
    // =========================================================================

    /// @brief Set feedback amount
    /// @param amount Feedback [0, 1.2] (0-120%)
    void setFeedbackAmount(float amount) noexcept {
        feedbackAmount_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
        feedbackNetwork_.setFeedbackAmount(feedbackAmount_);
    }

    // =========================================================================
    // Filter (FR-018, FR-019)
    // =========================================================================

    /// @brief Enable/disable feedback filter
    void setFilterEnabled(bool enabled) noexcept {
        filterEnabled_ = enabled;
        feedbackNetwork_.setFilterEnabled(enabled);
    }

    /// @brief Set filter cutoff frequency
    /// @param hz Cutoff frequency [20, 20000]
    void setFilterCutoff(float hz) noexcept {
        filterCutoffHz_ = std::clamp(hz, kMinFilterCutoff, kMaxFilterCutoff);
        feedbackNetwork_.setFilterCutoff(filterCutoffHz_);
    }

    /// @brief Set filter type
    void setFilterType(FilterType type) noexcept {
        feedbackNetwork_.setFilterType(type);
    }

    // =========================================================================
    // Mixing and Output (FR-020)
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param percent Mix amount [0, 100] (0 = all dry, 100 = all wet)
    void setDryWetMix(float percent) noexcept {
        dryWetMix_ = std::clamp(percent, kMinDryWetMix, kMaxDryWetMix);
        dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
    }

    // =========================================================================
    // State Queries
    // =========================================================================

    /// @brief Get latency in samples (equals chunk size)
    [[nodiscard]] std::size_t getLatencySamples() const noexcept {
        return reverseProcessor_.getLatencySamples();
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Calculate tempo-synced chunk size
    [[nodiscard]] float calculateTempoSyncedChunk(const BlockContext& ctx) const noexcept {
        std::size_t samples = ctx.tempoToSamples(noteValue_, noteModifier_);
        float ms = static_cast<float>(samples) * 1000.0f / static_cast<float>(ctx.sampleRate);
        return std::clamp(ms, kMinChunkMs, maxChunkMs_);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Core components
    FlexibleFeedbackNetwork feedbackNetwork_;
    ReverseFeedbackProcessor reverseProcessor_;

    // Parameter smoothers
    OnePoleSmoother dryWetSmoother_;

    // Dry signal buffers
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;

    // State
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;

    // Parameters
    float chunkSizeMs_ = kDefaultChunkMs;
    float crossfadePercent_ = kDefaultCrossfade;
    float dryWetMix_ = kDefaultDryWetMix;
    float feedbackAmount_ = kDefaultFeedback;
    float filterCutoffHz_ = kDefaultFilterCutoff;
    bool filterEnabled_ = false;

    // Configuration
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
    float maxChunkMs_ = kMaxChunkMs;
    bool prepared_ = false;
};

} // namespace Iterum::DSP
