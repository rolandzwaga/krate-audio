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

#include <krate/dsp/core/curve_table.h>
#include <krate/dsp/primitives/envelope_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <krate/dsp/core/db_utils.h>


namespace Krate::DSP {

// =============================================================================
// Enumerations (FR-001)
// =============================================================================
// Note: EnvCurve, RetriggerMode, and shared constants are now in envelope_utils.h

enum class ADSRStage : uint8_t {
    Idle = 0,
    Attack,
    Decay,
    Sustain,
    Release
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

    // An unchanged time returns early: hosts push every parameter on every block
    // (Ruinae audit R-001). Before the first recalcAllCoefficients() the stored
    // time has no coefficients behind it yet, so it is always computed then.
    ITERUM_NOINLINE void setAttack(float ms) noexcept {
        if (detail::isNaN(ms)) return;
        const float clamped = std::clamp(ms, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        if (coefficientsValid_ && clamped == attackTimeMs_) return;
        attackTimeMs_ = clamped;
        calcAttackCoefficients();
    }

    ITERUM_NOINLINE void setDecay(float ms) noexcept {
        if (detail::isNaN(ms)) return;
        const float clamped = std::clamp(ms, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        if (coefficientsValid_ && clamped == decayTimeMs_) return;
        decayTimeMs_ = clamped;
        calcDecayCoefficients();
    }

    ITERUM_NOINLINE void setSustain(float level) noexcept {
        if (detail::isNaN(level)) return;
        sustainLevel_ = std::clamp(level, 0.0f, 1.0f);
    }

    ITERUM_NOINLINE void setRelease(float ms) noexcept {
        if (detail::isNaN(ms)) return;
        const float clamped = std::clamp(ms, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        if (coefficientsValid_ && clamped == releaseTimeMs_) return;
        releaseTimeMs_ = clamped;
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

    // Continuous curve amount overloads (048-adsr-display)
    // curveAmount: [-1, +1], 0=linear, -1=logarithmic, +1=exponential

    // Each table remembers what built it (family + inputs); a setter called again
    // with the same inputs keeps the table instead of rebuilding it (R-001).

    void setAttackCurve(float amount) noexcept {
        const float curveAmount = std::clamp(amount, -1.0f, 1.0f);
        if (!claimTable(attackTableSource_, attackTableInputs_, CurveTableSource::Power, curveAmount))
            return;
        generatePowerCurveTable(attackTable_, curveAmount, 0.0f, 1.0f);
        ++tableGenerations_;
        useTableProcessing_ = true;
        calcAttackCoefficients();
    }

    void setDecayCurve(float amount) noexcept {
        const float curveAmount = std::clamp(amount, -1.0f, 1.0f);
        if (!claimTable(decayTableSource_, decayTableInputs_, CurveTableSource::Power, curveAmount))
            return;
        generatePowerCurveTable(decayTable_, curveAmount, 1.0f, 0.0f);
        ++tableGenerations_;
        useTableProcessing_ = true;
        calcDecayCoefficients();
    }

    void setReleaseCurve(float amount) noexcept {
        const float curveAmount = std::clamp(amount, -1.0f, 1.0f);
        if (!claimTable(releaseTableSource_, releaseTableInputs_, CurveTableSource::Power, curveAmount))
            return;
        generatePowerCurveTable(releaseTable_, curveAmount, 1.0f, 0.0f);
        ++tableGenerations_;
        useTableProcessing_ = true;
        calcReleaseCoefficients();
    }

    /// True when all four control-point coordinates are finite. A Bezier setter
    /// given a non-finite point keeps its previous table: one NaN would fill
    /// every table entry (even t = 0) and the envelope would output NaN.
    [[nodiscard]] static bool bezierPointsFinite(float cp1x, float cp1y,
                                                 float cp2x, float cp2y) noexcept {
        return detail::isFinite(cp1x) && detail::isFinite(cp1y) &&
               detail::isFinite(cp2x) && detail::isFinite(cp2y);
    }

    /// Set Bezier curve for attack segment (4 control point coordinates)
    void setAttackBezierCurve(float cp1x, float cp1y,
                               float cp2x, float cp2y) noexcept {
        if (!bezierPointsFinite(cp1x, cp1y, cp2x, cp2y)) return;
        if (!claimTable(attackTableSource_, attackTableInputs_, CurveTableSource::Bezier,
                        cp1x, cp1y, cp2x, cp2y))
            return;
        generateBezierCurveTable(attackTable_, cp1x, cp1y, cp2x, cp2y, 0.0f, 1.0f);
        ++tableGenerations_;
        useTableProcessing_ = true;
        calcAttackCoefficients();
    }

    /// Set Bezier curve for decay segment (4 control point coordinates)
    void setDecayBezierCurve(float cp1x, float cp1y,
                              float cp2x, float cp2y) noexcept {
        if (!bezierPointsFinite(cp1x, cp1y, cp2x, cp2y)) return;
        if (!claimTable(decayTableSource_, decayTableInputs_, CurveTableSource::Bezier,
                        cp1x, cp1y, cp2x, cp2y))
            return;
        generateBezierCurveTable(decayTable_, cp1x, cp1y, cp2x, cp2y, 1.0f, 0.0f);
        ++tableGenerations_;
        useTableProcessing_ = true;
        calcDecayCoefficients();
    }

    /// Set Bezier curve for release segment (4 control point coordinates)
    void setReleaseBezierCurve(float cp1x, float cp1y,
                                float cp2x, float cp2y) noexcept {
        if (!bezierPointsFinite(cp1x, cp1y, cp2x, cp2y)) return;
        if (!claimTable(releaseTableSource_, releaseTableInputs_, CurveTableSource::Bezier,
                        cp1x, cp1y, cp2x, cp2y))
            return;
        generateBezierCurveTable(releaseTable_, cp1x, cp1y, cp2x, cp2y, 1.0f, 0.0f);
        ++tableGenerations_;
        useTableProcessing_ = true;
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

    /// Number of curve-table rebuilds so far (test hook: unchanged curves must not rebuild).
    [[nodiscard]] uint32_t tableGenerationCountForTest() const noexcept { return tableGenerations_; }

private:
    // =========================================================================
    // Per-Stage Processing
    // =========================================================================

    float processAttack() noexcept {
        if (useTableProcessing_) {
            logPhase_ += logPhaseInc_;
            if (logPhase_ >= 1.0f) {
                logPhase_ = 1.0f;
                output_ = peakLevel_;
                enterDecay();
            } else {
                // Table maps phase [0,1] to normalized level [0,1]
                float tableVal = lookupCurveTable(attackTable_, logPhase_);
                output_ = logStartLevel_ + (peakLevel_ - logStartLevel_) * tableVal;
            }
            return output_;
        }

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

        if (useTableProcessing_) {
            logPhase_ += logPhaseInc_;
            if (logPhase_ >= 1.0f) {
                logPhase_ = 1.0f;
                output_ = sustainTarget;
                stage_ = ADSRStage::Sustain;
            } else {
                // Table maps phase [0,1] to normalized level [1,0]
                float tableVal = lookupCurveTable(decayTable_, logPhase_);
                output_ = sustainTarget + (logStartLevel_ - sustainTarget) * tableVal;
            }
            return output_;
        }

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
        if (useTableProcessing_) {
            logPhase_ += logPhaseInc_;
            if (logPhase_ >= 1.0f) {
                output_ = 0.0f;
                stage_ = ADSRStage::Idle;
            } else {
                // Table maps phase [0,1] to normalized level [1,0]
                float tableVal = lookupCurveTable(releaseTable_, logPhase_);
                output_ = logStartLevel_ * tableVal;
                if (output_ < kEnvelopeIdleThreshold) {
                    output_ = 0.0f;
                    stage_ = ADSRStage::Idle;
                }
            }
            return output_;
        }

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

    // Coefficient calculation delegates to shared envelope_utils.h functions.
    // StageCoefficients, calcEnvCoefficients, getAttackTargetRatio,
    // getDecayTargetRatio are all provided by envelope_utils.h.

    void calcAttackCoefficients() noexcept {
        if (attackCurve_ == EnvCurve::Logarithmic) {
            logPhaseInc_ = 1.0f / std::max(1.0f, attackTimeMs_ * 0.001f * sampleRate_);
        } else {
            float ratio = getAttackTargetRatio(attackCurve_);
            auto coeffs = calcEnvCoefficients(attackTimeMs_, sampleRate_, peakLevel_, ratio, true);
            attackCoef_ = coeffs.coef;
            attackBase_ = coeffs.base;
        }
    }

    void calcDecayCoefficients() noexcept {
        if (decayCurve_ == EnvCurve::Logarithmic) {
            // Phase increment for full peak->0 in decayTime (constant rate)
            logPhaseInc_ = 1.0f / std::max(1.0f, decayTimeMs_ * 0.001f * sampleRate_);
        } else {
            // FR-004: Decay time = full 1.0->0.0 ramp. Target 0.0 with undershoot.
            float ratio = getDecayTargetRatio(decayCurve_);
            auto coeffs = calcEnvCoefficients(decayTimeMs_, sampleRate_, 0.0f, ratio, false);
            decayCoef_ = coeffs.coef;
            decayBase_ = coeffs.base;
        }
    }

    void calcReleaseCoefficients() noexcept {
        if (releaseCurve_ == EnvCurve::Logarithmic) {
            logPhaseInc_ = 1.0f / std::max(1.0f, releaseTimeMs_ * 0.001f * sampleRate_);
        } else {
            float ratio = getDecayTargetRatio(releaseCurve_);
            auto coeffs = calcEnvCoefficients(releaseTimeMs_, sampleRate_, 0.0f, ratio, false);
            releaseCoef_ = coeffs.coef;
            releaseBase_ = coeffs.base;
        }
    }

    void recalcAllCoefficients() noexcept {
        calcAttackCoefficients();
        calcDecayCoefficients();
        calcReleaseCoefficients();
        coefficientsValid_ = true;
    }

    // =========================================================================
    // Curve table cache key
    // =========================================================================

    /// Which setter family last built a segment's curve table.
    enum class CurveTableSource : uint8_t { None = 0, Power, Bezier };

    /// Records @p family and its inputs (the power amount, or the four Bezier
    /// control-point coordinates) as the segment's table source. Returns false
    /// when the table already holds exactly that curve (no rebuild needed).
    static bool claimTable(CurveTableSource& source, std::array<float, 4>& storedInputs,
                           CurveTableSource family, float a, float b = 0.0f,
                           float c = 0.0f, float d = 0.0f) noexcept {
        const std::array<float, 4> inputs{a, b, c, d};
        if (source == family && storedInputs == inputs) return false;
        source = family;
        storedInputs = inputs;
        return true;
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
        if (useTableProcessing_) {
            logStartLevel_ = output_;
            logPhase_ = 0.0f;
            logPhaseInc_ = 1.0f / std::max(1.0f, attackTimeMs_ * 0.001f * sampleRate_);
        } else if (attackCurve_ == EnvCurve::Logarithmic) {
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
        if (useTableProcessing_) {
            logStartLevel_ = output_;
            logPhase_ = 0.0f;
            logPhaseInc_ = 1.0f / std::max(1.0f, decayTimeMs_ * 0.001f * sampleRate_);
        } else if (decayCurve_ == EnvCurve::Logarithmic) {
            logStartLevel_ = output_;
            logPhase_ = 0.0f;
            calcDecayCoefficients();
            // Scale phase increment for partial range (constant rate: full peak->0 in decayTime)
            float fullRange = peakLevel_;
            float actualRange = logStartLevel_ - sustainLevel_ * peakLevel_;
            if (fullRange > 0.0f && actualRange > 0.0f) {
                float fraction = actualRange / fullRange;
                logPhaseInc_ = 1.0f / std::max(1.0f, fraction * decayTimeMs_ * 0.001f * sampleRate_);
            }
        } else {
            calcDecayCoefficients();
        }
    }

    void enterRelease() noexcept {
        stage_ = ADSRStage::Release;
        if (useTableProcessing_) {
            logStartLevel_ = output_;
            logPhase_ = 0.0f;
            logPhaseInc_ = 1.0f / std::max(1.0f, releaseTimeMs_ * 0.001f * sampleRate_);
        } else if (releaseCurve_ == EnvCurve::Logarithmic) {
            logStartLevel_ = output_;
            logPhase_ = 0.0f;
            calcReleaseCoefficients();
            // Scale phase increment for partial range (constant rate: full 1->0 in releaseTime)
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
    // What each curve table was built from (R-001); kept beside stage_ so the
    // three bytes fill its padding.
    CurveTableSource attackTableSource_ = CurveTableSource::None;
    CurveTableSource decayTableSource_ = CurveTableSource::None;
    CurveTableSource releaseTableSource_ = CurveTableSource::None;

    float attackTimeMs_ = 10.0f;
    float decayTimeMs_ = 50.0f;
    float sustainLevel_ = 0.5f;
    float releaseTimeMs_ = 100.0f;

    EnvCurve attackCurve_ = EnvCurve::Exponential;
    EnvCurve decayCurve_ = EnvCurve::Exponential;
    EnvCurve releaseCurve_ = EnvCurve::Exponential;

    // Inputs each curve table was built from: the continuous curve amount
    // (048-adsr-display) in [0], or the four Bezier control-point coordinates.
    std::array<float, 4> attackTableInputs_{};
    std::array<float, 4> decayTableInputs_{};
    std::array<float, 4> releaseTableInputs_{};

    // Curve lookup tables (256 entries each)
    std::array<float, kCurveTableSize> attackTable_{};
    std::array<float, kCurveTableSize> decayTable_{};
    std::array<float, kCurveTableSize> releaseTable_{};
    bool useTableProcessing_ = false;  // Set to true when float curve overloads used
    bool coefficientsValid_ = false;   // true once recalcAllCoefficients() has run

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

    uint32_t tableGenerations_ = 0;  // curve-table rebuild count (test hook)

    bool gateOn_ = false;
};

} // namespace Krate::DSP

