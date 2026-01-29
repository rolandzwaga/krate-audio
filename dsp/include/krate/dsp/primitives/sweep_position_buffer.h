// ==============================================================================
// Layer 1: DSP Primitive - Sweep Position Buffer
// ==============================================================================
// Lock-free Single-Producer Single-Consumer (SPSC) ring buffer for
// communicating sweep position data from audio thread to UI thread.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/007-sweep-system/spec.md (FR-046, FR-047)
// ==============================================================================

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace Krate {
namespace DSP {

// Forward declaration of SweepFalloff (defined in plugin)
// Using uint8_t to avoid circular dependency
using SweepFalloffType = uint8_t;

/// @brief Data structure for audio-to-UI sweep position communication.
///
/// Contains all information needed by the UI to render the sweep indicator
/// at the correct position, synchronized with audio playback.
struct SweepPositionData {
    float centerFreqHz = 1000.0f;    ///< Current sweep center frequency in Hz
    float widthOctaves = 1.5f;       ///< Sweep width in octaves
    float intensity = 0.5f;          ///< Intensity multiplier [0.0, 2.0]
    uint64_t samplePosition = 0;     ///< Sample count for timing synchronization
    bool enabled = false;            ///< Sweep on/off state
    SweepFalloffType falloff = 1;    ///< Falloff mode (0=Sharp, 1=Smooth)
};

/// @brief Buffer size for sweep position data.
///
/// 8 entries provides approximately 100ms of data at typical block sizes
/// (e.g., 512 samples at 44.1kHz = ~11.6ms per block, 8 blocks = ~93ms).
constexpr int kSweepBufferSize = 8;

/// @brief Lock-free SPSC ring buffer for sweep position data.
///
/// Designed for single producer (audio thread) and single consumer (UI thread).
/// Uses atomic operations for thread-safe communication without locks.
///
/// Thread Safety:
/// - push(): Audio thread only (producer)
/// - pop()/getLatest(): UI thread only (consumer)
/// - clear(): Call only when both threads are synchronized
///
/// @note Real-time safe: no allocations, no locks
/// @note Per spec FR-046, FR-047
class SweepPositionBuffer {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    SweepPositionBuffer() noexcept = default;

    // Non-copyable (contains atomics)
    SweepPositionBuffer(const SweepPositionBuffer&) = delete;
    SweepPositionBuffer& operator=(const SweepPositionBuffer&) = delete;

    // =========================================================================
    // Producer Interface (Audio Thread)
    // =========================================================================

    /// @brief Push new position data into the buffer.
    ///
    /// Called by audio thread after processing each block. If buffer is full,
    /// returns false without modifying the buffer.
    ///
    /// @param data Sweep position data to push
    /// @return true if pushed successfully, false if buffer was full
    bool push(const SweepPositionData& data) noexcept {
        const int currentCount = elementCount_.load(std::memory_order_acquire);
        if (currentCount >= kSweepBufferSize) {
            // Buffer full - UI not keeping up
            return false;
        }

        buffer_[writeIndex_] = data;
        writeIndex_ = (writeIndex_ + 1) % kSweepBufferSize;
        elementCount_.fetch_add(1, std::memory_order_release);
        return true;
    }

    // =========================================================================
    // Consumer Interface (UI Thread)
    // =========================================================================

    /// @brief Pop oldest position data from the buffer.
    ///
    /// Called by UI thread to retrieve position data in FIFO order.
    ///
    /// @param data Output parameter for retrieved data
    /// @return true if data was available, false if buffer was empty
    bool pop(SweepPositionData& data) noexcept {
        const int currentCount = elementCount_.load(std::memory_order_acquire);
        if (currentCount == 0) {
            return false;  // Empty
        }

        data = buffer_[readIndex_];
        readIndex_ = (readIndex_ + 1) % kSweepBufferSize;
        elementCount_.fetch_sub(1, std::memory_order_release);
        return true;
    }

    /// @brief Get the latest (newest) position data without removing it.
    ///
    /// Useful for getting current state without draining the buffer.
    /// If multiple entries exist, returns the most recent one.
    ///
    /// @param data Output parameter for retrieved data
    /// @return true if data was available, false if buffer was empty
    [[nodiscard]] bool getLatest(SweepPositionData& data) const noexcept {
        const int currentCount = elementCount_.load(std::memory_order_acquire);
        if (currentCount == 0) {
            return false;  // Empty
        }

        // Latest is at writeIndex_ - 1 (with wraparound)
        int latestIndex = (writeIndex_ - 1 + kSweepBufferSize) % kSweepBufferSize;
        data = buffer_[latestIndex];
        return true;
    }

    /// @brief Drain buffer and get latest position data.
    ///
    /// Reads all available entries and returns the newest one.
    /// Useful for catching up after UI was blocked.
    ///
    /// @param data Output parameter for retrieved data
    /// @return true if any data was available, false if buffer was empty
    bool drainToLatest(SweepPositionData& data) noexcept {
        bool found = false;
        SweepPositionData temp;
        while (pop(temp)) {
            data = temp;
            found = true;
        }
        return found;
    }

    /// @brief Get interpolated position for a target sample position.
    ///
    /// Finds the two nearest entries by sample position and interpolates
    /// between them for smooth 60fps display. Per spec FR-047.
    ///
    /// @param targetSample Target sample position for interpolation
    /// @return Interpolated position data (or latest if interpolation not possible)
    [[nodiscard]] SweepPositionData getInterpolatedPosition(uint64_t targetSample) const noexcept {
        const int currentCount = elementCount_.load(std::memory_order_acquire);
        if (currentCount == 0) {
            return SweepPositionData{};  // Return default
        }

        if (currentCount == 1) {
            // Only one entry, return it
            return buffer_[readIndex_];
        }

        // Find entries bracketing the target sample
        int beforeIdx = -1;
        int afterIdx = -1;
        uint64_t beforeSample = 0;
        uint64_t afterSample = UINT64_MAX;

        for (int i = 0; i < currentCount; ++i) {
            int idx = (readIndex_ + i) % kSweepBufferSize;
            uint64_t sample = buffer_[idx].samplePosition;

            // Check for exact match first
            if (sample == targetSample) {
                return buffer_[idx];
            }

            if (sample < targetSample && sample >= beforeSample) {
                beforeSample = sample;
                beforeIdx = idx;
            }
            if (sample > targetSample && sample <= afterSample) {
                afterSample = sample;
                afterIdx = idx;
            }
        }

        // If no bracketing entries found, return latest
        if (beforeIdx < 0 || afterIdx < 0) {
            SweepPositionData latest;
            if (getLatest(latest)) {
                return latest;
            }
            return SweepPositionData{};
        }

        // Interpolate
        const auto& before = buffer_[beforeIdx];
        const auto& after = buffer_[afterIdx];

        float t = (afterSample > beforeSample)
            ? static_cast<float>(targetSample - beforeSample) /
              static_cast<float>(afterSample - beforeSample)
            : 0.0f;

        SweepPositionData result;
        result.centerFreqHz = before.centerFreqHz + t * (after.centerFreqHz - before.centerFreqHz);
        result.widthOctaves = before.widthOctaves + t * (after.widthOctaves - before.widthOctaves);
        result.intensity = before.intensity + t * (after.intensity - before.intensity);
        result.samplePosition = targetSample;
        result.enabled = after.enabled;  // Use latest state for booleans
        result.falloff = after.falloff;

        return result;
    }

    // =========================================================================
    // Utility
    // =========================================================================

    /// @brief Clear all entries from the buffer.
    ///
    /// @warning Only call when both threads are synchronized (e.g., during reset).
    void clear() noexcept {
        readIndex_ = 0;
        writeIndex_ = 0;
        elementCount_.store(0, std::memory_order_release);
    }

    /// @brief Check if buffer has any data.
    [[nodiscard]] bool isEmpty() const noexcept {
        return elementCount_.load(std::memory_order_acquire) == 0;
    }

    /// @brief Get number of entries in buffer.
    [[nodiscard]] int count() const noexcept {
        return elementCount_.load(std::memory_order_acquire);
    }

private:
    std::array<SweepPositionData, kSweepBufferSize> buffer_{};
    int writeIndex_ = 0;  // Only modified by producer
    int readIndex_ = 0;   // Only modified by consumer
    std::atomic<int> elementCount_{0};  // Shared between threads
};

} // namespace DSP
} // namespace Krate
