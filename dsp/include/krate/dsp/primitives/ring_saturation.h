// ==============================================================================
// Layer 1: DSP Primitive - Ring Saturation
// ==============================================================================
// Ring modulation + saturation distortion primitive that creates metallic,
// bell-like character through inharmonic sidebands.
//
// Feature: 108-ring-saturation
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 1: Waveshaper, DCBlocker, LinearRamp (smoother.h)
//   - Layer 0: Sigmoid::tanh (sigmoid.h)
//
// Core Formula:
//   saturated = waveshape(input * drive)
//   carrier   = oscillator(phase)          (1.0 when carrier inactive)
//   output    = input + (input * saturated * (carrier + bias) - input) * depth
//
// When carrier frequency is 0 (default), the carrier term is 1.0 and the
// formula reduces to the original self-modulation:
//   output = input + (input * saturate(input * drive) - input) * depth
//
// Every depth, 0 included, ends in the output stage
//   final = DCBlock(2 * tanh(output / 2))
// so depth 0 is NOT a raw bypass: it returns DCBlock(softLimit(input)), the
// limit of depth -> 0+ (D-163; SC-002 changed from "returns input unchanged").
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 1 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/108-ring-saturation/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>


namespace Krate::DSP {

// =============================================================================
// Carrier Waveform and Frequency Mode Enums
// =============================================================================

/// Carrier oscillator waveform types for ring modulation
enum class RSCarrierWaveform : uint8_t {
    Sine = 0,
    Triangle = 1,
    Square = 2,
    Saw = 3
};

/// Carrier frequency modes
enum class RSFreqMode : uint8_t {
    Fixed = 0,      ///< Carrier at user-set frequency in Hz
    Harmonic = 1,   ///< Carrier at tracked input frequency * integer ratio
    Track = 2,      ///< Carrier tracks input frequency via zero-crossing detection
    Random = 3      ///< Carrier frequency randomly wanders
};

// =============================================================================
// RingSaturation - Ring Modulation + Saturation Primitive
// =============================================================================

class RingSaturation {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kMinStages = 1;
    static constexpr int kMaxStages = 4;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kCrossfadeTimeMs = 10.0f;
    static constexpr float kSoftLimitScale = 2.0f;

    static constexpr float kMinCarrierFreqHz = 0.5f;  ///< Below this, carrier is bypassed
    static constexpr float kMinTrackedFreqHz = 20.0f;
    static constexpr float kMaxTrackedFreqHz = 5000.0f;
    /// After setCarrierState the zero-crossing tracker ignores its input for this long
    static constexpr double kCarrierResyncHoldSeconds = 0.002;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    RingSaturation() noexcept
        : shaper_()
        , dcBlocker_()
        , crossfade_() {
        shaper_.setType(WaveshapeType::Tanh);
        shaper_.setDrive(drive_);
    }

    // Default copy/move
    RingSaturation(const RingSaturation&) = default;
    RingSaturation& operator=(const RingSaturation&) = default;
    RingSaturation(RingSaturation&&) noexcept = default;
    RingSaturation& operator=(RingSaturation&&) noexcept = default;
    ~RingSaturation() = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::max(sampleRate, 1000.0);

        dcBlocker_.prepare(sampleRate_, kDCBlockerCutoffHz);

        crossfade_.ramp.configure(kCrossfadeTimeMs, static_cast<float>(sampleRate_));
        crossfade_.ramp.snapTo(1.0f);
        crossfade_.active = false;

        // Carrier state
        phase_ = 0.0;
        lastSample_ = 0.0f;
        samplesSinceCrossing_ = 0;
        trackerSynced_ = true;
        trackerHold_ = 0;
        trackedFreq_ = 220.0f;
        randomCurrent_ = 220.0f;
        randomTarget_ = 220.0f;
        randomCounter_ = 0;

        // Pre-compute random smoother coefficient: ~5 Hz smoothing
        float tau = 1.0f / (2.0f * std::numbers::pi_v<float> * 5.0f);
        float dt = 1.0f / static_cast<float>(sampleRate_);
        randomSmooth_ = dt / (tau + dt);

        prepared_ = true;
    }

    void reset() noexcept {
        dcBlocker_.reset();
        crossfade_.ramp.snapTo(1.0f);
        crossfade_.active = false;

        phase_ = 0.0;
        lastSample_ = 0.0f;
        samplesSinceCrossing_ = 0;
        trackerSynced_ = true;
        trackerHold_ = 0;
        trackedFreq_ = 220.0f;
        randomCurrent_ = 220.0f;
        randomTarget_ = 220.0f;
        randomCounter_ = 0;
    }

    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    void setSaturationCurve(WaveshapeType type) noexcept {
        if (!prepared_) {
            shaper_.setType(type);
            return;
        }
        if (shaper_.getType() == type) {
            return;
        }
        crossfade_.oldShaper = shaper_;
        shaper_.setType(type);
        shaper_.setDrive(drive_);
        crossfade_.ramp.snapTo(0.0f);
        crossfade_.ramp.setTarget(1.0f);
        crossfade_.active = true;
    }

    void setDrive(float drive) noexcept {
        drive_ = std::max(0.0f, drive);
        shaper_.setDrive(drive_);
    }

    void setModulationDepth(float depth) noexcept {
        depth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    void setStages(int stages) noexcept {
        stages_ = std::clamp(stages, kMinStages, kMaxStages);
    }

    /// @brief Set carrier oscillator waveform
    void setCarrierWaveform(RSCarrierWaveform waveform) noexcept {
        carrierWaveform_ = waveform;
    }

    /// @brief Set carrier frequency mode
    void setFreqMode(RSFreqMode mode) noexcept {
        freqMode_ = mode;
    }

    /// @brief Set carrier frequency in Hz (used in Fixed mode)
    void setCarrierFrequency(float hz) noexcept {
        carrierFreqHz_ = std::max(0.0f, hz);
    }

    /// @brief Set harmonic ratio (used in Harmonic mode)
    void setHarmonicRatio(float ratio) noexcept {
        harmonicRatio_ = std::clamp(ratio, 1.0f, 16.0f);
    }

    /// @brief Set carrier DC bias [-1, +1]
    /// At bias=0: pure ring modulation (carrier ranges -1 to +1)
    /// At bias=+1: amplitude modulation (carrier+bias ranges 0 to +2)
    void setBias(float bias) noexcept {
        bias_ = std::clamp(bias, -1.0f, 1.0f);
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept {
        return shaper_.getType();
    }

    [[nodiscard]] float getDrive() const noexcept {
        return drive_;
    }

    [[nodiscard]] float getModulationDepth() const noexcept {
        return depth_;
    }

    [[nodiscard]] int getStages() const noexcept {
        return stages_;
    }

    [[nodiscard]] RSCarrierWaveform getCarrierWaveform() const noexcept {
        return carrierWaveform_;
    }

    [[nodiscard]] RSFreqMode getFreqMode() const noexcept {
        return freqMode_;
    }

    [[nodiscard]] float getCarrierFrequency() const noexcept {
        return carrierFreqHz_;
    }

    [[nodiscard]] float getBias() const noexcept {
        return bias_;
    }

    /// @brief The carrier oscillator's running state: phase, zero-crossing tracker and the
    /// Random mode's wander. An owner that switches between two instances at different rates
    /// (an oversampling crossfade) carries it over, so the carrier keeps its phase.
    struct CarrierState {
        double phase = 0.0;
        float lastSample = 0.0f;
        int samplesSinceCrossing = 0;
        float trackedFreq = 220.0f;
        uint32_t rngState = 12345u;
        float randomTarget = 220.0f;
        float randomCurrent = 220.0f;
        int randomCounter = 0;
    };

    [[nodiscard]] CarrierState getCarrierState() const noexcept {
        return {.phase = phase_,
                .lastSample = lastSample_,
                .samplesSinceCrossing = samplesSinceCrossing_,
                .trackedFreq = trackedFreq_,
                .rngState = rngState_,
                .randomTarget = randomTarget_,
                .randomCurrent = randomCurrent_,
                .randomCounter = randomCounter_};
    }

    /// @brief Take over @p state from an instance running at @p fromSampleRate: phase and
    /// frequencies are kept, the Random mode's counter is rescaled to this instance's rate. The
    /// zero-crossing tracker keeps the tracked frequency and re-syncs on this instance's own
    /// input: it ignores the first kCarrierResyncHoldSeconds (an owner that has just cleared
    /// this instance's input path feeds it a few samples of zeros and filter settling), then its
    /// first crossing only starts the count (the other instance's count does not apply here). @p inputLagSeconds is how
    /// much later this instance's input is than the other's: the phase moves back by the carrier
    /// frequency times it, so the carrier keeps its phase against the signal it multiplies.
    void setCarrierState(const CarrierState& state, double fromSampleRate, double inputLagSeconds = 0.0) noexcept {
        const double scale = fromSampleRate > 0.0 ? sampleRate_ / fromSampleRate : 1.0;
        double freq = 0.0;
        switch (freqMode_) {
            case RSFreqMode::Fixed: freq = carrierFreqHz_; break;
            case RSFreqMode::Harmonic: freq = static_cast<double>(state.trackedFreq) * harmonicRatio_; break;
            case RSFreqMode::Track: freq = state.trackedFreq; break;
            case RSFreqMode::Random: freq = state.randomCurrent; break;
        }
        const double shifted = state.phase - freq * inputLagSeconds;
        phase_ = shifted - std::floor(shifted);
        lastSample_ = state.lastSample;
        samplesSinceCrossing_ = 0;
        trackerSynced_ = false;
        trackerHold_ = static_cast<int>(std::lround(kCarrierResyncHoldSeconds * sampleRate_));
        trackedFreq_ = state.trackedFreq;
        rngState_ = state.rngState;
        randomTarget_ = state.randomTarget;
        randomCurrent_ = state.randomCurrent;
        randomCounter_ = static_cast<int>(std::lround(static_cast<double>(state.randomCounter) * scale));
    }

    /// @brief Rate prepare() was given (at least 1000 Hz).
    [[nodiscard]] double getSampleRate() const noexcept {
        return sampleRate_;
    }

    // =========================================================================
    // Processing Methods
    // =========================================================================

    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return input;
        }

        if (detail::isNaN(input)) {
            return input;
        }

        if (detail::isInf(input)) {
            return input > 0.0f ? kSoftLimitScale : -kSoftLimitScale;
        }

        // Depth 0 skips only the ring-mod stages: the output stage (soft limiter
        // + DC blocker) runs at every depth, so depth 0 is continuous with depth
        // 0+ (D-163). Depth 0 therefore returns DCBlock(softLimit(input)), not
        // the raw input.
        float signal = input;
        if (depth_ != 0.0f) {
            // The tracker reads the input and the carrier advances once per sample; every stage
            // multiplies by the same carrier sample (Disrumpo audit follow-up 84)
            updateFrequencyTracker(input);
            const float carrier = nextCarrierSample();
            for (int stage = 0; stage < stages_; ++stage) {
                signal = processStage(signal, carrier);
            }
        }

        signal = softLimit(signal);
        signal = dcBlocker_.process(signal);

        return signal;
    }

    /// @brief process() over @p numSamples samples in place, stage by stage over chunks of
    /// kBlockChunk: every sample's carrier, then every sample's stage 1, stage 2, ..., then the
    /// output stage in sample order. A sample's stages form one dependent chain; across samples the
    /// stages are independent (the carrier, the tracker and the DC blocker, the only state, still
    /// run in sample order), so the processor overlaps the chains. Each sample takes the operations
    /// of process() in their order: the result is the per-sample one. A chunk holding a NaN / Inf
    /// or running a curve crossfade (its ramp advances once per stage) runs per sample.
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr || numSamples == 0 || !prepared_) {
            return;
        }

        size_t i = 0;
        while (i < numSamples) {
            const size_t len = std::min(kBlockChunk, numSamples - i);
            if (crossfade_.active || !allFinite(buffer + i, len)) {
                for (size_t k = 0; k < len; ++k) buffer[i + k] = process(buffer[i + k]);
            } else {
                processChunk(buffer + i, len);
            }
            i += len;
        }
    }

private:
    // =========================================================================
    // Internal Implementation
    // =========================================================================

    struct CrossfadeState {
        Waveshaper oldShaper;
        LinearRamp ramp;
        bool active = false;
    };

    /// Samples per stage-major chunk of processBlock().
    static constexpr size_t kBlockChunk = 64;

    /// @brief True when every sample of @p x[0, n) is finite (process() passes the others on).
    [[nodiscard]] static bool allFinite(const float* x, size_t n) noexcept {
        for (size_t k = 0; k < n; ++k) {
            if (!detail::isFinite(x[k])) return false;
        }
        return true;
    }

    /// @brief process() over @p n <= kBlockChunk finite samples, no crossfade running.
    void processChunk(float* x, size_t n) noexcept {
        if (depth_ != 0.0f) {
            std::array<float, kBlockChunk> carrier{};
            for (size_t k = 0; k < n; ++k) {
                updateFrequencyTracker(x[k]);
                carrier[k] = nextCarrierSample();
            }
            for (int stage = 0; stage < stages_; ++stage) {
                for (size_t k = 0; k < n; ++k) {
                    const float saturated = shaper_.process(x[k]);
                    const float ringModTerm = x[k] * saturated * (carrier[k] + bias_) - x[k];
                    x[k] = x[k] + ringModTerm * depth_;
                }
            }
        }
        for (size_t k = 0; k < n; ++k) {
            x[k] = softLimit(x[k]);
        }
        for (size_t k = 0; k < n; ++k) {
            x[k] = dcBlocker_.process(x[k]);
        }
    }

    /// @brief This sample's carrier (1.0 when the carrier is inactive); advances the phase.
    [[nodiscard]] float nextCarrierSample() noexcept {
        const float effectiveFreq = getEffectiveFrequency();
        if (effectiveFreq < kMinCarrierFreqHz) {
            return 1.0f;
        }
        const float carrier = getCarrierSample();
        phase_ += static_cast<double>(effectiveFreq) / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        return carrier;
    }

    [[nodiscard]] float processStage(float input, float carrier) noexcept {
        // Apply saturation (with crossfade handling)
        float saturated;
        if (crossfade_.active) {
            float oldSaturated = crossfade_.oldShaper.process(input);
            float newSaturated = shaper_.process(input);
            float position = crossfade_.ramp.process();
            saturated = std::lerp(oldSaturated, newSaturated, position);
            if (crossfade_.ramp.isComplete()) {
                crossfade_.active = false;
            }
        } else {
            saturated = shaper_.process(input);
        }

        // Ring modulation formula:
        //   input * saturated * (carrier + bias) - input
        // When carrier=1, bias=0: reduces to original self-mod formula
        float ringModTerm = input * saturated * (carrier + bias_) - input;
        float output = input + ringModTerm * depth_;

        return output;
    }

    /// @brief Generate one carrier sample from the current phase
    [[nodiscard]] float getCarrierSample() const noexcept {
        float p = static_cast<float>(phase_);
        switch (carrierWaveform_) {
            case RSCarrierWaveform::Sine:
                return std::sin(p * 6.283185307f);
            case RSCarrierWaveform::Triangle:
                return 4.0f * std::abs(p - 0.5f) - 1.0f;
            case RSCarrierWaveform::Square:
                return p < 0.5f ? 1.0f : -1.0f;
            case RSCarrierWaveform::Saw:
                return 2.0f * p - 1.0f;
            default:
                return 1.0f;
        }
    }

    /// @brief Get effective carrier frequency based on current mode
    [[nodiscard]] float getEffectiveFrequency() noexcept {
        switch (freqMode_) {
            case RSFreqMode::Fixed:
                return carrierFreqHz_;
            case RSFreqMode::Harmonic:
                return trackedFreq_ * harmonicRatio_;
            case RSFreqMode::Track:
                return trackedFreq_;
            case RSFreqMode::Random:
                updateRandomFreq();
                return randomCurrent_;
            default:
                return carrierFreqHz_;
        }
    }

    /// @brief Update zero-crossing frequency tracker
    void updateFrequencyTracker(float input) noexcept {
        if (freqMode_ != RSFreqMode::Track && freqMode_ != RSFreqMode::Harmonic) return;

        samplesSinceCrossing_++;
        bool currentPositive = input >= 0.0f;
        bool lastPositive = lastSample_ >= 0.0f;

        if (trackerHold_ > 0) {
            --trackerHold_;  // setCarrierState: the input path is still settling
            lastSample_ = input;
            return;
        }
        if (currentPositive != lastPositive && !trackerSynced_) {
            trackerSynced_ = true;  // setCarrierState: count from this crossing
            samplesSinceCrossing_ = 0;
        } else if (currentPositive != lastPositive && samplesSinceCrossing_ > 1) {
            float halfPeriod = static_cast<float>(samplesSinceCrossing_);
            float freq = static_cast<float>(sampleRate_) / (halfPeriod * 2.0f);
            freq = std::clamp(freq, kMinTrackedFreqHz, kMaxTrackedFreqHz);
            // Heavy smoothing to reduce jitter
            trackedFreq_ = trackedFreq_ * 0.95f + freq * 0.05f;
            samplesSinceCrossing_ = 0;
        }
        lastSample_ = input;
    }

    /// @brief Update random frequency wandering
    void updateRandomFreq() noexcept {
        randomCounter_++;
        // Change target ~10 times per second
        int interval = static_cast<int>(sampleRate_ * 0.1);
        interval = std::max(interval, 1);
        if (randomCounter_ >= interval) {
            float r = nextRandom();
            // Random freq in [50, 2000] Hz
            randomTarget_ = 50.0f + r * 1950.0f;
            randomCounter_ = 0;
        }
        // Smooth toward target
        randomCurrent_ += (randomTarget_ - randomCurrent_) * randomSmooth_;
    }

    /// @brief Simple LCG random number generator [0, 1)
    [[nodiscard]] float nextRandom() noexcept {
        rngState_ = rngState_ * 1664525u + 1013904223u;
        return static_cast<float>(rngState_ >> 8) / 16777216.0f;
    }

    [[nodiscard]] static float softLimit(float x) noexcept {
        return kSoftLimitScale * Sigmoid::tanh(x * 0.5f);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    Waveshaper shaper_;
    DCBlocker dcBlocker_;
    CrossfadeState crossfade_;

    float drive_ = 1.0f;
    float depth_ = 1.0f;
    int stages_ = 1;

    // Carrier oscillator
    RSCarrierWaveform carrierWaveform_ = RSCarrierWaveform::Sine;
    RSFreqMode freqMode_ = RSFreqMode::Fixed;
    float carrierFreqHz_ = 0.0f;    ///< Default 0 = carrier bypassed (backward compat)
    float harmonicRatio_ = 1.0f;
    float bias_ = 0.0f;
    double phase_ = 0.0;

    // Zero-crossing frequency tracker (for Track/Harmonic modes)
    float lastSample_ = 0.0f;
    int samplesSinceCrossing_ = 0;
    float trackedFreq_ = 220.0f;
    bool trackerSynced_ = true;  ///< false after setCarrierState until the first crossing
    int trackerHold_ = 0;        ///< samples the tracker still ignores after setCarrierState

    // Random frequency state (for Random mode)
    uint32_t rngState_ = 12345u;
    float randomTarget_ = 220.0f;
    float randomCurrent_ = 220.0f;
    int randomCounter_ = 0;
    float randomSmooth_ = 0.001f;

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace Krate::DSP

