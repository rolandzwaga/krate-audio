# Data Model: Mono/Legato Handler

**Feature Branch**: `035-mono-legato-handler`
**Date**: 2026-02-07
**Spec**: [spec.md](spec.md) | **Research**: [research.md](research.md)

---

## Entities

### MonoNoteEvent (FR-001)

Simple aggregate returned by noteOn/noteOff to instruct the caller.

```cpp
struct MonoNoteEvent {
    float frequency;    // Hz, computed via 12-TET
    uint8_t velocity;   // 0-127
    bool retrigger;     // true = caller should restart envelopes
    bool isNoteOn;      // true = note is active, false = all notes released
};
// Size: 8 bytes (4 float + 1 + 1 + 1 + 1 padding)
// No user-declared constructors (aggregate)
```

**Field semantics:**

| Field | noteOn (new phrase) | noteOn (legato tied) | noteOff (return to held) | noteOff (last released) |
|-------|--------------------|--------------------|------------------------|----------------------|
| frequency | target note Hz | target note Hz | returned-to note Hz | last note Hz |
| velocity | new note velocity | new note velocity | returned-to velocity | last note velocity |
| retrigger | true | false (legato on) / true (legato off) | false (legato on) / true (legato off) | false (undefined) |
| isNoteOn | true | true | true | false |

### MonoMode (FR-002)

```cpp
enum class MonoMode : uint8_t {
    LastNote = 0,   // Most recently pressed key (default)
    LowNote = 1,    // Lowest held key (Minimoog behavior)
    HighNote = 2    // Highest held key (Korg MS-20 behavior)
};
```

### PortaMode (FR-003)

```cpp
enum class PortaMode : uint8_t {
    Always = 0,       // Glide on every note transition (default)
    LegatoOnly = 1    // Glide only on overlapping notes
};
```

### NoteEntry (Internal)

Internal note stack entry.

```cpp
struct NoteEntry {
    uint8_t note;      // MIDI note number (0-127)
    uint8_t velocity;  // MIDI velocity (1-127, never 0 in stack)
};
// Size: 2 bytes
```

### MonoHandler (FR-004)

Main class. Layer 2 Processor.

```cpp
class MonoHandler {
public:
    // Constants
    static constexpr size_t kMaxStackSize = 16;
    static constexpr float kDefaultSampleRate = 44100.0f;
    static constexpr float kMinPortamentoTimeMs = 0.0f;
    static constexpr float kMaxPortamentoTimeMs = 10000.0f;

    // Initialization (FR-005)
    void prepare(double sampleRate) noexcept;

    // Note events (FR-006 through FR-016)
    [[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept;
    [[nodiscard]] MonoNoteEvent noteOff(int note) noexcept;

    // Portamento (FR-021 through FR-025)
    void setPortamentoTime(float ms) noexcept;
    [[nodiscard]] float processPortamento() noexcept;
    [[nodiscard]] float getCurrentFrequency() const noexcept;

    // Configuration (FR-010, FR-017, FR-027, FR-030)
    void setMode(MonoMode mode) noexcept;
    void setLegato(bool enabled) noexcept;
    void setPortamentoMode(PortaMode mode) noexcept;

    // State queries (FR-026)
    [[nodiscard]] bool hasActiveNote() const noexcept;

    // Reset (FR-029)
    void reset() noexcept;

private:
    // Note stack (insertion order maintained)
    std::array<NoteEntry, kMaxStackSize> stack_{};
    uint8_t stackSize_{0};

    // Configuration
    MonoMode mode_{MonoMode::LastNote};
    PortaMode portaMode_{PortaMode::Always};
    bool legato_{false};

    // Current state
    int8_t activeNote_{-1};       // Currently sounding MIDI note (-1 = none)
    uint8_t activeVelocity_{0};   // Current note's velocity
    float currentFrequency_{0.0f}; // Cached output frequency

    // Portamento engine (LinearRamp in semitone space)
    LinearRamp portamentoRamp_;
    float portamentoTimeMs_{0.0f};
    float sampleRate_{kDefaultSampleRate};

    // Internal helpers
    [[nodiscard]] int8_t findWinner() const noexcept;
    void updatePortamentoTarget(float targetSemitones, bool enableGlide) noexcept;
    [[nodiscard]] static float semitoneToFrequency(float semitones) noexcept;
};
```

## State Machine

### Note Lifecycle

```
                   noteOn (first note)
    [No Notes] -----------------------> [Notes Held]
        ^                                    |
        |          noteOff (last note)       |
        +------------------------------------+
        |                                    |
        |          noteOn (additional)       |
        |     [Notes Held] <--+----------+  |
        |         |           |              |
        |         +--- noteOff (not last) ---+
        |
        +--- reset()
```

### Portamento State

```
    noteOn/noteOff triggers new target
            |
            v
    [Calculate target semitones]
            |
      portamentoTimeMs > 0      portamentoTimeMs == 0
      AND glide enabled?         OR glide disabled?
            |                         |
            v                         v
    [LinearRamp.setTarget()]    [LinearRamp.snapTo()]
            |                         |
            v                         v
    [Gliding: process() per     [Instant: process()
     sample returns current      returns target
     interpolated value]         immediately]
```

### Priority Re-evaluation (setMode)

```
    setMode(newMode)
        |
        v
    stackSize_ > 0?
        |
      YES                  NO
        |                    |
        v                    v
    findWinner()           return (no-op)
        |
        v
    winner != activeNote_?
        |
      YES                  NO
        |                    |
        v                    v
    updatePortamento       return (no-op)
    Target to new winner
```

## Relationships

```
MonoHandler
    |
    +-- has-a --> std::array<NoteEntry, 16>   (note stack)
    |
    +-- has-a --> LinearRamp                   (portamento, from Layer 1 smoother.h)
    |
    +-- uses --> midiNoteToFrequency()         (Layer 0, midi_utils.h)
    |
    +-- uses --> semitonesToRatio()             (Layer 0, pitch_utils.h)
    |
    +-- uses --> kA4FrequencyHz, kA4MidiNote   (Layer 0, midi_utils.h)
    |
    +-- uses --> detail::isNaN(), isInf()      (Layer 0, db_utils.h)
    |
    +-- returns --> MonoNoteEvent              (aggregate, same header)
    |
    +-- configured by --> MonoMode enum        (same header)
    |
    +-- configured by --> PortaMode enum       (same header)
```

## Validation Rules

| Field/Parameter | Constraint | Enforcement |
|-----------------|-----------|-------------|
| MIDI note (noteOn/noteOff) | [0, 127] | Early return no-op (FR-004a) |
| Velocity (noteOn) | 0 = noteOff (FR-014), [1, 127] valid | Redirect to noteOff for 0 |
| Portamento time | [0.0, 10000.0] ms | Clamped (FR-021) |
| Note stack size | [0, 16] | Oldest dropped when full (FR-015) |
| MonoMode | enum class | Type-safe, no validation needed |
| PortaMode | enum class | Type-safe, no validation needed |
| Legato | bool | No validation needed |
| Sample rate | > 0 | Use default 44100 if not prepared (FR-005) |

## Memory Layout Estimate

| Member | Size (bytes) |
|--------|-------------|
| stack_ (16 x NoteEntry) | 32 |
| stackSize_ | 1 |
| mode_ | 1 |
| portaMode_ | 1 |
| legato_ | 1 |
| activeNote_ | 1 |
| activeVelocity_ | 1 |
| padding | 2 |
| currentFrequency_ | 4 |
| portamentoRamp_ (LinearRamp) | 20 |
| portamentoTimeMs_ | 4 |
| sampleRate_ | 4 |
| **Total** | **~72 bytes** |

Well under the 512-byte limit (SC-012).
