// ==============================================================================
// Layer 2: DSP Processor - Multi-Source Harmonic Sieve
// ==============================================================================
// Assigns spectral peaks to multiple F0 sources for polyphonic analysis.
// Each peak is assigned to the F0 whose harmonic series it best fits.
//
// Algorithm:
// 1. For each detected F0, build its expected harmonic series
// 2. For each peak, compute how well it fits each F0's harmonic series
// 3. Assign peak to best-fitting F0 (lowest frequency error, weighted by
//    amplitude consistency with that source's spectral envelope)
// 4. Unmatched peaks become "inharmonic" partials (sourceId = 0)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, fixed-size arrays)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends on Layer 0 core)
// ==============================================================================

#pragma once

#include <krate/dsp/processors/harmonic_types.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Krate::DSP {

/// @brief Assigns spectral peaks to multiple F0 sources for polyphonic analysis.
///
/// After multi-pitch detection provides K fundamental frequencies, this sieve
/// assigns each tracked partial to the F0 whose harmonic series it best fits.
class MultiSourceSieve {
public:
    /// Maximum harmonic index to check per source
    static constexpr int kMaxHarmonicCheck = 48;

    /// Base tolerance for frequency matching (fraction of F0)
    static constexpr float kBaseToleranceFactor = 0.06f;

    MultiSourceSieve() noexcept = default;

    /// @brief Prepare the sieve.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        nyquist_ = sampleRate_ * 0.5f;
    }

    /// Weight for amplitude consistency in scoring (vs frequency error)
    static constexpr float kAmplitudeConsistencyWeight = 0.3f;

    /// @brief Assign partials to sources based on multi-F0 detection.
    ///
    /// Modifies the partials in-place, setting their sourceId and harmonicIndex
    /// relative to the assigned F0. Uses a two-pass approach:
    /// Pass 1: Build per-source spectral envelopes from unambiguous assignments
    /// Pass 2: Resolve ambiguous assignments using amplitude consistency
    ///
    /// @param partials Array of tracked partials (from PartialTracker)
    /// @param numPartials Number of active partials
    /// @param f0s Multi-F0 detection result
    void assignSources(
        std::array<Partial, kMaxPartials>& partials,
        int numPartials,
        const MultiF0Result& f0s) noexcept
    {
        if (f0s.numDetected <= 0 || numPartials <= 0) {
            return;
        }

        const int numSources = f0s.numDetected;

        // Pass 1: Find candidate matches for each partial
        // Store per-partial: which sources match and at what harmonic/score
        struct CandidateMatch {
            int source = 0;    // 1-based source ID
            int harmonic = 0;
            float freqScore = 0.0f; // Normalized frequency error [0,1]
        };
        // Max 2 candidate matches per partial (for conflict resolution)
        static constexpr int kMaxCandidatesPerPartial = 8;
        std::array<std::array<CandidateMatch, kMaxCandidatesPerPartial>, kMaxPartials> candidates{};
        std::array<int, kMaxPartials> numCandidates{};

        for (int p = 0; p < numPartials; ++p) {
            numCandidates[static_cast<size_t>(p)] = 0;
            const auto& partial = partials[static_cast<size_t>(p)];
            if (partial.frequency <= 0.0f) continue;

            for (int s = 0; s < numSources; ++s) {
                const float f0 = f0s.estimates[static_cast<size_t>(s)].frequency;
                if (f0 <= 0.0f) continue;

                const int maxH = std::min(
                    kMaxHarmonicCheck,
                    static_cast<int>(nyquist_ / f0));

                // Find best matching harmonic for this source
                float bestErr = std::numeric_limits<float>::max();
                int bestH = 0;

                for (int h = 1; h <= maxH; ++h) {
                    const float harmonicFreq = static_cast<float>(h) * f0;
                    const float tolerance =
                        kBaseToleranceFactor * std::sqrt(static_cast<float>(h)) * f0;
                    const float error = std::abs(partial.frequency - harmonicFreq);

                    if (error < tolerance && error < bestErr) {
                        bestErr = error;
                        bestH = h;
                    }
                }

                if (bestH > 0) {
                    float tolerance =
                        kBaseToleranceFactor * std::sqrt(static_cast<float>(bestH)) *
                        f0;
                    int nc = numCandidates[static_cast<size_t>(p)];
                    if (nc < kMaxCandidatesPerPartial) {
                        candidates[static_cast<size_t>(p)][static_cast<size_t>(nc)] = {
                            s + 1, bestH, bestErr / tolerance};
                        numCandidates[static_cast<size_t>(p)] = nc + 1;
                    }
                }
            }
        }

        // Pass 2: Build initial spectral envelopes from unambiguous assignments
        // (partials that match exactly one source)
        // envelope[source][harmonic] = amplitude
        std::array<std::array<float, kMaxHarmonicCheck + 1>, kMaxPolyphonicVoices> envelope{};
        std::array<std::array<bool, kMaxHarmonicCheck + 1>, kMaxPolyphonicVoices> envelopeSet{};

        for (int p = 0; p < numPartials; ++p) {
            if (numCandidates[static_cast<size_t>(p)] == 1) {
                // Unambiguous: assign immediately and record in envelope
                const auto& cand = candidates[static_cast<size_t>(p)][0];
                int srcIdx = cand.source - 1;
                if (srcIdx >= 0 && srcIdx < numSources &&
                    cand.harmonic > 0 && cand.harmonic <= kMaxHarmonicCheck) {
                    envelope[static_cast<size_t>(srcIdx)]
                            [static_cast<size_t>(cand.harmonic)] =
                        partials[static_cast<size_t>(p)].amplitude;
                    envelopeSet[static_cast<size_t>(srcIdx)]
                               [static_cast<size_t>(cand.harmonic)] = true;
                }
            }
        }

        // Pass 3: Assign all partials, using amplitude consistency for conflicts
        for (int p = 0; p < numPartials; ++p) {
            auto& partial = partials[static_cast<size_t>(p)];
            if (partial.frequency <= 0.0f) continue;

            int nc = numCandidates[static_cast<size_t>(p)];
            if (nc == 0) {
                partial.sourceId = 0; // Inharmonic
                continue;
            }

            if (nc == 1) {
                // Unambiguous assignment
                const auto& cand = candidates[static_cast<size_t>(p)][0];
                partial.sourceId = cand.source;
                partial.harmonicIndex = cand.harmonic;
            } else {
                // Ambiguous: score using frequency error + amplitude consistency
                float bestScore = std::numeric_limits<float>::max();
                int bestSource = 0;
                int bestHarmonic = 0;

                for (int c = 0; c < nc; ++c) {
                    const auto& cand =
                        candidates[static_cast<size_t>(p)][static_cast<size_t>(c)];
                    int srcIdx = cand.source - 1;

                    // Amplitude consistency: how well does this partial's amplitude
                    // fit the source's spectral envelope?
                    float ampConsistency = 0.0f;
                    if (srcIdx >= 0 && srcIdx < numSources &&
                        cand.harmonic > 0 && cand.harmonic <= kMaxHarmonicCheck) {
                        // Estimate expected amplitude from neighboring harmonics
                        float expectedAmp = estimateEnvelopeAt(
                            envelope[static_cast<size_t>(srcIdx)],
                            envelopeSet[static_cast<size_t>(srcIdx)],
                            cand.harmonic);
                        if (expectedAmp > 1e-10f) {
                            float ratio = partial.amplitude / expectedAmp;
                            ampConsistency = std::abs(1.0f - ratio);
                            ampConsistency = std::min(ampConsistency, 1.0f);
                        }
                    }

                    float score = (1.0f - kAmplitudeConsistencyWeight) * cand.freqScore +
                                  kAmplitudeConsistencyWeight * ampConsistency;

                    if (score < bestScore) {
                        bestScore = score;
                        bestSource = cand.source;
                        bestHarmonic = cand.harmonic;
                    }
                }

                partial.sourceId = bestSource;
                partial.harmonicIndex = bestHarmonic;
            }

            // Update relative frequency for the assigned source
            if (partial.sourceId > 0) {
                const float f0 =
                    f0s.estimates[static_cast<size_t>(partial.sourceId - 1)].frequency;
                if (f0 > 0.0f) {
                    partial.relativeFrequency = partial.frequency / f0;
                    partial.inharmonicDeviation =
                        partial.relativeFrequency -
                        static_cast<float>(partial.harmonicIndex);
                }

                // Update envelope with this assignment
                int srcIdx = partial.sourceId - 1;
                if (srcIdx >= 0 && srcIdx < numSources &&
                    partial.harmonicIndex > 0 &&
                    partial.harmonicIndex <= kMaxHarmonicCheck) {
                    envelope[static_cast<size_t>(srcIdx)]
                            [static_cast<size_t>(partial.harmonicIndex)] =
                        partial.amplitude;
                    envelopeSet[static_cast<size_t>(srcIdx)]
                               [static_cast<size_t>(partial.harmonicIndex)] = true;
                }
            }
        }
    }

    /// @brief Build a PolyphonicFrame from assigned partials and F0 results.
    ///
    /// Separates the partials by sourceId into per-source HarmonicFrames
    /// and collects unassigned partials as inharmonic.
    ///
    /// @param partials Array of tracked partials with sourceId set
    /// @param numPartials Number of active partials
    /// @param f0s Multi-F0 detection result
    /// @param globalAmplitude Global RMS amplitude
    /// @return PolyphonicFrame with separated sources
    [[nodiscard]] PolyphonicFrame buildPolyphonicFrame(
        const std::array<Partial, kMaxPartials>& partials,
        int numPartials,
        const MultiF0Result& f0s,
        float globalAmplitude) noexcept
    {
        PolyphonicFrame frame{};
        frame.f0s = f0s;
        frame.numSources = f0s.numDetected;
        frame.globalAmplitude = globalAmplitude;

        // Distribute partials to their respective sources
        for (int p = 0; p < numPartials; ++p) {
            const auto& partial = partials[static_cast<size_t>(p)];

            if (partial.sourceId == 0) {
                // Inharmonic partial
                if (frame.numInharmonicPartials < static_cast<int>(kMaxPartials)) {
                    frame.inharmonicPartials[
                        static_cast<size_t>(frame.numInharmonicPartials)] = partial;
                    ++frame.numInharmonicPartials;
                }
            } else {
                // Assigned to a source (1-based)
                int srcIdx = partial.sourceId - 1;
                if (srcIdx >= 0 && srcIdx < f0s.numDetected) {
                    auto& src = frame.sources[static_cast<size_t>(srcIdx)];
                    if (src.numPartials < static_cast<int>(kMaxPartials)) {
                        src.partials[static_cast<size_t>(src.numPartials)] = partial;
                        ++src.numPartials;
                    }
                }
            }
        }

        // Set F0 info for each source frame
        for (int s = 0; s < f0s.numDetected; ++s) {
            auto& src = frame.sources[static_cast<size_t>(s)];
            src.f0 = f0s.estimates[static_cast<size_t>(s)].frequency;
            src.f0Confidence = f0s.estimates[static_cast<size_t>(s)].confidence;
            src.globalAmplitude = globalAmplitude; // Shared for now
        }

        return frame;
    }

private:
    /// Estimate expected amplitude at a given harmonic index by interpolating
    /// from the nearest known harmonics in the source's envelope.
    [[nodiscard]] static float estimateEnvelopeAt(
        const std::array<float, kMaxHarmonicCheck + 1>& envelope,
        const std::array<bool, kMaxHarmonicCheck + 1>& isSet,
        int targetH) noexcept
    {
        // Find nearest lower and upper known harmonics
        int lowerH = -1, upperH = -1;
        float lowerAmp = 0.0f, upperAmp = 0.0f;

        for (int h = targetH - 1; h >= 1; --h) {
            if (isSet[static_cast<size_t>(h)]) {
                lowerH = h;
                lowerAmp = envelope[static_cast<size_t>(h)];
                break;
            }
        }
        for (int h = targetH + 1; h <= kMaxHarmonicCheck; ++h) {
            if (isSet[static_cast<size_t>(h)]) {
                upperH = h;
                upperAmp = envelope[static_cast<size_t>(h)];
                break;
            }
        }

        if (lowerH >= 0 && upperH >= 0) {
            // Linear interpolation between neighbors
            float t = static_cast<float>(targetH - lowerH) /
                      static_cast<float>(upperH - lowerH);
            return lowerAmp + t * (upperAmp - lowerAmp);
        } else if (lowerH >= 0) {
            // Extrapolate from lower (typical spectral rolloff: ~1/h)
            float ratio = static_cast<float>(lowerH) / static_cast<float>(targetH);
            return lowerAmp * ratio;
        } else if (upperH >= 0) {
            // Extrapolate from upper
            float ratio = static_cast<float>(upperH) / static_cast<float>(targetH);
            return upperAmp / ratio;
        }

        return 0.0f; // No envelope data available
    }

    float sampleRate_ = 44100.0f;
    float nyquist_ = 22050.0f;
};

} // namespace Krate::DSP
