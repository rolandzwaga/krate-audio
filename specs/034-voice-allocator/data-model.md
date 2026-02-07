# Data Model: Voice Allocator

**Feature Branch**: `034-voice-allocator`
**Date**: 2026-02-07

## Entity Overview

```
VoiceAllocator
  |
  |-- owns --> VoiceSlot[32]  (fixed-size array, pre-allocated)
  |-- owns --> VoiceEvent[64] (event buffer, pre-allocated)
  |-- uses --> AllocationMode  (enum, strategy selector)
  |-- uses --> StealMode       (enum, hard/soft selector)
  |-- uses --> VoiceState      (enum, per-voice lifecycle)
```

---

## Enumerations

### VoiceState

Lifecycle state for a single voice slot.

| Value | Numeric | Description |
|-------|---------|-------------|
| Idle | 0 | Available for assignment. No note playing. |
| Active | 1 | Playing a held note. Gate is on. |
| Releasing | 2 | Note-off received, envelope completing release tail. Gate is off. |

**Transitions**:
```
Idle --> Active      (on noteOn)
Active --> Releasing  (on noteOff or steal)
Releasing --> Idle    (on voiceFinished)
Releasing --> Active  (on retrigger of same note while releasing)
Active --> Active     (on retrigger of same note, voice restarted)
```

### AllocationMode

Strategy for selecting voices from the pool.

| Value | Numeric | Description |
|-------|---------|-------------|
| RoundRobin | 0 | Cycle through indices sequentially |
| Oldest | 1 | Select voice with earliest timestamp (default) |
| LowestVelocity | 2 | Select voice with lowest velocity value |
| HighestNote | 3 | Select voice with highest MIDI note number |

### StealMode

Behavior when stealing an active voice.

| Value | Numeric | Description |
|-------|---------|-------------|
| Hard | 0 | Immediate reassignment with Steal event (default) |
| Soft | 1 | NoteOff to old note, then NoteOn for new note on same voice |

### VoiceEvent::Type

Event type returned by the allocator.

| Value | Numeric | Description |
|-------|---------|-------------|
| NoteOn | 0 | Voice should begin playing a new note |
| NoteOff | 1 | Voice should enter release phase |
| Steal | 2 | Voice is being hard-stolen (immediate silence + restart) |

---

## Structures

### VoiceEvent

A lightweight event descriptor. Aggregate struct, no constructors.

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| type | Type (uint8_t enum) | NoteOn, NoteOff, Steal | Event classification |
| voiceIndex | uint8_t | 0-31 | Which voice slot this event targets |
| note | uint8_t | 0-127 | MIDI note number |
| velocity | uint8_t | 0-127 | MIDI velocity |
| frequency | float | >0 Hz | Pre-computed frequency including pitch bend and unison detune |

**Size**: 8 bytes (4 bytes of uint8_t fields + 4 bytes float, naturally aligned)

### VoiceSlot (internal)

Per-voice tracking data. Not exposed in public API.

| Field | C++ Type | Atomic? | Description |
|-------|----------|---------|-------------|
| state | `std::atomic<uint8_t>` (underlying: VoiceState) | Yes | Current lifecycle state. Atomic for thread-safe UI queries (FR-039). |
| note | `std::atomic<int8_t>` | Yes | MIDI note (-1 = idle, 0-127 = assigned). Atomic for thread-safe UI queries (FR-038). |
| velocity | `uint8_t` | No | MIDI velocity of triggering note. Audio-thread only. |
| padding | `uint8_t` | -- | Explicit padding byte to maintain natural 4-byte alignment for subsequent fields and keep struct size predictable across compilers. |
| timestamp | `uint64_t` | No | Monotonic counter value at last noteOn. Audio-thread only. |
| frequency | `float` | No | Current frequency in Hz (includes pitch bend + detune). Audio-thread only. |

**Size**: 16 bytes per slot. Total for 32 slots: 512 bytes.

**Atomic fields**: `state` and `note` use `std::atomic<uint8_t>` / `std::atomic<int8_t>` with `std::memory_order_relaxed` to support thread-safe query methods (FR-038, FR-039, FR-039a). These are guaranteed lock-free on all target platforms (`std::atomic<uint8_t>::is_always_lock_free == true`). All other fields are only accessed from the audio thread and do not need atomic operations.

---

## VoiceAllocator Class

### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| kMaxVoices | size_t | 32 | Maximum simultaneous voices |
| kMaxUnisonCount | size_t | 8 | Maximum unison voices per note |
| kMaxEvents | size_t | 64 | Maximum events from single call (kMaxVoices * 2) |

### Member Variables

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| voices_ | std::array<VoiceSlot, 32> | All Idle | Voice slot pool |
| eventBuffer_ | std::array<VoiceEvent, 64> | Empty | Event return buffer |
| eventCount_ | size_t | 0 | Number of valid events in buffer |
| voiceCount_ | size_t | 8 | Active voice limit (1-32) |
| unisonCount_ | size_t | 1 | Voices per note (1-8) |
| unisonDetune_ | float | 0.0f | Detune amount (0.0-1.0) |
| pitchBendSemitones_ | float | 0.0f | Global pitch bend offset |
| a4Frequency_ | float | 440.0f | A4 tuning reference |
| allocationMode_ | AllocationMode | Oldest | Current strategy |
| stealMode_ | StealMode | Hard | Current steal behavior |
| timestamp_ | uint64_t | 0 | Monotonic allocation counter |
| rrCounter_ | size_t | 0 | Round-robin index |
| activeVoiceCount_ | std::atomic<uint32_t> | 0 | Thread-safe active count |

### Estimated Total Size

```
Voice slots:     32 * 16 =  512 bytes
Event buffer:    64 *  8 =  512 bytes
Config/counters:          ~  80 bytes
Alignment padding:        ~  16 bytes
--------------------------------------
Estimated total:         ~1120 bytes  (well under 4096 limit)
```

---

## State Transitions Diagram

```
                    noteOn()
                   +--------+
                   |        v
              +---------+  +---------+
              |  Idle   |  | Active  |
              +---------+  +---------+
                   ^        |
    voiceFinished()|        | noteOff() / stolen
                   |        v
              +-----------+
              | Releasing |
              +-----------+
                   |
                   | noteOn (same note retrigger)
                   v
              +---------+
              | Active  |
              +---------+
```

---

## Validation Rules

| Rule | Where Applied |
|------|---------------|
| voiceCount in [1, 32] | setVoiceCount() clamps |
| unisonCount in [1, 8] | setUnisonCount() clamps |
| unisonCount <= voiceCount | Internal enforcement |
| unisonDetune in [0.0, 1.0] | setUnisonDetune() clamps, NaN/Inf ignored |
| pitchBend: NaN/Inf ignored | setPitchBend() guards |
| a4Frequency: NaN/Inf ignored | setTuningReference() guards |
| velocity 0 = noteOff | noteOn() checks velocity |
| voiceIndex < voiceCount_ | voiceFinished() bounds check |
| Voice in Releasing state | voiceFinished() state check |
