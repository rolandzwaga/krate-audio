// ==============================================================================
// API Contract: HeldNoteBuffer & NoteSelector
// ==============================================================================
// Layer 1 Primitive - Arpeggiator note tracking and selection
//
// This file defines the public API contract for implementation.
// It is NOT the implementation file -- it specifies the interface that
// held_note_buffer.h must satisfy.
//
// Namespace: Krate::DSP
// Header: <krate/dsp/primitives/held_note_buffer.h>
// Dependencies: <krate/dsp/core/random.h> (Xorshift32)
// ==============================================================================

#pragma once

#include <krate/dsp/core/random.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP {

// =============================================================================
// Data Types
// =============================================================================

/// @brief A single held MIDI note with insertion-order tracking.
struct HeldNote {
    uint8_t note{0};          ///< MIDI note number (0-127)
    uint8_t velocity{0};      ///< MIDI velocity (1-127; 0 never stored)
    uint16_t insertOrder{0};  ///< Monotonically increasing counter for chronological ordering
};

/// @brief Arpeggiator pattern mode.
enum class ArpMode : uint8_t {
    Up = 0,       ///< Ascending pitch order, wrap at top
    Down,         ///< Descending pitch order, wrap at bottom
    UpDown,       ///< Ascending then descending, no endpoint repeat
    DownUp,       ///< Descending then ascending, no endpoint repeat
    Converge,     ///< Outside edges inward: lowest, highest, 2nd-lowest, ...
    Diverge,      ///< Center outward: center note(s), then expanding
    Random,       ///< Uniform random selection
    Walk,         ///< Random +/-1 step, clamped to bounds
    AsPlayed,     ///< Insertion order (chronological)
    Chord         ///< All notes simultaneously
};

/// @brief Octave expansion ordering mode.
enum class OctaveMode : uint8_t {
    Sequential = 0,  ///< Complete pattern at each octave before advancing
    Interleaved      ///< Each note at all octave transpositions before next note
};

/// @brief Result of NoteSelector::advance(). Fixed-capacity, no heap allocation.
struct ArpNoteResult {
    std::array<uint8_t, 32> notes{};       ///< MIDI note numbers (with octave offset applied)
    std::array<uint8_t, 32> velocities{};  ///< Corresponding velocities
    size_t count{0};                        ///< Number of valid entries (0 = empty, 1 = single, N = chord)
};

// =============================================================================
// HeldNoteBuffer
// =============================================================================

/// @brief Fixed-capacity (32) buffer tracking currently held MIDI notes.
///
/// Provides two views: pitch-sorted (ascending) for directional arp modes,
/// and insertion-ordered (chronological) for AsPlayed mode.
///
/// @par Real-Time Safety
/// All operations are noexcept and use zero heap allocation.
/// Designed for single-threaded (audio thread) access.
///
/// @par Usage
/// @code
/// HeldNoteBuffer buffer;
/// buffer.noteOn(60, 100);  // C3
/// buffer.noteOn(64, 90);   // E3
/// buffer.noteOn(67, 80);   // G3
///
/// auto pitched = buffer.byPitch();       // [60, 64, 67]
/// auto ordered = buffer.byInsertOrder(); // [60, 64, 67]
///
/// buffer.noteOff(64);  // Remove E3
/// // pitched: [60, 67], ordered: [60, 67]
/// @endcode
class HeldNoteBuffer {
public:
    static constexpr size_t kMaxNotes = 32;

    /// @brief Add or update a note in the buffer.
    /// If the note already exists, updates velocity without adding a duplicate.
    /// If the buffer is full and the note is new, silently ignores the request.
    /// @param note MIDI note number (0-127)
    /// @param velocity MIDI velocity (1-127)
    void noteOn(uint8_t note, uint8_t velocity) noexcept;

    /// @brief Remove a note from the buffer.
    /// If the note is not found, silently ignores the request.
    /// @param note MIDI note number (0-127)
    void noteOff(uint8_t note) noexcept;

    /// @brief Remove all notes and reset the insertion order counter.
    void clear() noexcept;

    /// @brief Get the number of currently held notes.
    [[nodiscard]] size_t size() const noexcept;

    /// @brief Check if the buffer is empty.
    [[nodiscard]] bool empty() const noexcept;

    /// @brief Get notes sorted by pitch (ascending MIDI note number).
    /// @return Span of held notes in pitch-ascending order
    [[nodiscard]] std::span<const HeldNote> byPitch() const noexcept;

    /// @brief Get notes in insertion order (chronological noteOn order).
    /// @return Span of held notes in the order they were added
    [[nodiscard]] std::span<const HeldNote> byInsertOrder() const noexcept;
};

// =============================================================================
// NoteSelector
// =============================================================================

/// @brief Stateful traversal engine for arpeggiator note selection.
///
/// Receives a `const HeldNoteBuffer&` on each advance() call and produces
/// the next note(s) to play according to the active ArpMode, octave range,
/// and OctaveMode. Holds NO reference to any buffer internally.
///
/// @par Real-Time Safety
/// All operations are noexcept and use zero heap allocation.
///
/// @par Usage
/// @code
/// NoteSelector selector;
/// selector.setMode(ArpMode::Up);
/// selector.setOctaveRange(2);
/// selector.setOctaveMode(OctaveMode::Sequential);
///
/// HeldNoteBuffer held;
/// held.noteOn(60, 100);
/// held.noteOn(64, 90);
///
/// auto r1 = selector.advance(held);  // C3 (note=60)
/// auto r2 = selector.advance(held);  // E3 (note=64)
/// auto r3 = selector.advance(held);  // C4 (note=72)
/// auto r4 = selector.advance(held);  // E4 (note=76)
/// @endcode
class NoteSelector {
public:
    /// @brief Construct with optional PRNG seed (for deterministic testing).
    /// @param seed PRNG seed value (default: 1)
    explicit NoteSelector(uint32_t seed = 1) noexcept;

    /// @brief Set the arp pattern mode. Calls reset() internally.
    /// @param mode One of the 10 ArpMode values
    void setMode(ArpMode mode) noexcept;

    /// @brief Set the octave range (1-4). 1 = no transposition.
    /// @param octaves Number of octaves (clamped to [1, 4])
    void setOctaveRange(int octaves) noexcept;

    /// @brief Set the octave ordering mode (Sequential or Interleaved).
    /// @param mode OctaveMode value
    void setOctaveMode(OctaveMode mode) noexcept;

    /// @brief Advance to the next note(s) in the pattern.
    ///
    /// @param held Current held notes (passed by const reference each call)
    /// @return ArpNoteResult with count=0 if held is empty, count=1 for single-note
    ///         modes, count=N for Chord mode
    [[nodiscard]] ArpNoteResult advance(const HeldNoteBuffer& held) noexcept;

    /// @brief Reset to the beginning of the current pattern.
    /// Resets index, direction, octave offset, and walk position.
    void reset() noexcept;
};

} // namespace Krate::DSP
