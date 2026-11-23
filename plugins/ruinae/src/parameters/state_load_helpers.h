#pragma once

// ==============================================================================
// State-load helpers
// ==============================================================================
// A state blob comes from disk: a damaged or hostile one can hold any float.
// Every value a loader stores must land in the range the parameter-change
// handler produces, or the DSP sees values no automation can reach (R-147:
// OSC PhaseMod = 1e30 hung the audio thread).

#include <krate/dsp/core/db_utils.h>

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cstdint>

namespace Ruinae {

/// Clamps a loaded float to [lo, hi]; a non-finite value loads as `def`.
[[nodiscard]] inline float finiteClamp(float v, float lo, float hi, float def) noexcept {
    if (!Krate::DSP::detail::isFinite(v)) {
        return def;
    }
    return std::clamp(v, lo, hi);
}

/// A saved float field: the range its parameter handler produces and the
/// default a non-finite value loads as. A pack defines one per field and both
/// parsers, the processor's loadX and the controller's loadXToController, read
/// the field through it, so the two cannot clamp differently (R-211).
struct LoadedFloat {
    float lo;
    float hi;
    float def;
    [[nodiscard]] float operator()(float v) const noexcept { return finiteClamp(v, lo, hi, def); }
};

/// A saved index or count field: the range its parameter handler produces,
/// shared by both parsers like LoadedFloat.
struct LoadedInt {
    int lo;
    int hi;
    [[nodiscard]] int operator()(std::int32_t v) const noexcept {
        return std::clamp(static_cast<int>(v), lo, hi);
    }
};

/// Reads one float field and sanitises it through `field`; false at the end
/// of the stream (`v` is then left as it was).
[[nodiscard]] inline bool readLoaded(Steinberg::IBStreamer& streamer, float& v,
                                     const LoadedFloat& field) {
    if (!streamer.readFloat(v)) return false;
    v = field(v);
    return true;
}

/// Reads one int32 field and clamps it through `field`; false at the end of
/// the stream (`v` is then left as it was).
[[nodiscard]] inline bool readLoaded(Steinberg::IBStreamer& streamer, Steinberg::int32& v,
                                     const LoadedInt& field) {
    if (!streamer.readInt32(v)) return false;
    v = field(v);
    return true;
}

} // namespace Ruinae
