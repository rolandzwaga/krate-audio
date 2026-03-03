// Contract: PitchTracker API (063-pitch-tracker)
// This file documents the exact public API contract for implementation.
// It is NOT compiled -- it serves as the binding specification.

#pragma once

#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

/// @brief Smoothed pitch tracker with confidence gating, median filtering,
///        hysteresis, minimum note duration, and frequency smoothing (Layer 1).
///
/// Processing pipeline (per internal analysis hop):
///   [1] Confidence gate  ->  [2] Median filter (confident frames only)
///   ->  [3] Hysteresis  ->  [4] Min note duration  ->  [5] Frequency smoother
///
/// @par Real-Time Safety
/// All methods are noexcept, no allocations in process path.
class PitchTracker {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr std::size_t kDefaultWindowSize           = 256;
    static constexpr std::size_t kMaxMedianSize               = 11;
    static constexpr float       kDefaultHysteresisThreshold  = 50.0f;  // cents
    static constexpr float       kDefaultConfidenceThreshold  = 0.5f;
    static constexpr float       kDefaultMinNoteDurationMs    = 50.0f;
    static constexpr float       kDefaultFrequencySmoothingMs = 25.0f;  // OnePoleSmoother time constant

    // =========================================================================
    // Lifecycle
    // =========================================================================
    PitchTracker() noexcept = default;

    /// @brief Initialize the tracker for the given sample rate and window size.
    /// @param sampleRate Audio sample rate in Hz (e.g. 44100.0)
    /// @param windowSize Analysis window size in samples (default 256)
    /// @post PitchDetector prepared, smoother configured, all state reset.
    /// @note This method allocates (via PitchDetector). Call from setup, not audio thread.
    void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept;

    /// @brief Reset all tracking state without changing configuration.
    /// @post Median buffer cleared, timers zeroed, no committed note, smoother reset.
    void reset() noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Feed audio samples into the tracker.
    /// @param samples Pointer to audio sample buffer
    /// @param numSamples Number of samples in the buffer
    /// @post Internally triggers PitchDetector::detect() for each completed
    ///       analysis hop and runs the 5-stage pipeline. 0..N detect() calls
    ///       per invocation depending on numSamples vs hop size.
    void pushBlock(const float* samples, std::size_t numSamples) noexcept;

    // =========================================================================
    // Output Queries (reflect pipeline stages 4 and 5)
    // =========================================================================

    /// @brief Get the smoothed output frequency in Hz (stage 5).
    /// @return Smoothed frequency from OnePoleSmoother. Returns 0 if no note committed.
    [[nodiscard]] float getFrequency() const noexcept;

    /// @brief Get the committed MIDI note as an integer (stage 4).
    /// @return Integer MIDI note (e.g. 69 for A4). Returns -1 if no note committed.
    /// @note NOT derived from smoothed frequency. Reflects hysteresis/duration state.
    [[nodiscard]] int getMidiNote() const noexcept;

    /// @brief Get the raw confidence value from the underlying PitchDetector.
    /// @return Confidence in [0.0, 1.0]. Higher = more reliable.
    [[nodiscard]] float getConfidence() const noexcept;

    /// @brief Check if the last detection frame passed the confidence gate.
    /// @return true if the most recent frame had confidence >= threshold.
    [[nodiscard]] bool isPitchValid() const noexcept;

    // =========================================================================
    // Configuration (safe to call from any thread; take effect on next hop)
    // =========================================================================

    /// @brief Set median filter window size.
    /// @param size Window size, clamped to [1, kMaxMedianSize]. Default 5.
    /// @post Resets median filter state (history buffer cleared).
    void setMedianFilterSize(std::size_t size) noexcept;

    /// @brief Set hysteresis threshold in cents.
    /// @param cents Threshold in cents. 0 = disabled. Default 50.
    void setHysteresisThreshold(float cents) noexcept;

    /// @brief Set confidence gating threshold.
    /// @param threshold Minimum confidence for accepting a frame. Default 0.5.
    void setConfidenceThreshold(float threshold) noexcept;

    /// @brief Set minimum note duration before committing a transition.
    /// @param ms Duration in milliseconds. 0 = disabled. Default 50.
    void setMinNoteDuration(float ms) noexcept;

private:
    // Internal pipeline method called once per hop
    void runPipeline() noexcept;

    // Median computation helper
    float computeMedian() const noexcept;

    PitchDetector detector_;

    // Stage 2: Median filter (confident frames only)
    std::array<float, kMaxMedianSize> pitchHistory_{};
    std::size_t medianSize_    = 5;
    std::size_t historyIndex_  = 0;
    std::size_t historyCount_  = 0;

    // Stage 3: Hysteresis state
    int   currentNote_           = -1;
    float hysteresisThreshold_   = kDefaultHysteresisThreshold;

    // Stage 1: Confidence gating
    float confidenceThreshold_   = kDefaultConfidenceThreshold;
    bool  pitchValid_            = false;

    // Stage 4: Note hold timer
    float       minNoteDurationMs_      = kDefaultMinNoteDurationMs;
    std::size_t noteHoldTimer_          = 0;
    std::size_t minNoteDurationSamples_ = 0;
    int         candidateNote_          = -1;

    // Hop tracking
    double      sampleRate_            = 44100.0;
    std::size_t hopSize_               = kDefaultWindowSize / 4;
    std::size_t samplesSinceLastHop_   = 0;
    std::size_t windowSize_            = kDefaultWindowSize;

    // Stage 5: Smoothed frequency output
    OnePoleSmoother frequencySmoother_;
    float           smoothedFrequency_ = 0.0f;
};

} // namespace Krate::DSP
