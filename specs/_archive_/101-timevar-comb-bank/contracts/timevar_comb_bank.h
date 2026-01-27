// ==============================================================================
// Layer 3: System Component - TimeVaryingCombBank
// API Contract / Interface Definition
// ==============================================================================
// Bank of up to 8 comb filters with independently modulated delay times.
// Creates evolving metallic and resonant textures.
//
// Feature: 101-timevar-comb-bank
// Layer: 3 (Systems)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 3 (composes Layer 0-1 components)
// - Principle X: DSP Constraints (linear interpolation for modulated delays)
// - Principle XI: Performance Budget (<1% CPU single core at 44.1kHz)
//
// Reference: specs/101-timevar-comb-bank/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/comb_filter.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Tuning Mode Enumeration (FR-006)
// =============================================================================

/// @brief Tuning mode for automatic delay time calculation.
enum class Tuning : uint8_t {
    Harmonic,    ///< f[n] = fundamental * (n+1) - musical harmonic series
    Inharmonic,  ///< f[n] = fundamental * sqrt(1 + n*spread) - bell-like partials
    Custom       ///< Manual per-comb delay times via setCombDelay()
};

// =============================================================================
// TimeVaryingCombBank Class
// =============================================================================

/// @brief Bank of up to 8 comb filters with independently modulated delay times.
///
/// Creates evolving metallic and resonant textures by modulating each comb
/// filter's delay time with independent LFOs and optional random drift.
/// Supports automatic harmonic/inharmonic tuning from a fundamental frequency.
///
/// @par Architecture
/// Layer 3 System Component composing:
/// - FeedbackComb x8 (Layer 1) - Core comb filters with damping
/// - LFO x8 (Layer 1) - Per-comb modulation oscillators
/// - OnePoleSmoother x32 (Layer 1) - Parameter smoothing (4 per comb)
/// - Xorshift32 x8 (Layer 0) - Per-comb random drift generators
///
/// @par Signal Flow
/// ```
/// Input -> [Sum for each active comb]:
///            +-> Comb[n] with modulated delay -> gain -> pan -> L/R sum
/// Output <- [L/R stereo output]
/// ```
///
/// @par Constitution Compliance
/// - Principle II: Real-time safe (noexcept, no allocations in process)
/// - Principle IX: Layer 3 depends only on Layers 0-1
/// - Principle X: Linear interpolation for modulated delays (not allpass)
/// - Principle XI: <1% CPU at 44.1kHz with 8 combs
///
/// @see FeedbackComb for individual comb filter implementation
/// @see FilterFeedbackMatrix for related multi-filter system pattern
class TimeVaryingCombBank {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum number of comb filters (compile-time limit)
    static constexpr size_t kMaxCombs = 8;

    /// Minimum fundamental frequency (corresponds to 50ms delay)
    static constexpr float kMinFundamental = 20.0f;

    /// Maximum fundamental frequency
    static constexpr float kMaxFundamental = 1000.0f;

    /// Minimum LFO modulation rate (FR-009)
    static constexpr float kMinModRate = 0.01f;

    /// Maximum LFO modulation rate (FR-009)
    static constexpr float kMaxModRate = 20.0f;

    /// Minimum modulation depth (FR-009)
    static constexpr float kMinModDepth = 0.0f;

    /// Maximum modulation depth as percentage (FR-009)
    static constexpr float kMaxModDepth = 100.0f;

    /// Smoothing time for delay parameter changes (FR-019)
    static constexpr float kDelaySmoothingMs = 20.0f;

    /// Smoothing time for feedback parameter changes (FR-019)
    static constexpr float kFeedbackSmoothingMs = 10.0f;

    /// Smoothing time for damping parameter changes (FR-019)
    static constexpr float kDampingSmoothingMs = 10.0f;

    /// Smoothing time for gain parameter changes (FR-019)
    static constexpr float kGainSmoothingMs = 5.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor.
    /// Creates an unprepared comb bank. Call prepare() before processing.
    TimeVaryingCombBank() noexcept;

    /// @brief Destructor.
    ~TimeVaryingCombBank() = default;

    // Non-copyable (contains move-only DelayLines via FeedbackComb)
    TimeVaryingCombBank(const TimeVaryingCombBank&) = delete;
    TimeVaryingCombBank& operator=(const TimeVaryingCombBank&) = delete;

    // Movable
    TimeVaryingCombBank(TimeVaryingCombBank&&) noexcept = default;
    TimeVaryingCombBank& operator=(TimeVaryingCombBank&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-015, FR-016)
    // =========================================================================

    /// @brief Prepare for processing at the given sample rate.
    ///
    /// Allocates delay line buffers and configures all internal components.
    /// Must be called before process() or processStereo().
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000 typical)
    /// @param maxDelayMs Maximum delay time in milliseconds (default 50ms)
    ///
    /// @note This is the ONLY method that may allocate memory.
    /// @note Safe to call multiple times (reconfigures for new sample rate).
    /// @note FR-015: Allocation failures are handled gracefully.
    void prepare(double sampleRate, float maxDelayMs = 50.0f) noexcept;

    /// @brief Clear all internal state without changing parameters.
    ///
    /// Clears delay lines, LFOs, and random generators.
    /// Call when starting a new audio region to prevent artifacts.
    ///
    /// @note FR-016: Also resets random generators for reproducible behavior.
    void reset() noexcept;

    /// @brief Check if the comb bank has been prepared.
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Comb Configuration (FR-001, FR-002, FR-003, FR-004, FR-005)
    // =========================================================================

    /// @brief Set the number of active comb filters.
    ///
    /// @param count Number of combs to process (clamped to [1, kMaxCombs])
    ///
    /// @note FR-001: Runtime adjustable, 1-8 combs
    /// @note Inactive combs are not processed (CPU optimization)
    void setNumCombs(size_t count) noexcept;

    /// @brief Get the current number of active combs.
    [[nodiscard]] size_t getNumCombs() const noexcept;

    /// @brief Set delay time for a specific comb in milliseconds.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param ms Delay time in milliseconds (clamped to [1, maxDelayMs])
    ///
    /// @note FR-002: Implicitly switches to Custom tuning mode
    void setCombDelay(size_t index, float ms) noexcept;

    /// @brief Set feedback amount for a specific comb.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param amount Feedback amount (clamped to [-0.9999, 0.9999])
    ///
    /// @note FR-003: Positive = normal resonance, negative = inverted phase
    void setCombFeedback(size_t index, float amount) noexcept;

    /// @brief Set damping (lowpass in feedback) for a specific comb.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param amount Damping amount (clamped to [0.0, 1.0])
    ///               0.0 = bright (no HF rolloff), 1.0 = dark (maximum HF rolloff)
    ///
    /// @note FR-004: One-pole lowpass in feedback path
    void setCombDamping(size_t index, float amount) noexcept;

    /// @brief Set output gain for a specific comb in decibels.
    ///
    /// @param index Comb index (0 to kMaxCombs-1)
    /// @param dB Gain in decibels (converted to linear internally)
    ///
    /// @note FR-005: No hard limit, converted via dbToGain()
    void setCombGain(size_t index, float dB) noexcept;

    // =========================================================================
    // Tuning Configuration (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set the tuning mode for automatic delay calculation.
    ///
    /// @param mode Tuning mode (Harmonic, Inharmonic, or Custom)
    ///
    /// @note FR-006: Changing to Harmonic/Inharmonic recalculates delays
    /// @note Changing to Custom preserves current delay values
    void setTuningMode(Tuning mode) noexcept;

    /// @brief Get the current tuning mode.
    [[nodiscard]] Tuning getTuningMode() const noexcept;

    /// @brief Set fundamental frequency for automatic tuning.
    ///
    /// @param hz Fundamental frequency in Hz (clamped to [20, 1000])
    ///
    /// @note FR-007: Only affects Harmonic and Inharmonic modes
    /// @note Harmonic: f[n] = fundamental * (n+1)
    /// @note Inharmonic: f[n] = fundamental * sqrt(1 + n*spread)
    void setFundamental(float hz) noexcept;

    /// @brief Get the current fundamental frequency.
    [[nodiscard]] float getFundamental() const noexcept;

    /// @brief Set the inharmonic spread factor.
    ///
    /// @param amount Spread amount (clamped to [0.0, 1.0])
    ///               0.0 = harmonic ratios, 1.0 = maximum inharmonicity
    ///
    /// @note FR-008: Only affects Inharmonic mode
    void setSpread(float amount) noexcept;

    /// @brief Get the current spread factor.
    [[nodiscard]] float getSpread() const noexcept;

    // =========================================================================
    // Modulation Configuration (FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set the global LFO modulation rate.
    ///
    /// @param hz Modulation rate in Hz (clamped to [0.01, 20.0])
    ///
    /// @note FR-009: Applied to all comb LFOs
    void setModRate(float hz) noexcept;

    /// @brief Get the current modulation rate.
    [[nodiscard]] float getModRate() const noexcept;

    /// @brief Set the modulation depth as a percentage.
    ///
    /// @param percent Modulation depth (clamped to [0.0, 100.0])
    ///                0% = no modulation, 100% = +/-100% of base delay
    ///
    /// @note FR-009: Delay varies by +/- (depth/100) * baseDelay
    void setModDepth(float percent) noexcept;

    /// @brief Get the current modulation depth (percentage).
    [[nodiscard]] float getModDepth() const noexcept;

    /// @brief Set the phase spread between adjacent comb LFOs.
    ///
    /// @param degrees Phase spread in degrees (wrapped to [0, 360))
    ///                Each comb gets: basePhase + index * phaseSpread
    ///
    /// @note FR-010: Creates stereo/spatial movement effects
    void setModPhaseSpread(float degrees) noexcept;

    /// @brief Get the current phase spread (degrees).
    [[nodiscard]] float getModPhaseSpread() const noexcept;

    /// @brief Set the random drift modulation amount.
    ///
    /// @param amount Random amount (clamped to [0.0, 1.0])
    ///               0.0 = no random drift, 1.0 = maximum drift
    ///
    /// @note FR-011: Adds organic variation using Xorshift32 PRNG
    void setRandomModulation(float amount) noexcept;

    /// @brief Get the current random modulation amount.
    [[nodiscard]] float getRandomModulation() const noexcept;

    // =========================================================================
    // Stereo Configuration (FR-012)
    // =========================================================================

    /// @brief Set the stereo spread amount.
    ///
    /// @param amount Spread amount (clamped to [0.0, 1.0])
    ///               0.0 = all combs centered, 1.0 = full L-R distribution
    ///
    /// @note FR-012: Combs are distributed L to R based on index
    void setStereoSpread(float amount) noexcept;

    /// @brief Get the current stereo spread.
    [[nodiscard]] float getStereoSpread() const noexcept;

    // =========================================================================
    // Processing Methods (FR-013, FR-014, FR-017, FR-020)
    // =========================================================================

    /// @brief Process a single mono sample.
    ///
    /// @param input Input sample
    /// @return Summed output from all active combs
    ///
    /// @note FR-013: Mono processing, combs summed equally
    /// @note FR-017: Real-time safe (noexcept, no allocations)
    /// @note FR-020: NaN/Inf in any comb resets that comb, returns 0 for it
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process stereo samples in-place.
    ///
    /// @param left Left channel sample (input and output)
    /// @param right Right channel sample (input and output)
    ///
    /// @note FR-014: Applies pan distribution per comb
    /// @note FR-017: Real-time safe
    /// @note FR-020: NaN/Inf handling per comb
    void processStereo(float& left, float& right) noexcept;

private:
    // Internal struct forward declaration
    struct CombChannel;

    // Per-comb state array
    std::array<CombChannel, kMaxCombs> channels_;

    // Global parameters
    size_t numCombs_ = 4;
    Tuning tuningMode_ = Tuning::Harmonic;
    float fundamental_ = 100.0f;
    float spread_ = 0.0f;
    float modRate_ = 1.0f;
    float modDepth_ = 0.0f;      // Stored as fraction [0, 1]
    float modPhaseSpread_ = 0.0f;
    float randomModAmount_ = 0.0f;
    float stereoSpread_ = 0.0f;

    // Runtime state
    double sampleRate_ = 44100.0;
    float maxDelayMs_ = 50.0f;
    bool prepared_ = false;

    // Internal helpers
    void recalculateTunedDelays() noexcept;
    void recalculatePanPositions() noexcept;
    void recalculateLfoPhases() noexcept;
    [[nodiscard]] float computeHarmonicDelay(size_t index) const noexcept;
    [[nodiscard]] float computeInharmonicDelay(size_t index) const noexcept;
};

// =============================================================================
// CombChannel Internal Structure
// =============================================================================

/// @brief Internal per-comb state (not part of public API).
struct TimeVaryingCombBank::CombChannel {
    FeedbackComb comb;
    LFO lfo;
    Xorshift32 rng{12345u};

    OnePoleSmoother delaySmoother;
    OnePoleSmoother feedbackSmoother;
    OnePoleSmoother dampingSmoother;
    OnePoleSmoother gainSmoother;

    float baseDelayMs = 10.0f;
    float feedbackTarget = 0.5f;
    float dampingTarget = 0.0f;
    float gainDb = 0.0f;
    float gainLinear = 1.0f;

    float pan = 0.0f;
    float panLeftGain = 0.707107f;
    float panRightGain = 0.707107f;
    float lfoPhaseOffset = 0.0f;
};

}  // namespace DSP
}  // namespace Krate
