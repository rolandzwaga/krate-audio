// ==============================================================================
// API Contract: Sidechain Filter Processor
// ==============================================================================
// Layer 2: DSP Processors
// Feature: 090-sidechain-filter
//
// This file defines the public API contract for SidechainFilter.
// Implementation details are omitted - this is for planning and review.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layers 0-1, peer Layer 2 EnvelopeFollower)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief State machine states for hold behavior
enum class SidechainFilterState : uint8_t {
    Idle = 0,     ///< Below threshold, filter at resting position
    Active = 1,   ///< Above threshold, envelope controlling filter
    Holding = 2   ///< Below threshold but in hold period
};

/// @brief Envelope-to-cutoff mapping direction
/// @note Declared locally to avoid EnvelopeFilter dependency
enum class Direction : uint8_t {
    Up = 0,    ///< Louder -> higher cutoff, rests at minCutoff when silent
    Down = 1   ///< Louder -> lower cutoff, rests at maxCutoff when silent
};

/// @brief Filter response type
/// @note Maps to SVFMode internally
enum class FilterType : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass
    Bandpass = 1,  ///< Constant 0 dB peak bandpass
    Highpass = 2   ///< 12 dB/oct highpass
};

// =============================================================================
// SidechainFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Sidechain-controlled dynamic filter
///
/// Dynamically controls a filter's cutoff frequency based on the amplitude
/// envelope of a sidechain signal. Supports external sidechain for ducking/pumping
/// effects and self-sidechain for auto-wah with optional lookahead.
///
/// @par Key Features
/// - External sidechain input for ducking/pumping (FR-001)
/// - Self-sidechain mode for auto-wah effects (FR-002)
/// - Configurable attack/release envelope times (FR-003, FR-004)
/// - Threshold triggering with dB domain comparison (FR-005)
/// - Hold time to prevent chattering (FR-014, FR-015, FR-016)
/// - Lookahead for transient anticipation (FR-013)
/// - Log-space cutoff mapping for perceptual linearity (FR-012)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (composes EnvelopeFollower, SVF, DelayLine)
///
/// @par Usage
/// @code
/// SidechainFilter filter;
/// filter.prepare(48000.0, 512);
/// filter.setDirection(Direction::Down);
/// filter.setThreshold(-30.0f);
///
/// // External sidechain
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.processSample(mainInput[i], sidechainInput[i]);
/// }
///
/// // Self-sidechain
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.processSample(input[i]);
/// }
/// @endcode
class SidechainFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kMinThresholdDb = -60.0f;
    static constexpr float kMaxThresholdDb = 0.0f;
    static constexpr float kMinSensitivityDb = -24.0f;
    static constexpr float kMaxSensitivityDb = 24.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kMinLookaheadMs = 0.0f;
    static constexpr float kMaxLookaheadMs = 50.0f;
    static constexpr float kMinHoldMs = 0.0f;
    static constexpr float kMaxHoldMs = 1000.0f;
    static constexpr float kMinSidechainHpHz = 20.0f;
    static constexpr float kMaxSidechainHpHz = 500.0f;

    // Defaults
    static constexpr float kDefaultAttackMs = 10.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;
    static constexpr float kDefaultThresholdDb = -30.0f;
    static constexpr float kDefaultSensitivityDb = 0.0f;
    static constexpr float kDefaultMinCutoffHz = 200.0f;
    static constexpr float kDefaultMaxCutoffHz = 2000.0f;
    static constexpr float kDefaultResonance = 8.0f;
    static constexpr float kDefaultSidechainHpHz = 80.0f;

    // =========================================================================
    // Lifecycle (FR-024, FR-025, FR-026)
    // =========================================================================

    /// @brief Default constructor
    SidechainFilter() noexcept = default;

    /// @brief Destructor
    ~SidechainFilter() = default;

    // Non-copyable (contains filter state)
    SidechainFilter(const SidechainFilter&) = delete;
    SidechainFilter& operator=(const SidechainFilter&) = delete;

    // Movable
    SidechainFilter(SidechainFilter&&) noexcept = default;
    SidechainFilter& operator=(SidechainFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-024)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @pre sampleRate >= 1000.0
    /// @note NOT real-time safe (allocates DelayLine buffer)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset internal state without reallocation (FR-025)
    /// @post Envelope cleared, filter reset, hold timer cleared
    /// @note Real-time safe
    void reset() noexcept;

    /// @brief Get processing latency in samples (FR-026)
    /// @return Latency (equals lookahead in samples, 0 if lookahead disabled)
    [[nodiscard]] size_t getLatency() const noexcept;

    // =========================================================================
    // Processing (FR-019, FR-020, FR-021)
    // =========================================================================

    /// @brief Process with external sidechain (FR-001, FR-019)
    /// @param mainInput Main audio sample to filter
    /// @param sidechainInput Sidechain sample for envelope detection
    /// @return Filtered output sample
    /// @pre prepare() has been called
    /// @note Real-time safe (noexcept, no allocations)
    [[nodiscard]] float processSample(float mainInput, float sidechainInput) noexcept;

    /// @brief Process with self-sidechain (FR-002)
    /// @param input Audio sample (used for both filtering and envelope)
    /// @return Filtered output sample
    /// @pre prepare() has been called
    /// @note In self-sidechain mode with lookahead, sidechain sees undelayed
    ///       signal while audio path is delayed (FR-013 clarification)
    [[nodiscard]] float processSample(float input) noexcept;

    /// @brief Block processing with external sidechain (FR-020)
    /// @param mainInput Main audio input buffer
    /// @param sidechainInput Sidechain input buffer
    /// @param output Output buffer (may alias mainInput)
    /// @param numSamples Number of samples to process
    void process(const float* mainInput, const float* sidechainInput,
                 float* output, size_t numSamples) noexcept;

    /// @brief Block processing in-place with external sidechain (FR-021)
    /// @param mainInOut Main audio buffer (modified in-place)
    /// @param sidechainInput Sidechain input buffer
    /// @param numSamples Number of samples to process
    void process(float* mainInOut, const float* sidechainInput,
                 size_t numSamples) noexcept;

    /// @brief Block processing with self-sidechain
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Sidechain Detection Parameters (FR-003 to FR-006)
    // =========================================================================

    /// @brief Set envelope attack time (FR-003)
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept;

    /// @brief Set envelope release time (FR-004)
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept;

    /// @brief Set trigger threshold (FR-005)
    /// @param dB Threshold in dB, clamped to [-60, 0]
    /// @note Comparison is: 20*log10(envelope) > threshold
    void setThreshold(float dB) noexcept;

    /// @brief Set sidechain sensitivity/pre-gain (FR-006)
    /// @param dB Gain in dB, clamped to [-24, +24]
    void setSensitivity(float dB) noexcept;

    [[nodiscard]] float getAttackTime() const noexcept;
    [[nodiscard]] float getReleaseTime() const noexcept;
    [[nodiscard]] float getThreshold() const noexcept;
    [[nodiscard]] float getSensitivity() const noexcept;

    // =========================================================================
    // Filter Response Parameters (FR-007 to FR-012)
    // =========================================================================

    /// @brief Set envelope-to-cutoff direction (FR-007)
    /// @param dir Up (louder=higher cutoff) or Down (louder=lower cutoff)
    void setDirection(Direction dir) noexcept;

    /// @brief Set minimum cutoff frequency (FR-008)
    /// @param hz Frequency in Hz, clamped to [20, maxCutoff-1]
    void setMinCutoff(float hz) noexcept;

    /// @brief Set maximum cutoff frequency (FR-009)
    /// @param hz Frequency in Hz, clamped to [minCutoff+1, sampleRate*0.45]
    void setMaxCutoff(float hz) noexcept;

    /// @brief Set filter resonance (FR-010)
    /// @param q Q value, clamped to [0.5, 20.0]
    void setResonance(float q) noexcept;

    /// @brief Set filter type (FR-011)
    /// @param type Lowpass, Bandpass, or Highpass
    void setFilterType(FilterType type) noexcept;

    [[nodiscard]] Direction getDirection() const noexcept;
    [[nodiscard]] float getMinCutoff() const noexcept;
    [[nodiscard]] float getMaxCutoff() const noexcept;
    [[nodiscard]] float getResonance() const noexcept;
    [[nodiscard]] FilterType getFilterType() const noexcept;

    // =========================================================================
    // Timing Parameters (FR-013 to FR-016)
    // =========================================================================

    /// @brief Set lookahead time (FR-013)
    /// @param ms Lookahead in milliseconds, clamped to [0, 50]
    /// @note Adds latency equal to lookahead time
    void setLookahead(float ms) noexcept;

    /// @brief Set hold time (FR-014)
    /// @param ms Hold time in milliseconds, clamped to [0, 1000]
    /// @note Hold delays release without affecting attack (FR-015)
    /// @note Re-triggering during hold resets the timer (FR-016)
    void setHoldTime(float ms) noexcept;

    [[nodiscard]] float getLookahead() const noexcept;
    [[nodiscard]] float getHoldTime() const noexcept;

    // =========================================================================
    // Sidechain Filter Parameters (FR-017, FR-018)
    // =========================================================================

    /// @brief Enable/disable sidechain highpass filter (FR-017)
    /// @param enabled true to enable filter
    void setSidechainFilterEnabled(bool enabled) noexcept;

    /// @brief Set sidechain filter cutoff (FR-018)
    /// @param hz Cutoff in Hz, clamped to [20, 500]
    void setSidechainFilterCutoff(float hz) noexcept;

    [[nodiscard]] bool isSidechainFilterEnabled() const noexcept;
    [[nodiscard]] float getSidechainFilterCutoff() const noexcept;

    // =========================================================================
    // Monitoring (FR-027, FR-028)
    // =========================================================================

    /// @brief Get current filter cutoff frequency (FR-027)
    /// @return Cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept;

    /// @brief Get current envelope value (FR-028)
    /// @return Envelope value (linear, typically 0.0 to 1.0, may exceed 1.0)
    [[nodiscard]] float getCurrentEnvelope() const noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Implementation details omitted from contract
    // See plan.md for full class design
};

} // namespace DSP
} // namespace Krate
