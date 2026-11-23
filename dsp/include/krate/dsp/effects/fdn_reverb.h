// ==============================================================================
// Layer 4: User Feature - FDN Reverb (8-Channel Feedback Delay Network)
// ==============================================================================
// Implements an 8-channel Feedback Delay Network reverb with:
// - Feedforward Hadamard diffuser (4 cascaded FWHT steps) (FR-008)
// - Householder feedback matrix O(N) for N=8 (FR-010)
// - One-pole damping filters per channel (FR-011)
// - DC blockers per channel (FR-012)
// - Gordon-Smith quadrature LFO on 4 longest channels (FR-013)
// - SoA layout with alignas(32) for SIMD vectorization (FR-014)
// - Google Highway SIMD acceleration (FR-015)
// - NaN/Inf input guarding (FR-019)
// - Multi-sample-rate support 8kHz-192kHz (FR-020)
//
// Signal flow:
//   Input -> Mono sum -> Pre-delay -> inject into channels
//         -> Read from 8 delay lines (output taps here)
//         -> One-pole damping -> DC blockers -> Hadamard diffuser
//         -> Householder feedback -> apply gains -> add input -> write to delays
//         -> Stereo output from delay reads with width control
//
// Composes:
// - DelayLine (Layer 1): Pre-delay
// - ReverbParams (Layer 4): Shared parameter interface
//
// Feature: 125-dual-reverb
// Layer: 4 (Effects)
// Reference: specs/125-dual-reverb/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IV: SIMD viability verified, scalar-first
// - Principle IX: Layer 4 (composes Layer 0-1)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)  // structure padded due to alignas (intentional for SIMD)
#endif

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/interpolation.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/effects/reverb.h>  // ReverbParams
#include <krate/dsp/primitives/delay_line.h>  // nextPowerOf2, DelayLine

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>


namespace Krate::DSP {

// =============================================================================
// Forward declarations for SIMD kernels (defined in fdn_reverb_simd.cpp)
// =============================================================================

/// SIMD-accelerated one-pole filter bank (8 channels)
void fdnApplyFilterBankSIMD(const float* inputs, float* states,
                            const float* coeffs, float* outputs,
                            size_t numChannels) noexcept;

/// SIMD-accelerated Hadamard (FWHT) butterfly
void fdnApplyHadamardSIMD(float* data, size_t numChannels) noexcept;

/// SIMD-accelerated Householder feedback matrix
void fdnApplyHouseholderSIMD(float* data, size_t numChannels) noexcept;

/// SIMD-accelerated feedback gain + input injection: data[i] = data[i] * gains[i] + input
void fdnApplyFeedbackSIMD(float* data, const float* gains,
                           float input, size_t numChannels) noexcept;

// =============================================================================
// FDN Reverb Constants
// =============================================================================

namespace fdn_detail {

/// Reference sample rate for delay length definitions
static constexpr double kReferenceSampleRate = 48000.0;

/// Reference delay lengths at 48kHz (FR-009)
/// Exponential spacing: base=149, r~1.27, adjusted for coprimality (all primes)
/// Original: [149, 189, 240, 305, 387, 492, 625, 794]
/// Adjusted: [149, 193, 241, 307, 389, 491, 631, 797] (all prime, GCD=1 for all pairs)
static constexpr size_t kRefDelays[8] = {149, 193, 241, 307, 389, 491, 631, 797};

/// Parameter smoothing time in milliseconds
static constexpr float kSmoothingTimeMs = 10.0f;

} // namespace fdn_detail

// =============================================================================
// FDNReverb Class (FR-007)
// =============================================================================

/// @brief 8-channel Feedback Delay Network reverb (Layer 4).
///
/// Architecture:
///   Input -> Mono sum -> Pre-delay -> Hadamard diffuser (4 steps)
///         -> Feedback loop (Householder matrix + 8 delay lines
///            + one-pole damping + DC blockers + 4-channel LFO modulation)
///         -> Stereo output with width control
class FDNReverb {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumChannels = 8;
    static constexpr size_t kNumModulatedChannels = 4;
    static constexpr size_t kNumDiffuserSteps = 4;
    static constexpr size_t kSubBlockSize = 16;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    FDNReverb() noexcept = default;

    /// @brief Prepare for processing. Allocates all buffers. (FR-020)
    /// @note Must be called from a non-real-time thread (allocates memory).
    /// @param sampleRate Sample rate in Hz [8000, 192000]
    void prepare(double sampleRate) noexcept {
        using namespace fdn_detail;

        sampleRate_ = sampleRate;

        // -- Scale delay lengths from 48kHz reference (FR-009) --
        for (size_t i = 0; i < kNumChannels; ++i) {
            size_t scaled = static_cast<size_t>(
                std::round(static_cast<double>(kRefDelays[i]) * sampleRate / kReferenceSampleRate));
            // Enforce 3ms min / 20ms max per FR-009 rule 3
            size_t minDelay = static_cast<size_t>(0.003 * sampleRate);
            size_t maxDelay = static_cast<size_t>(0.020 * sampleRate);
            delayLengths_[i] = std::clamp(scaled, minDelay, maxDelay);
        }

        // -- Allocate contiguous delay buffer with power-of-2 sections --
        totalDelayBufferSize_ = 0;
        for (size_t i = 0; i < kNumChannels; ++i) {
            // Add margin for LFO modulation on longer delays + safety
            size_t maxLen = delayLengths_[i] + 64;
            delaySectionSizes_[i] = nextPowerOf2(maxLen);
            delaySectionMasks_[i] = delaySectionSizes_[i] - 1;
            delaySectionOffsets_[i] = totalDelayBufferSize_;
            totalDelayBufferSize_ += delaySectionSizes_[i];
        }
        delayBuffer_.assign(totalDelayBufferSize_, 0.0f);

        // -- Per-channel diffuser delay lengths (FR-008) --
        // Each channel within a step has a distinct prime delay for improved
        // temporal diffusion and decorrelation.
        static constexpr size_t kDiffRefDelays[kNumDiffuserSteps][kNumChannels] = {
            {13, 17, 19, 23, 29, 31, 37, 41},       // Step 0: ~0.27-0.85ms
            {43, 47, 53, 59, 61, 67, 71, 73},       // Step 1: ~0.90-1.52ms
            {79, 83, 89, 97, 101, 103, 107, 109},   // Step 2: ~1.65-2.27ms
            {113, 127, 131, 137, 139, 149, 151, 157} // Step 3: ~2.35-3.27ms
        };

        totalDiffuserBufferSize_ = 0;
        for (size_t step = 0; step < kNumDiffuserSteps; ++step) {
            for (size_t ch = 0; ch < kNumChannels; ++ch) {
                size_t idx = step * kNumChannels + ch;
                size_t scaled = static_cast<size_t>(
                    std::round(static_cast<double>(kDiffRefDelays[step][ch])
                               * sampleRate / kReferenceSampleRate));
                scaled = std::max(scaled, size_t(1));
                diffuserDelayLengths_[idx] = scaled;

                size_t secSize = nextPowerOf2(scaled + 4);
                diffuserSectionSizes_[idx] = secSize;
                diffuserSectionMasks_[idx] = secSize - 1;
                diffuserSectionOffsets_[idx] = totalDiffuserBufferSize_;
                totalDiffuserBufferSize_ += secSize;
            }
        }
        diffuserBuffer_.assign(totalDiffuserBufferSize_, 0.0f);

        // Mean transit of one pass through the four in-loop diffuser steps (each
        // read is length + 1 samples behind its write). The Hadamard steps mix
        // the channels, so every pass through a line also pays, on average, this
        // much diffuser delay; the Jot gains below count it (R-220).
        meanDiffuserTransit_ = 0.0f;
        for (const size_t length : diffuserDelayLengths_) {
            meanDiffuserTransit_ += static_cast<float>(length + 1);
        }
        meanDiffuserTransit_ /= static_cast<float>(kNumChannels);

        // -- Pre-delay (up to 100ms) --
        preDelay_.prepare(sampleRate, 0.1f);

        // -- LFO initialization (FR-013): 4 channels, quadrature phase offsets --
        // Modulate the 4 longest delays (indices 4,5,6,7 after ascending sort)
        lfoModChannels_[0] = 4;
        lfoModChannels_[1] = 5;
        lfoModChannels_[2] = 6;
        lfoModChannels_[3] = 7;

        for (size_t j = 0; j < kNumModulatedChannels; ++j) {
            float phase = static_cast<float>(j) * kHalfPi;
            lfoSinState_[j] = std::sin(phase);
            lfoCosState_[j] = std::cos(phase);
        }
        lfoEpsilon_ = 0.0f;
        lfoMaxExcursion_ = 0.0f;

        // -- DC blocker coefficient: ~40Hz -3dB point at any sample rate --
        dcBlockR_ = 1.0f - (250.0f / static_cast<float>(sampleRate));

        // -- Initialize parameters to defaults --
        ReverbParams defaultParams;
        setParamsInternal(defaultParams);

        prepared_ = true;
        reset();
    }

    /// @brief Reset all internal state to silence. (FR-021)
    void reset() noexcept {
        // Zero-fill delay buffers
        std::fill(delayBuffer_.begin(), delayBuffer_.end(), 0.0f);
        std::fill(diffuserBuffer_.begin(), diffuserBuffer_.end(), 0.0f);

        // Reset write positions
        for (auto& wp : delayWritePos_) wp = 0;
        for (auto& wp : diffuserWritePos_) wp = 0;

        // Reset SoA state arrays
        for (size_t i = 0; i < kNumChannels; ++i) {
            delayOutputs_[i] = 0.0f;
            filterStates_[i] = 0.0f;
            dcBlockX_[i] = 0.0f;
            dcBlockY_[i] = 0.0f;
        }

        // Reset pre-delay
        preDelay_.reset();

        // Re-init LFO phasors to initial phase offsets
        for (size_t j = 0; j < kNumModulatedChannels; ++j) {
            float phase = static_cast<float>(j) * kHalfPi;
            lfoSinState_[j] = std::sin(phase);
            lfoCosState_[j] = std::cos(phase);
        }
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Update all reverb parameters. (FR-017)
    void setParams(const ReverbParams& params) noexcept {
        setParamsInternal(params);
    }

    // =========================================================================
    // Processing (real-time safe) (FR-021)
    // =========================================================================

    /// @brief Process a single stereo sample pair in-place.
    /// Runs the block path for one sample, so both entry points scale, filter
    /// and mix exactly the same way (R-220: this copy once scaled the damping
    /// filter's state by the Jot gain while processBlock scaled its output).
    void process(float& left, float& right) noexcept {
        processBlock(&left, &right, 1);
    }

    /// @brief Process a block of stereo samples in-place. (FR-016 SIMD path)
    /// Uses 16-sample sub-blocks: block-rate parameters (LFO epsilon, filter
    /// coefficients, dry/wet gains) are updated once per sub-block, then held
    /// constant for the inner 16-sample loop. SIMD kernels operate on the
    /// 8-channel dimension within each sample step.
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        if (!prepared_) return;

        size_t offset = 0;
        while (offset < numSamples) {
            const size_t blockLen = std::min(static_cast<size_t>(kSubBlockSize),
                                              numSamples - offset);

            // -- Block-rate parameter update (once per 16-sample sub-block) --
            // Snapshot LFO epsilon and max excursion for this sub-block
            const float subBlockLfoEpsilon = lfoEpsilon_;
            const float subBlockLfoMaxExcursion = lfoMaxExcursion_;
            const float subBlockPreDelaySamples = preDelaySamples_;
            const bool subBlockFreeze = freeze_;

            // Snapshot filter coefficients for this sub-block
            alignas(32) float subBlockFilterCoeffs[kNumChannels];
            alignas(32) float subBlockFeedbackGains[kNumChannels];
            alignas(32) float subBlockFilterGainDC[kNumChannels];
            for (size_t i = 0; i < kNumChannels; ++i) {
                subBlockFilterCoeffs[i] = filterCoeffs_[i];
                subBlockFeedbackGains[i] = feedbackGains_[i];
                subBlockFilterGainDC[i] = filterGainDC_[i];
            }

            // Compute dry/wet gains once per sub-block (avoids per-sample trig)
            const float subBlockDryGain = std::cos(mix_ * kHalfPi);
            const float subBlockWetGain = std::sin(mix_ * kHalfPi);
            const float subBlockWidth = width_;

            // -- Inner loop: process blockLen samples with held values --
            for (size_t s = 0; s < blockLen; ++s) {
                float& l = left[offset + s];
                float& r = right[offset + s];

                // -- Step 1: NaN/Inf input guard (FR-019) --
                if (detail::isNaN(l) || detail::isInf(l)) l = 0.0f;
                if (detail::isNaN(r) || detail::isInf(r)) r = 0.0f;

                const float dryL = l;
                const float dryR = r;

                // -- Step 2: Mono sum + pre-delay --
                float mono = (l + r) * 0.5f;
                if (subBlockFreeze) mono = 0.0f;

                preDelay_.write(mono);
                float preDelayed = preDelay_.readLinear(
                    std::max(0.0f, subBlockPreDelaySamples));

                // -- Step 3: Read delay outputs (output taps) --
                alignas(32) float delReads[kNumChannels];
                for (size_t i = 0; i < kNumChannels; ++i) {
                    float delayF = static_cast<float>(delayLengths_[i]);
                    delayF += lfoExcursionPerChannel_[i];
                    delayF = std::max(1.0f, delayF);
                    if (lfoExcursionPerChannel_[i] != 0.0f) {
                        delReads[i] = delayBufReadCubic(i, delayF);
                    } else {
                        delReads[i] = delayBufReadLinear(i, delayF);
                    }
                }

                // -- Step 4: One-pole damping filters via SIMD (FR-011, FR-015a) --
                alignas(32) float processed[kNumChannels];
                if (subBlockFreeze) {
                    for (size_t i = 0; i < kNumChannels; ++i) {
                        filterStates_[i] = delReads[i];
                        processed[i] = delReads[i];
                    }
                } else {
                    fdnApplyFilterBankSIMD(delReads, filterStates_,
                                           subBlockFilterCoeffs,
                                           processed, kNumChannels);
                    // Apply per-channel Jot DC gain after filter bank
                    for (size_t i = 0; i < kNumChannels; ++i) {
                        processed[i] *= subBlockFilterGainDC[i];
                    }
                }

                // -- Step 5: DC blockers (FR-012) --
                if (!subBlockFreeze) {
                    for (size_t i = 0; i < kNumChannels; ++i) {
                        dcBlockY_[i] = processed[i] - dcBlockX_[i]
                                     + dcBlockR_ * dcBlockY_[i];
                        dcBlockX_[i] = processed[i];
                        processed[i] = dcBlockY_[i];
                    }
                }

                // -- Step 6: Hadamard diffuser via SIMD (FR-008, FR-015b) --
                for (size_t step = 0; step < kNumDiffuserSteps; ++step) {
                    for (size_t ch = 0; ch < kNumChannels; ++ch) {
                        size_t idx = step * kNumChannels + ch;
                        float delayed = diffuserBufRead(step, ch,
                                                        diffuserDelayLengths_[idx]);
                        diffuserBufWrite(step, ch, processed[ch]);
                        processed[ch] = delayed;
                    }
                    fdnApplyHadamardSIMD(processed, kNumChannels);
                }

                // -- Step 7: Householder feedback via SIMD (FR-010, FR-015c) --
                fdnApplyHouseholderSIMD(processed, kNumChannels);

                // -- Step 8: Apply feedback gains and add new input (SIMD) --
                fdnApplyFeedbackSIMD(processed, subBlockFeedbackGains,
                                      preDelayed, kNumChannels);

                // -- Step 9: Write to delay lines --
                for (size_t i = 0; i < kNumChannels; ++i) {
                    delayBufWrite(i, processed[i]);
                }

                // -- Step 10: Advance LFO + pre-compute excursions --
                for (size_t j = 0; j < kNumModulatedChannels; ++j) {
                    float newSin = lfoSinState_[j]
                                 + subBlockLfoEpsilon * lfoCosState_[j];
                    float newCos = lfoCosState_[j]
                                 - subBlockLfoEpsilon * newSin;
                    lfoSinState_[j] = newSin;
                    lfoCosState_[j] = newCos;
                    lfoExcursionPerChannel_[lfoModChannels_[j]] =
                        newSin * subBlockLfoMaxExcursion;
                }

                // -- Step 11: Stereo output from delay reads --
                float wetL = 0.0f;
                float wetR = 0.0f;
                for (size_t i = 0; i < kNumChannels; i += 2) {
                    wetL += delReads[i];
                }
                for (size_t i = 1; i < kNumChannels; i += 2) {
                    wetR += delReads[i];
                }
                wetL *= 0.25f;
                wetR *= 0.25f;

                float mid = 0.5f * (wetL + wetR);
                float side = 0.5f * (wetL - wetR);
                wetL = mid + subBlockWidth * side;
                wetR = mid - subBlockWidth * side;

                l = subBlockDryGain * dryL + subBlockWetGain * wetL;
                r = subBlockDryGain * dryR + subBlockWetGain * wetR;
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

private:
    // =========================================================================
    // Internal parameter update
    // =========================================================================

    void setParamsInternal(const ReverbParams& requested) noexcept {
        using namespace fdn_detail;

        // A non-finite field keeps its last applied value: a NaN size or damping
        // would write NaN into every feedback gain and filter coefficient, and the
        // tank would recirculate it after the parameter is finite again.
        ReverbParams params = requested;
        const auto keepFinite = [](float& v, float last) noexcept {
            if (!detail::isFinite(v)) v = last;
        };
        keepFinite(params.roomSize, appliedParams_.roomSize);
        keepFinite(params.damping, appliedParams_.damping);
        keepFinite(params.width, appliedParams_.width);
        keepFinite(params.mix, appliedParams_.mix);
        keepFinite(params.preDelayMs, appliedParams_.preDelayMs);
        keepFinite(params.diffusion, appliedParams_.diffusion);
        keepFinite(params.modRate, appliedParams_.modRate);
        keepFinite(params.modDepth, appliedParams_.modDepth);
        appliedParams_ = params;

        float roomSize = std::clamp(params.roomSize, 0.0f, 1.0f);
        float damping = std::clamp(params.damping, 0.0f, 1.0f);

        // Freeze mode (FR-018): the loop bypasses the damping filters, the Jot
        // gains and the DC blockers, so the orthonormal matrices hold the energy.
        freeze_ = params.freeze;

        // The per-line Jot gain below is the whole loop gain (R-220). It used to
        // be divided by a size-derived base gain 0.78 + 0.17 * size^2 applied on
        // the write side and clamped to 1, and since every gDC exceeded that
        // base, the clamp always won: the loop gain was the base alone and the
        // decay ran 0.44-2.2 s whatever the T60 below asked for.
        for (float& feedbackGain : feedbackGains_) {
            feedbackGain = 1.0f;
        }

        // Jot's per-delay-line absorption: each line's gain is 10^(-3 L / (T60 sr))
        // for the samples L one pass through it takes (the line, plus the mean
        // diffuser transit), so every path decays at the same T60.
        //
        // T60_dc derived from roomSize: 0.5s (small) to 10s (large)
        float t60dc = 0.5f + roomSize * 9.5f;
        // T60_nyq derived from damping: shorter than T60_dc for HF absorption
        // damping=0 -> T60_nyq = T60_dc (bright), damping=1 -> T60_nyq = T60_dc/20 (dark)
        float t60nyq = t60dc * std::pow(0.05f, damping);

        // The in-loop DC blocker y = x - x1 + R y1 has a passband gain of
        // 2 / (1 + R) (+0.023 dB per pass, +1.5 dB/s), which lengthened a 10 s
        // tail to ~13 s; the Jot gain carries (1 + R) / 2 to cancel it.
        const float dcBlockPassbandNorm = 0.5f * (1.0f + dcBlockR_);

        float sr = static_cast<float>(sampleRate_);
        for (size_t i = 0; i < kNumChannels; ++i) {
            const float loopSamples =
                static_cast<float>(delayLengths_[i] + 1) + meanDiffuserTransit_;
            // Per-channel ideal gain at DC and Nyquist (Jot formula)
            float gDC = std::pow(10.0f, -3.0f * loopSamples / (t60dc * sr));
            float gNyq = std::pow(10.0f, -3.0f * loopSamples / (t60nyq * sr));
            filterGainDC_[i] = std::clamp(gDC * dcBlockPassbandNorm, 0.0f, 1.0f);
            // Derive one-pole coefficient from DC/Nyquist gain ratio
            // At DC: H(1) = 1.0 (pass-through), gain applied via filterGainDC_
            // At Nyquist: H(-1) = coeff / (2 - coeff)
            // We want gNyq/gDC = coeff / (2 - coeff)
            float ratio = (gDC > 1e-10f) ? (gNyq / gDC) : 1.0f;
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            float coeff = 2.0f * ratio / (1.0f + ratio);
            filterCoeffs_[i] = std::clamp(coeff, 0.001f, 1.0f);
        }

        // Store parameters
        mix_ = std::clamp(params.mix, 0.0f, 1.0f);
        width_ = std::clamp(params.width, 0.0f, 1.0f);

        // Pre-delay
        float preDelayMs = std::clamp(params.preDelayMs, 0.0f, 100.0f);
        preDelaySamples_ = preDelayMs * 0.001f * static_cast<float>(sampleRate_);

        // LFO rate (FR-013: Gordon-Smith epsilon)
        float modRate = std::clamp(params.modRate, 0.0f, 2.0f);
        float modDepth = std::clamp(params.modDepth, 0.0f, 1.0f);
        float newEpsilon = 2.0f * static_cast<float>(std::sin(
            kPi * static_cast<double>(modRate) / sampleRate_));
        if (newEpsilon != lfoEpsilon_) {
            // Renormalize phasor states to unit circle when epsilon changes
            // to prevent amplitude drift (Gordon-Smith phasor is only
            // amplitude-stable when epsilon is constant)
            for (size_t j = 0; j < kNumModulatedChannels; ++j) {
                float r2 = lfoSinState_[j] * lfoSinState_[j]
                          + lfoCosState_[j] * lfoCosState_[j];
                if (r2 > 0.0f) {
                    float invR = 1.0f / std::sqrt(r2);
                    lfoSinState_[j] *= invR;
                    lfoCosState_[j] *= invR;
                }
            }
            lfoEpsilon_ = newEpsilon;
        }
        // Max excursion: 5% of longest delay
        lfoMaxExcursion_ = modDepth * (static_cast<float>(delayLengths_[kNumChannels - 1]) * 0.05f);
    }

    // =========================================================================
    // Delay buffer helpers (contiguous buffer with power-of-2 sections)
    // =========================================================================

    void delayBufWrite(size_t channel, float sample) noexcept {
        size_t offset = delaySectionOffsets_[channel];
        delayBuffer_[offset + delayWritePos_[channel]] = sample;
        delayWritePos_[channel] = (delayWritePos_[channel] + 1) & delaySectionMasks_[channel];
    }

    [[nodiscard]] float delayBufRead(size_t channel, size_t delaySamples) const noexcept {
        size_t offset = delaySectionOffsets_[channel];
        size_t readPos = (delayWritePos_[channel] - 1 - delaySamples) & delaySectionMasks_[channel];
        return delayBuffer_[offset + readPos];
    }

    [[nodiscard]] float delayBufReadLinear(size_t channel, float delaySamples) const noexcept {
        float floored = std::floor(delaySamples);
        size_t delayInt = static_cast<size_t>(floored);
        float frac = delaySamples - floored;
        float a = delayBufRead(channel, delayInt);
        float b = delayBufRead(channel, delayInt + 1);
        return a + frac * (b - a);
    }

    /// Cubic Hermite (Catmull-Rom) interpolated read for modulated delay lines.
    /// Reduces HF loss compared to linear interpolation in recirculating paths.
    [[nodiscard]] float delayBufReadCubic(size_t channel, float delaySamples) const noexcept {
        float floored = std::floor(delaySamples);
        size_t delayInt = static_cast<size_t>(floored);
        float frac = delaySamples - floored;
        float ym1 = delayBufRead(channel, delayInt > 0 ? delayInt - 1 : 0);
        float y0  = delayBufRead(channel, delayInt);
        float y1  = delayBufRead(channel, delayInt + 1);
        float y2  = delayBufRead(channel, delayInt + 2);
        return Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, frac);
    }

    // =========================================================================
    // Diffuser buffer helpers
    // =========================================================================

    void diffuserBufWrite(size_t step, size_t channel, float sample) noexcept {
        size_t idx = step * kNumChannels + channel;
        size_t offset = diffuserSectionOffsets_[idx];
        diffuserBuffer_[offset + diffuserWritePos_[idx]] = sample;
        diffuserWritePos_[idx] = (diffuserWritePos_[idx] + 1) & diffuserSectionMasks_[idx];
    }

    [[nodiscard]] float diffuserBufRead(size_t step, size_t channel,
                                         size_t delaySamples) const noexcept {
        size_t idx = step * kNumChannels + channel;
        size_t offset = diffuserSectionOffsets_[idx];
        size_t readPos = (diffuserWritePos_[idx] - 1 - delaySamples) & diffuserSectionMasks_[idx];
        return diffuserBuffer_[offset + readPos];
    }

    // =========================================================================
    // Configuration
    // =========================================================================
    double sampleRate_ = 0.0;
    bool prepared_ = false;
    bool freeze_ = false;
    float mix_ = 0.3f;
    float width_ = 1.0f;
    float preDelaySamples_ = 0.0f;
    float dcBlockR_ = 0.9999f;  // DC blocker coefficient, computed in prepare()
    float meanDiffuserTransit_ = 0.0f;  // samples per pass through the 4 diffuser steps
    ReverbParams appliedParams_{};  // last applied (finite) parameters

    // =========================================================================
    // SoA state arrays (FR-014) - alignas(32) for SIMD
    // =========================================================================
    alignas(32) float delayOutputs_[kNumChannels] = {};
    alignas(32) float filterStates_[kNumChannels] = {};
    alignas(32) float filterCoeffs_[kNumChannels] = {};
    alignas(32) float filterGainDC_[kNumChannels] = {};  // Per-channel DC gain (Jot absorption)
    alignas(32) float dcBlockX_[kNumChannels] = {};
    alignas(32) float dcBlockY_[kNumChannels] = {};
    alignas(32) float feedbackGains_[kNumChannels] = {};

    // =========================================================================
    // Main delay buffer (contiguous, FR-009)
    // =========================================================================
    std::vector<float> delayBuffer_;
    size_t totalDelayBufferSize_ = 0;
    size_t delayLengths_[kNumChannels] = {};
    size_t delaySectionSizes_[kNumChannels] = {};
    size_t delaySectionMasks_[kNumChannels] = {};
    size_t delaySectionOffsets_[kNumChannels] = {};
    size_t delayWritePos_[kNumChannels] = {};

    // =========================================================================
    // Diffuser delay buffers (4 steps x 8 channels)
    // =========================================================================
    std::vector<float> diffuserBuffer_;
    size_t totalDiffuserBufferSize_ = 0;
    size_t diffuserDelayLengths_[kNumDiffuserSteps * kNumChannels] = {};
    size_t diffuserSectionSizes_[kNumDiffuserSteps * kNumChannels] = {};
    size_t diffuserSectionMasks_[kNumDiffuserSteps * kNumChannels] = {};
    size_t diffuserSectionOffsets_[kNumDiffuserSteps * kNumChannels] = {};
    size_t diffuserWritePos_[kNumDiffuserSteps * kNumChannels] = {};

    // =========================================================================
    // Pre-delay
    // =========================================================================
    DelayLine preDelay_;

    // =========================================================================
    // Gordon-Smith LFO state (FR-013)
    // =========================================================================
    alignas(32) float lfoSinState_[kNumModulatedChannels] = {};
    alignas(32) float lfoCosState_[kNumModulatedChannels] = {};
    alignas(32) float lfoExcursionPerChannel_[kNumChannels] = {};  // Pre-computed per-channel LFO
    float lfoEpsilon_ = 0.0f;
    size_t lfoModChannels_[kNumModulatedChannels] = {};
    float lfoMaxExcursion_ = 0.0f;
};

} // namespace Krate::DSP


#ifdef _MSC_VER
#pragma warning(pop)
#endif
