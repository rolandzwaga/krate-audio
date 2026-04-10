#pragma once

// ==============================================================================
// FrictionExciter -- Phase 2 stub (data-model.md §2.4)
// ==============================================================================
// Structurally-complete stub. Phase 2.A behaviour is silent. Phase 3 (T040)
// wires Krate::DSP::BowExciter in transient mode.
// ==============================================================================

#include <cstdint>

namespace Membrum {

struct FrictionExciter
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
