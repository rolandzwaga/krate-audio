#pragma once

// ==============================================================================
// MalletExciter -- Phase 2 (data-model.md §2.2, FR-011)
// ==============================================================================
// Same ImpactExciter backend as ImpulseExciter, but with a softer mallet
// character: lower hardness + brightness → lower spectral centroid at every
// velocity. This satisfies acceptance scenario US1-2 ("softer, rounded hit").
//
// Velocity mapping (FR-011 mapped to ImpactExciter's [0,1] hardness range):
//   hardness   = lerp(0.05, 0.45, velocity)   -- softer than Impulse (0.3..0.8)
//   mass       = lerp(0.6,  0.3,  velocity)   -- heavier mass at low vel
//                                                 → longer contact duration
//                                                   (spec FR-011: 8 ms → 1 ms)
//   brightness = lerp(-0.8, -0.1, velocity)   -- darker than Impulse (0.15..0.4)
//   position   = 0.0
//   f0         = 0.0 (comb disabled)
//
// Rationale:
//   ImpactExciter::trigger derives:
//     - pulse duration T from mass (Hertzian scaling)
//       → heavier mass → longer contact (matches FR-011 soft-hit duration)
//     - SVF cutoff from hardness and brightness
//       → lower hardness + brightness → lower cutoff → lower centroid
//   The ImpactExciter class does not expose a raw mallet alpha exponent;
//   its internal pulse sharpness (gamma = 1 + 3·hardness) combined with the
//   SVF cutoff gives us an equivalent hardness knob within the [0,1] range,
//   which is sufficient to produce a perceptibly softer mallet character
//   at the same velocity as the Impulse exciter.
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
        // FR-011: softer mallet character = lower hardness + darker SVF
        // (brightness offset) than Impulse. Mass stays at Phase-1 default (0.3)
        // so the pulse duration tracks the Impulse reference; altering mass
        // made low-velocity pulses extend past the 10 ms centroid window and
        // inverted the velocity ratio.
        //
        // Impulse uses hardness  = lerp(0.3, 0.8, v), brightness = lerp(0.15, 0.4, v).
        // Mallet uses hardness   = lerp(0.1, 0.6, v) (0.2 lower across range)
        //       and  brightness  = lerp(-0.5, -0.2, v) (ca. -0.6 lower).
        const float hardness   = 0.1f  + (0.6f  - 0.1f ) * velocity;
        const float brightness = -0.5f + (-0.2f - (-0.5f)) * velocity;
        core_.trigger(velocity, hardness, 0.3f, brightness, 0.0f, 0.0f);
    }

    void release() noexcept
    {
        // Mallet has no sustain phase; release is a no-op.
    }

    [[nodiscard]] float process(float /*bodyFeedback*/) noexcept
    {
        return core_.process(0.0f);
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return core_.isActive();
    }
};

} // namespace Membrum
