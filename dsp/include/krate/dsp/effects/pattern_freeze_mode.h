// ==============================================================================
// Layer 4: User Feature - Pattern Freeze Mode
// ==============================================================================
// A bar-synchronised re-sampler with a real hold (specs/iterum-freeze/design.md).
//
// Every Capture cycle (a note value phased to the host's song position, or to a free-running clock) is recorded
// into its own buffer from its first sample; at the next cycle boundary that buffer becomes the WINDOW the four
// pattern types play from. The pattern types never track the live input:
// - Euclidean: bar-phased steps; each hit copies Slice Length from the playhead (Fixed: the downbeat, Variable: the
//   bar position) and plays it forward
// - Granular Scatter: a Poisson cloud of grains from a region around the playhead
// - Harmonic Drones: four aligned Hann source heads over the whole window, the upper voices pitched by
//   PitchShiftProcessor (PitchSync), rendered on a 64-sample chunk grid
// - Noise Bursts: bar-phased bursts of a copied slice plus filtered noise (noise-only after the music)
// Hold keeps the window that is playing (and sustains slices and bursts); Dry = Mix / Insert selects the mix law.
//
// Composes:
// - SlicePool (Layer 1): pre-allocated slice and grain memory
// - OnePoleSmoother (Layer 1): mix, duck and hold gates, drone voice gains
// - PatternScheduler (Layer 2): bar-phased Euclidean steps with sample offsets
// - MultimodeFilter, NoiseGenerator (Layer 2): the noise of Noise Bursts
// - PitchShiftProcessor (Layer 2): the upper drone voices
//
// Feature: 069-pattern-freeze, redesigned by specs/iterum-freeze/design.md
// Layer: 4 (User Feature)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, everything sized in prepare(), no allocation in process())
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 4 (composes only from Layer 0-3)
// - Principle X: DSP Constraints (parameter smoothing, click-free)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/euclidean_pattern.h>
#include <krate/dsp/core/grain_envelope.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/core/pattern_freeze_types.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/slice_pool.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/multimode_filter.h>
#include <krate/dsp/processors/noise_generator.h>
#include <krate/dsp/processors/pattern_scheduler.h>
#include <krate/dsp/processors/pitch_shift_processor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Krate::DSP {

/// @brief Pattern-based freeze effect: rhythmic re-sampling of a held capture window
///
/// @par Usage
/// @code
/// PatternFreezeMode freeze;
/// freeze.prepare(44100.0, 512, 5000.0f);
/// freeze.setPatternType(PatternType::Euclidean);
/// freeze.setCaptureLength(CaptureLength::Whole);
/// freeze.setDryMode(DryMode::Insert);
/// freeze.snapParameters();
///
/// // In process callback (ctx carries tempo, isPlaying and projectTimeMusic):
/// freeze.process(left, right, numSamples, ctx);
/// @endcode
class PatternFreezeMode {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kDefaultDryWetMix = 100.0f;
    static constexpr float kSmoothingTimeMs = 20.0f;
    static constexpr float kGrainCoherentCap = 1.41f;  ///< summed grain gains: +3 dB when overlapping grains add in phase
    static constexpr float kGrainMinGain = 0.2f;
    static constexpr size_t kEnvelopeTableSize = 512;
    static constexpr size_t kMaxActiveSlices = 8;
    static constexpr float kMinReadyBufferMs = PatternFreezeConstants::kMinReadyBufferMs;

    /// "No offset" marker of getRecFrom()
    static constexpr int64_t kNone = std::numeric_limits<int64_t>::min();

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    PatternFreezeMode() noexcept = default;
    ~PatternFreezeMode() = default;

    PatternFreezeMode(const PatternFreezeMode&) = delete;
    PatternFreezeMode& operator=(const PatternFreezeMode&) = delete;
    PatternFreezeMode(PatternFreezeMode&&) noexcept = default;
    PatternFreezeMode& operator=(PatternFreezeMode&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare for processing (allocates every buffer the mode uses)
    void prepare(double sampleRate, size_t maxBlockSize, [[maybe_unused]] float maxDelayMs) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlockSize_ = std::max<size_t>(maxBlockSize, 1);
        const double sr = sampleRate_;

        // Three cycle buffers (design 2.1)
        capacity_ = static_cast<int64_t>(std::ceil(static_cast<double>(PatternFreezeConstants::kMaxCaptureLengthSeconds) * sr));
        for (auto& b : buf_) {
            b.l.assign(static_cast<size_t>(capacity_), 0.0f);
            b.r.assign(static_cast<size_t>(capacity_), 0.0f);
        }

        // Slice pool and the two burst voices (each kMaxSliceLengthMs x 2 ch)
        maxSliceSamples_ = static_cast<int64_t>(sr * static_cast<double>(PatternFreezeConstants::kMaxSliceLengthMs) / 1000.0);
        slicePool_.prepare(kMaxActiveSlices, sr, static_cast<size_t>(maxSliceSamples_));
        for (auto& bv : bursts_) {
            bv.l.assign(static_cast<size_t>(maxSliceSamples_), 0.0f);
            bv.r.assign(static_cast<size_t>(maxSliceSamples_), 0.0f);
        }

        // Step scheduler and the three event arrays (design 4.3)
        scheduler_.prepare(sr, maxBlockSize_);
        scheduler_.setTriggerCallback([this](int step, size_t offset) { onStepEvent(step, offset); });
        const double minStepSamples = sr * 60.0 / (PatternFreezeConstants::kMaxClockTempoBPM * 24.0);
        eventCapacity_ = static_cast<size_t>(std::ceil(static_cast<double>(maxBlockSize_) / minStepSamples)) + 2;
        captureEvents_.assign(eventCapacity_, ClockEvent{});
        hitEvents_.assign(eventCapacity_, ClockEvent{});
        burstEvents_.assign(eventCapacity_, ClockEvent{});

        // Noise: the eight colours all at kNoiseLevelDb (design 2.5.4, R14)
        filterL_.prepare(sr, maxBlockSize_);
        noiseGenerator_.prepare(static_cast<float>(sr), maxBlockSize_);
        noiseGenerator_.setSeed(24680);
        for (const NoiseType t : kColourTypes) {
            noiseGenerator_.setNoiseLevel(t, PatternFreezeConstants::kNoiseLevelDb);
        }
        applyNoiseColor();
        noiseGenerator_.snapLevelSmoothers();

        // Smoothers (design 2.7)
        const auto srf = static_cast<float>(sr);
        dryWetSmoother_.configure(kSmoothingTimeMs, srf);
        duckSmoother_.configure(PatternFreezeConstants::kDuckSmoothingMs, srf);
        holdDuckSmoother_.configure(PatternFreezeConstants::kHoldDuckSmoothingMs, srf);
        insertWeight_.configure(kSmoothingTimeMs, srf);
        droneRootGain_.configure(PatternFreezeConstants::kDroneVoiceFadeMs, srf);
        for (auto& g : droneVoiceGain_) {
            g.configure(PatternFreezeConstants::kDroneVoiceFadeMs, srf);
        }

        // Splices (design 2.10)
        spliceSpan_ = std::max<int64_t>(4, std::llround(static_cast<double>(PatternFreezeConstants::kSpliceSpanMs) * sr / 1000.0));
        spliceSearch_ = std::max<int64_t>(1, std::llround(static_cast<double>(PatternFreezeConstants::kSpliceSearchMs) * sr / 1000.0));
        spliceFade_ = std::max<int64_t>(4, std::llround(static_cast<double>(PatternFreezeConstants::kSpliceFadeMs) * sr / 1000.0));
        spliceStep_ = std::max<int64_t>(1, std::llround(sr / 44100.0));
        forcedFade_ = std::max<int64_t>(1, std::llround(static_cast<double>(PatternFreezeConstants::kForcedHeadFadeMs) * sr / 1000.0));
        const auto scratch = static_cast<size_t>(2 * spliceSearch_ + std::max(spliceSpan_, spliceFade_) + 8);
        scratchL_.assign(scratch, 0.0f);
        scratchR_.assign(scratch, 0.0f);
        targetL_.assign(static_cast<size_t>(std::max(spliceSpan_, spliceFade_) + 8), 0.0f);
        targetR_.assign(targetL_.size(), 0.0f);

        // Drones (design 2.5.3)
        for (auto& s : shifters_) {
            s.prepare(sr, static_cast<size_t>(PatternFreezeConstants::kDroneChunkSamples));
            s.setMode(PitchMode::PitchSync);
        }
        droneMinPass_ = static_cast<int64_t>(static_cast<double>(PatternFreezeConstants::kDroneMinPassMs) * sr / 1000.0);
        droneJoinChunks_ = static_cast<int>(std::ceil(0.030 * sr / static_cast<double>(PatternFreezeConstants::kDroneChunkSamples)));

        regenerateEnvelopeTable();
        scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
        scheduler_.setTempoSync(true, noteValue_, noteModifier_);

        prepared_ = true;
        reset();
    }

    /// @brief Reset all internal state (the mode-entry rule of design 7.1): no window, a new span
    void reset() noexcept {
        slicePool_.reset();
        for (auto& v : voices_) {
            v = PoolVoice{};
        }
        scheduler_.reset();
        filterL_.reset();
        noiseGenerator_.reset();

        // Capture state (design 2.2)
        rec_ = 0;
        play_ = 1;
        spare_ = 2;
        hasWindow_ = false;
        winFrom_ = 0;
        winLength_ = 0;
        winOnset_ = kNone;
        winPeak_ = 0.0f;
        winSilent_ = true;
        winComplete_ = false;
        winUncapped_ = true;
        spareLength_ = 0;
        playFollowsSpare_ = false;
        recFollowsPlay_ = false;
        lastBoundaryCaptured_ = false;
        cycleOffset_ = 0;
        recFrom_ = kNone;
        onsetOffset_ = kNone;
        recPeak_ = 0.0f;
        heard_ = false;
        sinceAboveFloor_ = std::numeric_limits<int64_t>::max() / 2;
        captureCount_ = 0;
        sliceTriggerCount_ = 0;
        grainTriggerCount_ = 0;
        hitCount_ = 0;
        lastSliceStart_ = 0;
        lastGrainStart_ = 0;

        // Hold (design 2.3): hold_ is a parameter and stays; the engagement waits again
        holdEngaged_ = false;
        releaseGap_ = false;

        // Transport (design 4.1)
        firstBlock_ = true;
        wasPlaying_ = false;
        isPlaying_ = false;
        freeRunPos_ = 0.0;
        expectedPos_ = 0.0;
        cycleStartIdx_ = 0;
        capClock_.lastIdx = -1;
        burstClock_.lastIdx = -1;
        appliedType_ = patternType_;
        appliedCapture_ = captureLength_;
        appliedStepBeats_ = static_cast<double>(getBeatsForNote(noteValue_, noteModifier_));
        appliedBurstBeats_ = static_cast<double>(getBeatsForNote(noiseBurstRate_, noiseBurstModifier_));

        // Drones
        for (auto& h : heads_) {
            h = DroneHead{};
        }
        droneFifoL_.fill(0.0f);
        droneFifoR_.fill(0.0f);
        droneRunning_ = false;
        droneEnvState_ = EnvState::Idle;
        droneEnv_ = 0.0f;
        dronePos_ = 0;
        droneRelStart_ = 0.0f;
        voiceOn_.fill(false);
        joinDelay_.fill(0);
        droneRootGain_.snapTo(0.0f);
        for (auto& g : droneVoiceGain_) {
            g.snapTo(0.0f);
        }
        drift_.fill(0.0);
        for (auto& s : shifters_) {
            s.reset();
        }

        // Bursts
        for (auto& bv : bursts_) {
            bv.core = VoiceCore{};
            bv.active = false;
            bv.hasSlice = false;
            bv.gates = false;
            bv.fadePos = -1;
        }
        newestBurst_ = 1;
        pendingBurst_ = false;
        pendingCountdown_ = 0;
        pendingK_ = 0;

        sampleCounter_ = 0;
        rng_.seed(12345);
        droneRng_.seed(67890);
        grainCountdown_ = drawGrainInterval();

        dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
        duckSmoother_.snapTo(0.0f);
        holdDuckSmoother_.snapTo(0.0f);
        insertWeight_.snapTo(dryMode_ == DryMode::Insert ? 1.0f : 0.0f);
    }

    /// @brief Snap all smoothers to current targets
    void snapParameters() noexcept {
        dryWetSmoother_.snapTo(dryWetMix_ / 100.0f);
        insertWeight_.snapTo(dryMode_ == DryMode::Insert ? 1.0f : 0.0f);
        scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
        scheduler_.setTempoSync(true, noteValue_, noteModifier_);
    }

    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    // =========================================================================
    // Capture, Hold, Dry (design 2.2, 2.3, 2.7)
    // =========================================================================

    /// @brief Hold keeps the window that is playing (R1); it engages on the first non-silent window
    void setHold(bool on) noexcept { hold_ = on; }
    [[nodiscard]] bool isHeld() const noexcept { return hold_; }
    /// @brief Hold is engaged: hold_ is on and a non-silent window exists
    [[nodiscard]] bool isHoldEngaged() const noexcept { return holdEngaged_; }

    void setCaptureLength(CaptureLength length) noexcept {
        captureLength_ = static_cast<size_t>(length) < static_cast<size_t>(kCaptureLengthCount) ? length
                                                                                                : kDefaultCaptureLength;
    }
    [[nodiscard]] CaptureLength getCaptureLength() const noexcept { return captureLength_; }

    /// @brief The two mix laws are blended by insertWeight_, so a toggle or an automation lane never steps the dry
    /// gain; before the first block the weight snaps (nothing has played yet)
    void setDryMode(DryMode mode) noexcept {
        dryMode_ = mode;
        if (firstBlock_) {
            insertWeight_.snapTo(mode == DryMode::Insert ? 1.0f : 0.0f);
        }
    }
    [[nodiscard]] DryMode getDryMode() const noexcept { return dryMode_; }

    // =========================================================================
    // Pattern Type
    // =========================================================================

    /// @brief The old type stops triggering, its voices finish (design 2.6); applied at the next block
    void setPatternType(PatternType type) noexcept { patternType_ = type; }
    [[nodiscard]] PatternType getPatternType() const noexcept { return patternType_; }

    // =========================================================================
    // Euclidean Pattern Parameters
    // =========================================================================

    void setEuclideanSteps(int steps) noexcept {
        const int s = std::clamp(steps, PatternFreezeConstants::kMinEuclideanSteps,
                                 PatternFreezeConstants::kMaxEuclideanSteps);
        if (s != euclideanSteps_) {
            euclideanSteps_ = s;
            euclideanHits_ = std::min(euclideanHits_, euclideanSteps_);
            euclideanRotation_ = ((euclideanRotation_ % euclideanSteps_) + euclideanSteps_) % euclideanSteps_;
            scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
        }
    }
    [[nodiscard]] int getEuclideanSteps() const noexcept { return euclideanSteps_; }

    void setEuclideanHits(int hits) noexcept {
        const int h = std::clamp(hits, 0, euclideanSteps_);
        if (h != euclideanHits_) {
            euclideanHits_ = h;
            scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
        }
    }
    [[nodiscard]] int getEuclideanHits() const noexcept { return euclideanHits_; }

    void setEuclideanRotation(int rotation) noexcept {
        const int r = ((rotation % euclideanSteps_) + euclideanSteps_) % euclideanSteps_;
        if (r != euclideanRotation_) {
            euclideanRotation_ = r;
            scheduler_.setEuclidean(euclideanHits_, euclideanSteps_, euclideanRotation_);
        }
    }
    [[nodiscard]] int getEuclideanRotation() const noexcept { return euclideanRotation_; }

    // =========================================================================
    // Slice Parameters
    // =========================================================================

    void setSliceLengthMs(float ms) noexcept {
        sliceLengthMs_ = std::clamp(ms, PatternFreezeConstants::kMinSliceLengthMs, PatternFreezeConstants::kMaxSliceLengthMs);
    }
    [[nodiscard]] float getSliceLengthMs() const noexcept { return sliceLengthMs_; }

    /// @brief Fixed replays the window's downbeat; Variable plays the slice at the bar position (design 2.4)
    void setSliceMode(SliceMode mode) noexcept { sliceMode_ = mode; }
    [[nodiscard]] SliceMode getSliceMode() const noexcept { return sliceMode_; }

    // =========================================================================
    // Envelope Parameters
    // =========================================================================

    void setEnvelopeAttackMs(float ms) noexcept {
        envelopeAttackMs_ = std::clamp(ms, PatternFreezeConstants::kMinEnvelopeAttackMs,
                                       PatternFreezeConstants::kMaxEnvelopeAttackMs);
    }
    [[nodiscard]] float getEnvelopeAttackMs() const noexcept { return envelopeAttackMs_; }

    void setEnvelopeReleaseMs(float ms) noexcept {
        envelopeReleaseMs_ = std::clamp(ms, PatternFreezeConstants::kMinEnvelopeReleaseMs,
                                        PatternFreezeConstants::kMaxEnvelopeReleaseMs);
    }
    [[nodiscard]] float getEnvelopeReleaseMs() const noexcept { return envelopeReleaseMs_; }

    /// @brief Linear / Exponential tables that end at exactly 0 (design 2.8); regenerated on change only
    void setEnvelopeShape(EnvelopeShape shape) noexcept {
        if (shape != envelopeShape_) {
            envelopeShape_ = shape;
            regenerateEnvelopeTable();
        }
    }
    [[nodiscard]] EnvelopeShape getEnvelopeShape() const noexcept { return envelopeShape_; }

    // =========================================================================
    // Timing Parameters
    // =========================================================================

    void setNoteValue(NoteValue note) noexcept {
        if (note != noteValue_) {
            noteValue_ = note;
            scheduler_.setTempoSync(true, noteValue_, noteModifier_);
        }
    }
    [[nodiscard]] NoteValue getNoteValue() const noexcept { return noteValue_; }

    void setNoteModifier(NoteModifier modifier) noexcept {
        if (modifier != noteModifier_) {
            noteModifier_ = modifier;
            scheduler_.setTempoSync(true, noteValue_, noteModifier_);
        }
    }
    [[nodiscard]] NoteModifier getNoteModifier() const noexcept { return noteModifier_; }

    // =========================================================================
    // Mix
    // =========================================================================

    void setDryWetMix(float percent) noexcept {
        dryWetMix_ = std::clamp(percent, 0.0f, 100.0f);
        dryWetSmoother_.setTarget(dryWetMix_ / 100.0f);
    }
    [[nodiscard]] float getDryWetMix() const noexcept { return dryWetMix_; }

    // =========================================================================
    // Granular Scatter Parameters
    // =========================================================================

    void setGranularDensity(float hz) noexcept {
        granularDensityHz_ = std::clamp(hz, PatternFreezeConstants::kMinGranularDensityHz,
                                        PatternFreezeConstants::kMaxGranularDensityHz);
    }
    [[nodiscard]] float getGranularDensity() const noexcept { return granularDensityHz_; }

    void setGranularPositionJitter(float jitter) noexcept { granularPositionJitter_ = std::clamp(jitter, 0.0f, 1.0f); }
    [[nodiscard]] float getGranularPositionJitter() const noexcept { return granularPositionJitter_; }

    void setGranularSizeJitter(float jitter) noexcept { granularSizeJitter_ = std::clamp(jitter, 0.0f, 1.0f); }
    [[nodiscard]] float getGranularSizeJitter() const noexcept { return granularSizeJitter_; }

    void setGranularGrainSize(float ms) noexcept {
        granularGrainSizeMs_ = std::clamp(ms, PatternFreezeConstants::kMinGranularGrainSizeMs,
                                          PatternFreezeConstants::kMaxGranularGrainSizeMs);
    }
    [[nodiscard]] float getGranularGrainSize() const noexcept { return granularGrainSizeMs_; }

    // =========================================================================
    // Harmonic Drones Parameters
    // =========================================================================

    void setDroneVoiceCount(int count) noexcept {
        droneVoiceCount_ = std::clamp(count, PatternFreezeConstants::kMinDroneVoices, PatternFreezeConstants::kMaxDroneVoices);
    }
    [[nodiscard]] int getDroneVoiceCount() const noexcept { return droneVoiceCount_; }

    void setDroneInterval(PitchInterval interval) noexcept { droneInterval_ = interval; }
    [[nodiscard]] PitchInterval getDroneInterval() const noexcept { return droneInterval_; }

    void setDroneDrift(float drift) noexcept { droneDrift_ = std::clamp(drift, 0.0f, 1.0f); }
    [[nodiscard]] float getDroneDrift() const noexcept { return droneDrift_; }

    void setDroneDriftRate(float hz) noexcept {
        droneDriftRateHz_ = std::clamp(hz, PatternFreezeConstants::kMinDroneDriftRateHz,
                                       PatternFreezeConstants::kMaxDroneDriftRateHz);
    }
    [[nodiscard]] float getDroneDriftRate() const noexcept { return droneDriftRateHz_; }

    // =========================================================================
    // Noise Bursts Parameters
    // =========================================================================

    void setNoiseColor(NoiseColor color) noexcept {
        if (color != noiseColor_) {
            noiseColor_ = color;
            applyNoiseColor();
        }
    }
    [[nodiscard]] NoiseColor getNoiseColor() const noexcept { return noiseColor_; }

    void setNoiseBurstRate(NoteValue rate, NoteModifier modifier = NoteModifier::None) noexcept {
        noiseBurstRate_ = rate;
        noiseBurstModifier_ = modifier;
    }
    [[nodiscard]] NoteValue getNoiseBurstRate() const noexcept { return noiseBurstRate_; }
    [[nodiscard]] NoteModifier getNoiseBurstModifier() const noexcept { return noiseBurstModifier_; }

    void setNoiseFilterType(FilterType type) noexcept { noiseFilterType_ = type; }
    [[nodiscard]] FilterType getNoiseFilterType() const noexcept { return noiseFilterType_; }

    void setNoiseFilterCutoff(float hz) noexcept {
        noiseFilterCutoffHz_ = std::clamp(hz, PatternFreezeConstants::kMinNoiseFilterCutoffHz,
                                          PatternFreezeConstants::kMaxNoiseFilterCutoffHz);
    }
    [[nodiscard]] float getNoiseFilterCutoff() const noexcept { return noiseFilterCutoffHz_; }

    void setNoiseFilterSweep(float sweep) noexcept { noiseFilterSweep_ = std::clamp(sweep, 0.0f, 1.0f); }
    [[nodiscard]] float getNoiseFilterSweep() const noexcept { return noiseFilterSweep_; }

    // =========================================================================
    // Queries and test accessors (design 7.1)
    // =========================================================================

    /// @brief A window has been captured since reset() (or the last play edge that dropped one)
    [[nodiscard]] bool hasWindow() const noexcept { return hasWindow_; }
    [[nodiscard]] uint64_t getCaptureCount() const noexcept { return captureCount_; }
    /// @brief Euclidean slices started since reset()
    [[nodiscard]] uint64_t getSliceTriggerCount() const noexcept { return sliceTriggerCount_; }
    /// @brief Granular grains started since reset()
    [[nodiscard]] uint64_t getGrainTriggerCount() const noexcept { return grainTriggerCount_; }
    /// @brief Euclidean hits fired since reset() (with or without material to play)
    [[nodiscard]] uint64_t getHitCount() const noexcept { return hitCount_; }
    /// @brief The cycle offset the last grain started at (negative: inside the contiguous spare_)
    [[nodiscard]] int64_t getLastGrainStart() const noexcept { return lastGrainStart_; }
    /// @brief The cycle offset the last Euclidean slice started at, after the read rules of design 2.4
    [[nodiscard]] int64_t getLastSliceStart() const noexcept { return lastSliceStart_; }
    /// @brief The index k of the last step boundary the Euclidean clock fired
    [[nodiscard]] int64_t getStepLastFiredIndex() const noexcept { return scheduler_.getLastFiredIndex(); }
    /// @brief The first cycle offset of unbroken recording in this cycle, or kNone
    [[nodiscard]] int64_t getRecFrom() const noexcept { return recFrom_; }
    [[nodiscard]] int64_t getWindowFrom() const noexcept { return winFrom_; }
    [[nodiscard]] int64_t getWindowLengthSamples() const noexcept { return winLength_; }
    [[nodiscard]] float getWindowPeak() const noexcept { return winPeak_; }
    /// @brief The window in force is silent (peak below kCaptureSilenceFloor); true when there is no window
    [[nodiscard]] bool isWindowSilent() const noexcept { return !hasWindow_ || winSilent_; }

    /// @brief The window's sample at cycle offset @p off (0 outside the stored window)
    void peekWindow(size_t off, float& l, float& r) const noexcept { readWindow(static_cast<int64_t>(off), l, r); }

    /// @brief The window's sample at cycle offset @p off (0 outside [from, length))
    void readWindow(int64_t off, float& l, float& r) const noexcept {
        if (!hasWindow_ || off < winFrom_ || off >= winLength_) {
            l = 0.0f;
            r = 0.0f;
            return;
        }
        const auto i = static_cast<size_t>(off);
        l = buf_[static_cast<size_t>(play_)].l[i];
        r = buf_[static_cast<size_t>(play_)].r[i];
    }

    /// @brief The window at a fractional cycle offset, interpolated towards the forward neighbour
    void readWindowLinear(double off, float& l, float& r) const noexcept {
        const double base = std::floor(off);
        const auto i0 = static_cast<int64_t>(base);
        const auto frac = static_cast<float>(off - base);
        float l0 = 0.0f;
        float r0 = 0.0f;
        float l1 = 0.0f;
        float r1 = 0.0f;
        readWindow(i0, l0, r0);
        readWindow(i0 + 1, l1, r1);
        l = l0 + frac * (l1 - l0);
        r = r0 + frac * (r1 - r0);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio in-place
    void process(float* left, float* right, size_t numSamples, const BlockContext& ctx) noexcept {
        if (!prepared_ || numSamples == 0 || left == nullptr || right == nullptr) {
            return;
        }
        // The event arrays are sized for maxBlockSize: a longer block is processed as consecutive sub-blocks
        size_t done = 0;
        while (done < numSamples) {
            const size_t n = std::min(maxBlockSize_, numSamples - done);
            BlockContext sub = ctx;
            if (done > 0) {
                const double tempo = clampedTempo(ctx.tempoBPM);
                sub.projectTimeMusic = ctx.projectTimeMusic + static_cast<double>(done) * tempo / (60.0 * sampleRate_);
                sub.transportPositionSamples = ctx.transportPositionSamples + static_cast<int64_t>(done);
            }
            sub.blockSize = n;
            processBlock(left + done, right + done, n, sub);
            done += n;
        }
    }

private:
    // =========================================================================
    // Internal types (private and nested: no namespace-level name is added)
    // =========================================================================

    struct CycleBuffer {
        std::vector<float> l;
        std::vector<float> r;
    };

    struct ClockEvent {
        size_t offset = 0;
        int64_t index = 0;
    };

    /// Boundaries k * interval of the musical position, fired by index (design 4.1)
    struct MusicalClock {
        double interval = 4.0;
        int64_t lastIdx = -1;

        void rebase(double pos, double tolBeats) noexcept {
            lastIdx = static_cast<int64_t>(std::ceil((pos - tolBeats) / interval)) - 1;
        }

        size_t collect(double pos0, double beatsPerSample, size_t n, ClockEvent* out, size_t cap) noexcept {
            const double end = pos0 + (static_cast<double>(n) - 0.5) * beatsPerSample;
            const auto last = static_cast<double>(n - 1);
            size_t count = 0;
            for (int64_t k = lastIdx + 1; static_cast<double>(k) * interval < end && count < cap; ++k) {
                const double off =
                    std::clamp(std::round((static_cast<double>(k) * interval - pos0) / beatsPerSample), 0.0, last);
                out[count++] = ClockEvent{static_cast<size_t>(off), k};
                lastIdx = k;
            }
            return count;
        }
    };

    enum class VoiceKind : uint8_t { Euclidean, Grain };
    enum class EnvState : uint8_t { Idle, Attack, Sustain, Release };

    /// Envelope, loop and release state shared by pool voices and burst voices
    struct VoiceCore {
        int64_t length = 0;
        int64_t pos = 0;
        int64_t elapsed = 0;
        int64_t attack = 0;
        int64_t release = 0;
        int64_t loopEnd = 0;
        int64_t loopFade = 0;
        int64_t relPos = 0;
        int64_t relLen = 0;
        float relStart = 0.0f;
        float lastEnv = 0.0f;
        bool sustained = false;
        bool looping = false;
        bool forcedRelease = false;
    };

    struct PoolVoice {
        Slice* slice = nullptr;
        VoiceCore core;
        float gain = 1.0f;
        VoiceKind kind = VoiceKind::Euclidean;
        bool gates = false;
    };

    struct BurstVoice {
        std::vector<float> l;
        std::vector<float> r;
        VoiceCore core;
        bool active = false;
        bool hasSlice = false;
        bool gates = false;
        int64_t fadePos = -1;  ///< >= 0 while the forced 5 ms fade of a reused voice runs
    };

    /// What a trigger reads (design 2.4): the window (or the span) and its readable range [lo, hi)
    struct Src {
        int buf = 0;
        int64_t from = 0;
        int64_t len = 0;
        int64_t onset = kNone;
        int64_t lo = 0;
        int64_t hi = 0;
        int64_t spareLen = 0;
        bool complete = false;
        bool uncapped = true;
    };

    struct DroneHead {
        bool active = false;
        int buf = 0;
        int64_t start = 0;
        int64_t k = 0;
        int64_t lp = 0;
        int64_t wait = 0;
        int64_t fadePos = -1;
    };

    static constexpr int kChunk = PatternFreezeConstants::kDroneChunkSamples;
    static constexpr int kHeads = PatternFreezeConstants::kDroneSourceHeads;
    static constexpr int kUpperVoices = PatternFreezeConstants::kMaxDroneVoices - 1;
    static constexpr double kTwoPi = 6.283185307179586476925286766559;
    static constexpr double kHalfPi = 1.5707963267948966192313216916398;
    static constexpr std::array<NoiseType, 8> kColourTypes = {NoiseType::White, NoiseType::Pink,  NoiseType::Brown,
                                                              NoiseType::Blue,  NoiseType::Violet, NoiseType::Grey,
                                                              NoiseType::Velvet, NoiseType::RadioStatic};

    // =========================================================================
    // Block processing (design 4.1, 4.3)
    // =========================================================================

    [[nodiscard]] static double clampedTempo(double bpm) noexcept {
        return detail::isFinite(bpm) ? std::clamp(bpm, kMinTempoBPM, PatternFreezeConstants::kMaxClockTempoBPM) : 120.0;
    }

    void processBlock(float* left, float* right, size_t n, const BlockContext& ctx) noexcept {
        const double tempo = clampedTempo(ctx.tempoBPM);
        spb_ = sampleRate_ * 60.0 / tempo;
        const double bps = 1.0 / spb_;
        const double tol = 0.5 * bps;
        isPlaying_ = ctx.isPlaying;
        const bool synced = ctx.isPlaying && ctx.projectTimeMusicValid && detail::isFinite(ctx.projectTimeMusic);
        const bool playEdge = ctx.isPlaying && !wasPlaying_;
        const double pos0 = synced ? ctx.projectTimeMusic : freeRunPos_;
        const double delta = pos0 - expectedPos_;
        const bool withinTolerance = std::abs(delta) < std::max(1.0 / 64.0, 0.005 * sampleRate_ / spb_);
        const bool jump = firstBlock_ || playEdge || !withinTolerance;

        applyPatternChange();
        updateHoldState();

        // A play edge that finds a window drops it (design 2.2, R9)
        if (playEdge && hasWindow_ && !holdEngaged_) {
            dropWindow();
        }

        // Clock intervals
        lb_ = getCaptureLengthBeats(captureLength_);
        const double stepBeats = static_cast<double>(getBeatsForNote(noteValue_, noteModifier_));
        const double burstBeats = static_cast<double>(getBeatsForNote(noiseBurstRate_, noiseBurstModifier_));
        const bool captureChanged = captureLength_ != appliedCapture_;
        const bool stepChanged = stepBeats != appliedStepBeats_;
        const bool burstChanged = burstBeats != appliedBurstBeats_;
        appliedCapture_ = captureLength_;
        appliedStepBeats_ = stepBeats;
        appliedBurstBeats_ = burstBeats;
        capClock_.interval = lb_;
        burstClock_.interval = std::max(burstBeats, 1e-6);

        if (jump) {
            capClock_.rebase(pos0, tol);
            cycleStartIdx_ = capClock_.lastIdx;
            scheduler_.rebase(pos0, bps);
            burstClock_.rebase(pos0, tol);
            const double nearest = std::round(pos0 / lb_) * lb_;
            if (std::abs(pos0 - nearest) >= tol || captureChanged) {
                dirty();
            }
        } else {
            if (captureChanged) {
                capClock_.rebase(pos0, tol);
                cycleStartIdx_ = capClock_.lastIdx;
                dirty();
            }
            if (stepChanged) {
                scheduler_.rebase(pos0, bps);
            }
            if (burstChanged) {
                burstClock_.rebase(pos0, tol);
            }
        }

        // The block's events: three fixed arrays, each in offset order (design 4.3)
        numCaptureEvents_ = capClock_.collect(pos0, bps, n, captureEvents_.data(), eventCapacity_);
        numHitEvents_ = 0;
        scheduler_.processFromPosition(pos0, bps, n);
        numBurstEvents_ = burstClock_.collect(pos0, bps, n, burstEvents_.data(), eventCapacity_);

        size_t ci = 0;
        size_t hi = 0;
        size_t bi = 0;
        for (size_t i = 0; i < n; ++i) {
            // Events at sample i: capture first, then hits, then bursts
            while (ci < numCaptureEvents_ && captureEvents_[ci].offset == i) {
                onCaptureBoundary(captureEvents_[ci].index);
                ++ci;
            }
            while (hi < numHitEvents_ && hitEvents_[hi].offset == i) {
                onHit(static_cast<double>(hitEvents_[hi].index) * stepBeats);
                ++hi;
            }
            while (bi < numBurstEvents_ && burstEvents_[bi].offset == i) {
                onBurst(static_cast<double>(burstEvents_[bi].index) * burstBeats);
                ++bi;
            }
            if (pendingBurst_ && --pendingCountdown_ <= 0) {
                pendingBurst_ = false;
                if (appliedType_ == PatternType::NoiseBursts && !releaseGap_) {
                    startBurstNow(pendingK_);
                }
            }

            // The Granular Poisson clock fires at its own sample, before the sample is recorded (design 4.3)
            if (appliedType_ == PatternType::GranularScatter) {
                grainCountdown_ -= 1.0;
                if (grainCountdown_ <= 0.0) {
                    grainCountdown_ += drawGrainInterval();
                    triggerGrain(pos0 + static_cast<double>(i) * bps);
                }
            }

            const float inL = left[i];
            const float inR = right[i];
            recordSample(inL, inR);

            // Wet
            float wetL = 0.0f;
            float wetR = 0.0f;
            float gate = 0.0f;
            renderPoolVoices(wetL, wetR, gate);
            renderBursts(wetL, wetR, gate);
            const int64_t chunkPos = static_cast<int64_t>(sampleCounter_ % static_cast<uint64_t>(kChunk));
            if (chunkPos == 0) {
                renderDroneChunk();
            }
            const float denv = advanceDroneEnvelope();
            wetL += droneFifoL_[static_cast<size_t>(chunkPos)] * denv;
            wetR += droneFifoR_[static_cast<size_t>(chunkPos)] * denv;
            if (droneRunning_) {
                gate = std::max(gate, denv);
            }
            ++sampleCounter_;

            // Mix (design 2.7)
            const float m = dryWetSmoother_.process();
            duckSmoother_.setTarget(releaseGap_ ? 0.0f : gate);
            const float gv = duckSmoother_.process();
            holdDuckSmoother_.setTarget(holdEngaged_ ? 1.0f : 0.0f);
            const float gh = holdDuckSmoother_.process();
            insertWeight_.setTarget(dryMode_ == DryMode::Insert ? 1.0f : 0.0f);
            const float w = insertWeight_.process();
            const float g = std::max(gv, gh);
            // Mix law (1 - m) and Insert law (1 - m g), blended; exactly one of them at w = 0 or 1
            float dryGain = (1.0f - w) * (1.0f - m) + w * (1.0f - m * g);
            if (w <= 0.0f) {
                dryGain = 1.0f - m;
            } else if (w >= 1.0f) {
                dryGain = 1.0f - m * g;
            }
            left[i] = inL * dryGain + wetL * m;
            right[i] = inR * dryGain + wetR * m;
        }

        freeRunPos_ = pos0 + static_cast<double>(n) * bps;
        expectedPos_ = freeRunPos_;
        wasPlaying_ = ctx.isPlaying;
        firstBlock_ = false;
    }

    /// The scheduler's callback: record the hit in the step event array (Euclidean only)
    void onStepEvent([[maybe_unused]] int step, size_t offset) noexcept {
        if (appliedType_ != PatternType::Euclidean || numHitEvents_ >= eventCapacity_) {
            return;
        }
        hitEvents_[numHitEvents_++] = ClockEvent{offset, scheduler_.getLastFiredIndex()};
    }

    void applyPatternChange() noexcept {
        if (patternType_ == appliedType_) {
            return;
        }
        // The old type stops triggering; a sustained slice or burst enters its release (design 2.6)
        releaseSustained();
        appliedType_ = patternType_;
        if (appliedType_ == PatternType::GranularScatter) {
            grainCountdown_ = drawGrainInterval();
        }
    }

    // =========================================================================
    // Capture (design 2.1, 2.2)
    // =========================================================================

    [[nodiscard]] int64_t needSpan() const noexcept {
        const float ms = std::max({kMinReadyBufferMs, sliceLengthMs_,
                                   granularGrainSizeMs_ * (1.0f + 0.5f * granularSizeJitter_)});
        const double samples = static_cast<double>(ms) * sampleRate_ / 1000.0;
        const double half = 0.5 * lb_ * spb_;
        return static_cast<int64_t>(std::min({samples, half, static_cast<double>(capacity_)}));
    }

    [[nodiscard]] int64_t stored() const noexcept { return std::min(cycleOffset_, capacity_); }

    [[nodiscard]] bool spanValid() const noexcept {
        return !hasWindow_ && heard_ && recFrom_ != kNone && stored() - recFrom_ >= needSpan();
    }

    void recordSample(float l, float r) noexcept {
        const float a = std::max(std::abs(l), std::abs(r));
        if (cycleOffset_ < capacity_) {
            const auto i = static_cast<size_t>(cycleOffset_);
            buf_[static_cast<size_t>(rec_)].l[i] = l;
            buf_[static_cast<size_t>(rec_)].r[i] = r;
        }
        if (a >= PatternFreezeConstants::kCaptureSilenceFloor) {
            sinceAboveFloor_ = 0;
            if (!heard_) {
                // The first onset is a dirtying event (no span or window exists before it)
                heard_ = true;
                recFrom_ = cycleOffset_;
                recPeak_ = 0.0f;
                playFollowsSpare_ = false;
                recFollowsPlay_ = false;
            }
            if (onsetOffset_ == kNone) {
                onsetOffset_ = cycleOffset_;
            }
        } else if (sinceAboveFloor_ < std::numeric_limits<int64_t>::max() / 2) {
            ++sinceAboveFloor_;
        }
        if (recFrom_ != kNone && cycleOffset_ >= recFrom_ && cycleOffset_ < capacity_) {
            recPeak_ = std::max(recPeak_, a);
        }
        ++cycleOffset_;
    }

    /// A dirtying event strictly after the cycle's first sample (design 2.2)
    void dirty() noexcept {
        if (cycleOffset_ == 0) {
            return;
        }
        if (spanValid()) {
            // A span that holds needSpan becomes the window, so what is playing is never cut off
            capture(false, recFrom_, stored(), cycleOffset_);
        }
        recFrom_ = heard_ ? cycleOffset_ : kNone;
        recPeak_ = 0.0f;
        playFollowsSpare_ = false;
        recFollowsPlay_ = false;
    }

    void dropWindow() noexcept {
        hasWindow_ = false;
        heard_ = false;
        recFrom_ = kNone;
        onsetOffset_ = kNone;
        recPeak_ = 0.0f;
        playFollowsSpare_ = false;
        recFollowsPlay_ = false;
        lastBoundaryCaptured_ = false;
    }

    void onCaptureBoundary(int64_t index) noexcept {
        cycleStartIdx_ = index;
        const int64_t elapsed = cycleOffset_;
        const int64_t st = stored();
        const double half = 0.5 * lb_ * spb_;
        bool captured = false;
        if (!holdEngaged_) {
            if (hasWindow_ && !winSilent_) {
                if (recFrom_ == 0 && heard_ && static_cast<double>(elapsed) >= half) {
                    capture(true, 0, st, elapsed);
                    captured = true;
                }
            } else if (heard_ && recFrom_ != kNone && st - recFrom_ >= needSpan()) {
                capture(false, recFrom_, st, elapsed);
                captured = true;
            }
        }
        if (!captured) {
            playFollowsSpare_ = false;
            recFollowsPlay_ = false;
        }
        lastBoundaryCaptured_ = captured;
        cycleOffset_ = 0;
        recFrom_ = heard_ ? 0 : kNone;
        onsetOffset_ = kNone;
        recPeak_ = 0.0f;
    }

    /// Rotates the roles: spare_ <- play_, play_ <- rec_, rec_ <- old spare_ (design 2.2 rule 4)
    void capture(bool complete, int64_t from, int64_t length, int64_t elapsed) noexcept {
        const bool pfs = complete && hasWindow_ && winComplete_ && winUncapped_ && lastBoundaryCaptured_;
        const int oldSpare = spare_;
        spareLength_ = winLength_;
        spare_ = play_;
        play_ = rec_;
        rec_ = oldSpare;

        hasWindow_ = true;
        winFrom_ = from;
        winLength_ = length;
        winOnset_ = (onsetOffset_ != kNone && onsetOffset_ >= from && onsetOffset_ < length) ? onsetOffset_ : kNone;
        winPeak_ = recPeak_;
        winSilent_ = recPeak_ < PatternFreezeConstants::kCaptureSilenceFloor;
        winComplete_ = complete;
        winUncapped_ = elapsed <= capacity_;
        playFollowsSpare_ = pfs;
        recFollowsPlay_ = winUncapped_;
        ++captureCount_;
        releaseGap_ = false;

        // A drone head still reading the buffer that becomes rec_ fades out over 5 ms (design 2.6)
        for (auto& h : heads_) {
            if (h.active && h.buf == rec_ && h.fadePos < 0) {
                h.fadePos = 0;
            }
        }
        updateHoldState();
    }

    // =========================================================================
    // Hold (design 2.3)
    // =========================================================================

    void updateHoldState() noexcept {
        const bool engaged = hold_ && hasWindow_ && !winSilent_;
        if (engaged && !holdEngaged_) {
            holdEngaged_ = true;
            releaseGap_ = false;
            for (auto& v : voices_) {
                if (v.slice != nullptr && v.kind == VoiceKind::Euclidean && appliedType_ == PatternType::Euclidean) {
                    trySustain(v.core, v.slice->getLeft(), v.slice->getRight());
                }
            }
            if (appliedType_ == PatternType::NoiseBursts) {
                for (auto& bv : bursts_) {
                    if (bv.active && bv.fadePos < 0) {
                        trySustain(bv.core, bv.l.data(), bv.r.data());
                    }
                }
            }
        } else if (!engaged && holdEngaged_) {
            holdEngaged_ = false;
            releaseSustained();
            if (!hold_ && dryMode_ == DryMode::Insert) {
                releaseGap_ = true;
            }
        }
    }

    // =========================================================================
    // Reading the window (design 2.4)
    // =========================================================================

    /// The material a trigger at the current sample reads, or false (no window, a silent one, no span)
    [[nodiscard]] bool resolveSource(Src& s) const noexcept {
        if (hasWindow_ && !winSilent_) {
            s.buf = play_;
            s.from = winFrom_;
            s.len = winLength_;
            s.onset = winOnset_;
            s.complete = winComplete_;
            s.uncapped = winUncapped_;
            s.spareLen = playFollowsSpare_ ? spareLength_ : 0;
            s.lo = playFollowsSpare_ ? -spareLength_ : winFrom_;
            s.hi = winLength_ + (recFollowsPlay_ ? stored() : 0);
            return true;
        }
        if (spanValid()) {
            s.buf = rec_;
            s.from = recFrom_;
            s.len = stored();
            s.onset = (onsetOffset_ != kNone && onsetOffset_ >= recFrom_ && onsetOffset_ < s.len) ? onsetOffset_ : kNone;
            s.complete = false;
            s.uncapped = true;
            s.spareLen = 0;
            s.lo = s.from;
            s.hi = s.len;
            return true;
        }
        return false;
    }

    /// The playhead P (design 2.4) for a cut at @p thetaBeats into the capture cycle
    [[nodiscard]] int64_t playhead(const Src& s, double thetaBeats) const noexcept {
        if (s.complete && sliceMode_ == SliceMode::Variable) {
            int64_t p = std::llround(std::max(thetaBeats, 0.0) * spb_);
            if (s.uncapped && s.len > 0) {
                p %= s.len;
            }
            return p;
        }
        if (s.complete) {
            return 0;
        }
        return s.onset != kNone ? s.onset : s.from;
    }

    [[nodiscard]] double thetaAt(double posBeats) const noexcept {
        return std::max(0.0, posBeats - static_cast<double>(cycleStartIdx_) * lb_);
    }

    void srcRead(const Src& s, int64_t x, float& l, float& r) const noexcept {
        int b = s.buf;
        int64_t idx = x;
        if (x < 0) {
            b = spare_;
            idx = s.spareLen + x;
        } else if (x >= s.len) {
            b = rec_;
            idx = x - s.len;
        }
        if (idx < 0 || idx >= capacity_) {
            l = 0.0f;
            r = 0.0f;
            return;
        }
        l = buf_[static_cast<size_t>(b)].l[static_cast<size_t>(idx)];
        r = buf_[static_cast<size_t>(b)].r[static_cast<size_t>(idx)];
    }

    /// Copies @p n samples from cycle offset @p start under the read rules of design 2.4; returns the start used
    int64_t copyFromSource(const Src& s, float* dstL, float* dstR, int64_t n, int64_t start, float& peak) noexcept {
        peak = 0.0f;
        const int64_t avail = s.hi - s.lo;
        if (n <= avail) {
            // Rules 1 and 2: contiguous continuation, otherwise a clamp inside [lo, hi - n]
            const int64_t st = std::clamp(start, s.lo, s.hi - n);
            for (int64_t j = 0; j < n; ++j) {
                srcRead(s, st + j, dstL[j], dstR[j]);
                peak = std::max(peak, std::max(std::abs(dstL[j]), std::abs(dstR[j])));
            }
            return st;
        }
        // Rule 3: read [lo, hi) and continue from lo through crossfaded splices (design 2.10)
        if (avail < 16) {
            std::fill(dstL, dstL + n, 0.0f);
            std::fill(dstR, dstR + n, 0.0f);
            return s.lo;
        }
        for (int64_t j = 0; j < avail; ++j) {
            srcRead(s, s.lo + j, dstL[j], dstR[j]);
        }
        int64_t pos = avail;
        const int64_t fade = std::min(spliceFade_, avail / 4);
        while (pos < n) {
            const int64_t numCand = std::max<int64_t>(1, std::min(spliceSearch_ + 1, avail - 2 * fade));
            for (int64_t j = 0; j < numCand - 1 + fade; ++j) {
                srcRead(s, s.lo + j, scratchL_[static_cast<size_t>(j)], scratchR_[static_cast<size_t>(j)]);
            }
            const int64_t c = s.lo + bestCorrelation(dstL + (pos - fade), dstR + (pos - fade), scratchL_.data(),
                                                     scratchR_.data(), fade, numCand);
            for (int64_t j = 0; j < fade; ++j) {
                float xl = 0.0f;
                float xr = 0.0f;
                srcRead(s, c + j, xl, xr);
                const float fi = fadeIn(j, fade);
                const auto d = static_cast<size_t>(pos - fade + j);
                dstL[d] = dstL[d] * (1.0f - fi) + xl * fi;
                dstR[d] = dstR[d] * (1.0f - fi) + xr * fi;
            }
            int64_t q = c + fade;
            if (q >= s.hi) {
                break;
            }
            while (pos < n && q < s.hi) {
                srcRead(s, q, dstL[pos], dstR[pos]);
                ++pos;
                ++q;
            }
        }
        for (int64_t j = pos; j < n; ++j) {
            dstL[j] = 0.0f;
            dstR[j] = 0.0f;
        }
        for (int64_t j = 0; j < n; ++j) {
            peak = std::max(peak, std::max(std::abs(dstL[j]), std::abs(dstR[j])));
        }
        return s.lo;
    }

    /// Equal-gain Hann fade-in weight sin^2 over @p len samples (design 2.10)
    [[nodiscard]] static float fadeIn(int64_t j, int64_t len) noexcept {
        const double x = kHalfPi * (static_cast<double>(j) + 0.5) / static_cast<double>(len);
        const double s = std::sin(x);
        return static_cast<float>(s * s);
    }

    /// findSplice (design 2.10): the candidate c in [0, numCand) whose x[c, c + span) (L + R) correlates best,
    /// normalised, with the target t[0, span)
    [[nodiscard]] int64_t bestCorrelation(const float* tL, const float* tR, const float* xL, const float* xR,
                                          int64_t span, int64_t numCand) const noexcept {
        if (numCand <= 1 || span <= 0) {
            return 0;
        }
        auto score = [&](int64_t c, int64_t step) {
            double num = 0.0;
            double ex = 0.0;
            double et = 0.0;
            for (int64_t j = 0; j < span; j += step) {
                const auto a = static_cast<size_t>(j);
                const auto b = static_cast<size_t>(c + j);
                num += static_cast<double>(tL[a]) * xL[b] + static_cast<double>(tR[a]) * xR[b];
                ex += static_cast<double>(xL[b]) * xL[b] + static_cast<double>(xR[b]) * xR[b];
                et += static_cast<double>(tL[a]) * tL[a] + static_cast<double>(tR[a]) * tR[a];
            }
            return num / std::sqrt(std::max(ex * et, 1e-30));
        };
        const int64_t step = spliceStep_;
        double best = std::numeric_limits<double>::lowest();
        int64_t bestIdx = 0;
        for (int64_t c = 0; c < numCand; c += step) {
            const double sc = score(c, step);
            if (sc > best) {
                best = sc;
                bestIdx = c;
            }
        }
        if (step > 1) {
            const int64_t a = std::max<int64_t>(0, bestIdx - step + 1);
            const int64_t b = std::min<int64_t>(numCand - 1, bestIdx + step - 1);
            best = std::numeric_limits<double>::lowest();
            for (int64_t c = a; c <= b; ++c) {
                const double sc = score(c, 1);
                if (sc > best) {
                    best = sc;
                    bestIdx = c;
                }
            }
        }
        return bestIdx;
    }

    // =========================================================================
    // Envelope (design 2.8)
    // =========================================================================

    void regenerateEnvelopeTable() noexcept {
        const auto type = envelopeShape_ == EnvelopeShape::Exponential ? GrainEnvelopeType::Exponential
                                                                        : GrainEnvelopeType::Linear;
        GrainEnvelope::generate(envelopeTable_.data(), kEnvelopeTableSize, type, 0.5f, 0.5f);
        if (envelopeShape_ == EnvelopeShape::Exponential) {
            const double e4 = std::exp(-4.0);
            for (size_t j = 0; j < kEnvelopeTableSize / 2; ++j) {
                const double v = (std::exp(-4.0 * static_cast<double>(j) / 255.0) - e4) / (1.0 - e4);
                envelopeTable_[kEnvelopeTableSize / 2 + j] = static_cast<float>(std::max(v, 0.0));
            }
            envelopeTable_[kEnvelopeTableSize - 1] = 0.0f;
        }
    }

    [[nodiscard]] float attackEnv(float phase) const noexcept {
        return GrainEnvelope::lookup(envelopeTable_.data(), kEnvelopeTableSize, phase * 0.5f);
    }
    [[nodiscard]] float releaseEnv(float phase) const noexcept {
        return GrainEnvelope::lookup(envelopeTable_.data(), kEnvelopeTableSize, 0.5f + phase * 0.5f);
    }

    /// Attack and release in samples for a voice of @p length, scaled down when they do not fit (H:609-636)
    void envelopeGeometry(VoiceCore& c) const noexcept {
        int64_t a = static_cast<int64_t>(sampleRate_ * static_cast<double>(envelopeAttackMs_) / 1000.0);
        int64_t r = static_cast<int64_t>(sampleRate_ * static_cast<double>(envelopeReleaseMs_) / 1000.0);
        if (a + r >= c.length && c.length > 0) {
            const float scale = static_cast<float>(c.length - 1) / static_cast<float>(a + r);
            a = static_cast<int64_t>(static_cast<float>(a) * scale);
            r = static_cast<int64_t>(static_cast<float>(r) * scale);
        }
        c.attack = a;
        c.release = r;
    }

    [[nodiscard]] float voiceEnvelope(const VoiceCore& c) const noexcept {
        if (c.forcedRelease) {
            if (c.relLen < 2) {
                return 0.0f;
            }
            return c.relStart * releaseEnv(static_cast<float>(c.relPos) / static_cast<float>(c.relLen - 1));
        }
        if (c.sustained) {
            if (c.attack > 0 && c.elapsed < c.attack) {
                return attackEnv(static_cast<float>(c.elapsed) / static_cast<float>(c.attack));
            }
            return 1.0f;
        }
        if (c.attack > 0 && c.pos < c.attack) {
            return attackEnv(static_cast<float>(c.pos) / static_cast<float>(c.attack));
        }
        if (c.release > 0 && c.pos >= c.length - c.release) {
            const int64_t rs = c.length - c.release;
            const float phase =
                c.release < 2 ? 0.0f : static_cast<float>(c.pos - rs) / static_cast<float>(c.release - 1);
            return releaseEnv(phase);
        }
        return 1.0f;
    }

    void voiceRead(const VoiceCore& c, const float* L, const float* R, float& l, float& r) const noexcept {
        const auto p = static_cast<size_t>(c.pos);
        if (c.looping && c.pos >= c.loopEnd) {
            const int64_t j = c.pos - c.loopEnd;
            const float fi = fadeIn(j, c.loopFade);
            l = L[p] * (1.0f - fi) + L[static_cast<size_t>(j)] * fi;
            r = R[p] * (1.0f - fi) + R[static_cast<size_t>(j)] * fi;
            return;
        }
        l = L[p];
        r = R[p];
    }

    /// Advances one sample; false when the voice has finished
    [[nodiscard]] static bool voiceAdvance(VoiceCore& c) noexcept {
        ++c.elapsed;
        if (c.forcedRelease) {
            ++c.relPos;
            if (c.relPos >= c.relLen) {
                return false;
            }
        }
        ++c.pos;
        if (c.looping) {
            if (c.pos >= c.loopEnd + c.loopFade) {
                c.pos = c.loopFade;
            }
            return true;
        }
        return c.pos < c.length;
    }

    /// R5: a slice or burst that is not yet releasing sustains: it loops its copy at the seam of design 2.10
    void trySustain(VoiceCore& c, const float* L, const float* R) noexcept {
        if (c.sustained || c.forcedRelease || c.length < 8 || c.pos >= c.length - c.release) {
            return;
        }
        const int64_t n = c.length;
        int64_t fade = spliceFade_;
        int64_t end = 0;
        if (n < 4 * fade) {
            fade = std::max<int64_t>(1, n / 4);
            end = n - fade;
        } else {
            const int64_t kmax = std::min(spliceSearch_, n / 4);
            const int64_t base = n - fade - kmax;
            end = base + bestCorrelation(L, R, L + base, R + base, fade, kmax + 1);
        }
        if (c.pos >= end || end < fade) {
            return;
        }
        c.loopEnd = end;
        c.loopFade = fade;
        c.looping = true;
        c.sustained = true;
    }

    void forceRelease(VoiceCore& c) const noexcept {
        if (!c.sustained) {
            return;
        }
        c.sustained = false;
        c.forcedRelease = true;
        c.relStart = c.lastEnv;
        c.relPos = 0;
        c.relLen = std::max<int64_t>(1, static_cast<int64_t>(sampleRate_ * static_cast<double>(envelopeReleaseMs_) / 1000.0));
    }

    /// Every sustained slice and burst enters its release (Hold release, type change)
    void releaseSustained() noexcept {
        for (auto& v : voices_) {
            if (v.slice != nullptr) {
                forceRelease(v.core);
            }
        }
        for (auto& bv : bursts_) {
            if (bv.active) {
                forceRelease(bv.core);
            }
        }
    }

    // =========================================================================
    // Euclidean (design 2.5.1)
    // =========================================================================

    [[nodiscard]] int64_t sliceSamples() const noexcept {
        return std::clamp<int64_t>(static_cast<int64_t>(sampleRate_ * static_cast<double>(sliceLengthMs_) / 1000.0), 1,
                                   maxSliceSamples_);
    }

    [[nodiscard]] size_t activeVoiceCount() const noexcept {
        return static_cast<size_t>(std::count_if(voices_.begin(), voices_.end(),
                                                 [](const PoolVoice& v) { return v.slice != nullptr; }));
    }

    PoolVoice* allocateVoice(VoiceKind kind) noexcept {
        Slice* slice = slicePool_.allocateSlice();
        if (slice == nullptr) {
            // Pool exhausted: Euclidean steals the slice with the largest position, Granular the shortest remaining
            size_t steal = 0;
            int64_t bestKey = std::numeric_limits<int64_t>::lowest();
            for (size_t i = 0; i < kMaxActiveSlices; ++i) {
                if (voices_[i].slice == nullptr) {
                    continue;
                }
                const auto& c = voices_[i].core;
                const int64_t key = kind == VoiceKind::Euclidean ? c.elapsed : -(c.length - c.pos);
                if (key > bestKey) {
                    bestKey = key;
                    steal = i;
                }
            }
            if (voices_[steal].slice != nullptr) {
                slicePool_.deallocateSlice(voices_[steal].slice);
                voices_[steal] = PoolVoice{};
                slice = slicePool_.allocateSlice();
            }
        }
        if (slice == nullptr) {
            return nullptr;
        }
        for (auto& v : voices_) {
            if (v.slice == nullptr) {
                v = PoolVoice{};
                v.slice = slice;
                v.kind = kind;
                return &v;
            }
        }
        slicePool_.deallocateSlice(slice);
        return nullptr;
    }

    void onHit(double hitBeat) noexcept {
        ++hitCount_;
        if (releaseGap_) {
            return;
        }
        Src s;
        if (!resolveSource(s)) {
            return;
        }
        // A sustained slice already playing enters its release at this hit (R5)
        for (auto& v : voices_) {
            if (v.slice != nullptr && v.kind == VoiceKind::Euclidean) {
                forceRelease(v.core);
            }
        }
        const size_t active = activeVoiceCount();
        PoolVoice* v = allocateVoice(VoiceKind::Euclidean);
        if (v == nullptr) {
            return;
        }
        const int64_t n = sliceSamples();
        float peak = 0.0f;
        const int64_t start = copyFromSource(s, v->slice->getLeft(), v->slice->getRight(), n,
                                             playhead(s, thetaAt(hitBeat)), peak);
        v->slice->setLength(static_cast<size_t>(n));
        v->core.length = n;
        envelopeGeometry(v->core);
        v->gain = 1.0f / std::sqrt(static_cast<float>(active + 1));
        v->gates = peak >= PatternFreezeConstants::kCaptureSilenceFloor;
        lastSliceStart_ = start;
        ++sliceTriggerCount_;
        if (holdEngaged_) {
            trySustain(v->core, v->slice->getLeft(), v->slice->getRight());
        }
    }

    void renderPoolVoices(float& wetL, float& wetR, float& gate) noexcept {
        for (auto& v : voices_) {
            if (v.slice == nullptr) {
                continue;
            }
            const float env = voiceEnvelope(v.core);
            float l = 0.0f;
            float r = 0.0f;
            voiceRead(v.core, v.slice->getLeft(), v.slice->getRight(), l, r);
            wetL += l * env * v.gain;
            wetR += r * env * v.gain;
            v.core.lastEnv = env;
            if (v.gates) {
                gate = std::max(gate, env);
            }
            if (!voiceAdvance(v.core)) {
                slicePool_.deallocateSlice(v.slice);
                v = PoolVoice{};
            }
        }
    }

    // =========================================================================
    // Granular Scatter (design 2.5.2)
    // =========================================================================

    [[nodiscard]] double drawGrainInterval() noexcept {
        const float u = std::max(rng_.nextUnipolar(), 0.001f);
        return -std::log(static_cast<double>(u)) / static_cast<double>(granularDensityHz_) * sampleRate_;
    }

    void triggerGrain(double posBeats) noexcept {
        if (releaseGap_) {
            return;
        }
        Src s;
        if (!resolveSource(s)) {
            return;
        }
        float grainMs = granularGrainSizeMs_;
        if (granularSizeJitter_ > 0.0f) {
            grainMs += (rng_.nextFloat()) * grainMs * 0.5f * granularSizeJitter_;
            grainMs = std::max(grainMs, 10.0f);
        }
        const int64_t g = std::clamp<int64_t>(static_cast<int64_t>(sampleRate_ * static_cast<double>(grainMs) / 1000.0),
                                              1, maxSliceSamples_);
        // The scatter region: Slice Length at Position Jitter 0, the whole window at 100 %
        const double wn = static_cast<double>(s.len - s.from);
        const double sl = static_cast<double>(sliceSamples());
        const double region = std::max(1.0, std::min(wn, sl + static_cast<double>(granularPositionJitter_) *
                                                                  std::max(0.0, wn - sl)));
        const double p = static_cast<double>(playhead(s, thetaAt(posBeats)));
        const double lo = static_cast<double>(s.lo);
        const double hiMinusG = static_cast<double>(s.hi - g);
        double a = p - 0.5 * region;
        a = std::min(a, hiMinusG - region);
        a = std::max(a, lo);
        const double u = static_cast<double>(rng_.nextUnipolar());
        double st = std::floor(a + u * region);
        st = std::min(st, a + region - 1.0);
        st = std::min(st, hiMinusG);
        st = std::max(st, lo);

        PoolVoice* v = allocateVoice(VoiceKind::Grain);
        if (v == nullptr) {
            return;
        }
        float grainGainSum = 0.0f;
        for (const PoolVoice& o : voices_) {
            if (o.slice != nullptr && &o != v) {
                grainGainSum += o.gain;
            }
        }
        float peak = 0.0f;
        const int64_t start =
            copyFromSource(s, v->slice->getLeft(), v->slice->getRight(), g, static_cast<int64_t>(st), peak);
        v->slice->setLength(static_cast<size_t>(g));
        v->core.length = g;
        envelopeGeometry(v->core);
        // Overlapping grains of a stationary tone sum coherently: the gain is what is left under a summed-gain cap
        // of +3 dB (sqrt 2), floored so a dense cloud still sounds (design 2.5.2)
        v->gain = std::clamp(kGrainCoherentCap - grainGainSum, kGrainMinGain, 1.0f);
        v->gates = false;  // the cloud never gates the dry (design 2.7)
        lastGrainStart_ = start;
        ++grainTriggerCount_;
    }

    // =========================================================================
    // Harmonic Drones (design 2.5.3)
    // =========================================================================

    /// The drone's source: the non-silent window, or the span
    [[nodiscard]] bool droneSource(int& b, int64_t& from, int64_t& len) const noexcept {
        if (hasWindow_ && !winSilent_) {
            b = play_;
            from = winFrom_;
            len = winLength_;
            return true;
        }
        if (spanValid()) {
            b = rec_;
            from = recFrom_;
            len = stored();
            return true;
        }
        return false;
    }

    [[nodiscard]] int64_t passLength(int64_t avail) const noexcept {
        int64_t lp = std::clamp<int64_t>(sliceSamples(), std::min(droneMinPass_, avail), avail);
        lp -= lp % 4;
        return lp;
    }

    void startDrone() noexcept {
        droneRunning_ = true;
        droneEnvState_ = EnvState::Attack;
        dronePos_ = 0;
        int b = 0;
        int64_t from = 0;
        int64_t len = 0;
        const int64_t lp0 = droneSource(b, from, len) ? passLength(len - from) : droneMinPass_;
        for (int h = 0; h < kHeads; ++h) {
            heads_[static_cast<size_t>(h)] = DroneHead{};
            heads_[static_cast<size_t>(h)].wait = static_cast<int64_t>(h) * lp0 / kHeads;
        }
        const int voices = droneVoiceCount_;
        droneRootGain_.snapTo(1.0f / std::sqrt(static_cast<float>(voices)));
        for (int v = 1; v <= kUpperVoices; ++v) {
            const auto vi = static_cast<size_t>(v - 1);
            voiceOn_[vi] = false;
            droneVoiceGain_[vi].snapTo(0.0f);
            drift_[vi] = static_cast<double>(v) / static_cast<double>(voices);
        }
    }

    void stopDrone() noexcept {
        droneRunning_ = false;
        for (auto& h : heads_) {
            h = DroneHead{};
        }
        voiceOn_.fill(false);
    }

    /// A head's pass start: latch the source, draw a start over the whole window, align it (design 2.5.3)
    void startPass(DroneHead& h) noexcept {
        int b = 0;
        int64_t from = 0;
        int64_t len = 0;
        if (!droneSource(b, from, len) || len - from < 16) {
            h.active = false;
            h.wait = kChunk;
            return;
        }
        const int64_t avail = len - from;
        const int64_t lp = passLength(avail);
        if (lp < 8) {
            h.active = false;
            h.wait = kChunk;
            return;
        }
        const double u = static_cast<double>(droneRng_.nextUnipolar());
        int64_t start = from + static_cast<int64_t>(u * static_cast<double>(avail - lp + 1));
        start = std::clamp(start, from, len - lp);

        // Aligned start: the offset within +/- 5 ms that best correlates with the other heads' next 5 ms
        const int64_t span = std::min(spliceSpan_, lp / 2);
        bool any = false;
        for (int64_t t = 0; t < span; ++t) {
            targetL_[static_cast<size_t>(t)] = 0.0f;
            targetR_[static_cast<size_t>(t)] = 0.0f;
        }
        for (const auto& o : heads_) {
            if (&o == &h || !o.active) {
                continue;
            }
            const auto& ob = buf_[static_cast<size_t>(o.buf)];
            for (int64_t t = 0; t < span; ++t) {
                const int64_t kk = o.k + t;
                if (kk >= o.lp) {
                    break;
                }
                const float w = hann(kk, o.lp);
                const auto idx = static_cast<size_t>(o.start + kk);
                targetL_[static_cast<size_t>(t)] += w * ob.l[idx];
                targetR_[static_cast<size_t>(t)] += w * ob.r[idx];
                any = true;
            }
        }
        if (any) {
            const int64_t c0 = std::max(from, start - spliceSearch_);
            const int64_t c1 = std::min(len - lp, start + spliceSearch_);
            if (c1 > c0) {
                const auto& bb = buf_[static_cast<size_t>(b)];
                start = c0 + bestCorrelation(targetL_.data(), targetR_.data(), bb.l.data() + c0, bb.r.data() + c0, span,
                                             c1 - c0 + 1);
            }
        }
        h.active = true;
        h.buf = b;
        h.start = start;
        h.k = 0;
        h.lp = lp;
        h.fadePos = -1;
    }

    [[nodiscard]] static float hann(int64_t k, int64_t lp) noexcept {
        const double s = std::sin(kTwoPi * 0.5 * static_cast<double>(k) / static_cast<double>(lp));
        return static_cast<float>(s * s);
    }

    void renderDroneChunk() noexcept {
        int sb = 0;
        int64_t sf = 0;
        int64_t sl = 0;
        const bool gate = appliedType_ == PatternType::HarmonicDrones && !releaseGap_ && droneSource(sb, sf, sl);
        if (gate && !droneRunning_) {
            startDrone();
        }
        if (droneRunning_) {
            if (gate) {
                if (droneEnvState_ == EnvState::Release || droneEnvState_ == EnvState::Idle) {
                    const int64_t a = attackSamples();
                    droneEnvState_ = EnvState::Attack;
                    dronePos_ = static_cast<int64_t>(static_cast<float>(a) * droneEnv_);
                }
            } else if (droneEnvState_ == EnvState::Attack || droneEnvState_ == EnvState::Sustain) {
                droneEnvState_ = EnvState::Release;
                droneRelStart_ = droneEnv_;
                dronePos_ = 0;
            } else if (droneEnvState_ == EnvState::Idle) {
                stopDrone();
            }
        }
        if (!droneRunning_) {
            droneFifoL_.fill(0.0f);
            droneFifoR_.fill(0.0f);
            return;
        }

        // The source: four Hann heads at rate 1, 75 % overlap, 0.5 x their sum
        for (int j = 0; j < kChunk; ++j) {
            for (auto& h : heads_) {
                if (!h.active) {
                    if (h.wait > 0) {
                        --h.wait;
                        continue;
                    }
                    startPass(h);
                }
            }
            float l = 0.0f;
            float r = 0.0f;
            for (auto& h : heads_) {
                if (!h.active) {
                    continue;
                }
                float w = hann(h.k, h.lp);
                if (h.fadePos >= 0) {
                    w *= 1.0f - static_cast<float>(h.fadePos) / static_cast<float>(forcedFade_);
                }
                const auto& hb = buf_[static_cast<size_t>(h.buf)];
                const auto idx = static_cast<size_t>(h.start + h.k);
                l += w * hb.l[idx];
                r += w * hb.r[idx];
                ++h.k;
                bool finished = h.k >= h.lp;
                if (h.fadePos >= 0) {
                    ++h.fadePos;
                    finished = finished || h.fadePos >= forcedFade_;
                }
                if (finished) {
                    h.active = false;
                    h.wait = 0;
                }
            }
            srcL_[static_cast<size_t>(j)] = 0.5f * l;
            srcR_[static_cast<size_t>(j)] = 0.5f * r;
        }

        // The voices: the root plays the source; voice v >= 1 runs a PitchSync shifter pair
        const int voices = droneVoiceCount_;
        const float voiceGain = 1.0f / std::sqrt(static_cast<float>(voices));
        droneRootGain_.setTarget(voiceGain);
        const float semis = getIntervalSemitones(droneInterval_);
        const double driftStep = static_cast<double>(droneDriftRateHz_) * kChunk / sampleRate_;
        for (int v = 1; v <= kUpperVoices; ++v) {
            const auto vi = static_cast<size_t>(v - 1);
            const bool want = v < voices;
            float sv = static_cast<float>(v) * semis;
            if (sv > 24.0f) {
                sv -= 48.0f;
            }
            const double sn = std::sin(kTwoPi * drift_[vi]);
            const auto d = static_cast<double>(droneDrift_);
            double cents = 0.0;
            if (sv == 0.0f) {
                const double sigma = (v % 2 == 1) ? 1.0 : -1.0;
                cents = sigma * (1.0 + 49.0 * d * (0.5 + 0.5 * sn));
            } else if (sv == 24.0f) {
                cents = -50.0 * d * (0.5 + 0.5 * sn);
            } else {
                cents = 50.0 * d * sn;
            }
            auto& shL = shifters_[2 * vi];
            auto& shR = shifters_[2 * vi + 1];
            if (want && !voiceOn_[vi]) {
                voiceOn_[vi] = true;
                shL.setSemitones(sv);
                shR.setSemitones(sv);
                shL.setCents(static_cast<float>(cents));
                shR.setCents(static_cast<float>(cents));
                shL.reset();
                shR.reset();
                joinDelay_[vi] = droneJoinChunks_;
                droneVoiceGain_[vi].snapTo(0.0f);
            }
            if (voiceOn_[vi]) {
                droneVoiceGain_[vi].setTarget(want && joinDelay_[vi] == 0 ? voiceGain : 0.0f);
                if (joinDelay_[vi] > 0) {
                    --joinDelay_[vi];
                }
                shL.setSemitones(sv);
                shR.setSemitones(sv);
                shL.setCents(static_cast<float>(cents));
                shR.setCents(static_cast<float>(cents));
                shL.process(srcL_.data(), voiceL_[vi].data(), static_cast<size_t>(kChunk));
                shR.process(srcR_.data(), voiceR_[vi].data(), static_cast<size_t>(kChunk));
            }
            drift_[vi] += driftStep;
            drift_[vi] -= std::floor(drift_[vi]);
        }
        for (int j = 0; j < kChunk; ++j) {
            const auto ju = static_cast<size_t>(j);
            const float g0 = droneRootGain_.process();
            float l = srcL_[ju] * g0;
            float r = srcR_[ju] * g0;
            for (size_t vi = 0; vi < static_cast<size_t>(kUpperVoices); ++vi) {
                if (!voiceOn_[vi]) {
                    continue;
                }
                const float g = droneVoiceGain_[vi].process();
                l += voiceL_[vi][ju] * g;
                r += voiceR_[vi][ju] * g;
            }
            droneFifoL_[ju] = l;
            droneFifoR_[ju] = r;
        }
        for (size_t vi = 0; vi < static_cast<size_t>(kUpperVoices); ++vi) {
            if (voiceOn_[vi] && static_cast<int>(vi) + 1 >= voices && droneVoiceGain_[vi].getCurrentValue() == 0.0f) {
                voiceOn_[vi] = false;  // a voice that left has faded out
            }
        }
    }

    [[nodiscard]] int64_t attackSamples() const noexcept {
        return static_cast<int64_t>(sampleRate_ * static_cast<double>(envelopeAttackMs_) / 1000.0);
    }
    [[nodiscard]] int64_t releaseSamples() const noexcept {
        return static_cast<int64_t>(sampleRate_ * static_cast<double>(envelopeReleaseMs_) / 1000.0);
    }

    /// The global drone envelope, per output sample after the FIFO
    float advanceDroneEnvelope() noexcept {
        switch (droneEnvState_) {
            case EnvState::Idle:
                droneEnv_ = 0.0f;
                break;
            case EnvState::Attack: {
                const int64_t a = attackSamples();
                if (a <= 0 || dronePos_ >= a) {
                    droneEnv_ = 1.0f;
                    droneEnvState_ = EnvState::Sustain;
                } else {
                    droneEnv_ = attackEnv(static_cast<float>(dronePos_) / static_cast<float>(a));
                    ++dronePos_;
                }
                break;
            }
            case EnvState::Sustain:
                droneEnv_ = 1.0f;
                break;
            case EnvState::Release: {
                const int64_t r = releaseSamples();
                if (r < 2 || dronePos_ >= r) {
                    droneEnv_ = 0.0f;
                    droneEnvState_ = EnvState::Idle;
                } else {
                    droneEnv_ = droneRelStart_ * releaseEnv(static_cast<float>(dronePos_) / static_cast<float>(r - 1));
                    ++dronePos_;
                }
                break;
            }
        }
        return droneEnv_;
    }

    // =========================================================================
    // Noise Bursts (design 2.5.4)
    // =========================================================================

    void applyNoiseColor() noexcept {
        for (const NoiseType t : kColourTypes) {
            noiseGenerator_.setNoiseEnabled(t, false);
        }
        const auto idx = static_cast<size_t>(noiseColor_);
        noiseGenerator_.setNoiseEnabled(idx < kColourTypes.size() ? kColourTypes[idx] : NoiseType::Pink, true);
    }

    [[nodiscard]] bool noiseOnlyAllowed() const noexcept {
        return heard_ && (isPlaying_ || static_cast<double>(sinceAboveFloor_) < 2.0 * lb_ * spb_);
    }

    void onBurst(double burstBeat) noexcept {
        if (appliedType_ != PatternType::NoiseBursts || releaseGap_) {
            return;
        }
        Src s;
        if (!resolveSource(s) && !noiseOnlyAllowed()) {
            return;
        }
        const size_t target = static_cast<size_t>(1 - newestBurst_);
        auto& tv = bursts_[target];
        if (tv.active && !pendingBurst_) {
            // The voice that is not the newest is still sounding: it fades out over 5 ms first
            tv.fadePos = 0;
            pendingBurst_ = true;
            pendingCountdown_ = forcedFade_;
            pendingK_ = burstBeat;
            return;
        }
        pendingBurst_ = false;
        startBurstNow(burstBeat);
    }

    void startBurstNow(double burstBeat) noexcept {
        const auto target = static_cast<size_t>(1 - newestBurst_);
        auto& bv = bursts_[target];
        Src s;
        const bool hasSrc = resolveSource(s);
        if (!hasSrc && !noiseOnlyAllowed()) {
            return;
        }
        // A sustained burst releases at the next burst (R5)
        for (auto& o : bursts_) {
            if (o.active) {
                forceRelease(o.core);
            }
        }
        const int64_t n = sliceSamples();
        bv.core = VoiceCore{};
        bv.core.length = n;
        envelopeGeometry(bv.core);
        bv.active = true;
        bv.fadePos = -1;
        bv.hasSlice = hasSrc;
        bv.gates = false;
        if (hasSrc) {
            float peak = 0.0f;
            (void)copyFromSource(s, bv.l.data(), bv.r.data(), n, playhead(s, thetaAt(burstBeat)), peak);
            bv.gates = peak >= PatternFreezeConstants::kCaptureSilenceFloor;
        } else {
            std::fill(bv.l.begin(), bv.l.begin() + n, 0.0f);
            std::fill(bv.r.begin(), bv.r.begin() + n, 0.0f);
        }
        newestBurst_ = static_cast<int>(target);
        filterL_.setType(noiseFilterType_);
        if (holdEngaged_) {
            trySustain(bv.core, bv.l.data(), bv.r.data());
        }
    }

    void renderBursts(float& wetL, float& wetR, float& gate) noexcept {
        if (!bursts_[0].active && !bursts_[1].active) {
            return;
        }
        // The cutoff sweep, every 32 samples from the newest burst's first sample
        const auto& nb = bursts_[static_cast<size_t>(newestBurst_)];
        if (nb.active && nb.core.elapsed % 32 == 0) {
            const float prog = static_cast<float>(std::min(nb.core.elapsed, nb.core.length)) /
                               static_cast<float>(std::max<int64_t>(nb.core.length, 1));
            filterL_.setCutoff(std::max(noiseFilterCutoffHz_ * (1.0f - noiseFilterSweep_ * prog), 20.0f));
        }
        float raw = 0.0f;
        noiseGenerator_.process(&raw, 1);
        const float noise = 0.5f * filterL_.processSample(raw);
        for (auto& bv : bursts_) {
            if (!bv.active) {
                continue;
            }
            float env = voiceEnvelope(bv.core);
            bv.core.lastEnv = env;
            if (bv.fadePos >= 0) {
                env *= 1.0f - static_cast<float>(bv.fadePos) / static_cast<float>(forcedFade_);
            }
            float l = 0.0f;
            float r = 0.0f;
            if (bv.hasSlice) {
                voiceRead(bv.core, bv.l.data(), bv.r.data(), l, r);
            }
            wetL += (l + noise) * env;
            wetR += (r + noise) * env;
            if (bv.gates) {
                gate = std::max(gate, env);
            }
            bool alive = voiceAdvance(bv.core);
            if (bv.fadePos >= 0) {
                ++bv.fadePos;
                if (bv.fadePos >= forcedFade_) {
                    alive = false;
                }
            }
            if (!alive) {
                bv.active = false;
                bv.fadePos = -1;
            }
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Cycle buffers and the window (design 2.1, 2.2)
    std::array<CycleBuffer, 3> buf_{};
    int64_t capacity_ = 0;
    int rec_ = 0;
    int play_ = 1;
    int spare_ = 2;
    bool hasWindow_ = false;
    int64_t winFrom_ = 0;
    int64_t winLength_ = 0;
    int64_t winOnset_ = kNone;
    float winPeak_ = 0.0f;
    bool winSilent_ = true;
    bool winComplete_ = false;
    bool winUncapped_ = true;
    int64_t spareLength_ = 0;
    bool playFollowsSpare_ = false;
    bool recFollowsPlay_ = false;
    bool lastBoundaryCaptured_ = false;
    int64_t cycleOffset_ = 0;
    int64_t recFrom_ = kNone;
    int64_t onsetOffset_ = kNone;
    float recPeak_ = 0.0f;
    bool heard_ = false;
    int64_t sinceAboveFloor_ = 0;
    uint64_t captureCount_ = 0;
    uint64_t sliceTriggerCount_ = 0;
    uint64_t grainTriggerCount_ = 0;
    uint64_t hitCount_ = 0;
    int64_t lastSliceStart_ = 0;
    int64_t lastGrainStart_ = 0;

    // Hold
    bool hold_ = false;
    bool holdEngaged_ = false;
    bool releaseGap_ = false;

    // Transport and clocks (design 4.1)
    bool firstBlock_ = true;
    bool wasPlaying_ = false;
    bool isPlaying_ = false;
    double freeRunPos_ = 0.0;
    double expectedPos_ = 0.0;
    double spb_ = 22050.0;
    double lb_ = 4.0;
    int64_t cycleStartIdx_ = 0;
    MusicalClock capClock_;
    MusicalClock burstClock_;
    PatternScheduler scheduler_;
    std::vector<ClockEvent> captureEvents_;
    std::vector<ClockEvent> hitEvents_;
    std::vector<ClockEvent> burstEvents_;
    size_t eventCapacity_ = 0;
    size_t numCaptureEvents_ = 0;
    size_t numHitEvents_ = 0;
    size_t numBurstEvents_ = 0;
    PatternType appliedType_ = kDefaultPatternType;
    CaptureLength appliedCapture_ = kDefaultCaptureLength;
    double appliedStepBeats_ = 0.5;
    double appliedBurstBeats_ = 0.5;

    // Slices and grains
    SlicePool slicePool_;
    std::array<PoolVoice, kMaxActiveSlices> voices_{};
    int64_t maxSliceSamples_ = 0;
    double grainCountdown_ = 0.0;

    // Splices (design 2.10)
    int64_t spliceSpan_ = 220;
    int64_t spliceSearch_ = 220;
    int64_t spliceFade_ = 220;
    int64_t spliceStep_ = 1;
    int64_t forcedFade_ = 220;
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
    std::vector<float> targetL_;
    std::vector<float> targetR_;

    // Drones (design 2.5.3)
    std::array<DroneHead, kHeads> heads_{};
    std::array<float, kChunk> srcL_{};
    std::array<float, kChunk> srcR_{};
    std::array<float, kChunk> droneFifoL_{};
    std::array<float, kChunk> droneFifoR_{};
    std::array<std::array<float, kChunk>, kUpperVoices> voiceL_{};
    std::array<std::array<float, kChunk>, kUpperVoices> voiceR_{};
    std::array<PitchShiftProcessor, 2 * kUpperVoices> shifters_{};
    std::array<bool, kUpperVoices> voiceOn_{};
    std::array<int, kUpperVoices> joinDelay_{};
    std::array<OnePoleSmoother, kUpperVoices> droneVoiceGain_{};
    std::array<double, kUpperVoices> drift_{};
    OnePoleSmoother droneRootGain_;
    bool droneRunning_ = false;
    EnvState droneEnvState_ = EnvState::Idle;
    float droneEnv_ = 0.0f;
    float droneRelStart_ = 0.0f;
    int64_t dronePos_ = 0;
    int64_t droneMinPass_ = 4410;
    int droneJoinChunks_ = 21;
    uint64_t sampleCounter_ = 0;

    // Noise Bursts (design 2.5.4)
    std::array<BurstVoice, PatternFreezeConstants::kBurstVoices> bursts_{};
    int newestBurst_ = 1;
    bool pendingBurst_ = false;
    int64_t pendingCountdown_ = 0;
    double pendingK_ = 0.0;
    MultimodeFilter filterL_;
    NoiseGenerator noiseGenerator_;

    // Smoothers (design 2.7)
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother duckSmoother_;
    OnePoleSmoother holdDuckSmoother_;
    OnePoleSmoother insertWeight_;  ///< 0 = Mix law, 1 = Insert law (the Dry Mode toggle never steps)

    // Envelope table
    std::array<float, kEnvelopeTableSize> envelopeTable_{};

    // RNG
    Xorshift32 rng_{12345};
    Xorshift32 droneRng_{67890};

    // Parameters
    PatternType patternType_ = kDefaultPatternType;
    int euclideanSteps_ = PatternFreezeConstants::kDefaultEuclideanSteps;
    int euclideanHits_ = PatternFreezeConstants::kDefaultEuclideanHits;
    int euclideanRotation_ = PatternFreezeConstants::kDefaultEuclideanRotation;
    float sliceLengthMs_ = PatternFreezeConstants::kDefaultSliceLengthMs;
    SliceMode sliceMode_ = kDefaultSliceMode;
    float envelopeAttackMs_ = PatternFreezeConstants::kDefaultEnvelopeAttackMs;
    float envelopeReleaseMs_ = PatternFreezeConstants::kDefaultEnvelopeReleaseMs;
    EnvelopeShape envelopeShape_ = kDefaultEnvelopeShape;
    NoteValue noteValue_ = NoteValue::Eighth;
    NoteModifier noteModifier_ = NoteModifier::None;
    float dryWetMix_ = kDefaultDryWetMix;
    CaptureLength captureLength_ = kDefaultCaptureLength;
    DryMode dryMode_ = kDefaultDryMode;
    float granularDensityHz_ = PatternFreezeConstants::kDefaultGranularDensityHz;
    float granularPositionJitter_ = PatternFreezeConstants::kDefaultPositionJitter;
    float granularSizeJitter_ = PatternFreezeConstants::kDefaultSizeJitter;
    float granularGrainSizeMs_ = PatternFreezeConstants::kDefaultGranularGrainSizeMs;
    int droneVoiceCount_ = PatternFreezeConstants::kDefaultDroneVoices;
    PitchInterval droneInterval_ = kDefaultPitchInterval;
    float droneDrift_ = PatternFreezeConstants::kDefaultDroneDrift;
    float droneDriftRateHz_ = PatternFreezeConstants::kDefaultDroneDriftRateHz;
    NoiseColor noiseColor_ = kDefaultNoiseColor;
    NoteValue noiseBurstRate_ = NoteValue::Eighth;
    NoteModifier noiseBurstModifier_ = NoteModifier::None;
    FilterType noiseFilterType_ = FilterType::Lowpass;
    float noiseFilterCutoffHz_ = PatternFreezeConstants::kDefaultNoiseFilterCutoffHz;
    float noiseFilterSweep_ = PatternFreezeConstants::kDefaultNoiseFilterSweep;
};

}  // namespace Krate::DSP
