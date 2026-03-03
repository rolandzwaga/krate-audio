# Quickstart: Basic Synth Voice (037)

## What This Feature Does

SynthVoice is a complete single-voice subtractive synthesis unit. It composes two oscillators, one multimode filter, and two ADSR envelopes into a self-contained voice suitable for polyphonic use. Signal flow: oscillators -> mix -> filter -> amplitude envelope -> output.

## Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `dsp/include/krate/dsp/core/pitch_utils.h` | Modify | Add `frequencyToMidiNote()` utility |
| `dsp/include/krate/dsp/systems/synth_voice.h` | Create | SynthVoice class (header-only) |
| `dsp/tests/unit/systems/synth_voice_test.cpp` | Create | Test suite for all 32 FRs and 10 SCs |
| `dsp/CMakeLists.txt` | Modify | Add synth_voice.h to KRATE_DSP_SYSTEMS_HEADERS |
| `dsp/tests/CMakeLists.txt` | Modify | Add synth_voice_test.cpp to dsp_tests |
| `dsp/lint_all_headers.cpp` | Modify | Add `#include <krate/dsp/systems/synth_voice.h>` |
| `specs/_architecture_/layer-3-systems.md` | Modify | Add SynthVoice to architecture docs |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all SynthVoice tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[synth-voice]"

# Run specific test sections
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "SynthVoice*"
```

## Key Implementation Details

### 1. Layer 0 Utility Addition (pitch_utils.h)

Add `frequencyToMidiNote(float hz)` function that returns continuous MIDI note number:
```cpp
float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;
```

### 2. SynthVoice Header Structure

The class follows the FMVoice pattern (Layer 3 system). Key differences:
- Uses PolyBlepOscillator instead of FMOperator
- Has built-in filter with envelope modulation
- Has dual ADSR envelopes instead of external envelope management
- Has velocity mapping

### 3. Process Loop (Per-Sample)

```
1. Generate osc1 sample at noteFrequency
2. Generate osc2 sample at noteFrequency * 2^octave * 2^(cents/1200)
3. Mix: sample = (1 - mix) * osc1 + mix * osc2
4. Compute effective cutoff from base + envelope + key tracking
5. Set SVF cutoff, process filter
6. Process amplitude envelope
7. Return filtered * ampEnvLevel
```

### 4. Filter Cutoff Modulation (Per-Sample)

```
filterEnvLevel = filterEnv_.process()
effectiveEnvAmount = envAmount * (1 - velToFilterEnv + velToFilterEnv * velocity)
keyTrackSemitones = keyTrackAmount * (frequencyToMidiNote(noteFrequency) - 60.0)
totalSemitones = effectiveEnvAmount * filterEnvLevel + keyTrackSemitones
effectiveCutoff = clamp(baseCutoff * 2^(totalSemitones / 12), 20, sr * 0.495)
```

### 5. Retrigger Behavior

On `noteOn()` while already active:
- Call `setVelocity()` with new velocity on amp envelope
- Call `gate(true)` on both envelopes (Hard mode attacks from current level)
- Update oscillator frequencies (no phase reset)

### 6. Default Parameter Values

| Parameter | Default | Source |
|-----------|---------|--------|
| Osc1 Waveform | Sawtooth | FR-009 |
| Osc2 Waveform | Sawtooth | FR-009 |
| Osc Mix | 0.5 | FR-010 |
| Osc2 Detune | 0.0 cents | FR-011 |
| Osc2 Octave | 0 | FR-012 |
| Filter Type | Lowpass | FR-014 |
| Filter Cutoff | 1000 Hz | FR-015 |
| Filter Resonance | 0.707 (Butterworth) | FR-016 |
| Filter Env Amount | 0.0 semitones | FR-017 |
| Filter Key Track | 0.0 | FR-020 |
| Amp Envelope | A=10ms, D=50ms, S=1.0, R=100ms | FR-023 |
| Filter Envelope | A=10ms, D=200ms, S=0.0, R=100ms | FR-023 |
| Vel->Filter Env | 0.0 | FR-027 |
