// ==============================================================================
// CONTRACT: EvolutionEngine -- Autonomous Timbral Drift
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
    void prepare(double sampleRate) noexcept;

    /// @brief Reset phase and direction to initial state.
    void reset() noexcept;

    /// @brief Update the waypoint list from current memory slots (FR-018).
    ///
    /// Scans the 8 memory slots, collects indices of occupied ones.
    /// Must be called when slots change (capture, recall, state load).
    ///
    /// @param slots Array of 8 MemorySlot references
    void updateWaypoints(const std::array<Krate::DSP::MemorySlot, 8>& slots) noexcept;

    /// @brief Set evolution mode (FR-017).
    void setMode(EvolutionMode mode) noexcept;

    /// @brief Set evolution speed in Hz (FR-015).
    /// @param speedHz [0.01, 10.0]
    void setSpeed(float speedHz) noexcept;

    /// @brief Set evolution depth (FR-016).
    /// @param depth [0.0, 1.0]
    void setDepth(float depth) noexcept;

    /// @brief Set manual morph offset for coexistence with manual control (FR-021).
    /// @param offset Bipolar offset from manual morph position centered at 0.5
    void setManualOffset(float offset) noexcept;

    /// @brief Advance the evolution phase by one sample.
    ///
    /// Updates internal phase according to current mode, speed, and depth.
    /// Call once per sample from the audio thread.
    ///
    /// @note Real-time safe
    void advance() noexcept;

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
        Krate::DSP::ResidualFrame& residual) const noexcept;

    /// @brief Get current evolution position [0.0, 1.0].
    [[nodiscard]] float getPosition() const noexcept;

    /// @brief Get number of active waypoints.
    [[nodiscard]] int getNumWaypoints() const noexcept;

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
