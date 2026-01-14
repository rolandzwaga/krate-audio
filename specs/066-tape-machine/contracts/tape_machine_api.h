// ==============================================================================
// API Contract: TapeMachine System
// ==============================================================================
// This file defines the public API contract for TapeMachine.
// Implementation must conform to these signatures.
//
// Location: dsp/include/krate/dsp/systems/tape_machine.h
// Layer: 3 (System Components)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// Forward declarations for internal types
class TapeSaturator;
class NoiseGenerator;
class LFO;
class Biquad;
class OnePoleSmoother;
enum class HysteresisSolver : uint8_t;

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Machine model presets (FR-031)
enum class MachineModel : uint8_t {
    Studer = 0,   ///< Swiss precision - tighter response, transparent
    Ampex = 1     ///< American warmth - fuller lows, more colored
};

/// @brief Tape speed selection (FR-004)
enum class TapeSpeed : uint8_t {
    IPS_7_5 = 0,  ///< 7.5 ips - lo-fi, pronounced character
    IPS_15 = 1,   ///< 15 ips - balanced, standard studio
    IPS_30 = 2    ///< 30 ips - hi-fi, minimal coloration
};

/// @brief Tape formulation selection (FR-005)
enum class TapeType : uint8_t {
    Type456 = 0,  ///< Warm, classic - earlier saturation, more harmonics
    Type900 = 1,  ///< Hot, punchy - higher headroom, tight transients
    TypeGP9 = 2   ///< Modern, clean - highest headroom, subtle color
};

// =============================================================================
// TapeMachine Class Contract
// =============================================================================

/// @brief Layer 3 tape machine system composing saturation, filtering, and modulation.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle IX: Layer 3 (composes Layers 0-2)
/// - Principle X: DSP Constraints (DC blocking via TapeSaturator)
///
/// @par Signal Flow (FR-033)
/// Input -> InputGain -> Saturation -> HeadBump -> HFRolloff -> Wow/Flutter -> Hiss -> OutputGain -> Output
///
/// @see specs/066-tape-machine/spec.md
class TapeMachine {
public:
    // =========================================================================
    // Lifecycle (FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor with safe defaults.
    TapeMachine() noexcept;

    /// @brief Destructor.
    ~TapeMachine();

    // Non-copyable, movable
    TapeMachine(const TapeMachine&) = delete;
    TapeMachine& operator=(const TapeMachine&) = delete;
    TapeMachine(TapeMachine&&) noexcept;
    TapeMachine& operator=(TapeMachine&&) noexcept;

    /// @brief Prepare for processing (FR-002).
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @param maxBlockSize Maximum samples per process() call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state (FR-003).
    void reset() noexcept;

    // =========================================================================
    // Model and Type Selection (FR-004, FR-005, FR-031)
    // =========================================================================

    /// @brief Set machine model preset (FR-031).
    /// @param model Studer or Ampex
    /// @note Sets default head bump frequency, wow/flutter depths (unless manually overridden)
    void setMachineModel(MachineModel model) noexcept;

    /// @brief Set tape speed (FR-004).
    /// @param speed 7.5, 15, or 30 ips
    /// @note Sets default HF rolloff frequency (unless manually overridden)
    void setTapeSpeed(TapeSpeed speed) noexcept;

    /// @brief Set tape formulation (FR-005).
    /// @param type Tape formulation affecting saturation characteristics
    void setTapeType(TapeType type) noexcept;

    // =========================================================================
    // Gain Staging (FR-006, FR-007)
    // =========================================================================

    /// @brief Set input level (FR-006).
    /// @param dB Input level [-24, +24] dB
    void setInputLevel(float dB) noexcept;

    /// @brief Set output level (FR-007).
    /// @param dB Output level [-24, +24] dB
    void setOutputLevel(float dB) noexcept;

    // =========================================================================
    // Saturation Parameters (FR-008, FR-009, FR-010)
    // =========================================================================

    /// @brief Set tape bias (FR-008).
    /// @param bias Bias value [-1, +1], 0=symmetric
    void setBias(float bias) noexcept;

    /// @brief Set saturation amount (FR-009).
    /// @param amount Saturation [0, 1], 0=linear, 1=full
    void setSaturation(float amount) noexcept;

    /// @brief Set hysteresis model solver (FR-010).
    /// @param solver RK2, RK4, NR4, or NR8
    void setHysteresisModel(HysteresisSolver solver) noexcept;

    // =========================================================================
    // Head Bump Parameters (FR-011, FR-012)
    // =========================================================================

    /// @brief Set head bump amount (FR-011).
    /// @param amount Amount [0, 1], 0=disabled, 1=max (+6dB)
    void setHeadBumpAmount(float amount) noexcept;

    /// @brief Set head bump frequency (FR-012).
    /// @param hz Center frequency [30, 120] Hz
    /// @note Overrides machine model default
    void setHeadBumpFrequency(float hz) noexcept;

    // =========================================================================
    // HF Rolloff Parameters (FR-035, FR-036)
    // =========================================================================

    /// @brief Set HF rolloff amount (FR-035).
    /// @param amount Amount [0, 1], 0=disabled, 1=max attenuation
    void setHighFreqRolloffAmount(float amount) noexcept;

    /// @brief Set HF rolloff frequency (FR-036).
    /// @param hz Cutoff frequency [5000, 22000] Hz
    /// @note Overrides tape speed default
    void setHighFreqRolloffFrequency(float hz) noexcept;

    // =========================================================================
    // Hiss Parameter (FR-013)
    // =========================================================================

    /// @brief Set tape hiss amount (FR-013).
    /// @param amount Amount [0, 1], 0=disabled, 1=max (-20dB RMS)
    void setHiss(float amount) noexcept;

    // =========================================================================
    // Wow/Flutter Parameters (FR-014, FR-015, FR-016, FR-037, FR-038)
    // =========================================================================

    /// @brief Set combined wow/flutter amount (FR-014).
    /// @param amount Combined amount [0, 1]
    /// @note Convenience method - sets both wow and flutter equally
    void setWowFlutter(float amount) noexcept;

    /// @brief Set wow amount (FR-015).
    /// @param amount Wow amount [0, 1]
    void setWow(float amount) noexcept;

    /// @brief Set flutter amount (FR-015).
    /// @param amount Flutter amount [0, 1]
    void setFlutter(float amount) noexcept;

    /// @brief Set wow rate (FR-016).
    /// @param hz Rate [0.1, 2.0] Hz
    void setWowRate(float hz) noexcept;

    /// @brief Set flutter rate (FR-016).
    /// @param hz Rate [2.0, 15.0] Hz
    void setFlutterRate(float hz) noexcept;

    /// @brief Set wow depth (FR-037).
    /// @param cents Max deviation [0, 15] cents
    /// @note Overrides machine model default
    void setWowDepth(float cents) noexcept;

    /// @brief Set flutter depth (FR-038).
    /// @param cents Max deviation [0, 6] cents
    /// @note Overrides machine model default
    void setFlutterDepth(float cents) noexcept;

    // =========================================================================
    // Processing (FR-017)
    // =========================================================================

    /// @brief Process audio buffer in-place (FR-017).
    /// @param buffer Audio buffer to process
    /// @param numSamples Number of samples
    /// @note Real-time safe: no allocations, noexcept (FR-024)
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Getters (for UI/debugging)
    // =========================================================================

    [[nodiscard]] MachineModel getMachineModel() const noexcept;
    [[nodiscard]] TapeSpeed getTapeSpeed() const noexcept;
    [[nodiscard]] TapeType getTapeType() const noexcept;
    [[nodiscard]] float getInputLevel() const noexcept;
    [[nodiscard]] float getOutputLevel() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getSaturation() const noexcept;
    [[nodiscard]] float getHeadBumpAmount() const noexcept;
    [[nodiscard]] float getHeadBumpFrequency() const noexcept;
    [[nodiscard]] float getHighFreqRolloffAmount() const noexcept;
    [[nodiscard]] float getHighFreqRolloffFrequency() const noexcept;
    [[nodiscard]] float getHiss() const noexcept;
    [[nodiscard]] float getWow() const noexcept;
    [[nodiscard]] float getFlutter() const noexcept;
    [[nodiscard]] float getWowRate() const noexcept;
    [[nodiscard]] float getFlutterRate() const noexcept;
    [[nodiscard]] float getWowDepth() const noexcept;
    [[nodiscard]] float getFlutterDepth() const noexcept;
};

} // namespace DSP
} // namespace Krate
