// ==============================================================================
// Layer 2: DSP Processor - FuzzProcessor API Contract
// ==============================================================================
// Fuzz Face style distortion with Germanium and Silicon transistor types.
//
// Feature: 063-fuzz-processor
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: core/db_utils.h, core/sigmoid.h, core/crossfade_utils.h
//   - Layer 1: primitives/waveshaper.h, primitives/biquad.h,
//              primitives/dc_blocker.h, primitives/smoother.h
//   - stdlib: <cstddef>, <cstdint>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.5% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/063-fuzz-processor/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// FuzzType Enumeration (FR-001)
// =============================================================================

/// @brief Transistor type selection for fuzz character
///
/// Each type has distinct harmonic characteristics:
/// - Germanium: Warm, saggy response with softer clipping and even harmonics
/// - Silicon: Brighter, tighter response with harder clipping and odd harmonics
enum class FuzzType : uint8_t {
    Germanium = 0,  ///< Warm, saggy, even harmonics, soft clipping
    Silicon = 1     ///< Bright, tight, odd harmonics, hard clipping
};

// =============================================================================
// FuzzProcessor Class (FR-002 to FR-053)
// =============================================================================

/// @brief Fuzz Face style distortion processor with dual transistor types
///
/// Provides classic fuzz pedal emulation with configurable transistor type
/// (Germanium/Silicon), bias control for "dying battery" effects, tone
/// filtering, and optional octave-up mode.
///
/// @par Signal Chain
/// Input -> [Octave-Up (optional)] -> [Drive Stage] -> [Type-Specific Saturation]
/// -> [Bias Gating] -> [DC Blocker] -> [Tone Filter] -> [Volume] -> Output
///
/// @par Features
/// - Dual transistor types: Germanium (warm, saggy) and Silicon (bright, tight)
/// - Germanium "sag" via envelope-modulated clipping threshold
/// - Bias control for gating effects (0=dying battery, 1=normal)
/// - Tone control (400Hz-8000Hz low-pass filter)
/// - Octave-up mode via self-modulation
/// - 5ms crossfade between types for click-free switching
/// - 5ms parameter smoothing on all controls
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
/// - Principle X: DSP Constraints (DC blocking after saturation)
/// - Principle XI: Performance Budget (< 0.5% CPU per instance)
///
/// @par Usage Example
/// @code
/// FuzzProcessor fuzz;
/// fuzz.prepare(44100.0, 512);
/// fuzz.setFuzzType(FuzzType::Germanium);
/// fuzz.setFuzz(0.7f);
/// fuzz.setBias(0.8f);
/// fuzz.setTone(0.5f);
/// fuzz.setVolume(0.0f);
///
/// // Process audio blocks
/// fuzz.process(buffer, numSamples);
/// @endcode
///
/// @see specs/063-fuzz-processor/spec.md
class FuzzProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Default fuzz amount (moderate saturation)
    static constexpr float kDefaultFuzz = 0.5f;

    /// Default output volume in dB (unity)
    static constexpr float kDefaultVolumeDb = 0.0f;

    /// Default bias (slight gating, near normal operation)
    static constexpr float kDefaultBias = 0.7f;

    /// Default tone (neutral)
    static constexpr float kDefaultTone = 0.5f;

    /// Minimum output volume in dB
    static constexpr float kMinVolumeDb = -24.0f;

    /// Maximum output volume in dB
    static constexpr float kMaxVolumeDb = +24.0f;

    /// Parameter smoothing time in milliseconds
    static constexpr float kSmoothingTimeMs = 5.0f;

    /// Type crossfade time in milliseconds
    static constexpr float kCrossfadeTimeMs = 5.0f;

    /// DC blocker cutoff frequency in Hz
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    /// Tone filter minimum frequency in Hz (dark)
    static constexpr float kToneMinHz = 400.0f;

    /// Tone filter maximum frequency in Hz (bright)
    static constexpr float kToneMaxHz = 8000.0f;

    /// Germanium sag envelope attack time in milliseconds
    static constexpr float kSagAttackMs = 1.0f;

    /// Germanium sag envelope release time in milliseconds
    static constexpr float kSagReleaseMs = 100.0f;

    // =========================================================================
    // Lifecycle (FR-002 to FR-005)
    // =========================================================================

    /// @brief Default constructor with safe defaults (FR-005)
    ///
    /// Initializes with:
    /// - Type: Germanium
    /// - Fuzz: 0.5 (moderate saturation)
    /// - Volume: 0 dB (unity)
    /// - Bias: 0.7 (slight gating)
    /// - Tone: 0.5 (neutral)
    /// - OctaveUp: false
    FuzzProcessor() noexcept;

    /// @brief Configure the processor for the given sample rate (FR-002)
    ///
    /// Configures internal components (Waveshapers, filters, smoothers)
    /// for the specified sample rate. Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum block size in samples (unused, for future use)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation (FR-003)
    ///
    /// Clears filter states and snaps smoothers to current target values.
    /// Call when starting a new audio stream or after discontinuity.
    ///
    /// @note Smoothers snap to targets per FR-040
    void reset() noexcept;

    // =========================================================================
    // Type Selection (FR-006, FR-006a, FR-011)
    // =========================================================================

    /// @brief Set the transistor type (FR-006)
    ///
    /// Selects between Germanium (warm, saggy) and Silicon (bright, tight).
    /// When changed during processing, triggers 5ms equal-power crossfade
    /// between type outputs (FR-006a).
    ///
    /// @param type Transistor type to use
    void setFuzzType(FuzzType type) noexcept;

    /// @brief Get the current transistor type (FR-011)
    /// @return Current transistor type
    [[nodiscard]] FuzzType getFuzzType() const noexcept;

    // =========================================================================
    // Parameter Setters (FR-007 to FR-010)
    // =========================================================================

    /// @brief Set the fuzz/saturation amount (FR-007)
    ///
    /// Controls the intensity of the fuzz effect.
    /// - 0.0: Minimal distortion, near-clean pass-through
    /// - 0.5: Moderate saturation (default)
    /// - 1.0: Maximum saturation, heavily distorted
    ///
    /// @param amount Fuzz amount, clamped to [0.0, 1.0]
    void setFuzz(float amount) noexcept;

    /// @brief Set the output volume in dB (FR-008)
    ///
    /// Post-fuzz gain adjustment for level matching.
    ///
    /// @param dB Output volume in decibels, clamped to [-24, +24]
    void setVolume(float dB) noexcept;

    /// @brief Set the transistor bias (FR-009)
    ///
    /// Controls the transistor operating point, affecting gating behavior.
    /// - 0.0: Maximum gating (dying battery effect)
    /// - 0.7: Slight gating (default)
    /// - 1.0: No gating, full sustain
    ///
    /// @param bias Bias amount, clamped to [0.0, 1.0]
    void setBias(float bias) noexcept;

    /// @brief Set the tone control (FR-010)
    ///
    /// Controls the low-pass filter cutoff frequency.
    /// - 0.0: 400 Hz (dark/muffled)
    /// - 0.5: 4200 Hz (neutral)
    /// - 1.0: 8000 Hz (bright/open)
    ///
    /// @param tone Tone amount, clamped to [0.0, 1.0]
    void setTone(float tone) noexcept;

    // =========================================================================
    // Octave-Up (FR-050 to FR-053)
    // =========================================================================

    /// @brief Enable or disable octave-up effect (FR-050)
    ///
    /// When enabled, applies self-modulation (input * |input|) before
    /// the main fuzz stage, creating an octave-up effect.
    ///
    /// @param enabled true to enable octave-up, false to disable
    void setOctaveUp(bool enabled) noexcept;

    /// @brief Get the octave-up state (FR-051)
    /// @return true if octave-up is enabled
    [[nodiscard]] bool getOctaveUp() const noexcept;

    // =========================================================================
    // Parameter Getters (FR-012 to FR-015)
    // =========================================================================

    /// @brief Get the current fuzz amount (FR-012)
    /// @return Fuzz amount [0.0, 1.0]
    [[nodiscard]] float getFuzz() const noexcept;

    /// @brief Get the current output volume in dB (FR-013)
    /// @return Volume in decibels [-24, +24]
    [[nodiscard]] float getVolume() const noexcept;

    /// @brief Get the current bias value (FR-014)
    /// @return Bias amount [0.0, 1.0]
    [[nodiscard]] float getBias() const noexcept;

    /// @brief Get the current tone value (FR-015)
    /// @return Tone amount [0.0, 1.0]
    [[nodiscard]] float getTone() const noexcept;

    // =========================================================================
    // Processing (FR-030 to FR-032)
    // =========================================================================

    /// @brief Process a block of audio samples in-place (FR-030)
    ///
    /// Applies the fuzz effect with the current parameter settings.
    /// Before prepare() is called, returns input unchanged (FR-004).
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call (FR-031)
    /// @note numSamples=0 is handled gracefully as no-op (FR-032)
    void process(float* buffer, size_t numSamples) noexcept;
};

} // namespace DSP
} // namespace Krate
