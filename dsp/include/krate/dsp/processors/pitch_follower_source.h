// ==============================================================================
// Layer 2: DSP Processor - Pitch Follower Modulation Source
// ==============================================================================
// Converts detected pitch to normalized modulation value using logarithmic
// (semitone-based) mapping within a configurable frequency range.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0-1)
//
// Reference: specs/008-modulation-system/spec.md (FR-041 to FR-047)
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Pitch follower modulation source.
///
/// Maps detected fundamental frequency to a [0, 1] modulation value
/// using logarithmic (semitone) mapping within a configurable Hz range.
///
/// @par Output Range: [0, +1]
class PitchFollowerSource : public ModulationSource {
public:
    static constexpr float kMinMinHz = 20.0f;
    static constexpr float kMaxMinHz = 500.0f;
    static constexpr float kDefaultMinHz = 80.0f;
    static constexpr float kMinMaxHz = 200.0f;
    static constexpr float kMaxMaxHz = 5000.0f;
    static constexpr float kDefaultMaxHz = 2000.0f;
    static constexpr float kMinConfidence = 0.0f;
    static constexpr float kMaxConfidence = 1.0f;
    static constexpr float kDefaultConfidence = 0.5f;
    static constexpr float kMinTrackingMs = 10.0f;
    static constexpr float kMaxTrackingMs = 300.0f;
    static constexpr float kDefaultTrackingMs = 50.0f;

    PitchFollowerSource() noexcept = default;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        detector_.prepare(sampleRate);
        outputSmoother_.configure(trackingSpeedMs_, static_cast<float>(sampleRate));
        lastValidValue_ = 0.0f;
        outputSmoother_.snapTo(0.0f);
    }

    void reset() noexcept {
        detector_.reset();
        outputSmoother_.reset();
        lastValidValue_ = 0.0f;
    }

    /// @brief Feed audio sample to pitch detector.
    void pushSample(float sample) noexcept {
        detector_.push(sample);
    }

    /// @brief Feed a block of audio and update output once (control-rate).
    ///
    /// More efficient than calling pushSample() + process() per sample.
    /// The PitchDetector buffers internally and triggers detection every
    /// windowSize/4 samples, so pushing a block is equivalent.
    ///
    /// @param monoInput Mono audio samples
    /// @param numSamples Number of samples in the block
    void processBlock(const float* monoInput, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            detector_.push(monoInput[i]);
        }
        process();
    }

    /// @brief Update modulation output from latest pitch detection.
    void process() noexcept {
        float freq = detector_.getDetectedFrequency();
        float conf = detector_.getConfidence();

        if (conf >= confidenceThreshold_ && freq > 0.0f) {
            lastValidValue_ = hzToModValue(freq);
        }

        outputSmoother_.setTarget(lastValidValue_);
        static_cast<void>(outputSmoother_.process());
    }

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return std::clamp(outputSmoother_.getCurrentValue(), 0.0f, 1.0f);
    }

    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {0.0f, 1.0f};
    }

    // Parameter setters
    void setMinHz(float hz) noexcept {
        minHz_ = std::clamp(hz, kMinMinHz, kMaxMinHz);
    }

    void setMaxHz(float hz) noexcept {
        maxHz_ = std::clamp(hz, kMinMaxHz, kMaxMaxHz);
    }

    void setConfidenceThreshold(float threshold) noexcept {
        confidenceThreshold_ = std::clamp(threshold, kMinConfidence, kMaxConfidence);
    }

    void setTrackingSpeed(float ms) noexcept {
        trackingSpeedMs_ = std::clamp(ms, kMinTrackingMs, kMaxTrackingMs);
        outputSmoother_.configure(trackingSpeedMs_, static_cast<float>(sampleRate_));
    }

    // Parameter getters
    [[nodiscard]] float getMinHz() const noexcept { return minHz_; }
    [[nodiscard]] float getMaxHz() const noexcept { return maxHz_; }
    [[nodiscard]] float getConfidenceThreshold() const noexcept { return confidenceThreshold_; }
    [[nodiscard]] float getTrackingSpeed() const noexcept { return trackingSpeedMs_; }

private:
    /// @brief Convert frequency to normalized modulation value using log mapping.
    [[nodiscard]] float hzToModValue(float hz) const noexcept {
        // Logarithmic mapping via MIDI note numbers
        // midiNote = 69 + 12 * log2(freq / 440)
        constexpr float kLog2Inv = 1.4426950408889634f;  // 1/ln(2)

        float midiNote = 69.0f + 12.0f * std::log(hz / 440.0f) * kLog2Inv;
        float minMidi = 69.0f + 12.0f * std::log(minHz_ / 440.0f) * kLog2Inv;
        float maxMidi = 69.0f + 12.0f * std::log(maxHz_ / 440.0f) * kLog2Inv;

        if (maxMidi <= minMidi) {
            return 0.5f;  // Degenerate range
        }

        return std::clamp((midiNote - minMidi) / (maxMidi - minMidi), 0.0f, 1.0f);
    }

    PitchDetector detector_;
    OnePoleSmoother outputSmoother_;

    float minHz_ = kDefaultMinHz;
    float maxHz_ = kDefaultMaxHz;
    float confidenceThreshold_ = kDefaultConfidence;
    float trackingSpeedMs_ = kDefaultTrackingMs;
    float lastValidValue_ = 0.0f;
    double sampleRate_ = 44100.0;
};

}  // namespace DSP
}  // namespace Krate
