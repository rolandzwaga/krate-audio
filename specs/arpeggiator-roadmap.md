# Ruinae Arpeggiator — Software Roadmap

**Status**: In Progress (Phase 11a complete — Layout Restructure & Lane Framework) | **Created**: 2026-02-20

A dependency-ordered implementation roadmap for the Ruinae arpeggiator. Phases build incrementally — each one produces a testable, usable arpeggiator that the next phase extends.

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Dependency Graph](#dependency-graph)
3. [Phase 1: HeldNoteBuffer & Note Selection](#phase-1-heldnotebuffer--note-selection)
4. [Phase 2: Arpeggiator Core — Timing & Event Generation](#phase-2-arpeggiator-core--timing--event-generation)
5. [Phase 3: Ruinae Integration — Processor & Parameters](#phase-3-ruinae-integration--processor--parameters)
6. [Phase 4: Independent Lane Architecture](#phase-4-independent-lane-architecture)
7. [Phase 5: Per-Step Modifiers — Slide, Accent, Tie, Rest](#phase-5-per-step-modifiers--slide-accent-tie-rest)
8. [Phase 6: Ratcheting](#phase-6-ratcheting)
9. [Phase 7: Euclidean Timing Mode](#phase-7-euclidean-timing-mode)
10. [Phase 8: Conditional Trig System](#phase-8-conditional-trig-system)
11. [Phase 9: Generative Features — Spice/Dice & Humanize](#phase-9-generative-features--spicedice--humanize)
12. [Phase 10: Modulation Integration](#phase-10-modulation-integration)
13. [Phase 11a: Layout Restructure & Lane Framework](#phase-11a-layout-restructure--lane-framework)
14. [Phase 11b: Specialized Lane Types](#phase-11b-specialized-lane-types)
15. [Phase 11c: Interaction Polish](#phase-11c-interaction-polish)
16. [Phase 12: Presets & Polish](#phase-12-presets--polish)
17. [Risk Analysis](#risk-analysis)

---

## Executive Summary

The arpeggiator is decomposed into **12 phases**. The first 3 phases produce a **fully functional basic arpeggiator** (Up/Down/Random modes, tempo sync, latch, gate control, integrated into Ruinae with parameters and basic UI). Phases 4-10 incrementally add the advanced features that differentiate this from every other synth arpeggiator. Phases 11-12 handle dedicated UI and polish.

### Milestone Map

| Milestone | After Phase | What You Get |
|---|---|---|
| **MVP** | 3 | Working arp with 10 modes, tempo sync, gate, latch, octave range. Playable. |
| **Sequencer** ✅ | 6 | Per-step velocity/gate/pitch/ratchet lanes, TB-303 modifiers. Deep. |
| **Conditional** ✅ | 8 | Euclidean rhythms, conditional trigs (probability, A:B ratios, Fill, First). Evolving. |
| **Generative** ✅ | 9 | Spice/Dice mutation, humanize. Unique. |
| **Modulated** ✅ | 10 | Arp params as mod destinations (rate, gate, octave, swing, spice). Expressive. |
| **Lane Framework** ✅ | 11a | SEQ tab restructured, velocity + gate lanes editable with playheads. |
| **Complete** | 12 | Dedicated UI, preset arp patterns. Polished. |

### Existing Components Reused

| Component | Phase Used | How |
|---|---|---|
| `SequencerCore` | 2 | Timing math pattern (conceptual reuse only — not composed directly; see Phase 2 design decisions) |
| `EuclideanPattern` | 7 | Timing lane rhythm generation |
| `VoiceAllocator` | 3 | Arp triggers notes through existing allocator |
| `MonoHandler` | 5 | Portamento mechanism for slide |
| `BlockContext + NoteValue` | 2 | Tempo sync infrastructure |
| `ModulationEngine` | 10 | Arp params as mod destinations |
| `StepPatternEditor` | 11 | Lane editing UI |

---

## Dependency Graph

```
Phase 1: HeldNoteBuffer + NoteSelector (DSP Layer 1)
    |
Phase 2: ArpeggiatorCore - timing + event gen (DSP Layer 2)
    |
Phase 3: Ruinae integration - processor, params, basic UI
    |           |
    |     [MVP MILESTONE - playable arpeggiator]
    |
    +---> Phase 4: Independent lane architecture
    |         |
    |         +---> Phase 5: Per-step modifiers (slide/accent/tie/rest)
    |         |
    |         +---> Phase 6: Ratcheting
    |         |
    |         +---> Phase 7: Euclidean timing mode
    |                   |
    |                   +---> Phase 8: Conditional trig system
    |                              |
    |                              +---> Phase 9: Spice/Dice + Humanize
    |
    +---> Phase 10: Modulation integration (independent of 4-9)
    |
    +---> Phase 11a: Layout restructure + lane framework (vel/gate) ✅
              |
              +---> Phase 11b: Specialized lanes (pitch/ratchet/modifier/condition)
              |
              +---> Phase 11c: Interaction polish (trail, transforms, copy/paste)
                        |
                        +---> Phase 12: Presets & polish
```

Phases 4-9 are sequential (each extends the lane system). Phase 10 can run in parallel with 4-9. Phase 11a-c are sequential UI phases, each producing a usable increment.

---

## Phase 1: HeldNoteBuffer & Note Selection ✅ COMPLETE

**DSP Layer**: 1 (primitives)
**File**: `dsp/include/krate/dsp/primitives/held_note_buffer.h`
**Test**: `dsp/tests/unit/primitives/held_note_buffer_test.cpp`
**Spec**: `specs/069-held-note-buffer/spec.md`
**Branch**: `069-held-note-buffer`

### Purpose

Fixed-capacity, real-time-safe buffer that tracks currently held MIDI notes and provides ordered access for all arp modes.

### Components

#### HeldNoteBuffer

```cpp
struct HeldNote {
    uint8_t note;          // MIDI note number
    uint8_t velocity;      // Original velocity
    uint16_t insertOrder;  // Monotonic counter for As Played mode
};

class HeldNoteBuffer {
    static constexpr size_t kMaxNotes = 32;

    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void clear();

    size_t size() const;
    bool empty() const;

    // Sorted views (no allocation — sort in place or maintain parallel index)
    std::span<const HeldNote> byPitch() const;       // ascending
    std::span<const HeldNote> byInsertOrder() const;  // chronological
};
```

**Design decisions**:
- Fixed capacity 32 (more than any keyboard can realistically hold simultaneously)
- No heap allocation — array-backed
- Maintains both pitch-sorted and insertion-ordered views
- Duplicate note-on for same pitch updates velocity, doesn't add second entry

#### NoteSelector

```cpp
enum class ArpMode {
    Up, Down, UpDown, DownUp,
    Converge, Diverge,
    Random, Walk,
    AsPlayed, Chord
};

enum class OctaveMode { Sequential, Interleaved };

class NoteSelector {
    void setMode(ArpMode mode);
    void setOctaveRange(int octaves);       // 1-4
    void setOctaveMode(OctaveMode mode);

    // Advance to next note(s). Returns note(s) to play.
    // For Chord mode, may return multiple notes.
    ArpNoteResult advance(const HeldNoteBuffer& held);

    // Reset to beginning of pattern (for retrigger)
    void reset();
};

struct ArpNoteResult {
    std::array<uint8_t, 32> notes;      // MIDI note numbers (with octave offset applied)
    std::array<uint8_t, 32> velocities;
    size_t count;                        // 1 for most modes, N for Chord
};
```

**Design decisions**:
- NoteSelector is stateful (tracks current index, direction, octave)
- `advance()` consults the held buffer each call — handles notes added/removed mid-pattern
- For Up/Down/UpDown: index wraps and octave shifts automatically
- For Walk: clamp to [0, size-1], random +/-1 step
- For Converge: alternate low/high indices moving inward
- Chord: returns all held notes at once

### Test Coverage

- Add/remove notes, verify pitch and insertion ordering
- Each ArpMode with 1, 2, 3, 4+ held notes
- Octave wrapping: 4 notes over 3 octaves = 12-step cycle
- Edge cases: single note held, all notes released, note-on during arp
- Converge/Diverge with even and odd note counts
- Walk stays within bounds
- `reset()` returns to start

### Acceptance Criteria

- [x] Zero heap allocation in all operations (SC-003: verified by code inspection — no dynamic containers)
- [x] All 10 modes produce correct note sequences (SC-001: 37 test cases, 33,228 assertions)
- [x] Octave Sequential vs Interleaved produce distinct orderings (SC-002: verified by unit tests)
- [x] Buffer handles rapid add/remove without corruption (SC-004: 1000-op stress test passes)
- [ ] Unit tests pass on Windows, macOS, Linux (SC-007: Windows verified, macOS/Linux CI pending — push branch to confirm)

---

## Phase 2: Arpeggiator Core — Timing & Event Generation ✅ COMPLETE

**DSP Layer**: 2 (processors)
**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`
**Test**: `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
**Spec**: `specs/070-arpeggiator-core/spec.md`
**Branch**: `070-arpeggiator-core`
**Depends on**: Phase 1

### Purpose

Combines HeldNoteBuffer + NoteSelector with a dedicated integer timing accumulator to produce a self-contained arp processor that consumes MIDI input and emits timed arp events. Uses the same timing math pattern as SequencerCore (tempo-to-samples conversion, swing formula) but does NOT compose SequencerCore directly — SequencerCore tracks per-sample gate state, while ArpeggiatorCore emits discrete events at specific sample offsets within a block (see spec.md Clarifications Q1 and research.md Q1).

### Components

#### ArpEvent

```cpp
struct ArpEvent {
    enum class Type { NoteOn, NoteOff };
    Type type;
    uint8_t note;
    uint8_t velocity;
    int32_t sampleOffset;  // sample-accurate offset within the block
};
```

#### ArpeggiatorCore

```cpp
class ArpeggiatorCore {
    void prepare(double sampleRate, size_t maxBlockSize);
    void reset();

    // MIDI input
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);

    // Configuration
    void setEnabled(bool enabled);
    void setMode(ArpMode mode);
    void setOctaveRange(int octaves);
    void setOctaveMode(OctaveMode mode);
    void setTempoSync(bool sync);
    void setNoteValue(NoteValue val, NoteModifier mod);
    void setFreeRate(float hz);
    void setGateLength(float percent);       // 1-200%
    void setSwing(float percent);            // 0-75%
    void setLatchMode(LatchMode mode);       // Off, Hold, Add
    void setRetrigger(ArpRetriggerMode mode);   // Off, Note, Beat -- ArpRetriggerMode, NOT RetriggerMode (ODR hazard: RetriggerMode in envelope_utils.h has values Hard/Legato)

    // Per-block processing — fills output event buffer
    // Returns number of events written
    size_t processBlock(const BlockContext& ctx,
                        std::span<ArpEvent> outputEvents);

private:
    HeldNoteBuffer heldNotes_;
    NoteSelector selector_;
    // Dedicated integer timing accumulator (NOT SequencerCore)
    // Track currently-playing arp notes for noteOff generation (array, for Chord mode)
};
```

**Design decisions**:
- Sample-accurate event generation: events have `sampleOffset` within the block
- Gate length determines when noteOff fires relative to the step duration
- Gate > 100% means notes overlap (legato arpeggio)
- Swing implemented using the same formula as SequencerCore (even steps * (1 + swing), odd steps * (1 - swing)), but computed inline — SequencerCore is not composed
- When disabled, `processBlock()` returns 0 events — passthrough handled by caller
- Latch Hold: on all-keys-released, heldNotes_ persists; new keys replace
- Latch Add: new keys are appended to existing heldNotes_
- Retrigger Note: `selector_.reset()` on any noteOn
- Retrigger Beat: reset at bar boundaries using `ctx.transportPositionSamples`

### State Management

The arp must track which note(s) it most recently triggered so it can emit corresponding noteOff events:
- `currentArpNotes_` — fixed-capacity array (32) of MIDI notes currently sounding from the arp (supports Chord mode where multiple notes sound simultaneously; FR-025)
- `currentArpNoteCount_` — number of valid entries in `currentArpNotes_`
- On each step tick: emit noteOff for all `currentArpNotes_`, then noteOn for the new note(s)
- For gate < 100%: noteOff fires at `gateLength * stepDuration` samples after noteOn
- `pendingNoteOffs_` — fixed-capacity array (32) tracking noteOff deadlines that span across block boundaries (FR-026)

### Test Coverage

- Tempo sync: at 120 BPM, 1/8 note = 250ms = 11025 samples at 44.1k. Verify events land at correct sample offsets.
- Free rate: at 4 Hz, step = 250ms. Same timing verification.
- Gate length: 50% gate at 1/8 = noteOff at sample 5512, 200% gate = noteOff after next noteOn (overlap).
- Swing: even steps delayed by swing %, odd steps on time.
- Latch modes: release all keys, verify arp continues (Hold) or stops (Off).
- Retrigger: press new note, verify pattern resets (Note) or continues (Off).
- Empty buffer: no crashes, no events emitted.
- Single note: arp plays that note rhythmically with octave shifting.

### Acceptance Criteria

- [x] Sample-accurate event timing (within 1 sample of expected position) (SC-001: verified at 60/120/200 BPM with 1/4, 1/8, 1/16, 1/8T — 100+ steps each)
- [x] Zero allocation in processBlock() (SC-003: code inspection confirmed — no new/delete/malloc/vector/string/map)
- [x] All latch and retrigger combinations work correctly (SC-004: 3+ tests per latch mode; SC-005: 2+ tests per retrigger mode)
- [x] Swing produces audibly correct shuffle at various percentages (SC-006: verified at 0%/25%/50%/75% with correct even/odd ratios)
- [x] Gate overlap (>100%) produces legato — noteOff after next noteOn (SC-007: verified at 150% and 200%)

---

## Phase 3: Ruinae Integration — Processor & Parameters ✅ COMPLETE

**Plugin Layer**: `plugins/ruinae/`
**Files**:
- `plugins/ruinae/src/plugin_ids.h` — new parameter IDs (3000-3099)
- `plugins/ruinae/src/parameters/arpeggiator_params.h` — atomic param storage
- `plugins/ruinae/src/processor/processor.cpp` — MIDI routing & block processing
- `plugins/ruinae/src/controller/controller.cpp` — parameter registration
- `plugins/ruinae/resources/editor.uidesc` — basic UI controls in SEQ tab
**Test**: `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`
**Spec**: `specs/071-arp-engine-integration/spec.md`
**Branch**: `071-arp-engine-integration`
**Depends on**: Phase 2

### Purpose

Wire ArpeggiatorCore into the Ruinae processor, expose parameters to the host, and provide basic UI controls so the arp is playable.

### Parameter IDs (plugin_ids.h)

```cpp
// Arpeggiator (3000-3099)
kArpEnabledId           = 3000,
kArpModeId              = 3001,  // Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord
kArpOctaveRangeId       = 3002,  // 1-4
kArpOctaveModeId        = 3003,  // Sequential, Interleaved
kArpTempoSyncId         = 3004,
kArpNoteValueId         = 3005,  // dropdown index (21 values)
kArpFreeRateId          = 3006,  // 0.5-50 Hz
kArpGateLengthId        = 3007,  // 1-200%
kArpSwingId             = 3008,  // 0-75%
kArpLatchModeId         = 3009,  // Off, Hold, Add
kArpRetriggerId         = 3010,  // Off, Note, Beat
```

### Processor Integration

In `processEvents()`:
```
if arp enabled:
    route noteOn/noteOff to arpCore_.noteOn/noteOff()
else:
    route noteOn/noteOff to engine_.noteOn/noteOff() (existing path)
```

In the block processing loop:
```
ArpEvent events[64];
size_t count = arpCore_.processBlock(blockCtx, events);
for each event:
    if NoteOn:  engine_.noteOn(event.note, event.velocity)
    if NoteOff: engine_.noteOff(event.note)
```

### Parameter Storage (arpeggiator_params.h)

Follow the exact pattern of `trance_gate_params.h`:
```cpp
struct ArpeggiatorParams {
    std::atomic<bool> enabled{false};
    std::atomic<int> mode{0};
    std::atomic<int> octaveRange{1};
    std::atomic<int> octaveMode{0};
    std::atomic<bool> tempoSync{true};
    std::atomic<int> noteValue{10};        // default: 1/16
    std::atomic<float> freeRate{4.0f};
    std::atomic<float> gateLength{80.0f};  // percent
    std::atomic<float> swing{0.0f};
    std::atomic<int> latchMode{0};
    std::atomic<int> retrigger{0};
};
```

### Basic UI (editor.uidesc)

Minimal controls in the SEQ tab:
- Arp Enabled toggle
- Mode dropdown (10 entries)
- Octave Range knob (1-4)
- Rate knob (tempo sync dropdown or free Hz)
- Gate Length knob
- Swing knob
- Latch mode dropdown
- Retrigger dropdown

This is a **functional UI**, not the final design. Dedicated UI comes in Phase 11.

### State Serialization

Add arp parameters to `Processor::getState()` / `setState()` following the existing pattern (IBStreamer read/write).

### Test Coverage

- Round-trip: set parameters via host, verify arp behavior changes
- State save/load: serialize and deserialize arp params
- Arp on/off: toggle mid-playback, verify clean transition
- Pluginval: strictness level 5 passes with arp enabled

### Acceptance Criteria

- [x] Arp plays notes when enabled with keys held and transport running
- [x] All 11 parameters controllable from host (automation, presets)
- [x] State save/load preserves all arp settings
- [x] Pluginval level 5 passes
- [x] No audio glitches on arp enable/disable transitions
- [x] Zero compiler warnings

---

## Phase 4: Independent Lane Architecture ✅ COMPLETE

**DSP Layer**: 1 (primitives) + 2 (processors)
**Files**:
- `dsp/include/krate/dsp/primitives/arp_lane.h` — generic lane container
- `dsp/include/krate/dsp/processors/arpeggiator_core.h` — extend with lanes
**Test**: `dsp/tests/unit/primitives/arp_lane_test.cpp`
**Spec**: `specs/072-independent-lanes/spec.md`
**Branch**: `072-independent-lanes`
**Depends on**: Phase 3

### Purpose

Replace the single-value gate/velocity settings with **independent-length lanes** — the core architectural differentiator.

### Components

#### ArpLane<T>

A generic fixed-capacity step lane that cycles independently:

```cpp
template<typename T, size_t MaxSteps = 32>
class ArpLane {
    void setLength(size_t length);       // 1-32
    void setStep(size_t index, T value);
    T getStep(size_t index) const;
    size_t length() const;

    // Advance and return current value
    T advance();

    // Reset to step 0
    void reset();

    // Current position (for UI playhead)
    size_t currentStep() const;
};
```

#### Lane Types in ArpeggiatorCore

```cpp
ArpLane<float>   velocityLane_;    // 0.0-1.0 (normalized velocity)
ArpLane<float>   gateLane_;        // 0.01-2.0 (gate percent / 100)
ArpLane<int8_t>  pitchLane_;       // -24 to +24 semitones
```

**Each lane advances once per arp step tick**, independently. Because lengths differ, they produce polymetric patterns.

### Parameter Additions (plugin_ids.h)

```cpp
// Velocity Lane
kArpVelocityLaneLengthId    = 3020,
kArpVelocityLaneStep0Id     = 3021,  // through Step31Id = 3052

// Gate Lane
kArpGateLaneLengthId        = 3060,
kArpGateLaneStep0Id         = 3061,  // through Step31Id = 3092

// Pitch Lane
kArpPitchLaneLengthId       = 3100,
kArpPitchLaneStep0Id        = 3101,  // through Step31Id = 3132
```

**Note**: This exceeds the 3000-3099 range. Expand to 3000-3199 or use a wider allocation.

### Interaction with Phase 3 Controls

- `kArpGateLengthId` (Phase 3) becomes a **global multiplier** on the gate lane values
- `velocity from input` mode bypasses the velocity lane
- Pitch lane offsets are added to the note from NoteSelector

### Test Coverage

- Different lane lengths produce correct polymetric cycling
- LCM verification: lanes of length 3 and 5 don't repeat for 15 steps
- Lane reset on retrigger
- Edge: lane length 1 = constant value (equivalent to global setting)
- Per-step parameter set/get round-trip

### Acceptance Criteria

- [x] Lanes cycle independently at different lengths
- [x] Lane values correctly applied to arp events (pitch offset, velocity scale, gate length)
- [x] Lane state serialized/deserialized with plugin state
- [x] No allocation in advance() path

---

## Phase 5: Per-Step Modifiers — Slide, Accent, Tie, Rest ✅ COMPLETE

**DSP Layer**: 2 (processors)
**Files**: Extend `arpeggiator_core.h`
**Test**: Extend `arpeggiator_core_test.cpp`
**Spec**: `specs/073-per-step-mods/spec.md`
**Branch**: `073-per-step-mods`
**Depends on**: Phase 4

### Purpose

Add TB-303-inspired per-step modifiers to the timing lane.

### Components

Per-step modifier bits (stored as a bitmask lane):

```cpp
enum ArpStepFlags : uint8_t {
    kStepActive = 0x01,   // Note fires (default on). Off = Rest.
    kStepTie    = 0x02,   // Extend previous note, no retrigger
    kStepSlide  = 0x04,   // Portamento to next note, suppress envelope retrigger
    kStepAccent = 0x08,   // Velocity boost (e.g., +30 or to fixed 127)
};

ArpLane<uint8_t> modifierLane_;  // bitmask per step
```

### Behavior

| Flag | NoteOff | NoteOn | Envelope | Portamento |
|---|---|---|---|---|
| **Normal** | Previous noteOff at gate end | New noteOn | Retrigger | No |
| **Rest** | Previous noteOff at gate end | No noteOn | — | No |
| **Tie** | No noteOff | No new noteOn | Continue | No |
| **Slide** | No noteOff until next note | New noteOn (legato) | No retrigger | Yes |
| **Accent** | Normal | Normal + boosted velocity | Retrigger | No |

### Slide Implementation

Slide requires the voice to glide from the current pitch to the next. Two approaches:
1. **Leverage MonoHandler portamento**: Route arp through mono handler's portamento when slide flag is set
2. **Dedicated glide in ArpeggiatorCore**: Emit a "legato noteOn" event that the engine interprets as a pitch change without envelope retrigger

Approach 2 is preferred — it works in both poly and mono modes. The ArpEvent struct gains a `legato` flag:

```cpp
struct ArpEvent {
    // ... existing fields ...
    bool legato;  // true = don't retrigger envelope, apply portamento
};
```

### Parameter Additions

```cpp
// Modifier Lane (bitmask per step)
kArpModifierLaneLengthId    = 3140,
kArpModifierLaneStep0Id     = 3141,  // through Step31Id = 3172
kArpAccentVelocityId        = 3180,  // accent velocity boost amount
kArpSlideTimeId             = 3181,  // portamento time for slide (ms)
```

### Test Coverage

- Rest: step produces no noteOn, timing advances
- Tie: no noteOff between tied steps, note sustains
- Slide: legato flag set, portamento applied
- Accent: velocity boosted by configured amount
- Combinations: Tie + Accent, Rest followed by Slide, etc.
- Modifier lane cycles independently of other lanes

### Acceptance Criteria

- [x] Each modifier produces correct event behavior (34 FRs MET: Rest/Tie/Slide/Accent all verified with 70+ tests)
- [x] Slide produces audible portamento between notes (FR-015, FR-033, FR-034: legato ArpEvent → engine noteOn with portamento in both Poly and Mono modes)
- [x] Accent is clearly louder than non-accented steps (FR-019, SC-004: 8 accent/velocity combinations tested including overflow clamping)
- [x] Tie sustains without audible retrigger (FR-011, SC-005: 3-step tie chain verified with zero events in tied region)
- [x] Modifiers interact correctly with gate lane (tie overrides gate) (FR-012: Tie_OverridesGateLane test passes; priority chain Rest > Tie > Slide > Accent verified)

---

## Phase 6: Ratcheting ✅ COMPLETE

**DSP Layer**: 2 (processors)
**Files**: Extend `arpeggiator_core.h`, add `ArpLane<uint8_t> ratchetLane_`
**Test**: Extend `arpeggiator_core_test.cpp`
**Spec**: `specs/074-ratcheting/spec.md`
**Branch**: `074-ratcheting`
**Depends on**: Phase 4

### Purpose

Subdivide individual arp steps into rapid retriggered repetitions (1-4 per step).

### Behavior

When ratchet count is N for a step:
- The step duration is divided into N equal sub-steps
- Each sub-step triggers a noteOn with the same pitch/velocity
- Gate length applies per sub-step (not per full step)
- Ratchet 1 = normal behavior (no subdivision)

```
Step duration: |=========|
Ratchet 1:     |===------|  (one note, normal gate)
Ratchet 2:     |==-|==----|  (two retrigs)
Ratchet 3:     |=-|=-|=----|  (three retrigs)
Ratchet 4:     |=|=|=|=----|  (four retrigs, machine-gun)
```

### Implementation

In `processBlock()`, when a step has ratchet > 1:
- Calculate sub-step duration = stepDuration / ratchetCount
- Emit noteOn events at each sub-step boundary
- Apply gate length to each sub-step independently

### Parameter Additions

```cpp
// Ratchet Lane
kArpRatchetLaneLengthId     = 3190,
kArpRatchetLaneStep0Id      = 3191,  // through Step31Id = 3222
```

### Test Coverage

- Ratchet 1/2/3/4 produce correct number of noteOn events per step
- Sub-step timing is evenly divided
- Gate length applies per sub-step
- Ratchet lane cycles independently
- Ratchet + Tie interaction (tie should probably override ratchet)
- Ratchet + Accent (accent applies to first sub-step only? or all?)

### Acceptance Criteria

- [x] Ratchet subdivisions are sample-accurate (SC-001, SC-002: 4 test cases verify exact sample offsets for ratchet 1/2/3/4)
- [x] Each ratchet retrig is a distinct noteOn/noteOff pair (SC-001: verified for all ratchet counts)
- [x] No timing drift over many ratcheted steps (SC-011: zero drift after 100 consecutive ratchet-4 steps)
- [x] Ratchet count 1 is identical to no-ratchet behavior (SC-003: bit-identical output at 120/140/180 BPM)
- [x] Per-sub-step gate length with correct Tie/Slide look-ahead on last sub-step only (SC-004: 3+ gate/ratchet combinations verified)
- [x] Modifier interaction correct: Rest/Tie suppress, Accent/Slide first-sub-step-only (SC-005: 6 modifier tests pass)
- [x] Ratchet lane cycles independently (polymetric) (SC-006: 15-step combined cycle verified)
- [x] State persistence with Phase 5 backward compatibility (SC-007, SC-008: round-trip and EOF compat pass)
- [x] Zero heap allocation in ratchet code paths (SC-009: code inspection confirmed)
- [x] All 33 parameter IDs registered with correct flags (SC-010: kArpEndId=3299, kNumParameters=3300)
- [x] 43 new tests (34 DSP + 9 integration), pluginval L5 pass, clang-tidy 0 findings

---

## Phase 7: Euclidean Timing Mode ✅ COMPLETE

**DSP Layer**: 2 (processors)
**Files**: Extend `arpeggiator_core.h`, reuse `euclidean_pattern.h`
**Test**: Extend `arpeggiator_core_test.cpp`
**Spec**: `specs/075-euclidean-timing/spec.md`
**Branch**: `075-euclidean-timing`
**Depends on**: Phase 4

### Purpose

Replace the manual timing lane with Euclidean rhythm generation — E(k, n) distributes k pulses across n steps maximally evenly.

### Implementation

When Euclidean mode is enabled:
- The timing lane is auto-generated from `EuclideanPattern::generate(hits, steps)`
- User controls: **Hits** (k), **Steps** (n), **Rotation** (offset)
- The generated pattern determines which steps are active (note fires) vs silent (rest)
- All other lanes (velocity, gate, pitch, ratchet, modifier) still cycle at their own lengths

### Parameter Additions

```cpp
kArpEuclideanEnabledId      = 3230,
kArpEuclideanHitsId         = 3231,  // 0-32
kArpEuclideanStepsId        = 3232,  // 2-32
kArpEuclideanRotationId     = 3233,  // 0-31
```

### Notable Euclidean Rhythms to Test

| Pattern | Result | Musical Tradition |
|---|---|---|
| E(3,8) | `10010010` | Cuban tresillo |
| E(5,8) | `10110110` | Cuban cinquillo |
| E(5,16) | `1001001000100100` | Bossa nova |
| E(7,12) | `101101010110` | West African bell |
| E(7,16) | `1001010100101010` | Samba |

### Test Coverage

- Known Euclidean patterns match expected bitmasks
- Rotation shifts pattern correctly
- Hits > Steps clamped or produces all-active
- Hits = 0 produces silence
- Euclidean mode on/off transition doesn't glitch timing
- Other lanes still cycle at their own lengths over the Euclidean pattern

### Acceptance Criteria

- [x] Euclidean patterns match Bjorklund algorithm reference output (SC-001: 5 known patterns verified — E(3,8) tresillo, E(8,8), E(0,8), E(5,8) cinquillo, E(5,16) bossa nova)
- [x] Rotation produces audibly different rhythms from same hits/steps (SC-002: all 16 rotations of E(5,16) are distinct with exactly 5 hits each)
- [x] Integrates with existing lane system (Euclidean only controls which steps fire) (SC-003: polymetric cycling verified — Euclidean steps=5 + velocity lane=3 = 15-step combined cycle)
- [x] Realtime-safe pattern generation (SC-010: zero heap allocation confirmed — EuclideanPattern::generate() is constexpr/static/noexcept, all state uses fixed-size types)
- [x] Euclidean disabled = Phase 6 identical output (SC-004: zero-tolerance test at 120/140/180 BPM — same notes, velocities, offsets, legato flags)
- [x] On/off transitions clean with no stuck notes (SC-005: 4 transition tests pass)
- [x] Rest breaks tie chain, ratchet interaction correct (SC-006, SC-007: verified)
- [x] State round-trip preserves all 4 values, Phase 6 backward compat (SC-008, SC-009: verified)
- [x] 4 parameter IDs (3230-3233) registered with kCanAutomate (SC-011: verified)
- [x] 44 new tests (37 DSP + 6 integration + 1 param registration), pluginval L5 pass, clang-tidy 0 findings
- [x] 35/35 functional requirements MET, 12/12 success criteria MET

---

## Phase 8: Conditional Trig System ✅ COMPLETE

**DSP Layer**: 2 (processors)
**Files**: Extend `arpeggiator_core.h`, add condition lane
**Test**: Extend `arpeggiator_core_test.cpp`
**Spec**: `specs/076-conditional-trigs/spec.md`
**Branch**: `076-conditional-trigs`
**Depends on**: Phase 7 (because Euclidean determines which steps exist to apply conditions to)

### Purpose

Elektron-inspired conditional triggers that create patterns evolving over multiple loops.

### Components

```cpp
enum class TrigCondition : uint8_t {
    Always,          // Always fires
    Prob10, Prob25, Prob50, Prob75, Prob90,  // Probability
    Ratio_1_2,       // Fire on 1st of every 2 loops
    Ratio_2_2,       // Fire on 2nd of every 2 loops
    Ratio_1_3,       // Fire on 1st of every 3 loops
    Ratio_2_3,
    Ratio_3_3,
    Ratio_1_4,
    Ratio_2_4,
    Ratio_3_4,
    Ratio_4_4,
    First,           // Only on first loop ever
    Fill,            // Only when fill mode active
    NotFill,         // Only when fill mode NOT active
};
```

### State Tracking

The arp must track:
- `loopCount_` — increments each time the pattern completes a full cycle
- `fillActive_` — toggled by a parameter (performance control)
- A PRNG state for probability evaluation (seeded, deterministic per pattern position for consistency)

### Parameter Additions

```cpp
// Condition Lane
kArpConditionLaneLengthId   = 3240,
kArpConditionLaneStep0Id    = 3241,  // through Step31Id = 3272
kArpFillToggleId            = 3280,  // performance toggle
```

### Evaluation Logic

On each step:
1. Check timing lane (is this step active?)
2. Check condition lane for this step's condition
3. Evaluate: probability → PRNG check, A:B → `loopCount % B == A-1`, Fill → `fillActive_`, etc.
4. If condition passes → fire note. If not → treat as rest.

### Test Coverage

- Probability: over 1000 iterations, X% condition fires approximately X% of the time
- A:B ratios: verify exact loop-count behavior (1:2 fires on loops 0, 2, 4...)
- First: fires only on loop 0, never again
- Fill/NotFill: respects fillActive_ state
- Condition lane cycles independently of other lanes
- Interaction with Euclidean: condition applies on top of Euclidean active steps

### Acceptance Criteria

- [x] Probability conditions produce statistically correct distribution (SC-001: Prob10/25/50/75/90 all within +/-3% over 10,000 iterations)
- [x] A:B ratios are deterministic and cycle correctly (SC-002: all 9 ratios verified across 12 loops with exact loop indices)
- [x] Fill toggle works as a real-time performance control (SC-004: Fill/NotFill toggle, preserved across resets, step-boundary responsive)
- [x] Conditions compose correctly with Euclidean timing and rest flags (SC-006: Euclidean rest = condition not evaluated/PRNG not consumed; Euclidean hit + condition fail = rest)
- [x] First-loop-only condition fires once then never again until reset (SC-003: verified with length-1 and length-4 lanes)
- [x] Default Always condition = Phase 7 identical output (SC-005: zero-tolerance bit-identical at 120/140/180 BPM)
- [x] Condition + modifier/ratchet interactions correct (SC-007: condition fail breaks tie, suppresses ratchet; condition pass respects modifier Rest)
- [x] Polymetric cycling with condition lane (SC-008: condition lane 3 + velocity lane 5 = 15-step cycle)
- [x] State round-trip preserves all condition values (SC-009: length, 32 steps, fill toggle all preserved exactly)
- [x] Phase 7 backward compatibility (SC-010: old presets default to length=1, all Always, fill=off)
- [x] Zero heap allocation in condition paths (SC-011: code inspection confirmed)
- [x] 34 parameter IDs registered with correct flags (SC-012: steps hidden, length/fill visible, all automatable)
- [x] Loop count lifecycle correct (SC-013: resets on retrigger/re-enable, NOT on disable alone)
- [x] Dedicated PRNG (seed 7919) distinct from NoteSelector (seed 42) (SC-014: sequences differ)
- [x] 51 new tests (37 DSP + 7 param + 7 integration), pluginval L5 pass, clang-tidy 0 findings
- [x] 48/48 functional requirements MET, 14/14 success criteria MET

---

## Phase 9: Generative Features — Spice/Dice & Humanize ✅ COMPLETE

**DSP Layer**: 2 (processors)
**Files**: Extend `arpeggiator_core.h`
**Test**: Extend `arpeggiator_core_test.cpp`
**Spec**: `specs/077-spice-dice-humanize/spec.md`
**Branch**: `077-spice-dice-humanize`
**Depends on**: Phase 8

### Purpose

Controlled randomization (Spice/Dice) and timing humanization for organic, evolving patterns.

### Spice/Dice

**Dice** (trigger action): When activated, generates a random variation overlay for velocity, gate, ratchet, and condition lanes. The original lane values are preserved.

**Spice** (continuous 0-100%): Controls how much the variation deviates from original values.

```cpp
// Per lane, variation is computed as:
effectiveValue = lerp(originalValue, randomVariation, spice / 100.0f);
```

Implementation:
- Store original lane values separately from variation overlay
- Dice generates new random values for the overlay
- Spice blends between original and overlay at read time
- Spice = 0% → original pattern. Spice = 100% → fully random variation.

### Humanize

Global knob (0-100%) that adds random per-step offsets:
- **Timing**: +/- 0 to 20ms random offset on each noteOn
- **Velocity**: +/- 0 to 15 random offset on each velocity value
- **Gate**: +/- 0 to 10% random offset on each gate value

At 0% humanize, everything is quantized. At 100%, maximum variation.

### Parameter Additions

```cpp
kArpSpiceId                 = 3290,  // 0-100%
kArpDiceTriggerId           = 3291,  // momentary trigger (button)
kArpHumanizeId              = 3292,  // 0-100%
```

### Test Coverage

- Spice 0% = output matches original lane values exactly
- Spice 100% = output matches random overlay exactly
- Dice generates different overlays on each trigger
- Humanize 0% = sample-accurate timing, exact velocities
- Humanize 100% = measurable timing/velocity spread
- Statistical distribution of humanize offsets is roughly uniform

### Acceptance Criteria

- [x] Spice/Dice preserves original pattern (Spice 0% always recovers it) (SC-001: zero-tolerance bit-identical at 120/140/180 BPM across 1000+ steps)
- [x] Dice is re-triggerable and produces different results each time (SC-004: consecutive triggerDice() calls produce different overlays)
- [x] Humanize produces musically natural variation (not chaotic) (SC-006/007/008: timing +/-20ms, velocity +/-15, gate +/-10% at full Humanize; SC-009: linear scaling at 50%)
- [x] All randomization is real-time safe (no allocation, fast PRNG) (SC-012: zero heap allocation confirmed by code inspection; triggerDice() is noexcept)
- [x] Spice blend correct for all lane types: velocity/gate lerp, ratchet round, condition threshold (SC-002/003: 4 blend formula tests pass)
- [x] Humanize PRNG consumed on every step including skips (FR-023: 5 skip points + defensive branch verified)
- [x] Plugin integration: 3 params (3290-3292) registered, automatable, correct display (SC-013: all verified)
- [x] State persistence: round-trip preserves Spice/Humanize; Phase 8 backward compat (SC-010/011: verified)
- [x] Spice and Humanize compose correctly without interference (SC-015: both effects measurably present)
- [x] 45 new tests (33 DSP + 7 param + 5 integration), pluginval L5 pass, clang-tidy 0 findings
- [x] 41/41 functional requirements MET, 15/15 success criteria MET

---

## Phase 10: Modulation Integration ✅ COMPLETE

**Plugin Layer**: `plugins/ruinae/`
**Files**:
- `plugins/ruinae/src/engine/ruinae_engine.h` — RuinaeModDest enum extension (ArpRate=74 through ArpSpice=78)
- `plugins/shared/src/ui/mod_matrix_types.h` — kNumGlobalDestinations 10→15, kGlobalDestNames extended
- `plugins/ruinae/src/controller/controller.cpp` — kGlobalDestParamIds extended
- `plugins/ruinae/src/processor/processor.cpp` — mod offset reads + application in applyParamsToEngine()
**Test**: `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`
**Spec**: `specs/078-modulation-integration/spec.md`
**Branch**: `078-modulation-integration`
**Depends on**: Phase 3 (independent of Phases 4-9)

### Purpose

Expose arpeggiator parameters as modulation destinations in the existing ModulationEngine (13 sources, 32 routings).

### Modulation Destinations

| Parameter | RuinaeModDest | Dest Index | Formula | Range |
|---|---|---|---|---|
| Arp Rate | ArpRate = 74 | 10 | `baseRate * (1.0 + 0.5 * offset)` | [0.5, 50.0] Hz |
| Gate Length | ArpGateLength = 75 | 11 | `baseGate + 100.0 * offset` | [1.0, 200.0]% |
| Octave Range | ArpOctaveRange = 76 | 12 | `baseOctave + round(3.0 * offset)` | [1, 4] |
| Swing | ArpSwing = 77 | 13 | `baseSwing + 50.0 * offset` | [0.0, 75.0]% |
| Spice | ArpSpice = 78 | 14 | `baseSpice + offset` (bipolar) | [0.0, 1.0] |

### Implementation

No new DSP components or parameter IDs. The existing ModulationEngine writes offsets per block; the processor reads them in `applyParamsToEngine()` and applies modulated values to arpCore_ setters. Rate modulation handles both free-rate and tempo-sync modes. Octave range uses change detection via `prevArpOctaveRange_`. Mod reads are skipped when arp is disabled (FR-015).

### Test Coverage

- 27 test cases covering all 5 destinations: free-rate +/-, tempo-sync +/-, gate +/-, octave expansion/clamping/change-detection, swing +/clamp, spice bipolar/clamp/negative, zero-offset identity
- Save/load roundtrip for single and all-5 destinations
- Phase 9 preset backward compatibility
- Existing destination unchanged after extension
- 10,000-block stress test (zero NaN/Inf)
- All 5 destinations simultaneous

### Acceptance Criteria

- [x] All 5 arp parameters available as mod destinations in UI dropdown (SC-001: kGlobalDestNames indices 10-14)
- [x] Modulation is block-rate accurate with 1-block latency (SC-002: ArpRateFreeMode_PositiveOffset test passes)
- [x] No clicks or glitches when mod values change rapidly (SC-003: 10,000-block stress test, 0 NaN, 0 Inf)
- [x] Mod routings serialize/deserialize correctly (SC-004: save/load roundtrip byte-identical)
- [x] Zero mod offset = Phase 9 identical behavior (SC-005: zero-offset tests pass for rate, gate, swing)
- [x] All formulas produce correct effective values (SC-006: 15 formula tests pass)
- [x] Real-time safe: zero heap allocation in mod application path (SC-007: code inspection confirmed)
- [x] Existing destinations unchanged (SC-008: 508 tests, 8800 assertions all pass)
- [x] Phase 9 presets load without error (SC-009: Phase9Preset_NoArpModActive test passes)
- [x] static_assert protects enum mapping invariant (SC-010: ArpRate == GlobalFilterCutoff + 10)
- [x] Pluginval strictness 5 pass (SC-011: exit code 0)
- [x] Clang-tidy 0 errors, 0 warnings (SC-012: 235 files analyzed)
- [x] 20/20 functional requirements MET, 12/12 success criteria MET

---

## Phase 11 — Arpeggiator UI (Overview)

Phase 11 is split into three sequential sub-phases. Each produces a usable, testable increment.

### Design Principles

- **Shared UI components**: All new arpeggiator UI views and controls MUST be implemented in `plugins/shared/src/ui/`, NOT in `plugins/ruinae/`. These are reusable components (lane editors, lane containers, modifier/condition views) that belong in shared infrastructure. Only Ruinae-specific layout and wiring (e.g., `editor.uidesc`, sub-controller registration) lives in `plugins/ruinae/`.
- **Reuse over rebuild**: Extend `StepPatternEditor` rather than creating from scratch. New custom views only for lane types that fundamentally differ from bar charts (modifier bitmask, condition enum).
- **Stacked multi-lane view**: All lanes visible simultaneously in a vertically scrollable container, sized to content. No tab-switching between lanes.
- **Per-lane playheads**: Each lane tracks its own position independently (polymetric support). Playhead includes a 2-3 step fading trail and an X overlay on steps that were evaluated but skipped (by condition/probability).
- **Left-aligned steps**: Shorter lanes display wider bars. Step 1 always aligns across lanes, making polymetric relationships visually obvious.
- **Collapsible lanes**: Each lane header is clickable to collapse/expand. Collapsed state shows a miniature bar/dot preview in the lane's accent color.
- **Progressive disclosure**: Warm/primary lanes (velocity, gate) at top; cooler/specialized lanes (modifier, condition) at bottom.

### Lane Color Palette

Cohesive earth-tone family under the arpeggiator copper (#C87850):

| Lane | Color | Hex | Rationale |
|------|-------|-----|-----------|
| Velocity | Copper | `#D0845C` | Warmest — primary lane, closest to parent |
| Gate | Sand | `#C8A464` | Pairs naturally with velocity |
| Pitch | Sage | `#6CA8A0` | Cool contrast for the "musical" lane |
| Ratchet | Lavender | `#9880B0` | Distinctive for the "exotic" rhythmic feature |
| Modifier | Rose | `#C0707C` | Alert-adjacent tone for flags/toggles |
| Condition | Slate | `#7C90B0` | Cool neutral for logic/probability |

All colors at similar saturation (~40-50%) and lightness (~55-60%) to avoid visual noise against the #1A1A1E background.

### SEQ Tab Layout (1400 x 620 content area)

The Trance Gate is shrunk from 390px to ~100px, freeing ~510px for the arpeggiator.

```
SEQ Tab (1400 x 620)
+================================================================+ y=0
|  TRANCE GATE  [ON] Steps:[16] [Presets▾] [↕][←][→]            |
|  Sync:[●] Rate:[1/16▾] Depth:[●] Atk:[●] Rel:[●] Phase:[●]   |
|  [Eucl] Hits:[●] Rot:[●]                                      | ~26px toolbar
|  ┌──────────────────────────────────────────────────────────┐  |
|  │  Thin StepPatternEditor bars (~70px)                     │  |
|  └──────────────────────────────────────────────────────────┘  |
+================================================================+ y≈104
|  ─── divider ───                                               |
+================================================================+ y≈108
|  ARPEGGIATOR  [ON] Mode:[Up▾] Oct:[2] [Seq▾]                  |
|  Sync:[●] Rate:[1/16▾] Gate:[●] Swing:[●]                     |
|  Latch:[Hold▾] Retrig:[Note▾]                                 | ~40px toolbar
+----------------------------------------------------------------+ y≈148
|                                                                |
|  SCROLLABLE LANE EDITOR (CScrollView, ~390px viewport)        |
|  ┌──────────────────────────────────────────────────────────┐  |
|  │ ▼ VEL  [16▾] ████▓▓██  ████▓▓  ██████               │  |
|  │               copper bars, 0-1 normalized       ~70px    │  |
|  │                                                          │  |
|  │ ▼ GATE [16▾] ██▓▓████  ████▓▓  ▓▓████               │  |
|  │               sand bars, 0-200% gate length     ~70px    │  |
|  │                                                          │  |
|  │ ▼ PITCH [8▾]  ▲    ▲                                │  |
|  │               ─────── 0 ────────                         │  |
|  │                          ▼    ▼                          │  |
|  │               sage bipolar bars, -24..+24 semi   ~70px   │  |
|  │                                                          │  |
|  │ ▼ RATCH [8▾] [2] [1] [3] [1] [4] [1]               │  |
|  │               lavender blocks, click cycles 1-4  ~36px   │  |
|  │                                                          │  |
|  │ ▼ MOD  [16▾]                                         │  |
|  │  Rest    ·  ●  ·  ·  ·  ●  ·  ·  ·  ·  ·  ·  ●  ·    │  |
|  │  Tie     ·  ·  ●──●  ·  ·  ·  ·  ·  ·  ●──●  ·  ·    │  |
|  │  Slide   ·  ·  ·  ·  ●  ·  ·  ·  ·  ·  ·  ·  ·  ●   ~44px│
|  │  Accent  ●  ·  ·  ·  ·  ·  ●  ·  ·  ●  ·  ·  ·  ·    │  |
|  │               rose toggle dots per step                  │  |
|  │                                                          │  |
|  │ ▼ COND  [8▾] [⚡][½][⚡][¼][2x][⚡][F][⚡]          │  |
|  │               slate icons + popup menu           ~28px   │  |
|  │               Alw 50% Alw 25% Ev2 Alw Fill Alw          │  |
|  └──────────────────────────────────────────────────────────┘  |
|  ◄ per-lane playhead with trail, ✕ on skipped steps ►         |
|                                                                |
+================================================================+ y≈540
|  Euclidean: [ON] Hits:[●] Steps:[●] Rot:[●]  ○●○●●○●●        |
|  Humanize:[●]  Spice:[●]  RatchSwing:[●]    [DICE]  [FILL]   |
+================================================================+ y≈620
```

**Lane header legend**: `▼` = collapse toggle, `[16▾]` = length dropdown. Note: transform buttons (`[↕]` invert, `[←][→]` shift, `[?]` randomize) are Phase 11c scope and are NOT implemented in Phase 11b.

### Lane Height Summary

| Lane | Expanded Height | Content |
|------|----------------|---------|
| Velocity | ~70px | Bar chart (0.0-1.0) |
| Gate | ~70px | Bar chart (0-200%) |
| Pitch | ~70px | Bipolar bars (-24..+24), center line at 0 |
| Ratchet | ~36px | Discrete blocks (1-4), click to cycle |
| Modifier | ~44px | 4-row dot toggle grid (Rest/Tie/Slide/Accent) |
| Condition | ~28px | Icon per step + popup menu (18 conditions) |
| **Headers** | ~16px × 6 | Label, length control, transform buttons |
| **Total** | ~414px | Fits in 390px viewport with 1 lane collapsed or small scroll |

### Mouse Interaction Summary

| Lane Type | Click | Drag | Right-Click |
|-----------|-------|------|-------------|
| Bar (Vel/Gate) | Set step level | Paint across steps | Set to 0 |
| Bipolar (Pitch) | Set semitone (snap to integer) | Paint, up=positive down=negative | Set to 0 |
| Discrete (Ratchet) | Cycle 1→2→3→4→1 | Drag up/down to change | Reset to 1 |
| Toggle (Modifier) | Toggle individual flag dot | — | — |
| Enum (Condition) | Open COptionMenu popup | — | Reset to Always |
| Lane header | Collapse/expand lane | — | Copy/paste context menu |

### Components to Build or Extend

| Component | Approach | Location |
|-----------|----------|----------|
| **ArpLaneEditor** | Subclass of StepPatternEditor | `plugins/shared/src/ui/arp_lane_editor.h` |
| **ArpLaneContainer** | CScrollView holding stacked lanes | `plugins/shared/src/ui/arp_lane_container.h` |
| **ArpModifierLane** | New custom CView (4-row dot grid) | `plugins/shared/src/ui/arp_modifier_lane.h` |
| **ArpConditionLane** | New custom CView (icon + popup) | `plugins/shared/src/ui/arp_condition_lane.h` |
| **ArpRatchetLane** | ArpLaneEditor in discrete mode | Extension of ArpLaneEditor |
| **ArpPitchLane** | ArpLaneEditor in bipolar mode | Extension of ArpLaneEditor |

---

## Phase 11a: Layout Restructure & Lane Framework ✅ COMPLETE

**Plugin Layer**: `plugins/ruinae/` and `plugins/shared/`
**Files**:
- `plugins/ruinae/resources/editor.uidesc` — SEQ tab layout restructure
- `plugins/shared/src/ui/arp_lane_editor.h` — ArpLaneEditor (StepPatternEditor subclass)
- `plugins/shared/src/ui/arp_lane_container.h` — scrollable lane container
**Depends on**: Phases 4-10 (all arp engine features stable)

### Purpose

Restructure the SEQ tab layout and build the lane editor framework. Prove the architecture with the two simplest lane types (velocity and gate).

### Scope

1. **Shrink Trance Gate**: Consolidate toolbar to single compact row (~26px), reduce StepPatternEditor to ~70px. Total Trance Gate height: ~100px.
2. **Expand Arpeggiator section**: Move existing Phase 3 controls into a compact toolbar row (~40px).
3. **ArpLaneEditor**: Subclass StepPatternEditor with:
   - Lane type enum (Velocity, Gate, Pitch, Ratchet — bar-based lanes)
   - Value range mapping (normalized ↔ plain value per lane type)
   - Lane accent color
   - Per-lane length control bound to parameter
   - Per-lane playhead position (independent of other lanes)
   - Collapsible header with miniature bar preview when collapsed
4. **ArpLaneContainer**: CScrollView that stacks ArpLaneEditor instances vertically. Handles:
   - Dynamic height calculation based on expanded/collapsed state
   - Scroll when total content exceeds viewport (~390px)
   - Left-aligned steps across lanes (wider bars for shorter lanes)
5. **Velocity lane**: ArpLaneEditor in default mode (0.0-1.0 bars). Wire to `kArpVelocityLaneStep0Id`..`kArpVelocityLaneStep31Id`.
6. **Gate lane**: ArpLaneEditor with 0-200% range. Wire to `kArpGateLaneStep0Id`..`kArpGateLaneStep31Id`.
7. **Basic playhead**: Per-lane step highlight synced from audio thread via controller callback. No trail yet.
8. **Lane color scheme**: Register Velocity (#D0845C) and Gate (#C8A464) colors in uidesc.

### Acceptance Criteria

- [x] Trance Gate section ≤ 100px tall, fully functional at reduced size
- [x] Velocity and Gate lanes visible, editable, with per-lane playhead
- [x] Lane collapse/expand works, collapsed lanes show miniature preview
- [x] Scrollable container scrolls when both lanes are expanded
- [x] Per-lane length dropdown changes step count immediately
- [x] Parameter changes round-trip: UI → processor → UI (host automation works)
- [x] No allocations in draw/mouse paths (verified with ASan)
- [x] Pluginval level 5 passes

---

## Phase 11b: Specialized Lane Types

**Plugin Layer**: `plugins/shared/` and `plugins/ruinae/`
**Files**:
- `plugins/shared/src/ui/arp_lane_editor.h` — bipolar + discrete modes
- `plugins/shared/src/ui/arp_modifier_lane.h` — new modifier toggle view
- `plugins/shared/src/ui/arp_condition_lane.h` — new condition icon view
- `plugins/ruinae/resources/editor.uidesc` — add remaining lanes
**Depends on**: Phase 11a

### Purpose

Implement the four specialized lane types that require custom rendering and interaction beyond standard bar charts.

### Scope

1. **Pitch lane** (ArpLaneEditor, bipolar mode):
   - Center line drawn at 0 semitones
   - Bars extend up (positive) or down (negative) from center
   - Range: -24 to +24 semitones
   - Snap to integer semitone values (no fractional)
   - Drag up from center = positive, down = negative
   - Wire to `kArpPitchLaneStep0Id`..`kArpPitchLaneStep31Id`
   - Lane color: Sage (#6CA8A0)

2. **Ratchet lane** (ArpLaneEditor, discrete mode):
   - Stacked blocks visualization (1-4 blocks per step)
   - Click to cycle: 1→2→3→4→1
   - Drag up/down to change value
   - Right-click resets to 1
   - Wire to `kArpRatchetLaneStep0Id`..`kArpRatchetLaneStep31Id`
   - Lane color: Lavender (#9880B0)

3. **Modifier lane** (ArpModifierLane, new custom CView):
   - 4-row dot grid layout: Rest, Tie, Slide, Accent
   - Each dot is a clickable toggle (filled ● = active, outline ○ = inactive)
   - Row labels on the left margin
   - Steps aligned to match other lanes (left-aligned, width matches lane length)
   - Collapsible with miniature dot preview
   - Wire to `kArpModifierLaneStep0Id`..`kArpModifierLaneStep31Id` (bitmask encoding)
   - Lane color: Rose (#C0707C)

4. **Condition lane** (ArpConditionLane, new custom CView):
   - One icon cell per step showing the active condition
   - Condition label text below icon (abbreviated: "Alw", "50%", "Ev2", etc.)
   - Click opens COptionMenu popup with all 18 conditions
   - Right-click resets to Always
   - Tooltips on hover showing full condition name
   - Steps aligned with other lanes
   - Collapsible with miniature icon preview
   - Wire to `kArpConditionLaneStep0Id`..`kArpConditionLaneStep31Id`
   - Lane color: Slate (#7C90B0)

5. **Integrate all lanes into ArpLaneContainer** in display order: Velocity, Gate, Pitch, Ratchet, Modifier, Condition.

### Condition Icons Reference

> Updated to match the TrigCondition enum as implemented in Phase 8 (specs/076-conditional-trigs/). The original roadmap table was a pre-implementation draft and has been superseded.

| Index | Enum Value | Name | Abbrev | Icon Concept |
|-------|-----------|------|--------|--------------|
| 0 | Always | Always | Alw | bolt |
| 1 | Prob10 | 10% | 10% | dice |
| 2 | Prob25 | 25% | 25% | dice |
| 3 | Prob50 | 50% | 50% | half |
| 4 | Prob75 | 75% | 75% | three-quarter |
| 5 | Prob90 | 90% | 90% | dice |
| 6 | Ratio_1_2 | Every 2 | Ev2 | 2x |
| 7 | Ratio_2_2 | 2nd of 2 | 2:2 | ratio |
| 8 | Ratio_1_3 | Every 3 | Ev3 | 3x |
| 9 | Ratio_2_3 | 2nd of 3 | 2:3 | ratio |
| 10 | Ratio_3_3 | 3rd of 3 | 3:3 | ratio |
| 11 | Ratio_1_4 | Every 4 | Ev4 | 4x |
| 12 | Ratio_2_4 | 2nd of 4 | 2:4 | ratio |
| 13 | Ratio_3_4 | 3rd of 4 | 3:4 | ratio |
| 14 | Ratio_4_4 | 4th of 4 | 4:4 | ratio |
| 15 | First | First | 1st | play arrow |
| 16 | Fill | Fill | Fill | F |
| 17 | NotFill | Not Fill | !F | F strikethrough |

### Acceptance Criteria

- [ ] All 6 lanes visible and editable in the stacked lane editor
- [ ] Pitch lane: center line visible, bars go up/down, snap to semitone
- [ ] Ratchet lane: click cycles 1→2→3→4→1, blocks visualization correct
- [ ] Modifier lane: 4-row dot grid, each flag toggleable independently per step
- [ ] Condition lane: icon display, click opens popup with all 18 options
- [ ] All lanes collapse/expand with miniature previews
- [ ] Left-alignment correct: step 1 lines up across lanes with different lengths
- [ ] All parameter wiring functional (automation, state save/load)
- [ ] No allocations in draw/mouse paths
- [ ] Pluginval level 5 passes

---

## Phase 11c: Interaction Polish

**Plugin Layer**: `plugins/shared/` and `plugins/ruinae/`
**Files**:
- All arp UI components from 11a/11b
- `plugins/ruinae/resources/editor.uidesc` — bottom bar, Euclidean section
**Depends on**: Phase 11b

### Purpose

Add playback feedback, pattern manipulation tools, and generative controls that make the arpeggiator feel alive and performance-ready.

### Scope

1. **Playhead trail**: Current step = bright highlight, previous 2-3 steps = fading highlight. Per-lane (each lane's playhead position is independent). ~30fps refresh via timer.

2. **Step activity indicators**: When a step is evaluated but skipped (by condition, probability, or rest flag), show a small X overlay on that step. Requires audio thread → controller communication of "step N was skipped" events via IMessage.

3. **Per-lane transform buttons** (on lane header right side):
   - Invert (↕): Mirror values around center (0.5 for bar lanes, 0 for pitch)
   - Shift Left (←): Rotate pattern one step left
   - Shift Right (→): Rotate pattern one step right
   - Randomize (?): Fill lane with random values appropriate to the lane type
   - Reuse existing ActionButton component and transform logic from StepPatternEditor

4. **Copy/paste** (right-click context menu on lane header):
   - Copy lane pattern (values + length)
   - Paste to same lane type (overwrite)
   - Paste shape to different lane type (normalize source range → target range)
   - Clipboard is in-memory, plugin-scoped (not system clipboard)

5. **Euclidean dual visualization**:
   - **Circular dot display**: Reuse TranceGate-style dots in the bottom bar, showing the E(k,n) pattern as a ring
   - **Linear overlay**: In the lane editor, overlay Euclidean active/inactive indicators above the step bars (dots like StepPatternEditor already supports)
   - Both update live when Hits/Steps/Rotation knobs change

6. **Bottom bar — Generative controls**:
   - Euclidean section: Enable toggle, Hits knob, Steps knob, Rotation knob, circular dot display
   - Humanize knob (0-100%)
   - Spice knob (0-100%)
   - Ratchet Swing knob (50-75%)
   - Dice button (ActionButton, momentary — triggers pattern variation)
   - Fill toggle (ToggleButton — fills rests on alternate cycles)
   - All wired to existing parameter IDs from engine phases

7. **Color scheme finalization**: Register all 6 lane colors + dim/fill variants in uidesc. Ensure collapsed preview, playhead, and trail all use the correct lane color.

### Acceptance Criteria

- [ ] Playhead trail visible: current step bright, 2-3 previous steps fading
- [ ] Skipped steps show X overlay within ~1 frame of the skip event
- [ ] Per-lane transforms (invert, shift L/R, randomize) work for all 6 lane types
- [ ] Copy/paste: same-type paste preserves values exactly; cross-type paste maps range
- [ ] Euclidean circular dots display in bottom bar, update live with knob changes
- [ ] Euclidean linear overlay visible in lane editor when Euclidean enabled
- [ ] Dice button triggers audible pattern variation
- [ ] Fill toggle is responsive for live performance (no UI lag)
- [ ] All generative controls (Humanize, Spice, Ratchet Swing) wired and functional
- [ ] Full color scheme applied: each lane has distinct accent, collapsed previews colored
- [ ] UI is responsive and doesn't block audio thread
- [ ] No allocations in draw/mouse/timer paths
- [ ] Pluginval level 5 passes

---

## Phase 12: Presets & Polish

**Plugin Layer**: `plugins/ruinae/`
**Depends on**: Phase 11c

### Purpose

Factory arp pattern presets, final polish, performance testing, documentation.

### Factory Arp Presets

Curate a library of arp patterns that showcase the engine's capabilities:

| Category | Examples |
|---|---|
| **Classic** | Basic Up 1/16, Down 1/8, UpDown 1/8T |
| **Acid** | TB-303 patterns with slide + accent |
| **Euclidean World** | Tresillo E(3,8), Bossa E(5,16), Samba E(7,16) |
| **Polymetric** | 3-vel × 5-gate × 7-pitch, 4-ratchet × 5-timing |
| **Generative** | High-spice patterns, conditional trig evolvers |
| **Performance** | Fill-aware patterns, probability cascades |

### Preset Storage

Arp patterns are part of the synth preset (saved in plugin state). Additionally, consider:
- Arp-only preset save/load (just the arp section, not the whole synth)
- This could be a future enhancement — not blocking for initial release

### Performance Testing

- CPU benchmark: arp enabled vs disabled overhead
- Target: < 0.1% additional CPU at 44.1kHz stereo (arp is logic, not DSP)
- Verify no allocation in the audio path (valgrind/ASan)
- Stress test: 10+ notes held, ratchet 4 on every step, all lanes active

### Final Polish

- [ ] All parameter names display correctly in host (automation lanes)
- [ ] Parameter value formatting is readable (e.g., "1/16 Note" not "10")
- [ ] Arp responds correctly to transport start/stop
- [ ] Arp resets cleanly on transport stop
- [ ] State save/load round-trips all lanes, all modifiers, all conditions
- [ ] No audio artifacts on preset change with arp active
- [ ] Pluginval level 5 passes

### Acceptance Criteria

- [ ] Minimum 12 factory arp presets across all categories
- [ ] CPU overhead < 0.1% at 44.1kHz
- [ ] Zero heap allocation in audio path verified with ASan
- [ ] All previously-passing tests still pass (regression)
- [ ] Full end-to-end: load preset → play chord → hear correctly arpeggiated output

---

## Risk Analysis

| Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|
| **Lane parameter explosion** | High — 6 lanes × 32 steps = 192+ parameters | High | Use bulk parameter encoding (pack lane steps into fewer parameter IDs) or use a custom binary blob for lane data |
| **Sample-accurate timing complexity** | Medium — events spanning block boundaries | Medium | Careful bookkeeping of pending noteOff deadlines across blocks |
| **Gate overlap (>100%) voice stealing** | Medium — overlapping notes consume voices | Low | Document that legato arp in poly mode uses 2+ voices per step |
| **Euclidean + Conditional interaction** | Low — complex evaluation order | Medium | Clear precedence: Euclidean determines rhythm, conditions filter on top |
| **UI complexity** | Medium — 6 editable lanes is a lot of UI | Medium | Stacked scrollable lanes with collapse/expand; content-sized heights; 3-phase incremental build |
| **Slide in poly mode** | Medium — portamento is typically mono | Medium | Implement as legato noteOn flag; voice allocator routes to same voice |
| **Preset compatibility** | Medium — adding arp state to existing presets | Low | Default to arp disabled — old presets load fine, arp off |
| **Spice/Dice state** | Low — random overlay is ephemeral | Low | Don't serialize the random overlay, only the original pattern + spice amount |
