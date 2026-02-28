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
// Extended: 084-arp-scale-mode (ScaleData, 16 ScaleTypes, variable-degree scales)
// ==============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>

namespace Krate::DSP {

// =============================================================================
// ScaleData Struct (084-arp-scale-mode T007)
// =============================================================================

/// Fixed-size scale interval data for variable-length scales.
/// Supports 5-note (pentatonic) through 12-note (chromatic) scales.
/// Zero-padded beyond degreeCount for unused slots.
struct ScaleData {
    std::array<int, 12> intervals{};  ///< Semitone offsets from root (e.g., {0,2,4,5,7,9,11} for Major)
    int degreeCount{0};               ///< Number of active degrees (5, 6, 7, 8, or 12)
};

// =============================================================================
// ScaleType Enum (084-arp-scale-mode T008)
// =============================================================================

/// Scale types for diatonic harmonization.
/// Each type maps to a ScaleData entry with variable-length intervals.
/// Chromatic (8) is a passthrough mode with no diatonic logic.
/// Existing values (0-8) are stable; new values (9-15) appended for 084-arp-scale-mode.
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
    // New values appended for 084-arp-scale-mode
    Locrian = 9,         ///< H-W-W-H-W-W-W  {0, 1, 3, 5, 6, 8, 10}
    MajorPentatonic = 10,///< W-W-m3-W-m3    {0, 2, 4, 7, 9}
    MinorPentatonic = 11,///< m3-W-W-m3-W    {0, 3, 5, 7, 10}
    Blues = 12,          ///< m3-W-H-H-m3-W  {0, 3, 5, 6, 7, 10}
    WholeTone = 13,      ///< W-W-W-W-W-W    {0, 2, 4, 6, 8, 10}
    DiminishedWH = 14,   ///< W-H-W-H-W-H-W-H {0, 2, 3, 5, 6, 8, 9, 11}
    DiminishedHW = 15,   ///< H-W-H-W-H-W-H-W {0, 1, 3, 4, 6, 7, 9, 10}
};

/// Total number of non-Chromatic scale types
inline constexpr int kNumNonChromaticScales = 15;

/// Total number of scale types including Chromatic
inline constexpr int kNumScaleTypes = 16;

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
    int scaleDegree;     ///< Target note's scale degree (0 to degreeCount-1), or -1 in Chromatic mode
    int octaveOffset;    ///< Number of complete octaves traversed by the diatonic interval
};

// =============================================================================
// Internal Constexpr Data Tables (084-arp-scale-mode T009, T010, T011)
// =============================================================================

namespace detail {

/// Scale interval tables: semitone offsets from root for all 16 scale types.
/// Indexed by static_cast<int>(ScaleType). Each entry has intervals and degreeCount.
/// Data from data-model.md E-003.
inline constexpr std::array<ScaleData, 16> kScaleIntervals = {{
    // Major (0):           7 degrees
    {{0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}, 7},
    // NaturalMinor (1):    7 degrees
    {{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}, 7},
    // HarmonicMinor (2):   7 degrees
    {{0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}, 7},
    // MelodicMinor (3):    7 degrees
    {{0, 2, 3, 5, 7, 9, 11, 0, 0, 0, 0, 0}, 7},
    // Dorian (4):          7 degrees
    {{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}, 7},
    // Mixolydian (5):      7 degrees
    {{0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0}, 7},
    // Phrygian (6):        7 degrees
    {{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}, 7},
    // Lydian (7):          7 degrees
    {{0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0}, 7},
    // Chromatic (8):      12 degrees
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 12},
    // Locrian (9):         7 degrees
    {{0, 1, 3, 5, 6, 8, 10, 0, 0, 0, 0, 0}, 7},
    // MajorPentatonic (10): 5 degrees
    {{0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0}, 5},
    // MinorPentatonic (11): 5 degrees
    {{0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}, 5},
    // Blues (12):           6 degrees
    {{0, 3, 5, 6, 7, 10, 0, 0, 0, 0, 0, 0}, 6},
    // WholeTone (13):      6 degrees
    {{0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0}, 6},
    // DiminishedWH (14):   8 degrees
    {{0, 2, 3, 5, 6, 8, 9, 11, 0, 0, 0, 0}, 8},
    // DiminishedHW (15):   8 degrees
    {{0, 1, 3, 4, 6, 7, 9, 10, 0, 0, 0, 0}, 8},
}};

/// Build reverse lookup table for a given scale type at compile time.
/// Maps each semitone offset (0-11) from root to nearest scale degree.
/// Uses degreeCount from kScaleIntervals instead of hardcoded 7.
/// Tie-breaking: round down (prefer lower degree index).
constexpr std::array<int, 12> buildReverseLookup(int scaleIndex) noexcept {
    std::array<int, 12> lookup = {};
    const auto& scaleData = kScaleIntervals[static_cast<size_t>(scaleIndex)];

    for (int semitone = 0; semitone < 12; ++semitone) {
        int bestDegree = 0;
        int bestDistance = 12;  // larger than any possible distance

        for (int d = 0; d < scaleData.degreeCount; ++d) {
            // Compute circular semitone distance
            int diff = semitone - scaleData.intervals[static_cast<size_t>(d)];
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

/// Precomputed reverse lookup tables for all 16 scale types.
inline constexpr std::array<std::array<int, 12>, 16> kReverseLookup = {{
    buildReverseLookup(0),   // Major
    buildReverseLookup(1),   // NaturalMinor
    buildReverseLookup(2),   // HarmonicMinor
    buildReverseLookup(3),   // MelodicMinor
    buildReverseLookup(4),   // Dorian
    buildReverseLookup(5),   // Mixolydian
    buildReverseLookup(6),   // Phrygian
    buildReverseLookup(7),   // Lydian
    buildReverseLookup(8),   // Chromatic
    buildReverseLookup(9),   // Locrian
    buildReverseLookup(10),  // MajorPentatonic
    buildReverseLookup(11),  // MinorPentatonic
    buildReverseLookup(12),  // Blues
    buildReverseLookup(13),  // WholeTone
    buildReverseLookup(14),  // DiminishedWH
    buildReverseLookup(15),  // DiminishedHW
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
    /// @param type One of the 16 supported scale types.
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
    // Core: Diatonic Interval Calculation (084-arp-scale-mode T012)
    // =========================================================================

    /// Compute the diatonic interval for an input MIDI note.
    ///
    /// For non-Chromatic scales: Finds the input note's scale degree (or nearest,
    /// for non-scale notes), applies the diatonic step offset, and computes the
    /// semitone shift to the target scale degree. Uses degreeCount for octave
    /// wrapping instead of hardcoded 7.
    ///
    /// For Chromatic mode: Returns diatonicSteps directly as the semitone shift
    /// with scaleDegree = -1.
    ///
    /// @param inputMidiNote Input MIDI note number (0-127 typical, any int accepted)
    /// @param diatonicSteps Scale degrees to shift. +1 = "2nd above", +2 = "3rd above",
    ///                      -2 = "3rd below", +degreeCount = octave, 0 = unison.
    /// @return DiatonicInterval with semitone shift, target note, scale degree, octave offset
    [[nodiscard]] DiatonicInterval calculate(int inputMidiNote, int diatonicSteps) const noexcept {
        // Chromatic mode: passthrough, diatonicSteps = raw semitones
        if (scale_ == ScaleType::Chromatic) {
            int target = std::clamp(inputMidiNote + diatonicSteps, kMinMidiNote, kMaxMidiNote);
            return DiatonicInterval{
                target - inputMidiNote,
                target,
                -1,
                0
            };
        }

        int scaleIdx = static_cast<int>(static_cast<uint8_t>(scale_));
        const auto& scaleData = detail::kScaleIntervals[static_cast<size_t>(scaleIdx)];
        const int degreeCount = scaleData.degreeCount;

        // Unison shortcut
        if (diatonicSteps == 0) {
            // Find the input's scale degree for the scaleDegree field
            int pitchClass = ((inputMidiNote % 12) + 12) % 12;
            int offset = ((pitchClass - rootNote_) % 12 + 12) % 12;
            int inputDegree = detail::kReverseLookup[static_cast<size_t>(scaleIdx)][static_cast<size_t>(offset)];
            return DiatonicInterval{0, inputMidiNote, inputDegree, 0};
        }

        // Diatonic mode
        const auto& reverseLookup = detail::kReverseLookup[static_cast<size_t>(scaleIdx)];

        // Step 1: Extract pitch class and compute offset from root
        int pitchClass = ((inputMidiNote % 12) + 12) % 12;
        int offset = ((pitchClass - rootNote_) % 12 + 12) % 12;

        // Step 2: Find nearest scale degree via reverse lookup (O(1))
        int inputDegree = reverseLookup[static_cast<size_t>(offset)];
        int inputSemitoneOffset = scaleData.intervals[static_cast<size_t>(inputDegree)];

        // Step 3: Compute target degree with octave wrapping
        // Use proper positive modulo for negative diatonicSteps
        int totalDegree = inputDegree + diatonicSteps;

        // Integer division toward zero for octaves, then adjust for negative modulo
        int octaves = 0;
        int targetDegree = 0;

        if (totalDegree >= 0) {
            octaves = totalDegree / degreeCount;
            targetDegree = totalDegree % degreeCount;
        } else {
            // For negative: need floor division behavior
            // e.g., -1 / degreeCount should give octaves=-1, targetDegree=degreeCount-1
            // C++ truncates toward zero, so we need manual floor division
            octaves = (totalDegree - (degreeCount - 1)) / degreeCount;  // floor division
            targetDegree = ((totalDegree % degreeCount) + degreeCount) % degreeCount;
        }

        // Step 4: Look up target degree's semitone offset
        int targetSemitoneOffset = scaleData.intervals[static_cast<size_t>(targetDegree)];

        // Step 5: Compute semitone shift
        int semitoneShift = targetSemitoneOffset - inputSemitoneOffset + octaves * 12;

        // Step 6: Compute target MIDI note and clamp
        int targetNote = inputMidiNote + semitoneShift;
        targetNote = std::clamp(targetNote, kMinMidiNote, kMaxMidiNote);

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
    ///
    /// Converts Hz to MIDI note (via frequencyToMidiNote()), rounds to nearest
    /// integer, then calls calculate(). Returns the semitone shift as a float
    /// for direct use with semitonesToRatio().
    ///
    /// @param inputFrequencyHz Input frequency in Hz (must be > 0)
    /// @param diatonicSteps Scale degrees to shift
    /// @return Semitone shift (float for potential sub-semitone adjustments)
    [[nodiscard]] float getSemitoneShift(float inputFrequencyHz, int diatonicSteps) const noexcept {
        int midiNote = static_cast<int>(std::round(frequencyToMidiNote(inputFrequencyHz)));
        return static_cast<float>(calculate(midiNote, diatonicSteps).semitones);
    }

    // =========================================================================
    // Queries: Scale Membership and Quantization (084-arp-scale-mode T013)
    // =========================================================================

    /// Get the scale degree of a MIDI note in the current key/scale.
    ///
    /// @param midiNote MIDI note number
    /// @return Scale degree (0 to degreeCount-1) if the note is in the scale, -1 if not.
    ///         Always returns -1 in Chromatic mode.
    [[nodiscard]] int getScaleDegree(int midiNote) const noexcept {
        if (scale_ == ScaleType::Chromatic) return -1;

        int pitchClass = ((midiNote % 12) + 12) % 12;
        int offset = ((pitchClass - rootNote_) % 12 + 12) % 12;

        int scaleIdx = static_cast<int>(static_cast<uint8_t>(scale_));
        const auto& scaleData = detail::kScaleIntervals[static_cast<size_t>(scaleIdx)];

        for (int d = 0; d < scaleData.degreeCount; ++d) {
            if (scaleData.intervals[static_cast<size_t>(d)] == offset) return d;
        }
        return -1;
    }

    /// Quantize a MIDI note to the nearest scale degree.
    ///
    /// Snaps the input note to the nearest note that belongs to the current
    /// key/scale. When equidistant between two scale notes, rounds down
    /// (toward the lower note).
    ///
    /// @param midiNote Input MIDI note number
    /// @return The nearest MIDI note that is in the current scale.
    ///         In Chromatic mode, returns the input unchanged.
    [[nodiscard]] int quantizeToScale(int midiNote) const noexcept {
        if (scale_ == ScaleType::Chromatic) return midiNote;

        int pitchClass = ((midiNote % 12) + 12) % 12;
        int offset = ((pitchClass - rootNote_) % 12 + 12) % 12;

        int scaleIdx = static_cast<int>(static_cast<uint8_t>(scale_));
        int nearestDegree = detail::kReverseLookup[static_cast<size_t>(scaleIdx)][static_cast<size_t>(offset)];
        int nearestOffset = detail::kScaleIntervals[static_cast<size_t>(scaleIdx)].intervals[static_cast<size_t>(nearestDegree)];

        int diff = nearestOffset - offset;  // snap direction: negative = down, positive = up
        return midiNote + diff;
    }

    // =========================================================================
    // Static: Scale Data Access (084-arp-scale-mode T014)
    // =========================================================================

    /// Get the ScaleData (intervals and degreeCount) for a scale type.
    [[nodiscard]] static constexpr ScaleData getScaleIntervals(ScaleType type) noexcept {
        return detail::kScaleIntervals[static_cast<size_t>(static_cast<uint8_t>(type))];
    }

private:
    int rootNote_ = 0;                       ///< Root key (0=C through 11=B)
    ScaleType scale_ = ScaleType::Major;     ///< Current scale type
};

} // namespace Krate::DSP
