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
// Optimizations (spec 125-dual-reverb):
// - Gordon-Smith (magic circle) phasor replaces std::sin/std::cos LFO (FR-001)
// - Block-rate parameter smoothing at 16-sample sub-blocks (FR-002, FR-003)
// - Single contiguous delay buffer for all 13 delay lines (FR-004)
// - Redundant flushDenormal() removed, FTZ/DAZ assumed (FR-005)
//
// Composes:
// - OnePoleLP (Layer 1): Bandwidth filter, damping filters
// - DCBlocker (Layer 1): Tank DC blockers
// - OnePoleSmoother (Layer 1): Parameter smoothing
//
// Feature: 040-reverb, 125-dual-reverb
// Layer: 4 (Effects)
// Reference: specs/040-reverb/spec.md, specs/125-dual-reverb/spec.md
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
#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

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

/// Output gain applied to tap sums (R8).
/// The mono sum (0.5x) reduces input by half. In a well-diffused tank with
/// 7 partially-decorrelated taps (4 additive, 3 subtractive, net sign = +1),
/// the RMS sum ≈ sqrt(7) ≈ 2.65x a single tap. The gain is set to produce
/// approximately unity RMS output for typical settings (roomSize=0.5, damping=0.5).
/// Recalibrated after fixing the bandwidth filter coefficient derivation
/// (was previously 3.0 when the filter incorrectly acted as a 3.8 Hz lowpass).
static constexpr float kOutputGain = 0.6f;

/// Parameter smoothing time in milliseconds (R4)
static constexpr float kSmoothingTimeMs = 10.0f;

/// Sub-block size for block-rate parameter smoothing (FR-002)
static constexpr size_t kSubBlockSize = 16;

/// Total number of delay sections in the contiguous buffer
static constexpr size_t kNumDelaySections = 13;

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
        // Dattorro paper specifies bandwidth = 0.9995 as a mixing coefficient:
        //   y[n] = bw * x[n] + (1-bw) * y[n-1]
        // Our OnePoleLP uses y[n] = (1-a) * x[n] + a * y[n-1], so a = 1 - bw.
        // Scale for sample rate: a_scaled = 1 - pow(1 - a_ref, refRate / sampleRate)
        float aRef = 1.0f - kBandwidthCoeff;  // 0.0005 at reference rate
        float aScaled = 1.0f - std::pow(1.0f - aRef,
            static_cast<float>(kReferenceSampleRate / sampleRate));
        bandwidthFilter_.prepare(sampleRate);
        bandwidthFilter_.setCoefficient(aScaled);

        // -- LFO excursion scaling (FR-017) --
        maxExcursion_ = static_cast<float>(
            static_cast<double>(kMaxExcursionRef) * sampleRate / kReferenceSampleRate);

        // -- Allocate contiguous delay buffer (FR-004) --
        // Section layout: [preDelay][inputDiff0..3][tankADD1][tankAPreDamp][tankADD2]
        //                  [tankAPostDamp][tankBDD1][tankBPreDamp][tankBDD2][tankBPostDamp]
        // Pre-delay: up to 100ms + margin
        size_t preDelayMaxSamples = static_cast<size_t>(sampleRate * 0.11) + 16;

        // Reference delay lengths to scale (13 sections total)
        const size_t refDelays[kNumDelaySections] = {
            0, // placeholder for pre-delay (sized differently)
            kInputDiffDelays[0], kInputDiffDelays[1],
            kInputDiffDelays[2], kInputDiffDelays[3],
            kTankADD1Delay, kTankAPreDampDelay, kTankADD2Delay, kTankAPostDampDelay,
            kTankBDD1Delay, kTankBPreDampDelay, kTankBDD2Delay, kTankBPostDampDelay
        };

        // Extra samples needed for modulation on DD1 delays
        const float extraSamples[kNumDelaySections] = {
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            maxExcursion_ + 2.0f, 0.0f, 0.0f, 0.0f,
            maxExcursion_ + 2.0f, 0.0f, 0.0f, 0.0f
        };

        totalBufferSize_ = 0;
        for (size_t s = 0; s < kNumDelaySections; ++s) {
            size_t maxSamples;
            if (s == 0) {
                // Pre-delay section
                maxSamples = preDelayMaxSamples;
            } else {
                double scaled = static_cast<double>(refDelays[s]) * sampleRate / kReferenceSampleRate;
                scaled += static_cast<double>(extraSamples[s]);
                scaled += 16.0; // safety margin
                maxSamples = static_cast<size_t>(scaled) + 1;
            }
            sectionSizes_[s] = nextPowerOf2(maxSamples);
            sectionMasks_[s] = sectionSizes_[s] - 1;
            sectionOffsets_[s] = totalBufferSize_;
            totalBufferSize_ += sectionSizes_[s];
        }

        contiguousBuffer_.assign(totalBufferSize_, 0.0f);

        // Reset write positions
        for (auto& wp : writePos_) wp = 0;

        // -- Compute scaled delay lengths --
        for (int i = 0; i < 4; ++i) {
            inputDiffDelaySamples_[i] = static_cast<float>(
                std::round(static_cast<double>(kInputDiffDelays[i]) * sampleRate / kReferenceSampleRate));
        }

        tankADD1Center_ = static_cast<float>(scaleDelay(kTankADD1Delay, sampleRate));
        tankAPreDampLen_ = scaleDelay(kTankAPreDampDelay, sampleRate);
        tankADD2Len_ = scaleDelay(kTankADD2Delay, sampleRate);
        tankAPostDampLen_ = scaleDelay(kTankAPostDampDelay, sampleRate);

        tankBDD1Center_ = static_cast<float>(scaleDelay(kTankBDD1Delay, sampleRate));
        tankBPreDampLen_ = scaleDelay(kTankBPreDampDelay, sampleRate);
        tankBDD2Len_ = scaleDelay(kTankBDD2Delay, sampleRate);
        tankBPostDampLen_ = scaleDelay(kTankBPostDampDelay, sampleRate);

        // -- Scaled output tap positions (FR-009) --
        for (int i = 0; i < 7; ++i) {
            leftTaps_[i] = scaleDelay(kLeftTapPositions[i], sampleRate);
            rightTaps_[i] = scaleDelay(kRightTapPositions[i], sampleRate);
        }

        // Bounds-check taps against their source section delay lengths
        // Left: [BPreDamp, BPreDamp, BDD2, BPostDamp, APreDamp, ADD2, APostDamp]
        const size_t leftMaxLens[7] = {
            tankBPreDampLen_, tankBPreDampLen_, tankBDD2Len_, tankBPostDampLen_,
            tankAPreDampLen_, tankADD2Len_, tankAPostDampLen_
        };
        // Right: [APreDamp, APreDamp, ADD2, APostDamp, BPreDamp, BDD2, BPostDamp]
        const size_t rightMaxLens[7] = {
            tankAPreDampLen_, tankAPreDampLen_, tankADD2Len_, tankAPostDampLen_,
            tankBPreDampLen_, tankBDD2Len_, tankBPostDampLen_
        };
        for (int i = 0; i < 7; ++i) {
            if (leftMaxLens[i] > 0)
                leftTaps_[i] = std::min(leftTaps_[i], leftMaxLens[i] - 1);
            if (rightMaxLens[i] > 0)
                rightTaps_[i] = std::min(rightTaps_[i], rightMaxLens[i] - 1);
        }

        // -- Damping filters --
        tankADamping_.prepare(sampleRate);
        tankBDamping_.prepare(sampleRate);

        // -- DC blockers (FR-029: 5-20 Hz) --
        tankADCBlocker_.prepare(sampleRate, 5.0f);
        tankBDCBlocker_.prepare(sampleRate, 5.0f);

        // -- Parameter smoothers --
        const float sr = static_cast<float>(sampleRate);
        decaySmoother_.configure(kSmoothingTimeMs, sr);
        dampingSmoother_.configure(kSmoothingTimeMs, sr);
        dryGainSmoother_.configure(kSmoothingTimeMs, sr);
        wetGainSmoother_.configure(kSmoothingTimeMs, sr);
        widthSmoother_.configure(kSmoothingTimeMs, sr);
        inputGainSmoother_.configure(kSmoothingTimeMs, sr);
        preDelaySmoother_.configure(kSmoothingTimeMs, sr);
        diffusion1Smoother_.configure(kSmoothingTimeMs, sr);
        diffusion2Smoother_.configure(kSmoothingTimeMs, sr);
        modDepthSmoother_.configure(kSmoothingTimeMs, sr);

        // -- Initialize smoother targets to defaults --
        ReverbParams defaultParams;
        float defaultDecay = 0.50f + defaultParams.roomSize * 0.40f;
        float defaultDampCutoff = 200.0f * std::pow(100.0f, 1.0f - defaultParams.damping);

        decaySmoother_.snapTo(defaultDecay);
        dampingSmoother_.snapTo(defaultDampCutoff);
        dryGainSmoother_.snapTo(std::cos(defaultParams.mix * kHalfPi));
        wetGainSmoother_.snapTo(std::sin(defaultParams.mix * kHalfPi));
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
        inputDiffCoeff1_ = defaultParams.diffusion * 0.75f;
        inputDiffCoeff2_ = defaultParams.diffusion * 0.625f;

        // -- Gordon-Smith LFO state (FR-001) --
        // Initialize at phase 0: sin=0, cos=1
        sinState_ = 0.0f;
        cosState_ = 1.0f;
        lfoEpsilon_ = 2.0f * static_cast<float>(std::sin(
            kPi * static_cast<double>(defaultParams.modRate) / sampleRate));

        // -- Tank state --
        tankAOut_ = 0.0f;
        tankBOut_ = 0.0f;

        // -- Input diffusion allpass state --
        for (auto& s : inputDiffState_) s = 0.0f;

        prepared_ = true;
    }

    /// @brief Reset all internal state to silence (FR-021).
    ///
    /// Clears delay lines, filter states, LFO phase, and tank feedback.
    /// Does not deallocate memory.
    void reset() noexcept {
        // Zero-fill contiguous buffer
        std::fill(contiguousBuffer_.begin(), contiguousBuffer_.end(), 0.0f);

        // Reset write positions
        for (auto& wp : writePos_) wp = 0;

        // Filters
        bandwidthFilter_.reset();
        tankADamping_.reset();
        tankBDamping_.reset();
        tankADCBlocker_.reset();
        tankBDCBlocker_.reset();

        // Tank state
        tankAOut_ = 0.0f;
        tankBOut_ = 0.0f;

        // Input diffusion allpass state
        for (auto& s : inputDiffState_) s = 0.0f;

        // LFO - reinitialize at phase 0 (FR-001)
        sinState_ = 0.0f;
        cosState_ = 1.0f;
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
        // With correct near-transparent bandwidth filter, the tank receives full
        // input energy. Decay is applied 4x per complete loop (2x in each tank),
        // so small changes near 1.0 have enormous effect on RT60.
        // Range [0.50, 0.90] → RT60 ~[2s, 12s] for musically useful plate reverb.
        // (Was [0.75, 0.9995] when bandwidth filter incorrectly blocked input.)
        float targetDecay = 0.50f + roomSize * 0.40f;

        // -- Damping to cutoff Hz mapping (FR-011, FR-013) --
        float targetDampCutoff = 200.0f * std::pow(100.0f, 1.0f - damping);

        // -- Freeze mode (FR-015, FR-016) --
        freeze_ = params.freeze;
        if (freeze_) {
            targetDecay = 1.0f;
            inputGainSmoother_.setTarget(0.0f);
            float nyquist = static_cast<float>(sampleRate_) * 0.495f;
            dampingSmoother_.setTarget(nyquist);
        } else {
            inputGainSmoother_.setTarget(1.0f);
            dampingSmoother_.setTarget(targetDampCutoff);
        }

        decaySmoother_.setTarget(targetDecay);
        dryGainSmoother_.setTarget(std::cos(mix * kHalfPi));
        wetGainSmoother_.setTarget(std::sin(mix * kHalfPi));
        widthSmoother_.setTarget(width);
        modDepthSmoother_.setTarget(modDepth);

        // -- Pre-delay in samples --
        float preDelaySamples = preDelayMs * 0.001f * static_cast<float>(sampleRate_);
        preDelaySmoother_.setTarget(preDelaySamples);

        // -- Diffusion coefficients (R12) --
        diffusion1Smoother_.setTarget(diffusion * 0.75f);
        diffusion2Smoother_.setTarget(diffusion * 0.625f);

        // -- LFO rate (FR-001: Gordon-Smith epsilon) --
        float newEpsilon = 2.0f * static_cast<float>(std::sin(
            kPi * static_cast<double>(modRate) / sampleRate_));
        if (newEpsilon != lfoEpsilon_) {
            // Renormalize phasor states to unit circle when epsilon changes
            // to prevent amplitude drift (Gordon-Smith phasor is only
            // amplitude-stable when epsilon is constant)
            float r2 = sinState_ * sinState_ + cosState_ * cosState_;
            if (r2 > 0.0f) {
                float invR = 1.0f / std::sqrt(r2);
                sinState_ *= invR;
                cosState_ *= invR;
            }
            lfoEpsilon_ = newEpsilon;
        }
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
        const float dryGain = dryGainSmoother_.process();
        const float wetGain = wetGainSmoother_.process();
        const float width = widthSmoother_.process();
        float inputGain = inputGainSmoother_.process();
        const float preDelaySamples = preDelaySmoother_.process();
        const float diff1 = diffusion1Smoother_.process();
        const float diff2 = diffusion2Smoother_.process();
        const float modDepth = modDepthSmoother_.process();

        // In freeze mode, snap decay to exactly 1.0 and inputGain to exactly 0.0
        if (freeze_) {
            if (decay > 0.999f) decay = 1.0f;
            if (inputGain < 0.001f) inputGain = 0.0f;
        }

        // -- Apply damping cutoff to tank filters (only when changed) --
        if (dampCutoff != cachedDampCutoff_) {
            tankADamping_.setCutoff(dampCutoff);
            tankBDamping_.setCutoff(dampCutoff);
            cachedDampCutoff_ = dampCutoff;
        }

        // -- Store input diffusion coefficients --
        inputDiffCoeff1_ = diff1;
        inputDiffCoeff2_ = diff2;

        // Process one sample with the core algorithm
        processOneSample(left, right, dryL, dryR, decay, dryGain, wetGain,
                         width, inputGain, preDelaySamples, modDepth);
    }

    /// @brief Process a block of stereo samples in-place (FR-024).
    ///
    /// Uses 16-sample sub-blocks for block-rate parameter smoothing (FR-002).
    /// Parameter changes arriving via setParams() are latched at the next
    /// sub-block boundary (FR-002, FR-003).
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        using namespace reverb_detail;

        if (!prepared_) return;

        size_t offset = 0;
        while (offset < numSamples) {
            const size_t blockLen = std::min(kSubBlockSize, numSamples - offset);

            // -- Update smoothers once per sub-block (FR-002) --
            float decay = decaySmoother_.process();
            const float dampCutoff = dampingSmoother_.process();
            const float dryGain = dryGainSmoother_.process();
            const float wetGain = wetGainSmoother_.process();
            const float width = widthSmoother_.process();
            float inputGain = inputGainSmoother_.process();
            const float preDelaySamples = preDelaySmoother_.process();
            const float diff1 = diffusion1Smoother_.process();
            const float diff2 = diffusion2Smoother_.process();
            const float modDepth = modDepthSmoother_.process();

            // Advance smoothers for remaining sub-block samples (FR-002)
            if (blockLen > 1) {
                decaySmoother_.advanceSamples(blockLen - 1);
                dampingSmoother_.advanceSamples(blockLen - 1);
                dryGainSmoother_.advanceSamples(blockLen - 1);
                wetGainSmoother_.advanceSamples(blockLen - 1);
                widthSmoother_.advanceSamples(blockLen - 1);
                inputGainSmoother_.advanceSamples(blockLen - 1);
                preDelaySmoother_.advanceSamples(blockLen - 1);
                diffusion1Smoother_.advanceSamples(blockLen - 1);
                diffusion2Smoother_.advanceSamples(blockLen - 1);
                modDepthSmoother_.advanceSamples(blockLen - 1);
            }

            // In freeze mode, snap values
            if (freeze_) {
                if (decay > 0.999f) decay = 1.0f;
                if (inputGain < 0.001f) inputGain = 0.0f;
            }

            // -- Update filter coefficients once per sub-block (FR-003) --
            tankADamping_.setCutoff(dampCutoff);
            tankBDamping_.setCutoff(dampCutoff);
            inputDiffCoeff1_ = diff1;
            inputDiffCoeff2_ = diff2;

            // -- Process sub-block samples with held values --
            for (size_t i = 0; i < blockLen; ++i) {
                float& l = left[offset + i];
                float& r = right[offset + i];

                // NaN/Inf input validation
                if (detail::isNaN(l) || detail::isInf(l)) l = 0.0f;
                if (detail::isNaN(r) || detail::isInf(r)) r = 0.0f;

                const float dryL = l;
                const float dryR = r;

                processOneSample(l, r, dryL, dryR, decay, dryGain, wetGain,
                                 width, inputGain, preDelaySamples, modDepth);
            }

            offset += blockLen;
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if the reverb has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get the total contiguous delay buffer size in samples (FR-004).
    [[nodiscard]] size_t totalBufferSize() const noexcept {
        return totalBufferSize_;
    }

private:
    // =========================================================================
    // Contiguous buffer helpers (FR-004)
    // =========================================================================

    /// Section indices into the contiguous buffer
    enum Section : size_t {
        kPreDelay = 0,
        kInputDiff0 = 1,
        kInputDiff1 = 2,
        kInputDiff2 = 3,
        kInputDiff3 = 4,
        kTankADD1 = 5,
        kTankAPreDamp = 6,
        kTankADD2 = 7,
        kTankAPostDamp = 8,
        kTankBDD1 = 9,
        kTankBPreDamp = 10,
        kTankBDD2 = 11,
        kTankBPostDamp = 12
    };

    /// Write a sample to a section
    void bufWrite(Section s, float sample) noexcept {
        contiguousBuffer_[sectionOffsets_[s] + writePos_[s]] = sample;
        writePos_[s] = (writePos_[s] + 1) & sectionMasks_[s];
    }

    /// Read from a section at a fixed integer delay.
    /// read(0) returns the most recently written sample (like DelayLine::read(0)).
    [[nodiscard]] float bufRead(Section s, size_t delaySamples) const noexcept {
        // writePos_ points to the NEXT write slot; most recent write is at writePos_ - 1
        size_t readPos = (writePos_[s] - 1 - delaySamples) & sectionMasks_[s];
        return contiguousBuffer_[sectionOffsets_[s] + readPos];
    }

    /// Read from a section with linear interpolation (for pre-delay)
    [[nodiscard]] float bufReadLinear(Section s, float delaySamples) const noexcept {
        float floored = std::floor(delaySamples);
        size_t delayInt = static_cast<size_t>(floored);
        float frac = delaySamples - floored;
        float a = bufRead(s, delayInt);
        float b = bufRead(s, delayInt + 1);
        return a + frac * (b - a);
    }

    /// Cubic Hermite (Catmull-Rom) interpolated read for modulated delay lines.
    /// Reduces HF loss compared to linear interpolation in recirculating paths.
    [[nodiscard]] float bufReadCubic(Section s, float delaySamples) const noexcept {
        float floored = std::floor(delaySamples);
        size_t delayInt = static_cast<size_t>(floored);
        float frac = delaySamples - floored;
        float ym1 = bufRead(s, delayInt > 0 ? delayInt - 1 : 0);
        float y0  = bufRead(s, delayInt);
        float y1  = bufRead(s, delayInt + 1);
        float y2  = bufRead(s, delayInt + 2);
        return Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, frac);
    }

    // =========================================================================
    // Core sample processing (shared between process() and processBlock())
    // =========================================================================

    void processOneSample(float& left, float& right,
                          float dryL, float dryR,
                          float decay, float dryGain, float wetGain,
                          float width, float inputGain,
                          float preDelaySamples,
                          float modDepth) noexcept {
        using namespace reverb_detail;

        // -- Sum to mono (FR-001) --
        float mono = (left + right) * 0.5f;

        // -- Bandwidth filter (FR-012) --
        mono = bandwidthFilter_.process(mono);

        // -- Pre-delay (FR-011) --
        bufWrite(kPreDelay, mono);
        float preDelayed = bufReadLinear(kPreDelay, std::max(0.0f, preDelaySamples));

        // -- Input diffusion (FR-002, FR-003) --
        // Always process allpasses to keep buffer state fresh. When coefficients
        // are near zero, allpasses become pure delays (cheap, no stale audio).
        float diffused = preDelayed;
        diffused = processAllpass(kInputDiff0, diffused, inputDiffCoeff1_,
                                  inputDiffDelaySamples_[0]);
        diffused = processAllpass(kInputDiff1, diffused, inputDiffCoeff1_,
                                  inputDiffDelaySamples_[1]);
        diffused = processAllpass(kInputDiff2, diffused, inputDiffCoeff2_,
                                  inputDiffDelaySamples_[2]);
        diffused = processAllpass(kInputDiff3, diffused, inputDiffCoeff2_,
                                  inputDiffDelaySamples_[3]);

        // Apply input gain (for freeze: gain -> 0.0)
        diffused *= inputGain;

        // -- LFO computation (FR-001: Gordon-Smith magic circle) --
        float lfoA = 0.0f;
        float lfoB = 0.0f;
        if (modDepth > 0.0001f) {
            lfoA = sinState_ * modDepth * maxExcursion_;
            lfoB = cosState_ * modDepth * maxExcursion_;
        }

        // -- Tank A processing (FR-004, FR-005) --
        {
            float tankAInput = diffused + decay * tankBOut_;

            // Decay Diffusion 1 (modulated allpass, coeff = -0.70)
            float dd1Delay = tankADD1Center_ + lfoA;
            dd1Delay = std::max(1.0f, dd1Delay);
            float dd1Delayed = bufReadCubic(kTankADD1, dd1Delay);
            float dd1Coeff = -kDecayDiffusion1;
            float dd1Out = dd1Coeff * tankAInput + dd1Delayed;
            bufWrite(kTankADD1, tankAInput + (-dd1Coeff) * dd1Out);

            // Pre-damping delay
            bufWrite(kTankAPreDamp, dd1Out);
            float preDamped = bufRead(kTankAPreDamp, tankAPreDampLen_);

            // Damping filter - bypass during freeze to preserve energy
            float damped = freeze_ ? preDamped
                                   : tankADamping_.process(preDamped);
            damped *= decay;

            // Decay Diffusion 2 (allpass, coeff = 0.50)
            float dd2Delayed = bufRead(kTankADD2, tankADD2Len_);
            float dd2Coeff = kDecayDiffusion2;
            float dd2Out = -dd2Coeff * damped + dd2Delayed;
            bufWrite(kTankADD2, damped + dd2Coeff * dd2Out);

            // Post-damping delay
            bufWrite(kTankAPostDamp, dd2Out);
            float postDamped = bufRead(kTankAPostDamp, tankAPostDampLen_);

            // DC blocker - bypass during freeze to preserve energy
            if (freeze_) {
                tankAOut_ = postDamped;
            } else {
                tankAOut_ = tankADCBlocker_.process(postDamped);
            }
            // FR-005: flushDenormal() removed -- FTZ/DAZ assumed at process entry
        }

        // -- Tank B processing (FR-004, FR-005) --
        {
            float tankBInput = diffused + decay * tankAOut_;

            // Decay Diffusion 1 (modulated allpass, coeff = -0.70)
            float dd1Delay = tankBDD1Center_ + lfoB;
            dd1Delay = std::max(1.0f, dd1Delay);
            float dd1Delayed = bufReadCubic(kTankBDD1, dd1Delay);
            float dd1Coeff = -kDecayDiffusion1;
            float dd1Out = dd1Coeff * tankBInput + dd1Delayed;
            bufWrite(kTankBDD1, tankBInput + (-dd1Coeff) * dd1Out);

            // Pre-damping delay
            bufWrite(kTankBPreDamp, dd1Out);
            float preDamped = bufRead(kTankBPreDamp, tankBPreDampLen_);

            // Damping filter - bypass during freeze to preserve energy
            float damped = freeze_ ? preDamped
                                   : tankBDamping_.process(preDamped);
            damped *= decay;

            // Decay Diffusion 2 (allpass, coeff = 0.50)
            float dd2Delayed = bufRead(kTankBDD2, tankBDD2Len_);
            float dd2Coeff = kDecayDiffusion2;
            float dd2Out = -dd2Coeff * damped + dd2Delayed;
            bufWrite(kTankBDD2, damped + dd2Coeff * dd2Out);

            // Post-damping delay
            bufWrite(kTankBPostDamp, dd2Out);
            float postDamped = bufRead(kTankBPostDamp, tankBPostDampLen_);

            // DC blocker - bypass during freeze to preserve energy
            if (freeze_) {
                tankBOut_ = postDamped;
            } else {
                tankBOut_ = tankBDCBlocker_.process(postDamped);
            }
            // FR-005: flushDenormal() removed -- FTZ/DAZ assumed at process entry
        }

        // -- Output tap computation (FR-008, FR-009) --
        float yL = 0.0f;
        float yR = 0.0f;

        // Left output taps (Table 2)
        yL += kLeftTapSigns[0] * bufRead(kTankBPreDamp, leftTaps_[0]);
        yL += kLeftTapSigns[1] * bufRead(kTankBPreDamp, leftTaps_[1]);
        yL += kLeftTapSigns[2] * bufRead(kTankBDD2, leftTaps_[2]);
        yL += kLeftTapSigns[3] * bufRead(kTankBPostDamp, leftTaps_[3]);
        yL += kLeftTapSigns[4] * bufRead(kTankAPreDamp, leftTaps_[4]);
        yL += kLeftTapSigns[5] * bufRead(kTankADD2, leftTaps_[5]);
        yL += kLeftTapSigns[6] * bufRead(kTankAPostDamp, leftTaps_[6]);

        // Right output taps (Table 2)
        yR += kRightTapSigns[0] * bufRead(kTankAPreDamp, rightTaps_[0]);
        yR += kRightTapSigns[1] * bufRead(kTankAPreDamp, rightTaps_[1]);
        yR += kRightTapSigns[2] * bufRead(kTankADD2, rightTaps_[2]);
        yR += kRightTapSigns[3] * bufRead(kTankAPostDamp, rightTaps_[3]);
        yR += kRightTapSigns[4] * bufRead(kTankBPreDamp, rightTaps_[4]);
        yR += kRightTapSigns[5] * bufRead(kTankBDD2, rightTaps_[5]);
        yR += kRightTapSigns[6] * bufRead(kTankBPostDamp, rightTaps_[6]);

        // Apply output gain (R8)
        yL *= kOutputGain;
        yR *= kOutputGain;

        // -- Stereo width processing (FR-011a) --
        float mid = 0.5f * (yL + yR);
        float side = 0.5f * (yL - yR);
        float wetL = mid + width * side;
        float wetR = mid - width * side;

        // -- Dry/wet mix (FR-011) --
        // dryGain/wetGain are pre-computed equal-power gains from smoothers
        left = dryGain * dryL + wetGain * wetL;
        right = dryGain * dryR + wetGain * wetR;

        // -- Advance Gordon-Smith LFO (FR-001) --
        float newSin = sinState_ + lfoEpsilon_ * cosState_;
        float newCos = cosState_ - lfoEpsilon_ * newSin;
        sinState_ = newSin;
        cosState_ = newCos;
    }

    // =========================================================================
    // Input diffusion allpass helper
    // =========================================================================

    /// Process one sample through a Schroeder allpass using contiguous buffer section.
    /// Matches the topology from comb_filter.h SchroederAllpass:
    /// w[n] = x[n] + g * w[n-D], y[n] = -g * x[n] + w[n-D]
    /// Read-before-write with D-1 offset to match DelayLine semantics.
    /// Note: The D-1 offset causes an off-by-one for sub-1-sample delays, but
    /// Dattorro's minimum delay length is 107 samples at ref rate (~173 at 48kHz),
    /// so this limitation never applies in practice.
    [[nodiscard]] float processAllpass(Section s, float input, float coeff,
                                        float delaySamples) noexcept {
        // Read before write (like SchroederAllpass), using D-1 offset
        size_t readDelay = static_cast<size_t>(std::max(0.0f, delaySamples - 1.0f));
        float delayedW = bufRead(s, readDelay);

        // y[n] = -g * x[n] + w[n-D]
        float output = -coeff * input + delayedW;

        // w[n] = x[n] + g * y[n]
        float writeValue = input + coeff * output;

        bufWrite(s, writeValue);
        return output;
    }

    // =========================================================================
    // Configuration
    // =========================================================================
    double sampleRate_ = 0.0;
    bool prepared_ = false;
    bool freeze_ = false;

    // =========================================================================
    // Contiguous delay buffer (FR-004)
    // =========================================================================
    std::vector<float> contiguousBuffer_;
    size_t totalBufferSize_ = 0;
    size_t sectionSizes_[reverb_detail::kNumDelaySections] = {};
    size_t sectionMasks_[reverb_detail::kNumDelaySections] = {};
    size_t sectionOffsets_[reverb_detail::kNumDelaySections] = {};
    size_t writePos_[reverb_detail::kNumDelaySections] = {};

    // =========================================================================
    // Input section
    // =========================================================================
    OnePoleLP bandwidthFilter_;
    float inputDiffDelaySamples_[4] = {};
    float inputDiffCoeff1_ = 0.0f;
    float inputDiffCoeff2_ = 0.0f;
    float inputDiffState_[4] = {};  // unused now, kept for potential future use

    // =========================================================================
    // Damping & DC blocking
    // =========================================================================
    OnePoleLP tankADamping_;
    OnePoleLP tankBDamping_;
    DCBlocker tankADCBlocker_;
    DCBlocker tankBDCBlocker_;
    float cachedDampCutoff_ = 0.0f;  ///< Cached damping cutoff to avoid redundant setCutoff

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
    // Gordon-Smith LFO state (FR-001)
    // =========================================================================
    float sinState_ = 0.0f;
    float cosState_ = 1.0f;
    float lfoEpsilon_ = 0.0f;
    float maxExcursion_ = 0.0f;

    // =========================================================================
    // Parameter smoothers
    // =========================================================================
    OnePoleSmoother decaySmoother_;
    OnePoleSmoother dampingSmoother_;
    OnePoleSmoother dryGainSmoother_;   ///< Pre-computed cos(mix * pi/2)
    OnePoleSmoother wetGainSmoother_;   ///< Pre-computed sin(mix * pi/2)
    OnePoleSmoother widthSmoother_;
    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother preDelaySmoother_;
    OnePoleSmoother diffusion1Smoother_;
    OnePoleSmoother diffusion2Smoother_;
    OnePoleSmoother modDepthSmoother_;
};

} // namespace DSP
} // namespace Krate
