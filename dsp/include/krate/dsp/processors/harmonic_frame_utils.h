#pragma once

// ==============================================================================
// Harmonic Frame Utilities - Interpolation and Filtering for Harmonic Frames
// ==============================================================================
// Layer 2: Processors
// Spec: specs/118-musical-control-layer/spec.md
// Covers: FR-011 to FR-015 (morph interpolation), FR-020 to FR-025 (harmonic filter)
//
// Utility functions for interpolating between HarmonicFrame/ResidualFrame pairs
// (morph) and applying per-partial amplitude masks (harmonic filter). These are
// reusable building blocks for Innexus M4 (Musical Control Layer), M5 (Harmonic
// Memory), P4 (Evolution Engine), and P6 (Multi-Source Blending).
// ==============================================================================

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <algorithm>
#include <array>

namespace Krate::DSP {

/// Interpolate between two HarmonicFrames.
/// - Amplitudes: lerp (missing side = 0.0)
/// - RelativeFrequencies: lerp (missing side = harmonicIndex)
/// - Frequency: lerp (missing side = f0 * harmonicIndex)
/// - InharmonicDeviation: lerp (missing side = 0.0)
/// - Stability: lerp (missing side = 1.0)
/// - Phase: NOT interpolated (oscillator bank is phase-continuous via MCF)
/// - HarmonicIndex, Age: copy from dominant source (b when t > 0.5)
/// - Metadata: lerp of globalAmplitude, spectralCentroid, brightness, noisiness
/// - numPartials: max of both frames
/// @param a Source frame (State A, frozen snapshot)
/// @param b Destination frame (State B, live analysis)
/// @param t Morph position [0.0, 1.0]: 0.0 = fully A, 1.0 = fully B
/// @return Interpolated HarmonicFrame
inline HarmonicFrame lerpHarmonicFrame(
    const HarmonicFrame& a, const HarmonicFrame& b, float t) noexcept
{
    HarmonicFrame result{};
    const float oneMinusT = 1.0f - t;

    // numPartials = max of both frames
    const int maxPartials = std::max(a.numPartials, b.numPartials);
    result.numPartials = maxPartials;

    // Determine dominant source for non-interpolated fields
    const bool bDominant = (t > 0.5f);

    for (int i = 0; i < maxPartials; ++i)
    {
        auto& rp = result.partials[static_cast<size_t>(i)];
        const auto& ap = a.partials[static_cast<size_t>(i)];
        const auto& bp = b.partials[static_cast<size_t>(i)];

        const bool inA = (i < a.numPartials);
        const bool inB = (i < b.numPartials);

        // Amplitude: lerp, missing side = 0.0f
        const float ampA = inA ? ap.amplitude : 0.0f;
        const float ampB = inB ? bp.amplitude : 0.0f;
        rp.amplitude = oneMinusT * ampA + t * ampB;

        // RelativeFrequency: lerp, missing side = float(harmonicIndex)
        // For the missing side, use the other side's harmonicIndex to get
        // the ideal harmonic ratio as the default
        const float relFreqA = inA
            ? ap.relativeFrequency
            : static_cast<float>(inB ? bp.harmonicIndex : (i + 1));
        const float relFreqB = inB
            ? bp.relativeFrequency
            : static_cast<float>(inA ? ap.harmonicIndex : (i + 1));
        rp.relativeFrequency = oneMinusT * relFreqA + t * relFreqB;

        // Frequency & inharmonicDeviation: lerp (missing side = ideal harmonic)
        const float freqA = inA ? ap.frequency : (a.f0 * static_cast<float>(i + 1));
        const float freqB = inB ? bp.frequency : (b.f0 * static_cast<float>(i + 1));
        rp.frequency = oneMinusT * freqA + t * freqB;

        const float devA = inA ? ap.inharmonicDeviation : 0.0f;
        const float devB = inB ? bp.inharmonicDeviation : 0.0f;
        rp.inharmonicDeviation = oneMinusT * devA + t * devB;

        // Stability: lerp (missing side = 1.0)
        const float stabA = inA ? ap.stability : 1.0f;
        const float stabB = inB ? bp.stability : 1.0f;
        rp.stability = oneMinusT * stabA + t * stabB;

        // Phase, harmonicIndex, age: copy from dominant source
        // Phase is NOT interpolated — the oscillator bank maintains
        // phase continuity through its MCF recurrence.
        if (bDominant && inB)
        {
            rp.phase = bp.phase;
            rp.harmonicIndex = bp.harmonicIndex;
            rp.age = bp.age;
        }
        else if (inA)
        {
            rp.phase = ap.phase;
            rp.harmonicIndex = ap.harmonicIndex;
            rp.age = ap.age;
        }
        else if (inB)
        {
            rp.phase = bp.phase;
            rp.harmonicIndex = bp.harmonicIndex;
            rp.age = bp.age;
        }
        else
        {
            rp.harmonicIndex = i + 1;
        }
    }

    // Metadata: lerp
    result.f0 = oneMinusT * a.f0 + t * b.f0;
    result.f0Confidence = oneMinusT * a.f0Confidence + t * b.f0Confidence;
    result.globalAmplitude = oneMinusT * a.globalAmplitude + t * b.globalAmplitude;
    result.spectralCentroid = oneMinusT * a.spectralCentroid + t * b.spectralCentroid;
    result.brightness = oneMinusT * a.brightness + t * b.brightness;
    result.noisiness = oneMinusT * a.noisiness + t * b.noisiness;

    return result;
}

/// Interpolate between two ResidualFrames.
/// - bandEnergies: per-band lerp
/// - totalEnergy: lerp
/// - transientFlag: from dominant source (b when t > 0.5, a otherwise)
/// @param a Source residual frame (State A)
/// @param b Destination residual frame (State B)
/// @param t Morph position [0.0, 1.0]
/// @return Interpolated ResidualFrame
inline ResidualFrame lerpResidualFrame(
    const ResidualFrame& a, const ResidualFrame& b, float t) noexcept
{
    ResidualFrame result{};
    const float oneMinusT = 1.0f - t;

    for (size_t i = 0; i < kResidualBands; ++i)
        result.bandEnergies[i] = oneMinusT * a.bandEnergies[i] + t * b.bandEnergies[i];

    result.totalEnergy = oneMinusT * a.totalEnergy + t * b.totalEnergy;
    result.transientFlag = (t > 0.5f) ? b.transientFlag : a.transientFlag;

    return result;
}

/// Compute harmonic filter mask for a given filter type index.
/// Writes mask values to the output array (1.0 = pass, 0.0 = attenuate).
/// Index is 0-based into the partials array; harmonicIndex is 1-based.
/// @param filterType 0=AllPass, 1=OddOnly, 2=EvenOnly, 3=LowHarmonics, 4=HighHarmonics
/// @param partials The partials array from the HarmonicFrame
/// @param numPartials Number of active partials
/// @param mask Output array to fill with mask values
/// @note Takes int (not HarmonicFilterType enum) to keep shared DSP free of plugin types.
inline void computeHarmonicMask(
    int filterType,
    const std::array<Partial, kMaxPartials>& partials,
    int numPartials,
    std::array<float, kMaxPartials>& mask) noexcept
{
    // Initialize all to 1.0 (pass-through)
    mask.fill(1.0f);

    const int count = std::min(numPartials, static_cast<int>(kMaxPartials));

    switch (filterType)
    {
    case 0: // All-Pass
        // Already filled with 1.0
        break;

    case 1: // Odd Only
        for (int i = 0; i < count; ++i)
        {
            const int idx = partials[static_cast<size_t>(i)].harmonicIndex;
            mask[static_cast<size_t>(i)] = (idx % 2 == 1) ? 1.0f : 0.0f;
        }
        break;

    case 2: // Even Only
        for (int i = 0; i < count; ++i)
        {
            const int idx = partials[static_cast<size_t>(i)].harmonicIndex;
            mask[static_cast<size_t>(i)] = (idx % 2 == 0) ? 1.0f : 0.0f;
        }
        break;

    case 3: // Low Harmonics: mask(n) = clamp(8.0 / n, 0.0, 1.0)
        for (int i = 0; i < count; ++i)
        {
            const int idx = partials[static_cast<size_t>(i)].harmonicIndex;
            if (idx > 0)
                mask[static_cast<size_t>(i)] =
                    std::clamp(8.0f / static_cast<float>(idx), 0.0f, 1.0f);
            else
                mask[static_cast<size_t>(i)] = 1.0f;
        }
        break;

    case 4: // High Harmonics: mask(n) = clamp(n / 8.0, 0.0, 1.0)
        for (int i = 0; i < count; ++i)
        {
            const int idx = partials[static_cast<size_t>(i)].harmonicIndex;
            if (idx > 0)
                mask[static_cast<size_t>(i)] =
                    std::clamp(static_cast<float>(idx) / 8.0f, 0.0f, 1.0f);
            else
                mask[static_cast<size_t>(i)] = 1.0f;
        }
        break;

    default:
        // Unknown filter type: treat as All-Pass
        break;
    }
}

/// Apply a pre-computed harmonic mask to a frame's partial amplitudes in-place.
/// Only modifies amplitude; all other partial fields are preserved.
/// @param frame The HarmonicFrame to modify
/// @param mask The mask array (1.0 = pass, 0.0 = attenuate)
inline void applyHarmonicMask(
    HarmonicFrame& frame,
    const std::array<float, kMaxPartials>& mask) noexcept
{
    for (int i = 0; i < frame.numPartials; ++i)
        frame.partials[static_cast<size_t>(i)].amplitude *= mask[static_cast<size_t>(i)];
}

} // namespace Krate::DSP
