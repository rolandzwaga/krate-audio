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
inline constexpr size_t kMaxPartials = 96;

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
/// tracking age, source ID (for polyphonic analysis), and bandwidth.
struct Partial {
    int harmonicIndex = 0;           ///< 1-based harmonic number (0 = unassigned)
    float frequency = 0.0f;          ///< Measured frequency in Hz (actual, not idealized)
    float amplitude = 0.0f;          ///< Linear amplitude
    float phase = 0.0f;              ///< Phase in radians [-pi, pi]
    float relativeFrequency = 0.0f;  ///< frequency / F0 ratio
    float inharmonicDeviation = 0.0f;///< relativeFrequency - harmonicIndex
    float stability = 0.0f;          ///< Tracking confidence [0.0, 1.0]
    int age = 0;                     ///< Frames since track birth
    int sourceId = 0;                ///< Which F0 this partial belongs to (0 = unassigned/mono)
    float bandwidth = 0.0f;          ///< Per-partial noisiness [0=pure sine, 1=pure noise]
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
    int numPartials = 0;                           ///< Active count [0, 96]
    float spectralCentroid = 0.0f;                 ///< Amplitude-weighted mean freq (Hz)
    float brightness = 0.0f;                       ///< Perceptual brightness descriptor
    float noisiness = 0.0f;                        ///< Residual-to-harmonic ratio [0.0, 1.0]
    float globalAmplitude = 0.0f;                  ///< Smoothed RMS of source
};

// ==============================================================================
// Multi-Pitch Detection Types (Polyphonic Analysis)
// ==============================================================================

/// Maximum number of simultaneous F0 voices for polyphonic analysis
inline constexpr int kMaxPolyphonicVoices = 8;

/// Result of multi-pitch detection: up to kMaxPolyphonicVoices F0 estimates
/// ranked by salience/confidence.
struct MultiF0Result {
    std::array<F0Estimate, kMaxPolyphonicVoices> estimates{};
    int numDetected = 0;
};

/// Analysis mode for the pipeline
enum class AnalysisMode : int {
    Mono = 0,   ///< Always use YIN monophonic detection
    Poly = 1,   ///< Always use multi-pitch detection
    Auto = 2    ///< Switch based on YIN confidence
};

/// A polyphonic analysis frame containing multiple harmonic sources.
/// Each source has its own HarmonicFrame, plus any unassigned (inharmonic)
/// partials that don't fit any detected F0.
struct PolyphonicFrame {
    MultiF0Result f0s{};
    std::array<HarmonicFrame, kMaxPolyphonicVoices> sources{};
    int numSources = 0;

    /// Partials that didn't match any detected F0 (inharmonic/free partials)
    std::array<Partial, kMaxPartials> inharmonicPartials{};
    int numInharmonicPartials = 0;

    /// Global amplitude (same as mono path)
    float globalAmplitude = 0.0f;
};

} // namespace Krate::DSP
