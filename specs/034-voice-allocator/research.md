# Research: Voice Allocator

**Feature Branch**: `034-voice-allocator`
**Date**: 2026-02-07

## Research Summary

This document consolidates research findings for implementing a polyphonic voice allocator as a Layer 3 system component in KrateDSP.

---

## R-001: Voice Allocation Algorithms

**Question**: What are the standard voice allocation strategies in polyphonic synthesizers?

**Decision**: Implement four allocation modes: RoundRobin, Oldest, LowestVelocity, HighestNote.

**Rationale**: These four modes cover the primary strategies documented in synthesizer design literature and used in commercial instruments:

1. **RoundRobin** -- Sequentially cycles through voices. Historically important for analog synths (Prophet-5, OB-X) where each voice circuit had slight timbral differences. Ensures even wear/use of all voice slots.

2. **Oldest** (default) -- Steals the voice triggered earliest. The most common strategy in modern digital synths because the oldest note's amplitude envelope has decayed the most, making it the least audible to steal. Used by default in most commercial synths (Serum, Massive, Vital).

3. **LowestVelocity** -- Steals the voice with the softest velocity. Preserves perceptually louder notes. Useful for dynamic playing styles where quiet notes should yield to loud notes.

4. **HighestNote** -- Steals the highest-pitched voice. Preserves lower notes that form the harmonic foundation. Common in bass-heavy styles where the root note must not drop out.

**Alternatives considered**:
- **LowestNote**: Rejected -- less common, opposite use case. Can be added later if needed.
- **Random**: Rejected -- unpredictable behavior is musically undesirable for most use cases.
- **Priority-based**: Rejected -- adds complexity without clear benefit at this stage. Priority can be layered on top.

**References**:
- KVR Audio Forums: "References for how voice/note allocation systems work from first principles"
- Will Pirkle, "Designing Software Synthesizer Plug-Ins in C++" (2nd ed., Routledge)
- Electronic Music Wiki, voice allocation entry

---

## R-002: Voice Stealing with Release Tail Support

**Question**: What is the best approach for voice stealing in a real-time polyphonic context?

**Decision**: Two steal modes (Hard and Soft) with a preference-for-releasing-voices heuristic.

**Rationale**:

**Hard steal** immediately reassigns the stolen voice. The caller gets a `Steal` event (for click-free envelope reset) followed by a `NoteOn` event. This is the simplest approach and appropriate when the synth engine does not need crossfade support.

**Soft steal** sends a `NoteOff` to the victim voice (initiating its release envelope) and immediately assigns the voice to the new note with a `NoteOn`. The caller is responsible for managing the old note's release tail separately from the voice's new note output. This produces smoother transitions at the cost of requiring the caller to maintain a crossfade buffer.

**Preference for releasing voices**: When both Active and Releasing voices exist, the allocator always prefers stealing a Releasing voice because its amplitude is already fading -- stealing it is less audible. This heuristic is standard in professional implementations (documented in Oberheim and Sequential Circuits designs).

**Implementation approach**:
- Voice selection happens in two phases: (1) filter by state preference (Releasing first, then Active), (2) apply allocation mode strategy within the filtered set.
- In unison mode, treat each unison group as a single entity for stealing. If any voice in the group is Releasing, the whole group is considered Releasing.

**Alternatives considered**:
- **Dual-buffer crossfade in allocator**: Rejected -- the allocator is a pure routing engine, not an audio processor. Crossfade management is the caller's responsibility.
- **Queue-based stealing**: Rejected -- adds latency to note allocation, violating real-time responsiveness.

---

## R-003: Lock-Free Atomic State for Cross-Thread Queries

**Question**: How should thread-safe queries be implemented for UI thread access to voice state?

**Decision**: Use `std::atomic` loads with `std::memory_order_relaxed` for query methods. Store voice state and note number as atomic types within the voice slot structure.

**Rationale**:

The spec requires three query methods to be thread-safe: `getVoiceNote()`, `getVoiceState()`, and `getActiveVoiceCount()`. These are read-only queries from the UI thread while the audio thread performs note operations.

**Approach**:
- `VoiceState` is an enum backed by `uint8_t`. Store as `std::atomic<uint8_t>` (or directly `std::atomic<VoiceState>` since it fits in a single byte).
- Voice note number: store as `std::atomic<int8_t>` (range -1 to 127 fits in signed byte).
- Active voice count: maintain a `std::atomic<uint32_t>` counter, incremented on noteOn/retrigger, decremented on voiceFinished.
- All atomic operations use `std::memory_order_relaxed` because:
  - The UI thread only needs a "recent enough" snapshot, not sequentially consistent ordering.
  - No other data depends on the ordering of these reads relative to other memory operations.
  - Relaxed atomics are essentially free on x86 (just a regular load/store) and extremely cheap on ARM.
  - `std::atomic<uint8_t>::is_lock_free()` returns true on all modern platforms.

**Key insight**: We do NOT need consistency between multiple atomic reads (e.g., reading voiceNote and voiceState for the same voice might see them from slightly different points in time). This is acceptable for UI display purposes. If perfect consistency were needed, we would need a sequence lock or similar -- but that is overkill for voice meters.

**Alternatives considered**:
- **std::mutex**: Rejected -- forbidden on audio thread (Constitution Principle II).
- **SPSC queue for state snapshots**: Rejected -- adds complexity and latency for a simple read-only query.
- **memory_order_acquire/release**: Rejected -- unnecessary ordering guarantees for display-only reads. Would add overhead on ARM for no benefit.
- **Double-buffering**: Rejected -- overkill for individual atomic fields.

---

## R-004: Efficient Fixed-Size Voice Pool Management

**Question**: What data structures provide efficient voice selection without dynamic allocation?

**Decision**: Use a fixed-size `std::array<VoiceSlot, 32>` with linear scan for voice selection.

**Rationale**:

With a maximum of 32 voices, the entire voice pool fits in a single cache line block (~2KB). At this scale, linear scan O(N) is faster than maintaining auxiliary data structures (priority queues, linked lists) because:

1. **Cache efficiency**: 32 voice slots are ~1-2KB total, fitting entirely in L1 cache. A linear scan over contiguous memory is branch-predictor friendly and prefetch friendly.

2. **No allocation overhead**: Linked lists require node allocation; priority queues may rebalance. A flat array with direct indexing has zero overhead.

3. **Simplicity**: The allocation mode strategies (oldest, lowest-velocity, highest-note) all require comparing properties across all voices. Even with a priority queue, you would need to rebuild it when the comparison criterion changes (e.g., when switching allocation modes). Linear scan is always correct.

4. **Worst-case timing**: A scan of 32 slots takes ~100ns. The SC-008 budget is 1 microsecond. There is substantial headroom.

**Voice slot structure** (per-voice):
```
VoiceState state       (atomic uint8_t)  -- 1 byte
int8_t     note        (atomic int8_t)   -- 1 byte
uint8_t    velocity                      -- 1 byte
uint8_t    padding                       -- 1 byte
uint64_t   timestamp                     -- 8 bytes
float      frequency                     -- 4 bytes
```
Total per slot: ~16 bytes. For 32 slots: ~512 bytes.

**Round-robin counter**: A single `size_t` index that wraps at `voiceCount_`.

**Note-to-voice mapping**: Not using a separate lookup table. For `noteOff()`, linear scan to find the voice playing that note is fast enough at 32 voices. A 128-entry note table (as some implementations use) would add 512 bytes of memory for marginal speedup.

**Alternatives considered**:
- **Linked list of idle voices**: Rejected -- adds pointer overhead, cache-unfriendly at 32 elements, and complicates unison group management.
- **Priority queue per allocation mode**: Rejected -- must rebuild on mode switch, O(N log N) vs O(N) for scan.
- **128-entry note lookup table**: Rejected -- adds memory overhead. With 32 voices, the scan to find a note is bounded at 32 comparisons. The note lookup would help if voice count were much higher (128+), but at 32 it is unnecessary.

---

## R-005: std::span for Zero-Allocation Event Return

**Question**: How should events be returned from noteOn/noteOff without heap allocation?

**Decision**: Pre-allocate a `std::array<VoiceEvent, kMaxEvents>` as a member variable. Return `std::span<const VoiceEvent>` pointing into this internal buffer.

**Rationale**:

`std::span` (C++20) is a non-owning view consisting of just a pointer and a size. It provides:
- Zero allocation: the span points to the allocator's internal buffer.
- Type safety: `std::span<const VoiceEvent>` prevents modification of returned events.
- Range-for compatibility: callers iterate with `for (auto& event : events)`.
- Size awareness: `events.size()` gives the count without separate length parameter.

**Pattern**:
```cpp
class VoiceAllocator {
    std::array<VoiceEvent, kMaxEvents> eventBuffer_{};
    size_t eventCount_ = 0;

    // Helper to append an event
    void pushEvent(VoiceEvent event) noexcept {
        if (eventCount_ < kMaxEvents) {
            eventBuffer_[eventCount_++] = event;
        }
    }

    // Reset at start of noteOn/noteOff
    void clearEvents() noexcept { eventCount_ = 0; }

    // Return view of events
    std::span<const VoiceEvent> events() const noexcept {
        return {eventBuffer_.data(), eventCount_};
    }
};
```

**Lifetime**: The returned span is valid until the next call to `noteOn()`, `noteOff()`, or `setVoiceCount()` (which may modify the event buffer). This is documented in the spec assumptions section.

**kMaxEvents calculation**: Maximum events from a single call = `kMaxVoices * 2` = 64. Worst case: unison count of 8 with all 32 voices occupied -- steal 8 voices (8 Steal events) + assign 8 new voices (8 NoteOn events) = 16 events. But with `setVoiceCount()` reducing from 32 to 1, up to 31 NoteOff events could be returned. The safe upper bound is `kMaxVoices * 2 = 64`.

**Alternatives considered**:
- **Return std::vector**: Rejected -- heap allocation, violates real-time safety.
- **Output parameter (pointer + count)**: Rejected -- less ergonomic, error-prone.
- **Return std::array by value**: Rejected -- always copies kMaxEvents worth of data.
- **Callback pattern**: Rejected -- adds complexity, harder to test.

---

## R-006: Unison Detune Frequency Distribution

**Question**: How should unison voices be distributed in frequency for the VoiceAllocator's simple linear detune?

**Decision**: Linear symmetric distribution across +/- detune range.

**Rationale**:

The spec (FR-030) defines the distribution clearly:
- Detune parameter: 0.0 (no detune) to 1.0 (maximum +/-50 cents)
- Odd N: one center voice at exact frequency, (N-1)/2 pairs symmetrically above and below
- Even N: N/2 pairs symmetrically above and below, no center voice

For unison count N with detune amount D (0.0-1.0), the maximum spread in cents is `D * 50.0`. Voices are placed at evenly spaced intervals across this range.

**Formula**:
```
maxSpreadCents = detuneAmount * 50.0  // +/- 50 cents at max

For odd N (e.g., 5):
  offsets = [0, +25, -25, +50, -50] cents at detune=1.0
  voiceIdx 0: 0 cents
  voiceIdx 1: +maxSpread/((N-1)/2) * 1 = +25 cents
  voiceIdx 2: -maxSpread/((N-1)/2) * 1 = -25 cents
  ...

For even N (e.g., 4):
  offsets = [+16.67, -16.67, +50, -50] cents at detune=1.0
  Pairs at 1/(2*numPairs), 3/(2*numPairs), ... of max spread
```

The conversion from cents to frequency ratio uses `semitonesToRatio(cents / 100.0f)` from `pitch_utils.h`.

**Distinction from UnisonEngine**: The UnisonEngine (spec 020) uses a non-linear power curve (`detune^1.7`) for JP-8000 supersaw timbral shaping. The VoiceAllocator's unison is simpler -- linear spacing for voice management. They serve complementary purposes and can be combined (VoiceAllocator manages which voices play, UnisonEngine adds additional per-oscillator detuning within a single voice).

---

## R-007: Memory Budget Feasibility (SC-009: < 4096 bytes)

**Question**: Can the voice allocator fit within the 4096-byte memory budget?

**Decision**: Yes, with careful layout. Estimated size is ~2400 bytes.

**Breakdown**:
```
Voice slots (32 x VoiceSlot):
  - state (atomic<uint8_t>):     1 byte
  - note (atomic<int8_t>):       1 byte
  - velocity (uint8_t):          1 byte
  - padding:                     1 byte
  - timestamp (uint64_t):        8 bytes
  - frequency (float):           4 bytes
  Subtotal per slot:             16 bytes
  Total: 32 * 16 =              512 bytes

Event buffer (64 x VoiceEvent):
  - type (uint8_t):              1 byte
  - voiceIndex (uint8_t):        1 byte
  - note (uint8_t):              1 byte
  - velocity (uint8_t):          1 byte
  - frequency (float):           4 bytes
  Subtotal per event:            8 bytes
  Total: 64 * 8 =               512 bytes

Unison group tracking (32 entries):
  - groupLeader or noteForVoice: 32 bytes  (1 byte per voice)

Configuration and counters:
  - voiceCount_ (size_t):        8 bytes
  - unisonCount_ (size_t):       8 bytes
  - unisonDetune_ (float):       4 bytes
  - pitchBendSemitones_ (float): 4 bytes
  - a4Frequency_ (float):        4 bytes
  - allocationMode_ (uint8_t):   1 byte
  - stealMode_ (uint8_t):        1 byte
  - eventCount_ (size_t):        8 bytes
  - timestamp_ (uint64_t):       8 bytes
  - rrCounter_ (size_t):         8 bytes
  - activeVoiceCount_ (atomic):  4 bytes
  Subtotal:                      ~58 bytes

GRAND TOTAL:                     ~1114 bytes
```

Even with alignment padding, this is well under the 4096-byte limit. The estimate leaves substantial headroom.

---

## R-008: Performance Feasibility (SC-008: < 1us per noteOn)

**Question**: Can a noteOn call complete within 1 microsecond?

**Decision**: Yes, with significant margin.

**Analysis**:

A noteOn call does the following:
1. Check for same-note retrigger -- linear scan of 32 voice slots: ~32 comparisons
2. Find idle/steal victim -- linear scan of 32 slots with mode-specific comparisons: ~32 comparisons
3. Compute frequency -- one `midiNoteToFrequency()` call (constexpr exp): ~20ns
4. Compute unison detune offsets -- up to 8 `semitonesToRatio()` calls: ~8 * 10ns = 80ns
5. Fill event buffer -- up to 16 writes of 8-byte structs: ~128 bytes

Total operations: ~64 comparisons + ~100ns math + ~128 bytes written.

At 3 GHz, 64 comparisons with branch prediction is ~20ns. The math is ~100ns. Buffer writes are ~50ns. Total: ~170ns worst case.

This is well under the 1us (1000ns) target. Even with cache misses and branch mispredictions, there is 5x headroom.

---

## R-009: Existing Code Reuse Assessment

**Question**: Which existing KrateDSP components can be reused?

**Decision**: Reuse Layer 0 utilities only. No Layer 1+ dependencies needed.

| Component | Location | Usage |
|-----------|----------|-------|
| `midiNoteToFrequency(int, float)` | `core/midi_utils.h` | Compute VoiceEvent frequency from MIDI note + tuning reference |
| `semitonesToRatio(float)` | `core/pitch_utils.h` | Pitch bend ratio and unison detune offset calculation |
| `detail::isNaN(float)` | `core/db_utils.h` | Guard NaN inputs on setPitchBend, setUnisonDetune, setTuningReference |
| `detail::isInf(float)` | `core/db_utils.h` | Guard infinity inputs on same setters |
| `kA4FrequencyHz` | `core/midi_utils.h` | Default A4 reference frequency constant |

**Verified API signatures**:
- `[[nodiscard]] constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz) noexcept`
- `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept`
- `constexpr bool detail::isNaN(float x) noexcept`
- `constexpr bool detail::isInf(float x) noexcept`

**Layer compliance** (FR-044): The VoiceAllocator at Layer 3 depends only on Layer 0 (core utilities) and stdlib. It does NOT depend on Layer 1 (primitives) or Layer 2 (processors). This is unusual for Layer 3 (most systems compose Layer 1-2 components), but correct -- the allocator is a pure routing/management system, not a DSP processor.

---

## R-010: Unison Group Tracking Design

**Question**: How should the allocator track which voices belong to the same unison group?

**Decision**: Store the "group note" in each voice slot. Voices with the same group note belong to the same unison group.

**Rationale**: Each voice slot already stores its MIDI note number. In unison mode, all N voices assigned to a single note-on share the same note number. To find all voices in a group, scan for voices with the matching note. This is O(32) but with the small pool size, it is fast enough.

For stealing decisions, when unison count > 1, the allocator groups voices by note and evaluates the group as a single entity (using the group's oldest timestamp, lowest velocity, or highest note depending on mode).

**Alternatives considered**:
- **Explicit group ID array**: Rejected -- adds 32 bytes of storage and another field to maintain. The note number already serves as the group identifier.
- **Linked list per group**: Rejected -- pointer overhead, allocation concerns, cache unfriendly.
