# Data Model: Ratcheting (074)

**Date**: 2026-02-22

## Entity Overview

### 1. Ratchet Lane (ArpLane<uint8_t>)

**Location**: Member of `ArpeggiatorCore` at `dsp/include/krate/dsp/processors/arpeggiator_core.h`

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| steps_[0..31] | uint8_t | 1-4 | 0 (overridden to 1 for step 0) | Per-step ratchet count |
| length_ | size_t | 1-32 | 1 | Active step count |
| position_ | size_t | 0 to length-1 | 0 | Current step position |

**Relationships**: Advances once per arp step tick (in `fireStep()`), independently of velocity, gate, pitch, and modifier lanes. Reset by `resetLanes()`.

**Validation**:
- Constructor must set step 0 to 1 explicitly (ArpLane zero-initializes to 0)
- Values clamped to [1, 4] at parameter boundary and at DSP read site
- Length clamped to [1, 32] by ArpLane::setLength()

### 2. Sub-Step State (ArpeggiatorCore members)

**Location**: Member variables in `ArpeggiatorCore`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| ratchetSubStepsRemaining_ | uint8_t | 0 | Sub-steps left to fire (0 = inactive) |
| ratchetSubStepDuration_ | size_t | 0 | Duration per sub-step in samples |
| ratchetSubStepCounter_ | size_t | 0 | Sample counter within current sub-step |
| ratchetNote_ | uint8_t | 0 | MIDI note for single-note retriggers |
| ratchetVelocity_ | uint8_t | 0 | Non-accented velocity for retriggers |
| ratchetGateDuration_ | size_t | 0 | Gate duration per sub-step in samples |
| ratchetIsLastSubStep_ | bool | false | True when next sub-step is the last one |

**Chord mode extensions**:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| ratchetNotes_ | std::array<uint8_t, 32> | {} | Chord note numbers |
| ratchetVelocities_ | std::array<uint8_t, 32> | {} | Chord per-note velocities |
| ratchetNoteCount_ | size_t | 0 | Number of chord notes |

**State transitions**:

```
INACTIVE (ratchetSubStepsRemaining_ == 0)
    |
    | fireStep() with ratchetCount > 1
    v
ACTIVE (ratchetSubStepsRemaining_ = N-1, counter = 0)
    |
    | processBlock() loop: SubStep event fires
    | -> emit noteOn, schedule noteOff, decrement remaining, reset counter
    v
ACTIVE (ratchetSubStepsRemaining_ = N-2, counter = 0)
    |
    | ... repeat until remaining == 0
    v
INACTIVE (ratchetSubStepsRemaining_ == 0)

CLEAR (any state -> INACTIVE):
    - resetLanes() (retrigger, bar boundary)
    - setEnabled(false)
    - transport stop
    - bar boundary coinciding with sub-step
```

### 3. Plugin Parameter Storage (ArpeggiatorParams extension)

**Location**: `plugins/ruinae/src/parameters/arpeggiator_params.h`

| Field | Type | Default | VST3 ID |
|-------|------|---------|---------|
| ratchetLaneLength | std::atomic<int> | 1 | kArpRatchetLaneLengthId (3190) |
| ratchetLaneSteps[32] | std::atomic<int>[32] | 1 each | kArpRatchetLaneStep0Id..31Id (3191-3222) |

**Note**: `std::atomic<int>` (not `std::atomic<uint8_t>`) to guarantee lock-free atomics. Cast to `uint8_t` at DSP boundary with clamping to [1, 4].

### 4. NextEvent Enum Extension

**Location**: Local enum inside `processBlock()` at `arpeggiator_core.h`

```
enum class NextEvent { BlockEnd, NoteOff, Step, SubStep, BarBoundary };
```

**Priority** (when multiple events coincide at same sample offset):
```
BarBoundary > NoteOff > Step > SubStep
```

### 5. Serialization Format Extension

**Location**: `saveArpParams()` / `loadArpParams()` in `arpeggiator_params.h`

After existing Phase 5 data (modifier lane steps, accent velocity, slide time):

| Order | Field | Type | Size |
|-------|-------|------|------|
| 1 | ratchetLaneLength | int32 | 4 bytes |
| 2-33 | ratchetLaneStep[0..31] | int32 x 32 | 128 bytes |

**Total ratchet serialization overhead**: 132 bytes

**Backward compatibility**: EOF at field 1 (ratchetLaneLength) = Phase 5 preset, return true with defaults. EOF mid-steps = corrupt, return false.

## Timing Calculations

### Sub-step duration

```
subStepDuration = stepDuration / ratchetCount  (integer division)
lastSubStepDuration = subStepDuration + (stepDuration % ratchetCount)
```

### Sub-step onset offsets (relative to step start)

```
onset[k] = k * subStepDuration   for k = 0, 1, ..., N-1
```

### Sub-step gate duration

```
subGateDuration = max(1, subStepDuration * gateLengthPercent / 100 * gateLaneValue)
```

### Example: 120 BPM, 1/8 note, 44.1kHz

| Ratchet Count | Step Duration | Sub-step Duration | Remainder | Onsets |
|---------------|---------------|-------------------|-----------|--------|
| 1 | 11025 | 11025 | 0 | 0 |
| 2 | 11025 | 5512 | 1 | 0, 5512 |
| 3 | 11025 | 3675 | 0 | 0, 3675, 7350 |
| 4 | 11025 | 2756 | 1 | 0, 2756, 5512, 8268 |
