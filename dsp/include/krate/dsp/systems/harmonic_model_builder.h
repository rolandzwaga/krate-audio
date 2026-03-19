#pragma once

// ==============================================================================
// Harmonic Model Builder - Layer 3 System
// ==============================================================================
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Covers: FR-029 (per-frame harmonic model), FR-030 (L2 normalization),
//         FR-031 (dual-timescale blending), FR-032 (spectral centroid/brightness),
//         FR-033 (median filtering), FR-034 (global amplitude tracking)
//
// Converts raw PartialTracker measurements into stable, smoothed HarmonicFrames
// ready for synthesis by the HarmonicOscillatorBank.
// ==============================================================================

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

/// Converts raw partial measurements into a stable, musically useful HarmonicFrame.
///
/// Responsibilities:
/// - L2 normalization: separates spectral shape from loudness (FR-030)
/// - Dual-timescale blending: fast (~5ms) and slow (~100ms) layers (FR-031)
/// - Spectral descriptors: centroid and brightness (FR-032)
/// - Median filtering: per-partial amplitude, window of 5 (FR-033)
/// - Global amplitude: smoothed RMS tracking (FR-034)
/// - Noisiness estimate: residual-to-harmonic ratio (FR-029)
class HarmonicModelBuilder {
public:
    /// Default fast smoothing time in ms (~5ms for articulation capture)
    static constexpr float kFastSmoothTimeMs = 5.0f;

    /// Default slow smoothing time in ms (~100ms for timbral identity)
    static constexpr float kSlowSmoothTimeMs = 100.0f;

    /// Default global amplitude smoothing time in ms
    static constexpr float kGlobalAmpSmoothTimeMs = 10.0f;

    /// Median filter window size (odd, for proper median)
    static constexpr size_t kMedianWindowSize = 5;

    /// Configure sample rate and internal state.
    /// @param sampleRate Audio sample rate in Hz
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        hopSize_ = 256; // default, can be overridden

        configureSmoothers();

        reset();
    }

    /// Reset all internal state (smoothers, median buffers).
    void reset() noexcept {
        for (size_t i = 0; i < kMaxPartials; ++i) {
            fastSmoothers_[i].reset();
            slowSmoothers_[i].reset();
            medianWriteIndex_[i] = 0;
            medianBufferFilled_[i] = false;
            for (size_t j = 0; j < kMedianWindowSize; ++j) {
                medianBuffer_[i][j] = 0.0f;
            }
        }
        globalAmpSmoother_.reset();
        smoothedBandwidth_.fill(0.0f);
    }

    /// Set the hop size in samples (affects smoother advance rate).
    /// @param hopSizeSamples Number of samples between analysis frames
    void setHopSize(int hopSizeSamples) noexcept {
        hopSize_ = hopSizeSamples;
    }

    /// Set the responsiveness parameter for dual-timescale blending.
    /// 0.0 = full slow layer (timbral identity only)
    /// 1.0 = full fast layer (captures articulation)
    /// 0.5 = equal blend (default for M1, FR-031)
    /// @param value Blend factor [0.0, 1.0]
    void setResponsiveness(float value) noexcept {
        responsiveness_ = std::clamp(value, 0.0f, 1.0f);
    }

    /// Build a HarmonicFrame from raw partial measurements.
    ///
    /// This is the main entry point, called once per analysis hop.
    /// It applies median filtering, dual-timescale blending, L2 normalization,
    /// and computes spectral descriptors.
    ///
    /// @param partials     Array of tracked partials from PartialTracker
    /// @param numPartials  Number of active partials in the array
    /// @param f0           Fundamental frequency estimate from YIN
    /// @param inputRms     RMS level of the input signal for this frame
    /// @return A fully processed HarmonicFrame ready for synthesis
    [[nodiscard]] HarmonicFrame build(
        const std::array<Partial, kMaxPartials>& partials,
        int numPartials,
        const F0Estimate& f0,
        float inputRms) noexcept
    {
        HarmonicFrame frame{};
        frame.f0 = f0.frequency;
        frame.f0Confidence = f0.confidence;
        frame.numPartials = numPartials;

        if (numPartials <= 0) {
            // Update global amplitude even with no partials
            globalAmpSmoother_.setTarget(inputRms);
            globalAmpSmoother_.advanceSamples(static_cast<size_t>(hopSize_));
            frame.globalAmplitude = globalAmpSmoother_.getCurrentValue();
            frame.noisiness = (inputRms > 1e-10f) ? 1.0f : 0.0f;
            return frame;
        }

        // ---- Step 1: Median filter per-partial amplitudes (FR-033) ----
        std::array<float, kMaxPartials> medianAmps{};
        for (int i = 0; i < numPartials; ++i) {
            medianAmps[i] = applyMedianFilter(
                static_cast<size_t>(i), partials[i].amplitude);
        }

        // ---- Step 2: Dual-timescale blending (FR-031) ----
        std::array<float, kMaxPartials> blendedAmps{};
        for (int i = 0; i < numPartials; ++i) {
            const auto idx = static_cast<size_t>(i);

            // Feed median-filtered amplitude to both smoothers
            fastSmoothers_[idx].setTarget(medianAmps[i]);
            slowSmoothers_[idx].setTarget(medianAmps[i]);

            // Advance by hopSize samples
            fastSmoothers_[idx].advanceSamples(static_cast<size_t>(hopSize_));
            slowSmoothers_[idx].advanceSamples(static_cast<size_t>(hopSize_));

            float fastVal = fastSmoothers_[idx].getCurrentValue();
            float slowVal = slowSmoothers_[idx].getCurrentValue();

            // Blend: output = lerp(slowModel, fastFrame, responsiveness)
            blendedAmps[i] = slowVal + responsiveness_ * (fastVal - slowVal);
        }

        // ---- Step 3: L2 normalization (FR-030) ----
        float sumSqr = 0.0f;
        for (int i = 0; i < numPartials; ++i) {
            sumSqr += blendedAmps[i] * blendedAmps[i];
        }

        float normFactor = 1.0f;
        if (sumSqr > 1e-20f) {
            normFactor = 1.0f / std::sqrt(sumSqr);
        }

        // ---- Step 4: Populate output partials ----
        for (int i = 0; i < numPartials; ++i) {
            frame.partials[i] = partials[i];
            frame.partials[i].amplitude = blendedAmps[i] * normFactor;
            // Smooth bandwidth with one-pole filter for stability
            float rawBw = partials[i].bandwidth;
            float& prevBw = smoothedBandwidth_[static_cast<size_t>(i)];
            constexpr float kBwSmoothCoeff = 0.3f; // ~3 frames to settle
            prevBw += kBwSmoothCoeff * (rawBw - prevBw);
            frame.partials[i].bandwidth = prevBw;
        }

        // ---- Step 5: Spectral centroid and brightness (FR-032) ----
        frame.spectralCentroid = computeSpectralCentroid(partials, numPartials);
        if (f0.frequency > 0.0f) {
            frame.brightness = frame.spectralCentroid / f0.frequency;
        }

        // ---- Step 6: Noisiness estimate (FR-029) ----
        // noisiness = 1.0 - partialEnergy / totalInputEnergy
        float partialEnergy = 0.0f;
        for (int i = 0; i < numPartials; ++i) {
            partialEnergy += partials[i].amplitude * partials[i].amplitude;
        }
        float totalInputEnergy = inputRms * inputRms;
        if (totalInputEnergy > 1e-20f) {
            float ratio = partialEnergy / totalInputEnergy;
            frame.noisiness = std::clamp(1.0f - ratio, 0.0f, 1.0f);
        } else {
            frame.noisiness = 0.0f;
        }

        // ---- Step 7: Global amplitude (FR-034) ----
        globalAmpSmoother_.setTarget(inputRms);
        globalAmpSmoother_.advanceSamples(static_cast<size_t>(hopSize_));
        frame.globalAmplitude = globalAmpSmoother_.getCurrentValue();

        return frame;
    }

private:
    /// Configure internal smoothers based on current sample rate.
    void configureSmoothers() noexcept {
        for (size_t i = 0; i < kMaxPartials; ++i) {
            fastSmoothers_[i].configure(kFastSmoothTimeMs, sampleRate_);
            slowSmoothers_[i].configure(kSlowSmoothTimeMs, sampleRate_);
        }
        globalAmpSmoother_.configure(kGlobalAmpSmoothTimeMs, sampleRate_);
    }

    /// Apply median filter to a partial's amplitude (FR-033).
    /// Uses a ring buffer of kMedianWindowSize entries per partial.
    /// @param partialIndex Index of the partial (0..kMaxPartials-1)
    /// @param amplitude Raw amplitude value to filter
    /// @return Median-filtered amplitude
    [[nodiscard]] float applyMedianFilter(size_t partialIndex,
                                           float amplitude) noexcept {
        auto& buffer = medianBuffer_[partialIndex];
        auto& writeIdx = medianWriteIndex_[partialIndex];
        auto& filled = medianBufferFilled_[partialIndex];

        // Write new value to ring buffer
        buffer[writeIdx] = amplitude;
        writeIdx = (writeIdx + 1) % kMedianWindowSize;
        if (writeIdx == 0) {
            filled = true;
        }

        // Determine how many valid entries we have
        size_t count = filled ? kMedianWindowSize : writeIdx;
        if (count == 0) {
            return amplitude;
        }

        // Copy valid entries and find median
        std::array<float, kMedianWindowSize> sorted{};
        for (size_t i = 0; i < count; ++i) {
            sorted[i] = buffer[i];
        }
        std::sort(sorted.begin(), sorted.begin() + count);

        return sorted[count / 2];
    }

    /// Compute spectral centroid from partials (FR-032).
    /// centroid = sum(freq_i * amp_i) / sum(amp_i)
    [[nodiscard]] static float computeSpectralCentroid(
        const std::array<Partial, kMaxPartials>& partials,
        int numPartials) noexcept
    {
        float weightedSum = 0.0f;
        float ampSum = 0.0f;

        for (int i = 0; i < numPartials; ++i) {
            weightedSum += partials[i].frequency * partials[i].amplitude;
            ampSum += partials[i].amplitude;
        }

        if (ampSum < 1e-10f) {
            return 0.0f;
        }
        return weightedSum / ampSum;
    }

    // -- Configuration --
    float sampleRate_ = 44100.0f;
    int hopSize_ = 256;
    float responsiveness_ = 0.5f; ///< Blend factor (FR-031 default 0.5)

    // -- Dual-timescale blending (FR-031) --
    std::array<OnePoleSmoother, kMaxPartials> fastSmoothers_{};
    std::array<OnePoleSmoother, kMaxPartials> slowSmoothers_{};

    // -- Median filter (FR-033) --
    std::array<std::array<float, kMedianWindowSize>, kMaxPartials> medianBuffer_{};
    std::array<size_t, kMaxPartials> medianWriteIndex_{};
    std::array<bool, kMaxPartials> medianBufferFilled_{};

    // -- Per-partial bandwidth smoothing --
    std::array<float, kMaxPartials> smoothedBandwidth_{};

    // -- Global amplitude (FR-034) --
    OnePoleSmoother globalAmpSmoother_;
};

} // namespace Krate::DSP
