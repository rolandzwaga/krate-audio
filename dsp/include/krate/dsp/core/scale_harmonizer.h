#pragma once

// ==============================================================================
// ScaleHarmonizer - Diatonic Interval Calculator (Layer 0: Core)
// ==============================================================================
// Computes musically correct diatonic intervals for harmonizer effects.
// Given an input MIDI note, key, scale, and diatonic step count, returns
// the correct semitone shift that maintains scale-correctness.
//
// All methods are noexcept and zero-allocation, suitable for real-time audio.
// Header-only implementation with constexpr lookup tables.
//
// Feature: 060-scale-interval-foundation
// ==============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// ScaleType Enum
// =============================================================================

/// Scale types for diatonic harmonization.
/// Each diatonic type (0-7) maps to a fixed array of 7 semitone offsets from root.
/// Chromatic (8) is a passthrough mode with no diatonic logic.
enum class ScaleType : uint8_t {
    Major = 0,           ///< Ionian: W-W-H-W-W-W-H  {0, 2, 4, 5, 7, 9, 11}
    NaturalMinor = 1,    ///< Aeolian: W-H-W-W-H-W-W  {0, 2, 3, 5, 7, 8, 10}
    HarmonicMinor = 2,   ///< W-H-W-W-H-A-H  {0, 2, 3, 5, 7, 8, 11}
    MelodicMinor = 3,    ///< Ascending: W-H-W-W-W-W-H  {0, 2, 3, 5, 7, 9, 11}
    Dorian = 4,          ///< W-H-W-W-W-H-W  {0, 2, 3, 5, 7, 9, 10}
    Mixolydian = 5,      ///< W-W-H-W-W-H-W  {0, 2, 4, 5, 7, 9, 10}
    Phrygian = 6,        ///< H-W-W-W-H-W-W  {0, 1, 3, 5, 7, 8, 10}
    Lydian = 7,          ///< W-W-W-H-W-W-H  {0, 2, 4, 6, 7, 9, 11}
    Chromatic = 8,       ///< All 12 semitones -- fixed shift, no diatonic logic
};

/// Total number of diatonic scale types (excludes Chromatic)
inline constexpr int kNumDiatonicScales = 8;

/// Total number of scale types including Chromatic
inline constexpr int kNumScaleTypes = 9;

/// Number of degrees in a diatonic scale
inline constexpr int kDegreesPerScale = 7;

/// Number of semitones in an octave
inline constexpr int kSemitonesPerOctave = 12;

// =============================================================================
// DiatonicInterval Result Struct
// =============================================================================

/// Result of a diatonic interval calculation.
///
/// Contains the semitone shift, absolute target MIDI note, target scale degree,
/// and octave offset. All fields are deterministic for a given input.
struct DiatonicInterval {
    int semitones;       ///< Actual semitone shift from input to target (can be negative)
    int targetNote;      ///< Absolute MIDI note of the target (0-127, clamped)
    int scaleDegree;     ///< Target note's scale degree (0-6), or -1 in Chromatic mode
    int octaveOffset;    ///< Number of complete octaves traversed by the diatonic interval
};

// =============================================================================
// Internal Constexpr Data Tables
// =============================================================================

namespace detail {

/// Scale interval tables: semitone offsets from root for each of the 8 diatonic scales.
/// Indexed by ScaleType (0-7).
inline constexpr std::array<std::array<int, 7>, 8> kScaleIntervals = {{
    {0, 2, 4, 5, 7, 9, 11},  // Major (Ionian)
    {0, 2, 3, 5, 7, 8, 10},  // NaturalMinor (Aeolian)
    {0, 2, 3, 5, 7, 8, 11},  // HarmonicMinor
    {0, 2, 3, 5, 7, 9, 11},  // MelodicMinor (ascending)
    {0, 2, 3, 5, 7, 9, 10},  // Dorian
    {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
    {0, 1, 3, 5, 7, 8, 10},  // Phrygian
    {0, 2, 4, 6, 7, 9, 11},  // Lydian
}};

/// Chromatic degenerate intervals (for getScaleIntervals with Chromatic type)
inline constexpr std::array<int, 7> kChromaticIntervals = {0, 1, 2, 3, 4, 5, 6};

/// Build reverse lookup table for a given scale type at compile time.
/// Maps each semitone offset (0-11) from root to nearest scale degree (0-6).
/// Tie-breaking: round down (prefer lower degree index).
constexpr std::array<int, 12> buildReverseLookup(int scaleIndex) noexcept {
    std::array<int, 12> lookup = {};
    const auto& intervals = kScaleIntervals[static_cast<size_t>(scaleIndex)];

    for (int semitone = 0; semitone < 12; ++semitone) {
        int bestDegree = 0;
        int bestDistance = 12;  // larger than any possible distance

        for (int d = 0; d < 7; ++d) {
            // Compute circular semitone distance
            int diff = semitone - intervals[static_cast<size_t>(d)];
            // Use positive modulo for circular distance
            int forward = ((diff % 12) + 12) % 12;
            int backward = (((-diff) % 12) + 12) % 12;
            int distance = (forward < backward) ? forward : backward;

            if (distance < bestDistance) {
                bestDistance = distance;
                bestDegree = d;
            }
            // On tie (distance == bestDistance), keep the earlier degree (round down)
        }

        lookup[static_cast<size_t>(semitone)] = bestDegree;
    }

    return lookup;
}

/// Precomputed reverse lookup tables for all 8 diatonic scales.
inline constexpr std::array<std::array<int, 12>, 8> kReverseLookup = {{
    buildReverseLookup(0),  // Major
    buildReverseLookup(1),  // NaturalMinor
    buildReverseLookup(2),  // HarmonicMinor
    buildReverseLookup(3),  // MelodicMinor
    buildReverseLookup(4),  // Dorian
    buildReverseLookup(5),  // Mixolydian
    buildReverseLookup(6),  // Phrygian
    buildReverseLookup(7),  // Lydian
}};

} // namespace detail

// =============================================================================
// ScaleHarmonizer Class
// =============================================================================

/// @brief Diatonic interval calculator for harmonizer intelligence (Layer 0).
///
/// Given a key (root note), scale type, input MIDI note, and desired diatonic
/// interval, computes the correct semitone shift. The shift varies per input
/// note to maintain scale-correctness. For example, "3rd above" in C Major:
/// C -> E (+4 semitones, major 3rd), D -> F (+3 semitones, minor 3rd).
///
/// @par Thread Safety
/// Immutable after setKey()/setScale(). Safe for concurrent reads from audio
/// thread without synchronization.
///
/// @par Real-Time Safety
/// All methods are noexcept and perform zero heap allocations. Suitable for
/// per-sample use on the audio thread.
///
/// @par Layer
/// Layer 0 (Core) -- depends only on stdlib and other Layer 0 utilities.
class ScaleHarmonizer {
public:
    // =========================================================================
    // Configuration
    // =========================================================================

    /// Set the root key for the scale.
    /// @param rootNote Root note (0=C, 1=C#, 2=D, ..., 11=B). Values outside
    ///                 [0, 11] are wrapped via modulo 12.
    void setKey(int rootNote) noexcept {
        rootNote_ = ((rootNote % 12) + 12) % 12;
    }

    /// Set the scale type.
    /// @param type One of the 9 supported scale types.
    void setScale(ScaleType type) noexcept {
        scale_ = type;
    }

    // =========================================================================
    // Getters
    // =========================================================================

    /// Get the current root key (0-11).
    [[nodiscard]] int getKey() const noexcept {
        return rootNote_;
    }

    /// Get the current scale type.
    [[nodiscard]] ScaleType getScale() const noexcept {
        return scale_;
    }

    // =========================================================================
    // Core: Diatonic Interval Calculation
    // =========================================================================

    /// Compute the diatonic interval for an input MIDI note.
    ///
    /// For diatonic scales (Major through Lydian): Finds the input note's scale
    /// degree (or nearest, for non-scale notes), applies the diatonic step
    /// offset, and computes the semitone shift to the target scale degree.
    ///
    /// For Chromatic mode: Returns diatonicSteps directly as the semitone shift
    /// with scaleDegree = -1.
    ///
    /// @param inputMidiNote Input MIDI note number (0-127 typical, any int accepted)
    /// @param diatonicSteps Scale degrees to shift. +1 = "2nd above", +2 = "3rd above",
    ///                      -2 = "3rd below", +7 = octave, 0 = unison.
    /// @return DiatonicInterval with semitone shift, target note, scale degree, octave offset
    [[nodiscard]] DiatonicInterval calculate(int inputMidiNote, int diatonicSteps) const noexcept {
        // Chromatic mode: passthrough, diatonicSteps = raw semitones
        if (scale_ == ScaleType::Chromatic) {
            int target = std::clamp(inputMidiNote + diatonicSteps, 0, 127);
            return DiatonicInterval{
                target - inputMidiNote,
                target,
                -1,
                0
            };
        }

        // Unison shortcut
        if (diatonicSteps == 0) {
            // Find the input's scale degree for the scaleDegree field
            int pitchClass = ((inputMidiNote % 12) + 12) % 12;
            int offset = ((pitchClass - rootNote_) % 12 + 12) % 12;
            int scaleIdx = static_cast<int>(static_cast<uint8_t>(scale_));
            int inputDegree = detail::kReverseLookup[static_cast<size_t>(scaleIdx)][static_cast<size_t>(offset)];
            return DiatonicInterval{0, inputMidiNote, inputDegree, 0};
        }

        // Diatonic mode
        int scaleIdx = static_cast<int>(static_cast<uint8_t>(scale_));
        const auto& intervals = detail::kScaleIntervals[static_cast<size_t>(scaleIdx)];
        const auto& reverseLookup = detail::kReverseLookup[static_cast<size_t>(scaleIdx)];

        // Step 1: Extract pitch class and compute offset from root
        int pitchClass = ((inputMidiNote % 12) + 12) % 12;
        int offset = ((pitchClass - rootNote_) % 12 + 12) % 12;

        // Step 2: Find nearest scale degree via reverse lookup (O(1))
        int inputDegree = reverseLookup[static_cast<size_t>(offset)];
        int inputSemitoneOffset = intervals[static_cast<size_t>(inputDegree)];

        // Step 3: Compute target degree with octave wrapping
        // Use proper positive modulo for negative diatonicSteps
        int totalDegree = inputDegree + diatonicSteps;

        // Integer division toward zero for octaves, then adjust for negative modulo
        int octaves = 0;
        int targetDegree = 0;

        if (totalDegree >= 0) {
            octaves = totalDegree / 7;
            targetDegree = totalDegree % 7;
        } else {
            // For negative: need floor division behavior
            // e.g., -1 / 7 should give octaves=-1, targetDegree=6
            // C++ truncates toward zero, so: -1/7 = 0, -1%7 = -1
            // We want: octaves = -1, targetDegree = 6
            octaves = (totalDegree - 6) / 7;  // floor division
            targetDegree = ((totalDegree % 7) + 7) % 7;
        }

        // Step 4: Look up target degree's semitone offset
        int targetSemitoneOffset = intervals[static_cast<size_t>(targetDegree)];

        // Step 5: Compute semitone shift
        int semitoneShift = targetSemitoneOffset - inputSemitoneOffset + octaves * 12;

        // Step 6: Compute target MIDI note and clamp
        int targetNote = inputMidiNote + semitoneShift;
        targetNote = std::clamp(targetNote, 0, 127);

        // Recompute semitones after clamping to maintain invariant
        semitoneShift = targetNote - inputMidiNote;

        return DiatonicInterval{
            semitoneShift,
            targetNote,
            targetDegree,
            octaves
        };
    }

    // =========================================================================
    // Convenience: Frequency-Based Interface
    // =========================================================================

    /// Compute semitone shift from input frequency.
    /// Stub for Phase 9 implementation (FR-012).
    [[nodiscard]] float getSemitoneShift(float inputFrequencyHz, int diatonicSteps) const noexcept {
        // Stub -- will be implemented in Phase 9
        (void)inputFrequencyHz;
        (void)diatonicSteps;
        return 0.0f;
    }

    // =========================================================================
    // Queries: Scale Membership and Quantization
    // =========================================================================

    /// Get the scale degree of a MIDI note in the current key/scale.
    /// Stub for Phase 6 implementation (FR-010).
    [[nodiscard]] int getScaleDegree([[maybe_unused]] int midiNote) const noexcept {
        // Stub -- will be implemented in Phase 6
        return -1;
    }

    /// Quantize a MIDI note to the nearest scale degree.
    /// Stub for Phase 6 implementation (FR-011).
    [[nodiscard]] int quantizeToScale([[maybe_unused]] int midiNote) const noexcept {
        // Stub -- will be implemented in Phase 6
        return midiNote;
    }

    // =========================================================================
    // Static: Scale Data Access
    // =========================================================================

    /// Get the 7 semitone offsets for a diatonic scale type.
    [[nodiscard]] static constexpr std::array<int, 7> getScaleIntervals(ScaleType type) noexcept {
        if (type == ScaleType::Chromatic) {
            return detail::kChromaticIntervals;
        }
        return detail::kScaleIntervals[static_cast<size_t>(static_cast<uint8_t>(type))];
    }

private:
    int rootNote_ = 0;                       ///< Root key (0=C through 11=B)
    ScaleType scale_ = ScaleType::Major;     ///< Current scale type
};

} // namespace Krate::DSP
