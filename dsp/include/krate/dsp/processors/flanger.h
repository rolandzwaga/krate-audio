// ==============================================================================
// Layer 2: DSP Processor - Flanger Effect
// ==============================================================================
// Classic flanger effect with modulated short delay line and feedback.
// Uses an LFO to sweep a delay time between 0.3ms and 4.0ms, creating
// characteristic comb-filter sweep effects.
//
// Features:
// - True dry/wet crossfade mix: output = (1-mix)*dry + mix*wet
// - Bipolar feedback (-1 to +1) with tanh soft-clipping
// - Stereo processing with configurable LFO phase offset
// - LFO waveform selection (Sine, Triangle)
// - Tempo sync support
// - Parameter smoothing on rate, depth, feedback, mix
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics)
// - Principle IX: Layer 2 (depends on Layer 0/1 only)
// - Principle X: DSP Processing Constraints (feedback soft-limiting)
// - Principle XII: Test-First Development
//
// Reference: specs/126-ruinae-flanger/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Flanger Class
// =============================================================================

/// @brief Classic flanger effect with modulated delay line and feedback.
///
/// The flanger creates characteristic sweeping comb-filter effects by modulating
/// a short delay line (0.3-4.0ms) with an LFO. Feedback creates resonance at
/// comb-filter frequencies.
///
/// @par Topology (true crossfade mix)
/// @code
/// Input ----+---> dry
///           |
///           +-- tanh(feedback * feedbackState) --->+
///           |                                      |
///           v                                      |
///         [Delay Line (modulated)] ---> wet ---> feedbackState
///           |                                     (for next sample)
///           v
///         [True Crossfade: (1-mix)*dry + mix*wet] ---> output
/// @endcode
///
/// @par Constitution Compliance
/// - Real-time safe: noexcept, no allocations, no locks
/// - Layer 2: Depends on Layer 0 (db_utils, note_value) and Layer 1 (DelayLine, LFO, OnePoleSmoother)
class Flanger {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinRate = 0.05f;           ///< Minimum LFO rate (Hz)
    static constexpr float kMaxRate = 5.0f;            ///< Maximum LFO rate (Hz)
    static constexpr float kDefaultRate = 0.5f;        ///< Default LFO rate (Hz)

    static constexpr float kMinDelayMs = 0.3f;         ///< Sweep floor (ms)
    static constexpr float kMaxDelayMs = 4.0f;         ///< Sweep ceiling (ms)
    static constexpr float kMaxDelayBufferMs = 10.0f;  ///< Buffer allocation (ms)

    static constexpr float kMinDepth = 0.0f;
    static constexpr float kMaxDepth = 1.0f;
    static constexpr float kDefaultDepth = 0.5f;

    static constexpr float kMinFeedback = -1.0f;
    static constexpr float kMaxFeedback = 1.0f;
    static constexpr float kDefaultFeedback = 0.0f;
    static constexpr float kFeedbackClamp = 0.98f;     ///< Internal safety clamp

    static constexpr float kMinMix = 0.0f;
    static constexpr float kMaxMix = 1.0f;
    static constexpr float kDefaultMix = 0.5f;

    static constexpr float kMinStereoSpread = 0.0f;    ///< Degrees
    static constexpr float kMaxStereoSpread = 360.0f;  ///< Degrees
    static constexpr float kDefaultStereoSpread = 90.0f;

    static constexpr float kSmoothingTimeMs = 5.0f;    ///< Parameter smoothing time

    // =========================================================================
    // Lifecycle
    // =========================================================================

    Flanger() noexcept = default;

    /// @brief Prepare for processing at the given sample rate.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Prepare delay lines with 10ms buffer
        delayL_.prepare(sampleRate_, kMaxDelayBufferMs * 0.001f);
        delayR_.prepare(sampleRate_, kMaxDelayBufferMs * 0.001f);

        // Prepare LFOs
        lfoL_.prepare(sampleRate_);
        lfoR_.prepare(sampleRate_);

        // Configure LFO parameters
        lfoL_.setWaveform(waveform_);
        lfoR_.setWaveform(waveform_);
        lfoL_.setFrequency(rate_);
        lfoR_.setFrequency(rate_);
        lfoR_.setPhaseOffset(stereoSpread_);

        // Configure tempo sync if enabled
        if (tempoSync_) {
            lfoL_.setTempoSync(true);
            lfoR_.setTempoSync(true);
            lfoL_.setTempo(static_cast<float>(tempo_));
            lfoR_.setTempo(static_cast<float>(tempo_));
            lfoL_.setNoteValue(noteValue_, noteModifier_);
            lfoR_.setNoteValue(noteValue_, noteModifier_);
        }

        // Configure smoothers
        const float sampleRateF = static_cast<float>(sampleRate_);
        rateSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        depthSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        feedbackSmoother_.configure(kSmoothingTimeMs, sampleRateF);
        mixSmoother_.configure(kSmoothingTimeMs, sampleRateF);

        // Snap smoothers to initial values
        rateSmoother_.snapTo(rate_);
        depthSmoother_.snapTo(depth_);
        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);

        prepared_ = true;
    }

    /// @brief Reset all internal state.
    void reset() noexcept {
        delayL_.reset();
        delayR_.reset();
        lfoL_.reset();
        lfoR_.reset();

        // Reapply stereo spread after LFO reset
        lfoR_.setPhaseOffset(stereoSpread_);

        // Reset feedback state
        feedbackStateL_ = 0.0f;
        feedbackStateR_ = 0.0f;

        // Snap smoothers to current targets
        rateSmoother_.snapToTarget();
        depthSmoother_.snapToTarget();
        feedbackSmoother_.snapToTarget();
        mixSmoother_.snapToTarget();
    }

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    void setRate(float rateHz) noexcept {
        rate_ = std::clamp(rateHz, kMinRate, kMaxRate);
        rateSmoother_.setTarget(rate_);
        if (!tempoSync_) {
            lfoL_.setFrequency(rate_);
            lfoR_.setFrequency(rate_);
        }
    }

    [[nodiscard]] float getRate() const noexcept {
        return rate_;
    }

    void setDepth(float depth) noexcept {
        depth_ = std::clamp(depth, kMinDepth, kMaxDepth);
        depthSmoother_.setTarget(depth_);
    }

    [[nodiscard]] float getDepth() const noexcept {
        return depth_;
    }

    void setFeedback(float feedback) noexcept {
        feedback_ = std::clamp(feedback, kMinFeedback, kMaxFeedback);
        feedbackSmoother_.setTarget(feedback_);
    }

    [[nodiscard]] float getFeedback() const noexcept {
        return feedback_;
    }

    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, kMinMix, kMaxMix);
        mixSmoother_.setTarget(mix_);
    }

    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    void setStereoSpread(float degrees) noexcept {
        // Wrap to [0, 360)
        stereoSpread_ = std::fmod(degrees, 360.0f);
        if (stereoSpread_ < 0.0f) {
            stereoSpread_ += 360.0f;
        }
        lfoR_.setPhaseOffset(stereoSpread_);
    }

    [[nodiscard]] float getStereoSpread() const noexcept {
        return stereoSpread_;
    }

    void setWaveform(Waveform wf) noexcept {
        waveform_ = wf;
        lfoL_.setWaveform(wf);
        lfoR_.setWaveform(wf);
    }

    [[nodiscard]] Waveform getWaveform() const noexcept {
        return waveform_;
    }

    // =========================================================================
    // Tempo Sync
    // =========================================================================

    void setTempoSync(bool enabled) noexcept {
        tempoSync_ = enabled;
        lfoL_.setTempoSync(enabled);
        lfoR_.setTempoSync(enabled);
        if (!enabled) {
            lfoL_.setFrequency(rate_);
            lfoR_.setFrequency(rate_);
        }
    }

    [[nodiscard]] bool isTempoSyncEnabled() const noexcept {
        return tempoSync_;
    }

    void setNoteValue(NoteValue nv, NoteModifier nm = NoteModifier::None) noexcept {
        noteValue_ = nv;
        noteModifier_ = nm;
        lfoL_.setNoteValue(nv, nm);
        lfoR_.setNoteValue(nv, nm);
    }

    void setTempo(double bpm) noexcept {
        tempo_ = bpm;
        const float bpmF = static_cast<float>(std::clamp(bpm, 1.0, 999.0));
        lfoL_.setTempo(bpmF);
        lfoR_.setTempo(bpmF);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a stereo block of audio in-place.
    void processStereo(float* left, float* right, size_t numSamples) noexcept {
        if (left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }

        // Guard: not prepared
        if (!prepared_) {
            return;
        }

        const float sampleRateF = static_cast<float>(sampleRate_);

        for (size_t i = 0; i < numSamples; ++i) {
            // Smooth all four parameters
            [[maybe_unused]] const float smoothedRate = rateSmoother_.process();
            const float smoothedDepth = depthSmoother_.process();
            const float smoothedFeedback = feedbackSmoother_.process();
            const float smoothedMix = mixSmoother_.process();

            // Update LFO rate if not tempo synced
            if (!tempoSync_) {
                lfoL_.setFrequency(smoothedRate);
                lfoR_.setFrequency(smoothedRate);
            }

            // Clamp feedback for stability
            const float clampedFeedback = std::clamp(smoothedFeedback, -kFeedbackClamp, kFeedbackClamp);

            // ---- Left channel ----
            float dryL = left[i];

            // Guard against NaN/Inf input
            if (detail::isNaN(dryL) || detail::isInf(dryL)) {
                dryL = 0.0f;
                feedbackStateL_ = 0.0f;
            }

            // Advance left LFO (bipolar [-1, +1])
            const float lfoValueL = lfoL_.process();

            // Convert bipolar to unipolar [0, 1]
            const float unipolarL = lfoValueL * 0.5f + 0.5f;

            // Calculate delay time
            const float maxDelayMs = kMinDelayMs + smoothedDepth * (kMaxDelayMs - kMinDelayMs);
            const float delayMsL = kMinDelayMs + unipolarL * (maxDelayMs - kMinDelayMs);
            const float delaySamplesL = delayMsL * sampleRateF * 0.001f;

            // Write input + feedback to delay line
            const float feedbackSignalL = std::tanh(clampedFeedback * feedbackStateL_);
            delayL_.write(dryL + feedbackSignalL);

            // Read wet signal from delay line
            const float wetL = delayL_.readLinear(delaySamplesL);

            // True crossfade mix: output = (1-mix)*dry + mix*wet
            left[i] = (1.0f - smoothedMix) * dryL + smoothedMix * wetL;

            // Store wet signal as feedback state (denormal flushed)
            feedbackStateL_ = detail::flushDenormal(wetL);

            // ---- Right channel ----
            float dryR = right[i];

            // Guard against NaN/Inf input
            if (detail::isNaN(dryR) || detail::isInf(dryR)) {
                dryR = 0.0f;
                feedbackStateR_ = 0.0f;
            }

            // Advance right LFO
            const float lfoValueR = lfoR_.process();
            const float unipolarR = lfoValueR * 0.5f + 0.5f;

            const float delayMsR = kMinDelayMs + unipolarR * (maxDelayMs - kMinDelayMs);
            const float delaySamplesR = delayMsR * sampleRateF * 0.001f;

            const float feedbackSignalR = std::tanh(clampedFeedback * feedbackStateR_);
            delayR_.write(dryR + feedbackSignalR);

            const float wetR = delayR_.readLinear(delaySamplesR);

            right[i] = (1.0f - smoothedMix) * dryR + smoothedMix * wetR;

            feedbackStateR_ = detail::flushDenormal(wetR);
        }
    }

private:
    // =========================================================================
    // State Variables
    // =========================================================================

    // Delay lines (L/R channels)
    DelayLine delayL_;
    DelayLine delayR_;

    // LFOs for modulation
    LFO lfoL_;
    LFO lfoR_;

    // Parameter smoothers
    OnePoleSmoother rateSmoother_;
    OnePoleSmoother depthSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;

    // Feedback state
    float feedbackStateL_ = 0.0f;
    float feedbackStateR_ = 0.0f;

    // Configuration
    double sampleRate_ = 44100.0;
    float rate_ = kDefaultRate;
    float depth_ = kDefaultDepth;
    float feedback_ = kDefaultFeedback;
    float mix_ = kDefaultMix;
    float stereoSpread_ = kDefaultStereoSpread;
    Waveform waveform_ = Waveform::Triangle;

    // Tempo sync
    bool tempoSync_ = false;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    double tempo_ = 120.0;

    // Prepared flag
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
