// ==============================================================================
// Layer 1: DSP Primitives
// held_note_buffer.h - Arpeggiator note tracking and selection
// ==============================================================================
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocation, no locks, no exceptions, no I/O
//
// Constitution Principle III: Modern C++ Standards
// - constexpr where possible, std::array over C-style arrays
//
// Constitution Principle IX: Layered DSP Architecture
// - Layer 1: Only depends on Layer 0 (Xorshift32 from core/random.h)
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
// SC-003: zero heap allocation -- no dynamic containers used
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
// SC-003: zero heap allocation -- no dynamic containers used
class HeldNoteBuffer {
public:
    static constexpr size_t kMaxNotes = 32;

    /// @brief Add or update a note in the buffer.
    /// If the note already exists, updates velocity without adding a duplicate.
    /// If the buffer is full and the note is new, silently ignores the request.
    /// @param note MIDI note number (0-127)
    /// @param velocity MIDI velocity (1-127)
    void noteOn(uint8_t note, uint8_t velocity) noexcept {
        // Check for duplicate -- linear scan entries_[0..size_-1]
        for (size_t i = 0; i < size_; ++i) {
            if (entries_[i].note == note) {
                // Update velocity in both arrays
                entries_[i].velocity = velocity;
                // Find in pitchSorted_ and update there too
                for (size_t j = 0; j < size_; ++j) {
                    if (pitchSorted_[j].note == note) {
                        pitchSorted_[j].velocity = velocity;
                        break;
                    }
                }
                return;
            }
        }

        // Not found -- add new note if capacity available
        if (size_ >= kMaxNotes) {
            return;  // Silently ignore if full
        }

        HeldNote newNote{note, velocity, nextInsertOrder_};
        ++nextInsertOrder_;

        // Append to entries_ (insertion order)
        entries_[size_] = newNote;

        // Insertion-sort into pitchSorted_ (ascending by note)
        size_t insertPos = size_;
        for (size_t i = 0; i < size_; ++i) {
            if (pitchSorted_[i].note > note) {
                insertPos = i;
                break;
            }
        }
        // Shift elements right to make room
        for (size_t i = size_; i > insertPos; --i) {
            pitchSorted_[i] = pitchSorted_[i - 1];
        }
        pitchSorted_[insertPos] = newNote;

        ++size_;
    }

    /// @brief Remove a note from the buffer.
    /// If the note is not found, silently ignores the request.
    /// @param note MIDI note number (0-127)
    void noteOff(uint8_t note) noexcept {
        // Linear scan entries_ to find pitch
        size_t entryIdx = size_;  // sentinel: not found
        for (size_t i = 0; i < size_; ++i) {
            if (entries_[i].note == note) {
                entryIdx = i;
                break;
            }
        }

        if (entryIdx == size_) {
            return;  // Not found -- silently ignore
        }

        // Shift-left to remove from entries_
        for (size_t i = entryIdx; i + 1 < size_; ++i) {
            entries_[i] = entries_[i + 1];
        }

        // Find and remove from pitchSorted_
        size_t pitchIdx = 0;
        for (size_t i = 0; i < size_; ++i) {
            if (pitchSorted_[i].note == note) {
                pitchIdx = i;
                break;
            }
        }
        for (size_t i = pitchIdx; i + 1 < size_; ++i) {
            pitchSorted_[i] = pitchSorted_[i + 1];
        }

        --size_;
    }

    /// @brief Remove all notes and reset the insertion order counter.
    void clear() noexcept {
        size_ = 0;
        nextInsertOrder_ = 0;
    }

    /// @brief Get the number of currently held notes.
    [[nodiscard]] size_t size() const noexcept {
        return size_;
    }

    /// @brief Check if the buffer is empty.
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    /// @brief Get notes sorted by pitch (ascending MIDI note number).
    /// @return Span of held notes in pitch-ascending order
    [[nodiscard]] std::span<const HeldNote> byPitch() const noexcept {
        return {pitchSorted_.data(), size_};
    }

    /// @brief Get notes in insertion order (chronological noteOn order).
    /// @return Span of held notes in the order they were added
    [[nodiscard]] std::span<const HeldNote> byInsertOrder() const noexcept {
        return {entries_.data(), size_};
    }

private:
    std::array<HeldNote, kMaxNotes> entries_{};       ///< Notes in insertion order
    std::array<HeldNote, kMaxNotes> pitchSorted_{};   ///< Notes sorted by pitch (ascending)
    size_t size_{0};
    uint16_t nextInsertOrder_{0};
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
// SC-003: zero heap allocation -- no dynamic containers used
class NoteSelector {
public:
    /// @brief Construct with optional PRNG seed (for deterministic testing).
    explicit NoteSelector(uint32_t seed = 1) noexcept
        : rng_(seed) {}

    /// @brief Set the arp pattern mode. Calls reset() internally.
    void setMode(ArpMode mode) noexcept {
        mode_ = mode;
        reset();
    }

    /// @brief Set the octave range (1-4). 1 = no transposition.
    void setOctaveRange(int octaves) noexcept {
        octaveRange_ = std::clamp(octaves, 1, 4);
    }

    /// @brief Set the octave ordering mode (Sequential or Interleaved).
    void setOctaveMode(OctaveMode mode) noexcept {
        octaveMode_ = mode;
    }

    /// @brief Advance to the next note(s) in the pattern.
    [[nodiscard]] ArpNoteResult advance(const HeldNoteBuffer& held) noexcept {
        if (held.empty()) {
            return {};
        }

        ArpNoteResult result;
        const auto pitched = held.byPitch();
        const auto size = pitched.size();

        switch (mode_) {
            case ArpMode::Up: {
                advanceUp(pitched, size, result);
                break;
            }
            case ArpMode::Down: {
                advanceDown(pitched, size, result);
                break;
            }
            case ArpMode::UpDown: {
                advanceUpDown(pitched, size, result);
                break;
            }
            case ArpMode::DownUp: {
                advanceDownUp(pitched, size, result);
                break;
            }
            case ArpMode::Converge: {
                advanceConverge(pitched, size, result);
                break;
            }
            case ArpMode::Diverge: {
                advanceDiverge(pitched, size, result);
                break;
            }
            case ArpMode::Random: {
                advanceRandom(pitched, size, result);
                break;
            }
            case ArpMode::Walk: {
                advanceWalk(pitched, size, result);
                break;
            }
            case ArpMode::AsPlayed: {
                advanceAsPlayed(held, size, result);
                break;
            }
            case ArpMode::Chord: {
                // FR-020: Return all held notes simultaneously, no octave transposition
                const auto pitchView = held.byPitch();
                result.count = size;
                for (size_t i = 0; i < size; ++i) {
                    result.notes[i] = pitchView[i].note;
                    result.velocities[i] = pitchView[i].velocity;
                }
                break;
            }
        }

        return result;
    }

    /// @brief Reset to the beginning of the current pattern.
    void reset() noexcept {
        noteIndex_ = 0;
        pingPongPos_ = 0;
        octaveOffset_ = 0;
        walkIndex_ = 0;
        convergeStep_ = 0;
        direction_ = 1;
    }

private:
    /// @brief Apply octave transposition with MIDI clamping (FR-028).
    static uint8_t applyOctave(uint8_t baseNote, int octaveOffset) noexcept {
        return static_cast<uint8_t>(std::min(127, static_cast<int>(baseNote) + octaveOffset * 12));
    }

    /// @brief Advance octave state for Sequential mode after pattern index wraps.
    /// For ascending modes: octaveOffset_ goes 0, 1, ..., octaveRange_-1, 0, ...
    void advanceOctaveSequentialAscending() noexcept {
        ++octaveOffset_;
        if (octaveOffset_ >= octaveRange_) {
            octaveOffset_ = 0;
        }
    }

    /// @brief Up mode: ascending pitch order with octave support.
    void advanceUp(std::span<const HeldNote> pitched, size_t size,
                   ArpNoteResult& result) noexcept {
        noteIndex_ = std::min(noteIndex_, size - 1);
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[noteIndex_].note, octaveOffset_);
            result.velocities[0] = pitched[noteIndex_].velocity;
            result.count = 1;
            noteIndex_ = (noteIndex_ + 1) % size;
            if (noteIndex_ == 0) {
                advanceOctaveSequentialAscending();
            }
        } else {
            // Interleaved: each note at all octaves before next
            result.notes[0] = applyOctave(pitched[noteIndex_].note, octaveOffset_);
            result.velocities[0] = pitched[noteIndex_].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
                noteIndex_ = (noteIndex_ + 1) % size;
            }
        }
    }

    /// @brief Down mode: descending pitch order with octave support.
    /// Down mode reverses the octave order: starts at the highest octave and descends.
    /// octaveOffset_ still counts 0, 1, 2, ... but the effective offset is reversed.
    void advanceDown(std::span<const HeldNote> pitched, size_t size,
                     ArpNoteResult& result) noexcept {
        noteIndex_ = std::min(noteIndex_, size - 1);
        size_t downIdx = size - 1 - noteIndex_;
        // Reverse octave: offset 0 maps to octaveRange_-1, offset 1 maps to octaveRange_-2, etc.
        int effectiveOctave = (octaveRange_ - 1) - octaveOffset_;
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[downIdx].note, effectiveOctave);
            result.velocities[0] = pitched[downIdx].velocity;
            result.count = 1;
            noteIndex_ = (noteIndex_ + 1) % size;
            if (noteIndex_ == 0) {
                advanceOctaveSequentialAscending();
            }
        } else {
            // Interleaved: each note at all octaves (descending) before next
            result.notes[0] = applyOctave(pitched[downIdx].note, effectiveOctave);
            result.velocities[0] = pitched[downIdx].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
                noteIndex_ = (noteIndex_ + 1) % size;
            }
        }
    }

    /// @brief UpDown mode: ping-pong ascending/descending with octave support.
    void advanceUpDown(std::span<const HeldNote> pitched, size_t size,
                       ArpNoteResult& result) noexcept {
        if (size == 1) {
            result.notes[0] = applyOctave(pitched[0].note, octaveOffset_);
            result.velocities[0] = pitched[0].velocity;
            result.count = 1;
            // For single note with octaves, advance octave each call
            if (octaveRange_ > 1) {
                advanceOctaveSequentialAscending();
            }
            return;
        }
        size_t cycleLen = 2 * (size - 1);
        size_t pos = pingPongPos_ % cycleLen;
        size_t idx = (pos < size) ? pos : (2 * (size - 1) - pos);
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            pingPongPos_ = (pingPongPos_ + 1) % cycleLen;
            if (pingPongPos_ == 0) {
                advanceOctaveSequentialAscending();
            }
        } else {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
                pingPongPos_ = (pingPongPos_ + 1) % cycleLen;
            }
        }
    }

    /// @brief DownUp mode: ping-pong descending/ascending with octave support.
    void advanceDownUp(std::span<const HeldNote> pitched, size_t size,
                       ArpNoteResult& result) noexcept {
        if (size == 1) {
            result.notes[0] = applyOctave(pitched[0].note, octaveOffset_);
            result.velocities[0] = pitched[0].velocity;
            result.count = 1;
            if (octaveRange_ > 1) {
                advanceOctaveSequentialAscending();
            }
            return;
        }
        size_t cycleLen = 2 * (size - 1);
        size_t pos = (pingPongPos_ + (size - 1)) % cycleLen;
        size_t idx = (pos < size) ? pos : (2 * (size - 1) - pos);
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            pingPongPos_ = (pingPongPos_ + 1) % cycleLen;
            if (pingPongPos_ == 0) {
                advanceOctaveSequentialAscending();
            }
        } else {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
                pingPongPos_ = (pingPongPos_ + 1) % cycleLen;
            }
        }
    }

    /// @brief Converge mode: outside-in pattern with octave support.
    void advanceConverge(std::span<const HeldNote> pitched, size_t size,
                         ArpNoteResult& result) noexcept {
        size_t step = convergeStep_ % size;
        size_t idx;
        if (step % 2 == 0) {
            idx = step / 2;              // From bottom
        } else {
            idx = size - 1 - (step / 2); // From top
        }
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            convergeStep_ = (convergeStep_ + 1) % size;
            if (convergeStep_ == 0) {
                advanceOctaveSequentialAscending();
            }
        } else {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
                convergeStep_ = (convergeStep_ + 1) % size;
            }
        }
    }

    /// @brief Diverge mode: center-out pattern with octave support.
    void advanceDiverge(std::span<const HeldNote> pitched, size_t size,
                        ArpNoteResult& result) noexcept {
        size_t step = convergeStep_ % size;
        size_t idx;
        if (size % 2 == 0) {
            size_t half = size / 2;
            if (step % 2 == 0) {
                idx = half - 1 - step / 2;
            } else {
                idx = half + (step - 1) / 2;
            }
        } else {
            size_t center = size / 2;
            if (step == 0) {
                idx = center;
            } else if (step % 2 == 1) {
                idx = center - (step + 1) / 2;
            } else {
                idx = center + step / 2;
            }
        }
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            convergeStep_ = (convergeStep_ + 1) % size;
            if (convergeStep_ == 0) {
                advanceOctaveSequentialAscending();
            }
        } else {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
                convergeStep_ = (convergeStep_ + 1) % size;
            }
        }
    }

    /// @brief Random mode: uniform random selection with octave support.
    void advanceRandom(std::span<const HeldNote> pitched, size_t size,
                       ArpNoteResult& result) noexcept {
        size_t idx = rng_.next() % size;
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            // Random doesn't have a pattern cycle, advance octave per call
            advanceOctaveSequentialAscending();
        } else {
            result.notes[0] = applyOctave(pitched[idx].note, octaveOffset_);
            result.velocities[0] = pitched[idx].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
            }
        }
    }

    /// @brief Walk mode: random +/-1 step with octave support.
    void advanceWalk(std::span<const HeldNote> pitched, size_t size,
                     ArpNoteResult& result) noexcept {
        walkIndex_ = std::min(walkIndex_, size - 1);
        int step = (rng_.next() & 1) ? 1 : -1;
        if (step == -1 && walkIndex_ == 0) {
            // Clamped at lower bound
        } else if (step == 1 && walkIndex_ >= size - 1) {
            // Clamped at upper bound
        } else {
            walkIndex_ = static_cast<size_t>(static_cast<int>(walkIndex_) + step);
        }
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(pitched[walkIndex_].note, octaveOffset_);
            result.velocities[0] = pitched[walkIndex_].velocity;
            result.count = 1;
            // Walk doesn't have a pattern cycle, advance octave per call
            advanceOctaveSequentialAscending();
        } else {
            result.notes[0] = applyOctave(pitched[walkIndex_].note, octaveOffset_);
            result.velocities[0] = pitched[walkIndex_].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
            }
        }
    }

    /// @brief AsPlayed mode: insertion order with octave support.
    void advanceAsPlayed(const HeldNoteBuffer& held, size_t size,
                         ArpNoteResult& result) noexcept {
        const auto ordered = held.byInsertOrder();
        noteIndex_ = std::min(noteIndex_, size - 1);
        if (octaveMode_ == OctaveMode::Sequential) {
            result.notes[0] = applyOctave(ordered[noteIndex_].note, octaveOffset_);
            result.velocities[0] = ordered[noteIndex_].velocity;
            result.count = 1;
            noteIndex_ = (noteIndex_ + 1) % size;
            if (noteIndex_ == 0) {
                advanceOctaveSequentialAscending();
            }
        } else {
            result.notes[0] = applyOctave(ordered[noteIndex_].note, octaveOffset_);
            result.velocities[0] = ordered[noteIndex_].velocity;
            result.count = 1;
            ++octaveOffset_;
            if (octaveOffset_ >= octaveRange_) {
                octaveOffset_ = 0;
                noteIndex_ = (noteIndex_ + 1) % size;
            }
        }
    }

    ArpMode mode_{ArpMode::Up};
    OctaveMode octaveMode_{OctaveMode::Sequential};
    int octaveRange_{1};
    size_t noteIndex_{0};
    size_t pingPongPos_{0};
    int direction_{1};
    size_t convergeStep_{0};
    size_t walkIndex_{0};
    int octaveOffset_{0};
    Xorshift32 rng_;
};

} // namespace Krate::DSP
