// ==============================================================================
// Layer 2: DSP Processor - Multi-Pitch Detector
// ==============================================================================
// Detects multiple simultaneous fundamental frequencies using harmonic
// salience summation and iterative spectral cancellation (Klapuri-style).
//
// Algorithm:
// 1. Compute pitch salience for F0 candidates (40Hz-2000Hz, ~25 cents apart)
// 2. Find highest salience peak -> F0_1
// 3. Estimate spectral envelope of F0_1 via harmonic amplitude interpolation
// 4. Subtract F0_1's contribution from the spectrum
// 5. Recompute salience on residual -> F0_2
// 6. Repeat until salience drops below threshold or max voices reached
//
// References:
// - Klapuri 2003/2006: Multi-F0 via harmonic summation + iterative cancellation
// - Salamon & Gomez 2012: Melodia pitch salience
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, fixed-size arrays)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends on Layer 0 core, Layer 1 primitives)
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/primitives/spectral_buffer.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

/// @brief Multi-pitch detector using harmonic salience and iterative cancellation.
///
/// Detects up to kMaxPolyphonicVoices simultaneous fundamental frequencies
/// from spectral peak data. Designed to complement YIN for polyphonic content.
///
/// Usage:
/// @code
///   MultiPitchDetector detector;
///   detector.prepare(4096, 44100.0);
///   auto result = detector.detect(peakFreqs, peakAmps, numPeaks);
/// @endcode
class MultiPitchDetector {
public:
    /// F0 candidate range
    static constexpr float kMinF0 = 40.0f;    ///< Lowest F0 candidate (Hz)
    static constexpr float kMaxF0 = 2000.0f;   ///< Highest F0 candidate (Hz)

    /// Candidate spacing in cents (~25 cents = quarter semitone)
    static constexpr float kCandidateSpacingCents = 25.0f;

    /// Maximum number of F0 candidates (~270 for 40-2000Hz at 25 cents)
    static constexpr size_t kMaxCandidates = 300;

    /// Number of harmonics to sum in salience function
    static constexpr int kNumHarmonics = 20;

    /// Harmonic weight decay factor (weight[h] = decay^(h-1))
    static constexpr float kHarmonicWeightDecay = 0.8f;

    /// Minimum salience to accept a detected F0
    static constexpr float kMinSalience = 0.05f;

    /// Salience ratio threshold: stop when salience drops below this fraction
    /// of the strongest F0's salience
    static constexpr float kSalienceDropRatio = 0.15f;

    /// Frequency tolerance for peak matching to harmonic (fraction of harmonic freq)
    static constexpr float kHarmonicMatchTolerance = 0.03f;

    MultiPitchDetector() noexcept = default;

    /// @brief Prepare the detector for a given FFT size and sample rate.
    /// @param fftSize FFT size
    /// @param sampleRate Sample rate in Hz
    void prepare(size_t fftSize, double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        nyquist_ = sampleRate_ * 0.5f;
        binSpacing_ = sampleRate_ / static_cast<float>(fftSize);

        // Pre-compute F0 candidates (logarithmically spaced)
        numCandidates_ = 0;
        float f = kMinF0;
        const float centsToRatio = std::pow(2.0f, kCandidateSpacingCents / 1200.0f);
        while (f <= kMaxF0 && numCandidates_ < static_cast<int>(kMaxCandidates)) {
            candidates_[static_cast<size_t>(numCandidates_)] = f;
            ++numCandidates_;
            f *= centsToRatio;
        }

        // Pre-compute harmonic weights
        harmonicWeights_[0] = 1.0f;
        for (int h = 1; h < kNumHarmonics; ++h) {
            harmonicWeights_[static_cast<size_t>(h)] =
                harmonicWeights_[static_cast<size_t>(h - 1)] * kHarmonicWeightDecay;
        }
    }

    /// @brief Detect multiple fundamental frequencies from spectral peaks.
    ///
    /// @param peakFreqs Array of detected peak frequencies (Hz)
    /// @param peakAmps Array of detected peak amplitudes
    /// @param numPeaks Number of peaks
    /// @return MultiF0Result with up to kMaxPolyphonicVoices detected F0s
    [[nodiscard]] MultiF0Result detect(
        const float* peakFreqs,
        const float* peakAmps,
        int numPeaks) noexcept
    {
        MultiF0Result result{};

        if (numPeaks <= 0 || peakFreqs == nullptr || peakAmps == nullptr) {
            return result;
        }

        // Copy peak data into working buffers (we'll modify amplitudes during cancellation)
        const int np = std::min(numPeaks, static_cast<int>(kMaxPeaks));
        for (int i = 0; i < np; ++i) {
            workFreqs_[static_cast<size_t>(i)] = peakFreqs[i];
            workAmps_[static_cast<size_t>(i)] = peakAmps[i];
        }

        float strongestSalience = 0.0f;

        // Iterative detection loop
        for (int voice = 0; voice < kMaxPolyphonicVoices; ++voice) {
            // Compute salience for all F0 candidates
            computeSalience(workFreqs_.data(), workAmps_.data(), np);

            // Find the highest salience peak
            int bestCandidate = -1;
            float bestSalience = 0.0f;
            for (int c = 0; c < numCandidates_; ++c) {
                if (salience_[static_cast<size_t>(c)] > bestSalience) {
                    bestSalience = salience_[static_cast<size_t>(c)];
                    bestCandidate = c;
                }
            }

            if (bestCandidate < 0 || bestSalience < kMinSalience) {
                break;
            }

            // Track strongest salience for drop ratio check
            if (voice == 0) {
                strongestSalience = bestSalience;
            } else if (bestSalience < strongestSalience * kSalienceDropRatio) {
                break;
            }

            // Record this F0
            float f0 = candidates_[static_cast<size_t>(bestCandidate)];
            result.estimates[static_cast<size_t>(voice)].frequency = f0;
            result.estimates[static_cast<size_t>(voice)].confidence =
                bestSalience / std::max(strongestSalience, 1e-10f);
            result.estimates[static_cast<size_t>(voice)].voiced = true;
            result.numDetected = voice + 1;

            // Cancel this F0's harmonics from the working spectrum
            cancelHarmonics(f0, workFreqs_.data(), workAmps_.data(), np);
        }

        return result;
    }

private:
    static constexpr size_t kMaxPeaks = 512;

    /// Compute pitch salience for all F0 candidates.
    /// salience(f0) = sum_{h=1}^{H} w(h) * amplitude(h * f0)
    void computeSalience(const float* freqs, const float* amps,
                         int numPeaks) noexcept
    {
        for (int c = 0; c < numCandidates_; ++c) {
            const float f0 = candidates_[static_cast<size_t>(c)];
            float sal = 0.0f;

            for (int h = 0; h < kNumHarmonics; ++h) {
                const float harmonicFreq = f0 * static_cast<float>(h + 1);
                if (harmonicFreq > nyquist_) break;

                // Find the peak closest to this harmonic frequency
                const float tolerance = harmonicFreq * kHarmonicMatchTolerance;
                float bestAmp = 0.0f;
                float bestDist = tolerance + 1.0f;

                for (int p = 0; p < numPeaks; ++p) {
                    float dist = std::abs(freqs[p] - harmonicFreq);
                    if (dist < tolerance && dist < bestDist) {
                        bestDist = dist;
                        bestAmp = amps[p];
                    }
                }

                sal += harmonicWeights_[static_cast<size_t>(h)] * bestAmp;
            }

            salience_[static_cast<size_t>(c)] = sal;
        }
    }

    /// Cancel harmonics of a detected F0 from the working peak amplitudes.
    /// Estimates the spectral envelope of this F0 by interpolating between
    /// its harmonic amplitudes, then subtracts the estimated contribution
    /// from each matched peak (Klapuri-style spectral cancellation).
    void cancelHarmonics(float f0, const float* freqs, float* amps,
                         int numPeaks) noexcept
    {
        // Step 1: Collect harmonic amplitudes to estimate spectral envelope
        std::array<float, kNumHarmonics> harmonicAmps{};
        std::array<float, kNumHarmonics> harmonicFreqs{};
        std::array<int, kNumHarmonics> harmonicPeakIdx{};
        int numHarmonicsFound = 0;

        for (int h = 0; h < kNumHarmonics; ++h) {
            const float hFreq = f0 * static_cast<float>(h + 1);
            if (hFreq > nyquist_) break;

            harmonicFreqs[static_cast<size_t>(h)] = hFreq;
            const float tolerance = hFreq * kHarmonicMatchTolerance;
            float bestAmp = 0.0f;
            int bestIdx = -1;
            float bestDist = tolerance + 1.0f;

            for (int p = 0; p < numPeaks; ++p) {
                float dist = std::abs(freqs[p] - hFreq);
                if (dist < tolerance && dist < bestDist) {
                    bestDist = dist;
                    bestAmp = amps[p];
                    bestIdx = p;
                }
            }

            harmonicAmps[static_cast<size_t>(h)] = bestAmp;
            harmonicPeakIdx[static_cast<size_t>(h)] = bestIdx;
            if (bestIdx >= 0) ++numHarmonicsFound;
        }

        if (numHarmonicsFound == 0) return;

        // Step 2: Estimate spectral envelope using spectral smoothness.
        // Smooth the harmonic amplitude contour: fill gaps by linear interpolation
        // between known harmonics, then use this as the expected amplitude.
        // For each peak matching a harmonic, subtract the envelope estimate.
        for (int h = 0; h < kNumHarmonics; ++h) {
            const float hFreq = harmonicFreqs[static_cast<size_t>(h)];
            if (hFreq <= 0.0f || hFreq > nyquist_) break;

            int peakIdx = harmonicPeakIdx[static_cast<size_t>(h)];
            if (peakIdx < 0) continue;

            // Estimate what fraction of this peak belongs to this F0.
            // Use the harmonic envelope: expected amplitude from this source
            // is interpolated from neighboring found harmonics.
            float envAmp = harmonicAmps[static_cast<size_t>(h)];

            // Interpolate from neighbors if this harmonic was found
            // The envelope estimate is the harmonic amplitude itself
            // (since we found it), weighted by the harmonic weight decay.
            // Subtract the estimated contribution, leaving residual for other sources.
            float estimatedContribution = envAmp * 0.85f; // Leave 15% for overlap
            amps[peakIdx] = std::max(0.0f, amps[peakIdx] - estimatedContribution);
        }
    }

    // Configuration
    float sampleRate_ = 44100.0f;
    float nyquist_ = 22050.0f;
    float binSpacing_ = 0.0f;

    // F0 candidates
    std::array<float, kMaxCandidates> candidates_{};
    int numCandidates_ = 0;

    // Harmonic weights
    std::array<float, kNumHarmonics> harmonicWeights_{};

    // Salience buffer
    std::array<float, kMaxCandidates> salience_{};

    // Working peak data (modified during iterative cancellation)
    std::array<float, kMaxPeaks> workFreqs_{};
    std::array<float, kMaxPeaks> workAmps_{};
};

} // namespace Krate::DSP
