// ==============================================================================
// Layer 1: DSP Primitive - Spectrum FIFO
// ==============================================================================
// Lock-free Single-Producer Single-Consumer (SPSC) ring buffer for
// streaming audio samples from audio thread to UI thread for spectrum
// analysis visualization.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in push)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Usage:
//   Audio thread: push(samples, count) each process block
//   UI thread:    readLatest(dest, fftSize) when ready for FFT
// ==============================================================================

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Lock-free SPSC ring buffer for streaming audio samples to UI thread.
///
/// Template parameter N must be power of 2 for efficient bitmask wraparound.
/// Default N=8192 provides ~185ms at 44.1kHz, sufficient for multiple FFT frames.
///
/// Thread Safety:
/// - push(): Audio thread only (producer)
/// - readLatest(): UI thread only (consumer)
/// - clear(): Call only when both threads are synchronized
///
/// @note Real-time safe: no allocations, no locks
/// @tparam N Buffer size (must be power of 2)
template <size_t N = 8192>
class SpectrumFIFO {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");
    static constexpr size_t kMask = N - 1;

public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    SpectrumFIFO() noexcept = default;

    // Non-copyable (contains atomics)
    SpectrumFIFO(const SpectrumFIFO&) = delete;
    SpectrumFIFO& operator=(const SpectrumFIFO&) = delete;

    // =========================================================================
    // Producer Interface (Audio Thread)
    // =========================================================================

    /// @brief Push a block of mono samples into the FIFO.
    ///
    /// If the buffer would overflow, older samples are implicitly overwritten
    /// by advancing the write position. The consumer will always read the
    /// most recent samples, so dropped old data is acceptable.
    ///
    /// @param samples Source buffer of audio samples
    /// @param count Number of samples to push
    /// @note Real-time safe, noexcept
    void push(const float* samples, size_t count) noexcept {
        if (samples == nullptr || count == 0) return;

        const size_t writePos = writePos_.load(std::memory_order_relaxed);

        // Copy samples into ring buffer with wraparound
        for (size_t i = 0; i < count; ++i) {
            buffer_[(writePos + i) & kMask] = samples[i];
        }

        // Advance write position (may wrap around the size_t range, which is fine
        // because we always mask with kMask when indexing)
        writePos_.store(writePos + count, std::memory_order_release);
    }

    // =========================================================================
    // Consumer Interface (UI Thread)
    // =========================================================================

    /// @brief Read the most recent N samples for FFT analysis.
    ///
    /// Copies the latest `count` samples into dest. If fewer than `count`
    /// samples have been written since construction/clear, returns 0.
    ///
    /// @param dest Destination buffer (must hold at least `count` floats)
    /// @param count Number of samples to read (typically FFT size)
    /// @return Number of samples actually copied (0 or count)
    /// @note NOT real-time safe (UI thread only)
    size_t readLatest(float* dest, size_t count) noexcept {
        if (dest == nullptr || count == 0 || count > N) return 0;

        const size_t writePos = writePos_.load(std::memory_order_acquire);
        const size_t totalWritten = writePos;  // writePos is cumulative

        if (totalWritten < count) {
            return 0;  // Not enough data written yet
        }

        // Read the most recent `count` samples
        const size_t start = writePos - count;
        for (size_t i = 0; i < count; ++i) {
            dest[i] = buffer_[(start + i) & kMask];
        }

        return count;
    }

    /// @brief Get total number of samples written since construction/clear.
    /// @note Useful for checking if enough data is available before readLatest.
    [[nodiscard]] size_t totalWritten() const noexcept {
        return writePos_.load(std::memory_order_acquire);
    }

    /// @brief Clear the buffer and reset write position.
    /// @warning Only call when both threads are synchronized (e.g., during reset).
    void clear() noexcept {
        writePos_.store(0, std::memory_order_release);
        buffer_.fill(0.0f);
    }

private:
    std::array<float, N> buffer_{};
    std::atomic<size_t> writePos_{0};  // Cumulative write position (only producer modifies)
};

} // namespace DSP
} // namespace Krate
