// ==============================================================================
// Layer 3: System Component - Vector Mixer
// ==============================================================================
// XY vector mixer for 4 audio sources with selectable topologies (square
// bilinear, diamond/Prophet VS), three mixing laws (linear, equal-power,
// square-root), per-axis exponential smoothing, and mono/stereo processing.
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (noexcept, no allocations in process())
// - Principle III: Modern C++ (C++20, std::atomic, [[nodiscard]])
// - Principle IX:  Layer 3 (depends on Layer 0 only)
// - Principle XII: Test-First Development
// - Principle XIV: ODR Prevention (unique class names verified)
//
// Reference: specs/031-vector-mixer/spec.md
// ==============================================================================

#pragma once

// Layer 0 dependencies
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/stereo_output.h>

// Standard library
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
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
    void prepare(double sampleRate) noexcept;

    /// @brief Reset smoothed positions to current targets (FR-002).
    void reset() noexcept;

    // =========================================================================
    // XY Position Control (FR-003, FR-004)
    // =========================================================================

    /// @brief Set horizontal position (FR-003).
    /// @param x Position in [-1, 1]. Clamped. -1=left/A, +1=right/B.
    /// @note Thread-safe (atomic store).
    void setVectorX(float x) noexcept;

    /// @brief Set vertical position (FR-003).
    /// @param y Position in [-1, 1]. Clamped. -1=top/A, +1=bottom/D.
    /// @note Thread-safe (atomic store).
    void setVectorY(float y) noexcept;

    /// @brief Set both X and Y simultaneously (FR-004).
    /// @note Thread-safe (two atomic stores).
    void setVectorPosition(float x, float y) noexcept;

    // =========================================================================
    // Configuration (FR-009, FR-021, FR-022)
    // =========================================================================

    /// @brief Select topology (FR-021).
    /// @note NOT thread-safe. Only call when not processing.
    void setTopology(Topology topo) noexcept;

    /// @brief Select mixing law (FR-009).
    /// @note NOT thread-safe. Only call when not processing.
    void setMixingLaw(MixingLaw law) noexcept;

    // =========================================================================
    // Smoothing (FR-018, FR-019)
    // =========================================================================

    /// @brief Set smoothing time in milliseconds (FR-018).
    /// @param ms Smoothing time. 0 = instant. Negative clamped to 0. Default: 5 ms.
    /// @note Thread-safe (atomic store).
    void setSmoothingTimeMs(float ms) noexcept;

    // =========================================================================
    // Processing - Mono (FR-013, FR-014)
    // =========================================================================

    /// @brief Process one mono sample (FR-013).
    /// @return Weighted sum of the four inputs. Returns 0.0 if not prepared.
    [[nodiscard]] float process(float a, float b, float c, float d) noexcept;

    /// @brief Process a block of mono samples (FR-014).
    void processBlock(const float* a, const float* b, const float* c, const float* d,
                      float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Processing - Stereo (FR-015, FR-016)
    // =========================================================================

    /// @brief Process one stereo sample (FR-015).
    [[nodiscard]] StereoOutput process(float aL, float aR,
                                        float bL, float bR,
                                        float cL, float cR,
                                        float dL, float dR) noexcept;

    /// @brief Process a block of stereo samples (FR-016).
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
    [[nodiscard]] Weights getWeights() const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    [[nodiscard]] static Weights computeSquareWeights(float x, float y) noexcept;
    [[nodiscard]] static Weights computeDiamondWeights(float x, float y) noexcept;
    [[nodiscard]] static Weights applyMixingLaw(Weights linearWeights, MixingLaw law) noexcept;
    void advanceSmoothing() noexcept;
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

// =============================================================================
// Inline Implementation
// =============================================================================

inline void VectorMixer::prepare(double sampleRate) noexcept {
    assert(sampleRate > 0.0);
    if (sampleRate <= 0.0) return;

    sampleRate_ = sampleRate;
    prepared_ = true;

    // Snap smoothed positions to current targets
    smoothedX_ = targetX_.load(std::memory_order_relaxed);
    smoothedY_ = targetY_.load(std::memory_order_relaxed);

    // Compute smoothing coefficient
    updateSmoothCoeff();

    // Compute initial weights
    Weights linear{};
    if (topology_ == Topology::Diamond) {
        linear = computeDiamondWeights(smoothedX_, smoothedY_);
    } else {
        linear = computeSquareWeights(smoothedX_, smoothedY_);
    }
    currentWeights_ = applyMixingLaw(linear, mixingLaw_);
}

inline void VectorMixer::reset() noexcept {
    smoothedX_ = targetX_.load(std::memory_order_relaxed);
    smoothedY_ = targetY_.load(std::memory_order_relaxed);

    // Recompute weights at snapped position
    Weights linear{};
    if (topology_ == Topology::Diamond) {
        linear = computeDiamondWeights(smoothedX_, smoothedY_);
    } else {
        linear = computeSquareWeights(smoothedX_, smoothedY_);
    }
    currentWeights_ = applyMixingLaw(linear, mixingLaw_);
}

inline void VectorMixer::setVectorX(float x) noexcept {
    targetX_.store(std::clamp(x, -1.0f, 1.0f), std::memory_order_relaxed);
}

inline void VectorMixer::setVectorY(float y) noexcept {
    targetY_.store(std::clamp(y, -1.0f, 1.0f), std::memory_order_relaxed);
}

inline void VectorMixer::setVectorPosition(float x, float y) noexcept {
    targetX_.store(std::clamp(x, -1.0f, 1.0f), std::memory_order_relaxed);
    targetY_.store(std::clamp(y, -1.0f, 1.0f), std::memory_order_relaxed);
}

inline void VectorMixer::setTopology(Topology topo) noexcept {
    topology_ = topo;
}

inline void VectorMixer::setMixingLaw(MixingLaw law) noexcept {
    mixingLaw_ = law;
}

inline void VectorMixer::setSmoothingTimeMs(float ms) noexcept {
    const float clamped = (ms < 0.0f) ? 0.0f : ms;
    smoothingTimeMs_.store(clamped, std::memory_order_relaxed);
    // Coefficient will be recomputed lazily per-sample or eagerly here
    // if we have a valid sample rate
    if (prepared_) {
        updateSmoothCoeff();
    }
}

inline void VectorMixer::updateSmoothCoeff() noexcept {
    const float timeMs = smoothingTimeMs_.load(std::memory_order_relaxed);
    if (timeMs <= 0.0f) {
        smoothCoeff_ = 0.0f;  // Instant response
    } else {
        smoothCoeff_ = std::exp(-kTwoPi / (timeMs * 0.001f * static_cast<float>(sampleRate_)));
    }
}

inline void VectorMixer::advanceSmoothing() noexcept {
    const float targetX = targetX_.load(std::memory_order_relaxed);
    const float targetY = targetY_.load(std::memory_order_relaxed);

    // One-pole update: smoothed = target + coeff * (smoothed - target)
    smoothedX_ = targetX + smoothCoeff_ * (smoothedX_ - targetX);
    smoothedY_ = targetY + smoothCoeff_ * (smoothedY_ - targetY);
}

[[nodiscard]] inline Weights VectorMixer::computeSquareWeights(float x, float y) noexcept {
    // Map [-1, 1] to [0, 1]
    const float u = (x + 1.0f) * 0.5f;
    const float v = (y + 1.0f) * 0.5f;

    return Weights{
        .a = (1.0f - u) * (1.0f - v),  // top-left
        .b = u * (1.0f - v),             // top-right
        .c = (1.0f - u) * v,             // bottom-left
        .d = u * v                        // bottom-right
    };
}

[[nodiscard]] inline Weights VectorMixer::computeDiamondWeights(float x, float y) noexcept {
    const float absX = std::abs(x);
    const float absY = std::abs(y);

    // Raw weights (Prophet VS-inspired formula)
    const float rA = (1.0f - x) * (1.0f - absY);   // left
    const float rB = (1.0f + x) * (1.0f - absY);   // right
    const float rC = (1.0f + y) * (1.0f - absX);   // top
    const float rD = (1.0f - y) * (1.0f - absX);   // bottom

    // Sum-normalization (R-005): guarantees solo weights at cardinal points
    const float sum = rA + rB + rC + rD;
    if (sum <= 0.0f) {
        return Weights{0.25f, 0.25f, 0.25f, 0.25f};
    }
    const float invSum = 1.0f / sum;

    return Weights{
        .a = rA * invSum,
        .b = rB * invSum,
        .c = rC * invSum,
        .d = rD * invSum
    };
}

[[nodiscard]] inline Weights VectorMixer::applyMixingLaw(Weights linearWeights, MixingLaw law) noexcept {
    switch (law) {
        case MixingLaw::EqualPower:
        case MixingLaw::SquareRoot:
            // Both use sqrt(linear weight) -- mathematically equivalent for
            // unit-sum topology weights (R-006, R-007)
            return Weights{
                .a = std::sqrt(linearWeights.a),
                .b = std::sqrt(linearWeights.b),
                .c = std::sqrt(linearWeights.c),
                .d = std::sqrt(linearWeights.d)
            };
        case MixingLaw::Linear:
        default:
            return linearWeights;
    }
}

[[nodiscard]] inline float VectorMixer::process(float a, float b, float c, float d) noexcept {
    if (!prepared_) return 0.0f;

    // Debug assertions for NaN/Inf inputs (FR-025)
    assert(!detail::isNaN(a) && !detail::isInf(a));
    assert(!detail::isNaN(b) && !detail::isInf(b));
    assert(!detail::isNaN(c) && !detail::isInf(c));
    assert(!detail::isNaN(d) && !detail::isInf(d));

    // Advance smoothing
    advanceSmoothing();

    // Compute weights from smoothed position
    Weights linear{};
    if (topology_ == Topology::Diamond) {
        linear = computeDiamondWeights(smoothedX_, smoothedY_);
    } else {
        linear = computeSquareWeights(smoothedX_, smoothedY_);
    }
    currentWeights_ = applyMixingLaw(linear, mixingLaw_);

    // Weighted sum
    return currentWeights_.a * a + currentWeights_.b * b +
           currentWeights_.c * c + currentWeights_.d * d;
}

inline void VectorMixer::processBlock(const float* a, const float* b,
                                       const float* c, const float* d,
                                       float* output, size_t numSamples) noexcept {
    if (!prepared_) {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = 0.0f;
        }
        return;
    }

    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = process(a[i], b[i], c[i], d[i]);
    }
}

[[nodiscard]] inline StereoOutput VectorMixer::process(float aL, float aR,
                                                        float bL, float bR,
                                                        float cL, float cR,
                                                        float dL, float dR) noexcept {
    if (!prepared_) return StereoOutput{0.0f, 0.0f};

    // Debug assertions for NaN/Inf inputs (FR-025)
    assert(!detail::isNaN(aL) && !detail::isInf(aL));
    assert(!detail::isNaN(aR) && !detail::isInf(aR));
    assert(!detail::isNaN(bL) && !detail::isInf(bL));
    assert(!detail::isNaN(bR) && !detail::isInf(bR));
    assert(!detail::isNaN(cL) && !detail::isInf(cL));
    assert(!detail::isNaN(cR) && !detail::isInf(cR));
    assert(!detail::isNaN(dL) && !detail::isInf(dL));
    assert(!detail::isNaN(dR) && !detail::isInf(dR));

    // Advance smoothing
    advanceSmoothing();

    // Compute weights from smoothed position
    Weights linear{};
    if (topology_ == Topology::Diamond) {
        linear = computeDiamondWeights(smoothedX_, smoothedY_);
    } else {
        linear = computeSquareWeights(smoothedX_, smoothedY_);
    }
    currentWeights_ = applyMixingLaw(linear, mixingLaw_);

    // Identical weights for both channels
    const float left = currentWeights_.a * aL + currentWeights_.b * bL +
                       currentWeights_.c * cL + currentWeights_.d * dL;
    const float right = currentWeights_.a * aR + currentWeights_.b * bR +
                        currentWeights_.c * cR + currentWeights_.d * dR;

    return StereoOutput{left, right};
}

inline void VectorMixer::processBlock(const float* aL, const float* aR,
                                       const float* bL, const float* bR,
                                       const float* cL, const float* cR,
                                       const float* dL, const float* dR,
                                       float* outL, float* outR,
                                       size_t numSamples) noexcept {
    if (!prepared_) {
        for (size_t i = 0; i < numSamples; ++i) {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        return;
    }

    for (size_t i = 0; i < numSamples; ++i) {
        const auto out = process(aL[i], aR[i], bL[i], bR[i],
                                  cL[i], cR[i], dL[i], dR[i]);
        outL[i] = out.left;
        outR[i] = out.right;
    }
}

[[nodiscard]] inline Weights VectorMixer::getWeights() const noexcept {
    return currentWeights_;
}

} // namespace Krate::DSP
