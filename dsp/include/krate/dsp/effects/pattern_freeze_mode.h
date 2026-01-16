// ==============================================================================
// Layer 4: User Feature - Pattern Freeze Mode
// ==============================================================================
// Rhythmic slice-based freeze effect with Euclidean and generative patterns.
//
// Captures incoming audio and plays it back in rhythmic patterns when freeze
// is engaged. Supports multiple pattern types:
// - Euclidean: Traditional rhythmic patterns (tresillo, cinquillo, etc.)
// - Granular Scatter: Random grain triggering
// - Harmonic Drones: Pitched loop layering
// - Noise Bursts: Filtered noise injection
//
// Composes:
// - RollingCaptureBuffer (Layer 1): Continuous audio capture
// - SlicePool (Layer 1): Pre-allocated slice memory
// - PatternScheduler (Layer 2): Tempo-synced pattern sequencing
// - MultimodeFilter (Layer 2): Feedback filtering
// - OnePoleSmoother (Layer 1): Parameter smoothing
//
// Feature: 069-pattern-freeze
// Layer: 4 (User Feature)
// Reference: specs/069-pattern-freeze/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/rolling_capture_buffer.h>
#include <krate/dsp/primitives/slice_pool.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/multimode_filter.h>
#include <krate/dsp/processors/noise_generator.h>
#include <krate/dsp/processors/pattern_scheduler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

/// @brief Pattern-based freeze effect with rhythmic slice playback
///
/// When freeze is engaged, captures incoming audio and plays it back in
/// rhythmic patterns. The pattern type determines the playback behavior:
/// Euclidean for traditional rhythms, Granular for random textures, etc.
///
/// @par Signal Flow (Freeze Engaged)
/// ```
/// Input (muted) ──────────────────────────────────────> Dry (silent)
///        │                                                   │
///        └─> [Capture Buffer] <─────────────────────────────┘
///                    │
///                    v
///        [Pattern Scheduler] ──> Trigger
///                    │
///                    v
///        [Slice Pool] ──> Allocate slice
///                    │
///                    v
///        [Envelope + Playback] ──> Mix ──> Output
/// ```
///
/// @par Usage
/// @code
/// PatternFreezeMode freeze;
/// freeze.prepare(44100.0, 512, 5000.0f);
/// freeze.setPatternType(PatternType::Euclidean);
/// freeze.setEuclideanSteps(8);
/// freeze.setEuclideanHits(3);
/// freeze.snapParameters();
///
/// // Enable freeze
/// freeze.setFreezeEnabled(true);
///
/// // In process callback
/// freeze.process(left, right, numSamples, ctx);
/// @endcode
class PatternFreezeMode {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kDefaultDryWetMix = 50.0f;
    static constexpr float kSmoothingTimeMs = 20.0f;
    static constexpr size_t kEnvelopeTableSize = 512;
    static constexpr size_t kMaxActiveSlices = 8;
    static constexpr float kPatternCrossfadeMs = PatternFreezeConstants::kPatternCrossfadeMs;
    static constexpr float kMinReadyBufferMs = PatternFreezeConstants::kMinReadyBufferMs;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    PatternFreezeMode() noexcept = default;
    ~PatternFreezeMode() = default;

    // Non-copyable, movable
    PatternFreezeMode(const PatternFreezeMode&) = delete;
    PatternFreezeMode& operator=(const PatternFreezeMode&) = delete;
    PatternFreezeMode(PatternFreezeMode&&) noexcept = default;
    PatternFreezeMode& operator=(PatternFreezeMode&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare for processing (allocates memory)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @param maxDelayMs Maximum delay/capture time in milliseconds
    void prepare(double sampleRate, size_t maxBlockSize,
                 [[maybe_unused]] float maxDelayMs) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Prepare capture buffer (2 seconds for slice material)
        captureBuffer_.prepare(sampleRate, PatternFreezeConstants::kDefaultCaptureBufferSeconds);

        // Prepare slice pool
        const size_t maxSliceSamples = static_cast<size_t>(
            sampleRate * PatternFreezeConstants::kMaxSliceLengthMs / 1000.0);
        slicePool_.prepare(kMaxActiveSlices, sampleRate, maxSliceSamples);

        // Prepare pattern scheduler
        scheduler_.prepare(sampleRate, maxBlockSize);
        scheduler_.setTriggerCallback([this](int step) {
            onPatternTrigger(step);
        });

        // Prepare filters
        filterL_.prepare(sampleRate, maxBlockSize);
        filterR_.prepare(sampleRate, maxBlockSize);

        // Prepare noise generator
        noiseGenerator_.prepare(static_cast<float>(sampleRate), maxBlockSize);
        noiseGenerator_.setNoiseLevel(NoiseType::White, -12.0f);
        noiseGenerator_.setNoiseLevel(NoiseType::Pink, -12.0f);
        noiseGenerator_.setNoiseLevel(NoiseType::Brown, -12.0f);
        noiseGenerator_.setNoiseEnabled(NoiseType::Pink, true);  // Default to pink

        // Allocate output buffer
        outputL_.resize(maxBlockSize, 0.0f);
        outputR_.resize(maxBlockSize, 0.0f);

        // Allocate crossfade buffers (same size as output)
        crossfadeOutL_.resize(maxBlockSize, 0.0f);
        crossfadeOutR_.resize(maxBlockSize, 0.0f);

        // Configure smoothers
        const float sr = static_cast<float>(sampleRate);
        dryWetSmoother_.configure(kSmoothingTimeMs, sr);
        freezeMixSmoother_.configure(kSmoothingTimeMs, sr);

        // Generate envelope table (Hann window by default)
        GrainEnvelope::generate(envelopeTable_.data(), kEnvelopeTableSize,
                                GrainEnvelopeType::Hann);

        // Initialize parameters
        snapParameters();

        prepared_ = true;
    }

    /// @brief Reset all internal state
    void reset() noexcept {
        captureBuffer_.reset();
        slicePool_.reset();
        scheduler_.reset();
        filterL_.reset();
        filterR_.reset();
        noiseGenerator_.reset();

        // Clear active slices
        for (auto& slice : activeSlices_) {
            slice = nullptr;
        }
        activeSliceCount_ = 0;

        // Reset granular state
        granularAccumulator_ = 0.0;

        // Reset drone state
        for (auto& phase : droneLfoPhase_) {
            phase = 0.0;
        }
        droneSliceActive_ = false;
        droneSlicePos_ = 0;
        droneSliceLength_ = 0;

        // Reset noise state
        noiseBurstAccumulator_ = 0.0;
        noiseBurstPosition_ = 0;
        noiseBurstSliceLength_ = 0;
        noiseBurstSliceOffset_ = 0;
        noiseBurstActive_ = false;

        // Keep freeze enabled (always on in Freeze mode)
        freezeEnabled_ = true;
        freezeMixSmoother_.snapTo(1.0f);
        dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);

        // Reset crossfade state
        crossfadeActive_ = false;
        crossfadeProgress_ = 0.0f;
        previousPatternType_ = patternType_;

        // Reset tempo tracking
        tempoValid_ = true;
        lastValidTempo_ = 120.0;
    }

    /// @brief Snap all smoothers to current targets
    void snapParameters() noexcept {
        dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
        const float freezeTarget = freezeEnabled_ ? 1.0f : 0.0f;
        freezeMixSmoother_.snapTo(freezeTarget);
        freezeMixSmoother_.setTarget(freezeTarget);  // Ensure target is also set

        // Update scheduler with current Euclidean parameters
        scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
        scheduler_.setTempoSync(true, noteValue_, noteModifier_);
    }

    /// @brief Check if prepared for processing
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Freeze Control
    // =========================================================================

    /// @brief Enable/disable freeze mode
    void setFreezeEnabled(bool enabled) noexcept {
        freezeEnabled_ = enabled;
        freezeMixSmoother_.setTarget(enabled ? 1.0f : 0.0f);

        if (!enabled) {
            // Return all slices when freeze disabled
            for (auto& slice : activeSlices_) {
                if (slice) {
                    slicePool_.deallocateSlice(slice);
                    slice = nullptr;
                }
            }
            activeSliceCount_ = 0;
        }
    }

    [[nodiscard]] bool isFreezeEnabled() const noexcept { return freezeEnabled_; }

    // =========================================================================
    // Pattern Type Configuration
    // =========================================================================

    void setPatternType(PatternType type) noexcept {
        if (type != patternType_) {
            // If freeze is active, initiate crossfade to new pattern
            if (freezeEnabled_) {
                previousPatternType_ = patternType_;
                crossfadeActive_ = true;
                crossfadeProgress_ = 0.0f;
                crossfadeSamples_ = static_cast<size_t>(sampleRate_ * kPatternCrossfadeMs / 1000.0);
                // Save current output for crossfade (will be filled during next process call)
                if (!outputL_.empty() && !crossfadeOutL_.empty()) {
                    std::copy(outputL_.begin(), outputL_.end(), crossfadeOutL_.begin());
                    std::copy(outputR_.begin(), outputR_.end(), crossfadeOutR_.begin());
                }
            }
            patternType_ = type;
        }
    }
    [[nodiscard]] PatternType getPatternType() const noexcept { return patternType_; }

    /// @brief Check if pattern crossfade is in progress
    [[nodiscard]] bool isCrossfading() const noexcept { return crossfadeActive_; }

    // =========================================================================
    // Euclidean Pattern Parameters
    // =========================================================================

    void setEuclideanSteps(int steps) noexcept {
        euclideanSteps_ = std::clamp(steps,
            PatternFreezeConstants::kMinEuclideanSteps,
            PatternFreezeConstants::kMaxEuclideanSteps);
        scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
    }
    [[nodiscard]] int getEuclideanSteps() const noexcept { return euclideanSteps_; }

    void setEuclideanHits(int hits) noexcept {
        euclideanHits_ = std::clamp(hits, 0, euclideanSteps_);
        scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
    }
    [[nodiscard]] int getEuclideanHits() const noexcept { return euclideanHits_; }

    void setEuclideanRotation(int rotation) noexcept {
        euclideanRotation_ = ((rotation % euclideanSteps_) + euclideanSteps_) % euclideanSteps_;
        scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
    }
    [[nodiscard]] int getEuclideanRotation() const noexcept { return euclideanRotation_; }

    // =========================================================================
    // Slice Parameters
    // =========================================================================

    void setSliceLengthMs(float ms) noexcept {
        sliceLengthMs_ = std::clamp(ms,
            PatternFreezeConstants::kMinSliceLengthMs,
            PatternFreezeConstants::kMaxSliceLengthMs);
    }
    [[nodiscard]] float getSliceLengthMs() const noexcept { return sliceLengthMs_; }

    void setSliceMode(SliceMode mode) noexcept { sliceMode_ = mode; }
    [[nodiscard]] SliceMode getSliceMode() const noexcept { return sliceMode_; }

    // =========================================================================
    // Envelope Parameters
    // =========================================================================

    void setEnvelopeAttackMs(float ms) noexcept {
        envelopeAttackMs_ = std::clamp(ms,
            PatternFreezeConstants::kMinEnvelopeAttackMs,
            PatternFreezeConstants::kMaxEnvelopeAttackMs);
    }
    [[nodiscard]] float getEnvelopeAttackMs() const noexcept { return envelopeAttackMs_; }

    void setEnvelopeReleaseMs(float ms) noexcept {
        envelopeReleaseMs_ = std::clamp(ms,
            PatternFreezeConstants::kMinEnvelopeReleaseMs,
            PatternFreezeConstants::kMaxEnvelopeReleaseMs);
    }
    [[nodiscard]] float getEnvelopeReleaseMs() const noexcept { return envelopeReleaseMs_; }

    void setEnvelopeShape(EnvelopeShape shape) noexcept {
        envelopeShape_ = shape;
        // Regenerate envelope table based on shape
        const auto grainType = (shape == EnvelopeShape::Exponential)
            ? GrainEnvelopeType::Blackman
            : GrainEnvelopeType::Hann;
        GrainEnvelope::generate(envelopeTable_.data(), kEnvelopeTableSize, grainType);
    }
    [[nodiscard]] EnvelopeShape getEnvelopeShape() const noexcept { return envelopeShape_; }

    // =========================================================================
    // Timing Parameters
    // =========================================================================

    void setNoteValue(NoteValue note) noexcept {
        noteValue_ = note;
        scheduler_.setTempoSync(true, noteValue_, noteModifier_);
    }
    [[nodiscard]] NoteValue getNoteValue() const noexcept { return noteValue_; }

    void setNoteModifier(NoteModifier modifier) noexcept {
        noteModifier_ = modifier;
        scheduler_.setTempoSync(true, noteValue_, noteModifier_);
    }
    [[nodiscard]] NoteModifier getNoteModifier() const noexcept { return noteModifier_; }

    // =========================================================================
    // Mix Parameters
    // =========================================================================

    void setDryWetMix(float percent) noexcept {
        dryWetMix_ = std::clamp(percent, 0.0f, 100.0f);
        dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
    }
    [[nodiscard]] float getDryWetMix() const noexcept { return dryWetMix_; }

    // =========================================================================
    // Granular Scatter Parameters (User Story 2)
    // =========================================================================

    void setGranularDensity(float hz) noexcept {
        granularDensityHz_ = std::clamp(hz,
            PatternFreezeConstants::kMinGranularDensityHz,
            PatternFreezeConstants::kMaxGranularDensityHz);
    }
    [[nodiscard]] float getGranularDensity() const noexcept { return granularDensityHz_; }

    void setGranularPositionJitter(float jitter) noexcept {
        granularPositionJitter_ = std::clamp(jitter, 0.0f, 1.0f);
    }
    [[nodiscard]] float getGranularPositionJitter() const noexcept { return granularPositionJitter_; }

    void setGranularSizeJitter(float jitter) noexcept {
        granularSizeJitter_ = std::clamp(jitter, 0.0f, 1.0f);
    }
    [[nodiscard]] float getGranularSizeJitter() const noexcept { return granularSizeJitter_; }

    void setGranularGrainSize(float ms) noexcept {
        granularGrainSizeMs_ = std::clamp(ms,
            PatternFreezeConstants::kMinGranularGrainSizeMs,
            PatternFreezeConstants::kMaxGranularGrainSizeMs);
    }
    [[nodiscard]] float getGranularGrainSize() const noexcept { return granularGrainSizeMs_; }

    // =========================================================================
    // Harmonic Drones Parameters (User Story 3)
    // =========================================================================

    void setDroneVoiceCount(int count) noexcept {
        droneVoiceCount_ = std::clamp(count,
            PatternFreezeConstants::kMinDroneVoices,
            PatternFreezeConstants::kMaxDroneVoices);
    }
    [[nodiscard]] int getDroneVoiceCount() const noexcept { return droneVoiceCount_; }

    void setDroneInterval(PitchInterval interval) noexcept {
        droneInterval_ = interval;
    }
    [[nodiscard]] PitchInterval getDroneInterval() const noexcept { return droneInterval_; }

    void setDroneDrift(float drift) noexcept {
        droneDrift_ = std::clamp(drift, 0.0f, 1.0f);
    }
    [[nodiscard]] float getDroneDrift() const noexcept { return droneDrift_; }

    void setDroneDriftRate(float hz) noexcept {
        droneDriftRateHz_ = std::clamp(hz,
            PatternFreezeConstants::kMinDroneDriftRateHz,
            PatternFreezeConstants::kMaxDroneDriftRateHz);
    }
    [[nodiscard]] float getDroneDriftRate() const noexcept { return droneDriftRateHz_; }

    // =========================================================================
    // Noise Bursts Parameters (User Story 4)
    // =========================================================================

    void setNoiseColor(NoiseColor color) noexcept {
        noiseColor_ = color;
        // Disable all noise types first
        noiseGenerator_.setNoiseEnabled(NoiseType::White, false);
        noiseGenerator_.setNoiseEnabled(NoiseType::Pink, false);
        noiseGenerator_.setNoiseEnabled(NoiseType::Brown, false);
        noiseGenerator_.setNoiseEnabled(NoiseType::Blue, false);
        noiseGenerator_.setNoiseEnabled(NoiseType::Violet, false);
        noiseGenerator_.setNoiseEnabled(NoiseType::Grey, false);
        noiseGenerator_.setNoiseEnabled(NoiseType::Velvet, false);
        noiseGenerator_.setNoiseEnabled(NoiseType::RadioStatic, false);
        // Enable selected noise type
        switch (color) {
            case NoiseColor::White:
                noiseGenerator_.setNoiseEnabled(NoiseType::White, true);
                break;
            case NoiseColor::Pink:
                noiseGenerator_.setNoiseEnabled(NoiseType::Pink, true);
                break;
            case NoiseColor::Brown:
                noiseGenerator_.setNoiseEnabled(NoiseType::Brown, true);
                break;
            case NoiseColor::Blue:
                noiseGenerator_.setNoiseEnabled(NoiseType::Blue, true);
                break;
            case NoiseColor::Violet:
                noiseGenerator_.setNoiseEnabled(NoiseType::Violet, true);
                break;
            case NoiseColor::Grey:
                noiseGenerator_.setNoiseEnabled(NoiseType::Grey, true);
                break;
            case NoiseColor::Velvet:
                noiseGenerator_.setNoiseEnabled(NoiseType::Velvet, true);
                break;
            case NoiseColor::RadioStatic:
                noiseGenerator_.setNoiseEnabled(NoiseType::RadioStatic, true);
                break;
        }
    }
    [[nodiscard]] NoiseColor getNoiseColor() const noexcept { return noiseColor_; }

    void setNoiseBurstRate(NoteValue rate, NoteModifier modifier = NoteModifier::None) noexcept {
        noiseBurstRate_ = rate;
        noiseBurstModifier_ = modifier;
    }
    [[nodiscard]] NoteValue getNoiseBurstRate() const noexcept { return noiseBurstRate_; }
    [[nodiscard]] NoteModifier getNoiseBurstModifier() const noexcept { return noiseBurstModifier_; }

    void setNoiseFilterType(FilterType type) noexcept {
        noiseFilterType_ = type;
    }
    [[nodiscard]] FilterType getNoiseFilterType() const noexcept { return noiseFilterType_; }

    void setNoiseFilterCutoff(float hz) noexcept {
        noiseFilterCutoffHz_ = std::clamp(hz,
            PatternFreezeConstants::kMinNoiseFilterCutoffHz,
            PatternFreezeConstants::kMaxNoiseFilterCutoffHz);
    }
    [[nodiscard]] float getNoiseFilterCutoff() const noexcept { return noiseFilterCutoffHz_; }

    void setNoiseFilterSweep(float sweep) noexcept {
        noiseFilterSweep_ = std::clamp(sweep, 0.0f, 1.0f);
    }
    [[nodiscard]] float getNoiseFilterSweep() const noexcept { return noiseFilterSweep_; }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Check if capture buffer has enough data
    [[nodiscard]] bool isCaptureReady(float minDurationMs) const noexcept {
        return captureBuffer_.isReady(minDurationMs);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0 || left == nullptr || right == nullptr) {
            return;
        }

        // Always capture incoming audio (before freeze mutes input)
        for (size_t i = 0; i < numSamples; ++i) {
            captureBuffer_.writeStereo(left[i], right[i]);
        }

        // Clear output buffer
        std::fill(outputL_.begin(), outputL_.begin() + numSamples, 0.0f);
        std::fill(outputR_.begin(), outputR_.begin() + numSamples, 0.0f);

        // Edge case: Validate tempo (edge case 5)
        const bool tempoNowValid = (ctx.tempoBPM > 0.0 && ctx.tempoBPM < 1000.0);
        if (tempoNowValid) {
            tempoValid_ = true;
            lastValidTempo_ = ctx.tempoBPM;
        } else {
            tempoValid_ = false;
        }

        // Process pattern scheduler if freeze is engaged
        const float freezeMix = freezeMixSmoother_.getCurrentValue();
        if (freezeMix > 0.001f) {
            // Edge case: Check if buffer has enough data (edge case 1)
            const bool bufferReady = captureBuffer_.isReady(kMinReadyBufferMs);

            // Edge case 5: For tempo-synced patterns, stop if tempo invalid
            const bool isTempoSynced = (patternType_ == PatternType::Euclidean ||
                                        patternType_ == PatternType::NoiseBursts);
            const bool canProcess = bufferReady && (!isTempoSynced || tempoValid_);

            if (canProcess) {
                // Use last valid tempo if current is invalid
                BlockContext effectiveCtx = ctx;
                if (!tempoValid_) {
                    effectiveCtx.tempoBPM = lastValidTempo_;
                }

                // Store tempo for pattern processors (NoiseBursts needs this)
                currentTempoBPM_ = effectiveCtx.tempoBPM;

                // Advance pattern scheduler
                scheduler_.process(numSamples, effectiveCtx);

                // Process active slices
                processActiveSlices(numSamples);
            }
        }

        // Handle pattern crossfade (Phase 9)
        if (crossfadeActive_ && numSamples > 0) {
            processCrossfade(numSamples);
        }

        // Mix dry/wet
        // In Freeze mode, dry/wet controls the mix between:
        // - Dry (1 - dryWet): Original input signal (pass-through)
        // - Wet (dryWet): Frozen slice playback
        for (size_t i = 0; i < numSamples; ++i) {
            const float currentDryWet = dryWetSmoother_.process();

            // Final mix: original input * (1-wet) + frozen output * wet
            left[i] = left[i] * (1.0f - currentDryWet) + outputL_[i] * currentDryWet;
            right[i] = right[i] * (1.0f - currentDryWet) + outputR_[i] * currentDryWet;
        }
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Calculate envelope value for a position within a slice
    ///
    /// Uses attack/release parameters to create a proper ADSR-style envelope:
    /// - Attack phase: 0 to attackSamples - fade in using envelope table
    /// - Sustain phase: attackSamples to (length - releaseSamples) - hold at 1.0
    /// - Release phase: (length - releaseSamples) to length - fade out
    ///
    /// @param pos Current position within the slice
    /// @param sliceLength Total length of the slice in samples
    /// @return Envelope value (0.0 to 1.0)
    [[nodiscard]] float calculateEnvelope(size_t pos, size_t sliceLength) const noexcept {
        // Convert ms to samples
        size_t attackSamples = static_cast<size_t>(sampleRate_ * envelopeAttackMs_ / 1000.0);
        size_t releaseSamples = static_cast<size_t>(sampleRate_ * envelopeReleaseMs_ / 1000.0);

        // Ensure attack + release doesn't exceed slice length
        const size_t totalEnvSamples = attackSamples + releaseSamples;
        if (totalEnvSamples >= sliceLength && sliceLength > 0) {
            // Scale down proportionally to fit
            const float scale = static_cast<float>(sliceLength - 1) / static_cast<float>(totalEnvSamples);
            attackSamples = static_cast<size_t>(static_cast<float>(attackSamples) * scale);
            releaseSamples = static_cast<size_t>(static_cast<float>(releaseSamples) * scale);
        }

        if (pos < attackSamples && attackSamples > 0) {
            // Attack phase - use first half of envelope table (0 to peak)
            const float phase = static_cast<float>(pos) / static_cast<float>(attackSamples);
            return GrainEnvelope::lookup(envelopeTable_.data(), kEnvelopeTableSize, phase * 0.5f);
        } else if (pos >= sliceLength - releaseSamples && releaseSamples > 0) {
            // Release phase - use second half of envelope table (peak to 0)
            const size_t releaseStart = sliceLength - releaseSamples;
            const float phase = static_cast<float>(pos - releaseStart) / static_cast<float>(releaseSamples);
            return GrainEnvelope::lookup(envelopeTable_.data(), kEnvelopeTableSize, 0.5f + phase * 0.5f);
        } else {
            // Sustain phase - full volume
            return 1.0f;
        }
    }

    /// @brief Handle pattern trigger (allocate and start new slice)
    void onPatternTrigger([[maybe_unused]] int step) noexcept {
        // Only trigger if freeze is active and capture buffer is ready
        if (!freezeEnabled_ || !captureBuffer_.isReady(sliceLengthMs_)) {
            return;
        }

        // Allocate a new slice
        Slice* slice = slicePool_.allocateSlice();
        if (!slice) {
            // Pool exhausted - find oldest slice to recycle
            size_t oldestIdx = 0;
            size_t maxPos = 0;
            for (size_t i = 0; i < kMaxActiveSlices; ++i) {
                if (activeSlices_[i] && activeSlices_[i]->getPosition() > maxPos) {
                    maxPos = activeSlices_[i]->getPosition();
                    oldestIdx = i;
                }
            }
            if (activeSlices_[oldestIdx]) {
                slicePool_.deallocateSlice(activeSlices_[oldestIdx]);
                activeSlices_[oldestIdx] = nullptr;
                --activeSliceCount_;
                slice = slicePool_.allocateSlice();
            }
        }

        if (!slice) {
            return;  // Still couldn't allocate
        }

        // Calculate slice length in samples
        const size_t sliceSamples = static_cast<size_t>(
            sampleRate_ * sliceLengthMs_ / 1000.0);

        // Extract slice from capture buffer
        // Use random offset for variety (or fixed offset based on step)
        const size_t maxOffset = captureBuffer_.getAvailableSamples() - sliceSamples;
        size_t offset = 0;
        if (patternType_ == PatternType::GranularScatter) {
            // Random offset for scatter
            offset = static_cast<size_t>(rng_.nextUnipolar() * static_cast<float>(maxOffset));
        }

        captureBuffer_.extractSlice(slice->getLeft(), slice->getRight(),
                                    sliceSamples, offset);
        slice->setLength(sliceSamples);
        slice->resetPosition();
        slice->setEnvelopePhase(0.0f);

        // Add to active slices
        for (auto& activeSlot : activeSlices_) {
            if (!activeSlot) {
                activeSlot = slice;
                ++activeSliceCount_;
                break;
            }
        }
    }

    /// @brief Process all active slices, mixing their output
    void processActiveSlices(size_t numSamples) noexcept {
        // Dispatch based on pattern type
        switch (patternType_) {
            case PatternType::Euclidean:
                processEuclideanSlices(numSamples);
                break;
            case PatternType::GranularScatter:
                processGranularScatter(numSamples);
                break;
            case PatternType::HarmonicDrones:
                processHarmonicDrones(numSamples);
                break;
            case PatternType::NoiseBursts:
                processNoiseBursts(numSamples);
                break;
            default:
                // Default to Euclidean if unknown pattern type
                processEuclideanSlices(numSamples);
                break;
        }
    }

    /// @brief Process Euclidean pattern slices
    void processEuclideanSlices(size_t numSamples) noexcept {
        // Calculate gain compensation for overlapping slices
        const float gainComp = (activeSliceCount_ > 0)
            ? 1.0f / std::sqrt(static_cast<float>(activeSliceCount_))
            : 1.0f;

        // Process each active slice
        for (auto& slice : activeSlices_) {
            if (!slice || slice->isComplete()) {
                if (slice) {
                    slicePool_.deallocateSlice(slice);
                    slice = nullptr;
                    --activeSliceCount_;
                }
                continue;
            }

            const size_t sliceLength = slice->getLength();
            const float* sliceL = slice->getLeft();
            const float* sliceR = slice->getRight();

            for (size_t i = 0; i < numSamples; ++i) {
                const size_t pos = slice->getPosition();
                if (pos >= sliceLength) {
                    break;
                }

                // Calculate envelope with attack/release phases
                const float envelope = calculateEnvelope(pos, sliceLength);

                // Add to output with envelope and gain compensation
                outputL_[i] += sliceL[pos] * envelope * gainComp;
                outputR_[i] += sliceR[pos] * envelope * gainComp;

                slice->advancePosition(1);
            }
        }
    }

    /// @brief Process Granular Scatter pattern with Poisson triggering
    void processGranularScatter(size_t numSamples) noexcept {
        if (!captureBuffer_.isReady(granularGrainSizeMs_)) {
            return;
        }

        // Calculate gain compensation for overlapping grains
        const float gainComp = (activeSliceCount_ > 0)
            ? 1.0f / std::sqrt(static_cast<float>(activeSliceCount_))
            : 1.0f;

        // Poisson grain triggering
        // Expected interval = 1/density, using exponential distribution
        for (size_t i = 0; i < numSamples; ++i) {
            // Advance Poisson accumulator
            granularAccumulator_ += granularDensityHz_ / sampleRate_;

            // Generate grain when accumulator exceeds threshold
            if (granularAccumulator_ >= 1.0) {
                granularAccumulator_ -= 1.0;

                // Add random variation using exponential distribution
                // This gives proper Poisson statistics
                const float u = std::max(rng_.nextUnipolar(), 0.001f);
                granularAccumulator_ += -std::log(u) * 0.5;

                // Trigger a new grain
                triggerGranularGrain();
            }
        }

        // Process existing grains
        for (auto& slice : activeSlices_) {
            if (!slice || slice->isComplete()) {
                if (slice) {
                    slicePool_.deallocateSlice(slice);
                    slice = nullptr;
                    --activeSliceCount_;
                }
                continue;
            }

            const size_t sliceLength = slice->getLength();
            const float* sliceL = slice->getLeft();
            const float* sliceR = slice->getRight();

            for (size_t i = 0; i < numSamples; ++i) {
                const size_t pos = slice->getPosition();
                if (pos >= sliceLength) {
                    break;
                }

                // Calculate envelope with attack/release phases
                const float envelope = calculateEnvelope(pos, sliceLength);

                outputL_[i] += sliceL[pos] * envelope * gainComp;
                outputR_[i] += sliceR[pos] * envelope * gainComp;

                slice->advancePosition(1);
            }
        }
    }

    /// @brief Trigger a new granular grain with jitter
    void triggerGranularGrain() noexcept {
        Slice* slice = slicePool_.allocateSlice();
        if (!slice) {
            // Pool exhausted - steal shortest remaining
            size_t shortestIdx = 0;
            size_t minRemaining = SIZE_MAX;
            for (size_t i = 0; i < kMaxActiveSlices; ++i) {
                if (activeSlices_[i]) {
                    const size_t remaining = activeSlices_[i]->getLength() - activeSlices_[i]->getPosition();
                    if (remaining < minRemaining) {
                        minRemaining = remaining;
                        shortestIdx = i;
                    }
                }
            }
            if (activeSlices_[shortestIdx]) {
                slicePool_.deallocateSlice(activeSlices_[shortestIdx]);
                activeSlices_[shortestIdx] = nullptr;
                --activeSliceCount_;
                slice = slicePool_.allocateSlice();
            }
        }

        if (!slice) return;

        // Calculate grain size with jitter (base +/- 50% at 100% jitter)
        float grainSizeMs = granularGrainSizeMs_;
        if (granularSizeJitter_ > 0.0f) {
            const float jitterRange = grainSizeMs * 0.5f * granularSizeJitter_;
            grainSizeMs += (rng_.nextFloat() * 2.0f - 1.0f) * jitterRange;
            grainSizeMs = std::max(grainSizeMs, 10.0f);  // Minimum 10ms
        }

        const size_t grainSamples = static_cast<size_t>(sampleRate_ * grainSizeMs / 1000.0);
        const size_t availableSamples = captureBuffer_.getAvailableSamples();
        const size_t actualGrainSamples = std::min(grainSamples, availableSamples);

        // Calculate position with jitter
        size_t offset = 0;
        if (availableSamples > actualGrainSamples) {
            const size_t maxOffset = availableSamples - actualGrainSamples;
            offset = static_cast<size_t>(rng_.nextUnipolar() * granularPositionJitter_ *
                                         static_cast<float>(maxOffset));
        }

        captureBuffer_.extractSlice(slice->getLeft(), slice->getRight(),
                                    actualGrainSamples, offset);
        slice->setLength(actualGrainSamples);
        slice->resetPosition();
        slice->setEnvelopePhase(0.0f);

        // Add to active slices
        for (auto& activeSlot : activeSlices_) {
            if (!activeSlot) {
                activeSlot = slice;
                ++activeSliceCount_;
                break;
            }
        }
    }

    /// @brief Process Harmonic Drones pattern with multi-voice pitch shifting
    void processHarmonicDrones(size_t numSamples) noexcept {
        if (!captureBuffer_.isReady(sliceLengthMs_)) {
            return;
        }

        // Initialize drone slice if needed
        if (!droneSliceActive_) {
            droneSliceLength_ = static_cast<size_t>(sampleRate_ * sliceLengthMs_ / 1000.0);
            droneSliceActive_ = true;
            droneSlicePos_ = 0;
        }

        // Gain compensation for multiple voices
        const float gainComp = 1.0f / std::sqrt(static_cast<float>(droneVoiceCount_));

        // Get pitch interval in semitones
        const float intervalSemitones = getPitchIntervalSemitones(droneInterval_);

        for (size_t i = 0; i < numSamples; ++i) {
            float sampleL = 0.0f;
            float sampleR = 0.0f;

            // Process each drone voice
            for (int voice = 0; voice < droneVoiceCount_; ++voice) {
                // Calculate pitch ratio for this voice
                const float voicePitch = static_cast<float>(voice) * intervalSemitones;
                float pitchRatio = std::pow(2.0f, voicePitch / 12.0f);

                // Apply drift modulation
                if (droneDrift_ > 0.0f) {
                    // Advance LFO phase
                    droneLfoPhase_[voice] += droneDriftRateHz_ / sampleRate_;
                    if (droneLfoPhase_[voice] >= 1.0) {
                        droneLfoPhase_[voice] -= 1.0;
                    }

                    // Calculate drift (sinusoidal LFO, +/- 50 cents max)
                    const float lfo = std::sin(static_cast<float>(droneLfoPhase_[voice] * 2.0 * 3.14159265358979));
                    const float driftCents = lfo * 50.0f * droneDrift_;
                    pitchRatio *= std::pow(2.0f, driftCents / 1200.0f);
                }

                // Read from capture buffer with pitch shift
                // Simple approach: adjust read position based on pitch ratio
                const double readPos = static_cast<double>(droneSlicePos_) * pitchRatio;
                const size_t readIdx = static_cast<size_t>(std::fmod(readPos, static_cast<double>(droneSliceLength_)));

                // Get sample from capture buffer (with linear interpolation)
                float left = 0.0f, right = 0.0f;
                captureBuffer_.extractSlice(&left, &right, 1, readIdx);

                // Calculate envelope with attack/release phases for crossfade looping
                const float envelope = calculateEnvelope(droneSlicePos_, droneSliceLength_);

                sampleL += left * envelope * gainComp;
                sampleR += right * envelope * gainComp;
            }

            outputL_[i] += sampleL;
            outputR_[i] += sampleR;

            // Advance position with wraparound
            ++droneSlicePos_;
            if (droneSlicePos_ >= droneSliceLength_) {
                droneSlicePos_ = 0;
            }
        }
    }

    /// @brief Get semitones for pitch interval
    [[nodiscard]] static float getPitchIntervalSemitones(PitchInterval interval) noexcept {
        switch (interval) {
            case PitchInterval::Unison: return 0.0f;
            case PitchInterval::MinorThird: return 3.0f;
            case PitchInterval::MajorThird: return 4.0f;
            case PitchInterval::Fourth: return 5.0f;
            case PitchInterval::Fifth: return 7.0f;
            case PitchInterval::Octave: return 12.0f;
            default: return 0.0f;
        }
    }

    /// @brief Process Noise Bursts pattern - slice playback mixed with filtered noise
    ///
    /// Plays captured audio slices with noise layered on top, both shaped by the
    /// shared attack/release envelope. Creates textured, rhythmic bursts that
    /// combine the captured audio character with noise coloration.
    void processNoiseBursts(size_t numSamples) noexcept {
        if (!captureBuffer_.isReady(sliceLengthMs_)) {
            return;
        }

        // Calculate burst interval from note value + modifier using actual tempo
        const float burstIntervalSeconds = noteValueToSeconds(noiseBurstRate_, noiseBurstModifier_, currentTempoBPM_);

        // Calculate slice length in samples (this determines playback length)
        const size_t sliceSamples = static_cast<size_t>(sampleRate_ * sliceLengthMs_ / 1000.0);

        for (size_t i = 0; i < numSamples; ++i) {
            // === Noise burst timing ===
            noiseBurstAccumulator_ += 1.0 / sampleRate_;

            // Trigger new burst at rhythm interval
            if (noiseBurstAccumulator_ >= burstIntervalSeconds) {
                noiseBurstAccumulator_ = 0.0;

                // Check if there's audio content in the capture buffer
                float peakL = 0.0f, peakR = 0.0f;
                const size_t checkSamples = std::min(sliceSamples, captureBuffer_.getAvailableSamples());
                for (size_t s = 0; s < checkSamples; s += 64) {
                    float sL = 0.0f, sR = 0.0f;
                    captureBuffer_.extractSlice(&sL, &sR, 1, s);
                    peakL = std::max(peakL, std::abs(sL));
                    peakR = std::max(peakR, std::abs(sR));
                }
                const float capturedLevel = std::max(peakL, peakR);

                // Only trigger burst if there's actual audio content (> -40dB)
                if (capturedLevel > 0.01f) {
                    noiseBurstActive_ = true;
                    noiseBurstPosition_ = 0;
                    noiseBurstSliceLength_ = sliceSamples;
                    // Random offset into capture buffer for variety
                    const size_t maxOffset = captureBuffer_.getAvailableSamples() > sliceSamples
                        ? captureBuffer_.getAvailableSamples() - sliceSamples : 0;
                    noiseBurstSliceOffset_ = static_cast<size_t>(rng_.nextUnipolar() * static_cast<float>(maxOffset));
                }
            }

            // === Generate output if burst is active ===
            if (noiseBurstActive_ && noiseBurstPosition_ < noiseBurstSliceLength_) {
                // Calculate envelope using shared attack/release parameters
                const float envelope = calculateEnvelope(noiseBurstPosition_, noiseBurstSliceLength_);

                // === Get slice sample from capture buffer ===
                float sliceL = 0.0f, sliceR = 0.0f;
                captureBuffer_.extractSlice(&sliceL, &sliceR, 1, noiseBurstSliceOffset_ + noiseBurstPosition_);

                // === Generate and filter noise ===
                float noiseSample = 0.0f;
                noiseGenerator_.process(&noiseSample, 1);

                // Apply filter with optional sweep
                float cutoff = noiseFilterCutoffHz_;
                if (noiseFilterSweep_ > 0.0f && noiseBurstSliceLength_ > 0) {
                    // Sweep cutoff down during burst
                    const float progress = static_cast<float>(noiseBurstPosition_) / static_cast<float>(noiseBurstSliceLength_);
                    cutoff *= 1.0f - noiseFilterSweep_ * progress;
                    cutoff = std::max(cutoff, 20.0f);
                }

                // Configure and apply filter
                filterL_.setType(noiseFilterType_);
                filterL_.setCutoff(cutoff);
                float filteredNoise = filterL_.processSample(noiseSample);

                // Scale noise to match slice level (noise adds texture, not dominates)
                const float sliceLevel = std::max(std::abs(sliceL), std::abs(sliceR));
                const float noiseLevel = std::max(sliceLevel, 0.1f);  // Minimum noise level

                // === Mix slice + noise, both shaped by envelope ===
                // Slice is primary, noise adds texture/color
                const float mixedL = (sliceL + filteredNoise * noiseLevel * 0.5f) * envelope;
                const float mixedR = (sliceR + filteredNoise * noiseLevel * 0.5f) * envelope;

                outputL_[i] += mixedL;
                outputR_[i] += mixedR;

                // Advance position
                ++noiseBurstPosition_;
                if (noiseBurstPosition_ >= noiseBurstSliceLength_) {
                    noiseBurstActive_ = false;
                }
            }
        }
    }

    /// @brief Convert note value + modifier to seconds at given tempo
    [[nodiscard]] static float noteValueToSeconds(NoteValue note, NoteModifier modifier, double bpm) noexcept {
        const double beatDuration = 60.0 / bpm;
        const float beatsPerNote = getBeatsForNote(note, modifier);
        return static_cast<float>(beatDuration * static_cast<double>(beatsPerNote));
    }

    /// @brief Process pattern crossfade (~500ms equal-power crossfade)
    void processCrossfade(size_t numSamples) noexcept {
        if (!crossfadeActive_ || crossfadeSamples_ == 0) {
            return;
        }

        // Apply crossfade to current output
        for (size_t i = 0; i < numSamples && crossfadeActive_; ++i) {
            // Calculate crossfade position (0.0 to 1.0)
            const float t = crossfadeProgress_ / static_cast<float>(crossfadeSamples_);

            // Equal-power crossfade (cosine-based)
            const float fadeIn = std::sin(t * 1.5707963267948966f);  // pi/2
            const float fadeOut = std::cos(t * 1.5707963267948966f);

            // Apply crossfade - fade out old pattern (stored in crossfade buffers)
            // fade in new pattern (already in output buffers)
            outputL_[i] = outputL_[i] * fadeIn + crossfadeOutL_[i] * fadeOut;
            outputR_[i] = outputR_[i] * fadeIn + crossfadeOutR_[i] * fadeOut;

            // Advance crossfade
            crossfadeProgress_ += 1.0f;
            if (crossfadeProgress_ >= static_cast<float>(crossfadeSamples_)) {
                crossfadeActive_ = false;
                crossfadeProgress_ = 0.0f;
            }
        }
    }

    /// @brief Check if tempo is valid for tempo-synced operations
    [[nodiscard]] bool isTempoValid() const noexcept { return tempoValid_; }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Layer 1 primitives
    RollingCaptureBuffer captureBuffer_;
    SlicePool slicePool_;

    // Layer 2 processors
    PatternScheduler scheduler_;
    MultimodeFilter filterL_;
    MultimodeFilter filterR_;
    NoiseGenerator noiseGenerator_;

    // Layer 1 smoothers
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother freezeMixSmoother_;

    // Envelope table
    std::array<float, kEnvelopeTableSize> envelopeTable_{};

    // Active slices
    std::array<Slice*, kMaxActiveSlices> activeSlices_{};
    size_t activeSliceCount_ = 0;

    // Output buffers
    std::vector<float> outputL_;
    std::vector<float> outputR_;

    // RNG for random pattern modes
    Xorshift32 rng_{12345};

    // Parameters - pattern type
    PatternType patternType_ = kDefaultPatternType;

    // Parameters - Euclidean
    int euclideanSteps_ = PatternFreezeConstants::kDefaultEuclideanSteps;
    int euclideanHits_ = PatternFreezeConstants::kDefaultEuclideanHits;
    int euclideanRotation_ = PatternFreezeConstants::kDefaultEuclideanRotation;

    // Parameters - slice
    float sliceLengthMs_ = PatternFreezeConstants::kDefaultSliceLengthMs;
    SliceMode sliceMode_ = kDefaultSliceMode;

    // Parameters - envelope
    float envelopeAttackMs_ = PatternFreezeConstants::kDefaultEnvelopeAttackMs;
    float envelopeReleaseMs_ = PatternFreezeConstants::kDefaultEnvelopeReleaseMs;
    EnvelopeShape envelopeShape_ = kDefaultEnvelopeShape;

    // Parameters - timing
    NoteValue noteValue_ = NoteValue::Sixteenth;
    NoteModifier noteModifier_ = NoteModifier::None;

    // Parameters - mix
    float dryWetMix_ = kDefaultDryWetMix;

    // Parameters - Granular Scatter (User Story 2)
    float granularDensityHz_ = PatternFreezeConstants::kDefaultGranularDensityHz;
    float granularPositionJitter_ = PatternFreezeConstants::kDefaultPositionJitter;
    float granularSizeJitter_ = PatternFreezeConstants::kDefaultSizeJitter;
    float granularGrainSizeMs_ = PatternFreezeConstants::kDefaultGranularGrainSizeMs;
    double granularAccumulator_ = 0.0;  // For Poisson triggering

    // Parameters - Harmonic Drones (User Story 3)
    int droneVoiceCount_ = PatternFreezeConstants::kDefaultDroneVoices;
    PitchInterval droneInterval_ = kDefaultPitchInterval;
    float droneDrift_ = PatternFreezeConstants::kDefaultDroneDrift;
    float droneDriftRateHz_ = PatternFreezeConstants::kDefaultDroneDriftRateHz;
    std::array<double, 4> droneLfoPhase_{};  // LFO phases for each drone voice
    bool droneSliceActive_ = false;
    size_t droneSlicePos_ = 0;
    size_t droneSliceLength_ = 0;

    // Parameters - Noise Bursts (User Story 4)
    NoiseColor noiseColor_ = kDefaultNoiseColor;
    NoteValue noiseBurstRate_ = NoteValue::Eighth;
    NoteModifier noiseBurstModifier_ = NoteModifier::None;
    FilterType noiseFilterType_ = FilterType::Lowpass;
    float noiseFilterCutoffHz_ = PatternFreezeConstants::kDefaultNoiseFilterCutoffHz;
    float noiseFilterSweep_ = PatternFreezeConstants::kDefaultNoiseFilterSweep;
    double noiseBurstAccumulator_ = 0.0;  // For burst timing
    size_t noiseBurstPosition_ = 0;       // Current position within burst
    size_t noiseBurstSliceLength_ = 0;    // Length of current burst slice
    size_t noiseBurstSliceOffset_ = 0;    // Offset into capture buffer for current burst
    bool noiseBurstActive_ = false;

    // Current tempo for pattern processors (set in process(), used by NoiseBursts)
    double currentTempoBPM_ = 120.0;

    // State - freeze is always enabled in Freeze mode (no checkbox)
    bool freezeEnabled_ = true;

    // Pattern crossfade state (Phase 9)
    PatternType previousPatternType_ = PatternType::Euclidean;
    bool crossfadeActive_ = false;
    float crossfadeProgress_ = 0.0f;
    size_t crossfadeSamples_ = 0;
    std::vector<float> crossfadeOutL_;
    std::vector<float> crossfadeOutR_;

    // Edge case handling: tempo loss detection
    bool tempoValid_ = true;
    double lastValidTempo_ = 120.0;
};

}  // namespace Krate::DSP
