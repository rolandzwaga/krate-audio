# Research: Arpeggiator Core -- Timing & Event Generation

**Branch**: `070-arpeggiator-core` | **Date**: 2026-02-20

---

## Research Question 1: How should ArpeggiatorCore handle timing internally?

### Decision: Dedicated integer sample counter (NOT SequencerCore composition)

### Rationale

SequencerCore (Layer 1) is a per-sample tick-based engine that returns `bool tick()` indicating step changes and tracks gate state per sample. ArpeggiatorCore needs to emit *discrete events at specific sample offsets within a block*, which is fundamentally different:

- SequencerCore: sample-by-sample, caller iterates calling `tick()` each sample
- ArpeggiatorCore: block-oriented, needs to produce `ArpEvent` structs with `sampleOffset` fields

Composing SequencerCore would require calling `tick()` in a sample loop inside `processBlock()` and translating gate transitions to events -- this adds indirection with no benefit. Instead, ArpeggiatorCore replicates the proven timing math:

**Step duration calculation** (from SequencerCore, verified in source):
```cpp
// Tempo-synced:
float beatsPerStep = getBeatsForNote(noteValue_, noteModifier_);
float stepMs = (60000.0f / tempoBPM) * beatsPerStep;
size_t stepDurationSamples = static_cast<size_t>(stepMs * 0.001f * sampleRate);

// Free-running:
size_t stepDurationSamples = static_cast<size_t>(sampleRate / freeRateHz);
```

**Swing formula** (from SequencerCore `applySwingToStep`, verified in source at line 551):
```cpp
// Even steps (0, 2, 4...): duration * (1 + swing)
// Odd steps (1, 3, 5...):  duration * (1 - swing)
```

The integer `size_t` accumulator increments by 1 per sample. When `sampleCounter_ >= stepDurationSamples`, a step boundary has been reached. This is identical to SequencerCore's approach and guarantees zero drift per SC-008.

### Alternatives Considered

1. **Compose SequencerCore directly**: Rejected because SequencerCore tracks per-sample gate on/off state (for audio gating), while ArpeggiatorCore needs event-based output. API mismatch would require an ugly adapter layer.

2. **Floating-point accumulator**: Rejected because floating-point accumulation drifts over time. After 1000 steps at 120 BPM / 1/8 note / 44100 Hz, the error could be several samples. Integer counting has exactly 0 drift (SC-008 mandate).

3. **Extract timing into a shared Layer 0 utility**: Premature -- only two consumers exist (SequencerCore and ArpeggiatorCore), and their usage patterns differ enough that a shared abstraction would be awkward. Revisit if a third consumer appears.

---

## Research Question 2: How should pending NoteOff events be tracked across block boundaries?

### Decision: Fixed-capacity array of `PendingNoteOff` structs with remaining sample count

### Rationale

When gate length is < 100%, the NoteOff fires *after* the NoteOn by `gateLength% * stepDuration` samples. This NoteOff deadline may fall in a future block. When gate > 100%, the NoteOff fires *after* the next step's NoteOn, creating legato overlap -- and this too may span blocks.

The tracking structure (authoritative definition in `data-model.md`):
```cpp
struct PendingNoteOff {
    uint8_t note;
    size_t samplesRemaining;
};
```

Fixed array of 32 entries (matching `HeldNoteBuffer::kMaxNotes`), because Chord mode can produce up to 32 simultaneous notes, each needing a pending NoteOff.

At the start of each `processBlock()`:
1. Iterate through pending NoteOffs
2. For each, check if `samplesRemaining < blockSize` (strictly less than, NOT <=)
   - If `samplesRemaining == blockSize`, the NoteOff falls at the *start of the next block*
     (sampleOffset 0 of the next block), so it must be deferred by subtracting blockSize.
   - Using `<=` would place the event at sampleOffset = blockSize, which violates FR-002
     ([0, blockSize-1] range).
3. If `samplesRemaining < blockSize`: emit the NoteOff at `sampleOffset = samplesRemaining`
   and remove from the array
4. If not (`samplesRemaining >= blockSize`): subtract blockSize from `samplesRemaining` and defer

Within the block, when a step fires:
1. Schedule NoteOff by adding a new PendingNoteOff with `samplesRemaining = gateDuration`
2. If the NoteOff falls within the current block, emit it immediately

### Alternatives Considered

1. **Priority queue**: Rejected -- heap allocation in the queue's internal container violates FR-029.
2. **Circular buffer**: Rejected -- NoteOffs don't fire in strict FIFO order (swing + varying gate lengths can reorder them).
3. **Single pending NoteOff**: Rejected -- Chord mode can have 32 notes pending simultaneously.

---

## Research Question 3: How should latch modes be implemented?

### Decision: Dual-buffer approach with physical key tracking

### Rationale

Latch modes require distinguishing between "physically held keys" and "notes the arp should play." The implementation uses:

1. **`heldNotes_` (HeldNoteBuffer)**: The notes the arp currently arpeggiate over. In Latch Off mode, this matches physical keys. In Latch Hold/Add modes, this persists after key release.

2. **`physicalKeysHeld_` (simple counter)**: Tracks how many physical keys are currently pressed. When this reaches 0, latch logic activates.

3. **`latchActive_` (bool)**: True when all physical keys are released and latch is preserving the pattern.

**Latch Off behavior:**
- `noteOn()`: Forward to `heldNotes_.noteOn()`
- `noteOff()`: Forward to `heldNotes_.noteOff()`. If buffer becomes empty, emit NoteOff for current arp note.

**Latch Hold behavior:**
- `noteOn()`: If `latchActive_` is true (new key while latched), clear `heldNotes_` first, then add the note. Set `latchActive_ = false`.
- `noteOff()`: Decrement `physicalKeysHeld_`. Do NOT remove from `heldNotes_`. When `physicalKeysHeld_` reaches 0, set `latchActive_ = true`.

**Latch Add behavior:**
- `noteOn()`: Always add to `heldNotes_` (never clear).
- `noteOff()`: Decrement `physicalKeysHeld_`. Do NOT remove from `heldNotes_`. Pattern accumulates indefinitely.

### Alternatives Considered

1. **Separate latched buffer**: Two HeldNoteBuffers (active + latched). Rejected -- doubles memory for no benefit; the single buffer approach is simpler and what hardware arpeggiators use.

2. **Boolean per-note tracking**: Track which notes are physically held vs latched with a per-note flag. More complex than a simple counter, no added benefit since we only need to know "are any keys held?"

---

## Research Question 4: How should retrigger Beat mode detect bar boundaries?

### Decision: Sample-accurate bar boundary detection within the block using `transportPositionSamples` and `samplesPerBar()`

### Rationale

BlockContext provides `transportPositionSamples` (int64_t) and `samplesPerBar()` (size_t). To detect if a bar boundary falls within the current block:

```cpp
size_t barSamples = ctx.samplesPerBar();
if (barSamples == 0) return; // Prevent division by zero

// Position at start of block
int64_t blockStart = ctx.transportPositionSamples;
// Position at end of block
int64_t blockEnd = blockStart + static_cast<int64_t>(ctx.blockSize);

// Find next bar boundary >= blockStart
int64_t currentBar = blockStart / static_cast<int64_t>(barSamples);
int64_t nextBarSample = (currentBar + 1) * static_cast<int64_t>(barSamples);

// If the bar boundary is also at blockStart exactly (blockStart % barSamples == 0),
// we should detect it
if (blockStart % static_cast<int64_t>(barSamples) == 0) {
    nextBarSample = blockStart; // This IS the bar boundary
}

// Check if bar boundary falls within [blockStart, blockEnd)
if (nextBarSample >= blockStart && nextBarSample < blockEnd) {
    int32_t offset = static_cast<int32_t>(nextBarSample - blockStart);
    // Reset NoteSelector at this sample offset
}
```

This is sample-accurate -- the reset happens at the exact sample within the block where the bar boundary falls.

### Alternatives Considered

1. **PPQ-based detection**: Use quarter-note position instead of samples. Rejected -- BlockContext provides `transportPositionSamples`, not PPQ. Converting adds unnecessary computation and potential float precision issues.

2. **Check only at block start**: Only check if the block start is on a bar boundary. Rejected -- misses bar boundaries that fall mid-block, violating sample-accuracy requirements.

---

## Research Question 5: What is the correct processBlock() iteration strategy?

### Decision: Sample-by-sample iteration through the block with event emission at step boundaries

### Rationale

The block processing must handle multiple concerns simultaneously:
1. Pending NoteOffs from previous blocks
2. Step boundaries that trigger new NoteOn events
3. NoteOff deadlines from current block's NoteOn events
4. Transport start/stop transitions
5. Retrigger Beat bar boundary detection

A sample-by-sample approach (similar to SequencerCore's `tick()`) is the clearest implementation:

```
processBlock(ctx, outputEvents):
    if blockSize == 0: return 0
    if not enabled: return emitDisableNoteOff()
    if not isPlaying: return emitTransportStopNoteOff()

    eventCount = 0

    for sample in 0..blockSize-1:
        // Check pending NoteOffs
        for each pending NoteOff:
            if samplesRemaining == 0:
                emit NoteOff at sample
                remove from pending
            else:
                decrement samplesRemaining

        // Check retrigger Beat at this sample
        if retrigger == Beat and isBarBoundary(ctx, sample):
            selector_.reset()
            swingStepCounter_ = 0

        // Check step boundary
        if sampleCounter_ >= currentStepDuration_:
            sampleCounter_ = 0
            advance NoteSelector
            emit NoteOn at sample
            schedule NoteOff (add to pending)
            swingStepCounter_++
            recalculate next step duration with swing

        sampleCounter_++

    return eventCount
```

However, this sample-by-sample loop can be optimized. Instead of iterating every sample, we can calculate the next event time and jump directly:

```
// Calculate when the next step boundary or pending NoteOff fires
// Jump to that sample, emit the event, repeat
```

The optimized approach is preferred for CPU efficiency (arp steps are tens of thousands of samples apart), but the logic is equivalent. The implementation should use the jump-ahead approach for efficiency while the tests verify sample-accurate behavior.

### Alternatives Considered

1. **Per-block calculation only**: Calculate how many steps fit in the block, emit all events at once. Rejected -- does not handle mid-block swing changes, bar boundaries, or pending NoteOffs correctly.

2. **Event queue pre-scheduling**: Pre-compute all events for the block before emitting. This is essentially what the jump-ahead approach does, but formalized. Viable alternative, but the iterative approach is more maintainable.

---

## Research Question 6: How should the ArpEvent struct be designed for future extensibility?

### Decision: Minimal struct now with documented extension points

### Rationale

Phase 5 will add a `legato` flag for Slide behavior. Rather than over-designing now, the ArpEvent struct should be:

```cpp
struct ArpEvent {
    enum class Type : uint8_t { NoteOn, NoteOff };
    Type type;
    uint8_t note;
    uint8_t velocity;
    int32_t sampleOffset;
};
```

This is 8 bytes (1 + 1 + 1 + padding + 4). Adding a `bool legato` in Phase 5 fits within the existing padding byte. Adding a `uint8_t flags` bitfield in Phase 5+ also fits.

No `reserved` bytes or union tricks -- keep it simple and add fields when needed. The struct is used only in fixed-size stack arrays (64 entries), so size changes are trivial.

### Alternatives Considered

1. **Add flags field now**: `uint8_t flags = 0;` for future use. Rejected -- YAGNI for Phase 2. Adding it later is a non-breaking change.
2. **Use a variant type**: `std::variant<NoteOnEvent, NoteOffEvent>`. Rejected -- heap allocation risk, unnecessarily complex for 4-field struct.

---

## Research Question 7: Should step duration be recalculated per step or per block?

### Decision: Per step, at each step tick

### Rationale

Per FR-019b: "Step duration MUST be computed as a `size_t` integer (truncating float result) on each step tick." This handles tempo changes mid-block correctly: already-ticking steps complete at their original duration, and new steps use the updated tempo.

The spec explicitly states the tempo is not re-evaluated mid-step. This matches SequencerCore's behavior where `updateStepDuration()` is called when tempo changes but the current step completes at its original duration.

For the ArpeggiatorCore, the step duration is recalculated at each step boundary using the BlockContext's current tempo:

```cpp
// At step boundary:
if (tempoSync_) {
    float beatsPerStep = getBeatsForNote(noteValue_, noteModifier_);
    float stepMs = (60000.0f / ctx.tempoBPM) * beatsPerStep;
    currentStepDuration_ = static_cast<size_t>(stepMs * 0.001f * ctx.sampleRate);
} else {
    currentStepDuration_ = static_cast<size_t>(ctx.sampleRate / freeRateHz_);
}
// Apply swing
currentStepDuration_ = applySwing(currentStepDuration_);
```

### Alternatives Considered

1. **Per block**: Recalculate at the start of each processBlock() call. Rejected -- spec explicitly requires per-step calculation.
2. **Per sample**: Continuously update. Rejected -- wasteful and can cause mid-step duration changes that break timing guarantees.
