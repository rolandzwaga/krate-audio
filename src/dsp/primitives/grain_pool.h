// ==============================================================================
// Layer 1: DSP Primitive - Grain Pool
// ==============================================================================
// Fixed-size pool of grains for granular synthesis with voice stealing.
// Part of Granular Delay feature (spec 034)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, std::span, value semantics)
// - Principle IX: Layer 1 (no dependencies on Layer 2+)
// ==============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Iterum::DSP {

/// State of a single grain
struct Grain {
    float readPosition = 0.0f;       ///< Current position in delay buffer (samples)
    float playbackRate = 1.0f;       ///< Samples to advance per output sample
    float envelopePhase = 0.0f;      ///< Progress through envelope [0, 1]
    float envelopeIncrement = 0.0f;  ///< Phase advance per sample
    float amplitude = 1.0f;          ///< Grain volume
    float panL = 1.0f;               ///< Left channel gain (from pan law)
    float panR = 1.0f;               ///< Right channel gain (from pan law)
    bool active = false;             ///< Is grain currently playing
    bool reverse = false;            ///< Play backwards
    size_t startSample = 0;          ///< Sample when grain was triggered (for age/voice stealing)
};

/// Pre-allocated grain pool with voice stealing
/// Manages a fixed pool of grains for real-time granular synthesis.
/// Uses voice stealing (oldest grain) when pool is exhausted.
class GrainPool {
public:
    static constexpr size_t kMaxGrains = 64;

    /// Prepare the pool for processing
    /// @param sampleRate Current sample rate (reserved for future use)
    void prepare([[maybe_unused]] double sampleRate) noexcept {
        reset();
    }

    /// Reset all grains to inactive state
    void reset() noexcept {
        for (auto& grain : grains_) {
            grain = Grain{};
        }
        activeCount_ = 0;
    }

    /// Acquire a grain from pool
    /// @param currentSample Current sample count (used for age tracking)
    /// @return Pointer to available grain, or oldest grain if pool exhausted
    [[nodiscard]] Grain* acquireGrain(size_t currentSample) noexcept {
        // First, try to find an inactive grain
        for (auto& grain : grains_) {
            if (!grain.active) {
                grain.active = true;
                grain.startSample = currentSample;
                ++activeCount_;
                return &grain;
            }
        }

        // Pool exhausted - steal the oldest active grain
        Grain* oldest = nullptr;
        size_t oldestAge = 0;

        for (auto& grain : grains_) {
            const size_t age = currentSample - grain.startSample;
            if (age >= oldestAge) {
                oldestAge = age;
                oldest = &grain;
            }
        }

        if (oldest != nullptr) {
            // Reset and reuse oldest grain
            *oldest = Grain{};
            oldest->active = true;
            oldest->startSample = currentSample;
            // Note: activeCount_ stays the same since we're stealing
        }

        return oldest;
    }

    /// Release a grain back to pool
    /// @param grain Pointer to grain to release
    void releaseGrain(Grain* grain) noexcept {
        if (grain != nullptr && grain->active) {
            grain->active = false;
            if (activeCount_ > 0) {
                --activeCount_;
            }
        }
    }

    /// Get all active grains for processing
    /// @return Span of pointers to active grains
    [[nodiscard]] std::span<Grain* const> activeGrains() noexcept {
        // Rebuild active list
        size_t count = 0;
        for (auto& grain : grains_) {
            if (grain.active && count < kMaxGrains) {
                activeList_[count++] = &grain;
            }
        }
        return std::span<Grain* const>(activeList_.data(), count);
    }

    /// Get count of active grains
    [[nodiscard]] size_t activeCount() const noexcept { return activeCount_; }

    /// Get maximum grain capacity
    [[nodiscard]] static constexpr size_t maxGrains() noexcept { return kMaxGrains; }

private:
    std::array<Grain, kMaxGrains> grains_{};
    std::array<Grain*, kMaxGrains> activeList_{};
    size_t activeCount_ = 0;
};

}  // namespace Iterum::DSP
