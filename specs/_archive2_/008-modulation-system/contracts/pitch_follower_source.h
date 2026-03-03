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

#include <cmath>
#include <cstddef>

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

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// @brief Feed audio sample to pitch detector.
    void pushSample(float sample) noexcept;

    /// @brief Update modulation output from latest pitch detection.
    void process() noexcept;

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override;

    // Parameter setters
    void setMinHz(float hz) noexcept;
    void setMaxHz(float hz) noexcept;
    void setConfidenceThreshold(float threshold) noexcept;
    void setTrackingSpeed(float ms) noexcept;

private:
    /// @brief Convert frequency to normalized modulation value using log mapping.
    [[nodiscard]] float hzToModValue(float hz) const noexcept;

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
