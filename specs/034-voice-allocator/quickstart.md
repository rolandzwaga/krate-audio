# Quickstart: Voice Allocator

**Feature Branch**: `034-voice-allocator`
**Date**: 2026-02-07

## What is it?

The VoiceAllocator is a polyphonic voice management system that maps MIDI note events to voice slot indices. It does NOT contain any DSP processing -- it only produces `VoiceEvent` instructions that tell the caller which voice to start, stop, or steal.

## Files

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/systems/voice_allocator.h` | Header-only implementation |
| `dsp/tests/unit/systems/voice_allocator_test.cpp` | Unit tests |

## Basic Usage

```cpp
#include <krate/dsp/systems/voice_allocator.h>

using namespace Krate::DSP;

// Create allocator with defaults: 8 voices, Oldest mode, Hard steal
VoiceAllocator allocator;

// Process a note-on
auto events = allocator.noteOn(60, 100);  // C4, velocity 100
for (const auto& event : events) {
    // event.type == VoiceEvent::Type::NoteOn
    // event.voiceIndex -- which voice to start
    // event.note == 60
    // event.velocity == 100
    // event.frequency -- ~261.63 Hz (pre-computed)
}

// Process a note-off
auto offEvents = allocator.noteOff(60);
for (const auto& event : offEvents) {
    // event.type == VoiceEvent::Type::NoteOff
    // event.voiceIndex -- which voice to release
}

// When voice finishes its envelope release
allocator.voiceFinished(voiceIndex);  // Returns voice to idle pool
```

## Configuration

```cpp
// Allocation mode (selects which voice to steal when pool is full)
allocator.setAllocationMode(AllocationMode::RoundRobin);

// Steal mode (how stolen voices behave)
allocator.setStealMode(StealMode::Soft);

// Voice count (1-32)
auto releaseEvents = allocator.setVoiceCount(16);  // May release excess voices

// Unison mode (1-8 voices per note)
allocator.setUnisonCount(3);        // 3 voices per note
allocator.setUnisonDetune(0.5f);    // 25 cents max spread

// Pitch bend (semitones, updates all active voices)
allocator.setPitchBend(2.0f);  // Bend up 2 semitones

// Tuning (A4 reference, updates all active voices)
allocator.setTuningReference(432.0f);
```

## Thread-Safe Queries (for UI)

```cpp
// These are safe to call from any thread
int note = allocator.getVoiceNote(3);          // -1 if idle
VoiceState state = allocator.getVoiceState(3); // Idle/Active/Releasing
bool active = allocator.isVoiceActive(3);      // true if Active or Releasing
uint32_t count = allocator.getActiveVoiceCount();
```

## Integration with Synth Engine (Future)

```cpp
// Typical usage in a polyphonic synth engine
class PolyphonicEngine {
    VoiceAllocator allocator_;
    std::array<SynthVoice, 32> voices_;

    void handleNoteOn(uint8_t note, uint8_t velocity) {
        auto events = allocator_.noteOn(note, velocity);
        for (const auto& event : events) {
            switch (event.type) {
                case VoiceEvent::Type::NoteOn:
                    voices_[event.voiceIndex].start(event.frequency, event.velocity);
                    break;
                case VoiceEvent::Type::Steal:
                    voices_[event.voiceIndex].hardReset();
                    break;
                case VoiceEvent::Type::NoteOff:
                    voices_[event.voiceIndex].release();
                    break;
            }
        }
    }

    void handleNoteOff(uint8_t note) {
        auto events = allocator_.noteOff(note);
        for (const auto& event : events) {
            voices_[event.voiceIndex].release();
        }
    }

    void handleVoiceFinished(size_t voiceIndex) {
        allocator_.voiceFinished(voiceIndex);
    }
};
```

## Key Behaviors

1. **Same-note retrigger**: Playing the same note twice reuses the voice (no double allocation)
2. **Velocity 0 = noteOff**: MIDI convention, handled automatically
3. **Releasing voices preferred for stealing**: Less audible interruption
4. **Unison groups stolen as whole**: All N voices of a note are stolen together
5. **Event buffer lifetime**: The returned span is valid until the next noteOn/noteOff/setVoiceCount call

## Build and Test

```bash
# Build (Windows)
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[VoiceAllocator]"
```
