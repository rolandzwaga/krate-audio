#pragma once

// ==============================================================================
// State-load helpers
// ==============================================================================
// A state blob comes from disk: a damaged or hostile one can hold any float.
// Every value a loader stores must land in the range the parameter-change
// handler produces, or the DSP sees values no automation can reach (R-147:
// OSC PhaseMod = 1e30 hung the audio thread).

#include <krate/dsp/core/db_utils.h>

#include <algorithm>

namespace Ruinae {

/// Clamps a loaded float to [lo, hi]; a non-finite value loads as `def`.
[[nodiscard]] inline float finiteClamp(float v, float lo, float hi, float def) noexcept {
    if (!Krate::DSP::detail::isFinite(v)) {
        return def;
    }
    return std::clamp(v, lo, hi);
}

} // namespace Ruinae
