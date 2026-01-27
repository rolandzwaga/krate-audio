// ==============================================================================
// API Contract: SampleHoldFilter
// ==============================================================================
// This file defines the public API contract for the SampleHoldFilter class.
// Implementation must conform to these signatures exactly.
//
// Feature: 089-sample-hold-filter
// Layer: 2 (DSP Processors)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations (FR-001, FR-006)
// =============================================================================

/// @brief Trigger mode selection for S&H timing (FR-001)
enum class TriggerSource : uint8_t {
    Clock = 0,   ///< Regular intervals based on hold time (FR-003)
    Audio,       ///< Transient detection from input signal (FR-004)
    Random       ///< Probability-based at hold intervals (FR-005)
};

/// @brief Sample value source selection per parameter (FR-006)
/// @note All sources output bipolar [-1, 1] for consistent modulation.
///       Envelope and External sources use conversion: (value * 2) - 1
enum class SampleSource : uint8_t {
    LFO = 0,     ///< Internal LFO output [-1, 1] (FR-007)
    Random,      ///< Xorshift32 random value [-1, 1] (FR-008)
    Envelope,    ///< EnvelopeFollower output [0,1] -> [-1,1] (FR-009)
    External     ///< User-provided value [0,1] -> [-1,1] (FR-010)
};

// =============================================================================
// SampleHoldFilter Class API
// =============================================================================

/// @brief Layer 2 DSP Processor - Sample & Hold Filter
///
/// Samples and holds filter parameters at configurable intervals,
/// creating stepped modulation effects synchronized to clock, audio
/// transients, or random probability.
///
/// @par Features
/// - Three trigger modes: Clock, Audio, Random (FR-001)
/// - Four sample sources per parameter: LFO, Random, Envelope, External (FR-006)
/// - Per-parameter source independence (FR-014)
/// - Stereo processing with symmetric pan offset (FR-013)
/// - Slew limiting for smooth transitions (FR-015, FR-016)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends on Layers 0-1)
class SampleHoldFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinHoldTimeMs = 0.1f;     ///< FR-002: Minimum hold time
    static constexpr float kMaxHoldTimeMs = 10000.0f; ///< FR-002: Maximum hold time
    static constexpr float kMinSlewTimeMs = 0.0f;     ///< FR-015: Instant
    static constexpr float kMaxSlewTimeMs = 500.0f;   ///< FR-015: Maximum slew
    static constexpr float kMinLFORate = 0.01f;       ///< FR-007: Minimum LFO rate
    static constexpr float kMaxLFORate = 20.0f;       ///< FR-007: Maximum LFO rate
    static constexpr float kMinCutoffOctaves = 0.0f;  ///< FR-011: No modulation
    static constexpr float kMaxCutoffOctaves = 8.0f;  ///< FR-011: 8 octaves
    static constexpr float kMinQRange = 0.0f;         ///< FR-012: No modulation
    static constexpr float kMaxQRange = 1.0f;         ///< FR-012: Full range
    static constexpr float kMinPanOctaveRange = 0.0f; ///< FR-013: No pan offset
    static constexpr float kMaxPanOctaveRange = 4.0f; ///< FR-013: 4 octave max offset
    static constexpr float kDefaultBaseQ = 0.707f;    ///< FR-020: Butterworth Q
    static constexpr float kMinBaseCutoff = 20.0f;    ///< FR-019: 20 Hz
    static constexpr float kMaxBaseCutoff = 20000.0f; ///< FR-019: 20 kHz
    static constexpr float kMinBaseQ = 0.1f;          ///< FR-020
    static constexpr float kMaxBaseQ = 30.0f;         ///< FR-020

    // =========================================================================
    // Lifecycle (FR-025, FR-026)
    // =========================================================================

    /// @brief Default constructor
    SampleHoldFilter() noexcept;

    /// @brief Destructor
    ~SampleHoldFilter() = default;

    // Non-copyable (contains filter state)
    SampleHoldFilter(const SampleHoldFilter&) = delete;
    SampleHoldFilter& operator=(const SampleHoldFilter&) = delete;

    // Movable
    SampleHoldFilter(SampleHoldFilter&&) noexcept = default;
    SampleHoldFilter& operator=(SampleHoldFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-025)
    /// @param sampleRate Audio sample rate in Hz (44100-192000)
    /// @pre sampleRate >= 1000.0
    /// @note NOT real-time safe (may initialize state)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all state while preserving configuration (FR-026)
    /// @post Held values initialized to base parameters (baseCutoff, baseQ=0.707, pan=0)
    /// @post Random state restored to saved seed
    /// @post Filter works immediately without requiring first trigger
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-021, FR-022, FR-023, FR-024)
    // =========================================================================

    /// @brief Process a single mono sample (FR-021)
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Real-time safe (noexcept, no allocations)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a stereo sample pair in-place (FR-022)
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    /// @note Real-time safe (noexcept, no allocations)
    void processStereo(float& left, float& right) noexcept;

    /// @brief Process a block of mono samples in-place (FR-023)
    /// @param buffer Audio samples (modified in-place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocations)
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a block of stereo samples in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocations)
    void processBlockStereo(float* left, float* right, size_t numSamples) noexcept;

    // =========================================================================
    // Trigger Configuration (FR-001 to FR-005)
    // =========================================================================

    /// @brief Set trigger source mode (FR-001)
    /// @param source Trigger mode (Clock, Audio, Random)
    /// @note Mode switch takes effect at first sample of next buffer (sample-accurate)
    void setTriggerSource(TriggerSource source) noexcept;

    /// @brief Get current trigger source mode
    [[nodiscard]] TriggerSource getTriggerSource() const noexcept;

    /// @brief Set hold time in milliseconds (FR-002)
    /// @param ms Hold time [0.1, 10000]
    void setHoldTime(float ms) noexcept;

    /// @brief Get current hold time in milliseconds
    [[nodiscard]] float getHoldTime() const noexcept;

    /// @brief Set audio trigger threshold (FR-004)
    /// @param threshold Normalized threshold [0, 1] mapping to [-60dB, 0dB]
    /// @note Uses EnvelopeFollower in DetectionMode::Peak with attack=0.1ms, release=50ms
    void setTransientThreshold(float threshold) noexcept;

    /// @brief Get audio trigger threshold
    [[nodiscard]] float getTransientThreshold() const noexcept;

    /// @brief Set random trigger probability (FR-005)
    /// @param probability Trigger probability [0, 1]
    void setTriggerProbability(float probability) noexcept;

    /// @brief Get random trigger probability
    [[nodiscard]] float getTriggerProbability() const noexcept;

    // =========================================================================
    // Sample Source Configuration (FR-006 to FR-010)
    // =========================================================================

    /// @brief Set LFO rate for LFO source (FR-007)
    /// @param hz Rate in Hz [0.01, 20]
    void setLFORate(float hz) noexcept;

    /// @brief Get LFO rate
    [[nodiscard]] float getLFORate() const noexcept;

    /// @brief Set external value for External source (FR-010)
    /// @param value Normalized value [0, 1]
    void setExternalValue(float value) noexcept;

    /// @brief Get external value
    [[nodiscard]] float getExternalValue() const noexcept;

    // =========================================================================
    // Cutoff Parameter Configuration (FR-011, FR-014)
    // =========================================================================

    /// @brief Enable/disable cutoff sampling (FR-014)
    void setCutoffSamplingEnabled(bool enabled) noexcept;

    /// @brief Check if cutoff sampling is enabled
    [[nodiscard]] bool isCutoffSamplingEnabled() const noexcept;

    /// @brief Set cutoff sample source (FR-014)
    void setCutoffSource(SampleSource source) noexcept;

    /// @brief Get cutoff sample source
    [[nodiscard]] SampleSource getCutoffSource() const noexcept;

    /// @brief Set cutoff modulation range in octaves (FR-011)
    /// @param octaves Range [0, 8]
    void setCutoffOctaveRange(float octaves) noexcept;

    /// @brief Get cutoff modulation range
    [[nodiscard]] float getCutoffOctaveRange() const noexcept;

    // =========================================================================
    // Q Parameter Configuration (FR-012, FR-014)
    // =========================================================================

    /// @brief Enable/disable Q sampling (FR-014)
    void setQSamplingEnabled(bool enabled) noexcept;

    /// @brief Check if Q sampling is enabled
    [[nodiscard]] bool isQSamplingEnabled() const noexcept;

    /// @brief Set Q sample source (FR-014)
    void setQSource(SampleSource source) noexcept;

    /// @brief Get Q sample source
    [[nodiscard]] SampleSource getQSource() const noexcept;

    /// @brief Set Q modulation range (FR-012)
    /// @param range Normalized range [0, 1]
    void setQRange(float range) noexcept;

    /// @brief Get Q modulation range
    [[nodiscard]] float getQRange() const noexcept;

    // =========================================================================
    // Pan Parameter Configuration (FR-013, FR-014)
    // =========================================================================

    /// @brief Enable/disable pan sampling (FR-014)
    void setPanSamplingEnabled(bool enabled) noexcept;

    /// @brief Check if pan sampling is enabled
    [[nodiscard]] bool isPanSamplingEnabled() const noexcept;

    /// @brief Set pan sample source (FR-014)
    void setPanSource(SampleSource source) noexcept;

    /// @brief Get pan sample source
    [[nodiscard]] SampleSource getPanSource() const noexcept;

    /// @brief Set pan modulation range in octaves (FR-013)
    /// @param octaves Octave offset for L/R cutoff [0, 4]
    /// @note Pan formula: L = base * pow(2, -pan * octaves), R = base * pow(2, +pan * octaves)
    void setPanOctaveRange(float octaves) noexcept;

    /// @brief Get pan modulation range
    [[nodiscard]] float getPanOctaveRange() const noexcept;

    // =========================================================================
    // Slew Configuration (FR-015, FR-016)
    // =========================================================================

    /// @brief Set slew time for sampled value transitions (FR-015)
    /// @param ms Slew time [0, 500]
    /// @note Slew applies ONLY to sampled modulation values; base parameter changes are instant
    void setSlewTime(float ms) noexcept;

    /// @brief Get slew time
    [[nodiscard]] float getSlewTime() const noexcept;

    // =========================================================================
    // Filter Configuration (FR-017 to FR-020)
    // =========================================================================

    /// @brief Set filter mode (FR-018)
    /// @param mode Filter type (Lowpass, Highpass, Bandpass, Notch)
    void setFilterMode(SVFMode mode) noexcept;

    /// @brief Get filter mode
    [[nodiscard]] SVFMode getFilterMode() const noexcept;

    /// @brief Set base cutoff frequency (FR-019)
    /// @param hz Frequency in Hz [20, 20000]
    void setBaseCutoff(float hz) noexcept;

    /// @brief Get base cutoff frequency
    [[nodiscard]] float getBaseCutoff() const noexcept;

    /// @brief Set base Q (resonance) (FR-020)
    /// @param q Q value [0.1, 30]
    void setBaseQ(float q) noexcept;

    /// @brief Get base Q
    [[nodiscard]] float getBaseQ() const noexcept;

    // =========================================================================
    // Reproducibility (FR-027)
    // =========================================================================

    /// @brief Set random seed for deterministic behavior (FR-027)
    /// @param seed Seed value (non-zero)
    void setSeed(uint32_t seed) noexcept;

    /// @brief Get current seed
    [[nodiscard]] uint32_t getSeed() const noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get configured sample rate
    [[nodiscard]] double sampleRate() const noexcept;
};

// Forward declaration for SVFMode (from svf.h)
enum class SVFMode : uint8_t;

} // namespace DSP
} // namespace Krate
