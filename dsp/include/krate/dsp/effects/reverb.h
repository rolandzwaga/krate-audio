// ==============================================================================
// Layer 4: User Feature - Reverb (Dattorro Plate Reverb)
// ==============================================================================
// Implements the Dattorro plate reverb algorithm as described in:
// "Effect Design Part 1: Reverberator and Other Filters"
// (J. Dattorro, J. Audio Eng. Soc., Vol. 45, No. 9, 1997 September)
//
// Features:
// - Input bandwidth filter + 4-stage input diffusion
// - Pre-delay (0-100ms)
// - Figure-eight tank topology with cross-coupled decay loops
// - LFO-modulated allpass diffusion in tank (quadrature phase)
// - Freeze mode for infinite sustain
// - Multi-tap stereo output with mid-side width control
// - Parameter smoothing for click-free transitions
//
// Composes:
// - DelayLine (Layer 1): Pre-delay, tank delays, allpass delay lines
// - OnePoleLP (Layer 1): Bandwidth filter, damping filters
// - DCBlocker (Layer 1): Tank DC blockers
// - SchroederAllpass (Layer 1): Input diffusion stages
// - OnePoleSmoother (Layer 1): Parameter smoothing
//
// Feature: 040-reverb
// Layer: 4 (Effects)
// Reference: specs/040-reverb/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-1)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/comb_filter.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

namespace reverb_detail {

/// Reference sample rate from the Dattorro paper
static constexpr double kReferenceSampleRate = 29761.0;

/// Input diffusion delay lengths at reference rate (FR-003)
static constexpr size_t kInputDiffDelays[4] = {142, 107, 379, 277};

/// Tank A delay lengths at reference rate (FR-006)
static constexpr size_t kTankADD1Delay = 672;
static constexpr size_t kTankAPreDampDelay = 4453;
static constexpr size_t kTankADD2Delay = 1800;
static constexpr size_t kTankAPostDampDelay = 3720;

/// Tank B delay lengths at reference rate (FR-006)
static constexpr size_t kTankBDD1Delay = 908;
static constexpr size_t kTankBPreDampDelay = 4217;
static constexpr size_t kTankBDD2Delay = 2656;
static constexpr size_t kTankBPostDampDelay = 3163;

/// Output tap positions at reference rate (FR-009, Table 2)
/// Left taps: source delay, tap position, sign (+1 or -1)
static constexpr size_t kLeftTapPositions[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};
static constexpr float kLeftTapSigns[7] = {1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f};
// Sources: [0]=B pre-damp, [1]=B pre-damp, [2]=B DD2, [3]=B post-damp,
//          [4]=A pre-damp, [5]=A DD2, [6]=A post-damp

/// Right taps
static constexpr size_t kRightTapPositions[7] = {353, 3627, 1228, 2673, 2111, 335, 121};
static constexpr float kRightTapSigns[7] = {1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f};
// Sources: [0]=A pre-damp, [1]=A pre-damp, [2]=A DD2, [3]=A post-damp,
//          [4]=B pre-damp, [5]=B DD2, [6]=B post-damp

/// Dattorro algorithm coefficients (FR-007)
static constexpr float kDecayDiffusion1 = 0.70f;   // negated when used: -0.70
static constexpr float kDecayDiffusion2 = 0.50f;    // fixed

/// Input bandwidth filter coefficient (FR-012)
static constexpr float kBandwidthCoeff = 0.9995f;

/// LFO max excursion in samples at reference rate (FR-017)
static constexpr float kMaxExcursionRef = 8.0f;

/// Output gain applied to tap sums (R8)
/// Compensates for mono input sum (0.5x) and tap cancellation.
static constexpr float kOutputGain = 3.0f;

/// Parameter smoothing time in milliseconds (R4)
static constexpr float kSmoothingTimeMs = 10.0f;

/// Scale a reference delay length to the operating sample rate (FR-010)
[[nodiscard]] inline size_t scaleDelay(size_t refLength, double sampleRate) noexcept {
    return static_cast<size_t>(
        std::round(static_cast<double>(refLength) * sampleRate / kReferenceSampleRate));
}

/// Compute max delay seconds for a given reference length (with margin for modulation)
[[nodiscard]] inline float maxDelaySeconds(size_t refLength, double sampleRate,
                                            float extraSamples = 0.0f) noexcept {
    double scaledSamples = static_cast<double>(refLength) * sampleRate / kReferenceSampleRate;
    scaledSamples += static_cast<double>(extraSamples);
    scaledSamples += 16.0; // safety margin
    return static_cast<float>(scaledSamples / sampleRate);
}

} // namespace reverb_detail

// =============================================================================
// ReverbParams (FR-011)
// =============================================================================

/// @brief Parameter structure for the Dattorro plate reverb.
///
/// All parameters have well-defined ranges and defaults. Pass to
/// Reverb::setParams() to update all parameters atomically.
struct ReverbParams {
    float roomSize = 0.5f;      ///< Decay control [0.0, 1.0]
    float damping = 0.5f;       ///< HF absorption [0.0, 1.0]
    float width = 1.0f;         ///< Stereo decorrelation [0.0, 1.0]
    float mix = 0.3f;           ///< Dry/wet blend [0.0, 1.0]
    float preDelayMs = 0.0f;    ///< Pre-delay in ms [0.0, 100.0]
    float diffusion = 0.7f;     ///< Input diffusion amount [0.0, 1.0]
    bool freeze = false;        ///< Infinite sustain mode
    float modRate = 0.5f;       ///< Tank LFO rate in Hz [0.0, 2.0]
    float modDepth = 0.0f;      ///< Tank LFO depth [0.0, 1.0]
};

// =============================================================================
// Reverb Class (FR-025)
// =============================================================================

/// @brief Dattorro plate reverb effect (Layer 4).
///
/// Implements the Dattorro plate reverb algorithm with:
/// - Input bandwidth filter + 4-stage input diffusion
/// - Pre-delay (0-100ms)
/// - Figure-eight tank topology with cross-coupled decay loops
/// - LFO-modulated allpass diffusion in tank
/// - Freeze mode for infinite sustain
/// - Multi-tap stereo output with mid-side width control
///
/// @par Usage
/// @code
/// Reverb reverb;
/// reverb.prepare(44100.0);
///
/// ReverbParams params;
/// params.roomSize = 0.7f;
/// params.mix = 0.4f;
/// reverb.setParams(params);
///
/// // In audio callback:
/// reverb.processBlock(leftBuffer, rightBuffer, numSamples);
/// @endcode
class Reverb {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Creates unprepared instance.
    Reverb() noexcept = default;

    /// @brief Prepare the reverb for processing (FR-020).
    ///
    /// Allocates all internal delay lines, initializes filters and LFO.
    /// Must be called before process()/processBlock().
    ///
    /// @param sampleRate Sample rate in Hz [8000, 192000]
    void prepare(double sampleRate) noexcept {
        using namespace reverb_detail;

        sampleRate_ = sampleRate;

        // -- Bandwidth filter (FR-012) --
        // Compute equivalent cutoff Hz from coefficient 0.9995
        // coeff = exp(-2*pi*fc/fs) => fc = -ln(coeff)*fs/(2*pi)
        float bandwidthCutoffHz = static_cast<float>(
            -std::log(static_cast<double>(kBandwidthCoeff)) * sampleRate / static_cast<double>(kTwoPi));
        bandwidthFilter_.prepare(sampleRate);
        bandwidthFilter_.setCutoff(bandwidthCutoffHz);

        // -- Pre-delay (FR-011, R7) --
        float maxPreDelaySec = 0.1f + 0.01f; // 100ms + margin
        preDelay_.prepare(sampleRate, maxPreDelaySec);

        // -- Input diffusion allpasses (FR-002, FR-003) --
        for (int i = 0; i < 4; ++i) {
            float maxSec = maxDelaySeconds(kInputDiffDelays[i], sampleRate);
            inputDiffusion_[i].prepare(sampleRate, maxSec);
            float delaySamples = static_cast<float>(
                std::round(static_cast<double>(kInputDiffDelays[i]) * sampleRate / kReferenceSampleRate));
            inputDiffusion_[i].setDelaySamples(delaySamples);
        }

        // -- LFO excursion scaling (FR-017) --
        maxExcursion_ = static_cast<float>(
            static_cast<double>(kMaxExcursionRef) * sampleRate / kReferenceSampleRate);

        // -- Tank A delay lines (FR-006) --
        // DD1: modulated allpass - needs extra room for LFO excursion
        tankADD1Delay_.prepare(sampleRate,
            maxDelaySeconds(kTankADD1Delay, sampleRate, maxExcursion_ + 2.0f));
        tankADD1Center_ = static_cast<float>(scaleDelay(kTankADD1Delay, sampleRate));

        tankAPreDampDelay_.prepare(sampleRate,
            maxDelaySeconds(kTankAPreDampDelay, sampleRate));
        tankAPreDampLen_ = scaleDelay(kTankAPreDampDelay, sampleRate);

        tankADD2Delay_.prepare(sampleRate,
            maxDelaySeconds(kTankADD2Delay, sampleRate));
        tankADD2Len_ = scaleDelay(kTankADD2Delay, sampleRate);

        tankAPostDampDelay_.prepare(sampleRate,
            maxDelaySeconds(kTankAPostDampDelay, sampleRate));
        tankAPostDampLen_ = scaleDelay(kTankAPostDampDelay, sampleRate);

        // -- Tank B delay lines (FR-006) --
        tankBDD1Delay_.prepare(sampleRate,
            maxDelaySeconds(kTankBDD1Delay, sampleRate, maxExcursion_ + 2.0f));
        tankBDD1Center_ = static_cast<float>(scaleDelay(kTankBDD1Delay, sampleRate));

        tankBPreDampDelay_.prepare(sampleRate,
            maxDelaySeconds(kTankBPreDampDelay, sampleRate));
        tankBPreDampLen_ = scaleDelay(kTankBPreDampDelay, sampleRate);

        tankBDD2Delay_.prepare(sampleRate,
            maxDelaySeconds(kTankBDD2Delay, sampleRate));
        tankBDD2Len_ = scaleDelay(kTankBDD2Delay, sampleRate);

        tankBPostDampDelay_.prepare(sampleRate,
            maxDelaySeconds(kTankBPostDampDelay, sampleRate));
        tankBPostDampLen_ = scaleDelay(kTankBPostDampDelay, sampleRate);

        // -- Damping filters --
        tankADamping_.prepare(sampleRate);
        tankBDamping_.prepare(sampleRate);

        // -- DC blockers (FR-029: 5-20 Hz) --
        // Use 5 Hz (minimum of spec range) to minimize energy drain during freeze
        tankADCBlocker_.prepare(sampleRate, 5.0f);
        tankBDCBlocker_.prepare(sampleRate, 5.0f);

        // -- Scaled output tap positions (FR-009) --
        for (int i = 0; i < 7; ++i) {
            leftTaps_[i] = scaleDelay(kLeftTapPositions[i], sampleRate);
            rightTaps_[i] = scaleDelay(kRightTapPositions[i], sampleRate);
        }

        // -- Parameter smoothers --
        const float sr = static_cast<float>(sampleRate);
        decaySmoother_.configure(kSmoothingTimeMs, sr);
        dampingSmoother_.configure(kSmoothingTimeMs, sr);
        mixSmoother_.configure(kSmoothingTimeMs, sr);
        widthSmoother_.configure(kSmoothingTimeMs, sr);
        inputGainSmoother_.configure(kSmoothingTimeMs, sr);
        preDelaySmoother_.configure(kSmoothingTimeMs, sr);
        diffusion1Smoother_.configure(kSmoothingTimeMs, sr);
        diffusion2Smoother_.configure(kSmoothingTimeMs, sr);
        modDepthSmoother_.configure(kSmoothingTimeMs, sr);

        // -- Initialize smoother targets to defaults --
        ReverbParams defaultParams;
        float defaultDecay = 0.75f + defaultParams.roomSize * 0.2495f;
        float defaultDampCutoff = 200.0f * std::pow(100.0f, 1.0f - defaultParams.damping);

        decaySmoother_.snapTo(defaultDecay);
        dampingSmoother_.snapTo(defaultDampCutoff);
        mixSmoother_.snapTo(defaultParams.mix);
        widthSmoother_.snapTo(defaultParams.width);
        inputGainSmoother_.snapTo(1.0f);
        float defaultPreDelaySamples = static_cast<float>(
            defaultParams.preDelayMs * 0.001f * sr);
        preDelaySmoother_.snapTo(defaultPreDelaySamples);
        diffusion1Smoother_.snapTo(defaultParams.diffusion * 0.75f);
        diffusion2Smoother_.snapTo(defaultParams.diffusion * 0.625f);
        modDepthSmoother_.snapTo(defaultParams.modDepth);

        // -- Set initial damping --
        tankADamping_.setCutoff(defaultDampCutoff);
        tankBDamping_.setCutoff(defaultDampCutoff);

        // -- Set initial input diffusion coefficients --
        inputDiffusion_[0].setCoefficient(defaultParams.diffusion * 0.75f);
        inputDiffusion_[1].setCoefficient(defaultParams.diffusion * 0.75f);
        inputDiffusion_[2].setCoefficient(defaultParams.diffusion * 0.625f);
        inputDiffusion_[3].setCoefficient(defaultParams.diffusion * 0.625f);

        // -- LFO state --
        lfoPhase_ = 0.0f;
        lfoPhaseIncrement_ = static_cast<float>(
            kTwoPi * static_cast<double>(defaultParams.modRate) / sampleRate);

        // -- Tank state --
        tankAOut_ = 0.0f;
        tankBOut_ = 0.0f;

        prepared_ = true;
    }

    /// @brief Reset all internal state to silence (FR-021).
    ///
    /// Clears delay lines, filter states, LFO phase, and tank feedback.
    /// Does not deallocate memory.
    void reset() noexcept {
        // Delay lines
        preDelay_.reset();
        for (auto& ap : inputDiffusion_) ap.reset();
        tankADD1Delay_.reset();
        tankAPreDampDelay_.reset();
        tankADD2Delay_.reset();
        tankAPostDampDelay_.reset();
        tankBDD1Delay_.reset();
        tankBPreDampDelay_.reset();
        tankBDD2Delay_.reset();
        tankBPostDampDelay_.reset();

        // Filters
        bandwidthFilter_.reset();
        tankADamping_.reset();
        tankBDamping_.reset();
        tankADCBlocker_.reset();
        tankBDCBlocker_.reset();

        // Tank state
        tankAOut_ = 0.0f;
        tankBOut_ = 0.0f;

        // LFO
        lfoPhase_ = 0.0f;
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Update all reverb parameters (FR-022).
    ///
    /// Parameters are applied with smoothing (no clicks/pops).
    /// @param params Parameter struct with all values
    void setParams(const ReverbParams& params) noexcept {
        using namespace reverb_detail;

        // Clamp inputs
        float roomSize = std::clamp(params.roomSize, 0.0f, 1.0f);
        float damping = std::clamp(params.damping, 0.0f, 1.0f);
        float width = std::clamp(params.width, 0.0f, 1.0f);
        float mix = std::clamp(params.mix, 0.0f, 1.0f);
        float preDelayMs = std::clamp(params.preDelayMs, 0.0f, 100.0f);
        float diffusion = std::clamp(params.diffusion, 0.0f, 1.0f);
        float modRate = std::clamp(params.modRate, 0.0f, 2.0f);
        float modDepth = std::clamp(params.modDepth, 0.0f, 1.0f);

        // -- RoomSize to decay mapping (FR-011) --
        // Linear: roomSize 0→0.75, 0.5→0.875, 1.0→0.9995
        float targetDecay = 0.75f + roomSize * 0.2495f;

        // -- Damping to cutoff Hz mapping (FR-011, FR-013) --
        float targetDampCutoff = 200.0f * std::pow(100.0f, 1.0f - damping);

        // -- Freeze mode (FR-015, FR-016) --
        freeze_ = params.freeze;
        if (freeze_) {
            targetDecay = 1.0f;
            inputGainSmoother_.setTarget(0.0f);
            // Set damping to Nyquist (bypass filtering)
            float nyquist = static_cast<float>(sampleRate_) * 0.495f;
            dampingSmoother_.setTarget(nyquist);
        } else {
            inputGainSmoother_.setTarget(1.0f);
            dampingSmoother_.setTarget(targetDampCutoff);
        }

        decaySmoother_.setTarget(targetDecay);
        mixSmoother_.setTarget(mix);
        widthSmoother_.setTarget(width);
        modDepthSmoother_.setTarget(modDepth);

        // -- Pre-delay in samples --
        float preDelaySamples = preDelayMs * 0.001f * static_cast<float>(sampleRate_);
        preDelaySmoother_.setTarget(preDelaySamples);

        // -- Diffusion coefficients (R12) --
        diffusion1Smoother_.setTarget(diffusion * 0.75f);
        diffusion2Smoother_.setTarget(diffusion * 0.625f);

        // -- LFO rate --
        lfoPhaseIncrement_ = static_cast<float>(
            kTwoPi * static_cast<double>(modRate) / sampleRate_);
    }

    // =========================================================================
    // Processing (real-time safe) (FR-023, FR-024, FR-026)
    // =========================================================================

    /// @brief Process a single stereo sample pair in-place (FR-023).
    void process(float& left, float& right) noexcept {
        using namespace reverb_detail;

        if (!prepared_) return;

        // -- Step 1: NaN/Inf input validation (FR-027) --
        if (detail::isNaN(left) || detail::isInf(left)) left = 0.0f;
        if (detail::isNaN(right) || detail::isInf(right)) right = 0.0f;

        // -- Step 2: Store dry signal --
        const float dryL = left;
        const float dryR = right;

        // -- Step 3: Advance parameter smoothers --
        float decay = decaySmoother_.process();
        const float dampCutoff = dampingSmoother_.process();
        const float mix = mixSmoother_.process();
        const float width = widthSmoother_.process();
        float inputGain = inputGainSmoother_.process();
        const float preDelaySamples = preDelaySmoother_.process();
        const float diff1 = diffusion1Smoother_.process();
        const float diff2 = diffusion2Smoother_.process();
        const float modDepth = modDepthSmoother_.process();

        // In freeze mode, snap decay to exactly 1.0 and inputGain to exactly 0.0
        // once the smoothers are close enough. This prevents slow energy drain
        // from the smoother never reaching exactly 1.0 (FR-015).
        if (freeze_) {
            if (decay > 0.999f) decay = 1.0f;
            if (inputGain < 0.001f) inputGain = 0.0f;
        }

        // -- Apply damping cutoff to tank filters --
        tankADamping_.setCutoff(dampCutoff);
        tankBDamping_.setCutoff(dampCutoff);

        // -- Apply input diffusion coefficients --
        inputDiffusion_[0].setCoefficient(diff1);
        inputDiffusion_[1].setCoefficient(diff1);
        inputDiffusion_[2].setCoefficient(diff2);
        inputDiffusion_[3].setCoefficient(diff2);

        // -- Step 4: Sum to mono (FR-001) --
        float mono = (left + right) * 0.5f;

        // -- Step 5: Bandwidth filter (FR-012) --
        mono = bandwidthFilter_.process(mono);

        // -- Step 6: Pre-delay (FR-011) --
        preDelay_.write(mono);
        float preDelayed = preDelay_.readLinear(std::max(0.0f, preDelaySamples));

        // -- Step 7: Input diffusion (FR-002, FR-003) --
        float diffused = preDelayed;
        // Skip diffusion when coefficients are near zero (optimization)
        if (diff1 > 0.001f || diff2 > 0.001f) {
            diffused = inputDiffusion_[0].process(diffused);
            diffused = inputDiffusion_[1].process(diffused);
            diffused = inputDiffusion_[2].process(diffused);
            diffused = inputDiffusion_[3].process(diffused);
        }

        // Apply input gain (for freeze: gain -> 0.0)
        diffused *= inputGain;

        // -- Step 8: LFO computation (FR-017, FR-018) --
        float lfoA = 0.0f;
        float lfoB = 0.0f;
        if (modDepth > 0.0001f) {
            lfoA = std::sin(lfoPhase_) * modDepth * maxExcursion_;
            lfoB = std::cos(lfoPhase_) * modDepth * maxExcursion_;  // 90-degree offset
        }

        // -- Step 9: Tank A processing (FR-004, FR-005) --
        {
            // Tank A input: diffused + decay * tankBOut_
            float tankAInput = diffused + decay * tankBOut_;

            // Decay Diffusion 1 (modulated allpass, coeff = -0.70) (FR-007)
            float dd1Delay = tankADD1Center_ + lfoA;
            dd1Delay = std::max(1.0f, dd1Delay);
            float dd1Delayed = tankADD1Delay_.readLinear(dd1Delay);  // FR-019: linear interp
            float dd1Coeff = -kDecayDiffusion1;
            float dd1Out = dd1Coeff * tankAInput + dd1Delayed;
            tankADD1Delay_.write(tankAInput + (-dd1Coeff) * dd1Out);

            // Pre-damping delay
            tankAPreDampDelay_.write(dd1Out);
            float preDamped = tankAPreDampDelay_.read(tankAPreDampLen_);

            // Damping filter - bypass during freeze to preserve energy (FR-015)
            float damped = (freeze_ && decay >= 1.0f) ? preDamped
                                                       : tankADamping_.process(preDamped);

            // Decay gain
            damped *= decay;

            // Decay Diffusion 2 (allpass, coeff = 0.50) (FR-007)
            float dd2Delayed = tankADD2Delay_.readLinear(static_cast<float>(tankADD2Len_));
            float dd2Coeff = kDecayDiffusion2;
            float dd2Out = -dd2Coeff * damped + dd2Delayed;
            tankADD2Delay_.write(damped + dd2Coeff * dd2Out);

            // Post-damping delay
            tankAPostDampDelay_.write(dd2Out);
            float postDamped = tankAPostDampDelay_.read(tankAPostDampLen_);

            // DC blocker (FR-029) - bypass during freeze to preserve energy
            if (freeze_ && decay >= 1.0f) {
                tankAOut_ = postDamped;
            } else {
                tankAOut_ = tankADCBlocker_.process(postDamped);
            }
            tankAOut_ = detail::flushDenormal(tankAOut_);  // FR-028
        }

        // -- Step 10: Tank B processing (FR-004, FR-005) --
        {
            // Tank B input: diffused + decay * tankAOut_
            float tankBInput = diffused + decay * tankAOut_;

            // Decay Diffusion 1 (modulated allpass, coeff = -0.70) (FR-007)
            float dd1Delay = tankBDD1Center_ + lfoB;
            dd1Delay = std::max(1.0f, dd1Delay);
            float dd1Delayed = tankBDD1Delay_.readLinear(dd1Delay);
            float dd1Coeff = -kDecayDiffusion1;
            float dd1Out = dd1Coeff * tankBInput + dd1Delayed;
            tankBDD1Delay_.write(tankBInput + (-dd1Coeff) * dd1Out);

            // Pre-damping delay
            tankBPreDampDelay_.write(dd1Out);
            float preDamped = tankBPreDampDelay_.read(tankBPreDampLen_);

            // Damping filter - bypass during freeze to preserve energy (FR-015)
            float damped = (freeze_ && decay >= 1.0f) ? preDamped
                                                       : tankBDamping_.process(preDamped);

            // Decay gain
            damped *= decay;

            // Decay Diffusion 2 (allpass, coeff = 0.50) (FR-007)
            float dd2Delayed = tankBDD2Delay_.readLinear(static_cast<float>(tankBDD2Len_));
            float dd2Coeff = kDecayDiffusion2;
            float dd2Out = -dd2Coeff * damped + dd2Delayed;
            tankBDD2Delay_.write(damped + dd2Coeff * dd2Out);

            // Post-damping delay
            tankBPostDampDelay_.write(dd2Out);
            float postDamped = tankBPostDampDelay_.read(tankBPostDampLen_);

            // DC blocker (FR-029) - bypass during freeze to preserve energy
            if (freeze_ && decay >= 1.0f) {
                tankBOut_ = postDamped;
            } else {
                tankBOut_ = tankBDCBlocker_.process(postDamped);
            }
            tankBOut_ = detail::flushDenormal(tankBOut_);  // FR-028
        }

        // -- Step 11: Output tap computation (FR-008, FR-009) --
        float yL = 0.0f;
        float yR = 0.0f;

        // Left output taps (Table 2)
        // [0] +tap(Tank B pre-damp, 266)
        yL += kLeftTapSigns[0] *
              tankBPreDampDelay_.read(leftTaps_[0]);
        // [1] +tap(Tank B pre-damp, 2974)
        yL += kLeftTapSigns[1] *
              tankBPreDampDelay_.read(leftTaps_[1]);
        // [2] -tap(Tank B DD2, 1913)
        yL += kLeftTapSigns[2] *
              tankBDD2Delay_.read(leftTaps_[2]);
        // [3] +tap(Tank B post-damp, 1996)
        yL += kLeftTapSigns[3] *
              tankBPostDampDelay_.read(leftTaps_[3]);
        // [4] -tap(Tank A pre-damp, 1990)
        yL += kLeftTapSigns[4] *
              tankAPreDampDelay_.read(leftTaps_[4]);
        // [5] -tap(Tank A DD2, 187)
        yL += kLeftTapSigns[5] *
              tankADD2Delay_.read(leftTaps_[5]);
        // [6] +tap(Tank A post-damp, 1066)
        yL += kLeftTapSigns[6] *
              tankAPostDampDelay_.read(leftTaps_[6]);

        // Right output taps (Table 2)
        // [0] +tap(Tank A pre-damp, 353)
        yR += kRightTapSigns[0] *
              tankAPreDampDelay_.read(rightTaps_[0]);
        // [1] +tap(Tank A pre-damp, 3627)
        yR += kRightTapSigns[1] *
              tankAPreDampDelay_.read(rightTaps_[1]);
        // [2] -tap(Tank A DD2, 1228)
        yR += kRightTapSigns[2] *
              tankADD2Delay_.read(rightTaps_[2]);
        // [3] +tap(Tank A post-damp, 2673)
        yR += kRightTapSigns[3] *
              tankAPostDampDelay_.read(rightTaps_[3]);
        // [4] -tap(Tank B pre-damp, 2111)
        yR += kRightTapSigns[4] *
              tankBPreDampDelay_.read(rightTaps_[4]);
        // [5] -tap(Tank B DD2, 335)
        yR += kRightTapSigns[5] *
              tankBDD2Delay_.read(rightTaps_[5]);
        // [6] +tap(Tank B post-damp, 121)
        yR += kRightTapSigns[6] *
              tankBPostDampDelay_.read(rightTaps_[6]);

        // Apply output gain (R8)
        yL *= kOutputGain;
        yR *= kOutputGain;

        // -- Step 12: Stereo width processing (FR-011a) --
        float mid = 0.5f * (yL + yR);
        float side = 0.5f * (yL - yR);
        float wetL = mid + width * side;
        float wetR = mid - width * side;

        // -- Step 13: Dry/wet mix (FR-011) --
        left = (1.0f - mix) * dryL + mix * wetL;
        right = (1.0f - mix) * dryR + mix * wetR;

        // -- Step 14: Advance LFO phase --
        lfoPhase_ += lfoPhaseIncrement_;
        if (lfoPhase_ >= kTwoPi) {
            lfoPhase_ -= kTwoPi;
        }
    }

    /// @brief Process a block of stereo samples in-place (FR-024).
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            process(left[i], right[i]);
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if the reverb has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Configuration
    // =========================================================================
    double sampleRate_ = 0.0;
    bool prepared_ = false;
    bool freeze_ = false;

    // =========================================================================
    // Input section
    // =========================================================================
    OnePoleLP bandwidthFilter_;
    DelayLine preDelay_;
    SchroederAllpass inputDiffusion_[4];

    // =========================================================================
    // Tank A
    // =========================================================================
    DelayLine tankADD1Delay_;       ///< Decay diffusion 1 (standalone for tap access + LFO)
    DelayLine tankAPreDampDelay_;   ///< Pre-damping delay
    OnePoleLP tankADamping_;        ///< Damping lowpass filter
    DelayLine tankADD2Delay_;       ///< Decay diffusion 2 (standalone for tap access)
    DelayLine tankAPostDampDelay_;  ///< Post-damping delay
    DCBlocker tankADCBlocker_;      ///< DC blocker

    // =========================================================================
    // Tank B
    // =========================================================================
    DelayLine tankBDD1Delay_;       ///< Decay diffusion 1 (standalone for tap access + LFO)
    DelayLine tankBPreDampDelay_;   ///< Pre-damping delay
    OnePoleLP tankBDamping_;        ///< Damping lowpass filter
    DelayLine tankBDD2Delay_;       ///< Decay diffusion 2 (standalone for tap access)
    DelayLine tankBPostDampDelay_;  ///< Post-damping delay
    DCBlocker tankBDCBlocker_;      ///< DC blocker

    // =========================================================================
    // Tank state variables
    // =========================================================================
    float tankAOut_ = 0.0f;
    float tankBOut_ = 0.0f;

    // =========================================================================
    // Scaled delay lengths (computed in prepare())
    // =========================================================================
    float tankADD1Center_ = 0.0f;
    size_t tankAPreDampLen_ = 0;
    size_t tankADD2Len_ = 0;
    size_t tankAPostDampLen_ = 0;

    float tankBDD1Center_ = 0.0f;
    size_t tankBPreDampLen_ = 0;
    size_t tankBDD2Len_ = 0;
    size_t tankBPostDampLen_ = 0;

    // =========================================================================
    // Scaled output tap positions
    // =========================================================================
    size_t leftTaps_[7] = {};
    size_t rightTaps_[7] = {};

    // =========================================================================
    // LFO state
    // =========================================================================
    float lfoPhase_ = 0.0f;
    float lfoPhaseIncrement_ = 0.0f;
    float maxExcursion_ = 0.0f;

    // =========================================================================
    // Parameter smoothers
    // =========================================================================
    OnePoleSmoother decaySmoother_;
    OnePoleSmoother dampingSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother preDelaySmoother_;
    OnePoleSmoother diffusion1Smoother_;
    OnePoleSmoother diffusion2Smoother_;
    OnePoleSmoother modDepthSmoother_;
};

} // namespace DSP
} // namespace Krate
