// ==============================================================================
// API Contract: DiodeClipper Processor
// ==============================================================================
// This file defines the public API contract for DiodeClipper.
// Implementation in: dsp/include/krate/dsp/processors/diode_clipper.h
//
// Feature: 060-diode-clipper
// Layer: 2 (Processors)
// Dependencies: Layer 0 (db_utils.h, sigmoid.h), Layer 1 (dc_blocker.h, smoother.h)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Diode semiconductor type affecting clipping characteristics.
///
/// Each type has distinct forward voltage threshold and knee sharpness:
/// - Silicon: Standard, balanced overdrive character
/// - Germanium: Vintage warmth, earliest clipping onset
/// - LED: Aggressive, late clipping with hard knee
/// - Schottky: Subtle warmth, softest knee
enum class DiodeType : uint8_t {
    Silicon = 0,    ///< ~0.6V threshold, sharp knee (default)
    Germanium = 1,  ///< ~0.3V threshold, soft knee
    LED = 2,        ///< ~1.8V threshold, very hard knee
    Schottky = 3    ///< ~0.2V threshold, softest knee
};

/// @brief Circuit topology determining how positive/negative half-cycles clip.
///
/// - Symmetric: Both polarities identical (odd harmonics only)
/// - Asymmetric: Different curves per polarity (even + odd harmonics)
/// - SoftHard: Soft knee for positive, hard knee for negative
enum class ClipperTopology : uint8_t {
    Symmetric = 0,  ///< Identical clipping both polarities
    Asymmetric = 1, ///< Different transfer functions per polarity
    SoftHard = 2    ///< Soft positive, hard negative
};

// =============================================================================
// DiodeClipper Class Contract
// =============================================================================

/// @brief Layer 2 DSP Processor - Configurable diode clipping circuit modeling.
///
/// Models various diode clipping circuits with configurable parameters:
/// - 4 diode types (Silicon, Germanium, LED, Schottky)
/// - 3 topologies (Symmetric, Asymmetric, SoftHard)
/// - Per-instance configurable voltage threshold and knee sharpness
/// - DC blocking after clipping
/// - Parameter smoothing for click-free modulation
///
/// @par Signal Flow
/// Input -> [Drive] -> [Clipping] -> [DC Block] -> [Output Level] -> [Mix] -> Output
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, RAII)
/// - Principle IX: Layer 2 (depends only on Layer 0/1)
/// - Principle X: DSP Constraints (DC blocking; external oversampling)
///
/// @par Usage Example
/// @code
/// DiodeClipper clipper;
/// clipper.prepare(44100.0, 512);
/// clipper.setDiodeType(DiodeType::Germanium);
/// clipper.setTopology(ClipperTopology::Asymmetric);
/// clipper.setDrive(12.0f);  // +12 dB drive
/// clipper.setMix(1.0f);     // 100% wet
///
/// // In process callback
/// clipper.process(buffer, numSamples);
/// @endcode
class DiodeClipper {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDriveDb = -24.0f;      ///< Minimum drive in dB
    static constexpr float kMaxDriveDb = +48.0f;      ///< Maximum drive in dB
    static constexpr float kMinOutputDb = -24.0f;     ///< Minimum output level in dB
    static constexpr float kMaxOutputDb = +24.0f;     ///< Maximum output level in dB
    static constexpr float kMinVoltage = 0.05f;       ///< Minimum forward voltage
    static constexpr float kMaxVoltage = 5.0f;        ///< Maximum forward voltage
    static constexpr float kMinKnee = 0.5f;           ///< Minimum knee sharpness
    static constexpr float kMaxKnee = 20.0f;          ///< Maximum knee sharpness
    static constexpr float kDefaultSmoothingMs = 5.0f; ///< Parameter smoothing time
    static constexpr float kDCBlockerCutoffHz = 10.0f; ///< DC blocker cutoff

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Prepare processor for given sample rate and block size.
    ///
    /// MUST be called before any processing. Configures smoothers and DC blocker.
    /// Call again if sample rate changes.
    ///
    /// @param sampleRate Audio sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum samples per process() call
    ///
    /// @note Does not allocate memory - all state is fixed-size
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation.
    ///
    /// Clears DC blocker state and snaps smoothers to current targets.
    /// Call when audio stream restarts (e.g., transport stop/start).
    void reset() noexcept;

    // =========================================================================
    // Diode Type Configuration (FR-004 to FR-008)
    // =========================================================================

    /// @brief Set diode type and update voltage/knee to type defaults.
    ///
    /// Changes both the clipping algorithm AND smoothly transitions
    /// forwardVoltage and kneeSharpness to the new type's default values.
    ///
    /// @param type Diode type (Silicon, Germanium, LED, Schottky)
    ///
    /// @note Transition to new defaults is smoothed over ~5ms (FR-008)
    void setDiodeType(DiodeType type) noexcept;

    /// @brief Get current diode type.
    [[nodiscard]] DiodeType getDiodeType() const noexcept;

    // =========================================================================
    // Topology Configuration (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set circuit topology for positive/negative half-cycle handling.
    ///
    /// @param topology Topology (Symmetric, Asymmetric, SoftHard)
    ///
    /// @note Change is instant (not smoothed)
    void setTopology(ClipperTopology topology) noexcept;

    /// @brief Get current topology.
    [[nodiscard]] ClipperTopology getTopology() const noexcept;

    // =========================================================================
    // Parameter Setters (FR-013, FR-014, FR-016, FR-025, FR-026, FR-027)
    // =========================================================================

    /// @brief Set input gain (pre-clipping drive).
    ///
    /// @param dB Drive in decibels, clamped to [-24, +48] dB
    ///
    /// @note Smoothed over 5ms to prevent clicks (FR-016)
    void setDrive(float dB) noexcept;

    /// @brief Set dry/wet mix ratio.
    ///
    /// @param mix Mix ratio: 0.0 = full dry, 1.0 = full wet
    ///
    /// @note When mix == 0.0, processing is bypassed for efficiency (FR-015)
    /// @note Smoothed to prevent clicks (FR-016)
    void setMix(float mix) noexcept;

    /// @brief Set forward voltage threshold override.
    ///
    /// Overrides the diode type's default voltage. When setDiodeType() is called,
    /// this smoothly transitions to the new type's default.
    ///
    /// @param voltage Voltage in normalized range [0.05, 5.0]
    ///
    /// @note Smoothed over 5ms to prevent clicks
    void setForwardVoltage(float voltage) noexcept;

    /// @brief Set knee sharpness override.
    ///
    /// Overrides the diode type's default knee. When setDiodeType() is called,
    /// this smoothly transitions to the new type's default.
    ///
    /// @param knee Dimensionless sharpness [0.5, 20.0]
    ///             Lower = softer knee, Higher = harder knee
    ///
    /// @note Smoothed over 5ms to prevent clicks
    void setKneeSharpness(float knee) noexcept;

    /// @brief Set output gain (post-clipping level).
    ///
    /// @param dB Output level in decibels, clamped to [-24, +24] dB
    ///
    /// @note Smoothed over 5ms to prevent clicks
    void setOutputLevel(float dB) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get current drive in dB.
    [[nodiscard]] float getDrive() const noexcept;

    /// @brief Get current mix ratio [0.0, 1.0].
    [[nodiscard]] float getMix() const noexcept;

    /// @brief Get current forward voltage.
    [[nodiscard]] float getForwardVoltage() const noexcept;

    /// @brief Get current knee sharpness.
    [[nodiscard]] float getKneeSharpness() const noexcept;

    /// @brief Get current output level in dB.
    [[nodiscard]] float getOutputLevel() const noexcept;

    // =========================================================================
    // Processing (FR-017, FR-018, FR-019, FR-020, FR-021)
    // =========================================================================

    /// @brief Process a buffer of audio samples in-place.
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @pre prepare() has been called
    /// @pre numSamples <= maxBlockSize from prepare()
    ///
    /// @note Real-time safe: no allocations, O(N) complexity
    /// @note DC blocking always applied (FR-019)
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a single sample.
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @pre prepare() has been called
    ///
    /// @note Includes DC blocking (single-sample state)
    [[nodiscard]] float processSample(float input) noexcept;

    // =========================================================================
    // Info
    // =========================================================================

    /// @brief Get processing latency in samples.
    ///
    /// @return Always 0 (no internal oversampling = no latency)
    [[nodiscard]] size_t getLatency() const noexcept;
};

} // namespace DSP
} // namespace Krate
