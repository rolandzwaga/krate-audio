// ==============================================================================
// Profundum Phase 6 (specs/profundum-phase6-mass-dynamics) - test-only probe
// ==============================================================================
// detail::TransientShaperProbe: the friend of TransientShaper (tasks.md T006, deviation 4:
// the shaper probe lives at processors level so dsp_processors_tests can include it; the
// MassDynamics probe is a separate systems header). Read-only after T006.
//
//   setHoldBypass          -> probeHoldBypass_          SC-009's negative control (plan S4.3 step 2)
//   setGainSmootherBypass  -> probeGainSmootherBypass_  SC-008(d)'s positive control (plan S4.3 step 6)
//   setSlowAttackArgMs     -> slow_.setAttackTime(argMs) the duration harvest's free variable
//                            (plan S8.1; tasks.md deviation 8). Call after prepare(): prepare()
//                            and setTransientDurationMs() re-apply the table argument.
//
// Every function is static.
// ==============================================================================

#pragma once

#include <krate/dsp/processors/transient_shaper.h>

namespace Krate::DSP::detail {

struct TransientShaperProbe {
    static void setHoldBypass(TransientShaper& s, bool on) noexcept { s.probeHoldBypass_ = on; }
    static void setGainSmootherBypass(TransientShaper& s, bool on) noexcept { s.probeGainSmootherBypass_ = on; }
    static void setSlowAttackArgMs(TransientShaper& s, float argMs) noexcept { s.slow_.setAttackTime(argMs); }
};

}  // namespace Krate::DSP::detail
