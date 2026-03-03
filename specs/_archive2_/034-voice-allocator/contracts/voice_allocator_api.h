// ==============================================================================
// API Contract: Voice Allocator
// ==============================================================================
// This file defines the public API contract for the VoiceAllocator class.
// It is a DESIGN document, not the implementation. The actual implementation
// will be at: dsp/include/krate/dsp/systems/voice_allocator.h
//
// Feature: 034-voice-allocator
// Layer: 3 (System)
// Namespace: Krate::DSP
// ==============================================================================

#pragma once

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
enum class VoiceState : uint8_t {
    Idle = 0,      ///< Available for assignment
    Active = 1,    ///< Playing a held note (gate on)
    Releasing = 2  ///< Note-off received, release tail active (gate off)
};

/// Voice allocation/stealing strategy.
enum class AllocationMode : uint8_t {
    RoundRobin = 0,      ///< Cycle through voices sequentially
    Oldest = 1,          ///< Select voice with earliest timestamp (default)
    LowestVelocity = 2,  ///< Select voice with lowest velocity
    HighestNote = 3      ///< Select voice with highest MIDI note
};

/// Voice stealing behavior.
enum class StealMode : uint8_t {
    Hard = 0,  ///< Immediate reassign: Steal event + NoteOn (default)
    Soft = 1   ///< Graceful: NoteOff (old) + NoteOn (new) on same voice
};

// =============================================================================
// VoiceEvent (FR-001)
// =============================================================================

/// Lightweight event descriptor returned by the allocator.
/// Simple aggregate with no user-declared constructors.
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
/// Manages a pool of voice slots and produces VoiceEvent instructions for
/// the caller to act on. Does NOT own or process any DSP -- it is purely
/// a note-to-voice routing engine.
///
/// @par Layer
/// Layer 3 (System). Depends only on Layer 0 (core utilities) and stdlib.
///
/// @par Thread Safety
/// - noteOn(), noteOff(), voiceFinished(), and all setters: audio thread only.
/// - getVoiceNote(), getVoiceState(), getActiveVoiceCount(): thread-safe
///   (safe to call from UI/automation threads concurrently).
///
/// @par Real-Time Safety
/// All methods are real-time safe: no allocation, no locks, no exceptions, no I/O.
/// All methods are noexcept.
///
/// @par Memory
/// All internal structures pre-allocated for kMaxVoices (32). No heap allocation
/// after construction. Total instance size < 4096 bytes.
class VoiceAllocator {
public:
    // =========================================================================
    // Constants (FR-003, FR-004, FR-005)
    // =========================================================================

    static constexpr size_t kMaxVoices = 32;
    static constexpr size_t kMaxUnisonCount = 8;
    static constexpr size_t kMaxEvents = kMaxVoices * 2;  // 64

    // =========================================================================
    // Construction
    // =========================================================================

    /// Default constructor. All voices Idle, 8 voices, Oldest mode, Hard steal.
    /// No heap allocation.
    VoiceAllocator() noexcept;

    // =========================================================================
    // Core Note Events (FR-010 through FR-016)
    // =========================================================================

    /// Process a note-on event.
    ///
    /// Assigns an idle voice (or steals if pool full). Handles same-note
    /// retrigger (FR-012), velocity-0-as-noteoff (FR-015), and unison (FR-029).
    ///
    /// @param note MIDI note number (0-127)
    /// @param velocity MIDI velocity (0-127, 0 treated as noteOff)
    /// @return Span of VoiceEvents (valid until next noteOn/noteOff/setVoiceCount)
    [[nodiscard]] std::span<const VoiceEvent> noteOn(uint8_t note,
                                                      uint8_t velocity) noexcept;

    /// Process a note-off event.
    ///
    /// Transitions voice(s) from Active to Releasing. In unison mode,
    /// releases all voices belonging to that note (FR-031).
    ///
    /// @param note MIDI note number (0-127)
    /// @return Span of VoiceEvents (empty if note not active)
    [[nodiscard]] std::span<const VoiceEvent> noteOff(uint8_t note) noexcept;

    /// Signal that a voice has finished its release phase.
    ///
    /// Transitions voice from Releasing to Idle. Ignored for non-Releasing
    /// voices or out-of-range indices (FR-016).
    ///
    /// @param voiceIndex Voice slot index (0 to kMaxVoices-1)
    void voiceFinished(size_t voiceIndex) noexcept;

    // =========================================================================
    // Configuration (FR-006, FR-007, FR-023, FR-028, FR-029, FR-034, FR-035)
    // =========================================================================

    /// Set the voice allocation strategy. Change takes effect on next noteOn.
    /// @param mode Allocation mode (FR-023: does not disrupt active voices)
    void setAllocationMode(AllocationMode mode) noexcept;

    /// Set the voice stealing behavior.
    /// @param mode Hard or Soft steal (FR-028)
    void setStealMode(StealMode mode) noexcept;

    /// Set the active voice count. Clamped to [1, kMaxVoices].
    /// Reducing count releases excess voices (returns NoteOff events via span).
    /// @param count Number of available voices (FR-035, FR-036)
    /// @return Span of NoteOff events for released excess voices
    [[nodiscard]] std::span<const VoiceEvent> setVoiceCount(size_t count) noexcept;

    /// Set unison voice count per note. Clamped to [1, kMaxUnisonCount].
    /// New count applies to subsequent noteOn events only (FR-033).
    /// @param count Voices per note
    void setUnisonCount(size_t count) noexcept;

    /// Set unison detune spread. Clamped to [0.0, 1.0]. NaN/Inf ignored.
    /// 0.0 = no detune, 1.0 = max +/-50 cents spread (FR-034).
    /// @param amount Detune amount
    void setUnisonDetune(float amount) noexcept;

    /// Set global pitch bend in semitones. Recalculates all active voice
    /// frequencies (FR-037). NaN/Inf ignored.
    /// @param semitones Pitch bend offset (typically +/-2)
    void setPitchBend(float semitones) noexcept;

    /// Set A4 tuning reference frequency. Recalculates all active voice
    /// frequencies (FR-041). NaN/Inf ignored.
    /// @param a4Hz Reference frequency for MIDI note 69
    void setTuningReference(float a4Hz) noexcept;

    // =========================================================================
    // State Queries (FR-017, FR-018, FR-038, FR-039, FR-039a)
    // =========================================================================

    /// Get MIDI note for a voice. Thread-safe (atomic read).
    /// @param voiceIndex Voice slot index
    /// @return MIDI note number (0-127) or -1 if idle (FR-038)
    [[nodiscard]] int getVoiceNote(size_t voiceIndex) const noexcept;

    /// Get voice lifecycle state. Thread-safe (atomic read).
    /// @param voiceIndex Voice slot index
    /// @return Current VoiceState (FR-039)
    [[nodiscard]] VoiceState getVoiceState(size_t voiceIndex) const noexcept;

    /// Check if voice is active (Active or Releasing). Thread-safe.
    /// @param voiceIndex Voice slot index
    /// @return true if not Idle (FR-018)
    [[nodiscard]] bool isVoiceActive(size_t voiceIndex) const noexcept;

    /// Get count of non-idle voices. Thread-safe (atomic read).
    /// @return Number of Active + Releasing voices (FR-017, FR-039a)
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept;

    // =========================================================================
    // Reset (FR-040)
    // =========================================================================

    /// Reset all voices to Idle. Clear all tracking. No events generated.
    void reset() noexcept;

private:
    // =========================================================================
    // Internal Helpers (not part of public API — shown for design clarity)
    // =========================================================================

    /// Compute the unison detune offset in cents for a given voice within a group.
    /// Uses symmetric linear distribution: offset = detuneAmount * 50 * ((2*i - (N-1)) / (N-1))
    /// @param voiceIndex Index within the unison group [0, N-1]
    /// @param unisonCount Total voices in group (N > 1; returns 0 when N == 1)
    /// @return Detune offset in cents (negative = below, 0 = center, positive = above)
    [[nodiscard]] float computeUnisonDetuneCents(size_t voiceIndex,
                                                  size_t unisonCount) const noexcept;

    /// Find an idle voice according to the current allocation mode.
    /// @return Voice index, or kMaxVoices if no idle voice found
    [[nodiscard]] size_t findIdleVoice() noexcept;

    /// Find the best voice to steal according to allocation mode and releasing preference.
    /// Prefers releasing voices over active voices (FR-025).
    /// In unison mode, selects the best unison group (returns the first voice index of the group).
    /// @return Voice index of the steal victim
    [[nodiscard]] size_t findStealVictim() noexcept;

    /// Compute frequency for a note including pitch bend and optional unison detune.
    /// @param note MIDI note number
    /// @param detuneCents Unison detune offset in cents (0 for no detune)
    /// @return Frequency in Hz
    [[nodiscard]] float computeFrequency(uint8_t note, float detuneCents = 0.0f) const noexcept;
};

} // namespace Krate::DSP
