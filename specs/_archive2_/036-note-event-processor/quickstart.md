# Quickstart: Note Event Processor (036)

## What This Component Does

NoteProcessor converts MIDI note numbers to oscillator frequencies with:
- Configurable A4 tuning reference (400-480 Hz)
- Smoothed pitch bend with configurable range (0-24 semitones)
- Four velocity curve types (Linear, Soft, Hard, Fixed)
- Multi-destination velocity routing (amplitude, filter, envelope time)

## Usage Example

```cpp
#include <krate/dsp/processors/note_processor.h>

using namespace Krate::DSP;

NoteProcessor noteProc;

// --- Setup (called once or on sample rate change) ---
noteProc.prepare(44100.0);
noteProc.setTuningReference(442.0f);   // European orchestral tuning
noteProc.setPitchBendRange(2.0f);      // +/- 2 semitones (standard)
noteProc.setVelocityCurve(VelocityCurve::Soft);
noteProc.setAmplitudeVelocityDepth(1.0f);
noteProc.setFilterVelocityDepth(0.5f);

// --- Per note-on ---
int velocity = 100;
VelocityOutput velOut = noteProc.mapVelocity(velocity);
// velOut.amplitude  = sqrt(100/127) * 1.0  = ~0.887
// velOut.filter     = sqrt(100/127) * 0.5  = ~0.444
// velOut.envelopeTime = sqrt(100/127) * 0.0 = 0.0

// --- Per audio block (once, shared by all voices) ---
noteProc.setPitchBend(0.5f);          // MIDI pitch bend received
float smoothedBend = noteProc.processPitchBend();  // Advance smoother

// --- Per voice (call for each active voice's note) ---
float freqA4 = noteProc.getFrequency(69);   // ~466 Hz (A4 + 1 semitone bend)
float freqC4 = noteProc.getFrequency(60);   // ~277 Hz (C4 + 1 semitone bend)
```

## Polyphonic Integration Pattern

```cpp
// In your synth engine's processBlock():
void processBlock(float** outputs, size_t numSamples) {
    // 1. Handle MIDI events
    for (auto& event : midiEvents) {
        if (event.isPitchBend()) {
            float bipolar = (event.value - 8192) / 8192.0f;
            noteProc_.setPitchBend(bipolar);
        }
    }

    // 2. Advance pitch bend smoother ONCE per block
    noteProc_.processPitchBend();

    // 3. For each active voice, get its frequency
    for (auto& voice : activeVoices_) {
        float freq = noteProc_.getFrequency(voice.midiNote);
        voice.oscillator.setFrequency(freq);
    }

    // 4. Process audio...
}
```

## Files to Create/Modify

| File | Action |
|------|--------|
| `dsp/include/krate/dsp/core/midi_utils.h` | Add `VelocityCurve` enum and `mapVelocity()` function |
| `dsp/include/krate/dsp/processors/note_processor.h` | New file: NoteProcessor class |
| `dsp/tests/unit/processors/note_processor_test.cpp` | New file: Unit tests |
| `dsp/tests/CMakeLists.txt` | Add test file to dsp_tests target |
| `dsp/CMakeLists.txt` | Add header to KRATE_DSP_PROCESSORS_HEADERS |
| `specs/_architecture_/layer-2-processors.md` | Add NoteProcessor documentation |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all NoteProcessor tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[note_processor]"

# Run specific test sections
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[us1]"  # Frequency conversion
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[us2]"  # Pitch bend
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[us3]"  # Velocity curves
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[us4]"  # Multi-destination velocity
```

## Key Design Decisions

1. **VelocityCurve enum in Layer 0** (`midi_utils.h`): Reusable by VoiceAllocator and other consumers.
2. **mapVelocity() as free function in Layer 0**: Pure, stateless, reusable. NoteProcessor delegates to it.
3. **Pre-cached bend ratio**: `processPitchBend()` computes and caches `semitonesToRatio(bendSemitones)` so `getFrequency()` avoids a `std::pow()` call per voice.
4. **NaN/Inf guarding before smoother**: NoteProcessor validates pitch bend input before calling `OnePoleSmoother::setTarget()` to achieve the "ignore invalid" semantics (FR-020).
