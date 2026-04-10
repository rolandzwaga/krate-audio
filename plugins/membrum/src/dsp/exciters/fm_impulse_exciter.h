#pragma once

// ==============================================================================
// FMImpulseExciter -- Phase 2 stub (data-model.md §2.5)
// ==============================================================================
// Structurally-complete stub. Phase 3 (T041) replaces this with a carrier +
// modulator FMOperator pair driven by a Chowning bell-like envelope.
// ==============================================================================

#include <cstdint>

namespace Membrum {

struct FMImpulseExciter
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
