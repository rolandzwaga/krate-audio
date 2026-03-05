#pragma once

// ==============================================================================
// Harmonic Snapshot - Normalized Timbral Storage for Harmonic Memory
// ==============================================================================
// Layer 2: Processors
// Spec: specs/119-harmonic-memory/spec.md
// Covers: FR-001 (HarmonicSnapshot), FR-010 (MemorySlot)
//
// A HarmonicSnapshot stores a complete, self-contained representation of a
// timbral moment in normalized harmonic domain. Used by the Harmonic Memory
// system for capture, recall, and preset persistence.
// ==============================================================================

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include <array>
#include <cmath>

namespace Krate::DSP {

/// @brief Complete snapshot of a timbral moment in normalized harmonic domain (FR-001).
///
/// Stores all data needed to reconstruct a HarmonicFrame and ResidualFrame for
/// playback at any MIDI pitch. Frequencies are stored as ratios relative to F0,
/// amplitudes are L2-normalized (FR-002). Both relativeFreqs and inharmonicDeviation
/// are stored for fast access despite redundancy (FR-003). Phases are stored for
/// forward compatibility (FR-004).
struct HarmonicSnapshot {
    float f0Reference = 0.0f;                              ///< Source F0 at capture (informational)
    int numPartials = 0;                                   ///< Active partial count (<= kMaxPartials)

    std::array<float, kMaxPartials> relativeFreqs{};       ///< freq_n / F0 ratios
    std::array<float, kMaxPartials> normalizedAmps{};      ///< L2-normalized amplitudes
    std::array<float, kMaxPartials> phases{};              ///< Phase in radians at capture
    std::array<float, kMaxPartials> inharmonicDeviation{}; ///< relativeFreq_n - harmonicIndex

    std::array<float, kResidualBands> residualBands{};     ///< Spectral envelope of residual
    float residualEnergy = 0.0f;                           ///< Overall residual level

    float globalAmplitude = 0.0f;                          ///< Source loudness (informational)
    float spectralCentroid = 0.0f;                         ///< Perceptual metadata for UI/sorting
    float brightness = 0.0f;                               ///< Perceptual metadata
};

/// @brief A memory slot pairing a HarmonicSnapshot with an occupancy flag (FR-010).
///
/// Pre-allocated as a fixed-size array of 8 in the Processor. No heap allocation
/// during capture or recall.
struct MemorySlot {
    HarmonicSnapshot snapshot{};
    bool occupied = false;
};

/// @brief Capture the current harmonic and residual state into a HarmonicSnapshot.
///
/// L2-normalizes amplitudes and stores relative frequencies.
/// Real-time safe: no allocation, no locks.
///
/// @param frame Source harmonic frame (pre-filter)
/// @param residual Source residual frame
/// @return Populated HarmonicSnapshot
inline HarmonicSnapshot captureSnapshot(const HarmonicFrame& frame,
                                        const ResidualFrame& residual) noexcept
{
    HarmonicSnapshot snap{};

    snap.f0Reference = frame.f0;
    snap.numPartials = frame.numPartials;
    snap.globalAmplitude = frame.globalAmplitude;
    snap.spectralCentroid = frame.spectralCentroid;
    snap.brightness = frame.brightness;

    // Extract per-partial fields and accumulate sum of squares for L2 norm
    float sumSquares = 0.0f;
    const int n = std::min(frame.numPartials, static_cast<int>(kMaxPartials));
    for (int i = 0; i < n; ++i)
    {
        const auto& p = frame.partials[static_cast<size_t>(i)];
        snap.relativeFreqs[static_cast<size_t>(i)] = p.relativeFrequency;
        snap.normalizedAmps[static_cast<size_t>(i)] = p.amplitude;
        snap.phases[static_cast<size_t>(i)] = p.phase;
        snap.inharmonicDeviation[static_cast<size_t>(i)] = p.inharmonicDeviation;
        sumSquares += p.amplitude * p.amplitude;
    }

    // L2-normalize amplitudes (FR-002)
    if (sumSquares > 0.0f)
    {
        const float invNorm = 1.0f / std::sqrt(sumSquares);
        for (int i = 0; i < n; ++i)
        {
            snap.normalizedAmps[static_cast<size_t>(i)] *= invNorm;
        }
    }

    // Copy residual data
    snap.residualBands = residual.bandEnergies;
    snap.residualEnergy = residual.totalEnergy;

    return snap;
}

/// @brief Reconstruct a HarmonicFrame and ResidualFrame from a stored HarmonicSnapshot.
///
/// Sets recalled partials as fully confident and stable (f0Confidence=1, stability=1, age=1).
/// Derives harmonicIndex from relativeFreqs and inharmonicDeviation.
/// Real-time safe: no allocation, no locks.
///
/// @param snap Source snapshot
/// @param[out] frame Reconstructed harmonic frame
/// @param[out] residual Reconstructed residual frame
inline void recallSnapshotToFrame(const HarmonicSnapshot& snap,
                                  HarmonicFrame& frame,
                                  ResidualFrame& residual) noexcept
{
    // Zero-initialize outputs
    frame = HarmonicFrame{};
    residual = ResidualFrame{};

    frame.f0 = snap.f0Reference;
    frame.f0Confidence = 1.0f;
    frame.numPartials = snap.numPartials;
    frame.globalAmplitude = snap.globalAmplitude;
    frame.spectralCentroid = snap.spectralCentroid;
    frame.brightness = snap.brightness;

    const int n = std::min(snap.numPartials, static_cast<int>(kMaxPartials));
    for (int i = 0; i < n; ++i)
    {
        auto& p = frame.partials[static_cast<size_t>(i)];
        p.relativeFrequency = snap.relativeFreqs[static_cast<size_t>(i)];
        p.amplitude = snap.normalizedAmps[static_cast<size_t>(i)];
        p.phase = snap.phases[static_cast<size_t>(i)];
        p.inharmonicDeviation = snap.inharmonicDeviation[static_cast<size_t>(i)];

        // Derive harmonicIndex: round(relativeFreq - inharmonicDeviation), clamp >= 1
        float idealHarmonic = snap.relativeFreqs[static_cast<size_t>(i)]
                            - snap.inharmonicDeviation[static_cast<size_t>(i)];
        int harmonicIdx = static_cast<int>(std::round(idealHarmonic));
        p.harmonicIndex = std::max(harmonicIdx, 1);

        p.stability = 1.0f;
        p.age = 1;

        // Compute absolute frequency from relative
        p.frequency = p.relativeFrequency * snap.f0Reference;
    }

    // Restore residual
    residual.bandEnergies = snap.residualBands;
    residual.totalEnergy = snap.residualEnergy;
}

} // namespace Krate::DSP
