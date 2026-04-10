#pragma once

// ==============================================================================
// UnnaturalZone -- Phase 2 stub container (data-model.md §7)
// ==============================================================================
// Houses Material Morph, Mode Inject, Nonlinear Coupling plus the two scalar
// parameters (Mode Stretch, Decay Skew) that flow through to the body mappers.
// ==============================================================================

#include "material_morph.h"
#include "mode_inject.h"
#include "nonlinear_coupling.h"

#include <cstdint>

namespace Membrum {

class UnnaturalZone
{
public:
    void prepare(double sampleRate, std::uint32_t voiceId) noexcept
    {
        materialMorph.prepare(sampleRate);
        modeInject.prepare(sampleRate, voiceId);
        nonlinearCoupling.prepare(sampleRate);
    }

    void reset() noexcept
    {
        materialMorph.reset();
        modeInject.reset();
        nonlinearCoupling.reset();
    }

    void setModeStretch(float value) noexcept { modeStretch_ = value; }
    void setDecaySkew(float value) noexcept { decaySkew_ = value; }
    [[nodiscard]] float getModeStretch() const noexcept { return modeStretch_; }
    [[nodiscard]] float getDecaySkew() const noexcept { return decaySkew_; }

    // Audio-rate components owned here
    MaterialMorph     materialMorph;
    ModeInject        modeInject;
    NonlinearCoupling nonlinearCoupling;

private:
    float modeStretch_ = 1.0f;
    float decaySkew_   = 0.0f;
};

} // namespace Membrum
