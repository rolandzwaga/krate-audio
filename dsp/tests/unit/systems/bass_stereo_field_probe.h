// ==============================================================================
// Profundum Phase 7 (specs/profundum-phase7-stereo-field) - test-only probe
// ==============================================================================
// detail::BassStereoFieldProbe: the friend of BassStereoField (tasks.md T003, deviation 5). The
// calibration harvest (T013 / T015, plan S9.1) evaluates candidate g_floor / K_wide values without
// editing the header per candidate. The field holds sideFloorGain_ and wideOffsetScale_ initialised
// from kSideFloorGain / kWideOffsetScale; only this probe changes them, and reset() / prepare() leave
// them alone. Precedent: dsp/tests/unit/systems/bass_character_stage_probe.h. Shared by several TUs,
// so it lives in one header (ODR). Read-only after T003.
//
// Every function is static.
// ==============================================================================

#pragma once

#include <krate/dsp/systems/bass_stereo_field.h>

namespace Krate::DSP::detail {

struct BassStereoFieldProbe {
    /// g_floor for this instance: the per-interval side-gain target becomes g * (1 - gravity) from the
    /// next advanceControl() or reset().
    static void setSideFloorGain(BassStereoField& f, float g) noexcept { f.sideFloorGain_ = g; }
    /// K_wide for this instance (read by wideOffsets, T005).
    static void setWideOffsetScale(BassStereoField& f, float k) noexcept { f.wideOffsetScale_ = k; }
    [[nodiscard]] static float sideFloorGain(const BassStereoField& f) noexcept { return f.sideFloorGain_; }
    [[nodiscard]] static float wideOffsetScale(const BassStereoField& f) noexcept { return f.wideOffsetScale_; }
};

}  // namespace Krate::DSP::detail
