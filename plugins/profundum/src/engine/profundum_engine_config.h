#pragma once

// ==============================================================================
// Profundum Engine Configuration (FR-053)   Plan section 2.2
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>

namespace Profundum {

/// FR-053: ProfundumEngineConfig{} unchanged -- PerVoice, sidechain oversampling on at 4x
/// (profundum_engine.h:84-90). Phase 9 measured its CPU figures with exactly this configuration
/// (phase-handoffs 10.1, 10.6, 10.9); any departure must be justified here against them.
[[nodiscard]] inline Krate::DSP::ProfundumEngineConfig makeProfundumEngineConfig() noexcept {
    return Krate::DSP::ProfundumEngineConfig{};
}

/// FR-023 step 4, FR-028: the output-gain ramp (99 % settle; plan 3.1).
inline constexpr float kOutputGainSmoothMs = 20.0f;

}  // namespace Profundum
