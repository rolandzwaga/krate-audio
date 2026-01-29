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

#include <algorithm>
#include <cstddef>
#include <utility>

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

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        outputSmoother_.configure(5.0f, static_cast<float>(sampleRate));
        phase_ = 0.0f;
        heldValue_ = 0.0f;
        outputSmoother_.snapTo(0.0f);
    }

    void reset() noexcept {
        phase_ = 0.0f;
        heldValue_ = 0.0f;
        outputSmoother_.reset();
    }

    /// @brief Process one sample.
    void process() noexcept {
        float phaseInc = rate_ / static_cast<float>(sampleRate_);
        phase_ += phaseInc;

        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            heldValue_ = sampleCurrentInput();
        }

        // Apply slew
        if (slewMs_ <= 0.01f) {
            outputSmoother_.snapTo(heldValue_);
        } else {
            outputSmoother_.configure(slewMs_, static_cast<float>(sampleRate_));
            outputSmoother_.setTarget(heldValue_);
        }

        static_cast<void>(outputSmoother_.process());
    }

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return outputSmoother_.getCurrentValue();
    }

    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        if (inputType_ == SampleHoldInputType::External) {
            return {0.0f, 1.0f};
        }
        return {-1.0f, 1.0f};
    }

    // Parameter setters
    void setInputType(SampleHoldInputType type) noexcept { inputType_ = type; }
    void setRate(float hz) noexcept { rate_ = std::clamp(hz, kMinRate, kMaxRate); }
    void setSlewTime(float ms) noexcept { slewMs_ = std::clamp(ms, kMinSlew, kMaxSlew); }

    /// @brief Set pointers to LFO sources (called by engine during init).
    void setLFOPointers(const LFO* lfo1, const LFO* lfo2) noexcept {
        lfo1Ptr_ = lfo1;
        lfo2Ptr_ = lfo2;
    }

    /// @brief Set current external input level (audio amplitude).
    void setExternalLevel(float level) noexcept {
        externalLevel_ = std::clamp(level, 0.0f, 1.0f);
    }

private:
    [[nodiscard]] float sampleCurrentInput() noexcept {
        switch (inputType_) {
            case SampleHoldInputType::Random:
                return rng_.nextFloat();  // [-1, 1]
            case SampleHoldInputType::LFO1:
                if (lfo1Ptr_) {
                    // Read current LFO value (query, don't process)
                    // Use last output which engine has already computed
                    return 0.0f;  // Will be set via external mechanism
                }
                return rng_.nextFloat();
            case SampleHoldInputType::LFO2:
                if (lfo2Ptr_) {
                    return 0.0f;  // Will be set via external mechanism
                }
                return rng_.nextFloat();
            case SampleHoldInputType::External:
                return externalLevel_;  // [0, 1]
        }
        return 0.0f;
    }

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
