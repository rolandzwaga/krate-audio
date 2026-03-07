// ==============================================================================
// pre_processing_pipeline.h - Analysis Signal Pre-Processing Pipeline
// ==============================================================================
// Cleans the analysis signal before pitch detection and spectral analysis.
// Applies: DC offset removal, high-pass filtering, noise gate, and transient
// suppression. Operates on a separate analysis buffer (FR-009).
//
// Feature: 115-innexus-m1-core-instrument
// User Story: US3 (Graceful Handling of Difficult Source Material)
// Requirements: FR-005, FR-006, FR-007, FR-008, FR-009
//
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocations, no locks, no exceptions, no I/O in processBlock()
// Constitution Principle IX: Layered DSP Architecture
// - Reuses existing Layer 1/2 primitives (Biquad, EnvelopeFollower)
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>

namespace Innexus {

/// Pre-processing pipeline for analysis signal cleaning.
///
/// Components in processing order:
/// 1. DC offset removal via IIR estimator with first-sample init (FR-005)
/// 2. High-pass filter (Biquad Butterworth at 30 Hz, Q=0.707) (FR-006)
/// 3. Transient suppression (EnvelopeFollower-based gain reduction) (FR-008)
/// 4. Noise gate (block RMS threshold) (FR-007)
///
/// The DC estimator initializes to the first sample's value, so the
/// downstream Biquad never sees a step transient. This allows fast DC
/// convergence (<1% within 13ms) while the Biquad provides additional
/// sub-bass rejection (>12 dB combined at 20 Hz, <1 dB loss at 100 Hz).
///
/// This class does NOT modify any audio output -- it operates solely on
/// a dedicated analysis buffer passed to processBlock() (FR-009).
class PreProcessingPipeline {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Default noise gate threshold in dB (FR-007)
    /// -40 dB rejects low-level noise that would otherwise cause spurious
    /// pitch detections during silence (especially with cross-synthesis blend).
    static constexpr float kDefaultNoiseGateThresholdDb = -50.0f;

    /// DC estimator pole frequency in Hz (FR-005)
    /// Controls DC tracking speed. At 40 Hz, time constant = 4ms.
    /// Combined with first-sample initialization, achieves <1% DC
    /// convergence within 13ms at 44.1 kHz.
    static constexpr float kDcEstimatorFreqHz = 40.0f;

    /// HPF cutoff frequency in Hz (FR-006)
    static constexpr float kHpfCutoffHz = 30.0f;

    /// Transient suppression envelope release time in ms (FR-008)
    static constexpr float kTransientEnvelopeReleaseMs = 50.0f;

    /// Transient suppression gain reduction ratio (FR-008: 10:1 default)
    static constexpr float kDefaultTransientRatio = 10.0f;

    /// Transient detection threshold
    static constexpr float kTransientDetectionThreshold = 2.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // FR-005: DC estimator coefficient
        // alpha = exp(-2*pi*freq/sampleRate)
        dcAlpha_ = static_cast<float>(
            std::exp(-2.0 * 3.14159265358979 *
                     static_cast<double>(kDcEstimatorFreqHz) / sampleRate));

        // FR-006: Butterworth HPF at 30 Hz
        highPass_.configure(
            Krate::DSP::FilterType::Highpass,
            kHpfCutoffHz,
            Krate::DSP::kButterworthQ,
            0.0f,
            static_cast<float>(sampleRate)
        );

        // FR-008: Transient suppression slow envelope follower
        slowEnvelope_.prepare(sampleRate, 0);
        slowEnvelope_.setMode(Krate::DSP::DetectionMode::Amplitude);
        slowEnvelope_.setAttackTime(kTransientEnvelopeReleaseMs);
        slowEnvelope_.setReleaseTime(kTransientEnvelopeReleaseMs);

        setNoiseGateThreshold(kDefaultNoiseGateThresholdDb);
        reset();
    }

    void reset() noexcept {
        dcEstimate_ = 0.0f;
        dcInitialized_ = false;
        highPass_.reset();
        slowEnvelope_.reset();
        gated_ = false;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// Process a block of samples in the analysis buffer (in-place).
    /// Processing order: DC removal -> HPF -> Transient suppression -> Noise gate
    void processBlock(float* analysisBuffer, size_t numSamples) noexcept {
        if (numSamples == 0) return;

        // Step 1: DC offset removal (FR-005)
        removeDC(analysisBuffer, numSamples);

        // Step 2: Butterworth HPF at 30 Hz (FR-006)
        highPass_.processBlock(analysisBuffer, numSamples);

        // Step 3: Transient suppression (FR-008)
        applyTransientSuppression(analysisBuffer, numSamples);

        // Step 4: Noise gate (FR-007)
        applyNoiseGate(analysisBuffer, numSamples);
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    void setNoiseGateThreshold(float thresholdDb) noexcept {
        noiseGateThresholdDb_ = thresholdDb;
        noiseGateThresholdLinear_ = Krate::DSP::dbToGain(thresholdDb);
    }

    void setTransientSuppression(float amount) noexcept {
        transientSuppression_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// Returns true if the last processBlock() call was gated (below threshold).
    bool isGated() const noexcept { return gated_; }

private:
    // =========================================================================
    // Internal Processing
    // =========================================================================

    /// Remove DC offset using a first-sample-initialized IIR estimator.
    ///
    /// The estimator initializes to the first sample's value, so the
    /// output starts near zero — the downstream Biquad HPF never sees a
    /// step and doesn't produce a long settling transient. Subsequent DC
    /// changes are tracked with time constant 1/(2*pi*freq).
    void removeDC(float* buffer, size_t numSamples) noexcept {
        if (!dcInitialized_ && numSamples > 0) {
            dcEstimate_ = buffer[0];
            dcInitialized_ = true;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            dcEstimate_ = dcAlpha_ * dcEstimate_ +
                          (1.0f - dcAlpha_) * buffer[i];

            // Flush denormals
            if (std::abs(dcEstimate_) < 1e-15f) dcEstimate_ = 0.0f;

            buffer[i] -= dcEstimate_;
        }
    }

    /// Apply transient suppression using envelope followers (FR-008).
    void applyTransientSuppression(float* buffer, size_t numSamples) noexcept {
        if (transientSuppression_ <= 0.0f) return;

        for (size_t i = 0; i < numSamples; ++i) {
            float sample = buffer[i];
            float instantLevel = std::abs(sample);
            float slowLevel = slowEnvelope_.processSample(sample);

            constexpr float kEpsilon = 1e-10f;
            float ratio = instantLevel / (slowLevel + kEpsilon);

            if (ratio > kTransientDetectionThreshold && slowLevel > kEpsilon) {
                float excessRatio = ratio - kTransientDetectionThreshold;
                float compressedExcess = excessRatio / kDefaultTransientRatio;
                float targetRatio = kTransientDetectionThreshold + compressedExcess;
                float gain = targetRatio / ratio;
                gain = 1.0f - transientSuppression_ * (1.0f - gain);
                buffer[i] = sample * gain;
            }
        }
    }

    /// Apply noise gate based on block RMS (FR-007).
    /// Sets gated_ flag so downstream pipeline can skip analysis immediately.
    void applyNoiseGate(float* buffer, size_t numSamples) noexcept {
        float sumSquared = 0.0f;
        for (size_t i = 0; i < numSamples; ++i) {
            sumSquared += buffer[i] * buffer[i];
        }
        float blockRMS = std::sqrt(sumSquared / static_cast<float>(numSamples));

        if (blockRMS < noiseGateThresholdLinear_) {
            for (size_t i = 0; i < numSamples; ++i) {
                buffer[i] = 0.0f;
            }
            gated_ = true;
        } else {
            gated_ = false;
        }
    }

    // =========================================================================
    // Members
    // =========================================================================

    // FR-005: DC offset removal (IIR estimator)
    float dcEstimate_ = 0.0f;
    float dcAlpha_ = 0.0f;
    bool dcInitialized_ = false;

    // FR-006: Butterworth HPF at 30 Hz
    Krate::DSP::Biquad highPass_;

    // FR-008: Transient suppression
    Krate::DSP::EnvelopeFollower slowEnvelope_;
    float transientSuppression_ = 1.0f;

    // FR-007: Noise gate
    float noiseGateThresholdDb_ = kDefaultNoiseGateThresholdDb;
    float noiseGateThresholdLinear_ = 0.00316f; // -50 dB
    bool gated_ = false;

    // Internal
    double sampleRate_ = 44100.0;
};

} // namespace Innexus
