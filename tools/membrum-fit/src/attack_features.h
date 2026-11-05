#pragma once

#include "types.h"

#include <span>

namespace MembrumFit {

// Compute attack-window features over [onset, attackEnd] plus a small lookback
// for pre-onset auto-correlation. All features are SDK-free scalar floats so
// the classifier can reason about them without spectral context.
AttackFeatures extractAttackFeatures(std::span<const float> samples,
                                     const SegmentedSample& seg,
                                     double sampleRate);

// --- Primitive feature helpers (reusable, also unit-tested individually) ---

// Log-attack time in log10 seconds (Peeters 2011: 2% threshold -> 90% peak).
float computeLogAttackTime(std::span<const float> attackWindow, double sampleRate);

// Spectral flatness = geomean / arithmean of |X[k]|. [0, 1].
float computeSpectralFlatness(std::span<const float> window, double sampleRate);

// Spectral centroid in Hz for one window.
float computeSpectralCentroid(std::span<const float> window, double sampleRate);

// Autocorrelation peak in lag range [minLagSamples, maxLagSamples].
float computeAutocorrelationPeak(std::span<const float> signal,
                                 std::size_t minLagSamples,
                                 std::size_t maxLagSamples);

// Inharmonicity (sum of relative deviations of peaks from harmonic series).
float computeInharmonicity(std::span<const float> window,
                           double sampleRate,
                           float fundamentalHz);

// Coarse pitch estimate using parabolic-interpolation peak on FFT magnitudes.
// Returns 0 when no usable peak is found.
float estimateFundamental(std::span<const float> window, double sampleRate);

}  // namespace MembrumFit
