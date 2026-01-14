// ==============================================================================
// Layer 2: DSP Processor - DiodeClipper
// ==============================================================================
// Configurable diode clipping circuit modeling with four diode types
// (Silicon, Germanium, LED, Schottky), three topologies (Symmetric, Asymmetric,
// SoftHard), and per-instance configurable parameters.
//
// Feature: 060-diode-clipper
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: core/db_utils.h (dbToGain), core/sigmoid.h (Asymmetric::diode, Sigmoid::hardClip)
//   - Layer 1: primitives/dc_blocker.h, primitives/smoother.h
//   - stdlib: <cstddef>, <algorithm>, <cmath>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.5% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/060-diode-clipper/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Diode semiconductor types with distinct clipping characteristics.
///
/// Each type defines default forward voltage threshold and knee sharpness:
/// - Silicon: Classic overdrive (~0.6V, sharp knee)
/// - Germanium: Warm, vintage (~0.3V, soft knee)
/// - LED: Aggressive, hard (~1.8V, very hard knee)
/// - Schottky: Subtle, early (~0.2V, softest knee)
enum class DiodeType : uint8_t {
    Silicon = 0,    ///< Standard silicon diode (~0.6V, sharp knee)
    Germanium = 1,  ///< Vintage germanium (~0.3V, soft knee)
    LED = 2,        ///< Light-emitting diode (~1.8V, very hard knee)
    Schottky = 3    ///< Schottky barrier (~0.2V, softest knee)
};

/// @brief Circuit topology configurations for positive/negative half-cycles.
///
/// Determines harmonic content:
/// - Symmetric: Both polarities use identical curves (odd harmonics only)
/// - Asymmetric: Different curves per polarity (even + odd harmonics)
/// - SoftHard: Soft knee positive, hard knee negative (even + odd harmonics)
enum class ClipperTopology : uint8_t {
    Symmetric = 0,  ///< Both polarities use identical curves (odd harmonics)
    Asymmetric = 1, ///< Different curves per polarity (even + odd harmonics)
    SoftHard = 2    ///< Soft knee positive, hard knee negative
};

// =============================================================================
// DiodeClipper Class
// =============================================================================

/// @brief Layer 2 DSP processor for diode clipping circuit modeling.
///
/// Models configurable diode clipping with four diode types and three
/// topologies. Composes Layer 1 primitives (DCBlocker, OnePoleSmoother)
/// with parameterized diode transfer functions.
///
/// @par Signal Chain
/// Input -> [Drive Gain (smoothed)] -> [Diode Clipping (topology-dependent)] ->
/// [DC Blocker] -> [Output Gain (smoothed)] -> Blend with Dry (mix smoothed)
///
/// @par Features
/// - Diode types: Silicon, Germanium, LED, Schottky with configurable voltage/knee
/// - Topologies: Symmetric (odd harmonics), Asymmetric (even+odd), SoftHard
/// - Parameter smoothing: 5ms smoothing on all parameters to prevent clicks
/// - DC blocking: Automatic DC removal after asymmetric clipping
/// - No internal oversampling (users wrap externally with Oversampler if needed)
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
/// DiodeClipper clipper;
/// clipper.prepare(44100.0, 512);
/// clipper.setDiodeType(DiodeType::Germanium);
/// clipper.setTopology(ClipperTopology::Asymmetric);
/// clipper.setDrive(12.0f);  // +12dB drive
/// clipper.setMix(1.0f);     // 100% wet
///
/// // Process audio blocks
/// clipper.process(buffer, numSamples);
/// @endcode
///
/// @see specs/060-diode-clipper/spec.md
class DiodeClipper {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum drive in dB
    static constexpr float kMinDriveDb = -24.0f;

    /// Maximum drive in dB
    static constexpr float kMaxDriveDb = +48.0f;

    /// Minimum output level in dB
    static constexpr float kMinOutputDb = -24.0f;

    /// Maximum output level in dB
    static constexpr float kMaxOutputDb = +24.0f;

    /// Minimum forward voltage in volts
    static constexpr float kMinVoltage = 0.05f;

    /// Maximum forward voltage in volts
    static constexpr float kMaxVoltage = 5.0f;

    /// Minimum knee sharpness (dimensionless)
    static constexpr float kMinKnee = 0.5f;

    /// Maximum knee sharpness (dimensionless)
    static constexpr float kMaxKnee = 20.0f;

    /// Default smoothing time in milliseconds
    static constexpr float kDefaultSmoothingMs = 5.0f;

    /// DC blocker cutoff frequency in Hz
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    // Diode type default values
    static constexpr float kSiliconVoltage = 0.6f;
    static constexpr float kSiliconKnee = 5.0f;
    static constexpr float kGermaniumVoltage = 0.3f;
    static constexpr float kGermaniumKnee = 2.0f;
    static constexpr float kLEDVoltage = 1.8f;
    static constexpr float kLEDKnee = 15.0f;
    static constexpr float kSchottkyVoltage = 0.2f;
    static constexpr float kSchottkyKnee = 1.5f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor with safe defaults.
    ///
    /// Initializes with:
    /// - Diode type: Silicon
    /// - Topology: Symmetric
    /// - Drive: 0 dB (unity)
    /// - Mix: 1.0 (100% wet)
    /// - Output level: 0 dB (unity)
    /// - Forward voltage: 0.6V (Silicon default)
    /// - Knee sharpness: 5.0 (Silicon default)
    DiodeClipper() noexcept = default;

    /// @brief Configure the processor for the given sample rate (FR-001).
    ///
    /// Configures internal components (DCBlocker, smoothers) for the specified
    /// sample rate. Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum block size in samples (unused, for future use)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state without reallocation (FR-002).
    ///
    /// Clears DC blocker state and snaps smoothers to current target values.
    /// Call when starting a new audio stream or after discontinuity.
    void reset() noexcept;

    /// @brief Get the latency introduced by this processor (FR-021).
    /// @return Latency in samples (always 0 for this processor)
    [[nodiscard]] constexpr size_t getLatency() const noexcept {
        return 0;
    }

    // =========================================================================
    // Diode Type (FR-004 to FR-008)
    // =========================================================================

    /// @brief Set the diode type (FR-008).
    ///
    /// Changes the diode type and smoothly transitions voltage/knee to new
    /// defaults over ~5ms to prevent clicks.
    ///
    /// @param type Diode type to use
    void setDiodeType(DiodeType type) noexcept;

    /// @brief Get the current diode type.
    /// @return Current diode type
    [[nodiscard]] DiodeType getDiodeType() const noexcept {
        return diodeType_;
    }

    // =========================================================================
    // Topology (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set the clipping topology (FR-012).
    ///
    /// Changes the circuit topology instantly (no smoothing needed).
    ///
    /// @param topology Topology configuration to use
    void setTopology(ClipperTopology topology) noexcept {
        topology_ = topology;
    }

    /// @brief Get the current topology.
    /// @return Current topology
    [[nodiscard]] ClipperTopology getTopology() const noexcept {
        return topology_;
    }

    // =========================================================================
    // Parameter Setters (FR-013, FR-014, FR-025, FR-026, FR-027)
    // =========================================================================

    /// @brief Set the drive (input gain) in dB (FR-013).
    ///
    /// Higher drive creates more saturation. Value is clamped to [-24, +48] dB.
    ///
    /// @param dB Drive in decibels
    void setDrive(float dB) noexcept;

    /// @brief Set the dry/wet mix (FR-014).
    ///
    /// Controls blend between dry and clipped signal.
    /// - 0.0 = full bypass (output equals input)
    /// - 1.0 = 100% clipped signal
    /// Value is clamped to [0.0, 1.0].
    ///
    /// @param mix Mix amount [0.0, 1.0]
    void setMix(float mix) noexcept;

    /// @brief Set the forward voltage threshold (FR-025).
    ///
    /// Overrides the diode type default. Value is clamped to [0.05, 5.0] volts.
    ///
    /// @param voltage Forward voltage in volts
    void setForwardVoltage(float voltage) noexcept;

    /// @brief Set the knee sharpness (FR-026).
    ///
    /// Overrides the diode type default. Value is clamped to [0.5, 20.0].
    /// Lower values = softer knee, higher values = harder knee.
    ///
    /// @param knee Knee sharpness (dimensionless)
    void setKneeSharpness(float knee) noexcept;

    /// @brief Set the output level in dB (FR-027).
    ///
    /// Post-clipping gain adjustment. Value is clamped to [-24, +24] dB.
    ///
    /// @param dB Output level in decibels
    void setOutputLevel(float dB) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get the current drive in dB.
    /// @return Drive in decibels (clamped value)
    [[nodiscard]] float getDrive() const noexcept {
        return driveDb_;
    }

    /// @brief Get the current mix amount.
    /// @return Mix amount [0.0, 1.0]
    [[nodiscard]] float getMix() const noexcept {
        return mixAmount_;
    }

    /// @brief Get the current forward voltage.
    /// @return Forward voltage in volts
    [[nodiscard]] float getForwardVoltage() const noexcept {
        return forwardVoltage_;
    }

    /// @brief Get the current knee sharpness.
    /// @return Knee sharpness (dimensionless)
    [[nodiscard]] float getKneeSharpness() const noexcept {
        return kneeSharpness_;
    }

    /// @brief Get the current output level in dB.
    /// @return Output level in decibels
    [[nodiscard]] float getOutputLevel() const noexcept {
        return outputLevelDb_;
    }

    // =========================================================================
    // Processing (FR-017, FR-018)
    // =========================================================================

    /// @brief Process a block of audio samples in-place (FR-017).
    ///
    /// Applies diode clipping with the current parameter settings.
    /// When mix is 0.0, acts as full bypass (output equals input - FR-015).
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call (FR-020)
    /// @note n=0 is handled gracefully (no-op)
    /// @note If prepare() not called, returns input unchanged (FR-003)
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a single sample (FR-018).
    ///
    /// Applies diode clipping to a single sample with smoothed parameters.
    ///
    /// @param input Input sample
    /// @return Processed sample
    ///
    /// @note If prepare() not called, returns input unchanged (FR-003)
    [[nodiscard]] float processSample(float input) noexcept;

private:
    // =========================================================================
    // Helper Methods
    // =========================================================================

    /// @brief Get default voltage and knee for a diode type.
    /// @param type Diode type
    /// @return Pair of {voltage, knee}
    [[nodiscard]] static constexpr std::pair<float, float> getDefaultsForType(DiodeType type) noexcept {
        switch (type) {
            case DiodeType::Germanium:
                return {kGermaniumVoltage, kGermaniumKnee};
            case DiodeType::LED:
                return {kLEDVoltage, kLEDKnee};
            case DiodeType::Schottky:
                return {kSchottkyVoltage, kSchottkyKnee};
            case DiodeType::Silicon:
            default:
                return {kSiliconVoltage, kSiliconKnee};
        }
    }

    /// @brief Apply configurable diode clipping to a sample.
    /// @param x Input sample
    /// @param voltage Forward voltage threshold
    /// @param knee Knee sharpness
    /// @return Clipped sample
    [[nodiscard]] float applyDiodeClip(float x, float voltage, float knee) const noexcept;

    // =========================================================================
    // Parameters
    // =========================================================================

    DiodeType diodeType_ = DiodeType::Silicon;
    ClipperTopology topology_ = ClipperTopology::Symmetric;
    float driveDb_ = 0.0f;
    float mixAmount_ = 1.0f;
    float outputLevelDb_ = 0.0f;
    float forwardVoltage_ = kSiliconVoltage;
    float kneeSharpness_ = kSiliconKnee;

    // =========================================================================
    // Parameter Smoothers
    // =========================================================================

    OnePoleSmoother driveSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother outputSmoother_;
    OnePoleSmoother voltageSmoother_;
    OnePoleSmoother kneeSmoother_;

    // =========================================================================
    // DSP Components
    // =========================================================================

    DCBlocker2 dcBlocker_;  ///< 2nd-order Bessel for faster settling (SC-006)

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

// =============================================================================
// Inline Implementations
// =============================================================================

inline void DiodeClipper::prepare(double sampleRate, [[maybe_unused]] size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;
    const float sr = static_cast<float>(sampleRate);

    // Configure all smoothers with 5ms smoothing time
    driveSmoother_.configure(kDefaultSmoothingMs, sr);
    mixSmoother_.configure(kDefaultSmoothingMs, sr);
    outputSmoother_.configure(kDefaultSmoothingMs, sr);
    voltageSmoother_.configure(kDefaultSmoothingMs, sr);
    kneeSmoother_.configure(kDefaultSmoothingMs, sr);

    // Set initial targets
    driveSmoother_.setTarget(dbToGain(driveDb_));
    mixSmoother_.setTarget(mixAmount_);
    outputSmoother_.setTarget(dbToGain(outputLevelDb_));
    voltageSmoother_.setTarget(forwardVoltage_);
    kneeSmoother_.setTarget(kneeSharpness_);

    // Snap to initial values
    driveSmoother_.snapToTarget();
    mixSmoother_.snapToTarget();
    outputSmoother_.snapToTarget();
    voltageSmoother_.snapToTarget();
    kneeSmoother_.snapToTarget();

    // Configure DC blocker
    dcBlocker_.prepare(sampleRate, kDCBlockerCutoffHz);

    prepared_ = true;
}

inline void DiodeClipper::reset() noexcept {
    // Reset DC blocker state
    dcBlocker_.reset();

    // Snap all smoothers to current targets (no ramp on next process)
    driveSmoother_.snapToTarget();
    mixSmoother_.snapToTarget();
    outputSmoother_.snapToTarget();
    voltageSmoother_.snapToTarget();
    kneeSmoother_.snapToTarget();
}

inline void DiodeClipper::setDiodeType(DiodeType type) noexcept {
    diodeType_ = type;
    auto [voltage, knee] = getDefaultsForType(type);
    forwardVoltage_ = voltage;
    kneeSharpness_ = knee;
    voltageSmoother_.setTarget(voltage);
    kneeSmoother_.setTarget(knee);
}

inline void DiodeClipper::setDrive(float dB) noexcept {
    driveDb_ = std::clamp(dB, kMinDriveDb, kMaxDriveDb);
    driveSmoother_.setTarget(dbToGain(driveDb_));
}

inline void DiodeClipper::setMix(float mix) noexcept {
    mixAmount_ = std::clamp(mix, 0.0f, 1.0f);
    mixSmoother_.setTarget(mixAmount_);
}

inline void DiodeClipper::setForwardVoltage(float voltage) noexcept {
    forwardVoltage_ = std::clamp(voltage, kMinVoltage, kMaxVoltage);
    voltageSmoother_.setTarget(forwardVoltage_);
}

inline void DiodeClipper::setKneeSharpness(float knee) noexcept {
    kneeSharpness_ = std::clamp(knee, kMinKnee, kMaxKnee);
    kneeSmoother_.setTarget(kneeSharpness_);
}

inline void DiodeClipper::setOutputLevel(float dB) noexcept {
    outputLevelDb_ = std::clamp(dB, kMinOutputDb, kMaxOutputDb);
    outputSmoother_.setTarget(dbToGain(outputLevelDb_));
}

inline float DiodeClipper::applyDiodeClip(float x, float voltage, float knee) const noexcept {
    // Pure odd function for symmetric clipping (SC-002: odd harmonics only)
    //
    // Uses tanh-based saturation which is mathematically an odd function:
    // f(-x) = -f(x), guaranteeing only odd harmonics are generated.
    //
    // The voltage parameter controls the saturation threshold.
    // The knee parameter controls the sharpness of the saturation curve.
    //
    // Research: Any odd function applied to a sinusoid produces only odd harmonics.
    // See: https://www.dsprelated.com/freebooks/pasp/Soft_Clipping.html

    if (voltage <= 0.0f) {
        return x;  // Safety check
    }

    // Scale factor based on knee: higher knee = sharper transition
    // Normalized around default knee of 5
    const float kneeScale = knee / 5.0f;

    // Pure tanh-based saturation (odd function: tanh(-x) = -tanh(x))
    // This guarantees only odd harmonics for symmetric topology
    //
    // The formula: voltage * tanh(x * kneeScale / voltage)
    // - For small x: output ≈ x (linear region)
    // - For large x: output ≈ ±voltage (saturation region)
    // - Transition smoothness controlled by kneeScale
    //
    // Using std::tanh for maximum precision in symmetric mode (SC-002).
    // FastMath::fastTanh has 0.05% error that can introduce subtle asymmetry.
    return voltage * std::tanh(x * kneeScale / voltage);
}

inline float DiodeClipper::processSample(float input) noexcept {
    // FR-003: If not prepared, return input unchanged
    if (!prepared_) {
        return input;
    }

    // Advance smoothers (must always advance to keep state consistent)
    const float driveGain = driveSmoother_.process();
    const float mixAmt = mixSmoother_.process();
    const float outputGain = outputSmoother_.process();
    const float voltage = voltageSmoother_.process();
    const float knee = kneeSmoother_.process();

    // Store dry sample
    const float dry = input;

    // FR-015: Early return for bypass (mix near zero)
    if (mixAmt < 0.0001f) {
        // Still need to run DC blocker to keep its state valid
        (void)dcBlocker_.process(input);
        return dry;
    }

    // Apply drive gain
    float wet = input * driveGain;

    // Apply topology-specific clipping
    switch (topology_) {
        case ClipperTopology::Symmetric:
            // FR-009: Both polarities use identical curves (odd harmonics)
            wet = applyDiodeClip(wet, voltage, knee);
            break;

        case ClipperTopology::Asymmetric:
            // FR-010: Different transfer functions per polarity
            // Models real diode physics: forward bias vs reverse bias
            // Uses the existing Asymmetric::diode() style function but parameterized
            if (wet >= 0.0f) {
                // Forward bias: soft exponential saturation
                // Clips earlier and softer on positive side
                const float kneeScale = knee / 5.0f;
                wet = voltage * 0.8f * (1.0f - std::exp(-wet * kneeScale * 1.5f / voltage));
            } else {
                // Reverse bias: harder, more linear
                // Uses Asymmetric::diode() style reverse bias
                // This creates the asymmetry needed for even harmonics
                wet = wet / (1.0f - 0.3f * wet / voltage);
            }
            break;

        case ClipperTopology::SoftHard:
            // FR-011: Soft knee positive, hard clip negative
            if (wet >= 0.0f) {
                // Soft knee for positive
                wet = applyDiodeClip(wet, voltage, knee);
            } else {
                // Hard clip for negative using Sigmoid::hardClip
                wet = -Sigmoid::hardClip(-wet, voltage);
            }
            break;
    }

    // Apply DC blocking (FR-019)
    wet = dcBlocker_.process(wet);

    // Apply output gain
    wet *= outputGain;

    // Apply dry/wet mix blend
    return dry * (1.0f - mixAmt) + wet * mixAmt;
}

inline void DiodeClipper::process(float* buffer, size_t numSamples) noexcept {
    // Handle null buffer or zero samples
    if (numSamples == 0 || buffer == nullptr) {
        return;
    }

    // FR-003: If not prepared, return input unchanged
    if (!prepared_) {
        return;
    }

    // FR-015: Early exit if mix is essentially 0 (bypass for efficiency)
    // Check if smoother is complete and at zero target
    if (mixSmoother_.isComplete() && mixAmount_ < 0.0001f) {
        return;
    }

    // Process sample-by-sample for proper parameter smoothing
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = processSample(buffer[i]);
    }
}

} // namespace DSP
} // namespace Krate
