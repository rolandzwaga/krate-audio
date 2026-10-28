// ==============================================================================
// Layer 2: DSP Processor - Grain Processor
// ==============================================================================
// Processes individual grains with pitch shifting, panning, and envelopes.
// Part of Granular Delay feature (spec 034)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends on Layer 0-1)
// ==============================================================================
#pragma once

#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/grain_pool.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace Krate::DSP {

/// Parameters for initializing a grain
struct GrainParams {
    float grainSizeMs = 100.0f;    ///< Grain duration in milliseconds
    float pitchSemitones = 0.0f;   ///< Pitch shift in semitones (-24 to +24)
    float positionSamples = 0.0f;  ///< Read position in delay buffer (samples)
    float pan = 0.0f;              ///< Pan position (-1 = L, 0 = center, +1 = R)
    bool reverse = false;          ///< Play grain backwards
    GrainEnvelopeType envelopeType = GrainEnvelopeType::Hann;
    bool frozenBuffer = false;     ///< The buffer's write head is stopped (freeze)
};

/// Processes individual grains with envelope, pitch shifting, and panning.
/// Handles grain initialization and sample-by-sample processing.
class GrainProcessor {
public:
    static constexpr size_t kDefaultEnvelopeSize = 2048;

    /// Prepare processor for use
    /// @param sampleRate Current sample rate
    /// @param maxEnvelopeSize Maximum envelope lookup table size
    void prepare(double sampleRate, size_t maxEnvelopeSize = kDefaultEnvelopeSize) noexcept {
        sampleRate_ = sampleRate;
        envelopeTableSize_ = maxEnvelopeSize;

        // Pre-allocate envelope table
        envelopeTable_.resize(maxEnvelopeSize);

        // Generate default Hann envelope
        regenerateEnvelope(GrainEnvelopeType::Hann);
    }

    /// Reset processor state
    void reset() noexcept {
        // Nothing to reset - stateless per-grain processing
    }

    /// Set envelope type (regenerates lookup table)
    /// @param type New envelope type
    void setEnvelopeType(GrainEnvelopeType type) noexcept {
        if (type != currentEnvelopeType_) {
            regenerateEnvelope(type);
        }
    }

    /// Get current envelope type
    [[nodiscard]] GrainEnvelopeType getEnvelopeType() const noexcept {
        return currentEnvelopeType_;
    }

    /// Initialize a grain with given parameters
    /// @param grain Grain state to initialize
    /// @param params Grain parameters
    void initializeGrain(Grain& grain, const GrainParams& params) noexcept {
        // Calculate grain duration in samples
        const float grainSizeSamples =
            params.grainSizeMs * static_cast<float>(sampleRate_) / 1000.0f;

        // Calculate envelope increment (phase advance per sample)
        grain.envelopePhase = 0.0f;
        grain.envelopeIncrement =
            (grainSizeSamples > 0.0f) ? (1.0f / grainSizeSamples) : 1.0f;

        // Calculate playback rate from pitch
        grain.playbackRate = semitonesToRatio(params.pitchSemitones);

        // Set initial read position (a delay back from the write head)
        grain.readPosition = params.positionSamples;

        if (params.reverse) {
            // Reverse reads further into the past as it plays, so it can start
            // at the requested delay.
            grain.playbackRate = -grain.playbackRate;  // Negative rate for reverse
        } else {
            // A forward grain closes in on the write head by (rate - write
            // advance) per sample; start far enough back to never reach it.
            const float writeAdvance = params.frozenBuffer ? 0.0f : 1.0f;
            grain.readPosition = std::max(grain.readPosition,
                                          grainSizeSamples * (grain.playbackRate - writeAdvance));
        }
        grain.reverse = params.reverse;

        // The grain reads both channels of a stereo buffer, so pan is a balance:
        // centre passes both channels at unity, and moving to one side turns the
        // other down along the equal-power curve (scaled so centre is 1).
        // pan: -1 = full left, 0 = center, +1 = full right
        const float panNorm = (params.pan + 1.0f) * 0.5f;  // 0 to 1
        grain.panL = std::min(1.0f, kSqrt2 * std::cos(panNorm * kHalfPi));
        grain.panR = std::min(1.0f, kSqrt2 * std::sin(panNorm * kHalfPi));

        grain.amplitude = 1.0f;
        grain.active = true;
    }

    /// Process one sample for a grain
    /// @param grain Grain state to process
    /// @param delayBufferL Left channel delay buffer
    /// @param delayBufferR Right channel delay buffer
    /// @param writeAdvance Samples the buffers' write head moved since the
    ///        previous call (1 while recording, 0 while frozen)
    /// @return Pair of {left, right} output samples
    [[nodiscard]] std::pair<float, float> processGrain(
        Grain& grain,
        const DelayLine& delayBufferL,
        const DelayLine& delayBufferR,
        float writeAdvance = 1.0f) noexcept {
        if (!grain.active) {
            return {0.0f, 0.0f};
        }

        // Get envelope value
        const float envelope =
            GrainEnvelope::lookup(envelopeTable_.data(), envelopeTableSize_,
                                  grain.envelopePhase);

        // Read from delay buffers with interpolation
        // Convert read position to delay time (samples from write head)
        const float delaySamples = std::max(0.0f, grain.readPosition);
        const float sampleL = delayBufferL.readLinear(delaySamples);
        const float sampleR = delayBufferR.readLinear(delaySamples);

        // Apply envelope and amplitude
        const float gainedL = sampleL * envelope * grain.amplitude;
        const float gainedR = sampleR * envelope * grain.amplitude;

        // Apply panning
        const float outputL = gainedL * grain.panL;
        const float outputR = gainedR * grain.panR;

        // Advance grain state. readPosition is a delay, so the write head moving
        // forward adds to it and playback (forward: rate > 0) takes it away.
        grain.envelopePhase += grain.envelopeIncrement;
        grain.readPosition += writeAdvance - grain.playbackRate;

        return {outputL, outputR};
    }

    /// Envelope value the grain's next processGrain() call applies
    [[nodiscard]] float envelopeValue(const Grain& grain) const noexcept {
        return GrainEnvelope::lookup(envelopeTable_.data(), envelopeTableSize_,
                                     grain.envelopePhase);
    }

    /// Check if grain has completed playback
    /// @param grain Grain to check
    /// @return true if grain envelope has completed
    [[nodiscard]] bool isGrainComplete(const Grain& grain) const noexcept {
        return grain.envelopePhase >= 1.0f;
    }

private:
    static constexpr float kHalfPi = 1.5707963267948966f;
    static constexpr float kSqrt2 = 1.4142135623730951f;

    void regenerateEnvelope(GrainEnvelopeType type) noexcept {
        GrainEnvelope::generate(envelopeTable_.data(), envelopeTableSize_, type);
        currentEnvelopeType_ = type;
    }

    std::vector<float> envelopeTable_;
    size_t envelopeTableSize_ = kDefaultEnvelopeSize;
    GrainEnvelopeType currentEnvelopeType_ = GrainEnvelopeType::Hann;
    double sampleRate_ = 44100.0;
};

}  // namespace Krate::DSP
