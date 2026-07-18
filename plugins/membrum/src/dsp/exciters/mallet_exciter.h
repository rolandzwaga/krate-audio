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

#include <algorithm>
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

    /// D4 (06-orchestralKit-fix-plan.md): body-size contact hint. The fixed
    /// mass 0.3 gives a ~9.5 ms base Hertzian contact -- right for drums,
    /// hopeless for small metals (a ~8 ms Hann-ish pulse has a spectral null
    /// region near ~244 Hz and needs to be ~0.5-2 ms for crotales/triangle).
    /// Piecewise: size >= 0.5 keeps the legacy mass EXACTLY (toms/timpani/
    /// gran cassa untouched -- the tension-glide calibration depends on the
    /// legacy contact energy); below 0.5 the effective mass shrinks
    /// cubically, giving a crotale (0.12) a ~1.3 ms contact at v=1.
    /// (Cubic, not quadratic -- see the rationale in trigger(): the quadratic
    /// curve left a crotale at ~2 ms, still too long.)
    /// Deliberately NOT velocity-mapped: velocity-mapped mass was tried and
    /// reverted (broke the 10 ms centroid window / velocity-ratio tests,
    /// US1-2).
    void setBodySizeHint(float sizeNorm) noexcept
    {
        bodySizeHint_ = std::clamp(sizeNorm, 0.0f, 1.0f);
    }

    void trigger(float velocity) noexcept
    {
        // FR-011: softer mallet character = lower hardness + darker SVF
        // (brightness offset) than Impulse. Mass follows the Phase-1 default
        // (0.3) scaled by the body-size hint (D4); the VELOCITY mapping never
        // touches mass -- altering mass by velocity made low-velocity pulses
        // extend past the 10 ms centroid window and inverted the velocity
        // ratio.
        //
        // Impulse uses hardness  = lerp(0.3, 0.8, v), brightness = lerp(0.15, 0.4, v).
        // Mallet uses hardness   = lerp(0.1, 0.6, v) (0.2 lower across range)
        //       and  brightness  = lerp(-0.5, -0.2, v) (ca. -0.6 lower).
        const float hardness   = 0.1f  + (0.6f  - 0.1f ) * velocity;
        const float brightness = -0.5f + (-0.2f - (-0.5f)) * velocity;
        // D4 piecewise mass: legacy 0.3 for size >= 0.5, cubic shrink below
        // (continuous at the knee). Cubic because the measured excitation
        // envelope carries an SVF/noise tail past the Hertzian T -- the
        // quadratic curve left a crotale at ~2 ms total, still outside the
        // 0.5-2 ms small-metal class.
        const float t    = std::min(bodySizeHint_ / 0.5f, 1.0f);
        const float mass = 0.3f * t * t * t;
        core_.trigger(velocity, hardness, mass, brightness, 0.0f, 0.0f);
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

private:
    float bodySizeHint_ = 1.0f;  // D4: neutral -> legacy fixed mass 0.3
};

} // namespace Membrum
