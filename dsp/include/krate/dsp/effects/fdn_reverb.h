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

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/effects/reverb.h>  // ReverbParams
#include <krate/dsp/primitives/delay_line.h>  // nextPowerOf2, DelayLine

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

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

/// Normalization factor for 8-point Hadamard: 1/sqrt(8)
static constexpr float kHadamardNorm = 0.35355339059327373f;  // 1/sqrt(8)

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

        // -- Diffuser delay lengths (FR-008) --
        // Use short prime delays scaled from 48kHz reference for each step
        // Steps use progressively longer delays for cascaded diffusion
        static constexpr size_t kDiffRefDelays[kNumDiffuserSteps] = {13, 19, 29, 37};

        totalDiffuserBufferSize_ = 0;
        for (size_t step = 0; step < kNumDiffuserSteps; ++step) {
            size_t scaled = static_cast<size_t>(
                std::round(static_cast<double>(kDiffRefDelays[step]) * sampleRate / kReferenceSampleRate));
            scaled = std::max(scaled, size_t(1));
            diffuserDelayLengths_[step] = scaled;

            for (size_t ch = 0; ch < kNumChannels; ++ch) {
                size_t idx = step * kNumChannels + ch;
                size_t secSize = nextPowerOf2(scaled + 4);
                diffuserSectionSizes_[idx] = secSize;
                diffuserSectionMasks_[idx] = secSize - 1;
                diffuserSectionOffsets_[idx] = totalDiffuserBufferSize_;
                totalDiffuserBufferSize_ += secSize;
            }
        }
        diffuserBuffer_.assign(totalDiffuserBufferSize_, 0.0f);

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
    void process(float& left, float& right) noexcept {
        if (!prepared_) return;

        // -- Step 1: NaN/Inf input guard (FR-019) --
        if (detail::isNaN(left) || detail::isInf(left)) left = 0.0f;
        if (detail::isNaN(right) || detail::isInf(right)) right = 0.0f;

        // Store dry
        const float dryL = left;
        const float dryR = right;

        // -- Step 2: Mono sum + pre-delay --
        float mono = (left + right) * 0.5f;
        if (freeze_) {
            mono = 0.0f;  // Block new input during freeze (FR-018)
        }

        preDelay_.write(mono);
        float preDelayed = preDelay_.readLinear(std::max(0.0f, preDelaySamples_));

        // -- Step 3: Read delay outputs (output taps) --
        alignas(32) float delReads[kNumChannels];
        for (size_t i = 0; i < kNumChannels; ++i) {
            // For modulated channels (4-7), apply LFO excursion
            float delayF = static_cast<float>(delayLengths_[i]);
            for (size_t j = 0; j < kNumModulatedChannels; ++j) {
                if (lfoModChannels_[j] == i) {
                    delayF += lfoSinState_[j] * lfoMaxExcursion_;
                    delayF = std::max(1.0f, delayF);
                    break;
                }
            }
            delReads[i] = delayBufReadLinear(i, delayF);
        }

        // -- Step 4: One-pole damping filters (FR-011) --
        // In freeze mode, bypass damping to preserve energy
        if (freeze_) {
            for (size_t i = 0; i < kNumChannels; ++i) {
                filterStates_[i] = delReads[i];
            }
        } else {
            for (size_t i = 0; i < kNumChannels; ++i) {
                filterStates_[i] = filterCoeffs_[i] * delReads[i]
                                 + (1.0f - filterCoeffs_[i]) * filterStates_[i];
            }
        }

        // -- Step 5: DC blockers (FR-012) --
        // In freeze mode, bypass DC blockers to preserve energy
        alignas(32) float processed[kNumChannels];
        if (freeze_) {
            for (size_t i = 0; i < kNumChannels; ++i) {
                processed[i] = filterStates_[i];
            }
        } else {
            for (size_t i = 0; i < kNumChannels; ++i) {
                float filtered = filterStates_[i];
                dcBlockY_[i] = filtered - dcBlockX_[i] + 0.9999f * dcBlockY_[i];
                dcBlockX_[i] = filtered;
                processed[i] = dcBlockY_[i];
            }
        }

        // -- Step 6: Hadamard diffuser in feedback path (FR-008) --
        // Apply 4 cascaded Hadamard steps to the processed delay outputs
        for (size_t step = 0; step < kNumDiffuserSteps; ++step) {
            applyDiffuserStep(processed, step);
        }

        // -- Step 7: Householder feedback matrix (FR-010) --
        applyHouseholder(processed);

        // -- Step 8: Apply feedback gains and add new input --
        for (size_t i = 0; i < kNumChannels; ++i) {
            processed[i] = processed[i] * feedbackGains_[i] + preDelayed;
        }

        // -- Step 9: Write to delay lines --
        for (size_t i = 0; i < kNumChannels; ++i) {
            delayBufWrite(i, processed[i]);
        }

        // -- Step 10: Advance LFO (Gordon-Smith phasor) --
        for (size_t j = 0; j < kNumModulatedChannels; ++j) {
            float newSin = lfoSinState_[j] + lfoEpsilon_ * lfoCosState_[j];
            float newCos = lfoCosState_[j] - lfoEpsilon_ * newSin;
            lfoSinState_[j] = newSin;
            lfoCosState_[j] = newCos;
        }

        // -- Step 11: Stereo output from delay reads --
        // Odd/even channel split for stereo
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (size_t i = 0; i < kNumChannels; i += 2) {
            wetL += delReads[i];
        }
        for (size_t i = 1; i < kNumChannels; i += 2) {
            wetR += delReads[i];
        }
        wetL *= 0.25f;  // 1/4 (sum of 4 channels)
        wetR *= 0.25f;

        // Width processing
        float mid = 0.5f * (wetL + wetR);
        float side = 0.5f * (wetL - wetR);
        wetL = mid + width_ * side;
        wetR = mid - width_ * side;

        // Dry/wet mix (equal-power)
        const float dryGain = std::cos(mix_ * kHalfPi);
        const float wetGain = std::sin(mix_ * kHalfPi);
        left = dryGain * dryL + wetGain * wetL;
        right = dryGain * dryR + wetGain * wetR;
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
            for (size_t i = 0; i < kNumChannels; ++i) {
                subBlockFilterCoeffs[i] = filterCoeffs_[i];
                subBlockFeedbackGains[i] = feedbackGains_[i];
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
                    for (size_t j = 0; j < kNumModulatedChannels; ++j) {
                        if (lfoModChannels_[j] == i) {
                            delayF += lfoSinState_[j] * subBlockLfoMaxExcursion;
                            delayF = std::max(1.0f, delayF);
                            break;
                        }
                    }
                    delReads[i] = delayBufReadLinear(i, delayF);
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
                }

                // -- Step 5: DC blockers (FR-012) --
                if (!subBlockFreeze) {
                    for (size_t i = 0; i < kNumChannels; ++i) {
                        dcBlockY_[i] = processed[i] - dcBlockX_[i]
                                     + 0.9999f * dcBlockY_[i];
                        dcBlockX_[i] = processed[i];
                        processed[i] = dcBlockY_[i];
                    }
                }

                // -- Step 6: Hadamard diffuser via SIMD (FR-008, FR-015b) --
                for (size_t step = 0; step < kNumDiffuserSteps; ++step) {
                    for (size_t ch = 0; ch < kNumChannels; ++ch) {
                        float delayed = diffuserBufRead(step, ch,
                                                        diffuserDelayLengths_[step]);
                        diffuserBufWrite(step, ch, processed[ch]);
                        processed[ch] = delayed;
                    }
                    fdnApplyHadamardSIMD(processed, kNumChannels);
                }

                // -- Step 7: Householder feedback via SIMD (FR-010, FR-015c) --
                fdnApplyHouseholderSIMD(processed, kNumChannels);

                // -- Step 8: Apply feedback gains and add new input --
                for (size_t i = 0; i < kNumChannels; ++i) {
                    processed[i] = processed[i] * subBlockFeedbackGains[i]
                                 + preDelayed;
                }

                // -- Step 9: Write to delay lines --
                for (size_t i = 0; i < kNumChannels; ++i) {
                    delayBufWrite(i, processed[i]);
                }

                // -- Step 10: Advance LFO --
                for (size_t j = 0; j < kNumModulatedChannels; ++j) {
                    float newSin = lfoSinState_[j]
                                 + subBlockLfoEpsilon * lfoCosState_[j];
                    float newCos = lfoCosState_[j]
                                 - subBlockLfoEpsilon * newSin;
                    lfoSinState_[j] = newSin;
                    lfoCosState_[j] = newCos;
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

    void setParamsInternal(const ReverbParams& params) noexcept {
        using namespace fdn_detail;

        float roomSize = std::clamp(params.roomSize, 0.0f, 1.0f);
        float damping = std::clamp(params.damping, 0.0f, 1.0f);

        // Map roomSize to feedback gain (FR-017) using quadratic curve.
        // Linear mapping caused audible echo ringing at 65-70% roomSize because
        // short FDN delays (3-17ms) amplify individual reflections at high gain.
        // Too-low base (0.60) made reverb inaudible below 60%.
        // Quadratic: fbGain = 0.78 + 0.17 * roomSize^2
        // roomSize=0.0 -> 0.78, roomSize=0.5 -> 0.82, roomSize=0.7 -> 0.86,
        // roomSize=0.9 -> 0.92, roomSize=1.0 -> 0.95
        float fbGain = 0.78f + 0.17f * roomSize * roomSize;

        // Freeze mode (FR-018)
        freeze_ = params.freeze;
        if (freeze_) {
            fbGain = 1.0f;
        }

        for (size_t i = 0; i < kNumChannels; ++i) {
            feedbackGains_[i] = fbGain;
        }

        // Map damping to one-pole coefficient
        // damping=0 -> 20kHz (bright), damping=1 -> 200Hz (dark)
        float dampCutoffHz = 200.0f * std::pow(100.0f, 1.0f - damping);
        float coeff = 1.0f - std::exp(-kTwoPi * dampCutoffHz / static_cast<float>(sampleRate_));
        coeff = std::clamp(coeff, 0.0f, 1.0f);
        for (size_t i = 0; i < kNumChannels; ++i) {
            filterCoeffs_[i] = coeff;
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
        lfoEpsilon_ = 2.0f * static_cast<float>(std::sin(
            kPi * static_cast<double>(modRate) / sampleRate_));
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
    // Hadamard FWHT (FR-008)
    // =========================================================================

    /// 3-stage butterfly FWHT for 8 channels + 1/sqrt(8) normalization
    static void applyHadamard(float x[kNumChannels]) noexcept {
        using namespace fdn_detail;

        // Stage 1: stride = 4
        for (size_t i = 0; i < 4; ++i) {
            float a = x[i];
            float b = x[i + 4];
            x[i] = a + b;
            x[i + 4] = a - b;
        }

        // Stage 2: stride = 2
        for (size_t k = 0; k < 8; k += 4) {
            for (size_t i = 0; i < 2; ++i) {
                float a = x[k + i];
                float b = x[k + i + 2];
                x[k + i] = a + b;
                x[k + i + 2] = a - b;
            }
        }

        // Stage 3: stride = 1
        for (size_t k = 0; k < 8; k += 2) {
            float a = x[k];
            float b = x[k + 1];
            x[k] = a + b;
            x[k + 1] = a - b;
        }

        // Normalize
        for (size_t i = 0; i < kNumChannels; ++i) {
            x[i] *= kHadamardNorm;
        }
    }

    /// Diffuser step: read from diffuser delay, apply Hadamard, write back (FR-008)
    void applyDiffuserStep(float x[kNumChannels], size_t stepIndex) noexcept {
        for (size_t ch = 0; ch < kNumChannels; ++ch) {
            float delayed = diffuserBufRead(stepIndex, ch, diffuserDelayLengths_[stepIndex]);
            diffuserBufWrite(stepIndex, ch, x[ch]);
            x[ch] = delayed;
        }

        applyHadamard(x);
    }

    // =========================================================================
    // Householder feedback matrix (FR-010)
    // =========================================================================

    /// y[i] = x[i] - (2/N) * sum(x) = x[i] - 0.25 * sum(x) for N=8
    /// Total: 8 adds + 1 mul + 8 subs = 17 arithmetic ops
    static void applyHouseholder(float x[kNumChannels]) noexcept {
        float sum = 0.0f;
        for (size_t i = 0; i < kNumChannels; ++i) {
            sum += x[i];
        }
        float scaled = sum * 0.25f;  // 2/8 = 0.25
        for (size_t i = 0; i < kNumChannels; ++i) {
            x[i] -= scaled;
        }
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

    // =========================================================================
    // SoA state arrays (FR-014) - alignas(32) for SIMD
    // =========================================================================
    alignas(32) float delayOutputs_[kNumChannels] = {};
    alignas(32) float filterStates_[kNumChannels] = {};
    alignas(32) float filterCoeffs_[kNumChannels] = {};
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
    size_t diffuserDelayLengths_[kNumDiffuserSteps] = {};
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
    float lfoEpsilon_ = 0.0f;
    size_t lfoModChannels_[kNumModulatedChannels] = {};
    float lfoMaxExcursion_ = 0.0f;
};

} // namespace DSP
} // namespace Krate
