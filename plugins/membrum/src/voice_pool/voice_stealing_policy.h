#pragma once

// ==============================================================================
// VoiceStealingPolicy -- Phase 3 user-selectable voice stealing policy
// ==============================================================================
// Exposed to the user as the `kVoiceStealingId` StringListParameter. Persisted
// as a single `uint8` byte in the v3 state blob (data-model.md §1, §7).
//
// FR references:
//   FR-120, FR-121, FR-122, FR-123
// ==============================================================================

#include <cstdint>

namespace Membrum {

enum class VoiceStealingPolicy : std::uint8_t
{
    Oldest   = 0,  // FR-121 -- default; maps to Krate::DSP::AllocationMode::Oldest
    Quietest = 1,  // FR-122 -- processor-layer-only per Clarification Q3
    Priority = 2,  // FR-123 -- maps to Krate::DSP::AllocationMode::HighestNote
};

} // namespace Membrum
