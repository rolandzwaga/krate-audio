// ==============================================================================
// Layer 2: DSP Processor - Multi-Stage Envelope Generator
// ==============================================================================
// Configurable multi-stage envelope generator (4-8 stages) with per-stage
// time/level/curve, sustain point selection, loop points for LFO-like behavior,
// retrigger modes (hard/legato), and real-time parameter changes.
//
// Per-sample operation:
// - Exponential/Linear: output = base + output * coef (1 mul + 1 add)
// - Logarithmic: phase-based quadratic mapping (2 mul + 1 add)
// - Plus stage transition check
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, value semantics, C++20)
// - Principle IX: Layer 2 (depends on Layer 0 core, Layer 1 primitives)
// - Principle XII: Test-First Development
//
// Reference: specs/033-multi-stage-envelope/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// EnvStageConfig (FR-002)
// =============================================================================

struct EnvStageConfig {
    float targetLevel = 0.0f;               // [0.0, 1.0]
    float timeMs = 100.0f;                  // [0.0, 10000.0]
    EnvCurve curve = EnvCurve::Exponential;  // FR-020 default
};

// =============================================================================
// MultiStageEnvState (FR-004)
// =============================================================================

enum class MultiStageEnvState : uint8_t {
    Idle = 0,
    Running,
    Sustaining,
    Releasing
};

// =============================================================================
// MultiStageEnvelope (FR-001 through FR-037)
// =============================================================================

class MultiStageEnvelope {
public:
    static constexpr int kMinStages = 4;             // FR-001
    static constexpr int kMaxStages = 8;             // FR-001
    static constexpr float kMaxStageTimeMs = 10000.0f; // FR-002

    MultiStageEnvelope() noexcept = default;

    // =========================================================================
    // Lifecycle (FR-010)
    // =========================================================================

    void prepare(float sampleRate) noexcept {
        if (sampleRate <= 0.0f) return;
        sampleRate_ = sampleRate;
        sustainSmoothCoef_ = std::exp(-5000.0f / (kSustainSmoothTimeMs * sampleRate_));
    }

    void reset() noexcept {
        output_ = 0.0f;
        state_ = MultiStageEnvState::Idle;
        currentStage_ = 0;
        sampleCounter_ = 0;
        totalStageSamples_ = 0;
        stageStartLevel_ = 0.0f;
        stageCoef_ = 0.0f;
        stageBase_ = 0.0f;
        refOutput_ = 0.0f;
        logPhase_ = 0.0f;
        logPhaseInc_ = 0.0f;
        releaseCoef_ = 0.0f;
        releaseBase_ = 0.0f;
    }

    // =========================================================================
    // Gate Control (FR-005, FR-014, FR-027, FR-028, FR-029)
    // =========================================================================

    void gate(bool on) noexcept {
        if (on) {
            if (retriggerMode_ == RetriggerMode::Hard) {
                // FR-028: Hard retrigger restarts from stage 0 at current level
                enterStage(0);
                state_ = MultiStageEnvState::Running;
            } else {
                // FR-029: Legato mode
                if (state_ == MultiStageEnvState::Idle) {
                    enterStage(0);
                    state_ = MultiStageEnvState::Running;
                } else if (state_ == MultiStageEnvState::Releasing) {
                    // Return to sustain point from current level
                    if (loopEnabled_) {
                        enterStage(loopStart_);
                        state_ = MultiStageEnvState::Running;
                    } else {
                        // Enter sustain state directly at current level
                        state_ = MultiStageEnvState::Sustaining;
                        currentStage_ = sustainPoint_;
                    }
                }
                // If Running or Sustaining in legato mode, do nothing
            }
        } else {
            // Gate off - enter release from any active state
            if (state_ != MultiStageEnvState::Idle && state_ != MultiStageEnvState::Releasing) {
                enterRelease();
            }
        }
    }

    // =========================================================================
    // Stage Configuration (FR-001, FR-002, FR-016, FR-020)
    // =========================================================================

    void setNumStages(int count) noexcept {
        numStages_ = std::clamp(count, kMinStages, kMaxStages);
        // Clamp sustain point and loop bounds to valid range
        sustainPoint_ = std::clamp(sustainPoint_, 0, numStages_ - 1);
        loopStart_ = std::clamp(loopStart_, 0, numStages_ - 1);
        loopEnd_ = std::clamp(loopEnd_, 0, numStages_ - 1);
        if (loopStart_ > loopEnd_) loopStart_ = loopEnd_;
    }

    ITERUM_NOINLINE void setStageLevel(int stage, float level) noexcept {
        if (stage < 0 || stage >= kMaxStages) return;
        if (detail::isNaN(level)) return;
        stages_[static_cast<size_t>(stage)].targetLevel = std::clamp(level, 0.0f, 1.0f);
    }

    ITERUM_NOINLINE void setStageTime(int stage, float ms) noexcept {
        if (stage < 0 || stage >= kMaxStages) return;
        if (detail::isNaN(ms)) return;
        stages_[static_cast<size_t>(stage)].timeMs = std::clamp(ms, 0.0f, kMaxStageTimeMs);

        // FR-031: If this is the currently active stage, recalculate coefficients
        if (state_ == MultiStageEnvState::Running && stage == currentStage_) {
            recalcCurrentStage();
        }
    }

    void setStageCurve(int stage, EnvCurve curve) noexcept {
        if (stage < 0 || stage >= kMaxStages) return;
        stages_[static_cast<size_t>(stage)].curve = curve;
    }

    void setStage(int stage, float level, float ms, EnvCurve curve) noexcept {
        if (stage < 0 || stage >= kMaxStages) return;
        if (detail::isNaN(level) || detail::isNaN(ms)) return;
        stages_[static_cast<size_t>(stage)].targetLevel = std::clamp(level, 0.0f, 1.0f);
        stages_[static_cast<size_t>(stage)].timeMs = std::clamp(ms, 0.0f, kMaxStageTimeMs);
        stages_[static_cast<size_t>(stage)].curve = curve;
    }

    // =========================================================================
    // Sustain Point (FR-012, FR-015)
    // =========================================================================

    void setSustainPoint(int stage) noexcept {
        sustainPoint_ = std::clamp(stage, 0, numStages_ - 1);
    }

    // =========================================================================
    // Loop Control (FR-022, FR-023, FR-025)
    // =========================================================================

    void setLoopEnabled(bool enabled) noexcept {
        loopEnabled_ = enabled;
    }

    void setLoopStart(int stage) noexcept {
        loopStart_ = std::clamp(stage, 0, numStages_ - 1);
        // FR-025: Enforce loopStart <= loopEnd by adjusting whichever was set
        if (loopStart_ > loopEnd_) loopEnd_ = loopStart_;
    }

    void setLoopEnd(int stage) noexcept {
        loopEnd_ = std::clamp(stage, 0, numStages_ - 1);
        // FR-025: Enforce loopStart <= loopEnd
        if (loopStart_ > loopEnd_) loopStart_ = loopEnd_;
    }

    // =========================================================================
    // Release (FR-006)
    // =========================================================================

    ITERUM_NOINLINE void setReleaseTime(float ms) noexcept {
        if (detail::isNaN(ms)) return;
        releaseTimeMs_ = std::clamp(ms, 0.0f, kMaxStageTimeMs);
    }

    // =========================================================================
    // Retrigger Mode (FR-028, FR-029)
    // =========================================================================

    void setRetriggerMode(RetriggerMode mode) noexcept {
        retriggerMode_ = mode;
    }

    // =========================================================================
    // Processing (FR-008, FR-033, FR-034)
    // =========================================================================

    [[nodiscard]] float process() noexcept {
        switch (state_) {
            case MultiStageEnvState::Idle:
                return 0.0f;

            case MultiStageEnvState::Running:
                return processRunning();

            case MultiStageEnvState::Sustaining:
                return processSustaining();

            case MultiStageEnvState::Releasing:
                return processReleasing();
        }
        return 0.0f;
    }

    void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // State Queries (FR-004, FR-009)
    // =========================================================================

    [[nodiscard]] MultiStageEnvState getState() const noexcept { return state_; }
    [[nodiscard]] bool isActive() const noexcept { return state_ != MultiStageEnvState::Idle; }
    [[nodiscard]] bool isReleasing() const noexcept { return state_ == MultiStageEnvState::Releasing; }
    [[nodiscard]] float getOutput() const noexcept { return output_; }
    [[nodiscard]] int getCurrentStage() const noexcept { return currentStage_; }

    // =========================================================================
    // Configuration Queries
    // =========================================================================

    [[nodiscard]] int getNumStages() const noexcept { return numStages_; }
    [[nodiscard]] int getSustainPoint() const noexcept { return sustainPoint_; }
    [[nodiscard]] bool getLoopEnabled() const noexcept { return loopEnabled_; }
    [[nodiscard]] int getLoopStart() const noexcept { return loopStart_; }
    [[nodiscard]] int getLoopEnd() const noexcept { return loopEnd_; }

private:
    // =========================================================================
    // Per-State Processing
    // =========================================================================

    float processRunning() noexcept {
        const auto& stage = stages_[static_cast<size_t>(currentStage_)];

        // Process current sample based on curve type
        if (stage.curve == EnvCurve::Linear) {
            // Linear: phase-based interpolation for perfect linearity at any
            // stage length, avoiding floating-point precision issues with
            // one-pole at very long durations.
            logPhase_ += logPhaseInc_;
            if (logPhase_ > 1.0f) logPhase_ = 1.0f;
            output_ = stageStartLevel_ + (stage.targetLevel - stageStartLevel_) * logPhase_;
        } else if (stage.curve == EnvCurve::Logarithmic) {
            logPhase_ += logPhaseInc_;
            if (logPhase_ > 1.0f) logPhase_ = 1.0f;

            bool rising = stage.targetLevel >= stageStartLevel_;
            if (rising) {
                // Convex: phase^2 (slow start, fast finish)
                float curvedPhase = logPhase_ * logPhase_;
                output_ = stageStartLevel_ + (stage.targetLevel - stageStartLevel_) * curvedPhase;
            } else {
                // Concave for falling: 1 - (1-phase)^2
                float remaining = 1.0f - logPhase_;
                float curvedPhase = 1.0f - remaining * remaining;
                output_ = stageStartLevel_ + (stage.targetLevel - stageStartLevel_) * curvedPhase;
            }
        } else {
            // Exponential: normalized one-pole mapped to actual range.
            // refOutput_ tracks a 0-to-1 reference curve using the EarLevel
            // one-pole formula. The actual output is mapped to start/end levels.
            refOutput_ = stageBase_ + refOutput_ * stageCoef_;
            output_ = stageStartLevel_ + (stage.targetLevel - stageStartLevel_) * refOutput_;
        }

        ++sampleCounter_;

        // FR-021: Time-based stage completion
        if (sampleCounter_ >= totalStageSamples_) {
            // Snap to exact target level
            output_ = stage.targetLevel;
            advanceToNextStage();
        }

        output_ = detail::flushDenormal(output_);
        return output_;
    }

    float processSustaining() noexcept {
        // FR-032: Smooth sustain level changes
        float sustainTarget = stages_[static_cast<size_t>(sustainPoint_)].targetLevel;
        output_ = sustainTarget + sustainSmoothCoef_ * (output_ - sustainTarget);
        output_ = detail::flushDenormal(output_);
        return output_;
    }

    float processReleasing() noexcept {
        // Exponential release to 0.0 using one-pole
        output_ = releaseBase_ + output_ * releaseCoef_;

        // FR-007: Transition to Idle when below threshold
        if (output_ < kEnvelopeIdleThreshold) {
            output_ = 0.0f;
            state_ = MultiStageEnvState::Idle;
        }

        output_ = detail::flushDenormal(output_);
        return output_;
    }

    // =========================================================================
    // Stage Management
    // =========================================================================

    void enterStage(int stageIndex) noexcept {
        currentStage_ = stageIndex;
        sampleCounter_ = 0;
        stageStartLevel_ = output_;

        const auto& stage = stages_[static_cast<size_t>(stageIndex)];

        // Calculate total samples for this stage (FR-021)
        totalStageSamples_ = std::max(1, static_cast<int>(
            std::round(stage.timeMs * 0.001f * sampleRate_)));

        if (stage.curve == EnvCurve::Linear || stage.curve == EnvCurve::Logarithmic) {
            // Phase-based curves: linear uses direct phase, logarithmic uses
            // quadratic mapping. Both use phase increment for precise timing.
            logPhase_ = 0.0f;
            logPhaseInc_ = 1.0f / static_cast<float>(totalStageSamples_);
        } else {
            // Exponential: normalized one-pole coefficients for a 0-to-1
            // reference curve using the EarLevel formula, then mapped to actual
            // start/end levels. This preserves the exponential shape regardless
            // of what the actual start and end levels are.
            float targetRatio = kDefaultTargetRatioA;
            auto coeffs = calcEnvCoefficients(
                stage.timeMs, sampleRate_, 1.0f, targetRatio, true);
            stageCoef_ = coeffs.coef;
            stageBase_ = coeffs.base;
            refOutput_ = 0.0f;  // Reference starts at 0, approaches 1
        }
    }

    void advanceToNextStage() noexcept {
        int nextStage = currentStage_ + 1;

        // Check if we should loop
        if (loopEnabled_ && currentStage_ == loopEnd_) {
            // FR-024: Jump back to loop start
            enterStage(loopStart_);
            return;
        }

        // Check if we reached the sustain point (FR-013)
        // FR-026: Sustain hold is bypassed when looping is enabled
        if (!loopEnabled_ && currentStage_ == sustainPoint_) {
            state_ = MultiStageEnvState::Sustaining;
            return;
        }

        // Check if we reached the end of all stages
        if (nextStage >= numStages_) {
            // All stages complete - hold at last level (or transition to idle?)
            // Per spec: sustain hold is the holding mechanism
            // If we get here, the gate is still on but we passed all stages
            state_ = MultiStageEnvState::Sustaining;
            currentStage_ = numStages_ - 1;
            return;
        }

        // Move to next stage
        enterStage(nextStage);
    }

    void enterRelease() noexcept {
        state_ = MultiStageEnvState::Releasing;

        // Calculate release coefficients (exponential curve to 0.0)
        float ratio = kDefaultTargetRatioDR;
        auto coeffs = calcEnvCoefficients(releaseTimeMs_, sampleRate_, 0.0f, ratio, false);
        releaseCoef_ = coeffs.coef;
        releaseBase_ = coeffs.base;
    }

    void recalcCurrentStage() noexcept {
        // FR-031: Recalculate rate for mid-stage time change
        const auto& stage = stages_[static_cast<size_t>(currentStage_)];

        // Recalculate total samples from new time
        int newTotalSamples = std::max(1, static_cast<int>(
            std::round(stage.timeMs * 0.001f * sampleRate_)));

        // If the new time means we should already be done, complete immediately
        if (newTotalSamples <= sampleCounter_) {
            totalStageSamples_ = sampleCounter_; // Will trigger completion next sample
            return;
        }

        totalStageSamples_ = newTotalSamples;
        int remaining = totalStageSamples_ - sampleCounter_;

        // Recalculate coefficients for remaining time
        float remainingTimeMs = static_cast<float>(remaining) / sampleRate_ * 1000.0f;

        if (stage.curve == EnvCurve::Linear || stage.curve == EnvCurve::Logarithmic) {
            float remainingPhase = 1.0f - logPhase_;
            if (remaining > 0) {
                logPhaseInc_ = remainingPhase / static_cast<float>(remaining);
            }
        } else {
            // Recalculate normalized reference curve for remaining portion
            float targetRatio = kDefaultTargetRatioA;
            auto coeffs = calcEnvCoefficients(
                remainingTimeMs, sampleRate_, 1.0f, targetRatio, true);
            stageCoef_ = coeffs.coef;
            stageBase_ = coeffs.base;
            // Keep refOutput_ as-is; the new coefficients will continue from here
        }
    }

    // =========================================================================
    // Member Fields
    // =========================================================================

    // Configuration
    std::array<EnvStageConfig, kMaxStages> stages_{};
    int numStages_ = kMinStages;
    int sustainPoint_ = kMinStages - 2;  // FR-015: default = numStages - 2
    bool loopEnabled_ = false;
    int loopStart_ = 0;
    int loopEnd_ = 0;
    float releaseTimeMs_ = 100.0f;
    RetriggerMode retriggerMode_ = RetriggerMode::Hard;

    // Runtime state
    MultiStageEnvState state_ = MultiStageEnvState::Idle;
    float output_ = 0.0f;
    int currentStage_ = 0;
    int sampleCounter_ = 0;
    int totalStageSamples_ = 0;
    float stageStartLevel_ = 0.0f;
    float stageCoef_ = 0.0f;
    float stageBase_ = 0.0f;
    float refOutput_ = 0.0f;
    float logPhase_ = 0.0f;
    float logPhaseInc_ = 0.0f;
    float releaseCoef_ = 0.0f;
    float releaseBase_ = 0.0f;
    float sustainSmoothCoef_ = 0.0f;
    float sampleRate_ = 44100.0f;
};

} // namespace DSP
} // namespace Krate
