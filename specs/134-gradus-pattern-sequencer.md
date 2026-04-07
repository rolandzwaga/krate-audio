# 134 — Gradus Pattern Sequencer

Save, recall, and sequence multiple arpeggiator patterns within a single Gradus instance.

## Overview

Gradus currently has a single pattern — one set of lane data, lengths, speeds, swing, jitter, and related settings. This feature adds a **pattern bank** (multiple pattern slots) and a **pattern chain** (an ordered sequence of patterns with repeat counts), turning Gradus from a single-pattern arpeggiator into a pattern-based sequencer.

### Goals

- Store multiple complete pattern snapshots in a bank
- Sequence patterns in a user-defined order with per-slot repeat counts
- Clean transitions at pattern boundaries (finish current cycle before switching)
- Copy/paste/initialize individual patterns
- Minimal parameter overhead — pattern data lives in state (binary blob), not in the parameter tree

### Non-Goals

- Song position / DAW timeline integration (future consideration)
- Per-pattern tempo or time signature changes
- Live recording of pattern sequences
- Pattern morphing / crossfading between patterns (different feature)

---

## Concepts

### Pattern

A **pattern** is a complete snapshot of **all** arpeggiator settings — lane data, global controls, scale, arp mode, everything. Switching patterns can change the key, the arp direction, the octave range, and the step content all at once.

| Data | Per Pattern |
|------|-------------|
| 8 lane step arrays (32 steps each) | Velocity, Gate, Pitch, Modifier, Ratchet, Condition, Chord, Inversion |
| 8 lane lengths | 1–32 per lane |
| 8 lane speed multipliers | 0.25x–4x |
| 8 lane swing values | 0–75% |
| 8 lane jitter values | 0–4 steps |
| 8 speed curve tables | 256 floats each, baked lookup tables |
| 8 speed curve depths | per-lane depth values |
| Euclidean settings | enabled, hits, steps, rotation |
| Modifier extras | accent velocity, slide time |
| Ratchet extras | decay, ratchet swing |
| Strum | time, direction |
| Velocity curve | type, amount |
| Transpose | value |
| Range mapping | low, high, mode |
| Pin settings | pin note, 32 pin flags |
| Markov | preset, 49 matrix cells |
| Spice / Dice / Humanize | values |
| Condition fill toggle | value |
| Operating mode | Arp / Order |
| Arp mode | Up, Down, UpDown, Random, Walk, Gravity, Markov, etc. |
| Octave range & mode | range + direction |
| Tempo sync, note value, free rate | timing settings |
| Gate length, global swing | groove settings |
| Latch mode, retrigger | performance settings |
| Scale type, root note | key/scale |
| Scale quantize input, MIDI output | routing |
| Voicing mode | chord voicing |

**Not included in pattern snapshot** (truly global, shared across all patterns):
- Audition synth settings (enabled, volume, waveform, decay)

### Pattern Bank

A fixed-size bank of **16 pattern slots** (A–P). Slot A is the default/active pattern on init. All slots start as copies of the default pattern (16-step velocity ramp, all other lanes at defaults).

### Pattern Chain

An ordered list of up to **32 chain entries**, each specifying:

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| pattern | uint8 | 0–15 (A–P) | Which pattern slot to play |
| repeats | uint8 | 1–64 | How many full cycles before advancing to next entry |

A **cycle** = the longest active lane completing one full pass through its steps. With polymetric lanes of different lengths, one cycle ends when the lane with the most steps wraps back to step 0.

### Chain Playback Modes

| Mode | Behavior |
|------|----------|
| **Off** | No chain — play the currently selected pattern indefinitely |
| **Chain** | Play through chain entries sequentially, loop back to start |
| **One-Shot** | Play through chain once, hold last pattern |

---

## Parameters

Pattern selection and chain mode are exposed as VST3 parameters for host automation. Pattern/chain data is stored in the processor state blob (like lane step data today).

| Parameter | ID | Range | Default | Description |
|-----------|----|-------|---------|-------------|
| `kArpActivePatternId` | 3510 | 0–15 | 0 (A) | Currently active/editing pattern |
| `kArpChainModeId` | 3511 | 0–2 | 0 (Off) | Off / Chain / One-Shot |
| `kArpChainLengthId` | 3512 | 1–32 | 1 | Number of active chain entries |
| `kArpChainPlayheadId` | 3513 | 0–31 | 0 | Current chain position (read-only, for UI) |

Chain entry data (pattern index + repeat count per entry) is stored in state, not as individual parameters — 32 entries x 2 fields = 64 values would waste parameter slots and aren't meaningful to automate individually.

**Total new parameters: 4**

---

## State Serialization

### State Version

Bump state version to **v3**. v2 states load into pattern A with chain disabled (backward compatible).

### New State Data (appended after v2 data)

```
// Pattern bank
for each pattern slot (0..15):
    // Same serialization as current v2 lane data block
    velocity lane: length + 32 steps
    gate lane: length + 32 steps
    pitch lane: length + 32 steps
    modifier lane: length + 32 steps + accentVelocity + slideTime
    ratchet lane: length + 32 steps + ratchetDecay + ratchetSwing
    euclidean: enabled, hits, steps, rotation
    condition lane: length + 32 steps + fillToggle
    spice, dice, humanize
    strum: time, direction
    velocity curve: type, amount
    transpose
    per-lane speeds (8)
    per-lane swings (8)
    per-lane jitters (8)
    range: low, high, mode
    pin: note, 32 flags
    markov: preset, 49 cells
    speed curves: 8 tables (256 floats each)
    // Global settings per pattern:
    operatingMode, mode, octaveRange, octaveMode
    tempoSync, noteValue, freeRate, gateLength, swing
    latchMode, retrigger
    scaleType, rootNote, scaleQuantizeInput, midiOut
    voicingMode

// Chain data
chainMode (int32)
chainLength (int32)
for each chain entry (0..31):
    patternIndex (int32)
    repeatCount (int32)
```

### Backward Compatibility

- **Loading v2 state**: All lane data loads into pattern slot A. Slots B–P get defaults. Chain mode = Off, chain length = 1.
- **Loading v3 state in old host**: Old processor ignores trailing bytes (existing EOF-safe pattern).

---

## Audio Thread Behavior

### Pattern Switching

When the active pattern changes (via parameter automation or chain advance):

1. **Wait for cycle boundary** — the current pattern plays to completion (longest lane wraps to step 0)
2. **Snapshot the new pattern's data** into the arp engine's working lanes and global settings
3. **Reset lane positions** to step 0 (all lanes restart together)
4. Held notes are preserved — only the pattern data changes

For **immediate switching** (user clicks a different pattern while stopped or while editing), the switch happens instantly without waiting for a cycle boundary.

### Chain Advance Logic

On each longest-lane wrap (the lane with the most steps returns to 0):

```
if chainMode == Off:
    continue with current pattern

repeatCounter++
if repeatCounter >= chain[chainPosition].repeats:
    repeatCounter = 0
    if chainMode == Chain:
        chainPosition = (chainPosition + 1) % chainLength
    else if chainMode == OneShot:
        if chainPosition < chainLength - 1:
            chainPosition++
        else:
            stay on last pattern

    load pattern bank[chain[chainPosition].pattern] into engine
    reset all lane positions to 0
```

### Thread Safety

- Pattern bank data lives in the processor (audio thread owns it)
- UI edits to lane data write to `ArpeggiatorParams` atomics as today — the processor applies them to the **currently active pattern slot**
- Pattern switching copies data between the bank and the engine's working lanes
- Chain state (position, repeat counter) is audio-thread-only; the chain playhead parameter is written by the processor for UI display

---

## UI Design

### Pattern Selector Strip

A row of 16 labeled buttons (A–P) placed above or below the ring display. The active pattern is highlighted. Clicking a button switches the editing context (and, if chain mode is off, the playback pattern). Two rows of 8 if horizontal space is tight.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  [ A ][ B ][ C ][ D ][ E ][ F ][ G ][ H ][ I ][ J ][ K ][ L ][ M ][ N ][ O ][ P ]  │
│   ^^^                                                                       │
│   active                                                                    │
└─────────────────────────────────────────────────────────────────────────────┘
```

- **Click**: Select pattern for editing (+ playback if chain is off)
- **Drag**: Drag a pattern button into the chain strip to add/assign it to a chain slot
- **Right-click / long-press**: Context menu — Copy, Paste, Initialize, Duplicate To...
- **Visual indicator**: Playing pattern gets a pulsing outline; editing pattern gets a solid highlight. When chain is active, the playing pattern may differ from the editing pattern.

### Chain Editor

A compact strip below the pattern selector, visible only when chain mode is not Off.

```
┌──────────────────────────────────────────────────────────┐
│ Chain: [On v]  Length: [4]                                │
│ ┌────┬────┬────┬────┬────────────────────────────────┐   │
│ │ A  │ A  │ B  │ C  │  ...                           │   │
│ │x4  │x4  │x2  │x8  │                               │   │
│ └────┴────┴────┴────┴────────────────────────────────┘   │
│  ^^^                                                     │
│  playing                                                 │
└──────────────────────────────────────────────────────────┘
```

- Each cell shows the pattern letter and repeat count
- **Drag a pattern button from the bank strip** into a chain cell to assign it
- Drag between chain cells to reorder
- Click repeat count to edit (scroll wheel or click-drag to adjust)
- Playhead indicator on the currently playing cell
- Chain length knob or +/- buttons to add/remove entries
- Drop a pattern on an empty cell at the end to append and auto-increment chain length

### Integration with Ring Display

The ring display always shows the **editing pattern's** lane data. When the playing pattern differs (chain mode), a small indicator (e.g., "Playing: C" badge) appears near the rings so the user knows they're editing a different pattern than what's sounding.

---

## Implementation Phases

### Phase 1: Pattern Bank (Core)
- Pattern data structure (full snapshot: all lane data, per-lane settings, AND global settings like arp mode, scale, octave, etc.)
- Pattern bank (16 slots) in processor
- Active pattern parameter
- Save/load pattern data to/from arp engine (including global setting swap on pattern switch)
- State serialization v3 with backward compat
- Copy/paste/initialize operations via IMessage

### Phase 2: Chain Sequencer (Core)
- Chain data structure (32 entries)
- Chain mode + length parameters
- Chain advance logic in audio thread (cycle boundary detection)
- Chain playhead parameter for UI feedback

### Phase 3: UI — Pattern Selector
- 16-button pattern selector strip (custom CView, draggable buttons)
- Active/playing indicators
- Right-click context menu (copy/paste/init)
- Wire to active pattern parameter

### Phase 4: UI — Chain Editor
- Chain strip with drop targets (custom CView)
- Drag-and-drop from pattern bank strip to assign patterns to chain cells
- Drag-to-reorder within chain
- Per-cell repeat count editing (scroll wheel / click-drag)
- Playhead visualization
- Chain mode dropdown + length control
- Wire to chain parameters and IMessage data

---

## Design Decisions (Resolved)

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| 1 | Pattern count | **16 slots (A–P)** | More headroom than 8; state size (~128KB for speed curves) is negligible on modern systems |
| 2 | What defines a "cycle"? | **Longest lane wrap** | The lane with the most steps dictates cycle length; no lane gets cut short on pattern switch |
| 3 | Global settings per-pattern? | **Yes — everything is per-pattern** | Empowers users to change key, arp mode, octave, etc. between patterns. Only audition synth settings are truly global |
| 4 | Chain entry editing UX | **Drag-and-drop from pattern bank** | Drag pattern buttons from the 16-slot bank strip into chain cells. More tactile and intuitive than dropdowns |
| 5 | Speed curves in snapshots | **Store full baked tables** | 8KB per pattern × 16 = 128KB total. Not worth the complexity of regeneration logic to save trivial disk space |
