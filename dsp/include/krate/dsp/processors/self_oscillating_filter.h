// ==============================================================================
// Layer 2: DSP Processor
// self_oscillating_filter.h - Melodically Playable Self-Oscillating Filter
// ==============================================================================
// A wrapper around LadderFilter that enables melodic sine-wave generation
// from filter resonance. Provides MIDI note control (noteOn/noteOff),
// configurable attack/release envelope, glide/portamento, external input
// mixing for filter ping effects, and wave shaping via tanh saturation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (C++20, RAII, value semantics)
// - Principle IX: Layer 2 (depends on Layers 0 and 1)
// - Principle X: DSP Constraints (DC blocking, per-sample updates)
// - Principle XII: Test-First Development
// - Principle XIV: ODR Prevention (unique class name verified)
//
// Real-Time Safety Guarantees (FR-022):
// - All processing methods (process, processBlock) are noexcept
// - Zero heap allocations in the audio processing path
// - All internal components (LadderFilter, DCBlocker2, smoothers) are noexcept
// - No locks, mutexes, or blocking operations in process path
// - No exception handling in process path
// - All buffers are pre-allocated during prepare()
//
// Threading Model (FR-023):
// Parameter setters use internal smoothers for click-free transitions.
// The VST3 host handles thread-safe parameter communication via the
// processParameterChanges() mechanism. Direct concurrent setter calls
// during process() are NOT supported (VST3 convention).
//
// The "safe to call during processing" requirement is fulfilled through
// parameter smoothing (OnePoleSmoother, LinearRamp), not thread-safety
// primitives. Each parameter setter updates a target value that the
// smoother interpolates toward on each process() call.
//
// Feature: 088-self-osc-filter
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/ladder_filter.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace Krate {
namespace DSP {

/// @brief Melodically playable self-oscillating filter
///
/// Wraps LadderFilter for melodic sine-wave generation from filter resonance.
/// Provides MIDI note control, configurable envelope, glide, external input
/// mixing, and wave shaping.
///
/// @par Layer
/// Layer 2 - DSP Processor
///
/// @par Thread Safety
/// NOT thread-safe. Must be used from a single thread (audio thread).
/// Parameter setters are safe to call between process() blocks (VST3 model).
/// All processing methods are noexcept and real-time safe after prepare().
///
/// @par Dependencies
/// - LadderFilter (primitives/ladder_filter.h)
/// - DCBlocker2 (primitives/dc_blocker.h)
/// - OnePoleSmoother, LinearRamp (primitives/smoother.h)
/// - midiNoteToFrequency, velocityToGain (core/midi_utils.h)
/// - FastMath::fastTanh (core/fast_math.h)
/// - dbToGain (core/db_utils.h)
///
/// @example
/// @code
/// SelfOscillatingFilter filter;
/// filter.prepare(44100.0, 512);
/// filter.setFrequency(440.0f);
/// filter.setResonance(1.0f);  // Full self-oscillation
///
/// // Process as pure oscillator
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.process(0.0f);
/// }
///
/// // Or use MIDI control
/// filter.noteOn(60, 127);  // C4, full velocity
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = filter.process(0.0f);
/// }
/// filter.noteOff();
/// @endcode
class SelfOscillatingFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum oscillation frequency (Hz)
    static constexpr float kMinFrequency = 20.0f;

    /// Maximum oscillation frequency (Hz)
    static constexpr float kMaxFrequency = 20000.0f;

    /// Minimum attack time (ms)
    static constexpr float kMinAttackMs = 0.0f;

    /// Maximum attack time (ms)
    static constexpr float kMaxAttackMs = 20.0f;

    /// Minimum release time (ms)
    static constexpr float kMinReleaseMs = 10.0f;

    /// Maximum release time (ms)
    static constexpr float kMaxReleaseMs = 2000.0f;

    /// Minimum glide time (ms)
    static constexpr float kMinGlideMs = 0.0f;

    /// Maximum glide time (ms)
    static constexpr float kMaxGlideMs = 5000.0f;

    /// Minimum output level (dB)
    static constexpr float kMinLevelDb = -60.0f;

    /// Maximum output level (dB)
    static constexpr float kMaxLevelDb = 6.0f;

    /// Internal resonance value for reliable self-oscillation
    /// LadderFilter resonance max is 4.0, self-oscillation at ~3.9
    static constexpr float kSelfOscResonance = 3.95f;

    /// Envelope release completion threshold (dB)
    static constexpr float kReleaseThresholdDb = -60.0f;

    /// Default parameter smoothing time (ms)
    static constexpr float kDefaultSmoothingMs = 5.0f;

    /// Reference sample rate for pitch compensation calibration
    static constexpr float kPitchCompensationRefRate = 44100.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    SelfOscillatingFilter() noexcept = default;

    /// @brief Prepare filter for processing
    ///
    /// Must be called before any processing. Configures all internal components.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size for processBlock()
    ///
    /// @note NOT real-time safe (may configure internal components)
    void prepare(double sampleRate, int maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Configure filter
        filter_.prepare(sampleRate, maxBlockSize);
        filter_.setModel(LadderModel::Nonlinear);  // For authentic oscillation
        filter_.setSlope(4);  // 24 dB/oct for best self-oscillation
        filter_.setOversamplingFactor(2);  // 2x oversampling for quality
        filter_.setResonance(mapResonanceToFilter(resonance_));
        filter_.setCutoff(frequency_);
        needsKick_ = true;  // Will kick-start oscillation on first process

        // Configure DC blocker
        dcBlocker_.prepare(sampleRate, 10.0f);  // 10 Hz cutoff for DC removal

        // Configure frequency ramp for glide
        frequencyRamp_.configure(glideMs_, static_cast<float>(sampleRate));
        frequencyRamp_.snapTo(frequency_);

        // Configure smoothers
        levelSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        levelSmoother_.snapTo(dbToGain(levelDb_));

        mixSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        mixSmoother_.snapTo(externalMix_);

        // Configure envelope smoothers
        float attackTime = (attackMs_ > 0.0f) ? attackMs_ : 0.1f;
        attackEnvelope_.configure(attackTime, static_cast<float>(sampleRate));
        attackEnvelope_.snapTo(0.0f);

        releaseEnvelope_.configure(releaseMs_, static_cast<float>(sampleRate));
        releaseEnvelope_.snapTo(0.0f);

        prepared_ = true;
    }

    /// @brief Reset filter state
    ///
    /// Clears all internal filter state while preserving configuration.
    /// Use when starting a new audio stream or after silence.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        filter_.reset();
        dcBlocker_.reset();
        frequencyRamp_.reset();
        levelSmoother_.reset();
        mixSmoother_.reset();
        attackEnvelope_.reset();
        releaseEnvelope_.reset();

        // Reset envelope state
        envelopeState_ = EnvelopeState::Idle;
        currentEnvelopeLevel_ = 0.0f;
        hasActiveNote_ = false;
        needsKick_ = true;

        // Restore smoother targets (preserve configuration)
        frequencyRamp_.snapTo(frequency_);
        levelSmoother_.snapTo(dbToGain(levelDb_));
        mixSmoother_.snapTo(externalMix_);
    }

    // =========================================================================
    // Processing (FR-021, FR-022)
    // =========================================================================

    /// @brief Process a single sample
    ///
    /// @param externalInput External audio input (0.0 for pure oscillation)
    /// @return Processed output sample
    ///
    /// @pre prepare() must have been called
    ///
    /// @note Real-time safe, noexcept, zero allocations
    [[nodiscard]] float process(float externalInput) noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // FR-004: Update filter cutoff every sample for pitch accuracy
        // Apply pitch compensation to achieve accurate oscillation frequency
        float targetOscFreq = frequencyRamp_.process();
        float compensatedCutoff = calculateCompensatedCutoff(targetOscFreq);
        filter_.setCutoff(compensatedCutoff);

        // Get current mix value
        float mix = mixSmoother_.process();

        // Mix external input with zero (for self-oscillation)
        // When mix = 0, filter receives minimal input allowing self-oscillation
        // When mix = 1, filter receives full external input
        float filterInput = externalInput * mix;

        // Kick-start oscillation with impulse if needed
        // This is required because self-oscillation needs some initial energy
        if (needsKick_ && resonance_ > 0.9f) {
            filterInput += 0.01f;  // Larger kick for reliable startup
            needsKick_ = false;
        }

        // Process through ladder filter
        float output = filter_.process(filterInput);

        // FR-019: DC blocking after filter
        output = dcBlocker_.process(output);

        // Apply wave shaping if enabled
        output = applyWaveShaping(output);

        // Process envelope (only if MIDI note control is being used)
        // When envelope is Idle but not using MIDI, allow direct oscillation
        if (envelopeState_ != EnvelopeState::Idle || hasActiveNote_) {
            float envelopeLevel = processEnvelope();
            output *= envelopeLevel;
        }
        // Otherwise, when no note has ever been triggered,
        // the filter operates in "direct" mode without envelope

        // Apply output level (FR-016, FR-017)
        float levelGain = levelSmoother_.process();
        output *= levelGain;

        return output;
    }

    /// @brief Process a block of samples
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @pre prepare() must have been called
    ///
    /// @note Real-time safe, noexcept, zero allocations
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || buffer == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // MIDI Control (FR-005, FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Trigger a note (MIDI noteOn)
    ///
    /// Sets frequency from MIDI note and velocity, starts attack envelope.
    ///
    /// @param midiNote MIDI note number (0-127)
    /// @param velocity MIDI velocity (0-127, 0 treated as noteOff)
    ///
    /// @note FR-008: velocity 0 treated as noteOff
    /// @note FR-008b: retriggering restarts attack from current level
    void noteOn(int midiNote, int velocity) noexcept {
        // FR-008: velocity 0 treated as noteOff
        if (velocity <= 0) {
            noteOff();
            return;
        }

        // Clamp to valid ranges
        midiNote = std::clamp(midiNote, kMinMidiNote, kMaxMidiNote);
        velocity = std::clamp(velocity, 1, kMaxMidiVelocity);

        // Convert MIDI note to frequency
        float freq = midiNoteToFrequency(midiNote);

        // Clamp frequency to valid range
        float maxFreq = std::min(kMaxFrequency, static_cast<float>(sampleRate_ * 0.45));
        freq = std::clamp(freq, kMinFrequency, maxFreq);

        // Convert velocity to gain (FR-007)
        targetVelocityGain_ = velocityToGain(velocity);

        // Configure frequency ramp for glide (FR-009, FR-010, FR-011)
        if (glideMs_ > 0.0f) {
            frequencyRamp_.configure(glideMs_, static_cast<float>(sampleRate_));
            frequencyRamp_.setTarget(freq);
        } else {
            // FR-011: glide 0 ms = immediate change
            frequencyRamp_.snapTo(freq);
        }

        // Configure attack envelope
        float attackTime = (attackMs_ > 0.0f) ? attackMs_ : 0.1f;
        attackEnvelope_.configure(attackTime, static_cast<float>(sampleRate_));

        // FR-008b: If retriggering, restart from current level
        if (envelopeState_ != EnvelopeState::Idle) {
            attackEnvelope_.snapTo(currentEnvelopeLevel_);
        } else {
            attackEnvelope_.snapTo(0.0f);
        }
        attackEnvelope_.setTarget(targetVelocityGain_);

        // Mark that MIDI control is being used
        hasActiveNote_ = true;

        // Transition to Attack state
        envelopeState_ = EnvelopeState::Attack;
    }

    /// @brief Release the current note (MIDI noteOff)
    ///
    /// Initiates exponential decay of oscillation amplitude.
    ///
    /// @note FR-006: natural decay, not instant cutoff
    void noteOff() noexcept {
        if (envelopeState_ == EnvelopeState::Idle) {
            return;
        }

        // Configure release envelope starting from current level
        releaseEnvelope_.configure(releaseMs_, static_cast<float>(sampleRate_));
        releaseEnvelope_.snapTo(currentEnvelopeLevel_);
        releaseEnvelope_.setTarget(0.0f);

        // Transition to Release state
        envelopeState_ = EnvelopeState::Release;
    }

    // =========================================================================
    // Parameter Setters (FR-023: safe during processing via smoothing)
    // =========================================================================

    /// @brief Set oscillation frequency
    ///
    /// @param hz Frequency in Hz (clamped to valid range)
    void setFrequency(float hz) noexcept {
        float maxFreq = std::min(kMaxFrequency, static_cast<float>(sampleRate_ * 0.45));
        float newFreq = std::clamp(hz, kMinFrequency, maxFreq);

        // If frequency changes significantly, kick-start oscillation
        if (resonance_ > 0.9f && std::abs(newFreq - frequency_) > 10.0f) {
            needsKick_ = true;
        }

        frequency_ = newFreq;
        frequencyRamp_.setTarget(frequency_);
    }

    /// @brief Set resonance amount (normalized)
    ///
    /// Values above ~0.95 enable self-oscillation.
    ///
    /// @param amount Resonance (0.0 to 1.0)
    void setResonance(float amount) noexcept {
        float oldResonance = resonance_;
        resonance_ = std::clamp(amount, 0.0f, 1.0f);
        filter_.setResonance(mapResonanceToFilter(resonance_));

        // Need kick when resonance goes above oscillation threshold
        if (resonance_ > 0.9f && oldResonance <= 0.9f) {
            needsKick_ = true;
        }
    }

    /// @brief Set glide/portamento time
    ///
    /// @param ms Glide time in milliseconds (0-5000)
    void setGlide(float ms) noexcept {
        glideMs_ = std::clamp(ms, kMinGlideMs, kMaxGlideMs);
        if (prepared_) {
            frequencyRamp_.configure(glideMs_, static_cast<float>(sampleRate_));
        }
    }

    /// @brief Set attack time
    ///
    /// @param ms Attack time in milliseconds (0-20)
    void setAttack(float ms) noexcept {
        attackMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
        if (prepared_) {
            float attackTime = (attackMs_ > 0.0f) ? attackMs_ : 0.1f;
            attackEnvelope_.configure(attackTime, static_cast<float>(sampleRate_));
        }
    }

    /// @brief Set release time
    ///
    /// @param ms Release time in milliseconds (10-2000)
    void setRelease(float ms) noexcept {
        releaseMs_ = std::clamp(ms, kMinReleaseMs, kMaxReleaseMs);
        if (prepared_) {
            releaseEnvelope_.configure(releaseMs_, static_cast<float>(sampleRate_));
        }
    }

    /// @brief Set external input mix
    ///
    /// @param mix Mix amount (0.0 = oscillation only, 1.0 = external only)
    void setExternalMix(float mix) noexcept {
        externalMix_ = std::clamp(mix, 0.0f, 1.0f);
        mixSmoother_.setTarget(externalMix_);
    }

    /// @brief Set wave shaping amount
    ///
    /// @param amount Shape amount (0.0 = clean, 1.0 = saturated)
    void setWaveShape(float amount) noexcept {
        waveShapeAmount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Set output level
    ///
    /// @param dB Level in decibels (-60 to +6)
    void setOscillationLevel(float dB) noexcept {
        levelDb_ = std::clamp(dB, kMinLevelDb, kMaxLevelDb);
        levelSmoother_.setTarget(dbToGain(levelDb_));
    }

    // =========================================================================
    // Getters
    // =========================================================================

    /// @brief Get current frequency setting
    [[nodiscard]] float getFrequency() const noexcept { return frequency_; }

    /// @brief Get current resonance setting (normalized)
    [[nodiscard]] float getResonance() const noexcept { return resonance_; }

    /// @brief Get current glide time
    [[nodiscard]] float getGlide() const noexcept { return glideMs_; }

    /// @brief Get current attack time
    [[nodiscard]] float getAttack() const noexcept { return attackMs_; }

    /// @brief Get current release time
    [[nodiscard]] float getRelease() const noexcept { return releaseMs_; }

    /// @brief Get current external mix setting
    [[nodiscard]] float getExternalMix() const noexcept { return externalMix_; }

    /// @brief Get current wave shape amount
    [[nodiscard]] float getWaveShape() const noexcept { return waveShapeAmount_; }

    /// @brief Get current output level
    [[nodiscard]] float getOscillationLevel() const noexcept { return levelDb_; }

    /// @brief Check if oscillating (envelope not idle)
    [[nodiscard]] bool isOscillating() const noexcept {
        return envelopeState_ != EnvelopeState::Idle;
    }

private:
    // =========================================================================
    // Envelope States
    // =========================================================================

    enum class EnvelopeState : uint8_t {
        Idle,     ///< No note active, envelope at zero
        Attack,   ///< Note triggered, ramping up to target
        Sustain,  ///< At target level, held until noteOff
        Release   ///< noteOff received, ramping down to zero
    };

    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Process envelope state machine
    /// @return Current envelope level
    [[nodiscard]] float processEnvelope() noexcept {
        switch (envelopeState_) {
            case EnvelopeState::Idle:
                currentEnvelopeLevel_ = 0.0f;
                return 0.0f;

            case EnvelopeState::Attack: {
                currentEnvelopeLevel_ = attackEnvelope_.process();
                // Check if attack is complete (>= 99% of target)
                if (currentEnvelopeLevel_ >= targetVelocityGain_ * 0.99f) {
                    currentEnvelopeLevel_ = targetVelocityGain_;
                    envelopeState_ = EnvelopeState::Sustain;
                }
                return currentEnvelopeLevel_;
            }

            case EnvelopeState::Sustain:
                currentEnvelopeLevel_ = targetVelocityGain_;
                return currentEnvelopeLevel_;

            case EnvelopeState::Release: {
                currentEnvelopeLevel_ = releaseEnvelope_.process();
                // Check if release is complete (< -60dB threshold)
                float threshold = dbToGain(kReleaseThresholdDb);
                if (currentEnvelopeLevel_ < threshold) {
                    currentEnvelopeLevel_ = 0.0f;
                    envelopeState_ = EnvelopeState::Idle;
                }
                return currentEnvelopeLevel_;
            }
        }
        return 0.0f;  // Should never reach here
    }

    /// @brief Apply wave shaping (soft saturation)
    /// @param input Input sample
    /// @return Shaped output
    [[nodiscard]] float applyWaveShaping(float input) const noexcept {
        if (waveShapeAmount_ <= 0.0f) {
            return input;
        }

        // FR-015: Map amount (0-1) to gain (1x-3x)
        float gain = 1.0f + waveShapeAmount_ * 2.0f;
        return FastMath::fastTanh(input * gain);
    }

    /// @brief Calculate compensated cutoff for pitch-accurate oscillation
    ///
    /// The ladder filter self-oscillation frequency is lower than the cutoff
    /// frequency due to phase shift through the 4 stages. The offset varies
    /// non-linearly with frequency, so we use a piecewise linear lookup table
    /// calibrated at 44100 Hz for best accuracy across the musical range.
    ///
    /// @param targetOscFreq Desired oscillation frequency in Hz
    /// @return Compensated cutoff frequency to pass to LadderFilter
    [[nodiscard]] float calculateCompensatedCutoff(float targetOscFreq) const noexcept {
        // Compensation lookup table (frequency Hz, compensation Hz)
        // Empirically calibrated at 44100 Hz sample rate for +/- 10 cents accuracy
        // The relationship is non-linear due to ladder filter phase characteristics
        static constexpr std::array<std::pair<float, float>, 6> kCompTable = {{
            {200.0f, 258.0f},   // Low frequencies: ~258 Hz comp
            {330.0f, 254.0f},   // E4 region: tuned for accuracy
            {440.0f, 256.0f},   // A4: ~256 Hz comp
            {880.0f, 266.0f},   // A5: peak compensation ~266 Hz
            {1760.0f, 240.0f},  // A6: ~240 Hz comp
            {3500.0f, 220.0f}   // High: ~220 Hz comp
        }};

        // Scale factor for sample rate
        float rateScale = static_cast<float>(sampleRate_) / kPitchCompensationRefRate;

        // Find interpolation segment
        float compensation;
        if (targetOscFreq <= kCompTable[0].first) {
            // Below table: use first value
            compensation = kCompTable[0].second;
        } else if (targetOscFreq >= kCompTable.back().first) {
            // Above table: use last value
            compensation = kCompTable.back().second;
        } else {
            // Interpolate between table entries
            for (size_t i = 0; i < kCompTable.size() - 1; ++i) {
                if (targetOscFreq >= kCompTable[i].first && targetOscFreq < kCompTable[i + 1].first) {
                    float f0 = kCompTable[i].first;
                    float f1 = kCompTable[i + 1].first;
                    float c0 = kCompTable[i].second;
                    float c1 = kCompTable[i + 1].second;

                    // Linear interpolation
                    float t = (targetOscFreq - f0) / (f1 - f0);
                    compensation = c0 + t * (c1 - c0);
                    break;
                }
            }
        }

        // Apply scaled compensation
        float compensatedCutoff = targetOscFreq + compensation * rateScale;

        // Clamp to valid range
        float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
        return std::clamp(compensatedCutoff, kMinFrequency, maxCutoff);
    }

    /// @brief Map normalized resonance to filter resonance
    ///
    /// Maps user-facing 0-1 range to LadderFilter's 0-4 range, with
    /// special handling for self-oscillation region:
    /// - 0.0 -> 0.0
    /// - 0.95 -> 3.9 (self-oscillation threshold)
    /// - 1.0 -> 3.95 (reliable self-oscillation)
    ///
    /// @param normalized User resonance (0.0 to 1.0)
    /// @return Filter resonance (0.0 to ~4.0)
    [[nodiscard]] float mapResonanceToFilter(float normalized) const noexcept {
        // Below oscillation threshold: linear mapping 0-0.95 -> 0-3.9
        if (normalized <= 0.95f) {
            return normalized * (3.9f / 0.95f);
        }
        // Above threshold: map 0.95-1.0 -> 3.9-3.95 for reliable oscillation
        return 3.9f + (normalized - 0.95f) * (0.05f / 0.05f);
    }

    // =========================================================================
    // Components
    // =========================================================================

    LadderFilter filter_;
    DCBlocker2 dcBlocker_;
    LinearRamp frequencyRamp_;
    OnePoleSmoother levelSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother attackEnvelope_;
    OnePoleSmoother releaseEnvelope_;

    // =========================================================================
    // State
    // =========================================================================

    EnvelopeState envelopeState_ = EnvelopeState::Idle;
    float currentEnvelopeLevel_ = 0.0f;
    float targetVelocityGain_ = 1.0f;
    bool hasActiveNote_ = false;  ///< True once noteOn() has been called
    bool needsKick_ = true;       ///< Needs tiny impulse to kick-start oscillation

    // =========================================================================
    // Parameters (user-facing)
    // =========================================================================

    float frequency_ = 440.0f;
    float resonance_ = 1.0f;       // Normalized 0-1
    float glideMs_ = 0.0f;
    float attackMs_ = 0.0f;
    float releaseMs_ = 500.0f;
    float externalMix_ = 0.0f;
    float waveShapeAmount_ = 0.0f;
    float levelDb_ = 0.0f;

    // =========================================================================
    // Runtime
    // =========================================================================

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

}  // namespace DSP
}  // namespace Krate
