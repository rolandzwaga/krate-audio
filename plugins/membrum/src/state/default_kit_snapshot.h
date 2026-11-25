#pragma once

// ==============================================================================
// The default kit (DefaultKit::apply) as a KitSnapshot, so the processor and
// the controller install it through the same kit apply as every other kit
// (fix plan T2.7, MB-116 / MB-090).
// ==============================================================================

#include "dsp/default_kit.h"
#include "dsp/pad_config.h"
#include "state/state_codec.h"

#include <array>
#include <cstddef>

namespace Membrum::State {

/// The 32 default-kit pads; every global keeps its KitSnapshot default, which
/// equals the processor's own initial value (polyphony 8, stealing 0, global
/// coupling / snare buzz / tom resonance 0, coupling delay 1 ms, master gain
/// 0.5, selected pad 0).
[[nodiscard]] inline KitSnapshot makeDefaultKitSnapshot()
{
    std::array<PadConfig, kNumPads> pads{};
    DefaultKit::apply(pads);

    KitSnapshot kit;
    for (std::size_t p = 0; p < pads.size(); ++p)
        kit.pads[p] = toPadSnapshot(pads[p]);
    return kit;
}

} // namespace Membrum::State
