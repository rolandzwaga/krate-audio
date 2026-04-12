#pragma once

// ==============================================================================
// VoiceMeta -- Phase 3 per-slot voice pool metadata
// ==============================================================================
// Tracks one VoicePool slot's lifecycle state, fast-release gain, and the
// bookkeeping fields that drive voice stealing + choke groups. One instance per
// slot lives in `VoicePool::meta_` (main) and `VoicePool::releasingMeta_`
// (shadow, for the two-array crossfade technique -- see plan.md Architecture).
//
// Thread contract: all fields are read/written exclusively on the audio thread
// by `VoicePool`. No atomics (FR-165). The `alignas(64)` avoids false sharing
// if future work parallelizes across voices.
//
// FR references:
//   FR-110, FR-112, FR-124, FR-128, FR-132, FR-165, FR-172
// ==============================================================================

#include <array>
#include <cstdint>

namespace Membrum {

/// Internal lifecycle state for a single slot in `VoicePool`.
///
/// Deliberately NOT named `VoiceState` to avoid reader confusion with
/// `Krate::DSP::VoiceState` from the allocator, even though they live in
/// distinct namespaces (data-model.md §1).
enum class VoiceSlotState : std::uint8_t
{
    Free          = 0,  // slot is idle; allocator's view must also be Idle
    Active        = 1,  // voice is rendering normally; amp envelope is active
    FastReleasing = 2,  // voice is in its 5 ms fast-release tail (steal or choke)
};

/// Per-slot metadata. Kept cache-line-sized so future parallelization across
/// voices cannot suffer false sharing.
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4324)  // structure padded due to alignment specifier (intentional)
#endif
struct alignas(64) VoiceMeta
{
    float          currentLevel      = 0.0f;                    // FR-165 per-block peak of scratch
    float          fastReleaseGain   = 1.0f;                    // FR-124 multiplicative gain, 1 -> 0
    std::uint64_t  noteOnSampleCount = 0;                       // FR-128 monotonic note-on timestamp
    std::uint8_t   originatingNote   = 0;                       // FR-172 source MIDI note (36..67)
    std::uint8_t   originatingChoke  = 0;                       // FR-132 cached choke group at note-on
    VoiceSlotState state             = VoiceSlotState::Free;    // FR-172 slot lifecycle
    std::uint8_t   _pad              = 0;                       // reserved for Phase 4+ flags
};
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

// FR-172: VoiceMeta must fit a single cache line so future SIMD / threaded
// voice dispatch does not suffer false sharing across slots.
static_assert(sizeof(VoiceMeta) <= 64, "VoiceMeta must fit one cache line");

} // namespace Membrum
