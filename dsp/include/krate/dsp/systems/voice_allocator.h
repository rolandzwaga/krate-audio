// ==============================================================================
// Layer 3: System Component
// voice_allocator.h - Polyphonic Voice Management
// ==============================================================================
// Constitution Principle II: Real-Time Audio Thread Safety
// - No allocation, no locks, no exceptions, no I/O
// - All methods noexcept
//
// Constitution Principle IX: Layered DSP Architecture
// - Layer 3 depends only on Layer 0 (core utilities)
//
// Feature: 034-voice-allocator
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>    // detail::isNaN, detail::isInf
#include <krate/dsp/core/midi_utils.h>  // midiNoteToFrequency, kA4FrequencyHz
#include <krate/dsp/core/pitch_utils.h> // semitonesToRatio

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP {

// =============================================================================
// Enumerations (FR-006, FR-007, FR-008)
// =============================================================================

/// Voice lifecycle state.
/// Tracks the three phases of a voice's life: available, playing, or releasing.
///
/// Transitions:
///   Idle -> Active      (on noteOn)
///   Active -> Releasing  (on noteOff or steal)
///   Releasing -> Idle    (on voiceFinished)
///   Releasing -> Active  (on same-note retrigger while releasing)
///   Active -> Active     (on same-note retrigger, voice restarted)
enum class VoiceState : uint8_t {
    Idle = 0,      ///< Available for assignment
    Active = 1,    ///< Playing a held note (gate on)
    Releasing = 2  ///< Note-off received, release tail active (gate off)
};

/// Voice allocation/stealing strategy (FR-006).
/// Determines which voice is chosen from the available pool and which
/// voice is stolen when the pool is full.
///
/// Default: Oldest -- the most common strategy in modern synthesizers,
/// providing the most musical voice stealing behavior.
enum class AllocationMode : uint8_t {
    RoundRobin = 0,      ///< Cycle through voices sequentially
    Oldest = 1,          ///< Select voice with earliest timestamp (default)
    LowestVelocity = 2,  ///< Select voice with lowest velocity
    HighestNote = 3      ///< Select voice with highest MIDI note
};

/// Voice stealing behavior (FR-007).
/// Determines what events are generated when a voice must be stolen.
///
/// Hard: Stolen voice receives a Steal event (immediate silence + restart).
/// Soft: Stolen voice receives a NoteOff (old note fades out), then NoteOn (new note).
enum class StealMode : uint8_t {
    Hard = 0,  ///< Immediate reassign: Steal event + NoteOn (default)
    Soft = 1   ///< Graceful: NoteOff (old) + NoteOn (new) on same voice
};

// =============================================================================
// VoiceEvent (FR-001)
// =============================================================================

/// Lightweight event descriptor returned by the allocator.
///
/// Simple aggregate with no user-declared constructors (FR-001).
/// The allocator produces these events; the caller (synth engine) acts on them
/// by starting, stopping, or stealing actual voice DSP instances.
///
/// @par Size
/// 8 bytes (4 bytes of uint8_t fields + 4 bytes float, naturally aligned).
///
/// @par Example
/// @code
/// auto events = allocator.noteOn(60, 100);
/// for (const auto& event : events) {
///     switch (event.type) {
///         case VoiceEvent::Type::NoteOn:
///             voices[event.voiceIndex].start(event.frequency, event.velocity);
///             break;
///         case VoiceEvent::Type::NoteOff:
///             voices[event.voiceIndex].release();
///             break;
///         case VoiceEvent::Type::Steal:
///             voices[event.voiceIndex].hardStop();
///             break;
///     }
/// }
/// @endcode
struct VoiceEvent {
    /// Event classification.
    enum class Type : uint8_t {
        NoteOn = 0,   ///< Voice should begin playing
        NoteOff = 1,  ///< Voice should enter release phase
        Steal = 2     ///< Voice is hard-stolen (silence + restart)
    };

    Type type;            ///< Event type
    uint8_t voiceIndex;   ///< Target voice slot (0 to kMaxVoices-1)
    uint8_t note;         ///< MIDI note number (0-127)
    uint8_t velocity;     ///< MIDI velocity (0-127)
    float frequency;      ///< Pre-computed frequency in Hz (includes pitch bend + detune)
};

// =============================================================================
// VoiceAllocator (FR-002 through FR-045)
// =============================================================================

/// Core polyphonic voice management system.
///
/// Manages a pool of up to 32 voice slots and produces VoiceEvent instructions
/// for the caller to act on. Does NOT own or process any DSP -- it is purely
/// a note-to-voice routing engine.
///
/// @par Layer
/// Layer 3 (System). Depends only on Layer 0 (core utilities) and stdlib (FR-044).
///
/// @par Thread Safety
/// - noteOn(), noteOff(), voiceFinished(), and all setters: audio thread only.
/// - getVoiceNote(), getVoiceState(), getActiveVoiceCount(): thread-safe
///   (safe to call from UI/automation threads concurrently) (FR-038, FR-039, FR-039a).
///
/// @par Real-Time Safety
/// All methods are real-time safe: no allocation, no locks, no exceptions, no I/O (FR-042).
/// All methods are noexcept (FR-043).
///
/// @par Memory
/// All internal structures pre-allocated for kMaxVoices (32). No heap allocation
/// after construction. Total instance size < 4096 bytes (SC-009).
///
/// @par Basic Usage
/// @code
/// Krate::DSP::VoiceAllocator allocator;
///
/// // Process note-on
/// auto events = allocator.noteOn(60, 100); // Middle C, velocity 100
/// for (const auto& e : events) {
///     // Start voice e.voiceIndex at frequency e.frequency
/// }
///
/// // Process note-off
/// auto offEvents = allocator.noteOff(60);
/// for (const auto& e : offEvents) {
///     // Release voice e.voiceIndex
/// }
///
/// // When voice envelope finishes
/// allocator.voiceFinished(voiceIndex);
/// @endcode
///
/// @par Unison Mode Example
/// @code
/// allocator.setUnisonCount(3);
/// allocator.setUnisonDetune(0.5f); // 25 cents spread
///
/// auto events = allocator.noteOn(60, 100);
/// // events contains 3 NoteOn events with different voice indices
/// // and slightly different frequencies (center, +detune, -detune)
/// @endcode
///
/// @par Voice Stealing Example
/// @code
/// allocator.setAllocationMode(AllocationMode::Oldest);
/// allocator.setStealMode(StealMode::Hard);
///
/// // Fill all voices
/// for (int i = 0; i < 8; ++i)
///     allocator.noteOn(60 + i, 100);
///
/// // Next note steals the oldest voice
/// auto events = allocator.noteOn(80, 100);
/// // events[0]: Steal event for oldest voice
/// // events[1]: NoteOn event for voice now playing note 80
/// @endcode
class VoiceAllocator {
public:
    // =========================================================================
    // Constants (FR-003, FR-004, FR-005)
    // =========================================================================

    static constexpr size_t kMaxVoices = 32;       ///< Maximum simultaneous voices
    static constexpr size_t kMaxUnisonCount = 8;   ///< Maximum unison voices per note
    static constexpr size_t kMaxEvents = kMaxVoices * 2;  ///< 64 max events per call

    // =========================================================================
    // Construction (FR-002)
    // =========================================================================

    /// Default constructor. All voices Idle, 8 voices, Oldest mode, Hard steal.
    /// No heap allocation (FR-002).
    VoiceAllocator() noexcept {
        for (auto& voice : voices_) {
            voice.state.store(static_cast<uint8_t>(VoiceState::Idle),
                              std::memory_order_relaxed);
            voice.note.store(-1, std::memory_order_relaxed);
            voice.velocity = 0;
            voice.padding = 0;
            voice.timestamp = 0;
            voice.frequency = 0.0f;
        }
        activeVoiceCount_.store(0, std::memory_order_relaxed);
    }

    // =========================================================================
    // Core Note Events (FR-010 through FR-016)
    // =========================================================================

    /// Process a note-on event.
    ///
    /// Assigns an idle voice (or steals if pool full). Handles same-note
    /// retrigger (FR-012), velocity-0-as-noteoff (FR-015), and unison (FR-029).
    ///
    /// @param note MIDI note number (0-127)
    /// @param velocity MIDI velocity (0-127, 0 treated as noteOff per FR-015)
    /// @return Span of VoiceEvents (valid until next noteOn/noteOff/setVoiceCount)
    [[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note,
                                                      uint8_t velocity) noexcept {
        // FR-015: velocity 0 treated as noteOff
        if (velocity == 0) {
            return noteOff(note);
        }

        clearEvents();
        ++timestamp_;

        // FR-012: Same-note retrigger - check if note is already playing
        size_t existingVoice = findVoicePlayingNote(note);
        if (existingVoice < kMaxVoices) {
            // Retrigger the existing voice (and its entire unison group)
            retriggerNote(existingVoice, note, velocity);
            return events();
        }

        // Allocate voice(s) for this note
        allocateNote(note, velocity);
        return events();
    }

    /// Process a note-off event.
    ///
    /// Transitions voice(s) from Active to Releasing. In unison mode,
    /// releases all voices belonging to that note (FR-031).
    ///
    /// @param note MIDI note number (0-127)
    /// @return Span of VoiceEvents (empty if note not active, FR-014)
    [[nodiscard]] std::span<const VoiceEvent> noteOff(uint8_t note) noexcept {
        clearEvents();

        // Find and release all voices playing this note (FR-013, FR-031)
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == VoiceState::Active &&
                voices_[i].note.load(std::memory_order_relaxed) == static_cast<int8_t>(note)) {
                voices_[i].state.store(static_cast<uint8_t>(VoiceState::Releasing),
                                       std::memory_order_relaxed);
                pushEvent(VoiceEvent{
                    VoiceEvent::Type::NoteOff,
                    static_cast<uint8_t>(i),
                    note,
                    voices_[i].velocity,
                    voices_[i].frequency
                });
            }
        }

        return events();
    }

    /// Signal that a voice has finished its release phase.
    ///
    /// Transitions voice from Releasing to Idle. Ignored for non-Releasing
    /// voices or out-of-range indices (FR-016).
    ///
    /// @param voiceIndex Voice slot index (0 to kMaxVoices-1)
    void voiceFinished(size_t voiceIndex) noexcept {
        if (voiceIndex >= kMaxVoices) return;

        auto state = static_cast<VoiceState>(
            voices_[voiceIndex].state.load(std::memory_order_relaxed));
        if (state != VoiceState::Releasing) return;

        voices_[voiceIndex].state.store(static_cast<uint8_t>(VoiceState::Idle),
                                         std::memory_order_relaxed);
        voices_[voiceIndex].note.store(-1, std::memory_order_relaxed);
        voices_[voiceIndex].velocity = 0;
        voices_[voiceIndex].frequency = 0.0f;

        activeVoiceCount_.fetch_sub(1, std::memory_order_relaxed);
    }

    // =========================================================================
    // Configuration (FR-006, FR-007, FR-023, FR-028, FR-029, FR-034, FR-035)
    // =========================================================================

    /// Set the voice allocation strategy. Change takes effect on next noteOn.
    /// Does not disrupt active voices (FR-023).
    /// @param mode Allocation mode
    void setAllocationMode(AllocationMode mode) noexcept {
        allocationMode_ = mode;
    }

    /// Set the voice stealing behavior (FR-028).
    /// @param mode Hard or Soft steal
    void setStealMode(StealMode mode) noexcept {
        stealMode_ = mode;
    }

    /// Set the active voice count. Clamped to [1, kMaxVoices] (FR-035).
    /// Reducing count releases excess voices (returns NoteOff events).
    /// Increasing count makes new voices available immediately (FR-036).
    /// @param count Number of available voices
    /// @return Span of NoteOff events for released excess voices
    [[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t count) noexcept {
        clearEvents();

        // Clamp to valid range
        if (count < 1) count = 1;
        if (count > kMaxVoices) count = kMaxVoices;

        // If reducing, release excess voices
        if (count < voiceCount_) {
            for (size_t i = count; i < voiceCount_; ++i) {
                auto state = static_cast<VoiceState>(
                    voices_[i].state.load(std::memory_order_relaxed));
                if (state == VoiceState::Active || state == VoiceState::Releasing) {
                    uint8_t voiceNote = static_cast<uint8_t>(
                        voices_[i].note.load(std::memory_order_relaxed));
                    pushEvent(VoiceEvent{
                        VoiceEvent::Type::NoteOff,
                        static_cast<uint8_t>(i),
                        voiceNote,
                        voices_[i].velocity,
                        voices_[i].frequency
                    });
                    voices_[i].state.store(static_cast<uint8_t>(VoiceState::Idle),
                                           std::memory_order_relaxed);
                    voices_[i].note.store(-1, std::memory_order_relaxed);
                    voices_[i].velocity = 0;
                    voices_[i].frequency = 0.0f;
                    activeVoiceCount_.fetch_sub(1, std::memory_order_relaxed);
                }
            }
        }

        voiceCount_ = count;
        return events();
    }

    /// Set unison voice count per note. Clamped to [1, kMaxUnisonCount] (FR-029).
    /// New count applies to subsequent noteOn events only (FR-033).
    /// @param count Voices per note
    void setUnisonCount(size_t count) noexcept {
        if (count < 1) count = 1;
        if (count > kMaxUnisonCount) count = kMaxUnisonCount;
        unisonCount_ = count;
    }

    /// Set unison detune spread. Clamped to [0.0, 1.0]. NaN/Inf ignored (FR-034).
    /// 0.0 = no detune, 1.0 = max +/-50 cents spread.
    /// @param amount Detune amount
    void setUnisonDetune(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        if (amount < 0.0f) amount = 0.0f;
        if (amount > 1.0f) amount = 1.0f;
        unisonDetune_ = amount;
    }

    /// Set global pitch bend in semitones. Recalculates all active voice
    /// frequencies immediately (FR-037). NaN/Inf ignored.
    /// @param semitones Pitch bend offset (typically +/-2)
    void setPitchBend(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        pitchBendSemitones_ = semitones;
        recalculateAllFrequencies();
    }

    /// Set A4 tuning reference frequency. Recalculates all active voice
    /// frequencies (FR-041). NaN/Inf ignored.
    /// @param a4Hz Reference frequency for MIDI note 69
    void setTuningReference(float a4Hz) noexcept {
        if (detail::isNaN(a4Hz) || detail::isInf(a4Hz)) return;
        a4Frequency_ = a4Hz;
        recalculateAllFrequencies();
    }

    // =========================================================================
    // State Queries (FR-017, FR-018, FR-038, FR-039, FR-039a)
    // =========================================================================

    /// Get MIDI note for a voice. Thread-safe (atomic read, FR-038).
    /// @param voiceIndex Voice slot index
    /// @return MIDI note number (0-127) or -1 if idle
    [[nodiscard]] int getVoiceNote(size_t voiceIndex) const noexcept {
        if (voiceIndex >= kMaxVoices) return -1;
        return static_cast<int>(voices_[voiceIndex].note.load(std::memory_order_relaxed));
    }

    /// Get voice lifecycle state. Thread-safe (atomic read, FR-039).
    /// @param voiceIndex Voice slot index
    /// @return Current VoiceState
    [[nodiscard]] VoiceState getVoiceState(size_t voiceIndex) const noexcept {
        if (voiceIndex >= kMaxVoices) return VoiceState::Idle;
        return static_cast<VoiceState>(
            voices_[voiceIndex].state.load(std::memory_order_relaxed));
    }

    /// Check if voice is active (Active or Releasing). Thread-safe (FR-018).
    /// @param voiceIndex Voice slot index
    /// @return true if not Idle
    [[nodiscard]] bool isVoiceActive(size_t voiceIndex) const noexcept {
        return getVoiceState(voiceIndex) != VoiceState::Idle;
    }

    /// Get count of non-idle voices. Thread-safe (atomic read, FR-017, FR-039a).
    /// @return Number of Active + Releasing voices
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept {
        return activeVoiceCount_.load(std::memory_order_relaxed);
    }

    /// Get the stored frequency for a voice. Audio-thread only (not atomic).
    /// @param voiceIndex Voice slot index
    /// @return Frequency in Hz, or 0.0f if idle or out-of-range
    [[nodiscard]] float getVoiceFrequency(size_t voiceIndex) const noexcept {
        if (voiceIndex >= kMaxVoices) return 0.0f;
        return voices_[voiceIndex].frequency;
    }

    // =========================================================================
    // Reset (FR-040)
    // =========================================================================

    /// Reset all voices to Idle. Clear all tracking. No events generated.
    void reset() noexcept {
        for (size_t i = 0; i < kMaxVoices; ++i) {
            voices_[i].state.store(static_cast<uint8_t>(VoiceState::Idle),
                                   std::memory_order_relaxed);
            voices_[i].note.store(-1, std::memory_order_relaxed);
            voices_[i].velocity = 0;
            voices_[i].timestamp = 0;
            voices_[i].frequency = 0.0f;
        }
        timestamp_ = 0;
        rrCounter_ = 0;
        eventCount_ = 0;
        activeVoiceCount_.store(0, std::memory_order_relaxed);
    }

private:
    // =========================================================================
    // Internal Types
    // =========================================================================

    /// Per-voice tracking data (FR-008, FR-009).
    /// Atomic fields for thread-safe UI queries (FR-038, FR-039).
    struct VoiceSlot {
        std::atomic<uint8_t> state{static_cast<uint8_t>(VoiceState::Idle)};
        std::atomic<int8_t> note{-1};
        uint8_t velocity{0};
        uint8_t padding{0};
        uint64_t timestamp{0};
        float frequency{0.0f};
    };

    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// Clear the event buffer for a new operation.
    void clearEvents() noexcept {
        eventCount_ = 0;
    }

    /// Append an event to the buffer.
    void pushEvent(VoiceEvent event) noexcept {
        if (eventCount_ < kMaxEvents) {
            eventBuffer_[eventCount_++] = event;
        }
    }

    /// Return a view of the current events.
    [[nodiscard]] std::span<const VoiceEvent> events() const noexcept {
        return {eventBuffer_.data(), eventCount_};
    }

    /// Compute frequency for a note including pitch bend and optional unison detune.
    /// @param midiNote MIDI note number
    /// @param detuneCents Unison detune offset in cents (0 for no detune)
    /// @return Frequency in Hz
    [[nodiscard]] float computeFrequency(uint8_t midiNote,
                                          float detuneCents = 0.0f) const noexcept {
        float baseFreq = midiNoteToFrequency(static_cast<int>(midiNote), a4Frequency_);
        float bendRatio = semitonesToRatio(pitchBendSemitones_);
        float detuneRatio = (detuneCents != 0.0f)
                            ? semitonesToRatio(detuneCents / 100.0f)
                            : 1.0f;
        return baseFreq * bendRatio * detuneRatio;
    }

    /// Compute the unison detune offset in cents for a given voice within a group.
    /// Uses symmetric linear distribution (FR-030).
    /// @param voiceIdx Index within the unison group [0, N-1]
    /// @param count Total voices in group (N > 1; returns 0 when N == 1)
    /// @return Detune offset in cents
    [[nodiscard]] float computeUnisonDetuneCents(size_t voiceIdx,
                                                  size_t count) const noexcept {
        if (count <= 1) return 0.0f;
        float maxSpreadCents = unisonDetune_ * 50.0f;
        // offset = maxSpread * ((2*i - (N-1)) / (N-1))
        float nMinus1 = static_cast<float>(count - 1);
        float position = (2.0f * static_cast<float>(voiceIdx) - nMinus1) / nMinus1;
        return maxSpreadCents * position;
    }

    /// Find an idle voice according to the current allocation mode.
    /// @return Voice index, or kMaxVoices if no idle voice found
    [[nodiscard]] size_t findIdleVoice() noexcept {
        switch (allocationMode_) {
            case AllocationMode::RoundRobin:
                return findIdleVoiceRoundRobin();
            case AllocationMode::Oldest:
                return findIdleVoiceOldest();
            case AllocationMode::LowestVelocity:
            case AllocationMode::HighestNote:
                return findIdleVoiceAny();
            default:
                return findIdleVoiceAny();
        }
    }

    /// RoundRobin: select the next idle voice starting from rrCounter_.
    [[nodiscard]] size_t findIdleVoiceRoundRobin() noexcept {
        for (size_t i = 0; i < voiceCount_; ++i) {
            size_t idx = (rrCounter_ + i) % voiceCount_;
            auto state = static_cast<VoiceState>(
                voices_[idx].state.load(std::memory_order_relaxed));
            if (state == VoiceState::Idle) {
                rrCounter_ = (idx + 1) % voiceCount_;
                return idx;
            }
        }
        return kMaxVoices;
    }

    /// Oldest: select the idle voice with the lowest timestamp (longest idle).
    [[nodiscard]] size_t findIdleVoiceOldest() noexcept {
        size_t best = kMaxVoices;
        uint64_t bestTimestamp = UINT64_MAX;
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == VoiceState::Idle) {
                if (voices_[i].timestamp < bestTimestamp) {
                    bestTimestamp = voices_[i].timestamp;
                    best = i;
                }
            }
        }
        return best;
    }

    /// Any: select the first available idle voice.
    [[nodiscard]] size_t findIdleVoiceAny() noexcept {
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == VoiceState::Idle) {
                return i;
            }
        }
        return kMaxVoices;
    }

    /// Find the best voice to steal according to allocation mode and
    /// releasing preference (FR-025).
    /// @return Voice index of the steal victim
    [[nodiscard]] size_t findStealVictim() noexcept {
        if (unisonCount_ > 1) {
            return findStealVictimUnison();
        }
        return findStealVictimSingle();
    }

    /// Find steal victim for single-voice (non-unison) mode.
    [[nodiscard]] size_t findStealVictimSingle() noexcept {
        // FR-025: Prefer releasing voices over active voices
        size_t releasingVictim = findBestVictimByState(VoiceState::Releasing);
        if (releasingVictim < kMaxVoices) return releasingVictim;

        // No releasing voices, steal an active voice
        return findBestVictimByState(VoiceState::Active);
    }

    /// Find the best victim among voices of a specific state, using the
    /// current allocation mode strategy.
    [[nodiscard]] size_t findBestVictimByState(VoiceState targetState) noexcept {
        switch (allocationMode_) {
            case AllocationMode::RoundRobin:
                return findVictimRoundRobin(targetState);
            case AllocationMode::Oldest:
                return findVictimOldest(targetState);
            case AllocationMode::LowestVelocity:
                return findVictimLowestVelocity(targetState);
            case AllocationMode::HighestNote:
                return findVictimHighestNote(targetState);
            default:
                return findVictimOldest(targetState);
        }
    }

    /// RoundRobin victim: next voice in cycle with target state.
    [[nodiscard]] size_t findVictimRoundRobin(VoiceState targetState) noexcept {
        for (size_t i = 0; i < voiceCount_; ++i) {
            size_t idx = (rrCounter_ + i) % voiceCount_;
            auto state = static_cast<VoiceState>(
                voices_[idx].state.load(std::memory_order_relaxed));
            if (state == targetState) {
                rrCounter_ = (idx + 1) % voiceCount_;
                return idx;
            }
        }
        return kMaxVoices;
    }

    /// Oldest victim: voice with lowest timestamp among target state (FR-020).
    [[nodiscard]] size_t findVictimOldest(VoiceState targetState) noexcept {
        size_t best = kMaxVoices;
        uint64_t bestTimestamp = UINT64_MAX;
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == targetState && voices_[i].timestamp < bestTimestamp) {
                bestTimestamp = voices_[i].timestamp;
                best = i;
            }
        }
        return best;
    }

    /// LowestVelocity victim: voice with lowest velocity, ties broken by age (FR-021).
    [[nodiscard]] size_t findVictimLowestVelocity(VoiceState targetState) noexcept {
        size_t best = kMaxVoices;
        uint8_t bestVelocity = 255;
        uint64_t bestTimestamp = UINT64_MAX;
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == targetState) {
                if (voices_[i].velocity < bestVelocity ||
                    (voices_[i].velocity == bestVelocity &&
                     voices_[i].timestamp < bestTimestamp)) {
                    bestVelocity = voices_[i].velocity;
                    bestTimestamp = voices_[i].timestamp;
                    best = i;
                }
            }
        }
        return best;
    }

    /// HighestNote victim: voice with highest note, ties broken by age (FR-022).
    [[nodiscard]] size_t findVictimHighestNote(VoiceState targetState) noexcept {
        size_t best = kMaxVoices;
        int8_t bestNote = -1;
        uint64_t bestTimestamp = UINT64_MAX;
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == targetState) {
                int8_t voiceNote = voices_[i].note.load(std::memory_order_relaxed);
                if (voiceNote > bestNote ||
                    (voiceNote == bestNote &&
                     voices_[i].timestamp < bestTimestamp)) {
                    bestNote = voiceNote;
                    bestTimestamp = voices_[i].timestamp;
                    best = i;
                }
            }
        }
        return best;
    }

    /// Find steal victim for unison mode - treats unison groups as single entities.
    [[nodiscard]] size_t findStealVictimUnison() noexcept {
        // Collect unique notes currently playing
        struct NoteGroup {
            int8_t note = -1;
            size_t firstVoice = kMaxVoices;
            uint64_t timestamp = UINT64_MAX;
            uint8_t velocity = 255;
            bool hasReleasing = false;
            bool hasActive = false;
        };

        std::array<NoteGroup, kMaxVoices> groups{};
        size_t groupCount = 0;

        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == VoiceState::Idle) continue;

            int8_t voiceNote = voices_[i].note.load(std::memory_order_relaxed);

            // Find or create group for this note
            size_t groupIdx = kMaxVoices;
            for (size_t g = 0; g < groupCount; ++g) {
                if (groups[g].note == voiceNote) {
                    groupIdx = g;
                    break;
                }
            }
            if (groupIdx >= kMaxVoices) {
                groupIdx = groupCount++;
                groups[groupIdx].note = voiceNote;
                groups[groupIdx].firstVoice = i;
                groups[groupIdx].timestamp = voices_[i].timestamp;
                groups[groupIdx].velocity = voices_[i].velocity;
            }

            if (state == VoiceState::Releasing) groups[groupIdx].hasReleasing = true;
            if (state == VoiceState::Active) groups[groupIdx].hasActive = true;
            if (voices_[i].timestamp < groups[groupIdx].timestamp) {
                groups[groupIdx].timestamp = voices_[i].timestamp;
                groups[groupIdx].firstVoice = i;
            }
        }

        // FR-025: Prefer groups with at least one releasing voice
        size_t bestGroup = kMaxVoices;
        bool bestIsReleasing = false;

        for (size_t g = 0; g < groupCount; ++g) {
            bool groupReleasing = groups[g].hasReleasing;

            // If we haven't found anything yet, take it
            if (bestGroup >= kMaxVoices) {
                bestGroup = g;
                bestIsReleasing = groupReleasing;
                continue;
            }

            // Prefer releasing groups over active groups
            if (groupReleasing && !bestIsReleasing) {
                bestGroup = g;
                bestIsReleasing = true;
                continue;
            }
            if (!groupReleasing && bestIsReleasing) {
                continue;
            }

            // Both same release status: use allocation mode strategy
            bool replace = false;
            switch (allocationMode_) {
                case AllocationMode::RoundRobin:
                case AllocationMode::Oldest:
                    replace = groups[g].timestamp < groups[bestGroup].timestamp;
                    break;
                case AllocationMode::LowestVelocity:
                    replace = (groups[g].velocity < groups[bestGroup].velocity) ||
                              (groups[g].velocity == groups[bestGroup].velocity &&
                               groups[g].timestamp < groups[bestGroup].timestamp);
                    break;
                case AllocationMode::HighestNote:
                    replace = (groups[g].note > groups[bestGroup].note) ||
                              (groups[g].note == groups[bestGroup].note &&
                               groups[g].timestamp < groups[bestGroup].timestamp);
                    break;
            }
            if (replace) {
                bestGroup = g;
                bestIsReleasing = groupReleasing;
            }
        }

        if (bestGroup < kMaxVoices) {
            return groups[bestGroup].firstVoice;
        }
        return 0; // Fallback: steal voice 0
    }

    /// Find a voice currently playing the given note (Active or Releasing).
    /// Returns the first matching voice index, or kMaxVoices if not found.
    [[nodiscard]] size_t findVoicePlayingNote(uint8_t note) noexcept {
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state != VoiceState::Idle &&
                voices_[i].note.load(std::memory_order_relaxed) == static_cast<int8_t>(note)) {
                return i;
            }
        }
        return kMaxVoices;
    }

    /// Retrigger a note that is already playing (same-note retrigger, FR-012).
    void retriggerNote(size_t firstVoice, uint8_t note, uint8_t velocity) noexcept {
        if (unisonCount_ <= 1) {
            // Single voice retrigger
            pushEvent(VoiceEvent{
                VoiceEvent::Type::Steal,
                static_cast<uint8_t>(firstVoice),
                static_cast<uint8_t>(voices_[firstVoice].note.load(std::memory_order_relaxed)),
                voices_[firstVoice].velocity,
                voices_[firstVoice].frequency
            });

            float freq = computeFrequency(note);
            voices_[firstVoice].state.store(static_cast<uint8_t>(VoiceState::Active),
                                             std::memory_order_relaxed);
            voices_[firstVoice].note.store(static_cast<int8_t>(note),
                                            std::memory_order_relaxed);
            voices_[firstVoice].velocity = velocity;
            voices_[firstVoice].timestamp = timestamp_;
            voices_[firstVoice].frequency = freq;

            pushEvent(VoiceEvent{
                VoiceEvent::Type::NoteOn,
                static_cast<uint8_t>(firstVoice),
                note,
                velocity,
                freq
            });
        } else {
            // Unison retrigger: steal all voices in the group, then re-assign
            // Collect all voices playing this note
            std::array<size_t, kMaxUnisonCount> groupVoices{};
            size_t groupSize = 0;
            for (size_t i = 0; i < voiceCount_ && groupSize < kMaxUnisonCount; ++i) {
                auto state = static_cast<VoiceState>(
                    voices_[i].state.load(std::memory_order_relaxed));
                if (state != VoiceState::Idle &&
                    voices_[i].note.load(std::memory_order_relaxed) == static_cast<int8_t>(note)) {
                    groupVoices[groupSize++] = i;
                }
            }

            // Steal events for existing voices
            for (size_t i = 0; i < groupSize; ++i) {
                size_t vi = groupVoices[i];
                pushEvent(VoiceEvent{
                    VoiceEvent::Type::Steal,
                    static_cast<uint8_t>(vi),
                    static_cast<uint8_t>(voices_[vi].note.load(std::memory_order_relaxed)),
                    voices_[vi].velocity,
                    voices_[vi].frequency
                });
            }

            // Re-assign voices with new detune
            for (size_t i = 0; i < groupSize; ++i) {
                size_t vi = groupVoices[i];
                float detuneCents = computeUnisonDetuneCents(i, groupSize);
                float freq = computeFrequency(note, detuneCents);

                voices_[vi].state.store(static_cast<uint8_t>(VoiceState::Active),
                                         std::memory_order_relaxed);
                voices_[vi].note.store(static_cast<int8_t>(note),
                                        std::memory_order_relaxed);
                voices_[vi].velocity = velocity;
                voices_[vi].timestamp = timestamp_;
                voices_[vi].frequency = freq;

                pushEvent(VoiceEvent{
                    VoiceEvent::Type::NoteOn,
                    static_cast<uint8_t>(vi),
                    note,
                    velocity,
                    freq
                });
            }
        }
    }

    /// Allocate voice(s) for a new note (normal allocation path).
    void allocateNote(uint8_t note, uint8_t velocity) noexcept {
        size_t needed = unisonCount_;
        std::array<size_t, kMaxUnisonCount> allocated{};
        size_t allocCount = 0;

        // Try to find idle voices first
        for (size_t i = 0; i < needed; ++i) {
            size_t idx = findIdleVoice();
            if (idx >= kMaxVoices) break;
            allocated[allocCount++] = idx;
            // Mark as active temporarily so findIdleVoice doesn't find it again
            voices_[idx].state.store(static_cast<uint8_t>(VoiceState::Active),
                                      std::memory_order_relaxed);
        }

        // If we didn't get enough idle voices, we need to steal
        if (allocCount < needed) {
            // Revert temporarily-allocated voices back to idle
            for (size_t i = 0; i < allocCount; ++i) {
                voices_[allocated[i]].state.store(
                    static_cast<uint8_t>(VoiceState::Idle), std::memory_order_relaxed);
            }
            allocCount = 0;

            // Find a complete group to steal
            size_t victimVoice = findStealVictim();

            if (unisonCount_ > 1) {
                // Steal entire unison group belonging to the victim's note
                int8_t victimNote = voices_[victimVoice].note.load(std::memory_order_relaxed);

                // Collect all voices with the victim's note
                std::array<size_t, kMaxUnisonCount> victimVoices{};
                size_t victimCount = 0;
                for (size_t i = 0; i < voiceCount_ && victimCount < needed; ++i) {
                    auto state = static_cast<VoiceState>(
                        voices_[i].state.load(std::memory_order_relaxed));
                    if (state != VoiceState::Idle &&
                        voices_[i].note.load(std::memory_order_relaxed) == victimNote) {
                        victimVoices[victimCount++] = i;
                    }
                }

                // Generate steal/noteoff events and reassign
                for (size_t i = 0; i < victimCount; ++i) {
                    size_t vi = victimVoices[i];
                    if (stealMode_ == StealMode::Hard) {
                        pushEvent(VoiceEvent{
                            VoiceEvent::Type::Steal,
                            static_cast<uint8_t>(vi),
                            static_cast<uint8_t>(voices_[vi].note.load(std::memory_order_relaxed)),
                            voices_[vi].velocity,
                            voices_[vi].frequency
                        });
                    } else {
                        pushEvent(VoiceEvent{
                            VoiceEvent::Type::NoteOff,
                            static_cast<uint8_t>(vi),
                            static_cast<uint8_t>(voices_[vi].note.load(std::memory_order_relaxed)),
                            voices_[vi].velocity,
                            voices_[vi].frequency
                        });
                    }
                    allocated[allocCount++] = vi;
                }

                // If victim group was smaller than needed, find more voices
                while (allocCount < needed) {
                    // Try idle first
                    size_t idx = findIdleVoiceAny();
                    if (idx >= kMaxVoices) {
                        // Steal another voice
                        idx = findStealVictimSingle();
                        if (idx >= kMaxVoices) break;
                        if (stealMode_ == StealMode::Hard) {
                            pushEvent(VoiceEvent{
                                VoiceEvent::Type::Steal,
                                static_cast<uint8_t>(idx),
                                static_cast<uint8_t>(voices_[idx].note.load(std::memory_order_relaxed)),
                                voices_[idx].velocity,
                                voices_[idx].frequency
                            });
                        } else {
                            pushEvent(VoiceEvent{
                                VoiceEvent::Type::NoteOff,
                                static_cast<uint8_t>(idx),
                                static_cast<uint8_t>(voices_[idx].note.load(std::memory_order_relaxed)),
                                voices_[idx].velocity,
                                voices_[idx].frequency
                            });
                        }
                    }
                    allocated[allocCount++] = idx;
                }
            } else {
                // Single voice steal
                if (stealMode_ == StealMode::Hard) {
                    pushEvent(VoiceEvent{
                        VoiceEvent::Type::Steal,
                        static_cast<uint8_t>(victimVoice),
                        static_cast<uint8_t>(voices_[victimVoice].note.load(std::memory_order_relaxed)),
                        voices_[victimVoice].velocity,
                        voices_[victimVoice].frequency
                    });
                } else {
                    pushEvent(VoiceEvent{
                        VoiceEvent::Type::NoteOff,
                        static_cast<uint8_t>(victimVoice),
                        static_cast<uint8_t>(voices_[victimVoice].note.load(std::memory_order_relaxed)),
                        voices_[victimVoice].velocity,
                        voices_[victimVoice].frequency
                    });
                }
                allocated[0] = victimVoice;
                allocCount = 1;
            }
        }

        // Now assign the new note to allocated voices
        for (size_t i = 0; i < allocCount; ++i) {
            size_t vi = allocated[i];
            float detuneCents = computeUnisonDetuneCents(i, unisonCount_);
            float freq = computeFrequency(note, detuneCents);

            voices_[vi].state.store(static_cast<uint8_t>(VoiceState::Active),
                                     std::memory_order_relaxed);
            voices_[vi].note.store(static_cast<int8_t>(note), std::memory_order_relaxed);
            voices_[vi].velocity = velocity;
            voices_[vi].timestamp = timestamp_;
            voices_[vi].frequency = freq;

            pushEvent(VoiceEvent{
                VoiceEvent::Type::NoteOn,
                static_cast<uint8_t>(vi),
                note,
                velocity,
                freq
            });
        }

        // Update active voice count: count only voices that were not
        // previously active (idle voices that became active)
        // For steal, we already counted the old voice, so no net change
        // For idle -> active, increment
        // Simplified: just recount
        uint32_t count = 0;
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state != VoiceState::Idle) ++count;
        }
        activeVoiceCount_.store(count, std::memory_order_relaxed);
    }

    /// Recalculate frequencies for all active and releasing voices.
    /// Called when pitch bend or tuning reference changes (FR-037, FR-041).
    void recalculateAllFrequencies() noexcept {
        // We need to know each voice's detune offset. Since we don't store
        // the original detune cents, we reconstruct from the unison group.
        // For simplicity, find each unique note and recompute detune for its group.
        for (size_t i = 0; i < voiceCount_; ++i) {
            auto state = static_cast<VoiceState>(
                voices_[i].state.load(std::memory_order_relaxed));
            if (state == VoiceState::Idle) continue;

            int8_t voiceNote = voices_[i].note.load(std::memory_order_relaxed);
            if (voiceNote < 0) continue;

            // Find this voice's position within its unison group
            float detuneCents = 0.0f;
            if (unisonCount_ > 1) {
                size_t posInGroup = 0;
                for (size_t j = 0; j < i; ++j) {
                    auto jState = static_cast<VoiceState>(
                        voices_[j].state.load(std::memory_order_relaxed));
                    if (jState != VoiceState::Idle &&
                        voices_[j].note.load(std::memory_order_relaxed) == voiceNote) {
                        ++posInGroup;
                    }
                }
                // Count total in group to determine detune
                size_t groupSize = 0;
                for (size_t j = 0; j < voiceCount_; ++j) {
                    auto jState = static_cast<VoiceState>(
                        voices_[j].state.load(std::memory_order_relaxed));
                    if (jState != VoiceState::Idle &&
                        voices_[j].note.load(std::memory_order_relaxed) == voiceNote) {
                        ++groupSize;
                    }
                }
                detuneCents = computeUnisonDetuneCents(posInGroup, groupSize);
            }

            voices_[i].frequency = computeFrequency(
                static_cast<uint8_t>(voiceNote), detuneCents);
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    std::array<VoiceSlot, kMaxVoices> voices_{};          ///< Voice slot pool
    std::array<VoiceEvent, kMaxEvents> eventBuffer_{};    ///< Event return buffer
    size_t eventCount_{0};                                 ///< Valid events in buffer
    size_t voiceCount_{8};                                 ///< Active voice limit (1-32)
    size_t unisonCount_{1};                                ///< Voices per note (1-8)
    float unisonDetune_{0.0f};                             ///< Detune amount (0.0-1.0)
    float pitchBendSemitones_{0.0f};                       ///< Global pitch bend
    float a4Frequency_{kA4FrequencyHz};                    ///< A4 tuning reference
    AllocationMode allocationMode_{AllocationMode::Oldest}; ///< Current strategy
    StealMode stealMode_{StealMode::Hard};                  ///< Current steal behavior
    uint64_t timestamp_{0};                                 ///< Monotonic counter
    size_t rrCounter_{0};                                   ///< Round-robin index
    std::atomic<uint32_t> activeVoiceCount_{0};             ///< Thread-safe active count
};

} // namespace Krate::DSP
