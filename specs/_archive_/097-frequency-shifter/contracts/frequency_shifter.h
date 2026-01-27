// ==============================================================================
// Layer 2: DSP Processor - Frequency Shifter
// ==============================================================================
// API Contract for FrequencyShifter processor.
// This file defines the public interface; implementation details may vary.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (feedback soft limiting, denormal flushing)
// - Principle XII: Test-First Development
//
// Reference: specs/097-frequency-shifter/spec.md
// ==============================================================================

#pragma once

#include <cstdint>

// Forward declarations for dependencies (actual includes in implementation)
// #include <krate/dsp/primitives/hilbert_transform.h>
// #include <krate/dsp/primitives/lfo.h>
// #include <krate/dsp/primitives/smoother.h>
// #include <krate/dsp/core/db_utils.h>
// #include <krate/dsp/core/math_constants.h>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Shift direction for single-sideband modulation
///
/// Determines which sideband(s) appear in the output:
/// - Up: Upper sideband only (input frequency + shift amount)
/// - Down: Lower sideband only (input frequency - shift amount)
/// - Both: Both sidebands (ring modulation effect)
///
/// @par Formulas
/// Given I (in-phase) and Q (quadrature) from Hilbert transform,
/// and carrier cos(wt), sin(wt):
/// - Up: output = I*cos(wt) - Q*sin(wt)
/// - Down: output = I*cos(wt) + Q*sin(wt)
/// - Both: output = 0.5*(up + down) = I*cos(wt)
enum class ShiftDirection : uint8_t {
    Up = 0,    ///< Upper sideband only (input + shift)
    Down,      ///< Lower sideband only (input - shift)
    Both       ///< Both sidebands (ring modulation)
};

// =============================================================================
// FrequencyShifter Class
// =============================================================================

/// @brief Frequency shifter using Hilbert transform for SSB modulation.
///
/// Shifts all frequencies by a constant Hz amount (not pitch shifting).
/// Unlike pitch shifting which preserves harmonic relationships, frequency
/// shifting adds/subtracts a fixed Hz value, creating inharmonic, metallic
/// textures. Based on the Bode frequency shifter principle.
///
/// @par Algorithm
/// 1. Generate analytic signal using Hilbert transform (I + jQ)
/// 2. Multiply by complex exponential carrier (cos(wt) + j*sin(wt))
/// 3. Take real part for desired sideband
///
/// @par Features
/// - Three direction modes: Up (upper sideband), Down (lower), Both (ring mod)
/// - LFO modulation of shift amount for evolving effects
/// - Feedback path with tanh saturation for spiraling (Shepard-tone) effects
/// - Stereo mode: left = +shift, right = -shift for width
/// - Dry/wet mix control
/// - Click-free parameter smoothing
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
/// Safe for audio callbacks.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances per audio channel or use
/// processStereo() for stereo processing on the same thread.
///
/// @par Layer
/// Layer 2 (Processor) - depends on Layer 0 (core) and Layer 1 (primitives)
///
/// @par Latency
/// Fixed 5-sample latency from Hilbert transform. Not compensated in output.
///
/// @par Aliasing
/// Frequency shifting is linear; aliasing occurs only when shifted frequencies
/// exceed Nyquist. No oversampling at Layer 2 to maintain CPU budget.
class FrequencyShifter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum shift amount in Hz (positive or negative)
    static constexpr float kMaxShiftHz = 5000.0f;

    /// Maximum modulation depth in Hz
    static constexpr float kMaxModDepthHz = 500.0f;

    /// Maximum feedback amount (0.99 to prevent infinite sustain)
    static constexpr float kMaxFeedback = 0.99f;

    /// Minimum LFO modulation rate in Hz
    static constexpr float kMinModRate = 0.01f;

    /// Maximum LFO modulation rate in Hz
    static constexpr float kMaxModRate = 20.0f;

    /// Oscillator renormalization interval (samples)
    static constexpr int kRenormInterval = 1024;

    // =========================================================================
    // Lifecycle (FR-001, FR-002)
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared processor. Call prepare() before processing.
    /// Processing before prepare() returns input unchanged.
    FrequencyShifter() noexcept = default;

    /// @brief Destructor
    ~FrequencyShifter() = default;

    // Non-copyable due to LFO containing vectors
    FrequencyShifter(const FrequencyShifter&) = delete;
    FrequencyShifter& operator=(const FrequencyShifter&) = delete;
    FrequencyShifter(FrequencyShifter&&) noexcept = default;
    FrequencyShifter& operator=(FrequencyShifter&&) noexcept = default;

    /// @brief Initialize for given sample rate (FR-001)
    ///
    /// Prepares the Hilbert transform, LFO, and smoothers.
    /// Must be called before processing. Call again if sample rate changes.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @note NOT real-time safe (LFO allocates wavetables)
    void prepare(double sampleRate) noexcept;

    /// @brief Clear all internal state (FR-002)
    ///
    /// Resets Hilbert transform, oscillator phase, and feedback sample.
    /// Does not change parameter values or sample rate.
    void reset() noexcept;

    // =========================================================================
    // Shift Control (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Set the base frequency shift amount (FR-004, FR-005)
    ///
    /// @param hz Shift amount in Hz, clamped to [-5000, +5000]
    ///           Typical musical range: -1000 to +1000
    void setShiftAmount(float hz) noexcept;

    /// @brief Set the shift direction (FR-006)
    ///
    /// @param dir Shift direction mode:
    ///            - Up: Upper sideband (input + shift)
    ///            - Down: Lower sideband (input - shift)
    ///            - Both: Ring modulation (both sidebands)
    void setDirection(ShiftDirection dir) noexcept;

    // =========================================================================
    // LFO Modulation (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set LFO modulation rate (FR-011)
    ///
    /// @param hz LFO frequency in Hz, clamped to [0.01, 20]
    void setModRate(float hz) noexcept;

    /// @brief Set LFO modulation depth (FR-012)
    ///
    /// Effective shift = baseShift + modDepth * lfoValue
    /// where lfoValue is in [-1, +1]
    ///
    /// @param hz Modulation range in Hz, clamped to [0, 500]
    void setModDepth(float hz) noexcept;

    // =========================================================================
    // Feedback (FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Set feedback amount for spiraling effects (FR-014)
    ///
    /// Feedback creates Shepard-tone-like spiraling where frequencies
    /// continue shifting through successive passes.
    ///
    /// @param amount Feedback level, clamped to [0.0, 0.99]
    /// @note Feedback is soft-limited with tanh() to prevent runaway (FR-015)
    void setFeedback(float amount) noexcept;

    // =========================================================================
    // Mix (FR-017, FR-018)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-017)
    ///
    /// output = (1-mix)*dry + mix*wet
    ///
    /// @param dryWet Mix amount, clamped to [0.0, 1.0]
    ///               0.0 = dry only (bypass), 1.0 = wet only
    void setMix(float dryWet) noexcept;

    // =========================================================================
    // Processing (FR-019, FR-020, FR-021, FR-022)
    // =========================================================================

    /// @brief Process a single mono sample (FR-019)
    ///
    /// @param input Input sample
    /// @return Frequency-shifted output sample (with mix applied)
    ///
    /// @note Returns input unchanged if prepare() not called
    /// @note Returns 0 and resets on NaN/Inf input (FR-023)
    /// @note noexcept, allocation-free (FR-022)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process stereo with opposite shifts per channel (FR-020, FR-021)
    ///
    /// Left channel receives positive shift (+shiftHz).
    /// Right channel receives negative shift (-shiftHz).
    ///
    /// @param left Left channel sample (in/out)
    /// @param right Right channel sample (in/out)
    ///
    /// @note noexcept, allocation-free (FR-022)
    void processStereo(float& left, float& right) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get current shift amount in Hz
    [[nodiscard]] float getShiftAmount() const noexcept;

    /// @brief Get current shift direction
    [[nodiscard]] ShiftDirection getDirection() const noexcept;

    /// @brief Get current LFO modulation rate in Hz
    [[nodiscard]] float getModRate() const noexcept;

    /// @brief Get current LFO modulation depth in Hz
    [[nodiscard]] float getModDepth() const noexcept;

    /// @brief Get current feedback amount
    [[nodiscard]] float getFeedback() const noexcept;

    /// @brief Get current dry/wet mix
    [[nodiscard]] float getMix() const noexcept;

private:
    // ==========================================================================
    // Private Members (intentionally omitted from contract)
    // ==========================================================================
    // This contract defines only the PUBLIC interface.
    // Private members are documented in data-model.md and may vary in the
    // actual implementation as long as the public contract is satisfied.
    // See: specs/097-frequency-shifter/data-model.md
};

} // namespace DSP
} // namespace Krate
