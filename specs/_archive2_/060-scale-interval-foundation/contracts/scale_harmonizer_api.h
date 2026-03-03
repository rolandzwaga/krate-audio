// ==============================================================================
// API Contract: ScaleHarmonizer
// Layer 0: Core Utilities
// Feature: 060-scale-interval-foundation
// ==============================================================================
// This is a CONTRACT file -- it defines the public API that the implementation
// must conform to. It is NOT the implementation file.
// ==============================================================================

#pragma once

#include <array>
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
    HarmonicMinor = 2,   ///< W-H-W-W-H-A-H  {0, 2, 3, 5, 7, 8, 11}  (A = augmented second = 3 semitones)
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
// ScaleHarmonizer Class
// =============================================================================

/// @brief Diatonic interval calculator for harmonizer intelligence (Layer 0).
///
/// Given a key (root note), scale type, input MIDI note, and desired diatonic
/// interval, computes the correct semitone shift. The shift varies per input
/// note to maintain scale-correctness. For example, "3rd above" in C Major:
/// C -> E (+4 semitones, major 3rd), D -> F (+3 semitones, minor 3rd).
///
/// @par Usage
/// @code
/// ScaleHarmonizer harm;
/// harm.setKey(0);                          // C
/// harm.setScale(ScaleType::Major);
/// auto result = harm.calculate(60, +2);    // C4 + 3rd above = E4 (+4 semitones)
/// auto shift = result.semitones;           // +4
/// float ratio = semitonesToRatio(shift);   // Use for pitch shifting
/// @endcode
///
/// @par Thread Safety
/// Immutable after setKey()/setScale(). Safe for concurrent reads from audio
/// thread without synchronization. The host guarantees parameter changes are
/// applied between process blocks.
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
    void setKey(int rootNote) noexcept;

    /// Set the scale type.
    /// @param type One of the 9 supported scale types.
    void setScale(ScaleType type) noexcept;

    // =========================================================================
    // Getters
    // =========================================================================

    /// Get the current root key (0-11).
    [[nodiscard]] int getKey() const noexcept;

    /// Get the current scale type.
    [[nodiscard]] ScaleType getScale() const noexcept;

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
    [[nodiscard]] DiatonicInterval calculate(int inputMidiNote, int diatonicSteps) const noexcept;

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
    [[nodiscard]] float getSemitoneShift(float inputFrequencyHz, int diatonicSteps) const noexcept;

    // =========================================================================
    // Queries: Scale Membership and Quantization
    // =========================================================================

    /// Get the scale degree of a MIDI note in the current key/scale.
    ///
    /// @param midiNote MIDI note number
    /// @return Scale degree (0-6) if the note is in the scale, -1 if not.
    ///         Always returns -1 in Chromatic mode.
    [[nodiscard]] int getScaleDegree(int midiNote) const noexcept;

    /// Quantize a MIDI note to the nearest scale degree.
    ///
    /// Snaps the input note to the nearest note that belongs to the current
    /// key/scale. When equidistant between two scale notes, rounds down
    /// (toward the lower note).
    ///
    /// @param midiNote Input MIDI note number
    /// @return The nearest MIDI note that is in the current scale.
    ///         In Chromatic mode, returns the input unchanged.
    [[nodiscard]] int quantizeToScale(int midiNote) const noexcept;

    // =========================================================================
    // Static: Scale Data Access
    // =========================================================================

    /// Get the 7 semitone offsets for a diatonic scale type.
    ///
    /// @param type A diatonic scale type (Major through Lydian).
    ///             For Chromatic, returns {0, 1, 2, 3, 4, 5, 6} as a
    ///             degenerate case (not meaningful for interval calculation).
    /// @return Array of 7 semitone offsets from the root.
    [[nodiscard]] static constexpr std::array<int, 7> getScaleIntervals(ScaleType type) noexcept;

private:
    int rootNote_ = 0;                       ///< Root key (0=C through 11=B)
    ScaleType scale_ = ScaleType::Major;     ///< Current scale type
};

} // namespace Krate::DSP
