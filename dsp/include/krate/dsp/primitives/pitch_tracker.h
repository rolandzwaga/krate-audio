// ==============================================================================
// Layer 1: DSP Primitive - PitchTracker
// ==============================================================================
// Smoothed pitch tracker with confidence gating, median filtering,
// hysteresis, minimum note duration, and frequency smoothing.
//
// Wraps PitchDetector with a fixed 5-stage post-processing pipeline to
// transform raw, jittery pitch detection into stable MIDI note decisions
// suitable for driving a diatonic harmonizer engine.
//
// Processing pipeline (per internal analysis hop):
//   [1] Confidence gate  ->  [2] Median filter (confident frames only)
//   ->  [3] Hysteresis  ->  [4] Min note duration  ->  [5] Frequency smoother
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process path)
// - Principle III: Modern C++ (C++20, std::array, constexpr, [[nodiscard]])
// - Principle IX: Layer 1 (depends only on Layer 0 and other Layer 1)
//
// Reference: specs/063-pitch-tracker/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>
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
    void prepare(double sampleRate,
                 std::size_t windowSize = kDefaultWindowSize) noexcept {
        sampleRate_ = sampleRate;
        windowSize_ = windowSize;
        hopSize_ = windowSize / 4;

        detector_.prepare(sampleRate, windowSize);

        minNoteDurationSamples_ = static_cast<std::size_t>(
            minNoteDurationMs_ / 1000.0 * sampleRate);

        frequencySmoother_.configure(kDefaultFrequencySmoothingMs,
                                     static_cast<float>(sampleRate));

        reset();
    }

    /// @brief Reset all tracking state without changing configuration.
    /// @post Median buffer cleared, timers zeroed, no committed note, smoother reset.
    void reset() noexcept {
        pitchHistory_.fill(0.0f);
        historyIndex_ = 0;
        historyCount_ = 0;

        currentNote_ = -1;
        candidateNote_ = -1;
        noteHoldTimer_ = 0;
        samplesSinceLastHop_ = 0;

        pitchValid_ = false;
        smoothedFrequency_ = 0.0f;

        detector_.reset();
        frequencySmoother_.reset();
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Feed audio samples into the tracker.
    /// @param samples Pointer to audio sample buffer
    /// @param numSamples Number of samples in the buffer
    /// @post Internally triggers PitchDetector::detect() for each completed
    ///       analysis hop and runs the 5-stage pipeline. 0..N detect() calls
    ///       per invocation depending on numSamples vs hop size.
    void pushBlock(const float* samples, std::size_t numSamples) noexcept {
        for (std::size_t i = 0; i < numSamples; ++i) {
            detector_.push(samples[i]);
            ++samplesSinceLastHop_;

            if (samplesSinceLastHop_ >= hopSize_) {
                runPipeline();
                samplesSinceLastHop_ = 0;
            }
        }
    }

    // =========================================================================
    // Output Queries (reflect pipeline stages 4 and 5)
    // =========================================================================

    /// @brief Get the smoothed output frequency in Hz (stage 5).
    /// @return Smoothed frequency from OnePoleSmoother. Returns 0 if no note committed.
    [[nodiscard]] float getFrequency() const noexcept {
        return smoothedFrequency_;
    }

    /// @brief Get the committed MIDI note as an integer (stage 4).
    /// @return Integer MIDI note (e.g. 69 for A4). Returns -1 if no note committed.
    /// @note NOT derived from smoothed frequency. Reflects hysteresis/duration state.
    [[nodiscard]] int getMidiNote() const noexcept {
        return currentNote_;
    }

    /// @brief Get the raw confidence value from the underlying PitchDetector.
    /// @return Confidence in [0.0, 1.0]. Higher = more reliable.
    [[nodiscard]] float getConfidence() const noexcept {
        return detector_.getConfidence();
    }

    /// @brief Check if the last detection frame passed the confidence gate.
    /// @return true if the most recent frame had confidence >= threshold.
    [[nodiscard]] bool isPitchValid() const noexcept {
        return pitchValid_;
    }

    // =========================================================================
    // Configuration (safe to call from any thread; take effect on next hop)
    // =========================================================================

    /// @brief Set median filter window size.
    /// @param size Window size, clamped to [1, kMaxMedianSize]. Default 5.
    /// @post Resets median filter state (history buffer cleared).
    void setMedianFilterSize(std::size_t size) noexcept {
        medianSize_ = std::clamp(size, std::size_t{1}, kMaxMedianSize);
        historyIndex_ = 0;
        historyCount_ = 0;
    }

    /// @brief Set hysteresis threshold in cents.
    /// @param cents Threshold in cents. 0 = disabled. Default 50.
    void setHysteresisThreshold(float cents) noexcept {
        hysteresisThreshold_ = (cents < 0.0f) ? 0.0f : cents;
    }

    /// @brief Set confidence gating threshold.
    /// @param threshold Minimum confidence for accepting a frame. Default 0.5.
    void setConfidenceThreshold(float threshold) noexcept {
        confidenceThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
    }

    /// @brief Set minimum note duration before committing a transition.
    /// @param ms Duration in milliseconds. 0 = disabled. Default 50.
    void setMinNoteDuration(float ms) noexcept {
        minNoteDurationMs_ = (ms < 0.0f) ? 0.0f : ms;
        minNoteDurationSamples_ = static_cast<std::size_t>(
            minNoteDurationMs_ / 1000.0 * sampleRate_);
    }

private:
    /// @brief Run the 5-stage pipeline once per analysis hop.
    ///
    /// Pipeline order (fixed, non-configurable):
    ///   [1] Confidence Gate -> [2] Median Filter -> [3] Hysteresis
    ///   -> [4] Min Note Duration -> [5] Frequency Smoother
    void runPipeline() noexcept {
        // =====================================================================
        // Stage 1: Confidence Gate
        // =====================================================================
        const float confidence = detector_.getConfidence();
        if (confidence < confidenceThreshold_) {
            pitchValid_ = false;
            // Hold last committed state -- do NOT modify currentNote_,
            // candidateNote_, noteHoldTimer_, or smoother.
            // Advance smoother to keep it warm (avoids stale state).
            frequencySmoother_.advanceSamples(hopSize_);
            smoothedFrequency_ = frequencySmoother_.getCurrentValue();
            return;
        }

        // =====================================================================
        // Stage 2: Median Filter (confident frames only)
        // =====================================================================
        pitchValid_ = true;

        const float detectedFreq = detector_.getDetectedFrequency();

        // Write confident detection to ring buffer
        pitchHistory_[historyIndex_] = detectedFreq;
        historyIndex_ = (historyIndex_ + 1) % medianSize_;
        if (historyCount_ < medianSize_) {
            ++historyCount_;
        }

        const float medianFreq = computeMedian();

        // =====================================================================
        // Stage 3: Hysteresis
        // =====================================================================
        if (currentNote_ == -1) {
            // First detection: bypass hysteresis, proceed to stage 4
        } else {
            // Compute cents distance from committed note center (FR-014)
            const float centsDistance =
                std::abs(frequencyToMidiNote(medianFreq) -
                         static_cast<float>(currentNote_)) * 100.0f;

            if (centsDistance <= hysteresisThreshold_) {
                // Within hysteresis zone: no candidate change, hold current note
                candidateNote_ = -1;
                noteHoldTimer_ = 0;
                // Still advance the smoother
                frequencySmoother_.advanceSamples(hopSize_);
                smoothedFrequency_ = frequencySmoother_.getCurrentValue();
                return;
            }

            // Hysteresis exceeded: propose a new candidate
            candidateNote_ = static_cast<int>(
                std::round(frequencyToMidiNote(medianFreq)));
        }

        // =====================================================================
        // Stage 4: Minimum Note Duration
        // =====================================================================
        if (currentNote_ == -1) {
            // First detection: commit immediately, bypass min duration (FR-015)
            currentNote_ = static_cast<int>(
                std::round(frequencyToMidiNote(medianFreq)));
            candidateNote_ = -1;
            noteHoldTimer_ = 0;
            frequencySmoother_.snapTo(midiNoteToFrequency(currentNote_));
        } else {
            const int proposedNote = static_cast<int>(
                std::round(frequencyToMidiNote(medianFreq)));

            if (proposedNote == candidateNote_) {
                // Same candidate: increment timer
                noteHoldTimer_ += hopSize_;

                if (noteHoldTimer_ >= minNoteDurationSamples_) {
                    // Timer expired: commit the candidate
                    currentNote_ = candidateNote_;
                    candidateNote_ = -1;
                    noteHoldTimer_ = 0;
                    frequencySmoother_.setTarget(
                        midiNoteToFrequency(currentNote_));
                }
            } else {
                // Candidate changed: reset timer with the new candidate
                candidateNote_ = proposedNote;
                noteHoldTimer_ = hopSize_;  // Count this hop
            }
        }

        // =====================================================================
        // Stage 5: Frequency Smoother
        // =====================================================================
        frequencySmoother_.advanceSamples(hopSize_);
        smoothedFrequency_ = frequencySmoother_.getCurrentValue();
    }

    /// @brief Compute the median of the confident pitch history ring buffer.
    /// @return Median frequency in Hz. Returns 0.0f if no history available.
    [[nodiscard]] float computeMedian() const noexcept {
        if (historyCount_ == 0) {
            return 0.0f;
        }

        // Optimization: when medianSize_ == 1, there is at most one entry --
        // return it directly without any copy or sort (plan.md SIMD section).
        if (medianSize_ == 1) {
            const std::size_t lastIdx =
                (historyIndex_ == 0) ? 0 : historyIndex_ - 1;
            return pitchHistory_[lastIdx];
        }

        // Optimization: skip sort for single-element case (partial buffer)
        if (historyCount_ == 1) {
            // Return the last written value
            const std::size_t lastIdx =
                (historyIndex_ == 0) ? medianSize_ - 1 : historyIndex_ - 1;
            return pitchHistory_[lastIdx];
        }

        // Copy available history entries to scratch array
        std::array<float, kMaxMedianSize> scratch{};
        for (std::size_t i = 0; i < historyCount_; ++i) {
            // Read from the ring buffer -- entries may not be contiguous
            // if historyCount_ < medianSize_ after a size change
            scratch[i] = pitchHistory_[i % medianSize_];
        }

        // Insertion sort (small N, no allocations)
        for (std::size_t i = 1; i < historyCount_; ++i) {
            const float key = scratch[i];
            std::size_t j = i;
            while (j > 0 && scratch[j - 1] > key) {
                scratch[j] = scratch[j - 1];
                --j;
            }
            scratch[j] = key;
        }

        // Return middle element
        return scratch[historyCount_ / 2];
    }

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

}  // namespace Krate::DSP
