# API Contract: MonoHandler

**Header**: `dsp/include/krate/dsp/processors/mono_handler.h`
**Namespace**: `Krate::DSP`
**Layer**: 2 (Processors)
**Test File**: `dsp/tests/unit/processors/mono_handler_test.cpp`

---

## Public Types

### MonoNoteEvent (FR-001)

```cpp
/// Lightweight event descriptor returned by MonoHandler.
/// Simple aggregate with no user-declared constructors.
struct MonoNoteEvent {
    float frequency;    ///< Frequency in Hz (12-TET, A4=440Hz)
    uint8_t velocity;   ///< MIDI velocity (0-127)
    bool retrigger;     ///< true = caller should restart envelopes
    bool isNoteOn;      ///< true = note active, false = all notes released
};
```

**Invariants:**
- `frequency` is always > 0 when `isNoteOn == true`
- `velocity` is always in [0, 127]
- `retrigger` is undefined (may be false) when `isNoteOn == false`

### MonoMode (FR-002)

```cpp
enum class MonoMode : uint8_t {
    LastNote = 0,   ///< Most recently pressed key takes priority (default)
    LowNote = 1,    ///< Lowest held key takes priority
    HighNote = 2    ///< Highest held key takes priority
};
```

### PortaMode (FR-003)

```cpp
enum class PortaMode : uint8_t {
    Always = 0,       ///< Portamento on every note transition (default)
    LegatoOnly = 1    ///< Portamento only on overlapping notes
};
```

---

## MonoHandler Class (FR-004)

### Construction

```cpp
/// Default constructor. Pre-allocates all internal state.
/// No heap allocation after construction (FR-004).
/// Default state: LastNote mode, portamento off (0ms), legato disabled,
/// PortaMode::Always, sample rate 44100 Hz.
MonoHandler() noexcept;
```

**Post-conditions:**
- `hasActiveNote() == false`
- `getCurrentFrequency() == 0.0f`
- Mode is `MonoMode::LastNote`
- Portamento time is 0.0ms
- Portamento mode is `PortaMode::Always`
- Legato is disabled

### Initialization

```cpp
/// Configure for given sample rate. Recalculates portamento coefficients.
/// Preserves current glide position if mid-glide (FR-005).
/// Safe to call multiple times. Uses 44100 Hz as default if never called.
/// @param sampleRate Sample rate in Hz (must be > 0)
void prepare(double sampleRate) noexcept;
```

### Note Events

```cpp
/// Process a MIDI note-on event.
///
/// @param note MIDI note number (0-127). Values outside range are ignored (FR-004a).
/// @param velocity MIDI velocity (0-127). Velocity 0 treated as noteOff (FR-014).
/// @return MonoNoteEvent describing the result.
///
/// Behavior depends on:
/// - MonoMode: which note wins when multiple are held (FR-006 through FR-009)
/// - Legato: whether retrigger is suppressed for overlapping notes (FR-017, FR-019)
/// - PortaMode: whether portamento glide is engaged (FR-027, FR-028)
/// - Same-note re-press: updates velocity, moves to top in LastNote (FR-016)
/// - Stack full: oldest entry dropped (FR-015)
///
/// @note Real-time safe, noexcept (FR-031, FR-032)
[[nodiscard]] MonoNoteEvent noteOn(int note, int velocity) noexcept;

/// Process a MIDI note-off event.
///
/// @param note MIDI note number (0-127). Values outside range are ignored (FR-004a).
/// @return MonoNoteEvent describing the result.
///
/// If released note is the active note: selects next winner from stack (FR-012).
/// If released note is a background note: removes from stack, no output change (FR-013).
/// If released note is not in stack: returns current state unchanged.
/// If last note released: returns isNoteOn=false (FR-012).
///
/// @note Real-time safe, noexcept (FR-031, FR-032)
[[nodiscard]] MonoNoteEvent noteOff(int note) noexcept;
```

### Portamento

```cpp
/// Set portamento glide duration. 0.0 = instantaneous. Clamped to [0, 10000] ms.
/// @param ms Glide time in milliseconds (FR-021)
void setPortamentoTime(float ms) noexcept;

/// Advance portamento by one sample and return current gliding frequency.
/// Must be called once per audio sample (FR-023).
/// Returns target frequency when no glide is active or glide is complete.
/// @return Current frequency in Hz
/// @note Real-time safe, noexcept (FR-031, FR-032)
[[nodiscard]] float processPortamento() noexcept;

/// Get current portamento output frequency without advancing state (FR-025).
/// @return Current frequency in Hz
[[nodiscard]] float getCurrentFrequency() const noexcept;
```

### Configuration

```cpp
/// Set note priority mode. Re-evaluates winner if notes are held (FR-010).
/// @param mode Priority algorithm
void setMode(MonoMode mode) noexcept;

/// Enable/disable legato mode (FR-017).
/// @param enabled true = suppress retrigger for overlapping notes
void setLegato(bool enabled) noexcept;

/// Set portamento activation mode (FR-027).
/// @param mode Always or LegatoOnly
void setPortamentoMode(PortaMode mode) noexcept;
```

### State Queries

```cpp
/// Returns true if at least one note is held (FR-026).
[[nodiscard]] bool hasActiveNote() const noexcept;
```

### Reset

```cpp
/// Clear all state: note stack, portamento, active note (FR-029).
/// hasActiveNote() returns false after reset.
void reset() noexcept;
```

---

## Thread Safety Contract (FR-033-threading)

All methods are designed for **single audio thread** usage. The caller MUST ensure:
- All method calls occur on the same thread
- No concurrent access from multiple threads
- No synchronization primitives are used internally

---

## Dependencies (FR-033 Layer Compliance)

| Dependency | Layer | Header | Usage |
|-----------|-------|--------|-------|
| midiNoteToFrequency() | 0 | `<krate/dsp/core/midi_utils.h>` | Note-to-frequency conversion |
| kA4FrequencyHz | 0 | `<krate/dsp/core/midi_utils.h>` | A4 reference frequency |
| kA4MidiNote | 0 | `<krate/dsp/core/midi_utils.h>` | A4 MIDI note number |
| semitonesToRatio() | 0 | `<krate/dsp/core/pitch_utils.h>` | Semitone-to-ratio for portamento |
| detail::isNaN() | 0 | `<krate/dsp/core/db_utils.h>` | NaN guard on setters |
| detail::isInf() | 0 | `<krate/dsp/core/db_utils.h>` | Inf guard on setters |
| LinearRamp | 1 | `<krate/dsp/primitives/smoother.h>` | Portamento interpolation |

No Layer 3 or Layer 4 dependencies.

---

## Usage Example

```cpp
#include <krate/dsp/processors/mono_handler.h>

using namespace Krate::DSP;

MonoHandler mono;
mono.prepare(44100.0);
mono.setLegato(true);
mono.setPortamentoTime(100.0f);  // 100ms glide
mono.setMode(MonoMode::LastNote);

// Note on: C4
auto event = mono.noteOn(60, 100);
// event.frequency ~= 261.63 Hz, retrigger = true, isNoteOn = true

// Per-sample processing in audio callback
for (size_t i = 0; i < blockSize; ++i) {
    float freq = mono.processPortamento();
    oscillator.setFrequency(freq);
    output[i] = oscillator.process();
}

// Overlapping note: E4
event = mono.noteOn(64, 80);
// event.frequency ~= 329.63 Hz, retrigger = false (legato), isNoteOn = true
// processPortamento() now glides from C4 to E4 over 100ms

// Release E4, return to C4
event = mono.noteOff(64);
// event.frequency ~= 261.63 Hz, retrigger = false (legato), isNoteOn = true
// processPortamento() glides back to C4
```
