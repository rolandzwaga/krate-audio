#pragma once

// ==============================================================================
// ModeInject -- Phase 2 no-op stub (data-model.md §7)
// ==============================================================================

#include <cstdint>

namespace Membrum {

class ModeInject
{
public:
    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        sampleRate_ = sampleRate;
        voiceId_    = voiceId;
    }
    void reset() noexcept {}

    void setAmount(float amount) noexcept { amount_ = amount; }
    void setFundamentalHz(float hz) noexcept { fundamentalHz_ = hz; }

    void trigger() noexcept {}

    // FR-052 zero-leak: amount_ == 0 returns 0 without any processing.
    [[nodiscard]] float process() noexcept { return 0.0f; }

private:
    float  amount_        = 0.0f;
    float  fundamentalHz_ = 440.0f;
    double sampleRate_    = 44100.0;
    std::uint32_t voiceId_ = 0;
};

} // namespace Membrum
