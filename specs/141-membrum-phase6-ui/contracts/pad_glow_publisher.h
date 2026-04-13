// ==============================================================================
// PadGlowPublisher -- Lock-Free 1024-bit Amplitude Publisher (Phase 6 Contract)
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-014, FR-101, R3)
// Plan: specs/141-membrum-phase6-ui/plan.md
// Data model: specs/141-membrum-phase6-ui/data-model.md section 4
//
// Purpose:
//   Publish per-pad voice-envelope amplitude from the audio thread to the UI
//   thread using a pre-allocated 128-byte bitfield (32 words x 32 bits = 1024
//   bits). Each pad occupies one std::atomic<uint32_t> word. Amplitude is
//   quantised to 5 bits (32 buckets) and encoded as a one-hot bit position.
//
// Threading contract:
//   - publish() is called from the audio thread, at most once per pad per
//     audio block. Uses memory_order_relaxed.
//   - snapshot() / readPadBucket() are called from the UI thread at <= 30 Hz
//     via a CVSTGUITimer. Use memory_order_acquire.
//
// Correctness invariants:
//   - std::atomic<uint32_t> is lock-free on all supported platforms
//     (static_assert at construction).
//   - A torn read is not possible for a single 32-bit atomic on x86/x64/ARMv8.
//   - Worst-case visual error: UI thread reads a stale value from the previous
//     frame (<= 33 ms lag). Safe, bounded, and within SC-005's 50 ms budget.
// ==============================================================================

#pragma once

#include "dsp/pad_config.h"
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>

namespace Membrum {

class PadGlowPublisher
{
public:
    static constexpr int          kNumPads          = 32;
    static constexpr int          kAmplitudeBuckets = 32;   // 5 bits per pad
    static_assert(kAmplitudeBuckets == 32, "Bucket count must match uint32_t width");

    PadGlowPublisher() noexcept {
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                      "PadGlowPublisher requires lock-free 32-bit atomics");
    }

    // UI-thread (or Processor::setActive(false)): clear all words.
    void reset() noexcept;

    // Audio-thread: quantise amplitude [0..1] to a 5-bit bucket and encode as
    // one-hot in the pad's word. Overwrites unconditionally.
    void publish(int padIndex, float amplitude) noexcept;

    // UI-thread: snapshot current amplitude buckets for all 32 pads.
    // out[padIndex] = bucket in [0..31], where 0 means silent.
    void snapshot(std::array<std::uint8_t, kNumPads>& out) const noexcept;

    // UI-thread convenience: read a single pad's current bucket.
    [[nodiscard]] std::uint8_t readPadBucket(int padIndex) const noexcept;

private:
    alignas(64) std::array<std::atomic<std::uint32_t>, kNumPads> words_{};
};

// ==============================================================================
// Inline implementations (header-only)
// ==============================================================================

inline void PadGlowPublisher::reset() noexcept
{
    for (auto& w : words_)
        w.store(0u, std::memory_order_relaxed);
}

inline void PadGlowPublisher::publish(int padIndex, float amplitude) noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return;
    const float clamped = (amplitude < 0.0f) ? 0.0f : ((amplitude > 1.0f) ? 1.0f : amplitude);
    const int bucket = static_cast<int>(clamped * static_cast<float>(kAmplitudeBuckets - 1) + 0.5f);
    const std::uint32_t word = (bucket > 0) ? (1u << bucket) : 0u;
    words_[static_cast<std::size_t>(padIndex)].store(word, std::memory_order_relaxed);
}

inline void PadGlowPublisher::snapshot(std::array<std::uint8_t, kNumPads>& out) const noexcept
{
    for (int i = 0; i < kNumPads; ++i) {
        const std::uint32_t w = words_[static_cast<std::size_t>(i)].load(std::memory_order_acquire);
        out[static_cast<std::size_t>(i)] =
            (w == 0u) ? std::uint8_t{0}
                      : static_cast<std::uint8_t>(31 - std::countl_zero(w));
    }
}

inline std::uint8_t PadGlowPublisher::readPadBucket(int padIndex) const noexcept
{
    if (padIndex < 0 || padIndex >= kNumPads)
        return 0;
    const std::uint32_t w = words_[static_cast<std::size_t>(padIndex)].load(std::memory_order_acquire);
    return (w == 0u) ? std::uint8_t{0}
                     : static_cast<std::uint8_t>(31 - std::countl_zero(w));
}

} // namespace Membrum
