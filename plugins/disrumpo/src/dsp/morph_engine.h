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
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

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
/// - setMorphPosition/setMode/setNodes and the other setters: NOT thread-safe
///   (plain member writes). Call them on the audio thread: from parameter
///   changes in process(), or through Processor::setState's pending state,
///   which process() applies (consumePendingState)
/// - process(): Real-time audio thread only
///
/// @note Real-time safe: no allocations in process()
/// @note Per spec FR-001 through FR-019
class MorphEngine {
public:
    /// @brief Test probe (D-003): setNodes() calls over all instances, so a test can
    /// tell which thread or call reconfigured the live engines.
    static inline std::atomic<std::uint64_t> debugSetNodesCalls{0};

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

    /// @brief Take over @p other's cursor: target and smoothed position (its smoother's
    /// value and target), so this engine continues the other's glide. The weights are
    /// recomputed and the node parameters re-applied with the offsets at the next sample.
    /// Used when a band starts its second processing lane for an oversampling crossfade
    /// (D-034, follow-up 6): both lanes' engines get every setter, but only the running
    /// lane's smoothers advance. Allocation-free.
    void syncCursorFrom(const MorphEngine& other) noexcept;

    /// @brief Warm-start the adapters of this engine's path from @p other's (DistortionAdapter::
    /// warmStartFrom), adapter for adapter where both run the same type: an oversampling
    /// crossfade's incoming lane, after it is clocked at its rate (user ruling 2026-11-11, round 4).
    /// The identical-type adapter is configured first, as the next sample would. Allocation-free.
    void warmStartFrom(const MorphEngine& other) noexcept;

    // =========================================================================
    // Attached adapters (BandProcessor's oversampling crossfade, follow-up 6)
    // =========================================================================
    // By default the engine owns its five adapters (four nodes and the identical-type
    // one), allocated in its first prepare(). An owner that keeps the adapters itself calls
    // useAttachedAdapters() before prepare() and attaches them: an engine runs only the
    // adapters its path needs (the identical-type one, or the active nodes), so a second
    // engine that runs beside the first for a crossfade needs only those (user ruling
    // 2026-11-10, slim spare lane). A slot without an adapter is skipped by every setter.

    /// @brief Take adapters from attach*() instead of owning them. Call before prepare().
    void useAttachedAdapters() noexcept { attached_ = true; }

    /// @brief Attach @p adapter (prepared by the owner) as node @p idx's adapter, or detach
    /// it with nullptr. An attached adapter takes the node's type, parameters and the
    /// engine's latency alignment; its processing state and rate are the owner's.
    void attachNodeAdapter(int idx, DistortionAdapter* adapter) noexcept;

    /// @brief Attach @p adapter as the identical-type adapter (nullptr detaches it). It is
    /// routed again at the next identical-type sample.
    void attachBlendedAdapter(DistortionAdapter* adapter) noexcept;

    /// @brief Node @p idx's adapter, nullptr when none is attached.
    [[nodiscard]] DistortionAdapter* nodeAdapterSlot(int idx) const noexcept { return adapters_[idx]; }

    /// @brief The identical-type adapter, nullptr when none is attached.
    [[nodiscard]] DistortionAdapter* blendedAdapterSlot() const noexcept { return blendedAdapter_; }

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
    [[nodiscard]] const std::array<MorphNode, kMaxMorphNodes>& getNodes() const noexcept { return nodes_; }
    [[nodiscard]] bool getAllSameType() const noexcept { return allSameType_; }
    [[nodiscard]] DistortionType getAdapterType(int idx) const noexcept {
        return adapters_[idx] != nullptr ? adapters_[idx]->getType() : nodes_[idx].type;
    }
    [[nodiscard]] DistortionType getNodeType(int idx) const noexcept { return nodes_[idx].type; }
    [[nodiscard]] const DistortionParams& getNodeParams(int idx) const noexcept { return nodes_[idx].params; }
    [[nodiscard]] const DistortionCommonParams& getNodeCommonParams(int idx) const noexcept { return nodes_[idx].commonParams; }
    [[nodiscard]] const DistortionAdapter& getAdapter(int idx) const noexcept { return *adapters_[idx]; }
    /// @brief Last identical-type interpolateParams() result (set in processSameFamily).
    [[nodiscard]] const DistortionParams& getBlendedParams() const noexcept { return lastBlendedParams_; }
    /// @brief How often the identical-type path has routed params to its processor.
    [[nodiscard]] std::uint64_t getBlendedRouteCount() const noexcept { return blendedRouteCount_; }

    /// @brief Latency of process() output in clocked samples (D-081, T2.10): the largest
    /// latency among the active nodes' types. Every node adapter and the blended adapter
    /// is padded to it, so morphing between a latent and a non-latent node never
    /// crossfades two time offsets.
    [[nodiscard]] int getLatency() const noexcept { return latency_; }

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

    /// @brief The one morph weight function (D-089): weights of the first @p count
    /// nodes at cursor (@p x, @p y) in @p mode. Holds the mode switch (Linear1D on
    /// the i / (N - 1) axis, Planar2D Euclidean, Radial2D angular), the distance
    /// epsilon, the one-hot on-node snap and the kWeightThreshold step (FR-001,
    /// FR-014, FR-015). MorphEngine, BandProcessor's oversampling choice and the
    /// MorphPad display all call it. Defined out of line: one copy, one set of
    /// floating-point flags.
    [[nodiscard]] static std::array<float, kMaxMorphNodes> computeWeights(
        MorphMode mode, const std::array<MorphNode, kMaxMorphNodes>& nodes, int count, float x,
        float y) noexcept;

    /// @brief Cross-family output gain of a node with normalized weight @p weight.
    /// Constant-voltage law: the weight itself (0 and 1 exact), so the gains sum to 1.
    [[nodiscard]] static float transitionGainForWeight(float weight) noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Check if all active nodes have the same DistortionType.
    [[nodiscard]] bool isSameType() const noexcept;

    /// @brief Interpolate parameters for identical-type morphing.
    [[nodiscard]] DistortionParams interpolateParams() const noexcept;

    /// @brief Interpolate common parameters for identical-type morphing.
    [[nodiscard]] DistortionCommonParams interpolateCommonParams() const noexcept;

    /// @brief Process identical types through one processor with interpolated parameters.
    [[nodiscard]] float processSameFamily(float input) noexcept;

    /// @brief Route the interpolated parameters to the identical-type adapter when the weights
    /// moved, a setter ran or the sweep multiplier changed (processSameFamily's control rate).
    void routeBlendedParams() noexcept;

    /// @brief Process different types (of one family or not) in parallel.
    [[nodiscard]] float processCrossFamily(float input) noexcept;

    /// @brief True when the next process() call would change nothing but the adapters'
    /// state and the last output: cursor settled at the weights' position, no path
    /// switch or fade pending, nothing to route. Every later sample of the block then
    /// takes the same path, so processBlock() runs it as a block (processSteadySpan).
    [[nodiscard]] bool isSteady() const noexcept;

    /// @brief process() for every sample of a span that starts steady (isSteady()):
    /// the same adapter calls and the same per-sample sum, one block call per adapter.
    void processSteadySpan(const float* input, float* output, int numSamples) noexcept;

    /// @brief Recompute latency_ from the active nodes and align every adapter to it.
    void updateLatencyAlignment() noexcept;

    /// @brief Forget the processed path and the last output (all adapters were just cleared).
    void clearSwitchFade() noexcept;

    /// @brief reset() every attached adapter.
    void resetAttachedAdapters() noexcept;

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

    // The cursor position weights_ was computed at (calculateMorphWeights). process()
    // recomputes only when the smoothed cursor moves; setMode/setNodes recompute
    // directly and reset() invalidates.
    float weightsX_ = 0.0f;
    float weightsY_ = 0.0f;
    bool weightsValid_ = false;

    // processSteadySpan scratch: the span's input (the caller may process in place)
    // and one node's output, in chunks of kSpanChunk samples
    static constexpr int kSpanChunk = 256;
    std::array<float, kSpanChunk> spanInput_{};
    std::array<float, kSpanChunk> spanNode_{};

    // Distortion adapters (one per potential node for parallel processing of different
    // types) and the single adapter for identical-type processing: owned (ownedAdapters_,
    // the last entry the identical-type one) or attached by the owner (useAttachedAdapters)
    std::array<DistortionAdapter*, kMaxMorphNodes> adapters_{};
    DistortionAdapter* blendedAdapter_ = nullptr;
    std::unique_ptr<std::array<DistortionAdapter, kMaxMorphNodes + 1>> ownedAdapters_;
    bool attached_ = false;

    // Last interpolateParams() result of the identical-type path (test accessor)
    DistortionParams lastBlendedParams_;
    // setType/setParams/setCommonParams rounds of the identical-type path (test accessor)
    std::uint64_t blendedRouteCount_ = 0;

    // Control-rate routing of the identical-type path (D-005): the weights and sweep
    // drive multiplier it last routed, and a flag every node / offset setter and reset
    // sets so the next sample routes again
    static constexpr float kBlendedWeightEpsilon = 1e-6f;
    std::array<float, kMaxMorphNodes> blendedWeights_{};
    float blendedSweepDriveScale_ = 1.0f;
    bool blendedParamsDirty_ = true;

    // Output latency in clocked samples (updateLatencyAlignment)
    int latency_ = 0;

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

    // True while the cross-family adapters hold offset-modulated params, so the offsets'
    // return to all 0 re-applies the base node params once (D-031)
    bool offsetsWereActive_ = false;

    // Path switch / node activation fade (D-032): the path and node count process() last
    // ran, its last output, and a linear 5 ms crossfade from that output
    static constexpr double kSwitchFadeSeconds = 0.005;
    bool processedSameType_ = true;
    int processedNodeCount_ = kDefaultActiveNodes;
    bool hasOutput_ = false;
    float lastOutput_ = 0.0f;
    float switchFadeOrigin_ = 0.0f;
    int switchFadeLength_ = 1;
    int switchFadeRemaining_ = 0;
    // A path switch (identical-type <-> parallel) crossfades from the outgoing path, which keeps
    // running warm beside the incoming one for the fade, instead of from its frozen last output
    // (user ruling 2026-11-11, round 3: the frozen origin dipped SoftClip/Tube -> SoftClip/SoftClip
    // -4 dB in the first 2 ms). A node activation still fades from the last output.
    bool switchFadeFromPath_ = false;
    /// True when the path (identical-type: true, parallel: false) has every adapter it runs attached.
    [[nodiscard]] bool pathAttached(bool sameType) const noexcept;
    bool switchFadeFromSameType_ = true;  // the outgoing path: identical-type (true) or parallel

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
