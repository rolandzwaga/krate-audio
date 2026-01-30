// ==============================================================================
// Layer 2: DSP Processor - Random Modulation Source
// ==============================================================================
// Generates random modulation values at a configurable rate with optional
// smoothing for gradual transitions.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0-1)
//
// Reference: specs/008-modulation-system/spec.md (FR-021 to FR-025)
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Random modulation source.
///
/// Generates new random values at a configurable rate with optional
/// smoothing. Supports tempo sync.
///
/// @par Output Range: [-1.0, +1.0] (bipolar)
class RandomSource : public ModulationSource {
public:
    static constexpr float kMinRate = 0.1f;
    static constexpr float kMaxRate = 50.0f;
    static constexpr float kDefaultRate = 4.0f;
    static constexpr float kMinSmoothness = 0.0f;
    static constexpr float kMaxSmoothness = 1.0f;
    static constexpr float kDefaultSmoothness = 0.0f;

    RandomSource() noexcept = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// @brief Process one sample.
    void process() noexcept;

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override;

    // Parameter setters
    void setRate(float hz) noexcept;
    void setSmoothness(float normalized) noexcept;
    void setTempoSync(bool enabled) noexcept;
    void setTempo(float bpm) noexcept;

private:
    float rate_ = kDefaultRate;
    float smoothness_ = kDefaultSmoothness;
    bool tempoSync_ = false;
    float bpm_ = 120.0f;

    float phase_ = 0.0f;
    float currentTarget_ = 0.0f;  // Target random value

    Xorshift32 rng_{98765};
    OnePoleSmoother outputSmoother_;
    double sampleRate_ = 44100.0;
};

}  // namespace DSP
}  // namespace Krate
