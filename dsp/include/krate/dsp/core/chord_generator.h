#pragma once

// ==============================================================================
// ChordGenerator - Chord generation via scale-degree stacking (Layer 0: Core)
// ==============================================================================
// Builds chords by stacking diatonic 3rds from a root note using
// ScaleHarmonizer. In chromatic mode, fixed intervals are used.
// All functions are noexcept, header-only, and real-time safe.
//
// Feature: arp-chord-lane
// ==============================================================================

#include <krate/dsp/core/scale_harmonizer.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// Enums
// =============================================================================

/// @brief Chord type for per-step chord generation.
enum class ChordType : uint8_t {
    None = 0,   ///< Single note (no chord)
    Dyad = 1,   ///< 2 notes (root + 5th)
    Triad = 2,  ///< 3 notes (root + 3rd + 5th)
    Seventh = 3,///< 4 notes (root + 3rd + 5th + 7th)
    Ninth = 4,  ///< 5 notes (root + 3rd + 5th + 7th + 9th)
    kCount = 5
};

/// @brief Inversion type for chord voicing.
enum class InversionType : uint8_t {
    Root = 0,   ///< No inversion (root position)
    First = 1,  ///< 1st inversion (bottom note up an octave)
    Second = 2, ///< 2nd inversion (bottom 2 notes up an octave)
    Third = 3,  ///< 3rd inversion (bottom 3 notes up an octave)
    kCount = 4
};

/// @brief Voicing mode for chord register spread.
enum class VoicingMode : uint8_t {
    Close = 0,   ///< Close voicing (notes within one octave)
    Drop2 = 1,   ///< Drop-2: second-from-top note dropped an octave
    Spread = 2,  ///< Spread: alternate notes up an octave
    Random = 3,  ///< Random octave displacement per note
    kCount = 4
};

// =============================================================================
// ChordResult
// =============================================================================

/// @brief Result of chord generation: up to 5 MIDI notes.
struct ChordResult {
    std::array<uint8_t, 5> notes{};  ///< MIDI note numbers
    size_t count{0};                  ///< Number of valid notes (1-5)
};

// =============================================================================
// Chord Generation Functions (all noexcept, pure, header-only)
// =============================================================================

/// @brief Generate chord notes from a root note using scale-degree stacking.
///
/// Diatonic (non-chromatic): stacks scale degrees above root.
///   Dyad:  root, root+4 degrees (5th)
///   Triad: root, root+2 degrees (3rd), root+4 degrees (5th)
///   7th:   root, +2, +4, +6
///   9th:   root, +2, +4, +6, +8
///
/// Chromatic: uses fixed semitone intervals.
///   Dyad:  root, +7
///   Triad: root, +4, +7
///   7th:   root, +4, +7, +10
///   9th:   root, +4, +7, +10, +14
///
/// @param rootNote    MIDI root note (0-127)
/// @param type        Chord type (None returns single root)
/// @param harmonizer  ScaleHarmonizer configured with key/scale
/// @return ChordResult with generated notes
[[nodiscard]] inline ChordResult generateChordNotes(
    uint8_t rootNote,
    ChordType type,
    const ScaleHarmonizer& harmonizer) noexcept
{
    ChordResult result;
    result.notes[0] = rootNote;
    result.count = 1;

    if (type == ChordType::None) {
        return result;
    }

    // Define degree offsets for diatonic stacking
    // Dyad: root + 5th (degree +4)
    // Triad: root + 3rd (degree +2) + 5th (degree +4)
    // 7th: root + 3rd + 5th + 7th (degree +6)
    // 9th: root + 3rd + 5th + 7th + 9th (degree +8)

    // Chromatic intervals
    constexpr std::array<int, 4> kChromaticIntervals = {4, 7, 10, 14};
    // Diatonic degree offsets (ordered: 3rd, 5th, 7th, 9th)
    constexpr std::array<int, 4> kDiatonicDegrees = {2, 4, 6, 8};

    // Determine how many extra notes based on chord type
    size_t extraNotes = 0;
    switch (type) {
        case ChordType::Dyad:    extraNotes = 1; break;
        case ChordType::Triad:   extraNotes = 2; break;
        case ChordType::Seventh: extraNotes = 3; break;
        case ChordType::Ninth:   extraNotes = 4; break;
        default: return result;
    }

    if (type == ChordType::Dyad) {
        // Dyad is root + 5th, not root + 3rd
        if (harmonizer.getScale() == ScaleType::Chromatic) {
            int note = static_cast<int>(rootNote) + 7;
            result.notes[1] = static_cast<uint8_t>(std::clamp(note, 0, 127));
        } else {
            auto interval = harmonizer.calculate(static_cast<int>(rootNote), 4);
            result.notes[1] = static_cast<uint8_t>(
                std::clamp(interval.targetNote, 0, 127));
        }
        result.count = 2;
    } else {
        // Triad, 7th, 9th: stack 3rd, 5th, 7th, 9th
        for (size_t i = 0; i < extraNotes; ++i) {
            if (harmonizer.getScale() == ScaleType::Chromatic) {
                int note = static_cast<int>(rootNote) + kChromaticIntervals[i];
                result.notes[i + 1] = static_cast<uint8_t>(
                    std::clamp(note, 0, 127));
            } else {
                auto interval = harmonizer.calculate(
                    static_cast<int>(rootNote), kDiatonicDegrees[i]);
                result.notes[i + 1] = static_cast<uint8_t>(
                    std::clamp(interval.targetNote, 0, 127));
            }
        }
        result.count = 1 + extraNotes;
    }

    return result;
}

/// @brief Apply inversion to a chord by rotating bottom notes up an octave.
///
/// 1st inversion: bottom note moved up 12 semitones
/// 2nd inversion: bottom 2 notes moved up 12 semitones
/// 3rd inversion: bottom 3 notes moved up 12 semitones (only for 7th/9th)
///
/// Notes are clamped to MIDI range [0, 127].
/// Inversions beyond chord size are clamped (e.g., 3rd inversion on triad
/// acts as 2nd inversion).
///
/// @param chord  ChordResult to modify in-place
/// @param inv    Inversion type
inline void applyInversion(ChordResult& chord, InversionType inv) noexcept {
    if (inv == InversionType::Root || chord.count <= 1) {
        return;
    }

    // Number of notes to rotate up = min(inversion index, count - 1)
    size_t rotations = static_cast<size_t>(inv);
    rotations = std::min(rotations, chord.count - 1);

    for (size_t r = 0; r < rotations; ++r) {
        // Move bottom note up an octave
        int raised = static_cast<int>(chord.notes[0]) + 12;
        raised = std::clamp(raised, 0, 127);

        // Shift all notes down one position
        for (size_t i = 0; i + 1 < chord.count; ++i) {
            chord.notes[i] = chord.notes[i + 1];
        }
        chord.notes[chord.count - 1] = static_cast<uint8_t>(raised);
    }
}

/// @brief Apply voicing transformation to chord register spread.
///
/// Close: no-op (notes in close position)
/// Drop-2: second-from-top note dropped down an octave
/// Spread: odd-indexed notes (1st, 3rd) raised up an octave
/// Random: each non-root note may be raised an octave based on rngState
///
/// Notes are clamped to MIDI range [0, 127].
///
/// @param chord     ChordResult to modify in-place
/// @param mode      Voicing mode
/// @param rngState  Random state for Random mode (XOR-shifted internally)
inline void applyVoicing(ChordResult& chord, VoicingMode mode,
                         uint32_t rngState) noexcept {
    if (mode == VoicingMode::Close || chord.count <= 1) {
        return;
    }

    switch (mode) {
        case VoicingMode::Drop2: {
            // Drop the second-from-top note down an octave
            if (chord.count >= 2) {
                size_t dropIdx = chord.count - 2;
                int dropped = static_cast<int>(chord.notes[dropIdx]) - 12;
                chord.notes[dropIdx] = static_cast<uint8_t>(
                    std::clamp(dropped, 0, 127));
            }
            break;
        }
        case VoicingMode::Spread: {
            // Raise odd-indexed notes up an octave
            for (size_t i = 1; i < chord.count; i += 2) {
                int raised = static_cast<int>(chord.notes[i]) + 12;
                chord.notes[i] = static_cast<uint8_t>(
                    std::clamp(raised, 0, 127));
            }
            break;
        }
        case VoicingMode::Random: {
            // Each non-root note: 50% chance to go up an octave
            uint32_t state = rngState;
            for (size_t i = 1; i < chord.count; ++i) {
                // Simple xorshift32 step
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                if (state & 1) {
                    int raised = static_cast<int>(chord.notes[i]) + 12;
                    chord.notes[i] = static_cast<uint8_t>(
                        std::clamp(raised, 0, 127));
                }
            }
            break;
        }
        default:
            break;
    }
}

} // namespace Krate::DSP
