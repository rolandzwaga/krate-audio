# Data Model: Arpeggiator Engine Integration (071)

**Date**: 2026-02-21
**Spec**: `specs/071-arp-engine-integration/spec.md`

## Entities

### ArpeggiatorParams (NEW -- `plugins/ruinae/src/parameters/arpeggiator_params.h`)

Atomic parameter storage struct. Thread-safe bridge between UI/host thread and audio thread.

```cpp
namespace Ruinae {

struct ArpeggiatorParams {
    std::atomic<bool>  enabled{false};
    std::atomic<int>   mode{0};              // ArpMode enum: 0=Up..9=Chord
    std::atomic<int>   octaveRange{1};       // 1-4
    std::atomic<int>   octaveMode{0};        // OctaveMode enum: 0=Sequential, 1=Interleaved
    std::atomic<bool>  tempoSync{true};
    std::atomic<int>   noteValue{Parameters::kNoteValueDefaultIndex}; // dropdown index 0-20, default 10 (1/8)
    std::atomic<float> freeRate{4.0f};       // 0.5-50 Hz
    std::atomic<float> gateLength{80.0f};    // 1-200 percent
    std::atomic<float> swing{0.0f};          // 0-75 percent
    std::atomic<int>   latchMode{0};         // LatchMode enum: 0=Off, 1=Hold, 2=Add
    std::atomic<int>   retrigger{0};         // ArpRetriggerMode enum: 0=Off, 1=Note, 2=Beat
};

} // namespace Ruinae
```

**Validation Rules**:
- `mode`: 0-9 (clamped)
- `octaveRange`: 1-4 (clamped)
- `octaveMode`: 0-1 (clamped)
- `noteValue`: 0-20 (clamped, `kNoteValueDropdownCount - 1`)
- `freeRate`: 0.5-50.0 Hz (clamped)
- `gateLength`: 1.0-200.0% (clamped)
- `swing`: 0.0-75.0% (clamped)
- `latchMode`: 0-2 (clamped)
- `retrigger`: 0-2 (clamped)

**Default State**: Disabled, Up mode, 1 octave, Sequential, Tempo Sync on, 1/8 note, 4 Hz free rate, 80% gate, 0% swing, Latch Off, Retrigger Off.

---

### Parameter IDs (EXTEND -- `plugins/ruinae/src/plugin_ids.h`)

```cpp
// Arpeggiator (11 parameters, range 3000-3099)
kArpBaseId = 3000,
kArpEnabledId = 3000,      // on/off toggle
kArpModeId = 3001,          // 10-entry StringListParameter
kArpOctaveRangeId = 3002,   // RangeParameter 1-4
kArpOctaveModeId = 3003,    // 2-entry StringListParameter
kArpTempoSyncId = 3004,     // on/off toggle
kArpNoteValueId = 3005,     // 21-entry StringListParameter (reuse note_value_ui.h)
kArpFreeRateId = 3006,      // continuous 0.5-50 Hz
kArpGateLengthId = 3007,    // continuous 1-200%
kArpSwingId = 3008,         // continuous 0-75%
kArpLatchModeId = 3009,     // 3-entry StringListParameter
kArpRetriggerId = 3010,     // 3-entry StringListParameter
kArpEndId = 3099,

kNumParameters = 3100,      // bumped from 2900
```

---

### Processor Fields (EXTEND -- `plugins/ruinae/src/processor/processor.h`)

```cpp
// Arpeggiator
ArpeggiatorParams arpParams_;
Krate::DSP::ArpeggiatorCore arpCore_;
std::array<Krate::DSP::ArpEvent, 128> arpEvents_{};
bool wasTransportPlaying_{false};  // for transport stop detection
```

---

### Controller Fields (EXTEND -- `plugins/ruinae/src/controller/controller.h`)

```cpp
// Arpeggiator sync/rate visibility groups
VSTGUI::CViewContainer* arpRateGroup_{nullptr};
VSTGUI::CViewContainer* arpNoteValueGroup_{nullptr};
```

---

## Relationships

```
Processor
  |-- has-a --> ArpeggiatorParams (atomic param storage)
  |-- has-a --> ArpeggiatorCore (DSP processor from spec 070)
  |-- has-a --> ArpEvent[128] (pre-allocated output buffer)
  |-- uses  --> engine_.noteOn()/noteOff() (routes arp events to synth)

ArpeggiatorCore
  |-- has-a --> HeldNoteBuffer (internal, from spec 069)
  |-- has-a --> NoteSelector (internal, from spec 069)
  |-- produces --> ArpEvent[] (note-on/note-off with sample offsets)

Controller
  |-- registers --> 11 arp parameters
  |-- formats  --> arp parameter display strings
  |-- toggles  --> arpRateGroup_ / arpNoteValueGroup_ visibility
```

---

## State Transitions

### Arp Enable/Disable

```
DISABLED --[user enables]--> ENABLED
  - Begin routing MIDI to arpCore_ instead of engine_

ENABLED --[user disables]--> DISABLED
  - setEnabled(false) queues cleanup note-offs
  - Next processBlock() emits note-offs through normal routing loop
  - Resume routing MIDI directly to engine_
```

### Transport Stop/Start

```
PLAYING --[transport stops]--> STOPPED
  - arpCore_.reset() resets timing + emits note-offs
  - held-note/latch buffer PRESERVED

STOPPED --[transport starts]--> PLAYING
  - arp resumes from step 1 with preserved notes
```

---

## Serialization Format

Appended after harmonizer enable flag in state stream:

```
saveArpParams():
  writeInt32(enabled ? 1 : 0)
  writeInt32(mode)
  writeInt32(octaveRange)
  writeInt32(octaveMode)
  writeInt32(tempoSync ? 1 : 0)
  writeInt32(noteValue)
  writeFloat(freeRate)
  writeFloat(gateLength)
  writeFloat(swing)
  writeInt32(latchMode)
  writeInt32(retrigger)
```

Total: 7 x int32 (28 bytes) + 3 x float (12 bytes) = 40 bytes appended.

Total serialized size: 40 bytes appended to the state stream.
