// ==============================================================================
// MorphEngine DSP System for Disrumpo Plugin
// ==============================================================================
// Core engine for morphing between 2-4 distortion types within each frequency band.
// Uses inverse distance weighting for weight computation, supports three morph modes,
// and handles both same-family parameter interpolation and cross-family parallel
// processing with equal-power crossfade.
//
// Real-time safe: no allocations after prepare().
//
// Layer: Plugin DSP (composes Layer 1 primitives)
//
// Reference: specs/005-morph-system/spec.md
// ==============================================================================

#pragma once

#include "morph_node.h"
#include "distortion_adapter.h"
#include "distortion_types.h"

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/crossfade_utils.h>

#include <array>
#include <cmath>

namespace Disrumpo {

/// @brief Core engine for morphing between distortion types.
///
/// Each frequency band owns a MorphEngine instance. The engine:
/// 1. Computes weights for each node based on cursor position and morph mode
/// 2. Detects if nodes belong to the same family (optimization path)
/// 3. For same-family: interpolates parameters through a single processor
/// 4. For cross-family: processes in parallel with equal-power crossfade
///
/// Thread Safety:
/// - prepare()/reset(): Call from non-audio thread only
/// - setMorphPosition/setMode/setNodes: Thread-safe parameter updates
/// - process(): Real-time audio thread only
///
/// @note Real-time safe: no allocations in process()
/// @note Per spec FR-001 through FR-019
class MorphEngine {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    MorphEngine() noexcept;

    /// @brief Prepare engine for processing.
    ///
    /// Allocates internal state, prepares distortion adapters, configures smoothers.
    /// Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size (for block-based processors)
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept;

    /// @brief Reset all internal state.
    ///
    /// Clears smoothers, resets distortion adapters, zeros buffers.
    /// Call when starting new playback or after discontinuity.
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set the morph cursor position.
    ///
    /// For 1D Linear mode: only X is used (0.0 = node A, 1.0 = node B/C/D based on count)
    /// For 2D modes: both X and Y define position in morph space
    ///
    /// @param x X position [0, 1]
    /// @param y Y position [0, 1] (ignored in 1D Linear mode)
    void setMorphPosition(float x, float y) noexcept;

    /// @brief Set the morph mode.
    /// @param mode The morph mode (Linear1D, Planar2D, Radial2D)
    void setMode(MorphMode mode) noexcept;

    /// @brief Set morph smoothing time.
    ///
    /// Per spec FR-009: configurable from 0ms to 500ms.
    ///
    /// @param timeMs Smoothing time in milliseconds
    void setSmoothingTime(float timeMs) noexcept;

    /// @brief Set nodes from BandState.
    ///
    /// Copies node configurations for weight computation and processing.
    ///
    /// @param nodes Array of morph nodes (fixed size kMaxMorphNodes)
    /// @param activeCount Number of active nodes (2-4)
    void setNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int activeCount) noexcept;

    /// @brief Get current computed weights for all nodes.
    ///
    /// Useful for UI visualization and sweep-morph linking.
    ///
    /// @return Array of weights (sum to 1.0 for active nodes)
    [[nodiscard]] const std::array<float, kMaxMorphNodes>& getWeights() const noexcept;

    /// @brief Get current smoothed morph X position.
    [[nodiscard]] float getSmoothedX() const noexcept;

    /// @brief Get current smoothed morph Y position.
    [[nodiscard]] float getSmoothedY() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample through the morph engine.
    ///
    /// The signal flow depends on whether nodes share a family:
    /// - Same family: Parameter interpolation through single processor
    /// - Cross-family: Parallel processing with equal-power crossfade
    ///
    /// @param input Input sample
    /// @return Processed output sample
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples.
    ///
    /// More efficient than per-sample processing for large blocks.
    ///
    /// @param input Input buffer
    /// @param output Output buffer (can be same as input for in-place)
    /// @param numSamples Number of samples to process
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    // =========================================================================
    // Weight Computation (Public for Testing)
    // =========================================================================

    /// @brief Calculate weights for all active nodes based on cursor position.
    ///
    /// Uses inverse distance weighting with exponent p=2:
    /// weight_i = 1 / distance_i^2
    /// Weights are normalized to sum to 1.0.
    ///
    /// Per spec FR-001, FR-014, FR-015:
    /// - Deterministic (same inputs -> same weights)
    /// - Skips weights below kWeightThreshold (0.001)
    /// - Renormalizes remaining weights
    ///
    /// @param posX Cursor X position [0, 1]
    /// @param posY Cursor Y position [0, 1]
    void calculateMorphWeights(float posX, float posY) noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Calculate 1D distance for Linear mode.
    [[nodiscard]] float calculate1DDistance(float cursorX, float nodeX) const noexcept;

    /// @brief Calculate 2D Euclidean distance for Planar mode.
    [[nodiscard]] float calculate2DDistance(float cursorX, float cursorY,
                                            float nodeX, float nodeY) const noexcept;

    /// @brief Calculate radial weights for Radial mode.
    void calculateRadialWeights(float cursorX, float cursorY) noexcept;

    /// @brief Check if all active nodes belong to the same family.
    [[nodiscard]] bool isSameFamily() const noexcept;

    /// @brief Interpolate parameters for same-family morphing.
    [[nodiscard]] DistortionParams interpolateParams() const noexcept;

    /// @brief Interpolate common parameters for same-family morphing.
    [[nodiscard]] DistortionCommonParams interpolateCommonParams() const noexcept;

    /// @brief Process using same-family parameter interpolation.
    [[nodiscard]] float processSameFamily(float input) noexcept;

    /// @brief Process using cross-family parallel processing.
    [[nodiscard]] float processCrossFamily(float input) noexcept;

    /// @brief Apply transition zone gain for cross-family processing.
    /// Per spec FR-008: 40-60% transition zone with equal-power ramp.
    [[nodiscard]] float calculateTransitionGain(float weight) const noexcept;

    // =========================================================================
    // State
    // =========================================================================

    double sampleRate_ = 44100.0;
    MorphMode mode_ = MorphMode::Linear1D;
    float smoothingTimeMs_ = 10.0f;  // Default 10ms smoothing

    // Current morph position (target values)
    float targetX_ = 0.5f;
    float targetY_ = 0.5f;

    // Position smoothers (for manual control per FR-009)
    Krate::DSP::OnePoleSmoother smootherX_;
    Krate::DSP::OnePoleSmoother smootherY_;

    // Node configuration
    std::array<MorphNode, kMaxMorphNodes> nodes_;
    int activeNodeCount_ = kDefaultActiveNodes;

    // Computed weights (normalized, sum to 1.0)
    std::array<float, kMaxMorphNodes> weights_ = {0.5f, 0.5f, 0.0f, 0.0f};

    // Transition zone gains (for cross-family processing)
    std::array<float, kMaxMorphNodes> transitionGains_ = {1.0f, 1.0f, 1.0f, 1.0f};

    // Distortion adapters (one per potential node for cross-family processing)
    std::array<DistortionAdapter, kMaxMorphNodes> adapters_;

    // Single adapter for same-family processing (optimization)
    DistortionAdapter blendedAdapter_;

    // Cached family check result
    bool allSameFamily_ = true;
    DistortionFamily dominantFamily_ = DistortionFamily::Saturation;

    // Prepared flag
    bool prepared_ = false;
};

} // namespace Disrumpo
