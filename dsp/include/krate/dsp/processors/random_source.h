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
#include <utility>

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

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        outputSmoother_.configure(5.0f, static_cast<float>(sampleRate));
        phase_ = 0.0f;
        currentTarget_ = rng_.nextFloat();
        outputSmoother_.snapTo(currentTarget_);
    }

    void reset() noexcept {
        phase_ = 0.0f;
        currentTarget_ = 0.0f;
        outputSmoother_.reset();
    }

    /// @brief Process a block at control rate (more efficient than per-sample).
    ///
    /// Advances phase by the full block duration and generates new random
    /// values for any triggers that occurred. Only the final trigger's value
    /// is captured (intra-block triggers are inaudible for modulation).
    ///
    /// @param numSamples Number of samples in the block
    void processBlock(size_t numSamples) noexcept {
        float rate = tempoSync_ ? tempoSyncRate() : rate_;
        float phaseInc = rate / static_cast<float>(sampleRate_);
        phase_ += phaseInc * static_cast<float>(numSamples);

        while (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            currentTarget_ = rng_.nextFloat();
        }

        if (smoothness_ <= 0.001f) {
            outputSmoother_.snapTo(currentTarget_);
        } else {
            float smoothMs = smoothness_ * 200.0f;
            outputSmoother_.configure(smoothMs, static_cast<float>(sampleRate_));
            outputSmoother_.setTarget(currentTarget_);
        }
        static_cast<void>(outputSmoother_.process());
    }

    /// @brief Process one sample.
    void process() noexcept {
        // Advance phase
        float rate = tempoSync_ ? tempoSyncRate() : rate_;
        float phaseInc = rate / static_cast<float>(sampleRate_);
        phase_ += phaseInc;

        // On trigger: generate new random value
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            currentTarget_ = rng_.nextFloat();  // [-1, 1]
        }

        // Apply smoothing based on smoothness parameter
        if (smoothness_ <= 0.001f) {
            // No smoothing: instant transitions
            outputSmoother_.snapTo(currentTarget_);
        } else {
            // Smoothing time proportional to smoothness (1ms to 200ms)
            float smoothMs = smoothness_ * 200.0f;
            outputSmoother_.configure(smoothMs, static_cast<float>(sampleRate_));
            outputSmoother_.setTarget(currentTarget_);
        }

        static_cast<void>(outputSmoother_.process());
    }

    // ModulationSource interface
    [[nodiscard]] float getCurrentValue() const noexcept override {
        return std::clamp(outputSmoother_.getCurrentValue(), -1.0f, 1.0f);
    }

    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};
    }

    // Parameter setters
    void setRate(float hz) noexcept {
        rate_ = std::clamp(hz, kMinRate, kMaxRate);
    }

    void setSmoothness(float normalized) noexcept {
        smoothness_ = std::clamp(normalized, kMinSmoothness, kMaxSmoothness);
    }

    void setTempoSync(bool enabled) noexcept {
        tempoSync_ = enabled;
    }

    void setTempo(float bpm) noexcept {
        bpm_ = std::clamp(bpm, 1.0f, 999.0f);
    }

    // Parameter getters
    [[nodiscard]] float getRate() const noexcept { return rate_; }
    [[nodiscard]] float getSmoothness() const noexcept { return smoothness_; }
    [[nodiscard]] bool isTempoSynced() const noexcept { return tempoSync_; }

private:
    [[nodiscard]] float tempoSyncRate() const noexcept {
        // Simple tempo sync: rate = BPM / 60
        return bpm_ / 60.0f;
    }

    float rate_ = kDefaultRate;
    float smoothness_ = kDefaultSmoothness;
    bool tempoSync_ = false;
    float bpm_ = 120.0f;

    float phase_ = 0.0f;
    float currentTarget_ = 0.0f;

    Xorshift32 rng_{98765};
    OnePoleSmoother outputSmoother_;
    double sampleRate_ = 44100.0;
};

}  // namespace DSP
}  // namespace Krate
