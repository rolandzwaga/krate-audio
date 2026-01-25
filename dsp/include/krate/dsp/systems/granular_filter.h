// ==============================================================================
// Layer 3: System Component - Granular Filter
// ==============================================================================
// Granular synthesis engine with per-grain SVF filtering.
// Part of spec 102-granular-filter
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 3 (composes Layer 0-2)
// - Principle X: DSP Constraints (parameter smoothing)
// ==============================================================================
#pragma once

#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/grain_pool.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/grain_processor.h>
#include <krate/dsp/processors/grain_scheduler.h>

#include <array>
#include <cmath>

namespace Krate::DSP {

/// Per-grain slot filter state. One instance per grain slot (64 total).
/// Indexed parallel to GrainPool's internal grain array.
struct FilteredGrainState {
    SVF filterL;           ///< Left channel SVF filter
    SVF filterR;           ///< Right channel SVF filter
    float cutoffHz = 1000.0f;  ///< This grain's randomized cutoff frequency
    bool filterEnabled = true; ///< Snapshot of global filterEnabled at grain trigger
};

/// Granular synthesis engine with per-grain SVF filtering.
/// Each grain has independent filter state, allowing spectral variations
/// impossible with post-granular filtering.
///
/// Signal flow per grain: read -> pitch -> envelope -> filter -> pan
class GranularFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr float kDefaultMaxDelaySeconds = 2.0f;
    static constexpr float kDefaultSmoothTimeMs = 20.0f;
    static constexpr float kFreezeCrossfadeMs = 50.0f;
    static constexpr float kMinCutoffHz = 20.0f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 20.0f;
    static constexpr float kMaxRandomizationOctaves = 4.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Prepare engine for processing
    /// @param sampleRate Current sample rate
    /// @param maxDelaySeconds Maximum delay buffer length in seconds
    void prepare(double sampleRate, float maxDelaySeconds = kDefaultMaxDelaySeconds) noexcept {
        sampleRate_ = sampleRate;

        // Prepare delay buffers
        delayL_.prepare(sampleRate, maxDelaySeconds);
        delayR_.prepare(sampleRate, maxDelaySeconds);

        // Prepare grain components
        pool_.prepare(sampleRate);
        scheduler_.prepare(sampleRate);
        processor_.prepare(sampleRate);

        // Prepare all 128 SVF instances (2 per grain slot)
        for (auto& state : filterStates_) {
            state.filterL.prepare(sampleRate);
            state.filterR.prepare(sampleRate);
        }

        // Configure parameter smoothers
        grainSizeSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));
        pitchSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));
        positionSmoother_.configure(kDefaultSmoothTimeMs, static_cast<float>(sampleRate));

        // Configure gain scaling smoother (very fast response)
        gainScaleSmoother_.configure(2.0f, static_cast<float>(sampleRate));

        // Configure freeze crossfade
        freezeCrossfade_.configure(kFreezeCrossfadeMs, static_cast<float>(sampleRate));

        // Generate default envelope table
        regenerateEnvelope(envelopeType_);

        reset();
    }

    /// Reset engine state
    void reset() noexcept {
        delayL_.reset();
        delayR_.reset();
        pool_.reset();
        scheduler_.reset();
        processor_.reset();

        // Reset all filter states
        for (auto& state : filterStates_) {
            state.filterL.reset();
            state.filterR.reset();
            state.cutoffHz = baseCutoffHz_;
            state.filterEnabled = filterEnabled_;
        }

        // Snap smoothers to current values
        grainSizeSmoother_.snapTo(grainSizeMs_);
        pitchSmoother_.snapTo(pitchSemitones_);
        positionSmoother_.snapTo(positionMs_);

        // Snap gain scaling to 1.0 (no grains active after reset)
        gainScaleSmoother_.snapTo(1.0f);

        freezeCrossfade_.snapTo(frozen_ ? 1.0f : 0.0f);
        currentSample_ = 0;
    }

    /// Seed RNG for reproducible behavior (testing)
    void seed(uint32_t seedValue) noexcept {
        rng_ = Xorshift32(seedValue);
        scheduler_.seed(seedValue + 1);
    }

    // =========================================================================
    // Granular Parameter Setters (same as GranularEngine)
    // =========================================================================

    /// Set grain size in milliseconds (10-500ms)
    void setGrainSize(float ms) noexcept {
        grainSizeMs_ = std::clamp(ms, 10.0f, 500.0f);
        grainSizeSmoother_.setTarget(grainSizeMs_);
    }

    /// Set grain density (grains per second, 1-100 Hz)
    void setDensity(float grainsPerSecond) noexcept {
        density_ = std::clamp(grainsPerSecond, 1.0f, 100.0f);
        scheduler_.setDensity(density_);
    }

    /// Set base pitch shift in semitones (-24 to +24)
    void setPitch(float semitones) noexcept {
        pitchSemitones_ = std::clamp(semitones, -24.0f, 24.0f);
        pitchSmoother_.setTarget(pitchSemitones_);
    }

    /// Set pitch spray/randomization (0-1)
    void setPitchSpray(float amount) noexcept {
        pitchSpray_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Set base delay position in milliseconds (0-2000ms)
    void setPosition(float ms) noexcept {
        positionMs_ = std::clamp(ms, 0.0f, 2000.0f);
        positionSmoother_.setTarget(positionMs_);
    }

    /// Set position spray/randomization (0-1)
    void setPositionSpray(float amount) noexcept {
        positionSpray_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Set reverse playback probability (0-1)
    void setReverseProbability(float probability) noexcept {
        reverseProbability_ = std::clamp(probability, 0.0f, 1.0f);
    }

    /// Set pan spray/randomization (0-1)
    void setPanSpray(float amount) noexcept {
        panSpray_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Set timing jitter (0-1)
    void setJitter(float amount) noexcept {
        scheduler_.setJitter(amount);
    }

    /// Set envelope type for new grains
    void setEnvelopeType(GrainEnvelopeType type) noexcept {
        envelopeType_ = type;
        processor_.setEnvelopeType(type);
    }

    /// Set pitch quantization mode
    void setPitchQuantMode(PitchQuantMode mode) noexcept {
        pitchQuantMode_ = mode;
    }

    /// Get current pitch quantization mode
    [[nodiscard]] PitchQuantMode getPitchQuantMode() const noexcept {
        return pitchQuantMode_;
    }

    /// Set texture/chaos amount (0-1)
    void setTexture(float amount) noexcept {
        texture_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Get current texture amount
    [[nodiscard]] float getTexture() const noexcept {
        return texture_;
    }

    /// Enable/disable freeze mode
    void setFreeze(bool frozen) noexcept {
        if (frozen != frozen_) {
            frozen_ = frozen;
            freezeCrossfade_.setTarget(frozen ? 1.0f : 0.0f);
        }
    }

    /// Check if frozen
    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

    // =========================================================================
    // Filter Parameter Setters (NEW)
    // =========================================================================

    /// Enable/disable per-grain filtering
    void setFilterEnabled(bool enabled) noexcept {
        filterEnabled_ = enabled;
    }

    /// Check if filtering is enabled
    [[nodiscard]] bool isFilterEnabled() const noexcept {
        return filterEnabled_;
    }

    /// Set base filter cutoff frequency in Hz
    void setFilterCutoff(float hz) noexcept {
        const float maxCutoff = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;
        baseCutoffHz_ = std::clamp(hz, kMinCutoffHz, maxCutoff);
    }

    /// Get base filter cutoff frequency
    [[nodiscard]] float getFilterCutoff() const noexcept {
        return baseCutoffHz_;
    }

    /// Set filter resonance (Q)
    void setFilterResonance(float q) noexcept {
        resonanceQ_ = std::clamp(q, kMinQ, kMaxQ);
        // Update Q for all active grains immediately (global parameter)
        for (Grain* grain : pool_.activeGrains()) {
            if (grain != nullptr && grain->active) {
                const size_t idx = getGrainSlotIndex(grain);
                filterStates_[idx].filterL.setResonance(resonanceQ_);
                filterStates_[idx].filterR.setResonance(resonanceQ_);
            }
        }
    }

    /// Get filter resonance
    [[nodiscard]] float getFilterResonance() const noexcept {
        return resonanceQ_;
    }

    /// Set filter type (LP/HP/BP/Notch)
    void setFilterType(SVFMode mode) noexcept {
        filterType_ = mode;
        // Update type for all active grains immediately (global parameter)
        for (Grain* grain : pool_.activeGrains()) {
            if (grain != nullptr && grain->active) {
                const size_t idx = getGrainSlotIndex(grain);
                filterStates_[idx].filterL.setMode(filterType_);
                filterStates_[idx].filterR.setMode(filterType_);
            }
        }
    }

    /// Get filter type
    [[nodiscard]] SVFMode getFilterType() const noexcept {
        return filterType_;
    }

    /// Set cutoff randomization in octaves (0-4)
    void setCutoffRandomization(float octaves) noexcept {
        cutoffRandomizationOctaves_ = std::clamp(octaves, 0.0f, kMaxRandomizationOctaves);
    }

    /// Get cutoff randomization
    [[nodiscard]] float getCutoffRandomization() const noexcept {
        return cutoffRandomizationOctaves_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process stereo audio
    /// @param inputL Left input sample
    /// @param inputR Right input sample
    /// @param outputL Reference to left output sample
    /// @param outputR Reference to right output sample
    void process(float inputL, float inputR,
                 float& outputL, float& outputR) noexcept {
        // Get smoothed parameters
        const float smoothedGrainSize = grainSizeSmoother_.process();
        const float smoothedPitch = pitchSmoother_.process();
        const float smoothedPosition = positionSmoother_.process();
        const float freezeAmount = freezeCrossfade_.process();

        // Write to delay buffers (unless fully frozen)
        if (freezeAmount < 1.0f) {
            const float writeAmount = 1.0f - freezeAmount;
            delayL_.write(inputL * writeAmount);
            delayR_.write(inputR * writeAmount);
        } else if (!frozen_) {
            delayL_.write(inputL);
            delayR_.write(inputR);
        }

        // Check if we should trigger a new grain
        if (scheduler_.process()) {
            triggerNewGrain(smoothedGrainSize, smoothedPitch, smoothedPosition);
        }

        // Process all active grains with per-grain filtering
        float sumL = 0.0f;
        float sumR = 0.0f;
        size_t activeCount = 0;

        for (Grain* grain : pool_.activeGrains()) {
            if (grain != nullptr && grain->active) {
                const size_t slotIndex = getGrainSlotIndex(grain);

                // Get envelope value
                const float envelope = GrainEnvelope::lookup(
                    envelopeTable_.data(), envelopeTableSize_, grain->envelopePhase);

                // Read from delay buffers with interpolation
                const float delaySamples = std::max(0.0f, grain->readPosition);
                float sampleL = delayL_.readLinear(delaySamples);
                float sampleR = delayR_.readLinear(delaySamples);

                // Apply envelope and amplitude
                sampleL *= envelope * grain->amplitude;
                sampleR *= envelope * grain->amplitude;

                // Apply filter AFTER envelope, BEFORE pan (if enabled)
                if (filterEnabled_ && filterStates_[slotIndex].filterEnabled) {
                    sampleL = filterStates_[slotIndex].filterL.process(sampleL);
                    sampleR = filterStates_[slotIndex].filterR.process(sampleR);
                }

                // Apply panning
                sumL += sampleL * grain->panL;
                sumR += sampleR * grain->panR;

                // Advance grain state
                grain->envelopePhase += grain->envelopeIncrement;
                grain->readPosition += std::abs(grain->playbackRate);

                ++activeCount;

                // Check if grain completed
                if (grain->envelopePhase >= 1.0f) {
                    pool_.releaseGrain(grain);
                }
            }
        }

        // Apply 1/sqrt(n) gain scaling
        const float targetGain = (activeCount > 0)
            ? 1.0f / std::sqrt(static_cast<float>(activeCount))
            : 1.0f;
        gainScaleSmoother_.setTarget(targetGain);
        const float smoothedGain = gainScaleSmoother_.process();

        outputL = sumL * smoothedGain;
        outputR = sumR * smoothedGain;

        ++currentSample_;
    }

    /// Get current active grain count
    [[nodiscard]] size_t activeGrainCount() const noexcept {
        return pool_.activeCount();
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// Get grain slot index from grain pointer
    /// Uses a simple hash based on pointer address since grains are in a fixed array
    [[nodiscard]] size_t getGrainSlotIndex(const Grain* grain) noexcept {
        // Use pointer value modulo max grains as stable index
        // This works because grains are allocated in a fixed contiguous array
        return reinterpret_cast<uintptr_t>(grain) / sizeof(Grain) % GrainPool::kMaxGrains;
    }

    /// Calculate randomized cutoff for a new grain
    [[nodiscard]] float calculateRandomizedCutoff() noexcept {
        if (cutoffRandomizationOctaves_ <= 0.0f) {
            return baseCutoffHz_;
        }

        // Bipolar random: [-1, 1]
        const float randomOffset = rng_.nextFloat();

        // Scale to octaves: [-octaves, +octaves]
        const float octaveOffset = randomOffset * cutoffRandomizationOctaves_;

        // Calculate cutoff: base * 2^offset
        const float cutoff = baseCutoffHz_ * std::exp2(octaveOffset);

        // Clamp to valid range
        const float maxCutoff = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;
        return std::clamp(cutoff, kMinCutoffHz, maxCutoff);
    }

    /// Trigger a new grain with filter initialization
    void triggerNewGrain(float grainSizeMs, float pitchSemitones,
                         float positionMs) noexcept {
        Grain* grain = pool_.acquireGrain(currentSample_);
        if (grain == nullptr) {
            return;
        }

        // Get slot index for this grain
        const size_t slotIndex = getGrainSlotIndex(grain);

        // Reset filter state BEFORE grain initialization (FR-008)
        filterStates_[slotIndex].filterL.reset();
        filterStates_[slotIndex].filterR.reset();

        // Calculate randomized cutoff for this grain
        const float grainCutoff = calculateRandomizedCutoff();
        filterStates_[slotIndex].cutoffHz = grainCutoff;
        filterStates_[slotIndex].filterEnabled = filterEnabled_;

        // Configure filters with current parameters
        filterStates_[slotIndex].filterL.setCutoff(grainCutoff);
        filterStates_[slotIndex].filterR.setCutoff(grainCutoff);
        filterStates_[slotIndex].filterL.setResonance(resonanceQ_);
        filterStates_[slotIndex].filterR.setResonance(resonanceQ_);
        filterStates_[slotIndex].filterL.setMode(filterType_);
        filterStates_[slotIndex].filterR.setMode(filterType_);

        // Apply randomization (spray)
        float effectivePitch = pitchSemitones;
        if (pitchSpray_ > 0.0f) {
            effectivePitch += pitchSpray_ * 24.0f * rng_.nextFloat();
        }

        // Apply pitch quantization
        effectivePitch = quantizePitch(effectivePitch, pitchQuantMode_);

        float effectivePositionMs = positionMs;
        if (positionSpray_ > 0.0f) {
            effectivePositionMs += positionSpray_ * positionMs * rng_.nextUnipolar();
        }

        float pan = 0.0f;
        if (panSpray_ > 0.0f) {
            pan = panSpray_ * rng_.nextFloat();
        }

        const bool reverse = (rng_.nextUnipolar() < reverseProbability_);

        // Convert position from ms to samples
        const float positionSamples =
            effectivePositionMs * static_cast<float>(sampleRate_) / 1000.0f;

        GrainParams params{
            .grainSizeMs = grainSizeMs,
            .pitchSemitones = effectivePitch,
            .positionSamples = positionSamples,
            .pan = pan,
            .reverse = reverse,
            .envelopeType = envelopeType_
        };

        processor_.initializeGrain(*grain, params);

        // Apply texture-based amplitude variation
        if (texture_ > 0.0f) {
            const float minAmplitude = 1.0f - texture_ * 0.8f;
            grain->amplitude = minAmplitude + rng_.nextUnipolar() * (1.0f - minAmplitude);
        }
    }

    /// Regenerate envelope lookup table
    void regenerateEnvelope(GrainEnvelopeType type) noexcept {
        GrainEnvelope::generate(envelopeTable_.data(), envelopeTableSize_, type);
        currentEnvelopeType_ = type;
    }

    // =========================================================================
    // Components
    // =========================================================================

    DelayLine delayL_;
    DelayLine delayR_;
    GrainPool pool_;
    GrainScheduler scheduler_;
    GrainProcessor processor_;

    // Filter state per grain slot
    std::array<FilteredGrainState, GrainPool::kMaxGrains> filterStates_;

    // Envelope table
    static constexpr size_t envelopeTableSize_ = 2048;
    std::array<float, envelopeTableSize_> envelopeTable_{};
    GrainEnvelopeType currentEnvelopeType_ = GrainEnvelopeType::Hann;

    // Parameter smoothers
    OnePoleSmoother grainSizeSmoother_;
    OnePoleSmoother pitchSmoother_;
    OnePoleSmoother positionSmoother_;
    OnePoleSmoother gainScaleSmoother_;
    LinearRamp freezeCrossfade_;

    // RNG
    Xorshift32 rng_{54321};

    // Granular parameter state
    float grainSizeMs_ = 100.0f;
    float density_ = 10.0f;
    float pitchSemitones_ = 0.0f;
    float pitchSpray_ = 0.0f;
    float positionMs_ = 500.0f;
    float positionSpray_ = 0.0f;
    float reverseProbability_ = 0.0f;
    float panSpray_ = 0.0f;
    GrainEnvelopeType envelopeType_ = GrainEnvelopeType::Hann;
    PitchQuantMode pitchQuantMode_ = PitchQuantMode::Off;
    float texture_ = 0.0f;
    bool frozen_ = false;

    // Filter parameter state
    bool filterEnabled_ = true;
    float baseCutoffHz_ = 1000.0f;
    float resonanceQ_ = SVF::kButterworthQ;
    SVFMode filterType_ = SVFMode::Lowpass;
    float cutoffRandomizationOctaves_ = 0.0f;

    // Runtime state
    size_t currentSample_ = 0;
    double sampleRate_ = 44100.0;
};

}  // namespace Krate::DSP
