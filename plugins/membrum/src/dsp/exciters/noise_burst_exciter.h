#pragma once

// ==============================================================================
// NoiseBurstExciter -- Phase 2 stub (data-model.md §2.3)
// ==============================================================================
// Structurally-complete stub. Phase 2.A behaviour is silent (process() returns
// 0). Phase 3 (T039) replaces this with a NoiseOscillator + SVF + linear decay
// envelope implementation.
// ==============================================================================

#include <cstdint>

namespace Membrum {

struct NoiseBurstExciter
{
    void prepare(double /*sampleRate*/, std::uint32_t /*voiceId*/) noexcept {}
    void reset() noexcept { active_ = false; }
    void trigger(float /*velocity*/) noexcept { active_ = false; }
    void release() noexcept {}
    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept { return 0.0f; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }

private:
    bool active_ = false;
};

} // namespace Membrum
