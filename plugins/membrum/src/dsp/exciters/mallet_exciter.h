#pragma once

// ==============================================================================
// MalletExciter -- Phase 2 stub (data-model.md §2.2)
// ==============================================================================
// Full implementation arrives in Phase 3 (T038). Phase 2.A provides a
// structurally-complete backend satisfying the exciter contract so
// ExciterBank's std::variant has a full set of alternatives.
//
// Until then it delegates to Krate::DSP::ImpactExciter with Phase 1's
// Impulse mapping — safe no-op behaviour that keeps Phase 1 tests green.
// ==============================================================================

#include <krate/dsp/processors/impact_exciter.h>

#include <cstdint>

namespace Membrum {

struct MalletExciter
{
    Krate::DSP::ImpactExciter core_;

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        core_.prepare(sampleRate, voiceId);
        core_.reset();
    }

    void reset() noexcept { core_.reset(); }

    void trigger(float velocity) noexcept
    {
        // Phase 2.A: use Phase 1 defaults so Phase 1 regression tests pass
        // regardless of which exciter alternative the variant currently holds.
        const float hardness   = 0.3f + (0.8f - 0.3f) * velocity;
        const float brightness = 0.15f + (0.4f - 0.15f) * velocity;
        core_.trigger(velocity, hardness, 0.3f, brightness, 0.0f, 0.0f);
    }

    void release() noexcept {}

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        return core_.process(0.0f);
    }

    [[nodiscard]] bool isActive() const noexcept { return core_.isActive(); }
};

} // namespace Membrum
