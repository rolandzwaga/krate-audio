#pragma once

// ==============================================================================
// NonlinearCoupling -- Phase 2 no-op stub (data-model.md §7)
// ==============================================================================
// amount_ == 0 is exact bypass (processSample(x) == x). Full topology added
// in Phase 6.
// ==============================================================================

namespace Membrum {

class NonlinearCoupling
{
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void reset() noexcept {}

    void setAmount(float amount) noexcept { amount_ = amount; }
    void setVelocity(float velocity) noexcept { velocity_ = velocity; }

    [[nodiscard]] float processSample(float bodyOutput) noexcept
    {
        // Phase 2.A: permanent bypass.
        return bodyOutput;
    }

private:
    float  amount_     = 0.0f;
    float  velocity_   = 0.0f;
    double sampleRate_ = 44100.0;
};

} // namespace Membrum
