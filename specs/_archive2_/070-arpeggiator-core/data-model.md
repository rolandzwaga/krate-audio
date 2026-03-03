# Data Model: Arpeggiator Core -- Timing & Event Generation

**Branch**: `070-arpeggiator-core` | **Date**: 2026-02-20

---

## Entity Relationship Overview

```
BlockContext (Layer 0, input)
    |
    v
ArpeggiatorCore (Layer 2, main processor)
    |--- owns ---> HeldNoteBuffer (Layer 1)
    |--- owns ---> NoteSelector (Layer 1)
    |--- owns ---> PendingNoteOff[32] (internal)
    |--- uses ---> NoteValue/NoteModifier (Layer 0)
    |--- uses ---> getBeatsForNote() (Layer 0)
    |
    v
ArpEvent[64] (output, caller-owned span)
```

---

## Enumerations

### LatchMode (new, defined in arpeggiator_core.h)

```cpp
enum class LatchMode : uint8_t {
    Off = 0,   // Stop arp when all keys released
    Hold,      // Continue playing latched pattern; new keys replace
    Add        // Accumulate notes into pattern
};
```

**State transitions:**
- Off: `noteOff() -> removes from buffer -> empty? stop`
- Hold: `all keys released -> latchActive_ = true -> arp continues`
- Hold: `new noteOn while latchActive_ -> clear buffer, add note, latchActive_ = false`
- Add: `noteOn -> always adds to buffer, never clears`
- Add: `noteOff -> does NOT remove from buffer`

### ArpRetriggerMode (new, defined in arpeggiator_core.h)

```cpp
enum class ArpRetriggerMode : uint8_t {
    Off = 0,   // Pattern continues its current position
    Note,      // Reset to pattern start on each noteOn
    Beat       // Reset at bar boundaries (from transport)
};
```

**ODR Safety**: Named `ArpRetriggerMode` to avoid conflict with `Krate::DSP::RetriggerMode` in `envelope_utils.h` (which has values Hard, Legato for envelope retriggering -- semantically different).

---

## Data Structures

### ArpEvent (new, FR-001)

```cpp
struct ArpEvent {
    enum class Type : uint8_t { NoteOn, NoteOff };

    Type type{Type::NoteOn};
    uint8_t note{0};          // MIDI note number (0-127)
    uint8_t velocity{0};      // MIDI velocity (0-127)
    int32_t sampleOffset{0};  // Sample position within block [0, blockSize-1]
};
```

**Size**: 8 bytes (1 + 1 + 1 + 1 padding + 4)
**Lifetime**: Caller-owned array/span, filled by processBlock()
**Validation**: sampleOffset in [0, blockSize-1] enforced by processBlock()

### PendingNoteOff (internal to ArpeggiatorCore)

```cpp
struct PendingNoteOff {
    uint8_t note{0};
    size_t samplesRemaining{0};
};
```

**Capacity**: Fixed array of 32 (matching HeldNoteBuffer::kMaxNotes)
**Lifecycle**: Created when NoteOn fires, consumed when NoteOff deadline reached
**Cross-block**: `samplesRemaining` is decremented by `blockSize` each block until `samplesRemaining < blockSize`, at which point the NoteOff fires at `sampleOffset = samplesRemaining` within that block. The condition is strictly `< blockSize` (NOT `<= blockSize`): when `samplesRemaining == blockSize` the event falls at the start of the *next* block and must be deferred by one more blockSize subtraction. Using `<=` would produce `sampleOffset = blockSize`, violating FR-002's [0, blockSize-1] constraint. (Authoritative definition — see also research.md Q2.)

---

## ArpeggiatorCore Class (Layer 2 Processor)

### Public Interface

```cpp
class ArpeggiatorCore {
public:
    // Constants
    static constexpr size_t kMaxEvents = 64;
    static constexpr size_t kMaxPendingNoteOffs = 32;
    static constexpr double kMinSampleRate = 1000.0;
    static constexpr float kMinFreeRate = 0.5f;
    static constexpr float kMaxFreeRate = 50.0f;
    static constexpr float kMinGateLength = 1.0f;    // percent
    static constexpr float kMaxGateLength = 200.0f;   // percent
    static constexpr float kMinSwing = 0.0f;           // percent
    static constexpr float kMaxSwing = 75.0f;          // percent

    // Lifecycle (FR-003, FR-004)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // MIDI input (FR-005, FR-006, FR-007)
    void noteOn(uint8_t note, uint8_t velocity) noexcept;
    void noteOff(uint8_t note) noexcept;

    // Configuration (FR-008 through FR-018)
    void setEnabled(bool enabled) noexcept;
    void setMode(ArpMode mode) noexcept;
    void setOctaveRange(int octaves) noexcept;
    void setOctaveMode(OctaveMode mode) noexcept;
    void setTempoSync(bool sync) noexcept;
    void setNoteValue(NoteValue val, NoteModifier mod) noexcept;
    void setFreeRate(float hz) noexcept;
    void setGateLength(float percent) noexcept;
    void setSwing(float percent) noexcept;
    void setLatchMode(LatchMode mode) noexcept;
    void setRetrigger(ArpRetriggerMode mode) noexcept;

    // Processing (FR-019 through FR-024)
    size_t processBlock(const BlockContext& ctx,
                        std::span<ArpEvent> outputEvents) noexcept;
};
```

### Private State

```
// Composed components (Layer 1)
HeldNoteBuffer heldNotes_;
NoteSelector selector_;

// Configuration state
bool enabled_ = false;
LatchMode latchMode_ = LatchMode::Off;
ArpRetriggerMode retriggerMode_ = ArpRetriggerMode::Off;
bool tempoSync_ = true;
NoteValue noteValue_ = NoteValue::Eighth;
NoteModifier noteModifier_ = NoteModifier::None;
float freeRateHz_ = 4.0f;
float gateLengthPercent_ = 80.0f;  // user-facing percent (1.0–200.0); stored as-is
float swing_ = 0.0f;  // stored as 0.0–0.75 (user-facing 0–75% divided by 100 at setSwing() call)

// Timing state
double sampleRate_ = 44100.0;
size_t sampleCounter_ = 0;          // Integer accumulator (FR-019c)
size_t currentStepDuration_ = 0;    // Samples for current step (with swing)
size_t swingStepCounter_ = 0;       // Even/odd counter for swing (FR-020)
bool wasPlaying_ = false;           // Previous transport state (FR-031)
bool firstStepPending_ = true;      // Need to fire first step on next boundary

// Latch state
size_t physicalKeysHeld_ = 0;       // Count of physically pressed keys
bool latchActive_ = false;          // True when latched pattern is active

// NoteOff tracking (FR-025, FR-026)
std::array<uint8_t, 32> currentArpNotes_{};  // Currently sounding notes
size_t currentArpNoteCount_ = 0;
std::array<PendingNoteOff, 32> pendingNoteOffs_{};
size_t pendingNoteOffCount_ = 0;

// Disable transition (FR-008)
bool needsDisableNoteOff_ = false;
```

### State Transition Diagram

```
                  setEnabled(true)
    DISABLED -----------------------> ENABLED_WAITING
       ^                                   |
       |                              noteOn() + isPlaying
       |                                   |
       |                                   v
       |  setEnabled(false)          ENABLED_RUNNING
       +------ (emit NoteOff) <----- (processBlock produces events)
                                           |
                                      isPlaying = false
                                           |
                                           v
                                    ENABLED_PAUSED
                                      (transport stopped)
                                           |
                                      isPlaying = true
                                           |
                                           v
                                    ENABLED_RUNNING (resume)
```

---

## Timing Model

### Step Duration Calculation (per step tick)

```
if tempoSync:
    beatsPerStep = getBeatsForNote(noteValue_, noteModifier_)
    secondsPerBeat = 60.0 / ctx.tempoBPM  (clamped to [20, 300])
    baseDuration = static_cast<size_t>(secondsPerBeat * beatsPerStep * ctx.sampleRate)
else:
    baseDuration = static_cast<size_t>(ctx.sampleRate / freeRateHz_)

// Apply swing
if swingStepCounter_ is even:
    currentStepDuration_ = static_cast<size_t>(baseDuration * (1.0 + swing_))
else:
    currentStepDuration_ = static_cast<size_t>(baseDuration * (1.0 - swing_))

// Minimum 1 sample
currentStepDuration_ = max(1, currentStepDuration_)
```

### Gate Duration Calculation (per NoteOn)

```
gateDuration = static_cast<size_t>(currentStepDuration_ * gateLengthPercent_ / 100.0f)
gateDuration = max(1, gateDuration)
```

### Swing Step Counter Reset Conditions

The `swingStepCounter_` resets to 0 on:
- `reset()` call
- `setMode()` call (FR-009, FR-020)
- Retrigger Note event (new noteOn with retrigger == Note)
- Retrigger Beat event (bar boundary crossed)

---

## Event Ordering Rules

Within a single sample offset, events are ordered:
1. Pending NoteOff events (from previous steps)
2. Gate-driven NoteOff for current step (if gate < 100% and deadline matches)
3. NoteOn for new step

This ensures:
- No overlapping notes when gate <= 100% (NoteOff before NoteOn at same offset)
- Correct legato when gate > 100% (NoteOn fires at step boundary, old NoteOff fires later)

---

## Dependency API Contracts (Verified from Source)

### BlockContext (block_context.h, verified)

| Member/Method | Signature |
|---|---|
| `sampleRate` | `double sampleRate = 44100.0` |
| `blockSize` | `size_t blockSize = 512` |
| `tempoBPM` | `double tempoBPM = 120.0` |
| `isPlaying` | `bool isPlaying = false` |
| `transportPositionSamples` | `int64_t transportPositionSamples = 0` |
| `timeSignatureNumerator` | `uint8_t timeSignatureNumerator = 4` |
| `timeSignatureDenominator` | `uint8_t timeSignatureDenominator = 4` |
| `tempoToSamples()` | `constexpr size_t tempoToSamples(NoteValue, NoteModifier) const noexcept` |
| `samplesPerBar()` | `constexpr size_t samplesPerBar() const noexcept` |

### HeldNoteBuffer (held_note_buffer.h, verified)

| Member/Method | Signature |
|---|---|
| `kMaxNotes` | `static constexpr size_t kMaxNotes = 32` |
| `noteOn()` | `void noteOn(uint8_t note, uint8_t velocity) noexcept` |
| `noteOff()` | `void noteOff(uint8_t note) noexcept` |
| `clear()` | `void clear() noexcept` |
| `size()` | `size_t size() const noexcept` |
| `empty()` | `bool empty() const noexcept` |
| `byPitch()` | `std::span<const HeldNote> byPitch() const noexcept` |
| `byInsertOrder()` | `std::span<const HeldNote> byInsertOrder() const noexcept` |

### NoteSelector (held_note_buffer.h, verified)

| Member/Method | Signature |
|---|---|
| Constructor | `explicit NoteSelector(uint32_t seed = 1) noexcept` |
| `setMode()` | `void setMode(ArpMode mode) noexcept` -- calls `reset()` internally |
| `setOctaveRange()` | `void setOctaveRange(int octaves) noexcept` -- clamped 1-4 |
| `setOctaveMode()` | `void setOctaveMode(OctaveMode mode) noexcept` |
| `advance()` | `ArpNoteResult advance(const HeldNoteBuffer& held) noexcept` |
| `reset()` | `void reset() noexcept` |

### ArpNoteResult (held_note_buffer.h, verified)

| Member | Type |
|---|---|
| `notes` | `std::array<uint8_t, 32>` |
| `velocities` | `std::array<uint8_t, 32>` |
| `count` | `size_t` (0 = empty, 1 = single, N = chord) |

### getBeatsForNote (note_value.h, verified)

```cpp
[[nodiscard]] inline constexpr float getBeatsForNote(
    NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept;
```

Returns beats per step: e.g., Quarter = 1.0, Eighth = 0.5, etc.
