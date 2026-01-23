// ==============================================================================
// Layer 2: DSP Processors
// sample_hold_filter.h - Sample & Hold Filter Processor
// ==============================================================================
// Feature: 089-sample-hold-filter
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layers 0-1, same-layer composition)
// - Principle X: DSP Constraints (sample-accurate timing, smoothed transitions)
// - Principle XII: Test-First Development
//
// Dependencies:
// - Layer 0: core/random.h (Xorshift32 PRNG)
// - Layer 1: primitives/svf.h (TPT State Variable Filter)
// - Layer 1: primitives/lfo.h (Internal LFO modulation source)
// - Layer 1: primitives/smoother.h (OnePoleSmoother for slew)
// - Layer 2: processors/envelope_follower.h (Envelope source + audio trigger)
// ==============================================================================

#pragma once

#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// TriggerSource Enumeration (FR-001)
// =============================================================================

/// @brief Trigger mode selection for S&H timing
/// @see SampleHoldFilter::setTriggerSource()
enum class TriggerSource : uint8_t {
    Clock = 0,  ///< Regular intervals based on hold time (FR-003)
    Audio,      ///< Transient detection from input signal (FR-004)
    Random      ///< Probability-based at hold intervals (FR-005)
};

// =============================================================================
// SampleSource Enumeration (FR-006)
// =============================================================================

/// @brief Sample value source selection per parameter
/// @note All sources output bipolar [-1, 1] for consistent modulation.
///       Envelope and External sources use conversion: (value * 2) - 1
enum class SampleSource : uint8_t {
    LFO = 0,    ///< Internal LFO output [-1, 1] (FR-007)
    Random,     ///< Xorshift32 random value [-1, 1] (FR-008)
    Envelope,   ///< EnvelopeFollower output [0,1] -> [-1,1] (FR-009)
    External    ///< User-provided value [0,1] -> [-1,1] (FR-010)
};

// =============================================================================
// SampleHoldFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Sample & Hold Filter
///
/// Samples and holds filter parameters at configurable intervals,
/// creating stepped modulation effects synchronized to clock, audio
/// transients, or random probability.
///
/// @par Features
/// - Three trigger modes: Clock, Audio, Random (FR-001)
/// - Four sample sources per parameter: LFO, Random, Envelope, External (FR-006)
/// - Per-parameter source independence (FR-014)
/// - Stereo processing with symmetric pan offset (FR-013)
/// - Slew limiting for smooth transitions (FR-015, FR-016)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends on Layers 0-1, same-layer EnvelopeFollower)
///
/// @par Usage
/// @code
/// SampleHoldFilter filter;
/// filter.prepare(44100.0);
/// filter.setTriggerSource(TriggerSource::Clock);
/// filter.setCutoffSamplingEnabled(true);
/// filter.setCutoffSource(SampleSource::LFO);
/// filter.setHoldTime(100.0f);
/// filter.setLFORate(2.0f);
///
/// // Mono processing
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
///
/// // Or stereo processing
/// filter.processStereo(left, right);
/// @endcode
class SampleHoldFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinHoldTimeMs = 0.1f;       ///< FR-002: Minimum hold time
    static constexpr float kMaxHoldTimeMs = 10000.0f;   ///< FR-002: Maximum hold time
    static constexpr float kMinSlewTimeMs = 0.0f;       ///< FR-015: Instant (no slew)
    static constexpr float kMaxSlewTimeMs = 500.0f;     ///< FR-015: Maximum slew
    static constexpr float kMinLFORate = 0.01f;         ///< FR-007: Minimum LFO rate
    static constexpr float kMaxLFORate = 20.0f;         ///< FR-007: Maximum LFO rate
    static constexpr float kMinCutoffOctaves = 0.0f;    ///< FR-011: No modulation
    static constexpr float kMaxCutoffOctaves = 8.0f;    ///< FR-011: 8 octaves
    static constexpr float kMinQRange = 0.0f;           ///< FR-012: No Q modulation
    static constexpr float kMaxQRange = 1.0f;           ///< FR-012: Full Q range
    static constexpr float kMinPanOctaveRange = 0.0f;   ///< FR-013: No pan offset
    static constexpr float kMaxPanOctaveRange = 4.0f;   ///< FR-013: 4 octave max
    static constexpr float kDefaultBaseQ = 0.707f;      ///< FR-020: Butterworth Q
    static constexpr float kMinBaseCutoff = 20.0f;      ///< FR-019: 20 Hz
    static constexpr float kMaxBaseCutoff = 20000.0f;   ///< FR-019: 20 kHz
    static constexpr float kMinBaseQ = 0.1f;            ///< FR-020
    static constexpr float kMaxBaseQ = 30.0f;           ///< FR-020

    // =========================================================================
    // Lifecycle (FR-025, FR-026)
    // =========================================================================

    /// @brief Default constructor
    SampleHoldFilter() noexcept = default;

    /// @brief Destructor
    ~SampleHoldFilter() = default;

    // Non-copyable (contains filter state)
    SampleHoldFilter(const SampleHoldFilter&) = delete;
    SampleHoldFilter& operator=(const SampleHoldFilter&) = delete;

    // Movable
    SampleHoldFilter(SampleHoldFilter&&) noexcept = default;
    SampleHoldFilter& operator=(SampleHoldFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-025)
    /// @param sampleRate Audio sample rate in Hz (44100-192000)
    /// @pre sampleRate >= 1000.0
    /// @note NOT real-time safe (may initialize state)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

        // Calculate max cutoff based on sample rate
        maxCutoff_ = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;

        // Prepare filters
        filterL_.prepare(sampleRate_);
        filterR_.prepare(sampleRate_);
        filterL_.setMode(filterMode_);
        filterR_.setMode(filterMode_);
        filterL_.setCutoff(baseCutoffHz_);
        filterR_.setCutoff(baseCutoffHz_);
        filterL_.setResonance(baseQ_);
        filterR_.setResonance(baseQ_);

        // Prepare LFO
        lfo_.prepare(sampleRate_);
        lfo_.setWaveform(Waveform::Sine);
        lfo_.setFrequency(lfoRateHz_);

        // Prepare envelope follower for audio trigger and envelope source
        // FR-004: attack=0.1ms, release=50ms, DetectionMode::Peak
        envelopeFollower_.prepare(sampleRate_, 0);
        envelopeFollower_.setMode(DetectionMode::Peak);
        envelopeFollower_.setAttackTime(0.1f);
        envelopeFollower_.setReleaseTime(50.0f);

        // Configure smoothers for slew limiting
        const float sampleRateF = static_cast<float>(sampleRate_);
        cutoffSmoother_.configure(slewTimeMs_, sampleRateF);
        qSmoother_.configure(slewTimeMs_, sampleRateF);
        panSmoother_.configure(slewTimeMs_, sampleRateF);

        // Calculate hold time in samples
        holdTimeSamples_ = holdTimeMs_ * sampleRate_ * 0.001;
        samplesUntilTrigger_ = holdTimeSamples_;

        // Initialize RNG
        rng_.seed(seed_);

        prepared_ = true;
    }

    /// @brief Reset all state while preserving configuration (FR-026)
    /// @post Held values initialized to base parameters (baseCutoff, baseQ=0.707, pan=0)
    /// @post Random state restored to saved seed
    /// @post Filter works immediately without requiring first trigger
    /// @note Real-time safe
    void reset() noexcept {
        // Reset filters
        filterL_.reset();
        filterR_.reset();

        // Reset LFO
        lfo_.reset();

        // Reset envelope follower
        envelopeFollower_.reset();

        // Reset smoothers to no modulation (0.0)
        cutoffSmoother_.snapTo(0.0f);
        qSmoother_.snapTo(0.0f);
        panSmoother_.snapTo(0.0f);

        // Reset trigger state
        samplesUntilTrigger_ = holdTimeSamples_;
        previousEnvelope_ = 0.0f;
        holdingAfterTransient_ = false;
        transientHoldSamples_ = 0.0;

        // Reset RNG to seed for determinism (FR-027)
        rng_.seed(seed_);

        // Reset held values (no modulation offset initially)
        cutoffHeldValue_ = 0.0f;
        qHeldValue_ = 0.0f;
        panHeldValue_ = 0.0f;
    }

    // =========================================================================
    // Processing (FR-021, FR-022, FR-023, FR-024)
    // =========================================================================

    /// @brief Process a single mono sample (FR-021)
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Real-time safe (noexcept, no allocations)
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return input;
        }

        // Handle NaN/Inf input
        if (!std::isfinite(input)) {
            filterL_.reset();
            return 0.0f;
        }

        // Update LFO
        lfoValue_ = lfo_.process();

        // Update envelope follower (for both envelope source and audio trigger)
        float envelopeValue = envelopeFollower_.processSample(input);

        // Check for trigger based on mode
        bool triggered = checkTrigger(input, envelopeValue);

        // Sample new values if triggered
        if (triggered) {
            onTrigger();
        }

        // Calculate final filter parameters with slew-limited modulation
        float finalCutoff = calculateFinalCutoff();
        float finalQ = calculateFinalQ();

        // Update filter
        filterL_.setCutoff(finalCutoff);
        filterL_.setResonance(finalQ);

        return filterL_.process(input);
    }

    /// @brief Process a stereo sample pair in-place (FR-022)
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    /// @note Real-time safe (noexcept, no allocations)
    void processStereo(float& left, float& right) noexcept {
        if (!prepared_) {
            return;
        }

        // Handle NaN/Inf input
        if (!std::isfinite(left) || !std::isfinite(right)) {
            filterL_.reset();
            filterR_.reset();
            left = 0.0f;
            right = 0.0f;
            return;
        }

        // Update LFO
        lfoValue_ = lfo_.process();

        // Mix L+R for trigger detection and envelope source
        float mono = (left + right) * 0.5f;
        float envelopeValue = envelopeFollower_.processSample(mono);

        // Check for trigger based on mode
        bool triggered = checkTrigger(mono, envelopeValue);

        // Sample new values if triggered
        if (triggered) {
            onTrigger();
        }

        // Calculate stereo cutoffs with pan offset
        float leftCutoff, rightCutoff;
        calculateStereoCutoffs(leftCutoff, rightCutoff);

        float finalQ = calculateFinalQ();

        // Update filters with stereo cutoffs
        filterL_.setCutoff(leftCutoff);
        filterL_.setResonance(finalQ);
        filterR_.setCutoff(rightCutoff);
        filterR_.setResonance(finalQ);

        // Process both channels
        left = filterL_.process(left);
        right = filterR_.process(right);
    }

    /// @brief Process a block of mono samples in-place (FR-023)
    /// @param buffer Audio samples (modified in-place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocations)
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || buffer == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    /// @brief Process a block of stereo samples in-place
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocations)
    void processBlockStereo(float* left, float* right, size_t numSamples) noexcept {
        if (!prepared_ || left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            processStereo(left[i], right[i]);
        }
    }

    // =========================================================================
    // Trigger Configuration (FR-001 to FR-005)
    // =========================================================================

    /// @brief Set trigger source mode (FR-001)
    /// @param source Trigger mode (Clock, Audio, Random)
    /// @note Mode switch takes effect at first sample of next buffer (sample-accurate)
    void setTriggerSource(TriggerSource source) noexcept {
        triggerSource_ = source;
    }

    /// @brief Get current trigger source mode
    [[nodiscard]] TriggerSource getTriggerSource() const noexcept {
        return triggerSource_;
    }

    /// @brief Set hold time in milliseconds (FR-002)
    /// @param ms Hold time [0.1, 10000]
    void setHoldTime(float ms) noexcept {
        holdTimeMs_ = std::clamp(ms, kMinHoldTimeMs, kMaxHoldTimeMs);
        if (prepared_) {
            holdTimeSamples_ = holdTimeMs_ * sampleRate_ * 0.001;
            // Don't reset trigger counter to maintain timing continuity
        }
    }

    /// @brief Get current hold time in milliseconds
    [[nodiscard]] float getHoldTime() const noexcept {
        return holdTimeMs_;
    }

    /// @brief Set audio trigger threshold (FR-004)
    /// @param threshold Normalized threshold [0, 1]
    /// @note Uses EnvelopeFollower in DetectionMode::Peak with attack=0.1ms, release=50ms
    void setTransientThreshold(float threshold) noexcept {
        transientThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
    }

    /// @brief Get audio trigger threshold
    [[nodiscard]] float getTransientThreshold() const noexcept {
        return transientThreshold_;
    }

    /// @brief Set random trigger probability (FR-005)
    /// @param probability Trigger probability [0, 1]
    void setTriggerProbability(float probability) noexcept {
        triggerProbability_ = std::clamp(probability, 0.0f, 1.0f);
    }

    /// @brief Get random trigger probability
    [[nodiscard]] float getTriggerProbability() const noexcept {
        return triggerProbability_;
    }

    // =========================================================================
    // Sample Source Configuration (FR-006 to FR-010)
    // =========================================================================

    /// @brief Set LFO rate for LFO source (FR-007)
    /// @param hz Rate in Hz [0.01, 20]
    void setLFORate(float hz) noexcept {
        lfoRateHz_ = std::clamp(hz, kMinLFORate, kMaxLFORate);
        if (prepared_) {
            lfo_.setFrequency(lfoRateHz_);
        }
    }

    /// @brief Get LFO rate
    [[nodiscard]] float getLFORate() const noexcept {
        return lfoRateHz_;
    }

    /// @brief Set external value for External source (FR-010)
    /// @param value Normalized value [0, 1]
    void setExternalValue(float value) noexcept {
        externalValue_ = std::clamp(value, 0.0f, 1.0f);
    }

    /// @brief Get external value
    [[nodiscard]] float getExternalValue() const noexcept {
        return externalValue_;
    }

    // =========================================================================
    // Cutoff Parameter Configuration (FR-011, FR-014)
    // =========================================================================

    /// @brief Enable/disable cutoff sampling (FR-014)
    void setCutoffSamplingEnabled(bool enabled) noexcept {
        cutoffSamplingEnabled_ = enabled;
    }

    /// @brief Check if cutoff sampling is enabled
    [[nodiscard]] bool isCutoffSamplingEnabled() const noexcept {
        return cutoffSamplingEnabled_;
    }

    /// @brief Set cutoff sample source (FR-014)
    void setCutoffSource(SampleSource source) noexcept {
        cutoffSource_ = source;
    }

    /// @brief Get cutoff sample source
    [[nodiscard]] SampleSource getCutoffSource() const noexcept {
        return cutoffSource_;
    }

    /// @brief Set cutoff modulation range in octaves (FR-011)
    /// @param octaves Range [0, 8]
    void setCutoffOctaveRange(float octaves) noexcept {
        cutoffOctaveRange_ = std::clamp(octaves, kMinCutoffOctaves, kMaxCutoffOctaves);
    }

    /// @brief Get cutoff modulation range
    [[nodiscard]] float getCutoffOctaveRange() const noexcept {
        return cutoffOctaveRange_;
    }

    // =========================================================================
    // Q Parameter Configuration (FR-012, FR-014)
    // =========================================================================

    /// @brief Enable/disable Q sampling (FR-014)
    void setQSamplingEnabled(bool enabled) noexcept {
        qSamplingEnabled_ = enabled;
    }

    /// @brief Check if Q sampling is enabled
    [[nodiscard]] bool isQSamplingEnabled() const noexcept {
        return qSamplingEnabled_;
    }

    /// @brief Set Q sample source (FR-014)
    void setQSource(SampleSource source) noexcept {
        qSource_ = source;
    }

    /// @brief Get Q sample source
    [[nodiscard]] SampleSource getQSource() const noexcept {
        return qSource_;
    }

    /// @brief Set Q modulation range (FR-012)
    /// @param range Normalized range [0, 1]
    void setQRange(float range) noexcept {
        qRange_ = std::clamp(range, kMinQRange, kMaxQRange);
    }

    /// @brief Get Q modulation range
    [[nodiscard]] float getQRange() const noexcept {
        return qRange_;
    }

    // =========================================================================
    // Pan Parameter Configuration (FR-013, FR-014)
    // =========================================================================

    /// @brief Enable/disable pan sampling (FR-014)
    void setPanSamplingEnabled(bool enabled) noexcept {
        panSamplingEnabled_ = enabled;
    }

    /// @brief Check if pan sampling is enabled
    [[nodiscard]] bool isPanSamplingEnabled() const noexcept {
        return panSamplingEnabled_;
    }

    /// @brief Set pan sample source (FR-014)
    void setPanSource(SampleSource source) noexcept {
        panSource_ = source;
    }

    /// @brief Get pan sample source
    [[nodiscard]] SampleSource getPanSource() const noexcept {
        return panSource_;
    }

    /// @brief Set pan modulation range in octaves (FR-013)
    /// @param octaves Octave offset for L/R cutoff [0, 4]
    /// @note Pan formula: L = base * pow(2, -pan * octaves), R = base * pow(2, +pan * octaves)
    void setPanOctaveRange(float octaves) noexcept {
        panOctaveRange_ = std::clamp(octaves, kMinPanOctaveRange, kMaxPanOctaveRange);
    }

    /// @brief Get pan modulation range
    [[nodiscard]] float getPanOctaveRange() const noexcept {
        return panOctaveRange_;
    }

    // =========================================================================
    // Slew Configuration (FR-015, FR-016)
    // =========================================================================

    /// @brief Set slew time for sampled value transitions (FR-015)
    /// @param ms Slew time [0, 500]
    /// @note Slew applies ONLY to sampled modulation values; base parameter changes are instant
    void setSlewTime(float ms) noexcept {
        slewTimeMs_ = std::clamp(ms, kMinSlewTimeMs, kMaxSlewTimeMs);
        if (prepared_) {
            const float sampleRateF = static_cast<float>(sampleRate_);
            cutoffSmoother_.configure(slewTimeMs_, sampleRateF);
            qSmoother_.configure(slewTimeMs_, sampleRateF);
            panSmoother_.configure(slewTimeMs_, sampleRateF);
        }
    }

    /// @brief Get slew time
    [[nodiscard]] float getSlewTime() const noexcept {
        return slewTimeMs_;
    }

    // =========================================================================
    // Filter Configuration (FR-017 to FR-020)
    // =========================================================================

    /// @brief Set filter mode (FR-018)
    /// @param mode Filter type (Lowpass, Highpass, Bandpass, Notch)
    void setFilterMode(SVFMode mode) noexcept {
        filterMode_ = mode;
        if (prepared_) {
            filterL_.setMode(mode);
            filterR_.setMode(mode);
        }
    }

    /// @brief Get filter mode
    [[nodiscard]] SVFMode getFilterMode() const noexcept {
        return filterMode_;
    }

    /// @brief Set base cutoff frequency (FR-019)
    /// @param hz Frequency in Hz [20, 20000]
    void setBaseCutoff(float hz) noexcept {
        float maxHz = prepared_ ? maxCutoff_ : kMaxBaseCutoff;
        baseCutoffHz_ = std::clamp(hz, kMinBaseCutoff, maxHz);
    }

    /// @brief Get base cutoff frequency
    [[nodiscard]] float getBaseCutoff() const noexcept {
        return baseCutoffHz_;
    }

    /// @brief Set base Q (resonance) (FR-020)
    /// @param q Q value [0.1, 30]
    void setBaseQ(float q) noexcept {
        baseQ_ = std::clamp(q, kMinBaseQ, kMaxBaseQ);
    }

    /// @brief Get base Q
    [[nodiscard]] float getBaseQ() const noexcept {
        return baseQ_;
    }

    // =========================================================================
    // Reproducibility (FR-027)
    // =========================================================================

    /// @brief Set random seed for deterministic behavior (FR-027)
    /// @param seed Seed value (non-zero)
    void setSeed(uint32_t seed) noexcept {
        seed_ = (seed != 0) ? seed : 1;
        rng_.seed(seed_);
    }

    /// @brief Get current seed
    [[nodiscard]] uint32_t getSeed() const noexcept {
        return seed_;
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get configured sample rate
    [[nodiscard]] double sampleRate() const noexcept {
        return sampleRate_;
    }

private:
    // =========================================================================
    // Internal: Trigger Logic
    // =========================================================================

    /// @brief Check if a trigger should occur based on current mode
    /// @param input Current input sample (for audio mode)
    /// @param envelopeValue Current envelope value
    /// @return true if trigger should occur
    [[nodiscard]] bool checkTrigger(float input, float envelopeValue) noexcept {
        (void)input;  // Used for audio mode via envelope

        switch (triggerSource_) {
            case TriggerSource::Clock:
                return clockTrigger();

            case TriggerSource::Audio:
                return audioTrigger(envelopeValue);

            case TriggerSource::Random:
                return randomTrigger();
        }

        return false;
    }

    /// @brief Clock trigger: decrement counter, trigger when <= 0, reset (FR-003)
    [[nodiscard]] bool clockTrigger() noexcept {
        samplesUntilTrigger_ -= 1.0;
        if (samplesUntilTrigger_ <= 0.0) {
            samplesUntilTrigger_ += holdTimeSamples_;
            return true;
        }
        return false;
    }

    /// @brief Audio trigger: detect transient crossing threshold (FR-004)
    [[nodiscard]] bool audioTrigger(float envelopeValue) noexcept {
        // If still in hold period after transient, count down and ignore
        if (holdingAfterTransient_) {
            transientHoldSamples_ -= 1.0;
            if (transientHoldSamples_ <= 0.0) {
                holdingAfterTransient_ = false;
            }
            previousEnvelope_ = envelopeValue;
            return false;
        }

        // Detect rising edge crossing threshold
        bool triggered = false;
        if (envelopeValue >= transientThreshold_ && previousEnvelope_ < transientThreshold_) {
            triggered = true;
            // Start hold period to ignore subsequent transients
            holdingAfterTransient_ = true;
            transientHoldSamples_ = holdTimeSamples_;
        }

        previousEnvelope_ = envelopeValue;
        return triggered;
    }

    /// @brief Random trigger: same timing as clock but evaluate probability (FR-005)
    [[nodiscard]] bool randomTrigger() noexcept {
        samplesUntilTrigger_ -= 1.0;
        if (samplesUntilTrigger_ <= 0.0) {
            samplesUntilTrigger_ += holdTimeSamples_;

            // Evaluate probability
            float randVal = rng_.nextUnipolar();  // [0, 1]
            return randVal < triggerProbability_;
        }
        return false;
    }

    /// @brief Called when a trigger occurs - sample new values
    void onTrigger() noexcept {
        // Sample cutoff modulation if enabled
        if (cutoffSamplingEnabled_) {
            cutoffHeldValue_ = getSampleValue(cutoffSource_);
            cutoffSmoother_.setTarget(cutoffHeldValue_);
        }

        // Sample Q modulation if enabled
        if (qSamplingEnabled_) {
            qHeldValue_ = getSampleValue(qSource_);
            qSmoother_.setTarget(qHeldValue_);
        }

        // Sample pan modulation if enabled
        if (panSamplingEnabled_) {
            panHeldValue_ = getSampleValue(panSource_);
            panSmoother_.setTarget(panHeldValue_);
        }
    }

    /// @brief Get sample value from specified source
    /// @param source The sample source to read from
    /// @return Value in [-1, 1]
    [[nodiscard]] float getSampleValue(SampleSource source) noexcept {
        switch (source) {
            case SampleSource::LFO:
                return lfoValue_;  // Already in [-1, 1]

            case SampleSource::Random:
                return rng_.nextFloat();  // Returns [-1, 1]

            case SampleSource::Envelope:
                // Envelope is [0, 1], convert to [-1, 1] via (value * 2) - 1
                return envelopeFollower_.getCurrentValue() * 2.0f - 1.0f;

            case SampleSource::External:
                // External is [0, 1], convert to [-1, 1] via (value * 2) - 1
                return externalValue_ * 2.0f - 1.0f;
        }

        return 0.0f;
    }

    // =========================================================================
    // Internal: Parameter Calculation
    // =========================================================================

    /// @brief Calculate final cutoff with modulation and slew
    /// @return Final cutoff frequency in Hz
    [[nodiscard]] float calculateFinalCutoff() noexcept {
        if (!cutoffSamplingEnabled_) {
            return baseCutoffHz_;
        }

        // Get smoothed modulation value
        float smoothedMod = cutoffSmoother_.process();

        // Octave-based modulation: base * pow(2, mod * octaveRange)
        float octaveOffset = smoothedMod * cutoffOctaveRange_;
        float modulatedCutoff = baseCutoffHz_ * std::pow(2.0f, octaveOffset);

        // Clamp to valid range
        return std::clamp(modulatedCutoff, SVF::kMinCutoff, maxCutoff_);
    }

    /// @brief Calculate final Q with modulation and slew
    /// @return Final Q value
    [[nodiscard]] float calculateFinalQ() noexcept {
        if (!qSamplingEnabled_) {
            return baseQ_;
        }

        // Get smoothed modulation value
        float smoothedMod = qSmoother_.process();

        // Q modulation: baseQ + (mod * qRange * (maxQ - minQ))
        float qOffset = smoothedMod * qRange_ * (kMaxBaseQ - kMinBaseQ);
        float modulatedQ = baseQ_ + qOffset;

        // Clamp to valid range
        return std::clamp(modulatedQ, kMinBaseQ, kMaxBaseQ);
    }

    /// @brief Calculate stereo cutoffs with pan offset
    /// @param leftCutoff Output left channel cutoff
    /// @param rightCutoff Output right channel cutoff
    void calculateStereoCutoffs(float& leftCutoff, float& rightCutoff) noexcept {
        // Get base cutoff (with any cutoff modulation)
        float baseCutoff = calculateFinalCutoff();

        if (!panSamplingEnabled_) {
            leftCutoff = baseCutoff;
            rightCutoff = baseCutoff;
            return;
        }

        // Get smoothed pan value
        float smoothedPan = panSmoother_.process();

        // Pan offset formula from FR-013:
        // leftCutoff = baseCutoff * pow(2, -panValue * panOctaveRange)
        // rightCutoff = baseCutoff * pow(2, +panValue * panOctaveRange)
        float panOffset = smoothedPan * panOctaveRange_;

        leftCutoff = baseCutoff * std::pow(2.0f, -panOffset);
        rightCutoff = baseCutoff * std::pow(2.0f, panOffset);

        // Clamp both to valid range
        leftCutoff = std::clamp(leftCutoff, SVF::kMinCutoff, maxCutoff_);
        rightCutoff = std::clamp(rightCutoff, SVF::kMinCutoff, maxCutoff_);
    }

    // =========================================================================
    // Composed DSP Components
    // =========================================================================

    SVF filterL_;                    ///< Left channel filter (FR-017)
    SVF filterR_;                    ///< Right channel filter (FR-017)
    LFO lfo_;                        ///< Internal LFO source (FR-007)
    EnvelopeFollower envelopeFollower_; ///< For envelope source & audio trigger
    Xorshift32 rng_{1};              ///< Random number generator (FR-008)

    // =========================================================================
    // Parameter Smoothers (FR-016)
    // =========================================================================

    OnePoleSmoother cutoffSmoother_;  ///< Slew limiter for cutoff modulation
    OnePoleSmoother qSmoother_;       ///< Slew limiter for Q modulation
    OnePoleSmoother panSmoother_;     ///< Slew limiter for pan modulation

    // =========================================================================
    // Trigger System State
    // =========================================================================

    TriggerSource triggerSource_ = TriggerSource::Clock;
    double samplesUntilTrigger_ = 0.0;   ///< Double for precision (SC-001)
    double holdTimeSamples_ = 0.0;       ///< Hold time in samples
    float previousEnvelope_ = 0.0f;      ///< For audio trigger edge detection
    bool holdingAfterTransient_ = false; ///< Ignore transients during hold
    double transientHoldSamples_ = 0.0;  ///< Remaining samples in hold period

    // =========================================================================
    // Sample State
    // =========================================================================

    float lfoValue_ = 0.0f;          ///< Current LFO value [-1, 1]
    float cutoffHeldValue_ = 0.0f;   ///< Last sampled cutoff value [-1, 1]
    float qHeldValue_ = 0.0f;        ///< Last sampled Q value [-1, 1]
    float panHeldValue_ = 0.0f;      ///< Last sampled pan value [-1, 1]

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    float holdTimeMs_ = 100.0f;          ///< Hold time in ms
    float slewTimeMs_ = 0.0f;            ///< Slew time in ms
    float baseCutoffHz_ = 1000.0f;       ///< Base cutoff frequency
    float baseQ_ = kDefaultBaseQ;        ///< Base Q (resonance)
    SVFMode filterMode_ = SVFMode::Lowpass;  ///< Filter type
    float lfoRateHz_ = 1.0f;             ///< LFO frequency
    float transientThreshold_ = 0.5f;    ///< Audio trigger threshold [0, 1]
    float triggerProbability_ = 1.0f;    ///< Random trigger probability [0, 1]
    float externalValue_ = 0.5f;         ///< External source value [0, 1]
    uint32_t seed_ = 1;                  ///< RNG seed for reproducibility

    // Per-parameter configuration
    bool cutoffSamplingEnabled_ = false;
    SampleSource cutoffSource_ = SampleSource::LFO;
    float cutoffOctaveRange_ = 2.0f;     ///< Cutoff modulation range [0, 8] octaves

    bool qSamplingEnabled_ = false;
    SampleSource qSource_ = SampleSource::LFO;
    float qRange_ = 0.5f;                ///< Q modulation range [0, 1] normalized

    bool panSamplingEnabled_ = false;
    SampleSource panSource_ = SampleSource::LFO;
    float panOctaveRange_ = 1.0f;        ///< Pan modulation range [0, 4] octaves

    // =========================================================================
    // Lifecycle State
    // =========================================================================

    bool prepared_ = false;
    float maxCutoff_ = 20000.0f;         ///< Cached max cutoff for sample rate
};

} // namespace DSP
} // namespace Krate
