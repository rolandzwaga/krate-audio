// ==============================================================================
// Layer 1: DSP Primitive - Slice Pool
// ==============================================================================
// Memory pool for audio slices used in Pattern Freeze Mode.
//
// Pre-allocates a fixed number of audio slice buffers that can be acquired
// and released without runtime allocation. Each slice contains stereo audio
// data and playback state for envelope-shaped grain playback.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept allocation/deallocation)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/069-pattern-freeze/data-model.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

/// @brief Single audio slice with stereo buffers and playback state
///
/// Represents a slice of captured audio that can be played back with
/// envelope shaping. The pool manages these slices to avoid runtime allocation.
class Slice {
public:
    friend class SlicePool;

    /// @brief Get pointer to left channel buffer
    [[nodiscard]] float* getLeft() noexcept { return left_.data(); }

    /// @brief Get pointer to right channel buffer
    [[nodiscard]] float* getRight() noexcept { return right_.data(); }

    /// @brief Get const pointer to left channel buffer
    [[nodiscard]] const float* getLeft() const noexcept { return left_.data(); }

    /// @brief Get const pointer to right channel buffer
    [[nodiscard]] const float* getRight() const noexcept { return right_.data(); }

    /// @brief Set the active length of this slice
    void setLength(size_t length) noexcept {
        length_ = std::min(length, maxLength_);
    }

    /// @brief Get the active length of this slice
    [[nodiscard]] size_t getLength() const noexcept { return length_; }

    /// @brief Get maximum possible length (buffer capacity)
    [[nodiscard]] size_t getMaxLength() const noexcept { return maxLength_; }

    /// @brief Reset playback position to start
    void resetPosition() noexcept {
        position_ = 0;
    }

    /// @brief Get current playback position
    [[nodiscard]] size_t getPosition() const noexcept { return position_; }

    /// @brief Advance playback position by given samples
    void advancePosition(size_t samples) noexcept {
        position_ += samples;
    }

    /// @brief Check if playback has completed
    [[nodiscard]] bool isComplete() const noexcept {
        return position_ >= length_;
    }

    /// @brief Set envelope phase [0, 1]
    void setEnvelopePhase(float phase) noexcept {
        envelopePhase_ = std::clamp(phase, 0.0f, 1.0f);
    }

    /// @brief Get envelope phase
    [[nodiscard]] float getEnvelopePhase() const noexcept {
        return envelopePhase_;
    }

private:
    /// @brief Initialize slice with given capacity (called by pool)
    void initialize(size_t maxSamples) {
        maxLength_ = maxSamples;
        left_.resize(maxSamples, 0.0f);
        right_.resize(maxSamples, 0.0f);
        reset();
    }

    /// @brief Reset slice state
    void reset() noexcept {
        length_ = 0;
        position_ = 0;
        envelopePhase_ = 0.0f;
        active_ = false;
    }

    std::vector<float> left_;       ///< Left channel buffer
    std::vector<float> right_;      ///< Right channel buffer
    size_t maxLength_ = 0;          ///< Buffer capacity
    size_t length_ = 0;             ///< Active length
    size_t position_ = 0;           ///< Playback position
    float envelopePhase_ = 0.0f;    ///< Envelope phase [0, 1]
    bool active_ = false;           ///< Currently allocated
};

/// @brief Memory pool for pre-allocated audio slices
///
/// Manages a fixed pool of Slice objects to avoid runtime allocation during
/// audio processing. Slices are acquired for playback and returned when done.
///
/// @note prepare() allocates memory; allocate/deallocate are allocation-free.
/// @note All allocation/deallocation methods are noexcept for real-time safety.
///
/// @example
/// @code
/// SlicePool pool;
/// pool.prepare(8, 44100.0, 4410);  // 8 slices, 100ms each
///
/// // In pattern trigger:
/// Slice* slice = pool.allocateSlice();
/// if (slice) {
///     // Fill slice from capture buffer
///     captureBuffer.extractSlice(slice->getLeft(), slice->getRight(), length, offset);
///     slice->setLength(length);
/// }
///
/// // After playback complete:
/// pool.deallocateSlice(slice);
/// @endcode
class SlicePool {
public:
    /// @brief Default constructor
    SlicePool() noexcept = default;

    /// @brief Destructor
    ~SlicePool() = default;

    // Non-copyable, movable
    SlicePool(const SlicePool&) = delete;
    SlicePool& operator=(const SlicePool&) = delete;
    SlicePool(SlicePool&&) noexcept = default;
    SlicePool& operator=(SlicePool&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare pool with given capacity
    ///
    /// @param maxSlices Maximum number of simultaneous slices
    /// @param sampleRate Sample rate in Hz (for reference)
    /// @param maxSliceSamples Maximum samples per slice
    void prepare(size_t maxSlices, double sampleRate,
                 size_t maxSliceSamples) noexcept {
        sampleRate_ = sampleRate;
        maxSlices_ = maxSlices;
        maxSliceSamples_ = maxSliceSamples;

        // Allocate slices
        slices_.resize(maxSlices);
        for (auto& slice : slices_) {
            slice.initialize(maxSliceSamples);
        }

        // Build free list (all slices initially available)
        freeList_.clear();
        freeList_.reserve(maxSlices);
        for (size_t i = 0; i < maxSlices; ++i) {
            freeList_.push_back(&slices_[i]);
        }

        activeCount_ = 0;
    }

    /// @brief Reset pool (return all slices to available)
    void reset() noexcept {
        // Reset all slices and rebuild free list
        freeList_.clear();
        for (auto& slice : slices_) {
            slice.reset();
            freeList_.push_back(&slice);
        }
        activeCount_ = 0;
    }

    // =========================================================================
    // Allocation (Real-Time Safe)
    // =========================================================================

    /// @brief Allocate a slice from the pool
    ///
    /// @return Pointer to slice, or nullptr if pool exhausted
    ///
    /// @note O(1) time, no allocations, noexcept - safe for audio thread
    [[nodiscard]] Slice* allocateSlice() noexcept {
        if (freeList_.empty()) {
            return nullptr;
        }

        Slice* slice = freeList_.back();
        freeList_.pop_back();

        slice->reset();
        slice->active_ = true;
        ++activeCount_;

        return slice;
    }

    /// @brief Return a slice to the pool
    ///
    /// @param slice Pointer to slice to deallocate (may be nullptr)
    ///
    /// @note O(1) time, no allocations, noexcept - safe for audio thread
    void deallocateSlice(Slice* slice) noexcept {
        if (slice == nullptr || !slice->active_) {
            return;
        }

        slice->reset();
        freeList_.push_back(slice);
        --activeCount_;
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Get maximum number of slices
    [[nodiscard]] size_t getMaxSlices() const noexcept { return maxSlices_; }

    /// @brief Get maximum samples per slice
    [[nodiscard]] size_t getMaxSliceSamples() const noexcept {
        return maxSliceSamples_;
    }

    /// @brief Get number of available (unallocated) slices
    [[nodiscard]] size_t getAvailableSlices() const noexcept {
        return freeList_.size();
    }

    /// @brief Get number of active (allocated) slices
    [[nodiscard]] size_t getActiveSlices() const noexcept {
        return activeCount_;
    }

private:
    std::vector<Slice> slices_;        ///< Slice storage
    std::vector<Slice*> freeList_;     ///< Available slices
    size_t maxSlices_ = 0;             ///< Pool capacity
    size_t maxSliceSamples_ = 0;       ///< Max samples per slice
    size_t activeCount_ = 0;           ///< Currently allocated count
    double sampleRate_ = 44100.0;      ///< Sample rate
};

}  // namespace Krate::DSP
