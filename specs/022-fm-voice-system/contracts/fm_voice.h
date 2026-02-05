// ==============================================================================
// API Contract: FM Voice System
// ==============================================================================
// This is the API contract for the FMVoice system component.
// Implementation will be in: dsp/include/krate/dsp/systems/fm_voice.h
//
// Feature Branch: 022-fm-voice-system
// Date: 2026-02-05
// Spec: specs/022-fm-voice-system/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/processors/fm_operator.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <array>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// Algorithm Enum (FR-004, FR-007)
// =============================================================================

/// @brief FM synthesis algorithm routing topologies.
///
/// Each algorithm defines a specific routing configuration for the 4 operators,
/// specifying which operators are carriers (produce audible output), which are
/// modulators (modulate other operators' phases), and the modulation routing.
enum class Algorithm : uint8_t {
    Stacked2Op = 0,           ///< Simple 2->1 stack (bass, leads)
    Stacked4Op = 1,           ///< Full 4->3->2->1 chain (rich leads, brass)
    Parallel2Plus2 = 2,       ///< Two parallel 2-op stacks (organ, pads)
    Branched = 3,             ///< Multiple mods to single carrier (bells, metallic)
    Stacked3PlusCarrier = 4,  ///< 3-op stack + independent carrier (e-piano)
    Parallel4 = 5,            ///< All 4 as carriers (additive/organ)
    YBranch = 6,              ///< Mod feeding two parallel stacks (complex)
    DeepStack = 7,            ///< 4->3->2->1 chain, mid-chain feedback (aggressive, noise)
    kNumAlgorithms = 8        ///< Sentinel for validation
};

// =============================================================================
// Operator Mode Enum (FR-013)
// =============================================================================

/// @brief Distinguishes ratio-tracking from fixed-frequency behavior.
enum class OperatorMode : uint8_t {
    Ratio = 0,  ///< frequency = baseFrequency * ratio (default, FR-016)
    Fixed = 1   ///< frequency = fixedFrequency, ignores base (FR-017)
};

// =============================================================================
// FMVoice Class (FR-001 through FR-028)
// =============================================================================

/// @brief Complete 4-operator FM synthesis voice with algorithm routing.
///
/// A Layer 3 system component that composes 4 FMOperator instances with
/// selectable algorithm routing, providing a complete FM synthesis voice.
///
/// @par Features
/// - 8 selectable algorithm topologies (stacked, parallel, branched)
/// - Per-operator ratio or fixed frequency modes
/// - Single feedback-enabled operator per algorithm
/// - Carrier output normalization (sum / carrier count)
/// - DC blocking on output (20.0 Hz highpass)
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and all setters are fully real-time safe.
/// prepare() is NOT real-time safe (initializes wavetables).
///
/// @par Memory
/// Approximately 360 KB per instance (4 operators with wavetables).
/// For polyphony, consider voice sharing at a higher level.
class FMVoice {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumOperators = 4;
    static constexpr size_t kNumAlgorithms = static_cast<size_t>(Algorithm::kNumAlgorithms);

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor (FR-001).
    ///
    /// Initializes to safe silence state:
    /// - All operators at zero frequency, zero level
    /// - Algorithm 0 (Stacked2Op) selected
    /// - Unprepared state
    ///
    /// process() returns 0.0 until prepare() is called (FR-026).
    FMVoice() noexcept;

    /// @brief Initialize the voice for the given sample rate (FR-002).
    ///
    /// Initializes all 4 operators and the DC blocker. All internal state
    /// is reset (phases, feedback history).
    ///
    /// @param sampleRate Sample rate in Hz (must be > 0)
    ///
    /// @note NOT real-time safe (generates wavetables via FFT)
    /// @note Calling prepare() multiple times is safe; state is fully reset
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all operator phases while preserving configuration (FR-003).
    ///
    /// After reset():
    /// - All operator phases start from 0
    /// - All feedback history cleared
    /// - Algorithm, frequency, ratios, levels preserved
    ///
    /// Use on note-on for clean attack in polyphonic context.
    ///
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Algorithm Selection (FR-004, FR-005, FR-005a, FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Select the active algorithm (FR-005).
    ///
    /// Changes the routing topology. Phase preservation is guaranteed (FR-005a):
    /// operators continue oscillating with only routing changed.
    ///
    /// @param algorithm The algorithm to select (0-7)
    ///
    /// @note Invalid values (>= kNumAlgorithms) are ignored (preserve previous)
    /// @note Change takes effect on next process() call
    /// @note Real-time safe
    void setAlgorithm(Algorithm algorithm) noexcept;

    /// @brief Get the current algorithm.
    [[nodiscard]] Algorithm getAlgorithm() const noexcept;

    // =========================================================================
    // Voice Control (FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Set the voice base frequency in Hz (FR-015).
    ///
    /// For operators in Ratio mode (FR-016): effective freq = base * ratio
    /// For operators in Fixed mode (FR-017): effective freq = fixed (ignores base)
    ///
    /// @param hz Frequency in Hz
    ///
    /// @note NaN/Inf inputs sanitized to 0 Hz
    /// @note Real-time safe
    void setFrequency(float hz) noexcept;

    /// @brief Get the current base frequency.
    [[nodiscard]] float getFrequency() const noexcept;

    // =========================================================================
    // Operator Configuration (FR-009, FR-010, FR-011, FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Set operator frequency ratio (FR-010).
    ///
    /// Only effective when operator is in Ratio mode.
    ///
    /// @param opIndex Operator index (0-3)
    /// @param ratio Frequency multiplier, clamped to [0.0, 16.0]
    ///
    /// @note Invalid opIndex silently ignored
    /// @note NaN/Inf inputs ignored (preserve previous)
    /// @note Real-time safe
    void setOperatorRatio(size_t opIndex, float ratio) noexcept;

    /// @brief Get operator frequency ratio.
    [[nodiscard]] float getOperatorRatio(size_t opIndex) const noexcept;

    /// @brief Set operator output level (FR-011).
    ///
    /// @param opIndex Operator index (0-3)
    /// @param level Output amplitude, clamped to [0.0, 1.0]
    ///
    /// @note Real-time safe
    void setOperatorLevel(size_t opIndex, float level) noexcept;

    /// @brief Get operator output level.
    [[nodiscard]] float getOperatorLevel(size_t opIndex) const noexcept;

    /// @brief Set operator frequency mode (FR-013).
    ///
    /// @param opIndex Operator index (0-3)
    /// @param mode Ratio (frequency tracking) or Fixed (absolute frequency)
    ///
    /// @note Mode change is glitch-free
    /// @note Real-time safe
    void setOperatorMode(size_t opIndex, OperatorMode mode) noexcept;

    /// @brief Get operator frequency mode.
    [[nodiscard]] OperatorMode getOperatorMode(size_t opIndex) const noexcept;

    /// @brief Set operator fixed frequency (FR-014).
    ///
    /// Only effective when operator is in Fixed mode.
    ///
    /// @param opIndex Operator index (0-3)
    /// @param hz Absolute frequency in Hz
    ///
    /// @note NaN/Inf inputs ignored (preserve previous)
    /// @note Real-time safe
    void setOperatorFixedFrequency(size_t opIndex, float hz) noexcept;

    /// @brief Get operator fixed frequency.
    [[nodiscard]] float getOperatorFixedFrequency(size_t opIndex) const noexcept;

    /// @brief Set feedback amount for the designated operator (FR-012).
    ///
    /// The feedback-enabled operator is determined by the current algorithm.
    /// Only the designated operator uses feedback; others ignore this setting.
    ///
    /// @param amount Feedback intensity, clamped to [0.0, 1.0]
    ///
    /// @note Soft-limited via tanh to prevent instability (FR-023)
    /// @note Real-time safe
    void setFeedback(float amount) noexcept;

    /// @brief Get the current feedback amount.
    [[nodiscard]] float getFeedback() const noexcept;

    // =========================================================================
    // Processing (FR-018, FR-019, FR-020, FR-021, FR-022, FR-026)
    // =========================================================================

    /// @brief Generate one mono output sample (FR-018).
    ///
    /// @return Output sample, normalized by carrier count and DC-blocked
    ///
    /// @note Returns 0.0 if prepare() has not been called (FR-026)
    /// @note Operators processed in dependency order (FR-021)
    /// @note Modulator outputs passed as phase modulation (FR-022)
    /// @note Output normalized by carrier count (FR-020)
    /// @note Real-time safe
    [[nodiscard]] float process() noexcept;

    /// @brief Generate a block of samples (FR-019).
    ///
    /// Equivalent to calling process() for each sample.
    ///
    /// @param output Output buffer (must have numSamples capacity)
    /// @param numSamples Number of samples to generate
    ///
    /// @note Real-time safe
    void processBlock(float* output, size_t numSamples) noexcept;

private:
    // Internal per-operator configuration
    struct OperatorConfig {
        OperatorMode mode = OperatorMode::Ratio;
        float ratio = 1.0f;
        float fixedFrequency = 440.0f;
    };

    // Sub-components
    std::array<FMOperator, kNumOperators> operators_;
    std::array<OperatorConfig, kNumOperators> configs_;
    DCBlocker dcBlocker_;

    // Parameters
    Algorithm currentAlgorithm_ = Algorithm::Stacked2Op;
    float baseFrequency_ = 440.0f;
    float feedbackAmount_ = 0.0f;

    // State
    double sampleRate_ = 0.0;
    bool prepared_ = false;

    // Internal helpers
    [[nodiscard]] static float sanitize(float x) noexcept;
    void updateOperatorFrequencies() noexcept;
};

} // namespace Krate::DSP
