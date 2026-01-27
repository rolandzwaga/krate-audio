// ==============================================================================
// Layer 2: DSP Processor - Dynamics Processor (Compressor/Limiter)
// ==============================================================================
// CONTRACT FILE - Defines the public API for DynamicsProcessor
// Implementation will be in src/dsp/processors/dynamics_processor.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XII: Test-First Development
//
// Reference: specs/011-dynamics-processor/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// Forward declarations (actual includes in implementation)
class EnvelopeFollower;
class OnePoleSmoother;
class DelayLine;
class Biquad;

// =============================================================================
// Detection Mode Enumeration (mirrors EnvelopeFollower)
// =============================================================================

/// @brief Detection algorithm type selection for level measurement
enum class DynamicsDetectionMode : uint8_t {
    RMS = 0,    ///< RMS detection - average-responding, suits program material
    Peak = 1    ///< Peak detection - transient-responding, suits limiting
};

// =============================================================================
// DynamicsProcessor Class (API Contract)
// =============================================================================

/// @brief Layer 2 DSP Processor - Dynamics control (compressor/limiter)
///
/// Provides flexible dynamics processing with:
/// - Configurable threshold, ratio, and knee
/// - Attack/release timing via EnvelopeFollower
/// - Optional soft knee for transparent compression
/// - Manual or auto makeup gain
/// - RMS or Peak detection modes
/// - Optional sidechain highpass filter
/// - Optional lookahead for transparent limiting
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated buffers)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends on Layer 0-1 components)
/// - Principle XII: Test-First Development
class DynamicsProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinThreshold = -60.0f;      // dB
    static constexpr float kMaxThreshold = 0.0f;        // dB
    static constexpr float kDefaultThreshold = -20.0f;  // dB

    static constexpr float kMinRatio = 1.0f;            // 1:1 (no compression)
    static constexpr float kMaxRatio = 100.0f;          // 100:1 (limiter)
    static constexpr float kDefaultRatio = 4.0f;        // 4:1

    static constexpr float kMinKnee = 0.0f;             // dB (hard knee)
    static constexpr float kMaxKnee = 24.0f;            // dB
    static constexpr float kDefaultKnee = 0.0f;         // dB (hard knee)

    static constexpr float kMinAttackMs = 0.1f;         // ms
    static constexpr float kMaxAttackMs = 500.0f;       // ms
    static constexpr float kDefaultAttackMs = 10.0f;    // ms

    static constexpr float kMinReleaseMs = 1.0f;        // ms
    static constexpr float kMaxReleaseMs = 5000.0f;     // ms
    static constexpr float kDefaultReleaseMs = 100.0f;  // ms

    static constexpr float kMinMakeupGain = -24.0f;     // dB
    static constexpr float kMaxMakeupGain = 24.0f;      // dB
    static constexpr float kDefaultMakeupGain = 0.0f;   // dB

    static constexpr float kMinLookaheadMs = 0.0f;      // ms (disabled)
    static constexpr float kMaxLookaheadMs = 10.0f;     // ms
    static constexpr float kDefaultLookaheadMs = 0.0f;  // ms (disabled)

    static constexpr float kMinSidechainHz = 20.0f;     // Hz
    static constexpr float kMaxSidechainHz = 500.0f;    // Hz
    static constexpr float kDefaultSidechainHz = 80.0f; // Hz

    // =========================================================================
    // Lifecycle (FR-024, FR-025)
    // =========================================================================

    /// @brief Default constructor
    DynamicsProcessor() noexcept;

    /// @brief Destructor
    ~DynamicsProcessor() = default;

    // Non-copyable, movable
    DynamicsProcessor(const DynamicsProcessor&) = delete;
    DynamicsProcessor& operator=(const DynamicsProcessor&) = delete;
    DynamicsProcessor(DynamicsProcessor&&) noexcept = default;
    DynamicsProcessor& operator=(DynamicsProcessor&&) noexcept = default;

    /// @brief Prepare processor for given sample rate
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note Allocates lookahead buffer; call before setActive(true)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset internal state without reallocation
    /// @note Clears envelope, gain state, and delay line
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-001, FR-002, FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed (compressed) sample
    /// @pre prepare() has been called
    [[nodiscard]] float processSample(float input) noexcept;

    /// @brief Process a block of samples in-place
    /// @param buffer Audio buffer (overwritten with processed audio)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a block with separate input/output buffers
    /// @param input Input audio buffer
    /// @param output Output audio buffer
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(const float* input, float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Parameter Setters (FR-003 to FR-011, FR-012 to FR-015, FR-018)
    // =========================================================================

    /// @brief Set compression threshold
    /// @param dB Threshold in dB, clamped to [-60, 0]
    void setThreshold(float dB) noexcept;

    /// @brief Set compression ratio
    /// @param ratio Ratio (e.g., 4.0 for 4:1), clamped to [1, 100]
    /// @note ratio >= 100 is treated as infinity (limiter mode)
    void setRatio(float ratio) noexcept;

    /// @brief Set soft knee width
    /// @param dB Knee width in dB, clamped to [0, 24]
    /// @note 0 dB = hard knee
    void setKneeWidth(float dB) noexcept;

    /// @brief Set attack time
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept;

    /// @brief Set release time
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept;

    /// @brief Set manual makeup gain
    /// @param dB Makeup gain in dB, clamped to [-24, 24]
    void setMakeupGain(float dB) noexcept;

    /// @brief Enable or disable auto makeup gain
    /// @param enabled true to auto-calculate makeup from threshold/ratio
    void setAutoMakeup(bool enabled) noexcept;

    /// @brief Set detection mode (RMS or Peak)
    /// @param mode Detection algorithm
    void setDetectionMode(DynamicsDetectionMode mode) noexcept;

    /// @brief Set lookahead time
    /// @param ms Lookahead in milliseconds, clamped to [0, 10]
    /// @note 0 ms = disabled (no latency)
    void setLookahead(float ms) noexcept;

    /// @brief Enable or disable sidechain highpass filter
    /// @param enabled true to enable sidechain filter
    void setSidechainEnabled(bool enabled) noexcept;

    /// @brief Set sidechain filter cutoff frequency
    /// @param hz Cutoff in Hz, clamped to [20, 500]
    void setSidechainCutoff(float hz) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] float getThreshold() const noexcept;
    [[nodiscard]] float getRatio() const noexcept;
    [[nodiscard]] float getKneeWidth() const noexcept;
    [[nodiscard]] float getAttackTime() const noexcept;
    [[nodiscard]] float getReleaseTime() const noexcept;
    [[nodiscard]] float getMakeupGain() const noexcept;
    [[nodiscard]] bool isAutoMakeupEnabled() const noexcept;
    [[nodiscard]] DynamicsDetectionMode getDetectionMode() const noexcept;
    [[nodiscard]] float getLookahead() const noexcept;
    [[nodiscard]] bool isSidechainEnabled() const noexcept;
    [[nodiscard]] float getSidechainCutoff() const noexcept;

    // =========================================================================
    // Metering (FR-016, FR-017)
    // =========================================================================

    /// @brief Get current gain reduction in dB
    /// @return Gain reduction (0 = no reduction, negative = reduction applied)
    [[nodiscard]] float getCurrentGainReduction() const noexcept;

    // =========================================================================
    // Info (FR-020)
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency (equals lookahead in samples, 0 if disabled)
    [[nodiscard]] size_t getLatency() const noexcept;

private:
    // Internal implementation details will be in the actual header
    // This contract defines only the public API
};

}  // namespace DSP
}  // namespace Iterum
