// ==============================================================================
// Layer 2: DSP Processor - Temporal Distortion
// ==============================================================================
// A distortion processor where the waveshaper drive changes based on signal
// history, creating dynamics-aware saturation that "feels alive" compared
// to static waveshaping.
//
// Four temporal modes are supported:
// 1. EnvelopeFollow: Drive increases with amplitude (guitar amp behavior)
// 2. InverseEnvelope: Drive increases as amplitude decreases (expansion effect)
// 3. Derivative: Drive modulated by rate of change (transient emphasis)
// 4. Hysteresis: Drive depends on signal trajectory (path-dependent behavior)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 2 (depends only on Layers 0-1)
// - Principle X: DSP Constraints (denormal flushing, edge case handling)
// - Principle XII: Test-First Development
//
// Reference: specs/107-temporal-distortion/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// TemporalMode Enumeration (FR-001)
// =============================================================================

/// @brief Temporal distortion mode selection.
///
/// Controls how waveshaper drive is modulated based on signal history.
/// Each mode creates different dynamics-aware distortion character.
enum class TemporalMode : uint8_t {
    /// @brief Drive increases with input amplitude (FR-010, FR-011)
    /// Louder signals get more distortion - classic dynamics-responsive behavior.
    /// At reference level (-12 dBFS RMS), drive equals base drive.
    EnvelopeFollow = 0,

    /// @brief Drive increases as input amplitude decreases (FR-012, FR-013)
    /// Quieter signals get more distortion - expansion-style effect.
    /// Capped at safe maximum (20.0) to prevent instability on silence.
    InverseEnvelope = 1,

    /// @brief Drive modulated by rate of amplitude change (FR-014, FR-015)
    /// Transients get more distortion, sustained signals stay cleaner.
    /// Uses highpass filter on envelope for smooth derivative.
    Derivative = 2,

    /// @brief Drive depends on recent signal trajectory (FR-016, FR-017)
    /// Rising and falling signals processed differently.
    /// Memory state decays exponentially toward neutral.
    Hysteresis = 3
};

// =============================================================================
// TemporalDistortion Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Memory-based distortion with dynamic drive.
///
/// A distortion processor where the waveshaper drive changes based on signal
/// history, creating dynamics-aware saturation that "feels alive" compared
/// to static waveshaping.
///
/// @par Features
/// - Four temporal modes: EnvelopeFollow, InverseEnvelope, Derivative, Hysteresis
/// - All 9 waveshape types (Tanh, Atan, Cubic, Quintic, etc.)
/// - Configurable envelope attack/release (0.1-500ms / 1-5000ms)
/// - Drive modulation depth control (0-100%)
/// - Hysteresis-specific depth and decay parameters
/// - Mode switching without artifacts (zipper-free)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layers 0-1)
/// - Principle X: DSP Constraints (denormal flushing, edge case handling)
///
/// @par Usage Example
/// @code
/// TemporalDistortion distortion;
/// distortion.prepare(44100.0, 512);
/// distortion.setMode(TemporalMode::EnvelopeFollow);
/// distortion.setBaseDrive(2.0f);
/// distortion.setDriveModulation(0.5f);
/// distortion.setAttackTime(10.0f);
/// distortion.setReleaseTime(100.0f);
/// distortion.setWaveshapeType(WaveshapeType::Tanh);
///
/// // Sample-by-sample
/// float output = distortion.processSample(input);
///
/// // Block processing
/// distortion.processBlock(buffer, numSamples);
/// @endcode
///
/// @see specs/107-temporal-distortion/spec.md
/// @see EnvelopeFollower for envelope tracking
/// @see Waveshaper for saturation curves
class TemporalDistortion {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @name Core Parameter Ranges
    /// @{
    static constexpr float kMinBaseDrive = 0.0f;
    static constexpr float kMaxBaseDrive = 10.0f;
    static constexpr float kDefaultBaseDrive = 1.0f;

    static constexpr float kMinDriveModulation = 0.0f;
    static constexpr float kMaxDriveModulation = 1.0f;
    static constexpr float kDefaultDriveModulation = 0.5f;

    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kDefaultAttackMs = 10.0f;

    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;
    /// @}

    /// @name Hysteresis Parameter Ranges (FR-008, FR-009)
    /// @{
    static constexpr float kMinHysteresisDepth = 0.0f;
    static constexpr float kMaxHysteresisDepth = 1.0f;
    static constexpr float kDefaultHysteresisDepth = 0.5f;

    static constexpr float kMinHysteresisDecayMs = 1.0f;
    static constexpr float kMaxHysteresisDecayMs = 500.0f;
    static constexpr float kDefaultHysteresisDecayMs = 50.0f;
    /// @}

    /// @name Internal Constants
    /// @{
    static constexpr float kReferenceLevel = 0.251189f;   ///< -12 dBFS RMS
    static constexpr float kMaxSafeDrive = 20.0f;         ///< InverseEnvelope cap (2x max base drive)
    static constexpr float kEnvelopeFloor = 0.001f;       ///< Div-by-zero protection
    static constexpr float kDerivativeFilterHz = 10.0f;   ///< Derivative HPF cutoff (chosen from 5-20 Hz range)
    static constexpr float kDerivativeSensitivity = 10.0f; ///< Normalizes derivative scale for musical response
    static constexpr float kDriveSmoothingMs = 5.0f;      ///< Zipper prevention (validated by SC-007)
    /// @}

    // =========================================================================
    // Construction
    // =========================================================================

    /// @brief Default constructor.
    /// Initializes with default parameters. prepare() must be called before processing.
    TemporalDistortion() noexcept = default;

    // Non-copyable (contains component state)
    TemporalDistortion(const TemporalDistortion&) = delete;
    TemporalDistortion& operator=(const TemporalDistortion&) = delete;

    // Movable
    TemporalDistortion(TemporalDistortion&&) noexcept = default;
    TemporalDistortion& operator=(TemporalDistortion&&) noexcept = default;

    ~TemporalDistortion() = default;

    // =========================================================================
    // Lifecycle (FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Prepare processor for given sample rate. (FR-021)
    ///
    /// Initializes all components (envelope follower, filters, smoothers).
    /// Must be called before any processing and when sample rate changes.
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000 typical)
    /// @param maxBlockSize Maximum samples per processBlock() call
    /// @pre sampleRate > 0
    /// @post ready for processing via processSample()/processBlock()
    /// @note NOT real-time safe (may allocate component state)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Not needed for this processor
        sampleRate_ = sampleRate;

        // Initialize envelope follower in RMS mode
        envelope_.prepare(sampleRate, maxBlockSize);
        envelope_.setMode(DetectionMode::RMS);
        envelope_.setAttackTime(attackTimeMs_);
        envelope_.setReleaseTime(releaseTimeMs_);

        // Initialize derivative filter (highpass for rate of change)
        derivativeFilter_.prepare(sampleRate);
        derivativeFilter_.setCutoff(kDerivativeFilterHz);

        // Initialize drive smoother for zipper-free transitions
        driveSmoother_.configure(kDriveSmoothingMs, static_cast<float>(sampleRate));
        driveSmoother_.snapTo(baseDrive_);

        // Calculate hysteresis decay coefficient
        updateHysteresisCoefficient();

        prepared_ = true;
        reset();
    }

    /// @brief Reset all internal state without reallocation. (FR-022)
    ///
    /// Clears envelope, hysteresis memory, and filter state.
    /// Call when starting new audio stream or after discontinuity.
    ///
    /// @note Real-time safe (no allocation)
    void reset() noexcept {
        envelope_.reset();
        derivativeFilter_.reset();
        driveSmoother_.reset();
        driveSmoother_.snapTo(baseDrive_);

        // Clear hysteresis state
        hysteresisState_ = 0.0f;
        prevEnvelope_ = 0.0f;
    }

    // =========================================================================
    // Processing (FR-018, FR-019, FR-020, FR-024, FR-025, FR-026)
    // =========================================================================

    /// @brief Process a single sample. (FR-018)
    ///
    /// Tracks envelope, calculates mode-dependent drive, applies waveshaping.
    ///
    /// @param x Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Returns input unchanged if prepare() not called (FR-023)
    /// @note Real-time safe: noexcept, no allocation (FR-024, FR-025)
    [[nodiscard]] float processSample(float x) noexcept {
        // FR-023: Return input unchanged if not prepared
        if (!prepared_) {
            return x;
        }

        // FR-027: Handle NaN/Inf input
        if (detail::isNaN(x) || detail::isInf(x)) {
            reset();
            return 0.0f;
        }

        // FR-029: Zero base drive outputs silence
        if (baseDrive_ == 0.0f) {
            return 0.0f;
        }

        // Track envelope (RMS mode)
        const float currentEnvelope = envelope_.processSample(x);

        // Calculate mode-dependent effective drive
        float effectiveDrive = calculateEffectiveDrive(currentEnvelope);

        // Clamp effective drive to valid range (must be >= 0)
        effectiveDrive = std::max(0.0f, effectiveDrive);

        // Smooth drive to prevent zipper noise (FR-002)
        driveSmoother_.setTarget(effectiveDrive);
        const float smoothedDrive = driveSmoother_.process();

        // Apply waveshaping with smoothed drive
        waveshaper_.setDrive(smoothedDrive);
        float output = waveshaper_.process(x);

        // Update state for next sample (hysteresis mode uses this)
        prevEnvelope_ = currentEnvelope;

        // Flush denormals (FR-026)
        output = detail::flushDenormal(output);

        return output;
    }

    /// @brief Process a block of samples in-place. (FR-019)
    ///
    /// Equivalent to calling processSample() for each sample sequentially.
    /// Produces bit-identical output to equivalent sequential processing (FR-020).
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocation
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = processSample(buffer[i]);
        }
    }

    // =========================================================================
    // Mode Selection (FR-001, FR-002)
    // =========================================================================

    /// @brief Set temporal distortion mode. (FR-001)
    ///
    /// Switching modes during processing is artifact-free due to drive
    /// smoothing (FR-002).
    ///
    /// @param mode Temporal mode (EnvelopeFollow, InverseEnvelope, Derivative, Hysteresis)
    void setMode(TemporalMode mode) noexcept {
        mode_ = mode;
    }

    /// @brief Get current temporal mode.
    [[nodiscard]] TemporalMode getMode() const noexcept {
        return mode_;
    }

    // =========================================================================
    // Core Parameters (FR-003, FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Set base drive amount. (FR-003)
    ///
    /// Controls drive at reference level. Higher values = more saturation.
    ///
    /// @param drive Drive amount, clamped to [0.0, 10.0]
    void setBaseDrive(float drive) noexcept {
        baseDrive_ = std::clamp(drive, kMinBaseDrive, kMaxBaseDrive);
    }

    /// @brief Get current base drive.
    [[nodiscard]] float getBaseDrive() const noexcept {
        return baseDrive_;
    }

    /// @brief Set drive modulation amount. (FR-004)
    ///
    /// Controls how much envelope affects drive (0 = static waveshaping).
    ///
    /// @param amount Modulation depth, clamped to [0.0, 1.0]
    void setDriveModulation(float amount) noexcept {
        driveModulation_ = std::clamp(amount, kMinDriveModulation, kMaxDriveModulation);
    }

    /// @brief Get current drive modulation amount.
    [[nodiscard]] float getDriveModulation() const noexcept {
        return driveModulation_;
    }

    /// @brief Set envelope attack time. (FR-005)
    ///
    /// How quickly envelope responds to increasing amplitude.
    ///
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttackTime(float ms) noexcept {
        attackTimeMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        envelope_.setAttackTime(attackTimeMs_);
    }

    /// @brief Get current attack time in milliseconds.
    [[nodiscard]] float getAttackTime() const noexcept {
        return attackTimeMs_;
    }

    /// @brief Set envelope release time. (FR-006)
    ///
    /// How quickly envelope responds to decreasing amplitude.
    ///
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setReleaseTime(float ms) noexcept {
        releaseTimeMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
        envelope_.setReleaseTime(releaseTimeMs_);
    }

    /// @brief Get current release time in milliseconds.
    [[nodiscard]] float getReleaseTime() const noexcept {
        return releaseTimeMs_;
    }

    /// @brief Set saturation curve type. (FR-007)
    ///
    /// @param type Waveshape type from existing WaveshapeType enum
    void setWaveshapeType(WaveshapeType type) noexcept {
        waveshaper_.setType(type);
    }

    /// @brief Get current waveshape type.
    [[nodiscard]] WaveshapeType getWaveshapeType() const noexcept {
        return waveshaper_.getType();
    }

    // =========================================================================
    // Hysteresis Parameters (FR-008, FR-009)
    // =========================================================================

    /// @brief Set hysteresis depth. (FR-008)
    ///
    /// How much signal history affects processing (Hysteresis mode only).
    ///
    /// @param depth Depth amount, clamped to [0.0, 1.0]
    void setHysteresisDepth(float depth) noexcept {
        hysteresisDepth_ = std::clamp(depth, kMinHysteresisDepth, kMaxHysteresisDepth);
    }

    /// @brief Get current hysteresis depth.
    [[nodiscard]] float getHysteresisDepth() const noexcept {
        return hysteresisDepth_;
    }

    /// @brief Set hysteresis decay time. (FR-009)
    ///
    /// How fast memory fades when input is silent (Hysteresis mode only).
    /// Memory settles within approximately 5x this time.
    ///
    /// @param ms Decay time in milliseconds, clamped to [1, 500]
    void setHysteresisDecay(float ms) noexcept {
        hysteresisDecayMs_ = std::clamp(ms, kMinHysteresisDecayMs, kMaxHysteresisDecayMs);
        updateHysteresisCoefficient();
    }

    /// @brief Get current hysteresis decay time in milliseconds.
    [[nodiscard]] float getHysteresisDecay() const noexcept {
        return hysteresisDecayMs_;
    }

    // =========================================================================
    // Info (SC-009)
    // =========================================================================

    /// @brief Get processing latency in samples. (SC-009)
    /// @return Always 0 (no lookahead required)
    [[nodiscard]] constexpr size_t getLatency() const noexcept { return 0; }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Calculate mode-dependent effective drive from envelope.
    ///
    /// Each mode calculates drive differently based on envelope and parameters.
    [[nodiscard]] float calculateEffectiveDrive(float envelope) noexcept {
        // FR-028: Zero drive modulation produces static waveshaping
        if (driveModulation_ == 0.0f) {
            return baseDrive_;
        }

        switch (mode_) {
            case TemporalMode::EnvelopeFollow: {
                // FR-010, FR-011: Drive increases with amplitude
                // At reference level, drive equals base drive
                const float ratio = envelope / kReferenceLevel;
                return baseDrive_ * (1.0f + driveModulation_ * (ratio - 1.0f));
            }

            case TemporalMode::InverseEnvelope: {
                // FR-012, FR-013: Drive increases as amplitude decreases
                // Use floor to prevent divide-by-zero
                const float safeEnv = std::max(envelope, kEnvelopeFloor);
                const float ratio = kReferenceLevel / safeEnv;
                const float drive = baseDrive_ * (1.0f + driveModulation_ * (ratio - 1.0f));
                // Cap at safe maximum to prevent instability
                return std::min(drive, kMaxSafeDrive);
            }

            case TemporalMode::Derivative: {
                // FR-014, FR-015: Drive proportional to rate of change
                // Apply highpass filter to get derivative of envelope
                const float derivative = derivativeFilter_.process(envelope);
                const float absDerivative = std::abs(derivative);
                return baseDrive_ * (1.0f + driveModulation_ * absDerivative * kDerivativeSensitivity);
            }

            case TemporalMode::Hysteresis: {
                // FR-016, FR-017: Drive depends on signal history
                // Calculate delta from previous envelope
                const float delta = envelope - prevEnvelope_;

                // Update hysteresis state with exponential decay
                hysteresisState_ = hysteresisState_ * hysteresisDecayCoeff_ + delta;
                hysteresisState_ = detail::flushDenormal(hysteresisState_);

                return baseDrive_ * (1.0f + hysteresisDepth_ * hysteresisState_ * driveModulation_);
            }

            default:
                return baseDrive_;
        }
    }

    /// @brief Update hysteresis decay coefficient from decay time and sample rate.
    void updateHysteresisCoefficient() noexcept {
        if (sampleRate_ <= 0.0) {
            hysteresisDecayCoeff_ = 0.0f;
            return;
        }
        // Calculate decay coefficient for exponential decay
        // Time constant: tau = decayMs / 5 (to settle in ~5x decay time)
        const float tau = hysteresisDecayMs_ * 0.001f / 5.0f;
        const float samplesPerTau = static_cast<float>(sampleRate_) * tau;
        if (samplesPerTau > 0.0f) {
            hysteresisDecayCoeff_ = detail::constexprExp(-1.0f / samplesPerTau);
        } else {
            hysteresisDecayCoeff_ = 0.0f;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Processing components
    EnvelopeFollower envelope_;      ///< Amplitude envelope tracker (RMS mode)
    Waveshaper waveshaper_;          ///< Saturation with variable drive
    OnePoleHP derivativeFilter_;     ///< Rate of change for Derivative mode
    OnePoleSmoother driveSmoother_;  ///< Zipper-free drive changes

    // Parameters
    TemporalMode mode_ = TemporalMode::EnvelopeFollow;
    float baseDrive_ = kDefaultBaseDrive;
    float driveModulation_ = kDefaultDriveModulation;
    float attackTimeMs_ = kDefaultAttackMs;
    float releaseTimeMs_ = kDefaultReleaseMs;
    float hysteresisDepth_ = kDefaultHysteresisDepth;
    float hysteresisDecayMs_ = kDefaultHysteresisDecayMs;

    // Hysteresis state
    float hysteresisState_ = 0.0f;      ///< Accumulated signal trajectory
    float prevEnvelope_ = 0.0f;         ///< Previous envelope for delta
    float hysteresisDecayCoeff_ = 0.0f; ///< Calculated decay coefficient

    // Runtime state
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
