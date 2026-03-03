# Quickstart: Polyphonic Synth Engine

**Feature Branch**: `038-polyphonic-synth-engine` | **Date**: 2026-02-07

## Pre-requisites

### Existing Components (all implemented and merged)

- `SynthVoice` at `dsp/include/krate/dsp/systems/synth_voice.h`
- `VoiceAllocator` at `dsp/include/krate/dsp/systems/voice_allocator.h`
- `MonoHandler` at `dsp/include/krate/dsp/processors/mono_handler.h`
- `NoteProcessor` at `dsp/include/krate/dsp/processors/note_processor.h`
- `SVF` at `dsp/include/krate/dsp/primitives/svf.h`
- `Sigmoid::tanh()` at `dsp/include/krate/dsp/core/sigmoid.h`

### Modification to Existing Code

Before implementing PolySynthEngine, add `setFrequency(float hz)` to `SynthVoice`:

```cpp
// In synth_voice.h, after the noteOff() method:
void setFrequency(float hz) noexcept {
    if (detail::isNaN(hz) || detail::isInf(hz)) return;
    noteFrequency_ = (hz < 0.0f) ? 0.0f : hz;
    osc1_.setFrequency(noteFrequency_);
    updateOsc2Frequency();
}
```

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/systems/poly_synth_engine.h` | Header (+ implementation, header-only) |
| `dsp/tests/unit/systems/poly_synth_engine_test.cpp` | Unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/systems/synth_voice.h` | Add `setFrequency()` method |
| `dsp/tests/CMakeLists.txt` | Add test file to build |

## Build Commands

```bash
# Configure (if not already done)
"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build DSP tests
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PolySynthEngine*"
```

## Usage Example

```cpp
#include <krate/dsp/systems/poly_synth_engine.h>

Krate::DSP::PolySynthEngine engine;

// Initialize
engine.prepare(44100.0, 512);

// Configure sound
engine.setOsc1Waveform(Krate::DSP::OscWaveform::Sawtooth);
engine.setFilterCutoff(2000.0f);
engine.setAmpRelease(200.0f);

// Play a chord
engine.noteOn(60, 100);  // C4
engine.noteOn(64, 100);  // E4
engine.noteOn(67, 100);  // G4

// Process audio
float output[512];
engine.processBlock(output, 512);

// Release chord
engine.noteOff(60);
engine.noteOff(64);
engine.noteOff(67);

// Continue processing for release tails
engine.processBlock(output, 512);
```

## Test Strategy Summary

### Test Groups

1. **Construction and Defaults** -- verify constants, default mode, default filter state
2. **Poly Mode Note Dispatch** -- noteOn/noteOff, voice stealing, active voice count
3. **Mono Mode** -- legato, retrigger, portamento, note priority
4. **Mode Switching** -- poly->mono, mono->poly, same-mode no-op
5. **Parameter Forwarding** -- verify all 16 voices receive parameter changes
6. **Global Filter** -- enable/disable, LP/HP/BP/Notch, bypass when disabled
7. **Master Output** -- gain compensation, soft limiting, extreme inputs
8. **Voice Lifecycle** -- deferred voiceFinished, mid-block release
9. **Edge Cases** -- velocity 0, prepare while playing, sample rate change
10. **Performance** -- CPU benchmark (SC-001), memory footprint (SC-010)
11. **Multi-Sample-Rate** -- verify operation at 44100, 48000, 88200, 96000, 176400, 192000
