// ==============================================================================
// Layer 2: DSP Processor - Chorus Effect
// ==============================================================================
// Production-quality chorus with multi-voice support, cubic Hermite interpolation,
// and configurable stereo spread. Follows the Flanger pattern for lifecycle,
// smoothing, and stereo processing.
//
// Features:
// - 1–4 voices with evenly distributed LFO phase offsets
// - Cubic Hermite (Catmull-Rom) interpolation for smooth pitch modulation
// - True dry/wet crossfade mix: output = (1-mix)*dry + mix*wet
// - Bipolar feedback (-1 to +1) with tanh soft-clipping
// - Stereo processing with configurable LFO phase offset (Juno-style 180°)
// - LFO waveform selection (Sine, Triangle, etc.)
// - Tempo sync support
// - Parameter smoothing on rate, depth, feedback, mix
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics)
// - Principle IX: Layer 2 (depends on Layer 0/1 only)
// - Principle X: DSP Processing Constraints (feedback soft-limiting)
// - Principle XII: Test-First Development
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/note_value.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Chorus Class
// =============================================================================

class Chorus {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinRate = 0.05f;
    static constexpr float kMaxRate = 10.0f;
    static constexpr float kDefaultRate = 0.5f;

    static constexpr float kMinDelayMs = 5.0f;
    static constexpr float kMaxDelayMs = 25.0f;
    static constexpr float kMaxDelayBufferMs = 40.0f;

    static constexpr float kMinDepth = 0.0f;
    static constexpr float kMaxDepth = 1.0f;
    static constexpr float kDefaultDepth = 0.5f;

    static constexpr float kMinFeedback = -1.0f;
    static constexpr float kMaxFeedback = 1.0f;
    static constexpr float kDefaultFeedback = 0.0f;
    static constexpr float kFeedbackClamp = 0.98f;

    static constexpr float kMinMix = 0.0f;
    static constexpr float kMaxMix = 1.0f;
    static constexpr float kDefaultMix = 0.5f;

    static constexpr float kMinStereoSpread = 0.0f;
    static constexpr float kMaxStereoSpread = 360.0f;
    static constexpr float kDefaultStereoSpread = 180.0f;

    static constexpr int kMinVoices = 1;
    static constexpr int kMaxVoices = 4;
    static constexpr int kDefaultVoices = 2;

    static constexpr float kSmoothingTimeMs = 5.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    Chorus() noexcept = default;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Prepare delay lines with buffer headroom
        delayL_.prepare(sampleRate_, kMaxDelayBufferMs * 0.001f);
        delayR_.prepare(sampleRate_, kMaxDelayBufferMs * 0.001f);

        // Prepare LFOs
        lfoL_.prepare(sampleRate_);
        lfoR_.prepare(sampleRate_);

        lfoL_.setWaveform(waveform_);
        lfoR_.setWaveform(waveform_);
        lfoL_.setFrequency(rate_);
        lfoR_.setFrequency(rate_);
        lfoR_.setPhaseOffset(stereoSpread_);

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

        rateSmoother_.snapTo(rate_);
        depthSmoother_.snapTo(depth_);
        feedbackSmoother_.snapTo(feedback_);
        mixSmoother_.snapTo(mix_);

        prepared_ = true;
    }

    void reset() noexcept {
        delayL_.reset();
        delayR_.reset();
        lfoL_.reset();
        lfoR_.reset();

        lfoR_.setPhaseOffset(stereoSpread_);

        feedbackStateL_ = 0.0f;
        feedbackStateR_ = 0.0f;

        rateSmoother_.snapToTarget();
        depthSmoother_.snapToTarget();
        feedbackSmoother_.snapToTarget();
        mixSmoother_.snapToTarget();
    }

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
        stereoSpread_ = std::fmod(degrees, 360.0f);
        if (stereoSpread_ < 0.0f) {
            stereoSpread_ += 360.0f;
        }
        lfoR_.setPhaseOffset(stereoSpread_);
    }

    [[nodiscard]] float getStereoSpread() const noexcept {
        return stereoSpread_;
    }

    void setVoices(int voices) noexcept {
        voices_ = std::clamp(voices, kMinVoices, kMaxVoices);
    }

    [[nodiscard]] int getVoices() const noexcept {
        return voices_;
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

    void processStereo(float* left, float* right, size_t numSamples) noexcept {
        if (left == nullptr || right == nullptr || numSamples == 0) {
            return;
        }

        if (!prepared_) {
            return;
        }

        const float sampleRateF = static_cast<float>(sampleRate_);
        const float voiceScale = 1.0f / static_cast<float>(voices_);

        for (size_t i = 0; i < numSamples; ++i) {
            // Smooth parameters
            [[maybe_unused]] const float smoothedRate = rateSmoother_.process();
            const float smoothedDepth = depthSmoother_.process();
            const float smoothedFeedback = feedbackSmoother_.process();
            const float smoothedMix = mixSmoother_.process();

            if (!tempoSync_) {
                lfoL_.setFrequency(smoothedRate);
                lfoR_.setFrequency(smoothedRate);
            }

            const float clampedFeedback = std::clamp(smoothedFeedback,
                -kFeedbackClamp, kFeedbackClamp);

            // Compute center delay and modulation depth in samples
            const float centerDelayMs = (kMinDelayMs + kMaxDelayMs) * 0.5f;
            const float sweepRangeMs = (kMaxDelayMs - kMinDelayMs) * 0.5f * smoothedDepth;
            const float centerDelaySamples = centerDelayMs * sampleRateF * 0.001f;
            const float sweepRangeSamples = sweepRangeMs * sampleRateF * 0.001f;

            // ---- Left channel ----
            float dryL = left[i];
            if (detail::isNaN(dryL) || detail::isInf(dryL)) {
                dryL = 0.0f;
                feedbackStateL_ = 0.0f;
            }

            // Advance left LFO (bipolar [-1, +1])
            const float lfoValueL = lfoL_.process();

            // Write input + feedback to delay line
            const float feedbackSignalL = FastMath::fastTanh(clampedFeedback * feedbackStateL_);
            delayL_.write(dryL + feedbackSignalL);

            // Multi-voice tap reading: offset in unipolar [0,1] domain, then wrap
            const float unipolarL = lfoValueL * 0.5f + 0.5f;
            float wetSumL = 0.0f;
            for (int v = 0; v < voices_; ++v) {
                const float voiceOffset = static_cast<float>(v) / static_cast<float>(voices_);
                float shifted = unipolarL + voiceOffset;
                if (shifted >= 1.0f) shifted -= 1.0f;
                const float modValue = shifted * 2.0f - 1.0f;  // back to bipolar

                const float delaySamples = centerDelaySamples + modValue * sweepRangeSamples;
                const float safeDelay = std::max(delaySamples, 1.0f);
                wetSumL += delayL_.readCubic(safeDelay);
            }
            const float wetL = wetSumL * voiceScale;

            left[i] = (1.0f - smoothedMix) * dryL + smoothedMix * wetL;
            feedbackStateL_ = detail::flushDenormal(wetL);

            // ---- Right channel ----
            float dryR = right[i];
            if (detail::isNaN(dryR) || detail::isInf(dryR)) {
                dryR = 0.0f;
                feedbackStateR_ = 0.0f;
            }

            const float lfoValueR = lfoR_.process();

            const float feedbackSignalR = FastMath::fastTanh(clampedFeedback * feedbackStateR_);
            delayR_.write(dryR + feedbackSignalR);

            const float unipolarR = lfoValueR * 0.5f + 0.5f;
            float wetSumR = 0.0f;
            for (int v = 0; v < voices_; ++v) {
                const float voiceOffset = static_cast<float>(v) / static_cast<float>(voices_);
                float shifted = unipolarR + voiceOffset;
                if (shifted >= 1.0f) shifted -= 1.0f;
                const float modValue = shifted * 2.0f - 1.0f;

                const float delaySamples = centerDelaySamples + modValue * sweepRangeSamples;
                const float safeDelay = std::max(delaySamples, 1.0f);
                wetSumR += delayR_.readCubic(safeDelay);
            }
            const float wetR = wetSumR * voiceScale;

            right[i] = (1.0f - smoothedMix) * dryR + smoothedMix * wetR;
            feedbackStateR_ = detail::flushDenormal(wetR);
        }
    }

private:

    // =========================================================================
    // State Variables
    // =========================================================================

    DelayLine delayL_;
    DelayLine delayR_;

    LFO lfoL_;
    LFO lfoR_;

    OnePoleSmoother rateSmoother_;
    OnePoleSmoother depthSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;

    float feedbackStateL_ = 0.0f;
    float feedbackStateR_ = 0.0f;

    double sampleRate_ = 44100.0;
    float rate_ = kDefaultRate;
    float depth_ = kDefaultDepth;
    float feedback_ = kDefaultFeedback;
    float mix_ = kDefaultMix;
    float stereoSpread_ = kDefaultStereoSpread;
    int voices_ = kDefaultVoices;
    Waveform waveform_ = Waveform::Triangle;

    bool tempoSync_ = false;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;
    double tempo_ = 120.0;

    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
