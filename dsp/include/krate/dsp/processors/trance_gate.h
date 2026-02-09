// ==============================================================================
// Layer 2: Processor - Trance Gate (Rhythmic Energy Shaper)
// ==============================================================================
// Pattern-driven VCA that applies a repeating step pattern as multiplicative
// gain to an audio signal. Provides click-free transitions via asymmetric
// one-pole smoothing, Euclidean pattern generation, depth-controlled mixing,
// tempo-synced and free-running modes, and per-voice/global clock modes.
//
// Designed for placement post-distortion, pre-VCA in the Ruinae voice chain.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocation, no locks, no IO)
// - Principle III: Modern C++ (C++20, constexpr, std::array, RAII)
// - Principle IX: Layer 2 (depends on Layer 0: EuclideanPattern, NoteValue;
//                          Layer 1: OnePoleSmoother)
// - Principle X: DSP Constraints (per-sample smoothing, no oversampling needed)
//
// Reference: specs/039-trance-gate/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// GateStep (FR-001)
// =============================================================================

/// @brief A single step in the trance gate pattern.
///
/// Holds a float gain level in [0.0, 1.0], enabling nuanced patterns with
/// ghost notes, accents, and silence -- not just boolean on/off.
struct GateStep {
    float level{1.0f};  ///< Gain level: 0.0 = silence, 1.0 = full volume
};

// =============================================================================
// TranceGateParams (FR-001 through FR-012)
// =============================================================================

/// @brief Configuration parameters for the TranceGate processor.
///
/// Uses NoteValue/NoteModifier enums (Layer 0) for tempo sync, consistent
/// with SequencerCore and delay effects.
struct TranceGateParams {
    int numSteps{16};                                ///< Active steps: [2, 32]
    float rateHz{4.0f};                              ///< Free-run step rate in Hz [0.1, 100.0]
    float depth{1.0f};                               ///< Gate depth [0.0, 1.0]: 0 = bypass, 1 = full
    float attackMs{2.0f};                            ///< Attack ramp time [1.0, 20.0] ms
    float releaseMs{10.0f};                          ///< Release ramp time [1.0, 50.0] ms
    float phaseOffset{0.0f};                         ///< Pattern rotation [0.0, 1.0]
    bool tempoSync{true};                            ///< true = tempo sync, false = free-run
    NoteValue noteValue{NoteValue::Sixteenth};       ///< Step note value (tempo sync)
    NoteModifier noteModifier{NoteModifier::None};   ///< Step note modifier (tempo sync)
    bool perVoice{true};                             ///< true = reset on noteOn, false = free-run clock
};

// =============================================================================
// TranceGate Class (Layer 2 Processor)
// =============================================================================

/// @brief Rhythmic energy shaper -- pattern-driven VCA for amplitude gating.
///
/// Applies a repeating step pattern as a multiplicative gain to the input
/// signal, with per-sample exponential smoothing for click-free transitions.
/// Designed for placement post-distortion, pre-VCA in the Ruinae voice chain.
///
/// @par Key Features
/// - Float-level step patterns (0.0-1.0) for ghost notes and accents (FR-001)
/// - Asymmetric attack/release one-pole smoothing (FR-003)
/// - Depth control for subtle rhythmic motion (FR-004)
/// - Tempo-synced and free-running modes (FR-005, FR-006)
/// - Euclidean pattern generation via EuclideanPattern (L0) (FR-007)
/// - Modulation output: current gate envelope value (FR-008)
/// - Per-voice and global clock modes (FR-010)
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free (Constitution II).
class TranceGate {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMaxSteps = 32;        ///< Maximum pattern length
    static constexpr int kMinSteps = 2;         ///< Minimum pattern length
    static constexpr float kMinAttackMs = 1.0f;
    static constexpr float kMaxAttackMs = 20.0f;
    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 50.0f;
    static constexpr float kMinRateHz = 0.1f;
    static constexpr float kMaxRateHz = 100.0f;
    static constexpr double kMinTempoBPM = 20.0;
    static constexpr double kMaxTempoBPM = 300.0;
    static constexpr double kDefaultSampleRate = 44100.0;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. All steps default to 1.0 (passthrough).
    TranceGate() noexcept {
        pattern_.fill(1.0f);
        attackSmoother_.snapTo(1.0f);
        releaseSmoother_.snapTo(1.0f);
        currentGainValue_ = 1.0f;
        updateStepDuration();
        attackSmoother_.configure(params_.attackMs, static_cast<float>(sampleRate_));
        releaseSmoother_.configure(params_.releaseMs, static_cast<float>(sampleRate_));
    }

    /// @brief Prepare for processing at given sample rate.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        prepared_ = true;
        attackSmoother_.configure(params_.attackMs, static_cast<float>(sampleRate_));
        releaseSmoother_.configure(params_.releaseMs, static_cast<float>(sampleRate_));
        updateStepDuration();
    }

    /// @brief Reset gate state based on mode.
    void reset() noexcept {
        if (params_.perVoice) {
            sampleCounter_ = 0;
            currentStep_ = 0;
            const int effectiveStep = rotationOffset_ % numSteps_;
            const float level = pattern_[static_cast<size_t>(effectiveStep)];
            attackSmoother_.snapTo(level);
            releaseSmoother_.snapTo(level);
            currentGainValue_ = 1.0f + (level - 1.0f) * params_.depth;
        }
        // Global mode: no-op
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set all gate parameters at once.
    void setParams(const TranceGateParams& params) noexcept {
        const float clampedAttack = std::clamp(params.attackMs, kMinAttackMs, kMaxAttackMs);
        const float clampedRelease = std::clamp(params.releaseMs, kMinReleaseMs, kMaxReleaseMs);

        params_ = params;
        params_.numSteps = std::clamp(params.numSteps, kMinSteps, kMaxSteps);
        params_.depth = std::clamp(params.depth, 0.0f, 1.0f);
        params_.attackMs = clampedAttack;
        params_.releaseMs = clampedRelease;
        params_.rateHz = std::clamp(params.rateHz, kMinRateHz, kMaxRateHz);
        params_.phaseOffset = std::clamp(params.phaseOffset, 0.0f, 1.0f);

        numSteps_ = params_.numSteps;
        rotationOffset_ = static_cast<int>(params_.phaseOffset * static_cast<float>(numSteps_));

        attackSmoother_.configure(params_.attackMs, static_cast<float>(sampleRate_));
        releaseSmoother_.configure(params_.releaseMs, static_cast<float>(sampleRate_));
        updateStepDuration();
    }

    /// @brief Set free-run rate in Hz (only effective when tempoSync is off).
    void setRate(float hz) noexcept {
        params_.rateHz = std::clamp(hz, 0.1f, 100.0f);
        updateStepDuration();
    }

    /// @brief Set tempo in BPM. Called once per processing block.
    void setTempo(double bpm) noexcept {
        tempoBPM_ = std::clamp(bpm, kMinTempoBPM, kMaxTempoBPM);
        updateStepDuration();
    }

    // =========================================================================
    // Pattern Control
    // =========================================================================

    /// @brief Set a single step's level.
    void setStep(int index, float level) noexcept {
        if (index < 0 || index >= kMaxSteps) {
            return;
        }
        pattern_[static_cast<size_t>(index)] = std::clamp(level, 0.0f, 1.0f);
    }

    /// @brief Set the entire pattern from an array.
    void setPattern(const std::array<float, kMaxSteps>& pattern,
                    int numSteps) noexcept {
        numSteps_ = std::clamp(numSteps, kMinSteps, kMaxSteps);
        params_.numSteps = numSteps_;
        for (int i = 0; i < kMaxSteps; ++i) {
            pattern_[static_cast<size_t>(i)] = std::clamp(pattern[static_cast<size_t>(i)], 0.0f, 1.0f);
        }
        updateStepDuration();
    }

    /// @brief Generate a Euclidean pattern.
    void setEuclidean(int hits, int steps, int rotation = 0) noexcept {
        steps = std::clamp(steps, kMinSteps, kMaxSteps);
        numSteps_ = steps;
        params_.numSteps = steps;

        const uint32_t bitmask = EuclideanPattern::generate(hits, steps, rotation);
        for (int i = 0; i < steps; ++i) {
            pattern_[static_cast<size_t>(i)] = EuclideanPattern::isHit(bitmask, i, steps) ? 1.0f : 0.0f;
        }
        // Clear remaining steps
        for (int i = steps; i < kMaxSteps; ++i) {
            pattern_[static_cast<size_t>(i)] = 0.0f;
        }
        updateStepDuration();
    }

    // =========================================================================
    // Processing (FR-012, FR-013)
    // =========================================================================

    /// @brief Process a single sample.
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return input;  // FR-014: passthrough when not prepared
        }

        // Early-out when depth == 0.0 (bypass)
        if (params_.depth == 0.0f) {
            // Still advance timing to keep step position correct
            advanceTimingOnly();
            currentGainValue_ = 1.0f;
            return input;
        }

        // Advance sample counter and check step boundary
        sampleCounter_++;
        if (sampleCounter_ >= samplesPerStep_) {
            sampleCounter_ = 0;
            currentStep_ = (currentStep_ + 1) % numSteps_;
        }

        // Read effective step with phase offset
        const int effectiveStep = (currentStep_ + rotationOffset_) % numSteps_;
        const float targetLevel = pattern_[static_cast<size_t>(effectiveStep)];

        // Set target on both smoothers
        attackSmoother_.setTarget(targetLevel);
        releaseSmoother_.setTarget(targetLevel);

        // Select smoother based on direction
        float smoothedGain;
        if (targetLevel > attackSmoother_.getCurrentValue()) {
            // Rising: use attack smoother
            smoothedGain = attackSmoother_.process();
            releaseSmoother_.snapTo(smoothedGain);
        } else {
            // Falling or steady: use release smoother
            smoothedGain = releaseSmoother_.process();
            attackSmoother_.snapTo(smoothedGain);
        }

        // Apply depth: finalGain = lerp(1.0, smoothedGain, depth)
        const float finalGain = 1.0f + (smoothedGain - 1.0f) * params_.depth;
        currentGainValue_ = finalGain;

        return input * finalGain;
    }

    /// @brief Process a mono block in-place.
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    /// @brief Process a stereo block in-place.
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            const float gain = processGain();
            left[i] *= gain;
            right[i] *= gain;
        }
    }

    // =========================================================================
    // Queries (FR-008, FR-009)
    // =========================================================================

    /// @brief Get current smoothed, depth-adjusted gate value.
    [[nodiscard]] float getGateValue() const noexcept {
        return currentGainValue_;
    }

    /// @brief Get current step index.
    [[nodiscard]] int getCurrentStep() const noexcept {
        return currentStep_;
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Compute gain for one sample (used by stereo processBlock).
    [[nodiscard]] float processGain() noexcept {
        if (!prepared_) {
            currentGainValue_ = 1.0f;
            return 1.0f;
        }

        if (params_.depth == 0.0f) {
            advanceTimingOnly();
            currentGainValue_ = 1.0f;
            return 1.0f;
        }

        // Advance sample counter and check step boundary
        sampleCounter_++;
        if (sampleCounter_ >= samplesPerStep_) {
            sampleCounter_ = 0;
            currentStep_ = (currentStep_ + 1) % numSteps_;
        }

        const int effectiveStep = (currentStep_ + rotationOffset_) % numSteps_;
        const float targetLevel = pattern_[static_cast<size_t>(effectiveStep)];

        attackSmoother_.setTarget(targetLevel);
        releaseSmoother_.setTarget(targetLevel);

        float smoothedGain;
        if (targetLevel > attackSmoother_.getCurrentValue()) {
            smoothedGain = attackSmoother_.process();
            releaseSmoother_.snapTo(smoothedGain);
        } else {
            smoothedGain = releaseSmoother_.process();
            attackSmoother_.snapTo(smoothedGain);
        }

        const float finalGain = 1.0f + (smoothedGain - 1.0f) * params_.depth;
        currentGainValue_ = finalGain;

        return finalGain;
    }

    /// @brief Advance timing without computing gain (for bypass).
    void advanceTimingOnly() noexcept {
        sampleCounter_++;
        if (sampleCounter_ >= samplesPerStep_) {
            sampleCounter_ = 0;
            currentStep_ = (currentStep_ + 1) % numSteps_;
        }
    }

    /// @brief Recalculate step duration from current params/tempo.
    void updateStepDuration() noexcept {
        if (params_.tempoSync) {
            const float beatsPerNote = getBeatsForNote(params_.noteValue, params_.noteModifier);
            const double secondsPerBeat = 60.0 / tempoBPM_;
            samplesPerStep_ = static_cast<size_t>(secondsPerBeat * static_cast<double>(beatsPerNote) * sampleRate_);
        } else {
            const float clampedRate = std::clamp(params_.rateHz, kMinRateHz, kMaxRateHz);
            samplesPerStep_ = static_cast<size_t>(sampleRate_ / static_cast<double>(clampedRate));
        }
        // Ensure at least 1 sample per step
        if (samplesPerStep_ < 1) {
            samplesPerStep_ = 1;
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    std::array<float, kMaxSteps> pattern_{};     ///< Step levels
    int numSteps_{16};                           ///< Active step count
    int currentStep_{0};                         ///< Current step index
    size_t sampleCounter_{0};                    ///< Samples within current step
    size_t samplesPerStep_{5513};                ///< Calculated step duration
    double sampleRate_{kDefaultSampleRate};       ///< Sample rate in Hz
    double tempoBPM_{120.0};                     ///< Current tempo
    OnePoleSmoother attackSmoother_;              ///< Rising transition smoother
    OnePoleSmoother releaseSmoother_;             ///< Falling transition smoother
    float currentGainValue_{1.0f};               ///< Last computed gain
    TranceGateParams params_{};                  ///< Current configuration
    bool prepared_{false};                       ///< Whether prepare() was called
    int rotationOffset_{0};                      ///< Step read offset from phaseOffset
};

} // namespace DSP
} // namespace Krate
