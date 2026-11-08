// ==============================================================================
// MorphEngine DSP System for Disrumpo Plugin
// ==============================================================================
// Core engine for morphing between 2-4 distortion types within each frequency band.
// Uses inverse distance weighting for weight computation, supports three morph modes,
// and handles both identical-type parameter interpolation and parallel processing of
// different types (of one family or not) with a constant-voltage (weight-sum) crossfade.
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

#include <algorithm>
#include <array>
#include <cmath>

namespace Disrumpo {

/// @brief Core engine for morphing between distortion types.
///
/// Each frequency band owns a MorphEngine instance. The engine:
/// 1. Computes weights for each node based on cursor position and morph mode
/// 2. Detects if all active nodes have the same type (optimization path)
/// 3. For identical types: interpolates parameters through a single processor
/// 4. For different types (also within one family, D-028): processes in parallel
///    with a constant-voltage crossfade
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
    /// @param sampleRate Sample rate in Hz (processing starts at this rate)
    /// @param maxBlockSize Maximum block size (for block-based processors)
    /// @param maxOversampleFactor Highest factor setProcessingRate() may be given:
    ///        the adapters are sized for sampleRate * maxOversampleFactor
    void prepare(double sampleRate, int maxBlockSize = 512, int maxOversampleFactor = 1) noexcept;

    /// @brief Move the cursor smoothers and all adapters to the rate process() is
    /// clocked at (D-009, D-138). Allocation-free. The smoothers keep their value and
    /// target; the adapters' filter state is reset (DistortionAdapter::setProcessingRate).
    /// @param rate Rate at which process() will be called, in Hz
    void setProcessingRate(double rate) noexcept;

    /// @brief Reset all internal state.
    ///
    /// Clears smoothers, resets distortion adapters, zeros buffers.
    /// Call when starting new playback or after discontinuity.
    void reset() noexcept;

    /// @brief Clear the distortion adapters' processing state only.
    ///
    /// Keeps parameters, weights, modulation offsets and the cursor smoothers.
    /// Used at the silent point of an oversampling factor switch. Allocation-free.
    void resetState() noexcept;

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

    /// @brief Set drive/mix modulation offsets from the modulation engine.
    ///
    /// Offsets are applied per-sample after morph interpolation (identical types)
    /// or to each adapter (different types). Offset range is [-1, +1] normalized.
    /// Drive offset is scaled by 10.0 (drive range [0, 10]).
    /// Mix offset is applied directly (mix range [0, 1]).
    ///
    /// @param driveOffset Drive modulation offset [-1, +1]
    /// @param mixOffset Mix modulation offset [-1, +1]
    void setDriveMixModOffset(float driveOffset, float mixOffset) noexcept;

    /// @brief Set tone and bias modulation offsets.
    /// Tone offset [-1, +1] is scaled by 7800 Hz (tone range [200, 8000]).
    /// Bias offset [-1, +1] is scaled by 2.0 (bias range [-1, +1]).
    void setToneBiasModOffset(float toneOffset, float biasOffset) noexcept;

    /// @brief Set the sweep's drive multiplier (D-019), applied per sample after the
    /// drive modulation offset; the result is clamped to [0, 10]. 1 = drive as set.
    void setSweepDriveScale(float scale) noexcept { sweepDriveScale_ = scale; }

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

    // Test accessors
    [[nodiscard]] int getActiveNodeCount() const noexcept { return activeNodeCount_; }
    [[nodiscard]] bool getAllSameType() const noexcept { return allSameType_; }
    [[nodiscard]] DistortionType getAdapterType(int idx) const noexcept { return adapters_[idx].getType(); }
    [[nodiscard]] DistortionType getNodeType(int idx) const noexcept { return nodes_[idx].type; }
    [[nodiscard]] const DistortionParams& getNodeParams(int idx) const noexcept { return nodes_[idx].params; }
    [[nodiscard]] const DistortionCommonParams& getNodeCommonParams(int idx) const noexcept { return nodes_[idx].commonParams; }
    [[nodiscard]] const DistortionAdapter& getAdapter(int idx) const noexcept { return adapters_[idx]; }
    /// @brief Last identical-type interpolateParams() result (set in processSameFamily).
    [[nodiscard]] const DistortionParams& getBlendedParams() const noexcept { return lastBlendedParams_; }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample through the morph engine.
    ///
    /// The signal flow depends on whether the active nodes share one type:
    /// - Same type: Parameter interpolation through single processor
    /// - Different types: Parallel processing with a constant-voltage crossfade
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

    /// @brief Cross-family output gain of a node with normalized weight @p weight.
    /// Constant-voltage law: the weight itself (0 and 1 exact), so the gains sum to 1.
    [[nodiscard]] static float transitionGainForWeight(float weight) noexcept;

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

    /// @brief Check if all active nodes have the same DistortionType.
    [[nodiscard]] bool isSameType() const noexcept;

    /// @brief Interpolate parameters for identical-type morphing.
    [[nodiscard]] DistortionParams interpolateParams() const noexcept;

    /// @brief Interpolate common parameters for identical-type morphing.
    [[nodiscard]] DistortionCommonParams interpolateCommonParams() const noexcept;

    /// @brief Process identical types through one processor with interpolated parameters.
    [[nodiscard]] float processSameFamily(float input) noexcept;

    /// @brief Process different types (of one family or not) in parallel.
    [[nodiscard]] float processCrossFamily(float input) noexcept;

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

    // Distortion adapters (one per potential node for parallel processing of different types)
    std::array<DistortionAdapter, kMaxMorphNodes> adapters_;

    // Single adapter for identical-type processing (optimization)
    DistortionAdapter blendedAdapter_;

    // Last interpolateParams() result of the identical-type path (test accessor)
    DistortionParams lastBlendedParams_;

    // Cached path check result (isSameType)
    bool allSameType_ = true;
    DistortionFamily dominantFamily_ = DistortionFamily::Saturation;

    // Prepared flag
    bool prepared_ = false;

    // Modulation offsets (block-rate, from ModulationEngine)
    float driveModOffset_ = 0.0f;  // [-1, +1] normalized offset
    float mixModOffset_ = 0.0f;    // [-1, +1] normalized offset
    float toneModOffset_ = 0.0f;   // [-1, +1] normalized offset (scaled by 7800 Hz)
    float biasModOffset_ = 0.0f;   // [-1, +1] normalized offset (scaled by 2.0)

    // Sweep drive multiplier (per sample, from BandProcessor) and the value the
    // cross-family adapters were last configured with
    float sweepDriveScale_ = 1.0f;
    float appliedSweepDriveScale_ = 1.0f;

    /// @brief Node drive with the modulation offset and the sweep multiplier applied.
    [[nodiscard]] float modulatedDrive(float baseDrive) const noexcept {
        float drive = baseDrive + driveModOffset_ * 10.0f;
        if (sweepDriveScale_ != 1.0f) {
            drive *= sweepDriveScale_;
        }
        return std::clamp(drive, 0.0f, 10.0f);
    }
};

} // namespace Disrumpo
