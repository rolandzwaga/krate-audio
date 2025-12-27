// Layer 2: DSP Processor - Grain Scheduler
// Part of Granular Delay feature (spec 034)
#pragma once

#include "dsp/core/random.h"

#include <cstdint>

namespace Iterum::DSP {

/// Scheduling mode for grain triggering
enum class SchedulingMode : uint8_t {
    Asynchronous,  ///< Stochastic timing based on density (default)
    Synchronous    ///< Regular intervals for pitched output
};

/// Controls when grains are triggered based on density settings.
/// Supports both synchronous (regular) and asynchronous (stochastic) modes.
class GrainScheduler {
public:
    /// Prepare scheduler for processing
    /// @param sampleRate Current sample rate
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        reset();
    }

    /// Reset scheduler state
    void reset() noexcept {
        samplesUntilNextGrain_ = 0.0f;
        calculateInteronset();
    }

    /// Set grain density (grains per second)
    /// @param grainsPerSecond Trigger rate (1-100 typical range)
    void setDensity(float grainsPerSecond) noexcept {
        density_ = std::max(0.1f, grainsPerSecond);
        calculateInteronset();
    }

    /// Get current density
    [[nodiscard]] float getDensity() const noexcept { return density_; }

    /// Set scheduling mode
    /// @param mode Synchronous or Asynchronous
    void setMode(SchedulingMode mode) noexcept { mode_ = mode; }

    /// Get current scheduling mode
    [[nodiscard]] SchedulingMode getMode() const noexcept { return mode_; }

    /// Process one sample
    /// @return true if a new grain should be triggered this sample
    [[nodiscard]] bool process() noexcept {
        samplesUntilNextGrain_ -= 1.0f;

        if (samplesUntilNextGrain_ <= 0.0f) {
            // Time for a new grain
            if (mode_ == SchedulingMode::Asynchronous) {
                // Add jitter for stochastic timing
                const float jitter = rng_.nextUnipolar() * 0.5f;  // +/- 25% jitter
                samplesUntilNextGrain_ = interonsetSamples_ * (0.75f + jitter);
            } else {
                // Regular intervals
                samplesUntilNextGrain_ = interonsetSamples_;
            }
            return true;
        }

        return false;
    }

    /// Seed RNG for reproducible behavior (useful for testing)
    /// @param seedValue Seed for random number generator
    void seed(uint32_t seedValue) noexcept { rng_ = Xorshift32(seedValue); }

private:
    void calculateInteronset() noexcept {
        // Interonset interval = samples per grain = sampleRate / density
        interonsetSamples_ = static_cast<float>(sampleRate_) / density_;
    }

    float samplesUntilNextGrain_ = 0.0f;
    float interonsetSamples_ = 4410.0f;  // Default ~10 grains/sec at 44.1kHz
    float density_ = 10.0f;              // grains per second
    SchedulingMode mode_ = SchedulingMode::Asynchronous;
    Xorshift32 rng_{12345};
    double sampleRate_ = 44100.0;
};

}  // namespace Iterum::DSP
