# Research: Mono/Legato Handler

**Feature Branch**: `035-mono-legato-handler`
**Date**: 2026-02-07
**Spec**: [spec.md](spec.md)

---

## R-001: LinearRamp Suitability for Constant-Time Portamento

### Question

Can the existing `LinearRamp` (Layer 1, `smoother.h`) be reused for the portamento engine, or does a custom implementation provide better fit?

### Research

The `LinearRamp` class provides:
- `configure(rampTimeMs, sampleRate)` -- sets the ramp duration
- `setTarget(value)` -- recalculates increment based on `(target - current) / numSamples`
- `process()` -- returns current value, advances by increment, clamps at target
- `snapTo(value)` -- sets both current and target immediately
- `isComplete()` -- returns true when current == target
- `getCurrentValue()` -- reads without advancing

The portamento needs constant-time behavior: every note transition takes the same configured portamento time regardless of the interval size. When `setTarget()` is called, `LinearRamp` calculates `increment = (target - current) / (rampTimeMs * 0.001 * sampleRate)`. This is exactly constant-time behavior: the full ramp time is used regardless of the delta.

**Key API behaviors verified from source (`smoother.h`):**
- `configure()` recalculates increment for an in-progress transition
- `setTarget()` recalculates increment from `current_` to new target (constant-time for the new transition)
- `setSampleRate()` also recalculates in-progress increment
- NaN/Inf protection is built in via `detail::isNaN()` / `detail::isInf()`
- Denormal flushing is built in

**Considerations:**
- The portamento operates in semitone space, not frequency space. The LinearRamp will ramp between semitone values, and we convert to frequency at output via `semitonesToRatio()`. This is exactly correct for "linear in pitch space" glide.
- When portamento time is 0, `setTarget()` produces `increment = delta / 0 samples` which returns `delta` (instant jump per `calculateLinearIncrement`: `if (rampTimeMs <= 0.0f) return delta`). So `process()` would jump to target in one sample. However, for zero portamento time we want truly instantaneous behavior, so we should use `snapTo()` instead to avoid even a one-sample delay.
- When redirecting mid-glide (FR-024), calling `setTarget()` recalculates from the current position, taking the full ramp time. This matches the spec.
- When `configure()` is called mid-glide (e.g., sample rate change in `prepare()`), it recalculates increment for the remaining distance. This matches FR-005 edge case.

### Decision

**Reuse LinearRamp** for the portamento engine. It provides exactly the constant-time linear interpolation behavior needed. The portamento will:
1. Configure LinearRamp with portamento time
2. Set targets in semitone values (MIDI note as float for semitone precision)
3. Call `process()` per sample to get current semitone value
4. Convert semitone value to frequency via `semitonesToRatio()` relative to A4

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Custom linear interpolation | Duplicates LinearRamp functionality, violates Principle XIV |
| OnePoleSmoother | Exponential approach (not linear in pitch), wrong portamento character |
| SlewLimiter | Rate-limited (constant-rate portamento), not constant-time |

---

## R-002: Note Stack Data Structure

### Question

What is the optimal data structure for the 16-entry note stack with LastNote (ordered by press time), LowNote, and HighNote priority modes?

### Research

**Requirements:**
- Fixed capacity of 16 entries (no heap allocation)
- Store MIDI note number (uint8_t) and velocity (uint8_t) per entry
- Add/remove by note number (not index)
- LastNote: return most recently pressed remaining note
- LowNote: return lowest note number among remaining
- HighNote: return highest note number among remaining
- Move-to-top for same-note re-press (LastNote mode)
- Drop oldest when full

**Data structure options:**

1. **std::array as linear list with count**: Simple contiguous array. Add at end, remove by shifting. O(n) for all operations but n <= 16. Cache-friendly.

2. **Sorted array**: Keep entries sorted by note number. O(n) insert/remove with shifting. Fast min/max (endpoints). Breaks LastNote ordering.

3. **Doubly-linked list within array**: Fixed array of nodes with prev/next indices. O(n) find by note, O(1) remove/insert at head/tail. Complex for 16 entries.

**Analysis for n=16:**
- Linear scan of 16 entries: 16 * 2 bytes = 32 bytes, fits in a single cache line
- All operations are O(16) at worst, well under the 500ns budget (SC-009)
- Simplicity matters more than asymptotic complexity at this scale

**Recommended structure:**
A simple linear array with an insertion-order count. Each entry stores `{note, velocity}`. New entries are appended at `count_`. Removal shifts entries left to maintain insertion order (for LastNote: most recent is at `count_ - 1`).

For priority evaluation:
- **LastNote**: The winner is always the last entry (`entries_[count_ - 1]`)
- **LowNote**: Linear scan for minimum note number
- **HighNote**: Linear scan for maximum note number

For same-note re-press in LastNote mode: remove the existing entry, then append at end (moves it to "most recent").

For stack-full handling: remove `entries_[0]` (oldest), shift everything left, append new at end.

### Decision

Use `std::array<NoteEntry, 16>` as a simple linear list with a `size_` counter. Entries maintain insertion order. This is the simplest correct implementation, cache-friendly, and well within performance budgets for n=16.

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Linked list in array | Over-engineered for 16 entries, more complex, no measurable perf benefit |
| Sorted array | Cannot maintain insertion order needed for LastNote priority |
| Ring buffer | Complex removal from middle, no clear benefit |

---

## R-003: Semitone-to-Frequency Conversion Strategy for Portamento

### Question

What is the most numerically stable and performant way to convert the portamento's semitone glide position to output frequency?

### Research

The portamento operates in semitone space (linear glide in semitones). The output must be frequency in Hz. Two approaches:

**Approach A: MIDI note as semitone value + semitonesToRatio()**
- Store portamento position as MIDI note number (float, e.g., 60.0 for C4, 66.0 for midpoint of C4-C5)
- Convert to frequency: `midiNoteToFrequency(int)` only works for integer notes
- For fractional semitones: `kA4FrequencyHz * semitonesToRatio(currentSemitones - 69.0f)`
- `semitonesToRatio()` uses `std::pow(2.0f, semitones / 12.0f)` which is a single transcendental call per sample

**Approach B: Direct frequency via exp2**
- Same math as Approach A but written differently: `440.0f * std::pow(2.0f, (semitones - 69.0f) / 12.0f)`
- Equivalent to `midiNoteToFrequency` but with fractional note support

**Approach C: Store log-frequency and exp at output**
- Internally glide in log2-frequency space
- Convert: `freq = a4 * exp2(logFreq)` at output
- Avoids the division by 12 per sample

**Performance consideration:**
- `std::pow(2.0f, x)` or `std::exp2(x)` is approximately 5-15ns on modern x86
- Called once per sample (44100/s), total = ~0.2-0.7ms per second = negligible
- No need for fast approximations

### Decision

**Approach A**: Use the MIDI note number as the semitone reference. The LinearRamp operates on semitone values directly. Convert to frequency at output using `kA4FrequencyHz * semitonesToRatio(currentSemitones - 69.0f)`. This reuses existing `semitonesToRatio()` from `pitch_utils.h` and is numerically clear.

The conversion in `processPortamento()` will be:
```cpp
float currentSemitones = portamentoRamp_.process();
return kA4FrequencyHz * semitonesToRatio(currentSemitones - static_cast<float>(kA4MidiNote));
```

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Direct frequency interpolation | Not linear in pitch space, violates FR-022 |
| Log-frequency space | Equivalent math, less readable, no perf benefit |
| Fast exp2 approximation | Unnecessary, std::pow cost is negligible at per-sample rate |

---

## R-004: MonoNoteEvent Design -- Return Value vs Output Parameter

### Question

Should `noteOn()` / `noteOff()` return a `MonoNoteEvent` by value, or write to an output parameter?

### Research

**Spec requirement (FR-001):** MonoNoteEvent is a simple aggregate with frequency (float), velocity (uint8_t), retrigger (bool), isNoteOn (bool). This is 8 bytes or less -- trivially copyable.

**VoiceAllocator pattern:** Returns `std::span<const VoiceEvent>` because it can generate multiple events per call (unison, steal + noteOn). The MonoHandler always produces exactly one event per noteOn/noteOff call.

**Options:**
1. **Return by value**: `[[nodiscard]] MonoNoteEvent noteOn(...)`. Simple, clear, no state management. Compiler will optimize (RVO/copy elision). 8 bytes returned in registers on x86-64.
2. **Output via span**: Match VoiceAllocator pattern. Over-engineered for single-event case.
3. **Output pointer parameter**: `void noteOn(int note, int vel, MonoNoteEvent* out)`. C-style, no benefit over return-by-value.

### Decision

**Return by value**. The MonoNoteEvent is tiny (8 bytes), always exactly one event per call, and return-by-value is the clearest API. This differs from VoiceAllocator's span pattern because the MonoHandler is fundamentally single-event.

```cpp
[[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept;
[[nodiscard]] MonoNoteEvent noteOff(int note) noexcept;
```

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| std::span return | Over-engineered for guaranteed single event |
| Output parameter | Less ergonomic than return value for single event |
| std::optional | Unnecessary -- noteOn/noteOff always produce an event |

---

## R-005: setMode() Re-evaluation Behavior

### Question

When `setMode()` is called while notes are held and the winning note changes, should it trigger portamento and what event is returned?

### Research

**Spec (FR-010):** "If the winning note changes, the portamento target is updated." This implies:
- Re-evaluate the note stack with the new priority
- If the winning note differs from current, update the portamento target
- Portamento glide applies (if enabled) from current position to new winner
- No MonoNoteEvent is returned from `setMode()` -- it is a setter with void return

**Design implication:** `setMode()` must:
1. Store the new mode
2. Find the winner under the new priority
3. If winner changed: update portamento target (which triggers a glide if portamento is active)
4. Return void (it's a setter, not an event generator)

The spec says "The method MUST be callable at any time without disrupting the note stack." So the stack itself is unchanged, only the priority evaluation changes.

### Decision

`setMode()` stores the new mode, re-evaluates the winner if notes are held, and updates the portamento target if the winner changes. It does not return an event. The pitch transition happens smoothly via portamento on the next `processPortamento()` calls.

---

## R-006: Memory Footprint Analysis (SC-012: <= 512 bytes)

### Question

Can the MonoHandler fit within 512 bytes including all internal state?

### Research

**Components and estimated sizes:**

| Component | Size (bytes) | Notes |
|-----------|-------------|-------|
| Note stack: 16 x NoteEntry (2 bytes each) | 32 | note (uint8_t) + velocity (uint8_t) |
| Note stack count | 4 | size_t or uint8_t (use uint8_t: 1 byte) |
| MonoMode enum | 1 | uint8_t |
| PortaMode enum | 1 | uint8_t |
| Legato enabled flag | 1 | bool |
| Active note index or MIDI note | 1 | int8_t or uint8_t |
| LinearRamp (portamento) | 20 | 5 floats: increment, current, target, rampTimeMs, sampleRate |
| Current frequency cache | 4 | float |
| Target semitones | 4 | float |
| Sample rate | 4 | float (or use LinearRamp's) |
| Portamento time (ms) | 4 | float |
| hasActiveNote flag | 1 | bool |
| Last portamento frequency | 4 | float |
| Padding/alignment | ~20 | struct alignment to 4/8 bytes |
| **Total estimate** | **~100 bytes** | Well under 512 |

Even with generous padding and additional state tracking, the total will be far under 512 bytes.

### Decision

The 512-byte budget is easily met. No special optimization needed. The LinearRamp is the largest sub-component at 20 bytes.

---

## R-007: Velocity 0 as NoteOff (FR-014) and Invalid Note Handling (FR-004a)

### Question

How should input validation be structured for clean, maintainable code?

### Research

**Requirements:**
- FR-004a: Notes outside [0, 127] are silently ignored (no-op)
- FR-014: Velocity 0 noteOn is treated as noteOff
- Edge case: MIDI note 0 and 127 are valid (from clarifications)

**VoiceAllocator pattern:** Uses `uint8_t` for note (0-255 range). Since valid MIDI is 0-127, and uint8_t can hold 0-255, the VoiceAllocator implicitly allows all uint8_t values.

**MonoHandler design choice:** Use `int` for the note parameter (matching `midiNoteToFrequency(int)`). This naturally allows negative values and >127, which we guard against. This is cleaner than using uint8_t where the caller might need to cast.

```cpp
MonoNoteEvent noteOn(int note, int velocity) noexcept {
    // FR-004a: invalid note range
    if (note < 0 || note > 127) return {0.0f, 0, false, false};
    // FR-014: velocity 0 = noteOff
    if (velocity <= 0) return noteOff(note);
    // ... normal processing
}
```

### Decision

Use `int` parameters for note and velocity to match `midiNoteToFrequency()` signature. Validate at entry point with early return. Return a "no-op" MonoNoteEvent (isNoteOn=false, retrigger=false) for invalid inputs.

---

## R-008: hasActiveNote() Semantics After All Notes Released

### Question

FR-026 says hasActiveNote() returns true "if the handler is in the release phase with a note still decaying." The MonoHandler does not own envelopes. How is this tracked?

### Research

The spec says: "It MUST return false only when the handler has been reset or all notes released." And the assumption section says: "When hasActiveNote() returns false (all notes released), getCurrentFrequency() still returns the last active note's frequency."

This means `hasActiveNote()` is simply: "are there notes in the stack?" It becomes false when the last note is released. The frequency is preserved for the caller's release envelope, but the handler considers itself inactive.

Re-reading FR-026 more carefully: "at least one note is held (the note stack is non-empty) **or** the handler is in the release phase with a note still decaying." The second clause suggests hasActiveNote() could remain true during release. But the MonoHandler doesn't track envelope release -- that's the caller's responsibility.

Given the design that MonoHandler does not own envelopes, the simplest correct interpretation is:
- `hasActiveNote() = (stackSize_ > 0)` -- true when notes are held
- After all notes released: hasActiveNote() returns false
- getCurrentFrequency() still returns the last frequency for envelope release

The "release phase" language in FR-026 is aspirational for the synth engine context. For the standalone MonoHandler, it means `stackSize_ > 0`.

### Decision

`hasActiveNote()` returns `stackSize_ > 0`. Simple and correct for the MonoHandler's scope.
