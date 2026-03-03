# Quickstart: Trance Gate (039)

## What This Is

TranceGate is a Layer 2 DSP processor that applies a repeating step pattern as multiplicative gain to an audio signal. It produces rhythmic gating effects with click-free transitions, Euclidean pattern generation, and per-voice or global clock modes. It sits post-distortion and pre-VCA in the Ruinae synth voice chain.

## File Locations

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/trance_gate.h` | Header (header-only implementation) |
| `dsp/tests/unit/processors/trance_gate_test.cpp` | Unit tests |
| `dsp/tests/CMakeLists.txt` | Add test source registration |

## Dependencies (All Existing)

| Component | Header | Layer |
|-----------|--------|-------|
| `EuclideanPattern` | `<krate/dsp/core/euclidean_pattern.h>` | 0 |
| `NoteValue`, `NoteModifier`, `getBeatsForNote()` | `<krate/dsp/core/note_value.h>` | 0 |
| `OnePoleSmoother`, `calculateOnePolCoefficient()` | `<krate/dsp/primitives/smoother.h>` | 1 |

## Minimal Usage

```cpp
#include <krate/dsp/processors/trance_gate.h>
using namespace Krate::DSP;

// Create and prepare
TranceGate gate;
gate.prepare(44100.0);
gate.setTempo(120.0);

// Configure parameters
TranceGateParams params;
params.numSteps = 16;
params.noteValue = NoteValue::Sixteenth;
params.depth = 1.0f;
params.attackMs = 2.0f;
params.releaseMs = 10.0f;
params.tempoSync = true;
params.perVoice = true;
gate.setParams(params);

// Set pattern: alternating on/off
for (int i = 0; i < 16; ++i)
    gate.setStep(i, (i % 2 == 0) ? 1.0f : 0.0f);

// Or use Euclidean: E(5, 16) = 5 hits in 16 steps
gate.setEuclidean(5, 16);

// Process audio (single sample)
float output = gate.process(input);

// Process audio (mono block, in-place)
gate.processBlock(buffer, numSamples);

// Process audio (stereo block, in-place)
gate.processBlock(left, right, numSamples);

// Read gate value for modulation routing
float modValue = gate.getGateValue();
int stepIndex = gate.getCurrentStep();
```

## Key Formulas

**Step duration (tempo sync)**:
```
beatsPerNote = getBeatsForNote(noteValue, noteModifier)
samplesPerStep = (60.0 / bpm) * beatsPerNote * sampleRate
```

**Step duration (free-run)**:
```
samplesPerStep = sampleRate / rateHz
```

**Gain computation**:
```
smoothedGain = onePoleSmoother.process()  // attack or release smoother
finalGain = lerp(1.0, smoothedGain, depth) = 1.0 + (smoothedGain - 1.0) * depth
output = input * finalGain
```

**Smoother coefficient**:
```
coeff = exp(-5000.0 / (timeMs * sampleRate))
```

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run TranceGate tests specifically
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "TranceGate*"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

## Test Strategy Overview

Tests organized by user story:

1. **Pattern-driven gating** (US-1): Alternating pattern, ghost notes, all-open transparency, individual step set
2. **Click-free edges** (US-2): Max sample-to-sample gain change within one-pole bounds, minimum ramp time
3. **Euclidean patterns** (US-3): Known reference outputs (tresillo, cinquillo), rotation, edge cases
4. **Depth control** (US-4): Bypass at 0.0, full at 1.0, linear interpolation at 0.5
5. **Tempo sync** (US-5): Step duration accuracy, tempo change, free-run mode
6. **Modulation output** (US-6): getGateValue() matches applied gain
7. **Per-voice/global** (US-7): Reset behavior in both modes
