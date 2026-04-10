#pragma once

// ==============================================================================
// ImpulseExciter -- Phase 1 carry-over (data-model.md §2.1)
// ==============================================================================
// Wraps Krate::DSP::ImpactExciter with Phase 1's default parameter mapping:
//   hardness    = lerp(0.3, 0.8, velocity)
//   mass        = 0.3
//   brightness  = lerp(0.15, 0.4, velocity)
//   position    = 0.0
//   f0          = 0.0 (comb disabled)
// ==============================================================================

#include <krate/dsp/processors/impact_exciter.h>

#include <cstdint>

namespace Membrum {

struct ImpulseExciter
{
    Krate::DSP::ImpactExciter core_;

    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        core_.prepare(sampleRate, voiceId);
        core_.reset();
    }

    void reset() noexcept
    {
        core_.reset();
    }

    void trigger(float velocity) noexcept
    {
        const float hardness   = 0.3f + (0.8f - 0.3f) * velocity;
        const float brightness = 0.15f + (0.4f - 0.15f) * velocity;
        core_.trigger(velocity, hardness, 0.3f, brightness, 0.0f, 0.0f);
    }

    void release() noexcept
    {
        // Impulse has no sustain phase; release is a no-op.
    }

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        // FR-010 bit-identical-to-Phase-1: do NOT clamp here. Phase 1 ran the
        // raw ImpactExciter output straight into ModalResonatorBank, which
        // attenuates the transient. Clamping here would break the Phase 1
        // regression golden. The contract's ≤0 dBFS peak is enforced at the
        // voice level after the body + amp envelope stages.
        return core_.process(0.0f);
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return core_.isActive();
    }
};

} // namespace Membrum
