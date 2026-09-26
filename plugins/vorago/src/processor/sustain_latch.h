#pragma once

// ==============================================================================
// Vorago Phase 12 - Vorago::SustainLatch (C-9, FR-030, plan section 4.7)
// ==============================================================================
// Fixed-size, allocation-free sustain-pedal latch. Audio thread only.
//
// held_    : keys physically down.
// latched_ : keys released while the pedal was down (engine noteOff deferred).
//
// A re-strike of a latched note clears its latch mark (the caller dispatches the
// note-on normally); a later pedal-up therefore skips it while the key is held.
// Note numbers >= 128 are ignored (never latched; noteOff returns true).
// ==============================================================================

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace Vorago {

class SustainLatch {
public:
    static constexpr std::size_t kNumNotes = 128;

    void noteOn(std::uint8_t n) noexcept {
        if (n >= kNumNotes) return;
        held_.set(n);
        latched_.reset(n);
    }

    /// @return true if the caller must send the engine noteOff now; false if latched.
    [[nodiscard]] bool noteOff(std::uint8_t n) noexcept {
        if (n >= kNumNotes) return true;
        held_.reset(n);
        if (down_) {
            latched_.set(n);
            return false;
        }
        return true;
    }

    /// Pedal transition. On down -> up, every latched note whose key is not held
    /// is passed to `release(std::uint8_t)` in ascending order; the latch clears.
    template <typename Release>
    void setPedal(bool down, Release&& release) noexcept {
        if (down_ && !down) {
            for (std::size_t n = 0; n < kNumNotes; ++n) {
                if (latched_.test(n) && !held_.test(n)) release(static_cast<std::uint8_t>(n));
            }
            latched_.reset();
        }
        down_ = down;
    }

    /// Releases every latched note (setActive(false) path) and lifts the pedal.
    template <typename Release>
    void releaseAll(Release&& release) noexcept {
        for (std::size_t n = 0; n < kNumNotes; ++n) {
            if (latched_.test(n)) release(static_cast<std::uint8_t>(n));
        }
        latched_.reset();
        down_ = false;
    }

    /// Forgets all state without releasing anything (setState path).
    void clearWithoutRelease() noexcept {
        held_.reset();
        latched_.reset();
        down_ = false;
    }

    [[nodiscard]] bool isDown() const noexcept { return down_; }

private:
    std::bitset<kNumNotes> held_{};
    std::bitset<kNumNotes> latched_{};
    bool down_ = false;
};

}  // namespace Vorago
