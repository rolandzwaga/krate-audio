// ==============================================================================
// EvolutionEngine -- Autonomous Timbral Drift
// ==============================================================================
// Plugin-local DSP class for Innexus M6 Phase 19.
// Location: plugins/innexus/src/dsp/evolution_engine.h
//
// Spec: specs/120-creative-extensions/spec.md
// Covers: FR-014 to FR-023
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_frame_utils.h>
#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/core/random.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Innexus {

/// Evolution traversal modes (FR-017).
enum class EvolutionMode : int {
    Cycle = 0,      ///< Wrap: 1->2->3->4->1->2->...
    PingPong = 1,   ///< Bounce: 1->2->3->4->3->2->1->...
    RandomWalk = 2  ///< Random drift within depth range
};

/// @brief Autonomous timbral drift engine using memory slot waypoints (FR-014).
///
/// Drives a morph position through a sequence of occupied memory slots,
/// producing interpolated HarmonicFrame + ResidualFrame output per sample.
/// The phase is global (not per-note, FR-020) and free-running.
///
/// @par Thread Safety: Single-threaded (audio thread only).
/// @par Real-Time Safety: All methods noexcept, no allocations.
class EvolutionEngine {
public:
    EvolutionEngine() noexcept = default;

    /// @brief Initialize for processing.
    /// @param sampleRate Sample rate in Hz
    /// @note NOT real-time safe (called from setupProcessing)
    void prepare(double sampleRate) noexcept
    {
        inverseSampleRate_ = 1.0f / static_cast<float>(sampleRate);
    }

    /// @brief Reset phase and direction to initial state.
    void reset() noexcept
    {
        phase_ = 0.0f;
        direction_ = 1;
    }

    /// @brief Update the waypoint list from current memory slots (FR-018).
    ///
    /// Scans the 8 memory slots, collects indices of occupied ones.
    /// Must be called when slots change (capture, recall, state load).
    ///
    /// @param slots Array of 8 MemorySlot references
    void updateWaypoints(const std::array<Krate::DSP::MemorySlot, 8>& slots) noexcept
    {
        numWaypoints_ = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (slots[static_cast<size_t>(i)].occupied)
            {
                waypointIndices_[static_cast<size_t>(numWaypoints_)] = i;
                ++numWaypoints_;
            }
        }
    }

    /// @brief Set evolution mode (FR-017).
    void setMode(EvolutionMode mode) noexcept
    {
        mode_ = mode;
    }

    /// @brief Set evolution speed in Hz (FR-015).
    /// @param speedHz [0.01, 10.0]
    void setSpeed(float speedHz) noexcept
    {
        speed_ = speedHz;
    }

    /// @brief Set evolution depth (FR-016).
    /// @param depth [0.0, 1.0]
    void setDepth(float depth) noexcept
    {
        depth_ = depth;
    }

    /// @brief Set manual morph offset for coexistence with manual control (FR-021).
    /// @param offset Bipolar offset from manual morph position centered at 0.5
    void setManualOffset(float offset) noexcept
    {
        manualOffset_ = offset;
    }

    /// @brief Advance the evolution phase by one sample.
    ///
    /// Updates internal phase according to current mode, speed, and depth.
    /// Call once per sample from the audio thread.
    ///
    /// @note Real-time safe
    void advance() noexcept
    {
        if (numWaypoints_ < 2)
            return;

        const float phaseIncrement = speed_ * inverseSampleRate_;

        switch (mode_)
        {
        case EvolutionMode::Cycle:
            phase_ += phaseIncrement;
            if (phase_ >= 1.0f)
                phase_ -= 1.0f;
            break;

        case EvolutionMode::PingPong:
            phase_ += static_cast<float>(direction_) * phaseIncrement;
            if (phase_ >= 1.0f)
            {
                phase_ = 2.0f - phase_; // Reflect
                direction_ = -1;
            }
            else if (phase_ <= 0.0f)
            {
                phase_ = -phase_; // Reflect
                direction_ = 1;
            }
            break;

        case EvolutionMode::RandomWalk:
        {
            // Random bipolar step scaled by speed and 0.1 factor
            // (data-model.md: limits max per-sample step)
            float step = rng_.nextFloat() * phaseIncrement * 0.1f;
            phase_ += step;
            phase_ = std::clamp(phase_, 0.0f, 1.0f - 1e-7f);
            break;
        }
        }
    }

    /// @brief Get the current interpolated frame from evolution waypoints (FR-019).
    ///
    /// Maps the current position to a pair of adjacent waypoints and
    /// interpolates using lerpHarmonicFrame(). Returns false if < 2 waypoints.
    ///
    /// @param slots The memory slot array (for snapshot data access)
    /// @param[out] frame Interpolated harmonic frame
    /// @param[out] residual Interpolated residual frame
    /// @return true if valid output produced, false if insufficient waypoints
    [[nodiscard]] bool getInterpolatedFrame(
        const std::array<Krate::DSP::MemorySlot, 8>& slots,
        Krate::DSP::HarmonicFrame& frame,
        Krate::DSP::ResidualFrame& residual) const noexcept
    {
        if (numWaypoints_ < 2)
            return false;

        // Apply manual offset and depth, clamp to [0, 1] (FR-021)
        float effectivePos = std::clamp(phase_ * depth_ + manualOffset_, 0.0f, 1.0f);

        // Map position to waypoint pair
        const float scaledPos = effectivePos * static_cast<float>(numWaypoints_ - 1);
        const int idxA = std::clamp(
            static_cast<int>(scaledPos),
            0, numWaypoints_ - 2);
        const int idxB = idxA + 1;
        const float localT = scaledPos - static_cast<float>(idxA);

        // Reconstruct frames from snapshots
        const int slotA = waypointIndices_[static_cast<size_t>(idxA)];
        const int slotB = waypointIndices_[static_cast<size_t>(idxB)];

        Krate::DSP::HarmonicFrame frameA{};
        Krate::DSP::ResidualFrame residualA{};
        Krate::DSP::recallSnapshotToFrame(
            slots[static_cast<size_t>(slotA)].snapshot, frameA, residualA);

        Krate::DSP::HarmonicFrame frameB{};
        Krate::DSP::ResidualFrame residualB{};
        Krate::DSP::recallSnapshotToFrame(
            slots[static_cast<size_t>(slotB)].snapshot, frameB, residualB);

        // Interpolate (FR-019)
        frame = Krate::DSP::lerpHarmonicFrame(frameA, frameB, localT);
        residual = Krate::DSP::lerpResidualFrame(residualA, residualB, localT);

        return true;
    }

    /// @brief Get current evolution position [0.0, 1.0].
    [[nodiscard]] float getPosition() const noexcept
    {
        return std::clamp(phase_ * depth_ + manualOffset_, 0.0f, 1.0f);
    }

    /// @brief Get number of active waypoints.
    [[nodiscard]] int getNumWaypoints() const noexcept
    {
        return numWaypoints_;
    }

private:
    float phase_ = 0.0f;
    int direction_ = 1;  // +1 or -1 for PingPong
    EvolutionMode mode_ = EvolutionMode::Cycle;
    float speed_ = 0.1f;
    float depth_ = 0.5f;
    float manualOffset_ = 0.0f;
    float inverseSampleRate_ = 1.0f / 44100.0f;

    int numWaypoints_ = 0;
    std::array<int, 8> waypointIndices_{};

    Krate::DSP::Xorshift32 rng_{42};
};

} // namespace Innexus
