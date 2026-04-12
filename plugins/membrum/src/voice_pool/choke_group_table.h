#pragma once

// ==============================================================================
// ChokeGroupTable -- Phase 3 choke-group assignment lookup
// ==============================================================================
// A fixed 32-entry `uint8_t` table indexed by `(midiNote - 36)` covering the
// GM drum pad range 36..67. Phase 3 mirrors the same value across all 32
// entries on every `kChokeGroupId` parameter change (FR-138); Phase 4 becomes
// per-pad without a state format change (Clarification Q1).
//
// No dynamic container -- `entries_` is `std::array` so the table lives inline
// in `VoicePool` and never hits the heap.
//
// FR references:
//   FR-130, FR-131, FR-132, FR-138, FR-141, FR-144
// ==============================================================================

#include <array>
#include <cstdint>

namespace Membrum {

class ChokeGroupTable
{
public:
    static constexpr int kSize = 32;  // GM drum pads 36..67

    /// Write `group` into all 32 entries (FR-138 -- Phase 3 single-pad template).
    void setGlobal(std::uint8_t group) noexcept;

    /// Write `group` into a single entry (Phase 4 per-pad choke groups).
    void setEntry(std::size_t padIndex, std::uint8_t group) noexcept;

    /// Return the choke group assigned to `midiNote`. Notes outside [36, 67]
    /// return 0 (no choke).
    [[nodiscard]] std::uint8_t lookup(std::uint8_t midiNote) const noexcept;

    /// Raw access for `getState` serialization (FR-141).
    [[nodiscard]] const std::array<std::uint8_t, kSize>& raw() const noexcept
    {
        return entries_;
    }

    /// Load all 32 entries from a raw blob, clamping each byte to [0, 8]
    /// (FR-144). Called from `setState` during v3 load.
    void loadFromRaw(const std::array<std::uint8_t, kSize>& in) noexcept;

private:
    std::array<std::uint8_t, kSize> entries_{};  // FR-131 -- fixed-size inline storage
};

// ------------------------------------------------------------------
// Inline method bodies (Phase 3.1 -- T3.1.7).
// ------------------------------------------------------------------
// Phase 3.1 implements setGlobal / lookup / loadFromRaw. Phase 3.3 then
// wires the table into the note-on path via VoicePool::processChokeGroups.

inline void ChokeGroupTable::setGlobal(std::uint8_t group) noexcept
{
    // FR-138: mirror the value across all 32 entries (Phase 3 single-pad
    // template / Clarification Q1). Group is clamped to [0, 8] -- any larger
    // byte is treated as "no choke" (FR-144 semantics).
    const std::uint8_t clamped = (group > 8U) ? std::uint8_t{0} : group;
    for (int i = 0; i < kSize; ++i)
        entries_[static_cast<std::size_t>(i)] = clamped;
}

inline void ChokeGroupTable::setEntry(std::size_t padIndex, std::uint8_t group) noexcept
{
    if (padIndex >= static_cast<std::size_t>(kSize))
        return;
    entries_[padIndex] = (group > 8U) ? std::uint8_t{0} : group;
}

inline std::uint8_t ChokeGroupTable::lookup(std::uint8_t midiNote) const noexcept
{
    // FR-132: return 0 for any note outside the GM drum-pad range [36, 67];
    // otherwise return the assigned group (already in [0, 8]).
    if (midiNote < 36U || midiNote > 67U)
        return 0U;
    return entries_[static_cast<std::size_t>(midiNote - 36U)];
}

inline void ChokeGroupTable::loadFromRaw(const std::array<std::uint8_t, kSize>& in) noexcept
{
    // FR-144: clamp every byte on load; any value > 8 is stored as 0.
    for (int i = 0; i < kSize; ++i)
    {
        const std::uint8_t v = in[static_cast<std::size_t>(i)];
        entries_[static_cast<std::size_t>(i)] = (v > 8U) ? std::uint8_t{0} : v;
    }
}

} // namespace Membrum
