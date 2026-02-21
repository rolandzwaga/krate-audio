# Data Model: Per-Step Modifiers

**Date**: 2026-02-21
**Phase**: 1 (Design & Contracts)

## Entities

### ArpStepFlags (New Enum)

**Location**: `dsp/include/krate/dsp/processors/arpeggiator_core.h` (before ArpEvent)
**Layer**: 2 (processors)

```cpp
enum ArpStepFlags : uint8_t {
    kStepActive = 0x01,   // Note fires (default on). Off = Rest.
    kStepTie    = 0x02,   // Extend previous note, no retrigger
    kStepSlide  = 0x04,   // Portamento to next note, legato noteOn
    kStepAccent = 0x08,   // Velocity boost
};
```

**Validation rules**:
- Default value: `kStepActive` (0x01)
- Any uint8_t value is valid (0x00 = rest, 0x0F = active+tie+slide+accent)
- Priority evaluation: Active check first, then Tie, then Slide, then Accent

### ArpEvent (Extended)

**Location**: `dsp/include/krate/dsp/processors/arpeggiator_core.h:56-63`
**Change**: Add `bool legato{false}` field

```cpp
struct ArpEvent {
    enum class Type : uint8_t { NoteOn, NoteOff };

    Type type{Type::NoteOn};
    uint8_t note{0};
    uint8_t velocity{0};
    int32_t sampleOffset{0};
    bool legato{false};       // NEW: true = suppress envelope retrigger, apply portamento
};
```

**Validation rules**:
- legato defaults to false (backward compatible)
- legato is only meaningful for NoteOn events
- legato=true signals: (1) don't retrigger envelope, (2) apply portamento to target pitch

### ArpeggiatorCore (Extended Members)

**Location**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`
**Change**: Add modifier lane + configuration members + accessors

**New members**:
```cpp
// After pitchLane_ declaration (~line 893)
ArpLane<uint8_t> modifierLane_;  // Bitmask per step (default: length=1, step[0]=kStepActive)

// Configuration
int accentVelocity_{30};    // 0-127, additive boost. Read by fireStep() to boost accented steps.
float slideTimeMs_{60.0f};  // 0-500ms portamento duration. Stored for API symmetry with
                             // setAccentVelocity(); NOT read by fireStep(). Effective routing:
                             // applyParamsToArp() -> engine_.setPortamentoTime(slideTime).

// State tracking (for tie chain)
bool tieActive_{false};     // True when currently in a tie chain.
                             // Private -- no public accessor. Tests verify reset via behavior:
                             // after resetLanes(), a Tie step with no preceding Active step
                             // must produce silence (proving tieActive_ was cleared).
```

**New accessors**:
```cpp
ArpLane<uint8_t>& modifierLane() noexcept;
const ArpLane<uint8_t>& modifierLane() const noexcept;

void setAccentVelocity(int amount) noexcept;
void setSlideTime(float ms) noexcept;
```

**Initialization** (in constructor, after pitchLane_ init):
```cpp
// MANDATORY: ArpLane<uint8_t> default-constructs all steps to 0 (std::array
// zero-initialization). 0x00 = Rest (kStepActive bit not set). Without this
// call, the default modifier lane would silence every arp step.
// The lane's length defaults to 1 in ArpLane's own constructor.
modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive));  // Default: active, no modifiers
```

### ArpeggiatorParams (Extended)

**Location**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**Change**: Add modifier lane atomic storage

**New fields** (after pitchLaneSteps):
```cpp
// --- Modifier Lane (073-per-step-mods) ---
std::atomic<int> modifierLaneLength{1};
std::atomic<int> modifierLaneSteps[32];  // uint8_t bitmask stored as int (lock-free guarantee)

// Modifier configuration
std::atomic<int> accentVelocity{30};     // 0-127
std::atomic<float> slideTime{60.0f};     // 0-500 ms
```

**Constructor initialization** (in initializer list):
```cpp
// All modifierLaneSteps default to kStepActive (1)
for (auto& step : modifierLaneSteps) {
    step.store(1, std::memory_order_relaxed);  // kStepActive = 0x01
}
```

### Parameter IDs (New)

**Location**: `plugins/ruinae/src/plugin_ids.h`
**Range**: 3140-3181 (within reserved 3133-3199)

```cpp
// --- Modifier Lane (3140-3172) ---
kArpModifierLaneLengthId = 3140,    // discrete: 1-32
kArpModifierLaneStep0Id  = 3141,    // discrete: 0-255 (uint8_t bitmask)
// ... through ...
kArpModifierLaneStep31Id = 3172,

// --- Modifier Configuration (3180-3181) ---
kArpAccentVelocityId     = 3180,    // discrete: 0-127, default 30
kArpSlideTimeId          = 3181,    // continuous: 0-500ms, default 60ms
```

## Relationships

```
ArpStepFlags (enum)
    |
    v (stored in)
ArpLane<uint8_t> modifierLane_  --->  ArpeggiatorCore
    |                                      |
    v (read per step in)                   v (emits)
fireStep() modifier evaluation       ArpEvent (with legato flag)
    |                                      |
    v (parameters from)                    v (consumed by)
ArpeggiatorParams                    RuinaeEngine.noteOn(note, vel, legato)
    |                                      |
    v (persisted in)                       v (routes to)
IBStream save/load                   MonoHandler (mono) / Voice (poly)
```

## State Transitions

### Tie Chain State Machine

```
                    +----------------+
                    |   No Tie Chain |  <-- initial state / after reset
                    |  tieActive_=F  |
                    +-------+--------+
                            |
                    step is Active (kStepActive set, no kStepTie)
                    emit NoteOn, set tieActive_=false
                            |
                            v
               +-------------------------+
               | Note Sounding (Active)  |
               | tieActive_=F, has note  |
               +----+--------+----------+
                    |        |          |
        Tie step   |  Rest  |  Slide   | Active step
        kStepTie   |  !kA   |  kSlide  | kStepActive
                    |        |          |
                    v        v          v
            +-----------+ +---------+ +----------+
            | Tie Chain | | Silence | | New Note |
            | tieActive | | noteOff | | noteOff  |
            | =true,    | | emit,   | | then     |
            | suppress  | | no note | | noteOn   |
            | noteOff & | |         | |          |
            | noteOn    | |         | |          |
            +-+----+----+ +---------+ +----------+
              |    |
    Tie step  | Rest/Active/Slide
    (chain    | (chain ends)
    continues)|
              v
        [loop back to Tie Chain]
```

### Modifier Evaluation Flowchart (in fireStep)

```
Read modifier = modifierLane_.advance()
    |
    +-- Is kStepActive set?
    |       |
    |     NO --> REST: emit noteOff for previous note(s), no noteOn
    |              advance all other lanes (consume values)
    |              tieActive_ = false
    |
    YES
    |
    +-- Is kStepTie set?
    |       |
    |     YES --> Is there a currently sounding note?
    |       |           |
    |       |         YES --> TIE: suppress noteOff AND noteOn
    |       |                   tieActive_ = true
    |       |                   advance all other lanes (consume values)
    |       |
    |       |         NO --> REST (tie with no predecessor)
    |       |                   tieActive_ = false
    |       |                   advance all other lanes
    |
    NO (active, not tied)
    |
    +-- tieActive_ = false (chain ends if it was active)
    |
    +-- Is kStepSlide set?
    |       |
    |     YES --> Is there a currently sounding note?
    |       |           |
    |       |         YES --> SLIDE: emit legato noteOn (legato=true)
    |       |                   suppress previous noteOff
    |       |
    |       |         NO --> NORMAL: emit noteOn (legato=false)
    |       |                   (no source pitch to slide from)
    |
    NO (active, not tied, not slide)
    |
    +-- NORMAL: emit noteOff for previous, then noteOn
    |
    +-- Is kStepAccent set? (applies to any step with a noteOn)
            |
          YES --> boost velocity by accentVelocity_
          NO  --> normal velocity
```
