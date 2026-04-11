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
// Inline stub bodies (Phase 3.0 scaffolding -- real implementations land
// in Phase 3.3).
// ------------------------------------------------------------------

inline void ChokeGroupTable::setGlobal(std::uint8_t /*group*/) noexcept
{
    // Phase 3.0 scaffolding stub -- full implementation in Phase 3.3.
}

inline std::uint8_t ChokeGroupTable::lookup(std::uint8_t /*midiNote*/) const noexcept
{
    // Phase 3.0 scaffolding stub -- full implementation in Phase 3.3.
    return 0;
}

inline void ChokeGroupTable::loadFromRaw(const std::array<std::uint8_t, kSize>& /*in*/) noexcept
{
    // Phase 3.0 scaffolding stub -- full implementation in Phase 3.3.
}

} // namespace Membrum
