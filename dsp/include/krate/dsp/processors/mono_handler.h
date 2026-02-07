// ==============================================================================
// Layer 2: DSP Processor - Mono/Legato Handler
// ==============================================================================
// Monophonic note handling with legato and portamento:
// - Last-note, low-note, and high-note priority modes
// - Legato mode (retrigger suppression for overlapping notes)
// - Constant-time portamento linear in pitch space (semitones)
// - 16-entry fixed-capacity note stack for release handling
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations after construction)
// - Principle III: Modern C++ (C++20, constexpr, value semantics)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (sample-accurate processing)
// - Principle XII: Test-First Development
//
// Reference: specs/035-mono-legato-handler/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace Krate {  // NOLINT(modernize-concat-nested-namespaces) -- project convention
namespace DSP {

// =============================================================================
// MonoNoteEvent (FR-001)
// =============================================================================

/// Lightweight event descriptor returned by MonoHandler.
/// Simple aggregate with no user-declared constructors.
struct MonoNoteEvent {
    float frequency;    ///< Frequency in Hz (12-TET, A4=440Hz)
    uint8_t velocity;   ///< MIDI velocity (0-127)
    bool retrigger;     ///< true = caller should restart envelopes
    bool isNoteOn;      ///< true = note active, false = all notes released
};

// =============================================================================
// MonoMode (FR-002)
// =============================================================================

/// Note priority algorithm selection.
enum class MonoMode : uint8_t {
    LastNote = 0,   ///< Most recently pressed key takes priority (default)
    LowNote = 1,    ///< Lowest held key takes priority
    HighNote = 2    ///< Highest held key takes priority
};

// =============================================================================
// PortaMode (FR-003)
// =============================================================================

/// Portamento activation mode selection.
enum class PortaMode : uint8_t {
    Always = 0,       ///< Portamento on every note transition (default)
    LegatoOnly = 1    ///< Portamento only on overlapping notes
};

// =============================================================================
// NoteEntry (Internal)
// =============================================================================

/// Internal note stack entry.
struct NoteEntry {
    uint8_t note;      ///< MIDI note number (0-127)
    uint8_t velocity;  ///< MIDI velocity (1-127, never 0 in stack)
};

// =============================================================================
// MonoHandler (FR-004)
// =============================================================================

/// Monophonic note management processor with legato and portamento.
///
/// Manages a 16-entry fixed-capacity note stack, implements three note
/// priority modes (LastNote, LowNote, HighNote), provides legato mode
/// for envelope retrigger suppression, and offers constant-time portamento
/// that operates linearly in pitch space (semitones).
///
/// Thread safety: Single audio thread only (FR-033-threading).
/// All methods called sequentially from the same thread.
class MonoHandler {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxStackSize = 16;
    static constexpr float kDefaultSampleRate = 44100.0f;
    static constexpr float kMinPortamentoTimeMs = 0.0f;
    static constexpr float kMaxPortamentoTimeMs = 10000.0f;

    // =========================================================================
    // Construction
    // =========================================================================

    /// Default constructor. Pre-allocates all internal state.
    /// Default state: LastNote mode, portamento off (0ms), legato disabled,
    /// PortaMode::Always, sample rate 44100 Hz.
    MonoHandler() noexcept {
        portamentoRamp_.configure(0.0f, kDefaultSampleRate);
        portamentoRamp_.snapTo(0.0f);
    }

    // =========================================================================
    // Initialization (FR-005)
    // =========================================================================

    /// Configure for given sample rate. Recalculates portamento coefficients.
    /// Preserves current glide position if mid-glide.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        portamentoRamp_.configure(portamentoTimeMs_, sampleRate_);
        portamentoRamp_.setSampleRate(sampleRate_);
    }

    // =========================================================================
    // Note Events (FR-006 through FR-016)
    // =========================================================================

    /// Process a MIDI note-on event.
    /// @param note MIDI note number (0-127). Values outside range are ignored.
    /// @param velocity MIDI velocity (0-127). Velocity 0 treated as noteOff.
    /// @return MonoNoteEvent describing the result.
    [[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept {
        // FR-004a: Validate note range
        if (note < kMinMidiNote || note > kMaxMidiNote) {
            return makeInactiveEvent();
        }

        // FR-014: Velocity 0 treated as noteOff
        if (velocity <= 0) {
            return noteOff(note);
        }

        // Clamp velocity to valid range
        const auto vel = static_cast<uint8_t>(
            velocity > kMaxMidiVelocity ? kMaxMidiVelocity : velocity);
        const auto midiNote = static_cast<uint8_t>(note);

        // Determine if this is a legato transition (notes already held)
        const bool hadNotesHeld = (stackSize_ > 0);

        // FR-016: Same-note re-press -- remove existing entry first
        removeFromStack(midiNote);

        // FR-015: Stack full -- drop oldest entry
        if (stackSize_ >= kMaxStackSize) {
            removeAtIndex(0);
        }

        // Add note to stack
        addToStack(midiNote, vel);

        // Determine winner based on priority mode
        const int8_t winner = findWinner();

        // Update active note
        activeNote_ = winner;
        activeVelocity_ = getVelocityForNote(static_cast<uint8_t>(winner));

        // Compute frequency for the winning note
        const float freq = midiNoteToFrequency(winner);

        // Determine retrigger based on legato mode (FR-017, FR-019)
        bool retrigger = true;
        if (legato_ && hadNotesHeld) {
            retrigger = false;  // Legato: suppress retrigger for overlapping notes
        }

        // Update portamento target
        // First note ever (activeNote_ was -1 before): always snap, no glide possible
        // Legato (overlapping notes): glide if portamento > 0
        // Staccato (non-overlapping): glide only in Always mode
        // LegatoOnly + staccato: snap
        const bool isFirstNoteEver = !hadPreviousNote_;
        hadPreviousNote_ = true;

        bool enableGlide = false;
        if (!isFirstNoteEver) {
            if (hadNotesHeld) {
                // Overlapping notes: always glide (both Always and LegatoOnly)
                enableGlide = true;
            } else {
                // Non-overlapping (staccato): glide only in Always mode
                enableGlide = (portaMode_ == PortaMode::Always);
            }
        }
        updatePortamentoTarget(static_cast<float>(winner), enableGlide);

        return MonoNoteEvent{
            .frequency = freq,
            .velocity = activeVelocity_,
            .retrigger = retrigger,
            .isNoteOn = true};
    }

    /// Process a MIDI note-off event.
    /// @param note MIDI note number (0-127). Values outside range are ignored.
    /// @return MonoNoteEvent describing the result.
    [[nodiscard]] MonoNoteEvent noteOff(int note) noexcept {
        // FR-004a: Validate note range
        if (note < kMinMidiNote || note > kMaxMidiNote) {
            return makeInactiveEvent();
        }

        const auto midiNote = static_cast<uint8_t>(note);

        // Check if the note is even in the stack
        if (!isInStack(midiNote)) {
            // Note not in stack, return current state
            if (stackSize_ > 0) {
                return MonoNoteEvent{
                    .frequency = midiNoteToFrequency(activeNote_),
                    .velocity = activeVelocity_,
                    .retrigger = false,
                    .isNoteOn = true};
            }
            return makeInactiveEvent();
        }

        const bool wasActiveNote = (static_cast<int8_t>(midiNote) == activeNote_);

        // Remove from stack
        removeFromStack(midiNote);

        // FR-012: If released note was active, select next winner
        if (wasActiveNote) {
            if (stackSize_ > 0) {
                const int8_t winner = findWinner();
                activeNote_ = winner;
                activeVelocity_ = getVelocityForNote(static_cast<uint8_t>(winner));

                const float freq = midiNoteToFrequency(winner);

                // FR-018: Legato -- returning to held note does NOT retrigger
                const bool retrigger = !legato_;

                // Update portamento target (glide back to held note)
                updatePortamentoTarget(static_cast<float>(winner), true);

                return MonoNoteEvent{
                    .frequency = freq,
                    .velocity = activeVelocity_,
                    .retrigger = retrigger,
                    .isNoteOn = true};
            }

            // Last note released
            activeNote_ = -1;
            activeVelocity_ = 0;
            return makeInactiveEvent();
        }

        // FR-013: Released note was not active -- no output change
        return MonoNoteEvent{
            .frequency = midiNoteToFrequency(activeNote_),
            .velocity = activeVelocity_,
            .retrigger = false,
            .isNoteOn = true};
    }

    // =========================================================================
    // Portamento (FR-021 through FR-025)
    // =========================================================================

    /// Set portamento glide duration. 0.0 = instantaneous.
    /// Clamped to [0, 10000] ms.
    /// @param ms Glide time in milliseconds
    void setPortamentoTime(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) {
            return;  // Ignore invalid values
        }
        portamentoTimeMs_ = std::clamp(ms, kMinPortamentoTimeMs, kMaxPortamentoTimeMs);
        portamentoRamp_.configure(portamentoTimeMs_, sampleRate_);
    }

    /// Advance portamento by one sample and return current gliding frequency.
    /// Must be called once per audio sample.
    /// @return Current frequency in Hz
    [[nodiscard]] float processPortamento() noexcept {
        const float currentSemitones = portamentoRamp_.process();
        currentFrequency_ = semitoneToFrequency(currentSemitones);
        return currentFrequency_;
    }

    /// Get current portamento output frequency without advancing state.
    /// @return Current frequency in Hz
    [[nodiscard]] float getCurrentFrequency() const noexcept {
        return currentFrequency_;
    }

    // =========================================================================
    // Configuration (FR-010, FR-017, FR-027, FR-030)
    // =========================================================================

    /// Set note priority mode. Re-evaluates winner if notes are held.
    /// @param mode Priority algorithm
    void setMode(MonoMode mode) noexcept {
        mode_ = mode;

        // Re-evaluate winner if notes are held
        if (stackSize_ > 0) {
            const int8_t winner = findWinner();
            if (winner != activeNote_) {
                activeNote_ = winner;
                activeVelocity_ = getVelocityForNote(static_cast<uint8_t>(winner));
                updatePortamentoTarget(static_cast<float>(winner), true);
            }
        }
    }

    /// Enable/disable legato mode.
    /// @param enabled true = suppress retrigger for overlapping notes
    void setLegato(bool enabled) noexcept {
        legato_ = enabled;
    }

    /// Set portamento activation mode.
    /// @param mode Always or LegatoOnly
    void setPortamentoMode(PortaMode mode) noexcept {
        portaMode_ = mode;
    }

    // =========================================================================
    // State Queries (FR-026)
    // =========================================================================

    /// Returns true if at least one note is held.
    [[nodiscard]] bool hasActiveNote() const noexcept {
        return stackSize_ > 0;
    }

    // =========================================================================
    // Reset (FR-029)
    // =========================================================================

    /// Clear all state: note stack, portamento, active note.
    void reset() noexcept {
        stackSize_ = 0;
        activeNote_ = -1;
        activeVelocity_ = 0;
        hadPreviousNote_ = false;
        currentFrequency_ = 0.0f;
        portamentoRamp_.snapTo(portamentoRamp_.getCurrentValue());
    }

private:
    // =========================================================================
    // Note Stack Operations
    // =========================================================================

    void addToStack(uint8_t note, uint8_t velocity) noexcept {
        if (stackSize_ < kMaxStackSize) {
            stack_[stackSize_] = NoteEntry{.note = note, .velocity = velocity};
            ++stackSize_;
        }
    }

    void removeFromStack(uint8_t note) noexcept {
        for (uint8_t i = 0; i < stackSize_; ++i) {
            if (stack_[i].note == note) {
                removeAtIndex(i);
                return;
            }
        }
    }

    void removeAtIndex(uint8_t index) noexcept {
        // Shift entries left to maintain insertion order
        for (uint8_t i = index; i + 1 < stackSize_; ++i) {
            stack_[i] = stack_[i + 1];
        }
        --stackSize_;
    }

    [[nodiscard]] bool isInStack(uint8_t note) const noexcept {
        for (uint8_t i = 0; i < stackSize_; ++i) {
            if (stack_[i].note == note) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] uint8_t getVelocityForNote(uint8_t note) const noexcept {
        for (uint8_t i = 0; i < stackSize_; ++i) {
            if (stack_[i].note == note) {
                return stack_[i].velocity;
            }
        }
        return 0;
    }

    // =========================================================================
    // Priority Mode Logic
    // =========================================================================

    /// Find the winning note based on the current priority mode.
    /// Assumes stackSize_ > 0.
    [[nodiscard]] int8_t findWinner() const noexcept {
        if (stackSize_ == 0) {
            return -1;
        }

        switch (mode_) {
            case MonoMode::LastNote:
                // Most recently pressed = last in stack
                return static_cast<int8_t>(stack_[stackSize_ - 1].note);

            case MonoMode::LowNote: {
                uint8_t lowest = stack_[0].note;
                for (uint8_t i = 1; i < stackSize_; ++i) {
                    lowest = std::min(stack_[i].note, lowest);
                }
                return static_cast<int8_t>(lowest);
            }

            case MonoMode::HighNote: {
                uint8_t highest = stack_[0].note;
                for (uint8_t i = 1; i < stackSize_; ++i) {
                    highest = std::max(stack_[i].note, highest);
                }
                return static_cast<int8_t>(highest);
            }

            default:
                return static_cast<int8_t>(stack_[stackSize_ - 1].note);
        }
    }

    // =========================================================================
    // Portamento Helpers
    // =========================================================================

    /// Update the portamento ramp target.
    /// @param targetSemitones Target pitch in semitones (MIDI note number as float)
    /// @param enableGlide If true and portamento > 0, use setTarget for glide;
    ///                    otherwise snapTo for instant pitch change
    void updatePortamentoTarget(float targetSemitones, bool enableGlide) noexcept {
        if (enableGlide && portamentoTimeMs_ > 0.0f) {
            portamentoRamp_.setTarget(targetSemitones);
        } else {
            portamentoRamp_.snapTo(targetSemitones);
            currentFrequency_ = semitoneToFrequency(targetSemitones);
        }
    }

    /// Convert semitone value to frequency in Hz.
    /// Uses A4 (MIDI note 69) = 440 Hz as reference.
    [[nodiscard]] static float semitoneToFrequency(float semitones) noexcept {
        return kA4FrequencyHz * semitonesToRatio(semitones - static_cast<float>(kA4MidiNote));
    }

    /// Create an inactive event (no note sounding).
    [[nodiscard]] static MonoNoteEvent makeInactiveEvent() noexcept {
        return MonoNoteEvent{
            .frequency = 0.0f,
            .velocity = 0,
            .retrigger = false,
            .isNoteOn = false};
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Note stack (insertion order maintained)
    std::array<NoteEntry, kMaxStackSize> stack_{};
    uint8_t stackSize_{0};

    // Configuration
    MonoMode mode_{MonoMode::LastNote};
    PortaMode portaMode_{PortaMode::Always};
    bool legato_{false};

    // Current state
    int8_t activeNote_{-1};        ///< Currently sounding MIDI note (-1 = none)
    uint8_t activeVelocity_{0};    ///< Current note's velocity
    bool hadPreviousNote_{false};  ///< True after first note-on (for first-note snap)
    float currentFrequency_{0.0f}; ///< Cached output frequency

    // Portamento engine (LinearRamp in semitone space)
    LinearRamp portamentoRamp_;
    float portamentoTimeMs_{0.0f};
    float sampleRate_{kDefaultSampleRate};
};

}  // namespace DSP
}  // namespace Krate
