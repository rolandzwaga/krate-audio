// ==============================================================================
// Layer 2: DSP Processor - Phaser Effect
// ==============================================================================
// Classic phaser effect with cascaded first-order allpass filters and LFO
// modulation. Phase 10 of the Filter Implementation Roadmap.
//
// Features:
// - 2-12 cascaded allpass stages (even numbers only)
// - LFO modulation with sine, triangle, square, sawtooth waveforms
// - Tempo sync support
// - Stereo processing with configurable LFO phase offset
// - Bipolar feedback (-1 to +1) with tanh soft-clipping
// - Mix-before-feedback topology
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics)
// - Principle IX: Layer 2 (depends on Layer 0/1 only)
// - Principle X: DSP Processing Constraints (feedback soft-limiting)
// - Principle XII: Test-First Development
//
// Reference: specs/079-phaser/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/allpass_1pole.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Phaser Class
// =============================================================================

/// @brief Classic phaser effect with cascaded allpass filters and LFO modulation.
///
/// The phaser creates characteristic sweeping notches by cascading first-order
/// allpass filters and modulating their break frequencies with an LFO.
/// N allpass stages produce N/2 notches in the frequency response.
///
/// @par Topology (mix-before-feedback)
/// @code
/// Input
///   |
///   +-- feedbackState * feedback (tanh soft-clipped) --->+
///   |                                                    |
///   v                                                    |
/// [Allpass Cascade (N stages)] ---> wet                  |
///   |                                                    |
///   v                                                    |
/// [Mix: dry * (1-mix) + wet * mix] ---> output           |
///   |                                                    |
///   +---------------------------------------------------+
///   (feedbackState = output for next sample)
/// @endcode
///
/// @par Constitution Compliance
/// - Real-time safe: noexcept, no allocations, no locks
/// - Layer 2: Depends on Layer 0 (math, note_value) and Layer 1 (Allpass1Pole, LFO, OnePoleSmoother)
///
/// @par Example Usage
/// @code
/// Phaser phaser;
/// phaser.prepare(44100.0);
/// phaser.setNumStages(4);
/// phaser.setRate(0.5f);        // 0.5 Hz sweep
/// phaser.setDepth(0.8f);       // 80% depth
/// phaser.setFeedback(0.5f);    // 50% feedback
/// phaser.setMix(0.5f);         // 50/50 dry/wet
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = phaser.process(input[i]);
/// }
/// @endcode
class Phaser {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum number of allpass stages (12 stages = 6 notches)
    static constexpr int kMaxStages = 12;

    /// Minimum number of stages
    static constexpr int kMinStages = 2;

    /// Default number of stages (4 stages = 2 notches)
    static constexpr int kDefaultStages = 4;

    /// Minimum LFO rate in Hz
    static constexpr float kMinRate = 0.01f;

    /// Maximum LFO rate in Hz
    static constexpr float kMaxRate = 20.0f;

    /// Default LFO rate in Hz
    static constexpr float kDefaultRate = 0.5f;

    /// Minimum depth (no modulation)
    static constexpr float kMinDepth = 0.0f;

    /// Maximum depth (full range modulation)
    static constexpr float kMaxDepth = 1.0f;

    /// Default depth
    static constexpr float kDefaultDepth = 0.5f;

    /// Minimum feedback (negative resonance)
    static constexpr float kMinFeedback = -1.0f;

    /// Maximum feedback (positive resonance)
    static constexpr float kMaxFeedback = 1.0f;

    /// Default feedback (no resonance)
    static constexpr float kDefaultFeedback = 0.0f;

    /// Minimum mix (dry only)
    static constexpr float kMinMix = 0.0f;

    /// Maximum mix (wet only)
    static constexpr float kMaxMix = 1.0f;

    /// Default mix (50/50)
    static constexpr float kDefaultMix = 0.5f;

    /// Minimum center frequency in Hz
    static constexpr float kMinCenterFreq = 100.0f;

    /// Maximum center frequency in Hz
    static constexpr float kMaxCenterFreq = 10000.0f;

    /// Default center frequency in Hz
    static constexpr float kDefaultCenterFreq = 1000.0f;

    /// Minimum stereo spread in degrees
    static constexpr float kMinStereoSpread = 0.0f;

    /// Maximum stereo spread in degrees
    static constexpr float kMaxStereoSpread = 360.0f;

    /// Default stereo spread in degrees (mono)
    static constexpr float kDefaultStereoSpread = 0.0f;

    /// Parameter smoothing time in milliseconds
    static constexpr float kSmoothingTimeMs = 5.0f;

    /// Minimum sweep frequency to prevent DC (Hz)
    static constexpr float kMinSweepFreq = 20.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor
    Phaser() noexcept = default;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Prepare the phaser for processing at a given sample rate.
    /// @param sampleRate Sample rate in Hz
    /// @post isPrepared() returns true
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Prepare all allpass filters
        for (auto& stage : stagesL_) {
            stage.prepare(sampleRate_);
        }
        for (auto& stage : stagesR_) {
            stage.prepare(sampleRate_);
        }

        // Prepare LFOs
        lfoL_.prepare(sampleRate_);
        lfoR_.prepare(sampleRate_);

        // Configure LFO parameters
        lfoL_.setFrequency(rate_);
        lfoR_.setFrequency(rate_);
        lfoL_.setWaveform(waveform_);
        lfoR_.setWaveform(waveform_);
        lfoR_.setPhaseOffset(stereoSpread_);

        // Configure tempo sync
        if (tempoSync_) {
            lfoL_.setTempoSync(true);
            lfoR_.setTempoSync(true);
            lfoL_.setTempo(tempo_);
            lfoR_.setTempo(tempo_);
            lfoL_.setNoteValue(noteValue_, noteModifier_);
            lfoR_.setNoteValue(noteValue_, noteModifier_);
        }

        // Configure smoothers
        const float sampleRateF = static_cast<float>(sampleRate_);
        rateSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        depthSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        feedbackSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        mixSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        centerFreqSmoother_.configure(kSmoothingTimeMs, sampleRateF);

        // Snap smoothers to initial values
        rateSmoother_.snapTo(rate_);
        depthSmoother_.snapTo(depth_);
        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);
        centerFreqSmoother_.snapTo(centerFrequency_);

        prepared_ = true;
    }

    /// @brief Reset all filter states and feedback.
    /// @post Filter states are cleared, but configuration is preserved
    void reset() noexcept {
        for (auto& stage : stagesL_) {
            stage.reset();
        }
        for (auto& stage : stagesR_) {
            stage.reset();
        }

        lfoL_.reset();
        lfoR_.reset();

        // Reset feedback state
        feedbackStateL_ = 0.0f;
        feedbackStateR_ = 0.0f;

        // Snap smoothers to targets
        rateSmoother_.snapToTarget();
        depthSmoother_.snapToTarget();
        feedbackSmoother_.snapToTarget();
        mixSmoother_.snapToTarget();
        centerFreqSmoother_.snapToTarget();
    }

    /// @brief Check if the phaser has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Stage Configuration
    // =========================================================================

    /// @brief Set the number of allpass stages.
    /// @param stages Number of stages, clamped to even numbers in range [2, 12]
    void setNumStages(int stages) noexcept {
        // Clamp to valid range
        stages = std::clamp(stages, kMinStages, kMaxStages);
        // Round down to nearest even number
        numStages_ = (stages / 2) * 2;
    }

    /// @brief Get the current number of stages.
    /// @return Number of active allpass stages
    [[nodiscard]] int getNumStages() const noexcept {
        return numStages_;
    }

    // =========================================================================
    // LFO Parameters
    // =========================================================================

    /// @brief Set the LFO rate (free-running mode).
    /// @param hz Rate in Hz, clamped to [0.01, 20]
    void setRate(float hz) noexcept {
        rate_ = std::clamp(hz, kMinRate, kMaxRate);
        rateSmoother_.setTarget(rate_);
        if (!tempoSync_) {
            lfoL_.setFrequency(rate_);
            lfoR_.setFrequency(rate_);
        }
    }

    /// @brief Get the current LFO rate.
    /// @return Rate in Hz
    [[nodiscard]] float getRate() const noexcept {
        return rate_;
    }

    /// @brief Set the modulation depth.
    /// @param amount Depth in range [0.0, 1.0]
    void setDepth(float amount) noexcept {
        depth_ = std::clamp(amount, kMinDepth, kMaxDepth);
        depthSmoother_.setTarget(depth_);
    }

    /// @brief Get the current modulation depth.
    /// @return Depth in range [0.0, 1.0]
    [[nodiscard]] float getDepth() const noexcept {
        return depth_;
    }

    /// @brief Set the LFO waveform.
    /// @param waveform Waveform type (Sine, Triangle, Square, Sawtooth)
    void setWaveform(Waveform waveform) noexcept {
        waveform_ = waveform;
        lfoL_.setWaveform(waveform_);
        lfoR_.setWaveform(waveform_);
    }

    /// @brief Get the current LFO waveform.
    /// @return Current waveform type
    [[nodiscard]] Waveform getWaveform() const noexcept {
        return waveform_;
    }

    // =========================================================================
    // Frequency Control
    // =========================================================================

    /// @brief Set the center frequency of the sweep range.
    /// @param hz Center frequency in Hz, clamped to [100, 10000]
    void setCenterFrequency(float hz) noexcept {
        centerFrequency_ = std::clamp(hz, kMinCenterFreq, kMaxCenterFreq);
        centerFreqSmoother_.setTarget(centerFrequency_);
    }

    /// @brief Get the current center frequency.
    /// @return Center frequency in Hz
    [[nodiscard]] float getCenterFrequency() const noexcept {
        return centerFrequency_;
    }

    // =========================================================================
    // Feedback Control
    // =========================================================================

    /// @brief Set the feedback amount.
    /// @param amount Feedback in range [-1.0, +1.0]
    void setFeedback(float amount) noexcept {
        feedback_ = std::clamp(amount, kMinFeedback, kMaxFeedback);
        feedbackSmoother_.setTarget(feedback_);
    }

    /// @brief Get the current feedback amount.
    /// @return Feedback in range [-1.0, +1.0]
    [[nodiscard]] float getFeedback() const noexcept {
        return feedback_;
    }

    // =========================================================================
    // Stereo Control
    // =========================================================================

    /// @brief Set the stereo spread (LFO phase offset between channels).
    /// @param degrees Phase offset in degrees, wrapped to [0, 360)
    void setStereoSpread(float degrees) noexcept {
        // Wrap to [0, 360)
        stereoSpread_ = std::fmod(degrees, 360.0f);
        if (stereoSpread_ < 0.0f) {
            stereoSpread_ += 360.0f;
        }
        lfoR_.setPhaseOffset(stereoSpread_);
    }

    /// @brief Get the current stereo spread.
    /// @return Stereo spread in degrees [0, 360)
    [[nodiscard]] float getStereoSpread() const noexcept {
        return stereoSpread_;
    }

    // =========================================================================
    // Mix Control
    // =========================================================================

    /// @brief Set the dry/wet mix.
    /// @param dryWet Mix in range [0.0, 1.0] (0 = dry, 1 = wet)
    void setMix(float dryWet) noexcept {
        mix_ = std::clamp(dryWet, kMinMix, kMaxMix);
        mixSmoother_.setTarget(mix_);
    }

    /// @brief Get the current mix.
    /// @return Mix in range [0.0, 1.0]
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    // =========================================================================
    // Tempo Sync Control
    // =========================================================================

    /// @brief Enable or disable tempo sync.
    /// @param enabled true to enable tempo sync
    void setTempoSync(bool enabled) noexcept {
        tempoSync_ = enabled;
        lfoL_.setTempoSync(enabled);
        lfoR_.setTempoSync(enabled);
        if (!enabled) {
            lfoL_.setFrequency(rate_);
            lfoR_.setFrequency(rate_);
        }
    }

    /// @brief Check if tempo sync is enabled.
    /// @return true if tempo sync is enabled
    [[nodiscard]] bool isTempoSyncEnabled() const noexcept {
        return tempoSync_;
    }

    /// @brief Set the note value for tempo sync.
    /// @param value Note value
    /// @param modifier Note modifier (default: None)
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        noteValue_ = value;
        noteModifier_ = modifier;
        lfoL_.setNoteValue(value, modifier);
        lfoR_.setNoteValue(value, modifier);
    }

    /// @brief Set the tempo for tempo sync.
    /// @param bpm Tempo in beats per minute
    void setTempo(float bpm) noexcept {
        tempo_ = std::clamp(bpm, kMinBPM, kMaxBPM);
        lfoL_.setTempo(tempo_);
        lfoR_.setTempo(tempo_);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample (mono).
    /// @param input Input sample
    /// @return Processed output sample
    /// @note Uses left channel LFO and filter states
    [[nodiscard]] float process(float input) noexcept {
        // Bypass if not prepared
        if (!prepared_) {
            return input;
        }

        // Handle NaN/Inf input (FR-015)
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Get smoothed parameters
        const float smoothedRate = rateSmoother_.process();
        const float smoothedDepth = depthSmoother_.process();
        const float smoothedFeedback = feedbackSmoother_.process();
        const float smoothedMix = mixSmoother_.process();
        const float smoothedCenterFreq = centerFreqSmoother_.process();

        // Update LFO rate if not tempo synced
        if (!tempoSync_) {
            lfoL_.setFrequency(smoothedRate);
        }

        // Get LFO value [-1, +1]
        const float lfoValue = lfoL_.process();

        // Calculate sweep frequency with exponential mapping (FR-002)
        const float sweepFreq = calculateSweepFrequency(lfoValue, smoothedCenterFreq, smoothedDepth);

        // Set allpass filter frequencies
        for (int i = 0; i < numStages_; ++i) {
            stagesL_[static_cast<size_t>(i)].setFrequency(sweepFreq);
        }

        // Add feedback to input (tanh soft-clipped) (FR-012)
        const float feedbackSignal = std::tanh(feedbackStateL_ * smoothedFeedback);
        float signal = input + feedbackSignal;

        // Process through allpass cascade
        for (int i = 0; i < numStages_; ++i) {
            signal = stagesL_[static_cast<size_t>(i)].process(signal);
        }

        // Flush denormals from signal (FR-016)
        signal = detail::flushDenormal(signal);

        // Mix dry and wet
        const float dry = input;
        const float wet = signal;
        const float output = dry * (1.0f - smoothedMix) + wet * smoothedMix;

        // Store feedback state (from mixed output, mix-before-feedback topology)
        feedbackStateL_ = output;
        feedbackStateL_ = detail::flushDenormal(feedbackStateL_);

        return output;
    }

    /// @brief Process a block of samples in-place (mono).
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    /// @brief Process stereo audio with LFO phase offset.
    /// @param left Left channel buffer (modified in place)
    /// @param right Right channel buffer (modified in place)
    /// @param numSamples Number of samples per channel
    void processStereo(float* left, float* right, size_t numSamples) noexcept {
        if (left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }

        // Bypass if not prepared
        if (!prepared_) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters (shared between channels)
            const float smoothedRate = rateSmoother_.process();
            const float smoothedDepth = depthSmoother_.process();
            const float smoothedFeedback = feedbackSmoother_.process();
            const float smoothedMix = mixSmoother_.process();
            const float smoothedCenterFreq = centerFreqSmoother_.process();

            // Update LFO rates if not tempo synced
            if (!tempoSync_) {
                lfoL_.setFrequency(smoothedRate);
                lfoR_.setFrequency(smoothedRate);
            }

            // Get LFO values for both channels
            const float lfoValueL = lfoL_.process();
            const float lfoValueR = lfoR_.process();

            // Calculate sweep frequencies
            const float sweepFreqL = calculateSweepFrequency(lfoValueL, smoothedCenterFreq, smoothedDepth);
            const float sweepFreqR = calculateSweepFrequency(lfoValueR, smoothedCenterFreq, smoothedDepth);

            // Process left channel
            float inputL = left[i];
            if (detail::isNaN(inputL) || detail::isInf(inputL)) {
                inputL = 0.0f;
                feedbackStateL_ = 0.0f;
            }

            for (int s = 0; s < numStages_; ++s) {
                stagesL_[static_cast<size_t>(s)].setFrequency(sweepFreqL);
            }

            const float feedbackSignalL = std::tanh(feedbackStateL_ * smoothedFeedback);
            float signalL = inputL + feedbackSignalL;

            for (int s = 0; s < numStages_; ++s) {
                signalL = stagesL_[static_cast<size_t>(s)].process(signalL);
            }
            signalL = detail::flushDenormal(signalL);

            const float outputL = inputL * (1.0f - smoothedMix) + signalL * smoothedMix;
            feedbackStateL_ = detail::flushDenormal(outputL);
            left[i] = outputL;

            // Process right channel
            float inputR = right[i];
            if (detail::isNaN(inputR) || detail::isInf(inputR)) {
                inputR = 0.0f;
                feedbackStateR_ = 0.0f;
            }

            for (int s = 0; s < numStages_; ++s) {
                stagesR_[static_cast<size_t>(s)].setFrequency(sweepFreqR);
            }

            const float feedbackSignalR = std::tanh(feedbackStateR_ * smoothedFeedback);
            float signalR = inputR + feedbackSignalR;

            for (int s = 0; s < numStages_; ++s) {
                signalR = stagesR_[static_cast<size_t>(s)].process(signalR);
            }
            signalR = detail::flushDenormal(signalR);

            const float outputR = inputR * (1.0f - smoothedMix) + signalR * smoothedMix;
            feedbackStateR_ = detail::flushDenormal(outputR);
            right[i] = outputR;
        }
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate sweep frequency from LFO value using exponential mapping.
    /// @param lfoValue LFO output in range [-1, +1]
    /// @param centerFreq Center frequency in Hz
    /// @param depth Modulation depth [0, 1]
    /// @return Sweep frequency in Hz
    [[nodiscard]] float calculateSweepFrequency(float lfoValue, float centerFreq, float depth) const noexcept {
        // If depth is 0, return center frequency (stationary notches)
        if (depth < 0.001f) {
            return centerFreq;
        }

        // Calculate min/max from center and depth (FR-007)
        float minFreq = centerFreq * (1.0f - depth);
        float maxFreq = centerFreq * (1.0f + depth);

        // Clamp min to prevent negative/zero frequencies
        minFreq = std::max(minFreq, kMinSweepFreq);

        // Ensure max > min
        if (maxFreq <= minFreq) {
            maxFreq = minFreq * 1.01f;
        }

        // Map LFO [-1, +1] to [0, 1]
        const float lfoNorm = (lfoValue + 1.0f) * 0.5f;

        // Exponential mapping for perceptually even sweep (FR-002)
        // freq = minFreq * pow(maxFreq/minFreq, lfoNorm)
        const float freqRatio = maxFreq / minFreq;
        const float sweepFreq = minFreq * std::pow(freqRatio, lfoNorm);

        // Clamp to safe range (0.99 * Nyquist)
        const float maxSafeFreq = static_cast<float>(sampleRate_) * 0.5f * 0.99f;
        return std::clamp(sweepFreq, kMinSweepFreq, maxSafeFreq);
    }

    // =========================================================================
    // State Variables
    // =========================================================================

    // Allpass filter stages (L/R channels)
    std::array<Allpass1Pole, kMaxStages> stagesL_;
    std::array<Allpass1Pole, kMaxStages> stagesR_;

    // LFOs for modulation
    LFO lfoL_;
    LFO lfoR_;

    // Parameter smoothers
    OnePoleSmoother rateSmoother_;
    OnePoleSmoother depthSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother centerFreqSmoother_;

    // Feedback state
    float feedbackStateL_ = 0.0f;
    float feedbackStateR_ = 0.0f;

    // Configuration
    double sampleRate_ = 44100.0;
    int numStages_ = kDefaultStages;
    float rate_ = kDefaultRate;
    float depth_ = kDefaultDepth;
    float feedback_ = kDefaultFeedback;
    float mix_ = kDefaultMix;
    float centerFrequency_ = kDefaultCenterFreq;
    float stereoSpread_ = kDefaultStereoSpread;
    Waveform waveform_ = Waveform::Sine;

    // Tempo sync
    bool tempoSync_ = false;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    float tempo_ = 120.0f;

    // Prepared flag
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
