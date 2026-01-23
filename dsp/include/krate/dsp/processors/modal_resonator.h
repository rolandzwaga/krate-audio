// ==============================================================================
// Layer 2: DSP Processor - Modal Resonator
// ==============================================================================
// Models vibrating bodies as a sum of decaying sinusoidal modes for physically
// accurate resonance of complex bodies like bells, bars, and plates.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layers 0-1)
// - Principle X: DSP Constraints (sample-accurate processing)
// - Principle XII: Test-First Development
//
// Reference: specs/086-modal-resonator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
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

/// ln(1000) - used for T60 to time constant conversion
/// T60 = 6.91 * tau where tau is the time constant
inline constexpr float kModalLn1000 = 6.907755278982137f;

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

/// @brief Coefficients for frequency-dependent decay model.
/// @note R_k = b1 + b3 * f_k^2, T60_k = 6.91 / R_k
struct MaterialCoefficients {
    float b1;                           ///< Global damping (Hz)
    float b3;                           ///< Frequency-dependent damping (s)
    std::array<float, 8> ratios;        ///< Mode frequency multipliers
    int numModes;                       ///< Number of active modes for preset
};

/// Material preset coefficients from research.md
inline constexpr MaterialCoefficients kMaterialPresets[] = {
    // Wood: warm, quick HF decay
    { 2.0f, 1.0e-7f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Metal: bright, sustained
    { 0.3f, 1.0e-9f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Glass: bright, ringing
    { 0.5f, 5.0e-8f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Ceramic: warm/bright, medium
    { 1.5f, 8.0e-8f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 },
    // Nylon: dull, heavily damped
    { 4.0f, 2.0e-7f, {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}, 8 }
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
    explicit ModalResonator(float smoothingTimeMs = kDefaultModalSmoothingTimeMs) noexcept
        : smoothingTimeMs_(smoothingTimeMs)
    {
        // Initialize all arrays to default values
        for (size_t i = 0; i < kMaxModes; ++i) {
            y1_[i] = 0.0f;
            y2_[i] = 0.0f;
            a1_[i] = 0.0f;
            a2_[i] = 0.0f;
            gains_[i] = 0.0f;
            frequencies_[i] = kModalBaseFrequency;
            t60s_[i] = 1.0f;
            enabled_[i] = false;
        }
    }

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
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Configure smoothers
        const float sampleRateF = static_cast<float>(sampleRate);
        for (size_t i = 0; i < kMaxModes; ++i) {
            frequencySmooth_[i].configure(smoothingTimeMs_, sampleRateF);
            amplitudeSmooth_[i].configure(smoothingTimeMs_, sampleRateF);

            // Snap smoothers to initial values
            frequencySmooth_[i].snapTo(frequencies_[i]);
            amplitudeSmooth_[i].snapTo(gains_[i]);
        }

        // Recalculate coefficients for all modes
        for (size_t i = 0; i < kMaxModes; ++i) {
            if (enabled_[i]) {
                calculateModeCoefficients(i);
            }
        }

        prepared_ = true;
    }

    /// @brief Reset all oscillator states to silence (FR-025).
    /// @note Parameters remain unchanged; only state is cleared.
    /// @note No memory allocation (FR-028).
    void reset() noexcept {
        // Clear oscillator states
        for (size_t i = 0; i < kMaxModes; ++i) {
            y1_[i] = 0.0f;
            y2_[i] = 0.0f;
        }

        // Reset smoothers
        for (size_t i = 0; i < kMaxModes; ++i) {
            frequencySmooth_[i].reset();
            amplitudeSmooth_[i].reset();

            // Re-snap to current targets if prepared
            if (prepared_) {
                frequencySmooth_[i].snapTo(frequencies_[i]);
                amplitudeSmooth_[i].snapTo(gains_[i]);
            }
        }
    }

    // =========================================================================
    // Per-Mode Control (FR-005, FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set frequency for a specific mode (FR-005).
    /// @param index Mode index [0, kMaxModes-1]
    /// @param hz Frequency in Hz [20, sampleRate * 0.45]
    /// @note Frequency is clamped to valid range.
    /// @note Uses smoothing to prevent clicks (FR-030).
    void setModeFrequency(int index, float hz) noexcept {
        if (index < 0 || static_cast<size_t>(index) >= kMaxModes) return;

        const float maxFreq = static_cast<float>(sampleRate_) * kMaxModeFrequencyRatio;
        frequencies_[static_cast<size_t>(index)] = std::clamp(hz, kMinModeFrequency, maxFreq);

        // Update smoother target (coefficients calculated per-sample in process())
        frequencySmooth_[static_cast<size_t>(index)].setTarget(frequencies_[static_cast<size_t>(index)]);

        // Enable the mode when frequency is set
        enabled_[static_cast<size_t>(index)] = true;
    }

    /// @brief Set decay time (T60) for a specific mode (FR-006).
    /// @param index Mode index [0, kMaxModes-1]
    /// @param t60Seconds Decay time in seconds [0.001, 30.0]
    /// @note Decay is clamped to valid range.
    void setModeDecay(int index, float t60Seconds) noexcept {
        if (index < 0 || static_cast<size_t>(index) >= kMaxModes) return;

        t60s_[static_cast<size_t>(index)] = std::clamp(t60Seconds, kMinModeDecay, kMaxModeDecay);

        // Recalculate coefficients
        if (prepared_) {
            calculateModeCoefficients(static_cast<size_t>(index));
        }
    }

    /// @brief Set amplitude for a specific mode (FR-007).
    /// @param index Mode index [0, kMaxModes-1]
    /// @param amplitude Amplitude [0.0, 1.0]
    /// @note Amplitude is clamped to valid range.
    /// @note Uses smoothing to prevent clicks (FR-030).
    void setModeAmplitude(int index, float amplitude) noexcept {
        if (index < 0 || static_cast<size_t>(index) >= kMaxModes) return;

        gains_[static_cast<size_t>(index)] = std::clamp(amplitude, 0.0f, 1.0f);

        // Update smoother target
        amplitudeSmooth_[static_cast<size_t>(index)].setTarget(gains_[static_cast<size_t>(index)]);
    }

    /// @brief Bulk-configure modes from analysis data (FR-008).
    /// @param modes Array of ModalData structures
    /// @param count Number of modes to configure (excess > 32 ignored)
    /// @note Disables modes beyond count.
    void setModes(const ModalData* modes, int count) noexcept {
        if (modes == nullptr || count <= 0) return;

        const int effectiveCount = std::min(count, static_cast<int>(kMaxModes));

        // Configure provided modes
        for (int i = 0; i < effectiveCount; ++i) {
            setModeFrequency(i, modes[i].frequency);
            setModeDecay(i, modes[i].t60);
            setModeAmplitude(i, modes[i].amplitude);
        }

        // Disable remaining modes
        for (size_t i = static_cast<size_t>(effectiveCount); i < kMaxModes; ++i) {
            enabled_[i] = false;
        }
    }

    // =========================================================================
    // Material Presets (FR-009, FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Select a material preset (FR-009).
    /// @param mat Material type
    /// @note Configures frequency ratios and frequency-dependent decay (FR-010, FR-011).
    /// @note Presets can be further customized (FR-012).
    void setMaterial(Material mat) noexcept {
        const auto& preset = kMaterialPresets[static_cast<size_t>(mat)];

        // Configure modes based on material preset
        for (int i = 0; i < preset.numModes; ++i) {
            const float modeFreq = kModalBaseFrequency * preset.ratios[static_cast<size_t>(i)];
            const float modeT60 = calculateMaterialT60(modeFreq, preset.b1, preset.b3);

            setModeFrequency(i, modeFreq);
            setModeDecay(i, modeT60);
            setModeAmplitude(i, 1.0f / static_cast<float>(i + 1));  // 1/n amplitude decay
        }

        // Disable remaining modes
        for (size_t i = static_cast<size_t>(preset.numModes); i < kMaxModes; ++i) {
            enabled_[i] = false;
        }
    }

    // =========================================================================
    // Global Controls (FR-013, FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Set size scaling factor (FR-013).
    /// @param scale Size multiplier [0.1, 10.0]
    /// @note Size 2.0 = frequencies halved (larger object, lower pitch).
    /// @note Size 0.5 = frequencies doubled (smaller object, higher pitch).
    /// @note Clamped to valid range (FR-014).
    void setSize(float scale) noexcept {
        size_ = std::clamp(scale, kMinSizeScale, kMaxSizeScale);

        // Recalculate coefficients for all enabled modes
        if (prepared_) {
            for (size_t i = 0; i < kMaxModes; ++i) {
                if (enabled_[i]) {
                    calculateModeCoefficients(i);
                }
            }
        }
    }

    /// @brief Set global damping (FR-015).
    /// @param amount Damping amount [0.0, 1.0]
    /// @note Damping 0.0 = no change (full decay).
    /// @note Damping 1.0 = instant silence.
    /// @note Applied multiplicatively: effective_T60 = base_T60 * (1 - damping) (FR-016).
    void setDamping(float amount) noexcept {
        damping_ = std::clamp(amount, 0.0f, 1.0f);

        // Recalculate coefficients for all enabled modes
        if (prepared_) {
            for (size_t i = 0; i < kMaxModes; ++i) {
                if (enabled_[i]) {
                    calculateModeCoefficients(i);
                }
            }
        }
    }

    // =========================================================================
    // Strike/Excitation (FR-017, FR-018, FR-019, FR-020)
    // =========================================================================

    /// @brief Excite all modes with an impulse (FR-017).
    /// @param velocity Excitation strength [0.0, 1.0] (FR-018)
    /// @note Velocity scales the excitation amplitude.
    /// @note Energy is added to existing state (accumulative) (FR-019).
    /// @note Output appears on next process() call (FR-020).
    void strike(float velocity = 1.0f) noexcept {
        const float clampedVelocity = std::clamp(velocity, 0.0f, 1.0f);

        // Add energy to all enabled modes (FR-019)
        for (size_t i = 0; i < kMaxModes; ++i) {
            if (enabled_[i]) {
                y1_[i] += clampedVelocity * gains_[i];
            }
        }
    }

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
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return 0.0f;  // FR-026
        }

        // NaN/Inf input handling (FR-032)
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        float output = 0.0f;

        for (size_t k = 0; k < kMaxModes; ++k) {
            if (!enabled_[k]) continue;

            // Get smoothed parameters (FR-030)
            const float smoothedFreq = frequencySmooth_[k].process();
            const float smoothedAmp = amplitudeSmooth_[k].process();

            // Recalculate coefficients from smoothed frequency for click-free changes
            // Apply size scaling to frequency (inverse relationship)
            const float effectiveFreq = smoothedFreq / size_;

            // Clamp to valid frequency range
            const float maxFreq = static_cast<float>(sampleRate_) * kMaxModeFrequencyRatio;
            const float clampedFreq = std::clamp(effectiveFreq, kMinModeFrequency, maxFreq);

            // Apply damping to T60
            const float dampingScale = 1.0f - damping_ * 0.9999f;
            const float effectiveT60 = t60s_[k] * dampingScale;

            // Calculate pole radius from T60
            const float R = t60ToPoleRadius(effectiveT60);

            // Calculate angular frequency
            const float theta = kTwoPi * clampedFreq / static_cast<float>(sampleRate_);

            // Two-pole coefficients
            const float a1 = 2.0f * R * std::cos(theta);
            const float a2 = R * R;

            // Two-pole oscillator difference equation (FR-021):
            // y[n] = input * amp + a1 * y[n-1] - a2 * y[n-2]
            const float y = input * smoothedAmp + a1 * y1_[k] - a2 * y2_[k];

            // Update state
            y2_[k] = y1_[k];
            y1_[k] = detail::flushDenormal(y);  // FR-029

            output += y;
        }

        return detail::flushDenormal(output);
    }

    /// @brief Process a block of samples in-place (FR-022).
    /// @param buffer Audio buffer (input, modified to output)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, int numSamples) noexcept {
        if (buffer == nullptr || numSamples <= 0) return;

        for (int i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if the resonator has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get the number of active (enabled) modes.
    /// @return Number of enabled modes [0, kMaxModes]
    [[nodiscard]] int getNumActiveModes() const noexcept {
        int count = 0;
        for (size_t i = 0; i < kMaxModes; ++i) {
            if (enabled_[i]) ++count;
        }
        return count;
    }

    /// @brief Get the frequency of a specific mode.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return Frequency in Hz, or 0.0f if index invalid
    [[nodiscard]] float getModeFrequency(int index) const noexcept {
        if (index < 0 || static_cast<size_t>(index) >= kMaxModes) return 0.0f;
        return frequencies_[static_cast<size_t>(index)];
    }

    /// @brief Get the decay time of a specific mode.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return Decay time in seconds, or 0.0f if index invalid
    [[nodiscard]] float getModeDecay(int index) const noexcept {
        if (index < 0 || static_cast<size_t>(index) >= kMaxModes) return 0.0f;
        return t60s_[static_cast<size_t>(index)];
    }

    /// @brief Get the amplitude of a specific mode.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return Amplitude [0.0, 1.0], or 0.0f if index invalid
    [[nodiscard]] float getModeAmplitude(int index) const noexcept {
        if (index < 0 || static_cast<size_t>(index) >= kMaxModes) return 0.0f;
        return gains_[static_cast<size_t>(index)];
    }

    /// @brief Check if a specific mode is enabled.
    /// @param index Mode index [0, kMaxModes-1]
    /// @return true if mode is enabled, false if disabled or index invalid
    [[nodiscard]] bool isModeEnabled(int index) const noexcept {
        if (index < 0 || static_cast<size_t>(index) >= kMaxModes) return false;
        return enabled_[static_cast<size_t>(index)];
    }

    /// @brief Get the current size scaling factor.
    /// @return Size multiplier [0.1, 10.0]
    [[nodiscard]] float getSize() const noexcept {
        return size_;
    }

    /// @brief Get the current damping amount.
    /// @return Damping [0.0, 1.0]
    [[nodiscard]] float getDamping() const noexcept {
        return damping_;
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Convert T60 decay time to pole radius.
    /// @param t60 Decay time in seconds
    /// @return Pole radius R where R = exp(-6.91 / (t60 * sampleRate))
    [[nodiscard]] float t60ToPoleRadius(float t60) const noexcept {
        // T60 is the time to decay by 60dB (factor of 1000)
        // -60dB = ln(1000) time constants = 6.91 time constants
        // R = exp(-1/tau_samples) where tau_samples = T60 * sampleRate / 6.91
        // Simplified: R = exp(-6.91 / (T60 * sampleRate))
        const float exponent = -kModalLn1000 / (t60 * static_cast<float>(sampleRate_));
        return std::exp(exponent);
    }

    /// @brief Calculate frequency-dependent T60 for material presets.
    /// @param frequency Mode frequency in Hz
    /// @param b1 Global damping coefficient (Hz)
    /// @param b3 Frequency-dependent damping coefficient (s)
    /// @return T60 decay time in seconds
    [[nodiscard]] float calculateMaterialT60(float frequency, float b1, float b3) const noexcept {
        // Loss factor: R_k = b1 + b3 * f_k^2
        // T60 = 6.91 / R_k
        const float lossFactor = b1 + b3 * frequency * frequency;
        const float t60 = kModalLn1000 / lossFactor;
        return std::clamp(t60, kMinModeDecay, kMaxModeDecay);
    }

    /// @brief Calculate oscillator coefficients for a mode.
    /// @param index Mode index
    void calculateModeCoefficients(size_t index) noexcept {
        if (index >= kMaxModes) return;

        // Apply size scaling to frequency (inverse relationship)
        const float effectiveFreq = frequencies_[index] / size_;

        // Clamp to valid frequency range
        const float maxFreq = static_cast<float>(sampleRate_) * kMaxModeFrequencyRatio;
        const float clampedFreq = std::clamp(effectiveFreq, kMinModeFrequency, maxFreq);

        // Apply damping to T60
        // damping = 0: full decay, damping = 1: instant silence
        const float dampingScale = 1.0f - damping_ * 0.9999f;  // Prevent exact 0
        const float effectiveT60 = t60s_[index] * dampingScale;

        // Calculate pole radius from T60
        const float R = t60ToPoleRadius(effectiveT60);

        // Calculate angular frequency
        const float theta = kTwoPi * clampedFreq / static_cast<float>(sampleRate_);

        // Two-pole coefficients: y[n] = input*amp + a1*y[n-1] - a2*y[n-2]
        // where a1 = 2*R*cos(theta), a2 = R^2
        a1_[index] = 2.0f * R * std::cos(theta);
        a2_[index] = R * R;
    }

    // =========================================================================
    // Data Members
    // =========================================================================

    // Oscillator state arrays
    std::array<float, kMaxModes> y1_{};      ///< y[n-1] delay state
    std::array<float, kMaxModes> y2_{};      ///< y[n-2] delay state

    // Oscillator coefficients
    std::array<float, kMaxModes> a1_{};      ///< 2 * R * cos(theta)
    std::array<float, kMaxModes> a2_{};      ///< R * R

    // Mode parameters
    std::array<float, kMaxModes> gains_{};       ///< Mode amplitudes [0, 1]
    std::array<float, kMaxModes> frequencies_{}; ///< Mode frequencies in Hz
    std::array<float, kMaxModes> t60s_{};        ///< Decay times in seconds
    std::array<bool, kMaxModes> enabled_{};      ///< Mode enabled flags

    // Parameter smoothers
    std::array<OnePoleSmoother, kMaxModes> frequencySmooth_;
    std::array<OnePoleSmoother, kMaxModes> amplitudeSmooth_;

    // Global parameters
    double sampleRate_ = 44100.0;
    float size_ = 1.0f;                     ///< Size scaling factor [0.1, 10.0]
    float damping_ = 0.0f;                  ///< Global damping [0.0, 1.0]
    float smoothingTimeMs_ = kDefaultModalSmoothingTimeMs;

    // State
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
