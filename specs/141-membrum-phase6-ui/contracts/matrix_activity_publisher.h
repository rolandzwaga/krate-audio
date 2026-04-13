// ==============================================================================
// MatrixActivityPublisher -- Lock-Free 1024-bit Coupling Activity Mask
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-052)
// Plan: specs/141-membrum-phase6-ui/plan.md
// Data model: specs/141-membrum-phase6-ui/data-model.md section 5
//
// Purpose:
//   Publish per-source coupling activity masks from the audio thread to the UI
//   thread. Each of the 32 sources (pads) has one 32-bit word; bit D in
//   activityMask_[S] = "source S is currently driving coupling energy above
//   threshold into destination D". This is consumed by CouplingMatrixView to
//   draw per-cell activity outlines during playback.
//
// Threading contract:
//   - publishSourceActivity() is called from the audio thread, at most once
//     per source per audio block. memory_order_relaxed.
//   - readSourceActivity() / snapshot() are called from the UI thread at
//     <= 30 Hz via CVSTGUITimer. memory_order_acquire.
// ==============================================================================

#pragma once

#include "dsp/pad_config.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace Membrum {

class MatrixActivityPublisher
{
public:
    static constexpr int kNumPads = 32;

    MatrixActivityPublisher() noexcept {
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                      "MatrixActivityPublisher requires lock-free 32-bit atomics");
    }

    void reset() noexcept;

    // Audio-thread: publish the 32-bit destination mask for source `src`.
    // Bit D set -> pair (src, dst) is active this block.
    void publishSourceActivity(int src, std::uint32_t dstMask) noexcept;

    // UI-thread: read a single source's activity mask.
    [[nodiscard]] std::uint32_t readSourceActivity(int src) const noexcept;

    // UI-thread: snapshot all 32 source masks.
    void snapshot(std::array<std::uint32_t, kNumPads>& out) const noexcept;

private:
    alignas(64) std::array<std::atomic<std::uint32_t>, kNumPads> activityMask_{};
};

// ==============================================================================
// Inline implementations
// ==============================================================================

inline void MatrixActivityPublisher::reset() noexcept
{
    for (auto& w : activityMask_)
        w.store(0u, std::memory_order_relaxed);
}

inline void MatrixActivityPublisher::publishSourceActivity(
    int src, std::uint32_t dstMask) noexcept
{
    if (src < 0 || src >= kNumPads)
        return;
    activityMask_[static_cast<std::size_t>(src)].store(dstMask, std::memory_order_relaxed);
}

inline std::uint32_t MatrixActivityPublisher::readSourceActivity(int src) const noexcept
{
    if (src < 0 || src >= kNumPads)
        return 0u;
    return activityMask_[static_cast<std::size_t>(src)].load(std::memory_order_acquire);
}

inline void MatrixActivityPublisher::snapshot(
    std::array<std::uint32_t, kNumPads>& out) const noexcept
{
    for (int i = 0; i < kNumPads; ++i)
        out[static_cast<std::size_t>(i)] = activityMask_[static_cast<std::size_t>(i)].load(
            std::memory_order_acquire);
}

} // namespace Membrum
