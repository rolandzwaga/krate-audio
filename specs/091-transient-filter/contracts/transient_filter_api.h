// ==============================================================================
// Layer 2: DSP Processor - Transient-Aware Filter
// API Contract (Header-Only Interface Definition)
// ==============================================================================
// This file defines the API contract for TransientAwareFilter.
// It serves as documentation and compile-time interface verification.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layer 0/1 and peer Layer 2)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XIII: Test-First Development
//
// Reference: specs/091-transient-filter/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// TransientFilterMode Enumeration (FR-014)
// =============================================================================

/// @brief Filter response type selection for TransientAwareFilter
///
/// Determines the filter type used for audio processing. Maps to SVFMode
/// internally for modulation-stable filtering.
enum class TransientFilterMode : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass response
    Bandpass = 1,  ///< Constant 0 dB peak bandpass response
    Highpass = 2   ///< 12 dB/oct highpass response
};

// =============================================================================
// TransientAwareFilter Class API
// =============================================================================

/// @brief Layer 2 DSP Processor - Transient-aware dynamic filter
///
/// Detects transients using dual envelope follower comparison (fast/slow) and
/// modulates filter cutoff and/or resonance in response. Unlike EnvelopeFilter
/// which follows overall amplitude, this responds only to sudden level changes
/// (attacks), creating dynamic percussive tonal shaping.
///
/// @par Key Features
/// - Dual envelope transient detection (1ms fast, 50ms slow) (FR-005, FR-006)
/// - Level-independent detection via normalization (FR-001)
/// - Configurable sensitivity threshold (FR-002)
/// - Exponential attack/decay response curves (FR-003, FR-004)
/// - Log-space frequency interpolation for perceptual sweeps (FR-009)
/// - Resonance boost during transients (FR-012)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (composes EnvelopeFollower, SVF, OnePoleSmoother)
///
/// @par Usage Example
/// @code
/// TransientAwareFilter filter;
/// filter.prepare(48000.0);
/// filter.setIdleCutoff(200.0f);
/// filter.setTransientCutoff(4000.0f);
/// filter.setSensitivity(0.5f);
///
/// // In process callback
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
/// @endcode
class TransientAwareFilter {
public:
    // =========================================================================
    // Constants (from spec FR-xxx)
    // =========================================================================

    /// @brief Fast envelope attack time in ms (FR-005)
    static constexpr float kFastEnvelopeAttackMs = 1.0f;

    /// @brief Fast envelope release time in ms (FR-005)
    static constexpr float kFastEnvelopeReleaseMs = 1.0f;

    /// @brief Slow envelope attack time in ms (FR-006)
    static constexpr float kSlowEnvelopeAttackMs = 50.0f;

    /// @brief Slow envelope release time in ms (FR-006)
    static constexpr float kSlowEnvelopeReleaseMs = 50.0f;

    /// @brief Minimum sensitivity value (FR-002)
    static constexpr float kMinSensitivity = 0.0f;

    /// @brief Maximum sensitivity value (FR-002)
    static constexpr float kMaxSensitivity = 1.0f;

    /// @brief Minimum transient attack time in ms (FR-003)
    static constexpr float kMinAttackMs = 0.1f;

    /// @brief Maximum transient attack time in ms (FR-003)
    static constexpr float kMaxAttackMs = 50.0f;

    /// @brief Minimum transient decay time in ms (FR-004)
    static constexpr float kMinDecayMs = 1.0f;

    /// @brief Maximum transient decay time in ms (FR-004)
    static constexpr float kMaxDecayMs = 1000.0f;

    /// @brief Minimum cutoff frequency in Hz (FR-007, FR-008)
    static constexpr float kMinCutoffHz = 20.0f;

    /// @brief Minimum resonance (Q) value (FR-011)
    static constexpr float kMinResonance = 0.5f;

    /// @brief Maximum resonance (Q) value for idle resonance parameter (FR-011)
    static constexpr float kMaxResonance = 20.0f;

    /// @brief Maximum total resonance (idle + boost) for stability (FR-013)
    /// @note Total is higher than individual max because idle Q (max 20) + Q boost (max 20) = 40,
    ///       but we clamp the combined result to 30 for SVF stability. This allows users to set
    ///       high idle Q with moderate boost, or low idle Q with high boost.
    static constexpr float kMaxTotalResonance = 30.0f;

    /// @brief Maximum Q boost value (FR-012)
    static constexpr float kMaxQBoost = 20.0f;

    // =========================================================================
    // Lifecycle (FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Default constructor
    TransientAwareFilter() noexcept = default;

    /// @brief Destructor
    ~TransientAwareFilter() = default;

    // Non-copyable (contains filter state)
    TransientAwareFilter(const TransientAwareFilter&) = delete;
    TransientAwareFilter& operator=(const TransientAwareFilter&) = delete;

    // Movable
    TransientAwareFilter(TransientAwareFilter&&) noexcept = default;
    TransientAwareFilter& operator=(TransientAwareFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-021)
    /// @param sampleRate Audio sample rate in Hz (clamped to >= 1000)
    /// @note Call before any processing; call again if sample rate changes
    void prepare(double sampleRate) noexcept;

    /// @brief Reset internal state without changing parameters (FR-022)
    /// @note Clears envelope and filter state
    void reset() noexcept;

    /// @brief Get processing latency in samples (FR-023)
    /// @return Latency (0 - no lookahead in this processor)
    [[nodiscard]] size_t getLatency() const noexcept;

    // =========================================================================
    // Processing (FR-016, FR-017, FR-018, FR-019, FR-020)
    // =========================================================================

    /// @brief Process a single sample (FR-016)
    /// @param input Input audio sample
    /// @return Filtered output sample
    /// @pre prepare() has been called
    /// @note Returns input unchanged if not prepared
    /// @note Returns 0 and resets state on NaN/Inf input (FR-018)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place (FR-017)
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocations (FR-019, FR-020)
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Transient Detection Parameters (FR-002, FR-003, FR-004)
    // =========================================================================

    /// @brief Set transient detection sensitivity (FR-002)
    /// @param sensitivity Value from 0.0 (none) to 1.0 (all), clamped
    /// @note Controls threshold: higher = more sensitive to transients
    void setSensitivity(float sensitivity) noexcept;

    /// @brief Set transient response attack time (FR-003)
    /// @param ms Attack time in milliseconds, clamped to [0.1, 50]
    /// @note How quickly filter responds to detected transients
    void setTransientAttack(float ms) noexcept;

    /// @brief Set transient response decay time (FR-004)
    /// @param ms Decay time in milliseconds, clamped to [1, 1000]
    /// @note How quickly filter returns to idle state
    void setTransientDecay(float ms) noexcept;

    // =========================================================================
    // Filter Cutoff Parameters (FR-007, FR-008, FR-009, FR-010)
    // =========================================================================

    /// @brief Set idle cutoff frequency (FR-007)
    /// @param hz Cutoff in Hz when no transient is detected
    /// @note Clamped to [20, sampleRate * 0.45]
    void setIdleCutoff(float hz) noexcept;

    /// @brief Set transient cutoff frequency (FR-008)
    /// @param hz Cutoff in Hz at peak transient response
    /// @note Clamped to [20, sampleRate * 0.45]
    /// @note Can be higher OR lower than idle cutoff (FR-010)
    void setTransientCutoff(float hz) noexcept;

    // =========================================================================
    // Filter Resonance Parameters (FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set idle resonance (FR-011)
    /// @param q Q factor when no transient is detected, clamped to [0.5, 20.0]
    void setIdleResonance(float q) noexcept;

    /// @brief Set transient Q boost (FR-012)
    /// @param boost Additional Q during transient, clamped to [0.0, 20.0]
    /// @note Total Q (idle + boost) clamped to 30.0 for stability (FR-013)
    void setTransientQBoost(float boost) noexcept;

    // =========================================================================
    // Filter Configuration (FR-014, FR-015)
    // =========================================================================

    /// @brief Set filter type (FR-014)
    /// @param type Lowpass, Bandpass, or Highpass
    /// @note Uses SVF for modulation stability (FR-015)
    void setFilterType(TransientFilterMode type) noexcept;

    // =========================================================================
    // Monitoring (FR-024, FR-025, FR-026)
    // =========================================================================

    /// @brief Get current filter cutoff frequency (FR-024)
    /// @return Cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept;

    /// @brief Get current filter resonance (FR-025)
    /// @return Current Q value
    [[nodiscard]] float getCurrentResonance() const noexcept;

    /// @brief Get current transient detection level (FR-026)
    /// @return Transient level [0.0, 1.0] for UI visualization
    [[nodiscard]] float getTransientLevel() const noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get current sensitivity setting
    [[nodiscard]] float getSensitivity() const noexcept;

    /// @brief Get current transient attack time
    [[nodiscard]] float getTransientAttack() const noexcept;

    /// @brief Get current transient decay time
    [[nodiscard]] float getTransientDecay() const noexcept;

    /// @brief Get current idle cutoff
    [[nodiscard]] float getIdleCutoff() const noexcept;

    /// @brief Get current transient cutoff
    [[nodiscard]] float getTransientCutoff() const noexcept;

    /// @brief Get current idle resonance
    [[nodiscard]] float getIdleResonance() const noexcept;

    /// @brief Get current transient Q boost
    [[nodiscard]] float getTransientQBoost() const noexcept;

    /// @brief Get current filter type
    [[nodiscard]] TransientFilterMode getFilterType() const noexcept;
};

} // namespace DSP
} // namespace Krate
