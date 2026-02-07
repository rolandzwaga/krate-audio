// ==============================================================================
// Layer 1: DSP Primitive - ADSR Envelope Generator
// ==============================================================================
// Five-state ADSR envelope generator. Uses the EarLevel Engineering one-pole
// iterative approach for Exponential and Linear curves, and a quadratic phase
// mapping for Logarithmic curves.
//
// Per-sample operation:
// - Exponential/Linear: output = base + output * coef (1 mul + 1 add)
// - Logarithmic: phase-based quadratic mapping (2 mul + 1 add)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/032-adsr-envelope-generator/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <krate/dsp/core/db_utils.h>

namespace Krate {
namespace DSP {

// =============================================================================
// Compiler Compatibility Macros
// =============================================================================

#ifndef ITERUM_NOINLINE
#if defined(_MSC_VER)
#define ITERUM_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define ITERUM_NOINLINE __attribute__((noinline))
#else
#define ITERUM_NOINLINE
#endif
#endif

// =============================================================================
// Constants (FR-007, FR-011)
// =============================================================================

inline constexpr float kEnvelopeIdleThreshold = 1e-4f;
inline constexpr float kMinEnvelopeTimeMs = 0.1f;
inline constexpr float kMaxEnvelopeTimeMs = 10000.0f;
inline constexpr float kSustainSmoothTimeMs = 5.0f;
inline constexpr float kDefaultTargetRatioA = 0.3f;
inline constexpr float kDefaultTargetRatioDR = 0.0001f;
inline constexpr float kLinearTargetRatio = 100.0f;

// =============================================================================
// Enumerations (FR-001, FR-013)
// =============================================================================

enum class ADSRStage : uint8_t {
    Idle = 0,
    Attack,
    Decay,
    Sustain,
    Release
};

enum class EnvCurve : uint8_t {
    Exponential = 0,
    Linear,
    Logarithmic
};

enum class RetriggerMode : uint8_t {
    Hard = 0,
    Legato
};

// =============================================================================
// ADSREnvelope Class (FR-001 through FR-030)
// =============================================================================

class ADSREnvelope {
public:
    ADSREnvelope() noexcept = default;
    ~ADSREnvelope() = default;

    ADSREnvelope(const ADSREnvelope&) noexcept = default;
    ADSREnvelope& operator=(const ADSREnvelope&) noexcept = default;
    ADSREnvelope(ADSREnvelope&&) noexcept = default;
    ADSREnvelope& operator=(ADSREnvelope&&) noexcept = default;

    // =========================================================================
    // Initialization (FR-010)
    // =========================================================================

    void prepare(float sampleRate) noexcept {
        if (sampleRate <= 0.0f) return;
        sampleRate_ = sampleRate;
        recalcAllCoefficients();
        sustainSmoothCoef_ = std::exp(-5000.0f / (kSustainSmoothTimeMs * sampleRate_));
    }

    void reset() noexcept {
        output_ = 0.0f;
        stage_ = ADSRStage::Idle;
        gateOn_ = false;
        logPhase_ = 0.0f;
    }

    // =========================================================================
    // Gate Control (FR-002, FR-018, FR-019, FR-020)
    // =========================================================================

    void gate(bool on) noexcept {
        if (on) {
            gateOn_ = true;
            if (retriggerMode_ == RetriggerMode::Hard) {
                enterAttack();
            } else {
                if (stage_ == ADSRStage::Idle) {
                    enterAttack();
                } else if (stage_ == ADSRStage::Release) {
                    float sustainTarget = sustainLevel_ * peakLevel_;
                    if (output_ > sustainTarget) {
                        enterDecay();
                    } else {
                        stage_ = ADSRStage::Sustain;
                    }
                }
            }
        } else {
            gateOn_ = false;
            if (stage_ != ADSRStage::Idle && stage_ != ADSRStage::Release) {
                enterRelease();
            }
        }
    }

    // =========================================================================
    // Parameter Setters (FR-011, FR-012, FR-023, FR-024, FR-025)
    // =========================================================================

    ITERUM_NOINLINE void setAttack(float ms) noexcept {
        if (detail::isNaN(ms)) return;
        attackTimeMs_ = std::clamp(ms, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        calcAttackCoefficients();
    }

    ITERUM_NOINLINE void setDecay(float ms) noexcept {
        if (detail::isNaN(ms)) return;
        decayTimeMs_ = std::clamp(ms, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        calcDecayCoefficients();
    }

    ITERUM_NOINLINE void setSustain(float level) noexcept {
        if (detail::isNaN(level)) return;
        sustainLevel_ = std::clamp(level, 0.0f, 1.0f);
    }

    ITERUM_NOINLINE void setRelease(float ms) noexcept {
        if (detail::isNaN(ms)) return;
        releaseTimeMs_ = std::clamp(ms, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        calcReleaseCoefficients();
    }

    // =========================================================================
    // Curve Shape Setters (FR-013, FR-014, FR-015, FR-016, FR-017)
    // =========================================================================

    void setAttackCurve(EnvCurve curve) noexcept {
        attackCurve_ = curve;
        calcAttackCoefficients();
    }

    void setDecayCurve(EnvCurve curve) noexcept {
        decayCurve_ = curve;
        calcDecayCoefficients();
    }

    void setReleaseCurve(EnvCurve curve) noexcept {
        releaseCurve_ = curve;
        calcReleaseCoefficients();
    }

    // =========================================================================
    // Retrigger Mode (FR-018, FR-019)
    // =========================================================================

    void setRetriggerMode(RetriggerMode mode) noexcept {
        retriggerMode_ = mode;
    }

    // =========================================================================
    // Velocity Scaling (FR-021, FR-022)
    // =========================================================================

    void setVelocityScaling(bool enabled) noexcept {
        velocityScalingEnabled_ = enabled;
        updatePeakLevel();
    }

    ITERUM_NOINLINE void setVelocity(float velocity) noexcept {
        if (detail::isNaN(velocity)) return;
        velocity_ = std::clamp(velocity, 0.0f, 1.0f);
        updatePeakLevel();
    }

    // =========================================================================
    // Processing (FR-008, FR-026, FR-027)
    // =========================================================================

    [[nodiscard]] float process() noexcept {
        switch (stage_) {
            case ADSRStage::Idle:
                return 0.0f;

            case ADSRStage::Attack:
                return processAttack();

            case ADSRStage::Decay:
                return processDecay();

            case ADSRStage::Sustain: {
                float sustainTarget = sustainLevel_ * peakLevel_;
                output_ = sustainTarget + sustainSmoothCoef_ * (output_ - sustainTarget);
                return output_;
            }

            case ADSRStage::Release:
                return processRelease();
        }
        return 0.0f;
    }

    void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // State Queries (FR-001, FR-009)
    // =========================================================================

    [[nodiscard]] ADSRStage getStage() const noexcept { return stage_; }
    [[nodiscard]] bool isActive() const noexcept { return stage_ != ADSRStage::Idle; }
    [[nodiscard]] bool isReleasing() const noexcept { return stage_ == ADSRStage::Release; }
    [[nodiscard]] float getOutput() const noexcept { return output_; }

private:
    // =========================================================================
    // Per-Stage Processing
    // =========================================================================

    float processAttack() noexcept {
        if (attackCurve_ == EnvCurve::Logarithmic) {
            // Logarithmic attack: quadratic phase mapping (convex: slow start, fast finish)
            logPhase_ += logPhaseInc_;
            if (logPhase_ >= 1.0f) {
                logPhase_ = 1.0f;
                output_ = peakLevel_;
                enterDecay();
            } else {
                output_ = logStartLevel_ + (peakLevel_ - logStartLevel_) * logPhase_ * logPhase_;
            }
        } else {
            // Exponential/Linear: one-pole formula
            output_ = attackBase_ + output_ * attackCoef_;
            if (output_ >= peakLevel_) {
                output_ = peakLevel_;
                enterDecay();
            }
        }
        return output_;
    }

    float processDecay() noexcept {
        float sustainTarget = sustainLevel_ * peakLevel_;

        if (decayCurve_ == EnvCurve::Logarithmic) {
            // Logarithmic decay: quadratic phase (slow initial drop, fast approach to sustain)
            logPhase_ += logPhaseInc_;
            if (logPhase_ >= 1.0f) {
                logPhase_ = 1.0f;
                output_ = sustainTarget;
                stage_ = ADSRStage::Sustain;
            } else {
                float remaining = 1.0f - logPhase_;
                output_ = sustainTarget + (logStartLevel_ - sustainTarget) * remaining * remaining;
            }
        } else {
            // Exponential/Linear: one-pole formula targeting 0.0 (FR-004)
            output_ = decayBase_ + output_ * decayCoef_;
            if (output_ <= sustainTarget) {
                output_ = sustainTarget;
                stage_ = ADSRStage::Sustain;
            }
        }
        return output_;
    }

    float processRelease() noexcept {
        if (releaseCurve_ == EnvCurve::Logarithmic) {
            // Logarithmic release: quadratic phase (slow initial drop, fast approach to zero)
            logPhase_ += logPhaseInc_;
            if (logPhase_ >= 1.0f) {
                output_ = 0.0f;
                stage_ = ADSRStage::Idle;
            } else {
                float remaining = 1.0f - logPhase_;
                output_ = logStartLevel_ * remaining * remaining;
                if (output_ < kEnvelopeIdleThreshold) {
                    output_ = 0.0f;
                    stage_ = ADSRStage::Idle;
                }
            }
        } else {
            // Exponential/Linear: one-pole formula targeting 0.0 (FR-006)
            output_ = releaseBase_ + output_ * releaseCoef_;
            if (output_ < kEnvelopeIdleThreshold) {
                output_ = 0.0f;
                stage_ = ADSRStage::Idle;
            }
        }
        return output_;
    }

    // =========================================================================
    // Coefficient Calculation
    // =========================================================================

    struct StageCoefficients {
        float coef = 0.0f;
        float base = 0.0f;
    };

    static StageCoefficients calcCoefficients(
        float timeMs, float sampleRate,
        float targetLevel, float targetRatio, bool rising) noexcept
    {
        StageCoefficients result;
        float rate = timeMs * 0.001f * sampleRate;
        if (rate < 1.0f) rate = 1.0f;

        result.coef = std::exp(-std::log((1.0f + targetRatio) / targetRatio) / rate);

        if (rising) {
            result.base = (targetLevel + targetRatio) * (1.0f - result.coef);
        } else {
            result.base = (targetLevel - targetRatio) * (1.0f - result.coef);
        }

        return result;
    }

    static float getAttackTargetRatio(EnvCurve curve) noexcept {
        switch (curve) {
            case EnvCurve::Exponential:  return kDefaultTargetRatioA;
            case EnvCurve::Linear:       return kLinearTargetRatio;
            case EnvCurve::Logarithmic:  return kDefaultTargetRatioA; // Not used for log
        }
        return kDefaultTargetRatioA;
    }

    static float getDecayTargetRatio(EnvCurve curve) noexcept {
        switch (curve) {
            case EnvCurve::Exponential:  return kDefaultTargetRatioDR;
            case EnvCurve::Linear:       return kLinearTargetRatio;
            case EnvCurve::Logarithmic:  return kDefaultTargetRatioDR; // Not used for log
        }
        return kDefaultTargetRatioDR;
    }

    void calcAttackCoefficients() noexcept {
        if (attackCurve_ == EnvCurve::Logarithmic) {
            logPhaseInc_ = 1.0f / std::max(1.0f, attackTimeMs_ * 0.001f * sampleRate_);
        } else {
            float ratio = getAttackTargetRatio(attackCurve_);
            auto coeffs = calcCoefficients(attackTimeMs_, sampleRate_, peakLevel_, ratio, true);
            attackCoef_ = coeffs.coef;
            attackBase_ = coeffs.base;
        }
    }

    void calcDecayCoefficients() noexcept {
        if (decayCurve_ == EnvCurve::Logarithmic) {
            // Phase increment for full peak→0 in decayTime (constant rate)
            logPhaseInc_ = 1.0f / std::max(1.0f, decayTimeMs_ * 0.001f * sampleRate_);
        } else {
            // FR-004: Decay time = full 1.0→0.0 ramp. Target 0.0 with undershoot.
            float ratio = getDecayTargetRatio(decayCurve_);
            auto coeffs = calcCoefficients(decayTimeMs_, sampleRate_, 0.0f, ratio, false);
            decayCoef_ = coeffs.coef;
            decayBase_ = coeffs.base;
        }
    }

    void calcReleaseCoefficients() noexcept {
        if (releaseCurve_ == EnvCurve::Logarithmic) {
            logPhaseInc_ = 1.0f / std::max(1.0f, releaseTimeMs_ * 0.001f * sampleRate_);
        } else {
            float ratio = getDecayTargetRatio(releaseCurve_);
            auto coeffs = calcCoefficients(releaseTimeMs_, sampleRate_, 0.0f, ratio, false);
            releaseCoef_ = coeffs.coef;
            releaseBase_ = coeffs.base;
        }
    }

    void recalcAllCoefficients() noexcept {
        calcAttackCoefficients();
        calcDecayCoefficients();
        calcReleaseCoefficients();
    }

    void updatePeakLevel() noexcept {
        peakLevel_ = velocityScalingEnabled_ ? velocity_ : 1.0f;
        recalcAllCoefficients();
    }

    // =========================================================================
    // Stage Entry Helpers
    // =========================================================================

    void enterAttack() noexcept {
        stage_ = ADSRStage::Attack;
        if (attackCurve_ == EnvCurve::Logarithmic) {
            logStartLevel_ = output_;
            float range = peakLevel_ - logStartLevel_;
            if (range > 0.0f) {
                logPhase_ = 0.0f;
            } else {
                logPhase_ = 1.0f;
            }
            calcAttackCoefficients();
        } else {
            calcAttackCoefficients();
        }
    }

    void enterDecay() noexcept {
        stage_ = ADSRStage::Decay;
        if (decayCurve_ == EnvCurve::Logarithmic) {
            logStartLevel_ = output_;
            logPhase_ = 0.0f;
            calcDecayCoefficients();
            // Scale phase increment for partial range (constant rate: full peak→0 in decayTime)
            float fullRange = peakLevel_;
            float actualRange = logStartLevel_ - sustainLevel_ * peakLevel_;
            if (fullRange > 0.0f && actualRange > 0.0f) {
                float fraction = actualRange / fullRange;
                // Phase needs to go from 0 to 1 in fraction * decayTime
                logPhaseInc_ = 1.0f / std::max(1.0f, fraction * decayTimeMs_ * 0.001f * sampleRate_);
            }
        } else {
            calcDecayCoefficients();
        }
    }

    void enterRelease() noexcept {
        stage_ = ADSRStage::Release;
        if (releaseCurve_ == EnvCurve::Logarithmic) {
            logStartLevel_ = output_;
            logPhase_ = 0.0f;
            calcReleaseCoefficients();
            // Scale phase increment for partial range (constant rate: full 1→0 in releaseTime)
            if (peakLevel_ > 0.0f && logStartLevel_ > 0.0f) {
                float fraction = logStartLevel_ / peakLevel_;
                logPhaseInc_ = 1.0f / std::max(1.0f, fraction * releaseTimeMs_ * 0.001f * sampleRate_);
            }
        } else {
            calcReleaseCoefficients();
        }
    }

    // =========================================================================
    // Member Fields
    // =========================================================================

    float sampleRate_ = 44100.0f;
    float output_ = 0.0f;
    ADSRStage stage_ = ADSRStage::Idle;

    float attackTimeMs_ = 10.0f;
    float decayTimeMs_ = 50.0f;
    float sustainLevel_ = 0.5f;
    float releaseTimeMs_ = 100.0f;

    EnvCurve attackCurve_ = EnvCurve::Exponential;
    EnvCurve decayCurve_ = EnvCurve::Exponential;
    EnvCurve releaseCurve_ = EnvCurve::Exponential;

    RetriggerMode retriggerMode_ = RetriggerMode::Hard;

    bool velocityScalingEnabled_ = false;
    float velocity_ = 1.0f;
    float peakLevel_ = 1.0f;

    float attackCoef_ = 0.0f;
    float attackBase_ = 0.0f;
    float decayCoef_ = 0.0f;
    float decayBase_ = 0.0f;
    float releaseCoef_ = 0.0f;
    float releaseBase_ = 0.0f;

    float sustainSmoothCoef_ = 0.0f;

    // Logarithmic curve state (quadratic phase mapping)
    float logPhase_ = 0.0f;
    float logPhaseInc_ = 0.0f;
    float logStartLevel_ = 0.0f;

    bool gateOn_ = false;
};

} // namespace DSP
} // namespace Krate
