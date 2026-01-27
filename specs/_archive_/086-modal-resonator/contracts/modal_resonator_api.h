// ==============================================================================
// API Contract: Modal Resonator
// ==============================================================================
// This file defines the public API contract for the ModalResonator class.
// Implementation will be in dsp/include/krate/dsp/processors/modal_resonator.h
//
// Feature: 086-modal-resonator
// Layer: 2 (DSP Processor)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Maximum number of modes in the resonator (FR-001)
inline constexpr size_t kMaxModes = 32;

/// Minimum mode frequency in Hz
inline constexpr float kMinModeFrequency = 20.0f;

/// Maximum mode frequency ratio (relative to sample rate)
inline constexpr float kMaxModeFrequencyRatio = 0.45f;

/// Minimum decay time in seconds (FR-006 edge case)
inline constexpr float kMinModeDecay = 0.001f;

/// Maximum decay time in seconds
inline constexpr float kMaxModeDecay = 30.0f;

/// Minimum size scaling factor (FR-014)
inline constexpr float kMinSizeScale = 0.1f;

/// Maximum size scaling factor (FR-014)
inline constexpr float kMaxSizeScale = 10.0f;

/// Default parameter smoothing time in milliseconds (FR-031)
inline constexpr float kDefaultModalSmoothingTimeMs = 20.0f;

/// Base frequency for material presets (A4)
inline constexpr float kModalBaseFrequency = 440.0f;

// =============================================================================
// Data Structures
// =============================================================================

/// @brief Mode configuration data for bulk import (FR-008).
/// @see setModes()
struct ModalData {
    float frequency;   ///< Mode frequency in Hz [20, sampleRate * 0.45]
    float t60;         ///< Decay time in seconds (RT60) [0.001, 30.0]
    float amplitude;   ///< Mode amplitude [0.0, 1.0]
};

/// @brief Material presets for frequency-dependent decay (FR-009).
/// @see setMaterial()
enum class Material : uint8_t {
    Wood,     ///< Warm, quick HF decay (marimba-like)
    Metal,    ///< Bright, sustained (bell-like)
    Glass,    ///< Bright, ringing (glass bowl-like)
    Ceramic,  ///< Warm/bright, medium decay (tile-like)
    Nylon     ///< Dull, heavily damped (damped string-like)
};

// =============================================================================
// ModalResonator Class
// =============================================================================

/// @brief Modal resonator modeling vibrating bodies as decaying sinusoidal modes.
///
/// Implements up to 32 parallel modes using the impulse-invariant transform of
/// a two-pole complex resonator. Each mode has independent frequency, decay
/// (T60), and amplitude parameters.
///
/// @par Key Features
/// - 32 parallel modes (FR-001)
/// - Two-pole sinusoidal oscillator topology (FR-002, FR-003)
/// - Material presets with frequency-dependent decay (FR-009, FR-011)
/// - Size and damping global controls (FR-013, FR-015)
/// - Strike excitation with energy accumulation (FR-017, FR-019)
/// - Parameter smoothing for click-free changes (FR-030)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
/// - Principle III: Modern C++ (constexpr, RAII, value semantics)
/// - Principle IX: Layer 2 (depends only on Layers 0-1)
/// - Principle XII: Test-First Development
///
/// @par Usage Example
/// @code
/// ModalResonator resonator;
/// resonator.prepare(44100.0);
/// resonator.setMaterial(Material::Metal);
///
/// // Strike to excite
/// resonator.strike(1.0f);
///
/// // Process audio
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = resonator.process(0.0f);
/// }
/// @endcode
///
/// @see specs/086-modal-resonator/spec.md
class ModalResonator {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Construct a modal resonator.
    /// @param smoothingTimeMs Parameter smoothing time in ms (FR-031)
    /// @note Default smoothing time is 20ms.
    explicit ModalResonator(float smoothingTimeMs = kDefaultModalSmoothingTimeMs) noexcept;

    /// @brief Destructor.
    ~ModalResonator() = default;

    // Non-copyable, movable
    ModalResonator(const ModalResonator&) = delete;
    ModalResonator& operator=(const ModalResonator&) = delete;
    ModalResonator(ModalResonator&&) noexcept = default;
    ModalResonator& operator=(ModalResonator&&) noexcept = default;

    /// @brief Initialize the resonator for processing (FR-024).
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @note Must be called before process() or strike().
    /// @note Recalculates all mode coefficients for new sample rate.
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all oscillator states to silence (FR-025).
    /// @note Parameters remain unchanged; only state is cleared.
    /// @note No memory allocation (FR-028).
    void reset() noexcept;

    // =========================================================================
    // Per-Mode Control (FR-005, FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set frequency for a specific mode (FR-005).
    /// @param index Mode index [0, kMaxModes-1]
    /// @param hz Frequency in Hz [20, sampleRate * 0.45]
    /// @note Frequency is clamped to valid range.
    /// @note Uses smoothing to prevent clicks (FR-030).
    void setModeFrequency(int index, float hz) noexcept;

    /// @brief Set decay time (T60) for a specific mode (FR-006).
    /// @param index Mode index [0, kMaxModes-1]
    /// @param t60Seconds Decay time in seconds [0.001, 30.0]
    /// @note Decay is clamped to valid range.
    void setModeDecay(int index, float t60Seconds) noexcept;

    /// @brief Set amplitude for a specific mode (FR-007).
    /// @param index Mode index [0, kMaxModes-1]
    /// @param amplitude Amplitude [0.0, 1.0]
    /// @note Amplitude is clamped to valid range.
    /// @note Uses smoothing to prevent clicks (FR-030).
    void setModeAmplitude(int index, float amplitude) noexcept;

    /// @brief Bulk-configure modes from analysis data (FR-008).
    /// @param modes Array of ModalData structures
    /// @param count Number of modes to configure (excess > 32 ignored)
    /// @note Disables modes beyond count.
    void setModes(const ModalData* modes, int count) noexcept;

    // =========================================================================
    // Material Presets (FR-009, FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Select a material preset (FR-009).
    /// @param mat Material type
    /// @note Configures frequency ratios and frequency-dependent decay (FR-010, FR-011).
    /// @note Presets can be further customized (FR-012).
    void setMaterial(Material mat) noexcept;

    // =========================================================================
    // Global Controls (FR-013, FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Set size scaling factor (FR-013).
    /// @param scale Size multiplier [0.1, 10.0]
    /// @note Size 2.0 = frequencies halved (larger object, lower pitch).
    /// @note Size 0.5 = frequencies doubled (smaller object, higher pitch).
    /// @note Clamped to valid range (FR-014).
    void setSize(float scale) noexcept;

    /// @brief Set global damping (FR-015).
    /// @param amount Damping amount [0.0, 1.0]
    /// @note Damping 0.0 = no change (full decay).
    /// @note Damping 1.0 = instant silence.
    /// @note Applied multiplicatively: effective_T60 = base_T60 * (1 - damping) (FR-016).
    void setDamping(float amount) noexcept;

    // =========================================================================
    // Strike/Excitation (FR-017, FR-018, FR-019, FR-020)
    // =========================================================================

    /// @brief Excite all modes with an impulse (FR-017).
    /// @param velocity Excitation strength [0.0, 1.0] (FR-018)
    /// @note Velocity scales the excitation amplitude.
    /// @note Energy is added to existing state (accumulative) (FR-019).
    /// @note Output appears on next process() call (FR-020).
    void strike(float velocity = 1.0f) noexcept;

    // =========================================================================
    // Processing (FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Process a single sample (FR-021).
    /// @param input Input sample (excites all modes) (FR-023)
    /// @return Sum of all mode outputs
    /// @note Returns 0.0f if prepare() not called (FR-026).
    /// @note NaN/Inf input causes reset and returns 0.0f (FR-032).
    /// @note All processing is noexcept (FR-027).
    /// @note No memory allocation (FR-028).
    /// @note Denormals are flushed (FR-029).
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place (FR-022).
    /// @param buffer Audio buffer (input, modified to output)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, int numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if the resonator has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get the number of active (enabled) modes.
    /// @return Number of enabled modes [0, kMaxModes]
    [[nodiscard]] int getNumActiveModes() const noexcept;

    /// @brief Get the frequency of a specific mode.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return Frequency in Hz, or 0.0f if index invalid
    [[nodiscard]] float getModeFrequency(int index) const noexcept;

    /// @brief Get the decay time of a specific mode.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return Decay time in seconds, or 0.0f if index invalid
    [[nodiscard]] float getModeDecay(int index) const noexcept;

    /// @brief Get the amplitude of a specific mode.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return Amplitude [0.0, 1.0], or 0.0f if index invalid
    [[nodiscard]] float getModeAmplitude(int index) const noexcept;

    /// @brief Check if a specific mode is enabled.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return true if mode is enabled, false if disabled or index invalid
    [[nodiscard]] bool isModeEnabled(int index) const noexcept;

    /// @brief Get the current size scaling factor.
    /// @return Size multiplier [0.1, 10.0]
    [[nodiscard]] float getSize() const noexcept;

    /// @brief Get the current damping amount.
    /// @return Damping [0.0, 1.0]
    [[nodiscard]] float getDamping() const noexcept;

private:
    // Implementation details hidden from contract
    // See modal_resonator.h for actual implementation
};

} // namespace DSP
} // namespace Krate
