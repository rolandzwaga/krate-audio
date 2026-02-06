// ==============================================================================
// API Contract: VectorMixer (Layer 3 System Component)
// ==============================================================================
// This file defines the PUBLIC API contract for the VectorMixer class.
// It is NOT compilable code -- it is a design contract documenting the
// exact signatures, parameter ranges, thread safety guarantees, and
// behavioral specifications.
//
// Reference: specs/031-vector-mixer/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/math_constants.h>  // kTwoPi
#include <krate/dsp/core/db_utils.h>        // detail::constexprExp, isNaN, isInf
#include <krate/dsp/core/stereo_output.h>   // StereoOutput (extracted from unison_engine.h)

#include <atomic>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// Enums (FR-009, FR-021)
// =============================================================================

/// @brief Spatial arrangement of the four sources.
enum class Topology : uint8_t {
    Square = 0,   ///< Bilinear interpolation. A=top-left, B=top-right, C=bottom-left, D=bottom-right.
    Diamond = 1   ///< Prophet VS style. A=left, B=right, C=top, D=bottom.
};

/// @brief Weight transformation applied after topology computation.
enum class MixingLaw : uint8_t {
    Linear = 0,      ///< Direct topology weights. Sum = 1.0.
    EqualPower = 1,   ///< sqrt(topology weights). Sum-of-squares = 1.0.
    SquareRoot = 2    ///< sqrt(topology weights). Equivalent to EqualPower for unit-sum inputs.
};

// =============================================================================
// Weights Struct (FR-017)
// =============================================================================

/// @brief Current mixing weights for the four sources.
struct Weights {
    float a = 0.25f;  ///< Weight for source A
    float b = 0.25f;  ///< Weight for source B
    float c = 0.25f;  ///< Weight for source C
    float d = 0.25f;  ///< Weight for source D
};

// =============================================================================
// VectorMixer Class
// =============================================================================

/// @brief XY vector mixer for 4 audio sources (Layer 3 system).
///
/// Computes mixing weights from a 2D XY position using selectable topology
/// (square bilinear or diamond/Prophet VS) and mixing law (linear, equal-power,
/// square-root). Supports per-axis exponential smoothing for artifact-free
/// parameter automation.
///
/// @par Thread Safety
/// Modulation parameters (X, Y, smoothing time) use std::atomic<float> and
/// are safe to set from any thread while processBlock() runs on the audio
/// thread. Structural configuration (topology, mixing law) is NOT thread-safe
/// and must only be changed when audio processing is stopped.
///
/// @par Real-Time Safety
/// All processing methods are fully real-time safe: no allocation, no
/// exceptions, no blocking, no I/O. Approximately 20 FLOPs per sample.
///
/// @par Memory
/// ~52 bytes per instance. No heap allocation. No internal buffers.
class VectorMixer {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002)
    // =========================================================================

    /// @brief Default constructor. Unprepared state.
    /// process() returns 0.0 until prepare() is called.
    VectorMixer() noexcept = default;

    /// @brief Initialize for the given sample rate (FR-001).
    ///
    /// Computes smoothing coefficient from current smoothing time.
    /// Resets smoothed positions to current targets.
    ///
    /// @param sampleRate Sample rate in Hz (must be > 0)
    ///
    /// @note NOT real-time safe (calls std::exp)
    /// @note Calling prepare() multiple times is safe
    void prepare(double sampleRate) noexcept;

    /// @brief Reset smoothed positions to current targets (FR-002).
    ///
    /// Snaps internal smoothed X/Y to their target values without
    /// deallocating memory. Preserves all configuration.
    ///
    /// @note Real-time safe, noexcept
    void reset() noexcept;

    // =========================================================================
    // XY Position Control (FR-003, FR-004)
    // =========================================================================

    /// @brief Set horizontal position (FR-003).
    /// @param x Position in [-1, 1]. Clamped. -1=left/A, +1=right/B.
    /// @note Thread-safe (atomic store). Can be called from any thread.
    void setVectorX(float x) noexcept;

    /// @brief Set vertical position (FR-003).
    /// @param y Position in [-1, 1]. Clamped. -1=top/A, +1=bottom/D.
    /// @note Thread-safe (atomic store). Can be called from any thread.
    void setVectorY(float y) noexcept;

    /// @brief Set both X and Y simultaneously (FR-004).
    /// @param x Horizontal position in [-1, 1]. Clamped.
    /// @param y Vertical position in [-1, 1]. Clamped.
    /// @note Thread-safe (two atomic stores). Can be called from any thread.
    void setVectorPosition(float x, float y) noexcept;

    // =========================================================================
    // Configuration (FR-009, FR-021, FR-022)
    // =========================================================================

    /// @brief Select topology (FR-021).
    /// @param topo Square or Diamond.
    /// @note NOT thread-safe. Only call when not processing.
    void setTopology(Topology topo) noexcept;

    /// @brief Select mixing law (FR-009).
    /// @param law Linear, EqualPower, or SquareRoot.
    /// @note NOT thread-safe. Only call when not processing.
    void setMixingLaw(MixingLaw law) noexcept;

    // =========================================================================
    // Smoothing (FR-018, FR-019)
    // =========================================================================

    /// @brief Set smoothing time in milliseconds (FR-018).
    /// @param ms Smoothing time. 0 = instant. Negative clamped to 0. Default: 5 ms.
    /// @note Thread-safe (atomic store). Coefficient recomputed on next sample.
    void setSmoothingTimeMs(float ms) noexcept;

    // =========================================================================
    // Processing - Mono (FR-013, FR-014)
    // =========================================================================

    /// @brief Process one mono sample (FR-013).
    /// @return Weighted sum of the four inputs using current smoothed position.
    /// @note Returns 0.0 if prepare() has not been called.
    /// @note Real-time safe, noexcept.
    [[nodiscard]] float process(float a, float b, float c, float d) noexcept;

    /// @brief Process a block of mono samples (FR-014).
    ///
    /// Smoothed position advances per-sample for artifact-free transitions.
    /// Supports block sizes up to 8192 samples.
    ///
    /// @param a Input buffer for source A (numSamples elements)
    /// @param b Input buffer for source B (numSamples elements)
    /// @param c Input buffer for source C (numSamples elements)
    /// @param d Input buffer for source D (numSamples elements)
    /// @param output Output buffer (numSamples elements)
    /// @param numSamples Number of samples to process
    ///
    /// @note Real-time safe, noexcept.
    void processBlock(const float* a, const float* b, const float* c, const float* d,
                      float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Processing - Stereo (FR-015, FR-016)
    // =========================================================================

    /// @brief Process one stereo sample (FR-015).
    /// @return {left, right} weighted sums using identical weights for both channels.
    /// @note Real-time safe, noexcept.
    [[nodiscard]] StereoOutput process(float aL, float aR,
                                        float bL, float bR,
                                        float cL, float cR,
                                        float dL, float dR) noexcept;

    /// @brief Process a block of stereo samples (FR-016).
    ///
    /// Identical weights applied to both channels. Supports block sizes up to 8192.
    ///
    /// @param aL, aR Left/right input buffers for source A
    /// @param bL, bR Left/right input buffers for source B
    /// @param cL, cR Left/right input buffers for source C
    /// @param dL, dR Left/right input buffers for source D
    /// @param outL, outR Left/right output buffers
    /// @param numSamples Number of samples to process
    ///
    /// @note Real-time safe, noexcept.
    void processBlock(const float* aL, const float* aR,
                      const float* bL, const float* bR,
                      const float* cL, const float* cR,
                      const float* dL, const float* dR,
                      float* outL, float* outR,
                      size_t numSamples) noexcept;

    // =========================================================================
    // Weight Query (FR-017)
    // =========================================================================

    /// @brief Get current mixing weights (FR-017).
    /// @return Weights struct reflecting the current smoothed position,
    ///         topology, and mixing law.
    /// @note Real-time safe, noexcept.
    [[nodiscard]] Weights getWeights() const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Compute weights for square (bilinear) topology (FR-005).
    [[nodiscard]] static Weights computeSquareWeights(float x, float y) noexcept;

    /// @brief Compute weights for diamond topology (FR-007).
    [[nodiscard]] static Weights computeDiamondWeights(float x, float y) noexcept;

    /// @brief Apply mixing law transformation to linear weights (FR-010, FR-011, FR-012).
    [[nodiscard]] static Weights applyMixingLaw(Weights linearWeights, MixingLaw law) noexcept;

    /// @brief Update smoothed position toward target by one sample (FR-019, FR-020).
    void advanceSmoothing() noexcept;

    /// @brief Recompute smoothing coefficient from current smoothing time and sample rate.
    void updateSmoothCoeff() noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Thread-safe modulation parameters (FR-026)
    std::atomic<float> targetX_{0.0f};
    std::atomic<float> targetY_{0.0f};
    std::atomic<float> smoothingTimeMs_{5.0f};

    // Internal smoothing state (audio thread only)
    float smoothedX_ = 0.0f;
    float smoothedY_ = 0.0f;
    float smoothCoeff_ = 0.0f;

    // Cached weights (updated per sample)
    Weights currentWeights_{};

    // Configuration (NOT thread-safe)
    Topology topology_ = Topology::Square;
    MixingLaw mixingLaw_ = MixingLaw::Linear;

    // State
    double sampleRate_ = 0.0;
    bool prepared_ = false;
};

} // namespace Krate::DSP
