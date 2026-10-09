#pragma once

// ==============================================================================
// Vorago Phase 14 - getTailSamples() estimate (FR-060, SC-030, plan section 5.3)
// ==============================================================================
// Header-only, allocation-free. tail = Rel + RT60_eff + G, where RT60_eff is the
// stored cavern decay with the KNOB macros applied through the shipped macro
// matrix and clamped to the cavern's range, and G is the ghost-grain ceiling.
// ==============================================================================

#include "parameters/space_params.h"
#include <krate/dsp/systems/atmosphere_engine.h>
#include <krate/dsp/systems/vorago_macro_matrix.h>
#include <algorithm>

namespace Vorago {

/// FR-060's G: the longest life a ghost grain born before NoteOff can have
/// (atmosphere_engine.h:311; setGrainSeconds clamps to it, :855-857). A
/// state-independent ceiling, NOT the 12 s the engine configures.
inline constexpr double kGhostGrainCeilingSeconds =
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxGrainSeconds);

/// C-6 / FR-060 effective RT60: the shipped matrix's CavernDecaySeconds with the
/// stored decay installed as that target's base, the KNOB macros applied, then
/// clamped to the cavern's [0.5, 60] s range (space_params.h:58-59). One
/// definition, used by getTailSamples AND by the harness timeline.
[[nodiscard]] inline float effectiveCavernDecaySeconds(const Krate::DSP::VoragoMacroValues& knobs,
                                                       float storedDecaySeconds) noexcept {
    Krate::DSP::VoragoMacroMatrix m{};
    m.setTargetBase(Krate::DSP::VoragoMacroTarget::CavernDecaySeconds, storedDecaySeconds);
    m.setMacros(knobs);
    return std::clamp(m.computeCavernTargets().decaySeconds,
                      static_cast<float>(kSpaceDecayMinSeconds),
                      static_cast<float>(kSpaceDecayMaxSeconds));
}

/// FR-060: Rel + RT60_eff + G, in seconds.
[[nodiscard]] inline double tailSeconds(float releaseMs, float rt60Seconds) noexcept {
    return static_cast<double>(releaseMs) / 1000.0 + static_cast<double>(rt60Seconds) +
           kGhostGrainCeilingSeconds;
}

}  // namespace Vorago
