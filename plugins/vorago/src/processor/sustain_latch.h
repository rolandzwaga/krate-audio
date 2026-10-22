#pragma once

// ==============================================================================
// Vorago::SustainLatch - alias of Krate::Plugins::SustainLatch
// ==============================================================================
// Promoted to plugins/shared/src/midi/sustain_latch.h by Profundum Phase 11
// (FR-061). Vorago keeps its name; there is no second definition and no
// call-site change.
// ==============================================================================

#include "midi/sustain_latch.h"   // plugins/shared/src/midi/sustain_latch.h

namespace Vorago {
using SustainLatch = Krate::Plugins::SustainLatch;   // FR-061
}  // namespace Vorago
