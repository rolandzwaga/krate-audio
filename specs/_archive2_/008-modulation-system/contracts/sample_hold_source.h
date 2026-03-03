// ==============================================================================
// Layer 2: DSP Processor - Sample & Hold Modulation Source
// ==============================================================================
// Periodically samples a configurable input and holds the value with
// optional slew limiting for smooth transitions.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0-1)
//
// Reference: specs/008-modulation-system/spec.md (FR-036 to FR-040)
// ==============================================================================

#pragma once

#include <krate/dsp/core/modulation_source.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>

#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Sample & Hold modulation source.
///
/// Samples a selectable input at a configurable rate and holds the value.
/// Supports 4 input sources: Random, LFO 1, LFO 2, External (audio).
///
/// @par Output Range: [-1, +1] for Random/LFO sources; [0, +1] for External
class SampleHoldSource : public ModulationSource {
public:
    static constexpr float kMinRate = 0.1f;
    static constexpr float kMaxRate = 50.0f;
    static constexpr float kDefaultRate = 4.0f;
    static constexpr float kMinSlew = 0.0f;
    static constexpr float kMaxSlew = 500.0f;
    static constexpr float kDefaultSlew = 0.0f;

    SampleHoldSource() noexcept = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// @brief Process one sample.
    void process() noexcept;

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override;
    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override;

    // Parameter setters
    void setInputType(SampleHoldInputType type) noexcept;
    void setRate(float hz) noexcept;
    void setSlewTime(float ms) noexcept;

    /// @brief Set pointers to LFO sources (called by engine during init).
    void setLFOPointers(const LFO* lfo1, const LFO* lfo2) noexcept;

    /// @brief Set current external input level (audio amplitude).
    void setExternalLevel(float level) noexcept;

private:
    [[nodiscard]] float sampleCurrentInput() noexcept;

    SampleHoldInputType inputType_ = SampleHoldInputType::Random;
    float rate_ = kDefaultRate;
    float slewMs_ = kDefaultSlew;
    float phase_ = 0.0f;
    float heldValue_ = 0.0f;
    float externalLevel_ = 0.0f;

    Xorshift32 rng_{54321};
    OnePoleSmoother outputSmoother_;
    double sampleRate_ = 44100.0;

    const LFO* lfo1Ptr_ = nullptr;
    const LFO* lfo2Ptr_ = nullptr;
};

}  // namespace DSP
}  // namespace Krate
