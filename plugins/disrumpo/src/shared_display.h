#pragma once

// ==============================================================================
// SharedDisplay: the Tier-3 display bridge record (processor -> controller)
// ==============================================================================
// The processor registers a pointer to its SharedDisplay with
// Krate::Plugins::SharedDisplayBridge (a void* registry); the controller looks
// it up and casts it back. Both sides include this one definition, so the
// layout the controller reads is the layout the processor wrote (audit D-112).
// Holds only pointers: no processor or controller types.
// ==============================================================================

#include "meter_window.h"

#include <krate/dsp/primitives/spectrum_fifo.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace Disrumpo {

/// Level-meter windows for Tier 3: a sequence lock over atomics, one writer
/// (the audio thread, publish) and one reader (the UI thread, consume). A
/// window the reader has not consumed yet is merged into the next one, so no
/// peak or clip is lost between two reads; nothing is read twice.
class SharedMeters {
public:
    /// Audio thread, once per send window. Lock-free, no allocation.
    void publish(const MeterWindow& window) noexcept {
        const uint32_t seq = sequence_.load(std::memory_order_relaxed);
        // An unread window joins this one. Past kMaxCarrySamples unread (no
        // reader: editor closed) the carry restarts, so it never grows without bound
        const bool unread = consumed_.load(std::memory_order_acquire) != seq;
        if (unread && carry_.numSamples < kMaxCarrySamples)
            mergeMeterWindow(carry_, window);
        else
            carry_ = window;

        sequence_.store(seq + 1u, std::memory_order_relaxed);  // odd: writing
        std::atomic_thread_fence(std::memory_order_release);
        size_t i = 0;
        for (int c = 0; c < 2; ++c) {
            values_[i++].store(carry_.inPeak[c], std::memory_order_relaxed);
            values_[i++].store(carry_.inMeanSq[c], std::memory_order_relaxed);
            values_[i++].store(carry_.outPeak[c], std::memory_order_relaxed);
            values_[i++].store(carry_.outMeanSq[c], std::memory_order_relaxed);
        }
        for (const float peak : carry_.bandPeak)
            values_[i++].store(peak, std::memory_order_relaxed);
        clipFlags_.store(carry_.clipFlags, std::memory_order_relaxed);
        numSamples_.store(carry_.numSamples, std::memory_order_relaxed);
        sequence_.store(seq + 2u, std::memory_order_release);
    }

    /// UI thread: the window published since the last consume, if any.
    [[nodiscard]] bool consume(MeterWindow& out) noexcept {
        const uint32_t s1 = sequence_.load(std::memory_order_acquire);
        if ((s1 & 1u) != 0u || s1 == consumed_.load(std::memory_order_relaxed))
            return false;
        MeterWindow w;
        size_t i = 0;
        for (int c = 0; c < 2; ++c) {
            w.inPeak[c] = values_[i++].load(std::memory_order_relaxed);
            w.inMeanSq[c] = values_[i++].load(std::memory_order_relaxed);
            w.outPeak[c] = values_[i++].load(std::memory_order_relaxed);
            w.outMeanSq[c] = values_[i++].load(std::memory_order_relaxed);
        }
        for (float& peak : w.bandPeak)
            peak = values_[i++].load(std::memory_order_relaxed);
        w.clipFlags = clipFlags_.load(std::memory_order_relaxed);
        w.numSamples = numSamples_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence_.load(std::memory_order_relaxed) != s1)
            return false;  // torn: the writer published meanwhile; the next read gets it merged
        consumed_.store(s1, std::memory_order_release);
        out = w;
        return true;
    }

private:
    static constexpr uint32_t kMaxCarrySamples = 192000;
    static constexpr size_t kValueCount = 8 + kMeterBands;

    std::atomic<uint32_t> sequence_{0};
    std::atomic<uint32_t> consumed_{0};
    std::array<std::atomic<float>, kValueCount> values_{};
    std::atomic<uint32_t> clipFlags_{0};
    std::atomic<uint32_t> numSamples_{0};
    MeterWindow carry_{};  // writer only
};

struct SharedDisplay {
    Krate::DSP::SpectrumFIFO<8192>* inputFIFO;
    Krate::DSP::SpectrumFIFO<8192>* outputFIFO;
    std::atomic<float>* sampleRate;
    SharedMeters* meters;
};

} // namespace Disrumpo
