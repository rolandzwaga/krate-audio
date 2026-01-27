// ==============================================================================
// Layer 2: DSP Processor - Ducking Processor (API Contract)
// ==============================================================================
// This is the API contract for DuckingProcessor. It defines the public interface
// that must be implemented. Do not use this file directly - use the implementation
// in src/dsp/processors/ducking_processor.h.
//
// A sidechain-triggered gain reduction processor that attenuates a main audio
// signal based on the level of an external sidechain signal.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]])
// - Principle IX: Layer 2 (depends on Layer 0-1 plus peer Layer 2 EnvelopeFollower)
// - Principle XII: Test-First Development
//
// Reference: specs/012-ducking-processor/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

// =============================================================================
// Ducking State Enumeration
// =============================================================================

/// @brief Internal state machine states for hold time behavior
enum class DuckingState : uint8_t {
    Idle = 0,     ///< Sidechain below threshold, no gain reduction
    Ducking = 1,  ///< Sidechain above threshold, gain reduction active
    Holding = 2   ///< Sidechain dropped below threshold, holding before release
};

// =============================================================================
// DuckingProcessor Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Sidechain-triggered gain reduction
///
/// Attenuates a main audio signal when an external sidechain signal exceeds
/// a threshold. Used for voiceover ducking, podcast mixing, and similar
/// applications where one audio source should automatically reduce the level
/// of another.
///
/// @par Key Features
/// - External sidechain input (FR-017)
/// - Threshold-triggered ducking (FR-001, FR-002, FR-003)
/// - Configurable depth (FR-004)
/// - Attack/release timing (FR-005, FR-006)
/// - Hold time to prevent chattering (FR-008, FR-009, FR-010)
/// - Range limit for maximum attenuation (FR-011, FR-012, FR-013)
/// - Optional sidechain highpass filter (FR-014, FR-015, FR-016)
/// - Gain reduction metering (FR-025)
/// - Zero latency (SC-008)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends on Layer 0-1 and peer EnvelopeFollower)
/// - Principle XII: Test-First Development
///
/// @par Usage
/// @code
/// DuckingProcessor ducker;
/// ducker.prepare(44100.0, 512);
/// ducker.setThreshold(-30.0f);
/// ducker.setDepth(-12.0f);
/// ducker.setHoldTime(50.0f);
///
/// // In process callback
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = ducker.processSample(mainInput[i], sidechainInput[i]);
/// }
/// // Or block processing:
/// ducker.process(mainBuffer, sidechainBuffer, outputBuffer, numSamples);
/// @endcode
class DuckingProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinThreshold = -60.0f;      // dB
    static constexpr float kMaxThreshold = 0.0f;        // dB
    static constexpr float kDefaultThreshold = -30.0f;  // dB

    static constexpr float kMinDepth = -48.0f;          // dB
    static constexpr float kMaxDepth = 0.0f;            // dB
    static constexpr float kDefaultDepth = -12.0f;      // dB

    static constexpr float kMinAttackMs = 0.1f;         // ms
    static constexpr float kMaxAttackMs = 500.0f;       // ms
    static constexpr float kDefaultAttackMs = 10.0f;    // ms

    static constexpr float kMinReleaseMs = 1.0f;        // ms
    static constexpr float kMaxReleaseMs = 5000.0f;     // ms
    static constexpr float kDefaultReleaseMs = 100.0f;  // ms

    static constexpr float kMinHoldMs = 0.0f;           // ms
    static constexpr float kMaxHoldMs = 1000.0f;        // ms
    static constexpr float kDefaultHoldMs = 50.0f;      // ms

    static constexpr float kMinRange = -48.0f;          // dB
    static constexpr float kMaxRange = 0.0f;            // dB (0 = disabled)
    static constexpr float kDefaultRange = 0.0f;        // dB (disabled)

    static constexpr float kMinSidechainHz = 20.0f;     // Hz
    static constexpr float kMaxSidechainHz = 500.0f;    // Hz
    static constexpr float kDefaultSidechainHz = 80.0f; // Hz

    // =========================================================================
    // Lifecycle (FR-023, FR-024)
    // =========================================================================

    /// @brief Default constructor
    DuckingProcessor() noexcept;

    /// @brief Destructor
    ~DuckingProcessor() = default;

    // Non-copyable, movable
    DuckingProcessor(const DuckingProcessor&) = delete;
    DuckingProcessor& operator=(const DuckingProcessor&) = delete;
    DuckingProcessor(DuckingProcessor&&) noexcept = default;
    DuckingProcessor& operator=(DuckingProcessor&&) noexcept = default;

    /// @brief Prepare processor for given sample rate
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note Call before audio processing begins
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset internal state without reallocation
    /// @note Clears envelope, gain state, and hold timer
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-017, FR-018, FR-019, FR-020, FR-021, FR-022)
    // =========================================================================

    /// @brief Process a single sample pair
    /// @param main Main audio sample to process
    /// @param sidechain Sidechain sample for level detection
    /// @return Processed (ducked) main signal
    /// @pre prepare() has been called
    [[nodiscard]] float processSample(float main, float sidechain) noexcept;

    /// @brief Process a block with separate main and sidechain buffers
    /// @param main Main audio input buffer
    /// @param sidechain Sidechain input buffer
    /// @param output Output buffer (may alias main for in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(const float* main, const float* sidechain,
                 float* output, size_t numSamples) noexcept;

    /// @brief Process a block in-place on main buffer
    /// @param mainInOut Main audio buffer (overwritten with output)
    /// @param sidechain Sidechain input buffer
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(float* mainInOut, const float* sidechain,
                 size_t numSamples) noexcept;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set threshold level (FR-003)
    /// @param dB Threshold in dB, clamped to [-60, 0]
    void setThreshold(float dB) noexcept;

    /// @brief Set ducking depth (FR-004)
    /// @param dB Depth in dB (negative value), clamped to [-48, 0]
    void setDepth(float dB) noexcept;

    /// @brief Set attack time (FR-005)
    /// @param ms Attack in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept;

    /// @brief Set release time (FR-006)
    /// @param ms Release in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept;

    /// @brief Set hold time (FR-008)
    /// @param ms Hold in milliseconds, clamped to [0, 1000]
    void setHoldTime(float ms) noexcept;

    /// @brief Set range/maximum attenuation limit (FR-011)
    /// @param dB Range in dB (negative value), clamped to [-48, 0]
    /// @note 0 dB disables range limiting
    void setRange(float dB) noexcept;

    /// @brief Enable or disable sidechain highpass filter (FR-015)
    /// @param enabled true to enable filter
    void setSidechainFilterEnabled(bool enabled) noexcept;

    /// @brief Set sidechain filter cutoff (FR-014)
    /// @param hz Cutoff in Hz, clamped to [20, 500]
    void setSidechainFilterCutoff(float hz) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] float getThreshold() const noexcept;
    [[nodiscard]] float getDepth() const noexcept;
    [[nodiscard]] float getAttackTime() const noexcept;
    [[nodiscard]] float getReleaseTime() const noexcept;
    [[nodiscard]] float getHoldTime() const noexcept;
    [[nodiscard]] float getRange() const noexcept;
    [[nodiscard]] bool isSidechainFilterEnabled() const noexcept;
    [[nodiscard]] float getSidechainFilterCutoff() const noexcept;

    // =========================================================================
    // Metering (FR-025)
    // =========================================================================

    /// @brief Get current gain reduction in dB
    /// @return Gain reduction (negative when ducking, 0 when idle)
    [[nodiscard]] float getCurrentGainReduction() const noexcept;

    // =========================================================================
    // Info
    // =========================================================================

    /// @brief Get processing latency in samples
    /// @return Latency (always 0 for DuckingProcessor per SC-008)
    [[nodiscard]] size_t getLatency() const noexcept;
};

}  // namespace DSP
}  // namespace Iterum
