#pragma once

// ==============================================================================
// Harmonic Types - Shared Data Types for Analysis/Synthesis Pipeline
// ==============================================================================
// Layer 2: Processors
// Spec: specs/115-innexus-m1-core-instrument/spec.md
// Covers: FR-013 (F0Estimate), FR-028 (Partial), FR-029 (HarmonicFrame)
//
// These structs are the core data contracts between all analysis and synthesis
// components in the Innexus plugin: YIN pitch detector, partial tracker,
// harmonic model builder, and harmonic oscillator bank.
// ==============================================================================

#include <array>
#include <cstddef>

namespace Krate::DSP {

/// Maximum number of tracked partials per frame (FR-026)
inline constexpr size_t kMaxPartials = 48;

/// Output of the YIN pitch detector for one analysis frame (FR-013).
/// Contains estimated frequency in Hz, confidence (0-1), and voiced/unvoiced
/// classification.
struct F0Estimate {
    float frequency = 0.0f;   ///< Hz (0 if unvoiced)
    float confidence = 0.0f;  ///< Detection confidence [0.0, 1.0]
    bool voiced = false;       ///< True if confidence > threshold
};

/// A single tracked harmonic component within a HarmonicFrame (FR-028).
/// Carries harmonic index, measured frequency, amplitude, phase,
/// relative frequency (frequency / F0), inharmonic deviation, stability score,
/// and tracking age.
struct Partial {
    int harmonicIndex = 0;           ///< 1-based harmonic number (0 = unassigned)
    float frequency = 0.0f;          ///< Measured frequency in Hz (actual, not idealized)
    float amplitude = 0.0f;          ///< Linear amplitude
    float phase = 0.0f;              ///< Phase in radians [-pi, pi]
    float relativeFrequency = 0.0f;  ///< frequency / F0 ratio
    float inharmonicDeviation = 0.0f;///< relativeFrequency - harmonicIndex
    float stability = 0.0f;          ///< Tracking confidence [0.0, 1.0]
    int age = 0;                     ///< Frames since track birth
};

/// A snapshot of the harmonic analysis at one point in time (FR-029).
/// The fundamental data unit flowing from analysis to synthesis.
/// Contains F0, F0 confidence, up to 48 partials with their attributes,
/// spectral centroid, brightness descriptor, noisiness estimate, and
/// smoothed global amplitude.
struct HarmonicFrame {
    float f0 = 0.0f;                              ///< Fundamental frequency (Hz)
    float f0Confidence = 0.0f;                     ///< From YIN detector [0.0, 1.0]
    std::array<Partial, kMaxPartials> partials{};   ///< Active partials
    int numPartials = 0;                           ///< Active count [0, 48]
    float spectralCentroid = 0.0f;                 ///< Amplitude-weighted mean freq (Hz)
    float brightness = 0.0f;                       ///< Perceptual brightness descriptor
    float noisiness = 0.0f;                        ///< Residual-to-harmonic ratio [0.0, 1.0]
    float globalAmplitude = 0.0f;                  ///< Smoothed RMS of source
};

} // namespace Krate::DSP
